#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "internal.hpp"
#include "on_disk_structures.hpp"

namespace bleb {

template <typename T> T min(T a, T b) {
    return (a < b) ? a : b;
}

template <typename T> T roundUpBlockLength(T length) {
    T roundUpTo;

    // 0-255:       round to 32
    // 256-4k:      round to 256
    // 4k-128k:     round to 4k
    // 128k+:       round to 16k

    if (length < 256)
        roundUpTo = 32;
    else if (length < 4 * 1024)
        roundUpTo = 256;
    else if (length < 128 * 1024)
        roundUpTo = 4096;
    else
        roundUpTo = 16 * 1024;

    return align(length, roundUpTo);
}

class RepositoryStream /*: IOStream*/ {
    Repository* repo;
    ByteIO* io;
    bool isReadOnly = false;

    uint64_t descrLocation;
    StreamDescriptor_t descr;
    bool descrDirty;

    uint64_t pos;

    bool haveCurrentSpan;
    SpanHeader_t firstSpan, currentSpan;
    uint64_t currentSpanLocation, currentSpanPosInStream, posInCurrentSpan;

public:
    RepositoryStream(Repository* repo, uint64_t streamDescriptorLocation) {
        this->repo = repo;
        this->io = repo->io;
        this->descrLocation = streamDescriptorLocation;

        descrDirty = false;
        haveCurrentSpan = false;

        retrieveStruct(io, descrLocation, descr);

        if (descr.location != 0) {
            retrieveStruct(io, descr.location, firstSpan);
            setCurrentSpan(firstSpan, descr.location, 0);
        }
    }

    RepositoryStream(Repository* repo, uint64_t streamDescriptorLocation, uint32_t reserveLength,
            uint64_t expectedSize) {
        this->repo = repo;
        this->io = repo->io;
        this->descrLocation = streamDescriptorLocation;

        descrDirty = false;
        haveCurrentSpan = false;

        // create a new stream
        uint64_t firstSpanLocation;
        assert(repo->allocateSpan(firstSpanLocation, firstSpan, reserveLength)); // FIXME: error handling

        // initialize Stream Descriptor
        descr.location = firstSpanLocation;
        descr.length = 0;
        descrDirty = true;

        setCurrentSpan(firstSpan, firstSpanLocation, 0);
    }

    ~RepositoryStream() {
        if (descrDirty)
            storeStruct(io, descrLocation, descr);
    }

    uint64_t getPos() {
        return pos;
    }

    uint64_t getSize() {
        return descr.length;
    }

    void setPos(uint64_t pos) {
        this->pos = pos;
        //haveCurrentSpan = false;        // FIXME: haveCurrentSpan x currentSpanIsTheOne
        gotoRightSpan();    // FIXME
    }

    size_t read(void* buffer_in, size_t length) {
        if (length == 0)
            return 0;

        uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_in);

        size_t readTotal = 0;

        if (!haveCurrentSpan) {
            printf("goto right for read\n");
            if (!gotoRightSpan())
                return readTotal;
        }

        for (; length > 0;) {
            // start by checking whether we currently are within any span

            const uint64_t remainingBytesInSpan = currentSpan.usedLength - posInCurrentSpan;
printf("%llu = %u - %llu\n", remainingBytesInSpan, currentSpan.usedLength, posInCurrentSpan);
            if (remainingBytesInSpan > 0) {
                const size_t read = min<uint64_t>(remainingBytesInSpan, length);

                if (!io->getBytesAt(currentSpanLocation + SpanHeader_t::SIZE + posInCurrentSpan, buffer, read))
                    return false;

                posInCurrentSpan += read;
                pos += read;
                readTotal += read;

                buffer += read;
                length -= read;
            }

            if (length > 0) {
                // continue in next span
                SpanHeader_t nextSpan;

                uint64_t nextSpanLocation = currentSpan.nextSpanLocation;

                if (nextSpanLocation != 0) {
                    if (!retrieveStruct(io, nextSpanLocation, nextSpan))
                        return readTotal;    // FIXME: errors
                }
                else {
                    assert(false);
                }

                setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
            }
        }

        return readTotal;
    }

    size_t write(const void* buffer_in, size_t length) {
        if (isReadOnly || length == 0)
            return 0;

        const uint8_t* buffer = reinterpret_cast<const uint8_t*>(buffer_in);

        size_t writtenTotal = 0;

        if (!haveCurrentSpan) {
            if (descr.length == 0) {
                // the block is empty; allocate initial span
                uint64_t firstSpanLocation;

                if (!repo->allocateSpan(firstSpanLocation, firstSpan, length))
                    return writtenTotal;    // FIXME: errors

                setCurrentSpan(firstSpan, firstSpanLocation, 0);

                descr.location = firstSpanLocation;
                descrDirty = true;
            }
            else {
                printf("goto right for write\n");
                // seek; will fail if pos > length
                if (!gotoRightSpan())
                    return writtenTotal;    // FIXME: errors
            }
        }

        for (; length > 0;) {
            // start by checking whether we currently are within any span

            const uint64_t remainingBytesInSpan = currentSpan.reservedLength - posInCurrentSpan;

            if (remainingBytesInSpan > 0) {
                const size_t written = min<uint64_t>(remainingBytesInSpan, length);

                if (!io->setBytesAt(currentSpanLocation + SpanHeader_t::SIZE + posInCurrentSpan, buffer, written))
                    return false;

                posInCurrentSpan += written;
                pos += written;
                writtenTotal += written;

                if (pos > descr.length) {
                    descr.length = pos;
                    descrDirty = true;
                }

                ////if (written < (unsigned int) affectedBytesInSpan)
                //    return writtenTotal;

                buffer += written;
                length -= written;

                currentSpan.usedLength = posInCurrentSpan;

                if (!storeStruct(io, currentSpanLocation, currentSpan))
                    return writtenTotal;

                // we cache firstSpan, so it might need to be updated
                if (currentSpanPosInStream == 0)
                    firstSpan = currentSpan;
            }

            if (length > 0) {
                // continue in next span
                SpanHeader_t nextSpan;

                uint64_t nextSpanLocation = currentSpan.nextSpanLocation;

                if (nextSpanLocation != 0) {
                    // next span had been already allocated

                    if (!retrieveStruct(io, nextSpanLocation, nextSpan))
                        return writtenTotal;    // FIXME: errors
                }
                else {
                    // allocate a new span to hold the rest of the data

                    if (!repo->allocateSpan(nextSpanLocation, nextSpan, length))
                        return writtenTotal;

                    // update CURRENT span to point to the NEW span
                    currentSpan.nextSpanLocation = nextSpanLocation;

                    if (!storeStruct(io, currentSpanLocation, currentSpan))
                        return writtenTotal;    // FIXME: errors

                    // we cache firstSpan, so it might need to be updated
                    if (currentSpanPosInStream == 0)
                        firstSpan = currentSpan;
                }

                setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
            }
        }

        return writtenTotal;
    }

private:

    bool gotoRightSpan() {
        // FIXME: this is highly unoptimal right now (and possibly broken)
        // we're always starting from the block beginning and going span-by-span

        if (descr.location == 0)        // we don't actually "exist"
            assert(false);

        if (pos > descr.length)
            return false;

        setCurrentSpan(firstSpan, descr.location, 0);

        while (pos != currentSpanPosInStream) {
            // this should never happen
            if (currentSpanPosInStream > descr.length)
                assert(false);

            // is the 'pos' we're looking for within this span?
            if (pos <= currentSpanPosInStream + currentSpan.reservedLength) {
                posInCurrentSpan = pos - currentSpanPosInStream;
                break;
            }

            SpanHeader_t nextSpan;
            uint64_t nextSpanLocation = currentSpan.nextSpanLocation;

            assert(nextSpanLocation != 0);  // FIXME: errors

            if (!retrieveStruct(io, nextSpanLocation, nextSpan))
                return false;   // FIXME: errors

            setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
        }

        return true;
    }

    void setCurrentSpan(const SpanHeader_t& span, uint64_t spanLocation, uint64_t spanPosInStream) {
        currentSpan = span;

        currentSpanLocation = spanLocation;
        currentSpanPosInStream = spanPosInStream;
        posInCurrentSpan = 0;

        haveCurrentSpan = true;
    }
};

inline bool getBytesAt(RepositoryStream* io, uint64_t pos, uint8_t* buffer, uint64_t count) {
    return io->setPos(pos), io->read(buffer, count) == count;
}

inline bool setBytesAt(RepositoryStream* io, uint64_t pos, const uint8_t* buffer, uint64_t count) {
    return io->setPos(pos), io->write(buffer, count) == count;
}

inline bool clearBytesAt(RepositoryStream* io, uint64_t pos, uint64_t count) {
    io->setPos(pos);

    while (count--)
        if (!io->write("\x00", 1))
            return false;

    return true;
}

class RepositoryDirectory {
    RepositoryStream* directoryStream;

    // create a new directory, storing its descriptor at the specified location
    /*this(Repository repo, uint64_t streamDescriptorLocation, uint64_t expectedSize) {
        directoryStream = new RepositoryStream(repo, streamDescriptorLocation);
    }*/

public:
    RepositoryDirectory(RepositoryStream* directoryStream) {
        this->directoryStream = directoryStream;
    }

    ~RepositoryDirectory() {
        delete directoryStream;
    }

    void setObjectContents1(const char* objectName, const uint8_t* contents, size_t contentsLength,
            unsigned int flags, unsigned int objectFlags) {
        // FIXME: error handling
        // Browse the directory and look for the object already existing
        // Also mark any nice usable spot to put it in

        const size_t objectNameLength = strlen(objectName);
        const uint16_t prologueLength = objectEntryPrologueLength(objectNameLength);

        bool useInlinePayload = false;

        if (flags & PREFER_INLINE_PAYLOAD) {
            if (prologueLength + contentsLength < ObjectEntryPrologueHeader_t::LENGTH_MASK)
                useInlinePayload = true;
        }

        uint16_t objectEntryLength;

        if (!useInlinePayload) {
            objectFlags |= ObjectEntryPrologueHeader_t::HAS_STREAM_DESCR;
            objectEntryLength = prologueLength + StreamDescriptor_t::SIZE;
        }
        else {
            objectFlags |= ObjectEntryPrologueHeader_t::HAS_INLINE_PAYLOAD;
            objectEntryLength = prologueLength + contentsLength;
        }

        const uint16_t paddedObjectEntryLength = align(objectEntryLength, 16);

        ObjectEntryPrologueHeader_t objectPrologueHeader;
        objectPrologueHeader.length = objectEntryLength;
        objectPrologueHeader.flags = objectFlags;
        objectPrologueHeader.nameLength = objectNameLength;

        StreamDescriptor_t streamDescr;
        streamDescr.location = 0;
        streamDescr.length = 0;
        assert(useInlinePayload);

        directoryStream->setPos(0);

        uint64_t pos = 0;

        while (pos < directoryStream->getSize()) {
            ObjectEntryPrologueHeader_t prologueHeader;

            if (!retrieveStruct(directoryStream, pos, prologueHeader)) {
                return;
            }

            const uint16_t paddedEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::LENGTH_MASK, 16);

            if (prologueHeader.length & ObjectEntryPrologueHeader_t::IS_INVALIDATED) {
                // entry is invalidated, might be a candidate for overwriting
            }
            else {
                if (prologueHeader.nameLength == objectNameLength) {
                    // compare entry names

                }
            }

            pos += paddedEntryLength;
        }

        // no match found; create new entry
        if (!storeStruct(directoryStream, pos, objectPrologueHeader))
            return;

        pos += ObjectEntryPrologueHeader_t::SIZE;

        if (!setBytesAt(directoryStream, pos, (const uint8_t*) objectName, objectNameLength))
            return;

        pos += objectNameLength;

        if (useInlinePayload) {
            if (!setBytesAt(directoryStream, pos, contents, contentsLength))
                return;

            pos += contentsLength;

            // padding
            if (!clearBytesAt(directoryStream, pos, paddedObjectEntryLength - objectEntryLength))
                return;
        }
        else
            assert(false);
    }
};

Repository::Repository(ByteIO* bio, bool canCreateNew) {
    this->io = bio;

    this->canCreateNew = canCreateNew;
}

Repository::~Repository() {
    close();
}

bool Repository::open() {
    RepositoryPrologue_t prologue;

    const unsigned int cdsDescrLocation = RepositoryPrologue_t::SIZE;

    if (io->getSize() == 0) {
        diagnostic("repo:\tRepository file is empty; must create new");

        // Must create new
        if (!canCreateNew)
            return error("can't create new"), false;

        memcpy(prologue.magic, prologueMagic, sizeof(prologueMagic));
        prologue.formatVersion = 1;

        if (!storeStruct(io, 0, prologue)
            || !clearBytesAt(io, RepositoryPrologue_t::SIZE, StreamDescriptor_t::SIZE))
            return false;

        // create Content Directory
        // cds = Contend Directory Stream

        auto cds = new RepositoryStream(this, cdsDescrLocation,
                contentDirectoryReserveLength,
                contentDirectoryExpectedSize);

        contentDirectory = new RepositoryDirectory(cds);
    }
    else {
        if (!retrieveStruct(io, 0, prologue))
            return false;

        if (memcmp(prologue.magic, prologueMagic, sizeof(prologueMagic)) != 0)
            return error("magic doesn't match"), false;

        if (prologue.formatVersion > 1)
            return error("version not recognized"), false;

        diagnostic("repo:\tHeader: format version %d", prologue.formatVersion);
    }

    isOpen = true;
    return true;
}

void Repository::close() {
    if (isOpen) {
        diagnostic("repo:\tClosing Content Directory");
        delete contentDirectory;

        isOpen = false;
    }

    io->close();
    io = nullptr;
}

bool Repository::allocateSpan(uint64_t& location_out, SpanHeader_t& header_out, uint64_t dataLength) {
    dataLength = roundUpBlockLength(dataLength);

    uint64_t pos = align(io->getSize(), /*reserveAlignment*/ 1);
    diagnostic("allocating %u-byte span @ %u (end at %u)", (unsigned) dataLength, (unsigned) pos,
            (unsigned) (pos + StreamDescriptor_t::SIZE + dataLength));

    // initialize span
    SpanHeader_t header;
    header.reservedLength = dataLength;
    header.usedLength = 0;
    header.nextSpanLocation = 0;

    // alignment
    clearBytesAt(io, io->getSize(), pos - io->getSize());
    // header
    storeStruct(io, pos, header);
    // data
    clearBytesAt(io, pos + StreamDescriptor_t::SIZE, dataLength);

    location_out = pos;
    header_out = header;
    return true;
}

void Repository::setObjectContents1(const char* objectName, const char* contents) {
    contentDirectory->setObjectContents1(objectName, (const uint8_t*) contents, strlen(contents),
            PREFER_INLINE_PAYLOAD, ObjectEntryPrologueHeader_t::IS_TEXT);
}
}
