#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "internal.hpp"
#include "on_disk_structures.hpp"

namespace bleb {

template <typename T> static T min(T a, T b) {
    return (a < b) ? a : b;
}

template <typename T> static T max(T a, T b) {
    return (a > b) ? a : b;
}

template <typename T> static T roundUpBlockLength(T length) {
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

class RepositoryStream : public ByteIO {
    Repository* repo;
    ByteIO* io;
    bool isReadOnly = false;

    ByteIO* descrIO;
    uint64_t descrPos;
    StreamDescriptor_t descr;
    bool descrDirty;

    uint64_t pos;

    bool haveCurrentSpan;
    SpanHeader_t firstSpan, currentSpan;
    uint64_t currentSpanLocation, currentSpanPosInStream;
    uint32_t posInCurrentSpan;

public:
    RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos) {
        this->repo = repo;
        this->io = repo->io;
        this->descrIO = streamDescrIO;
        this->descrPos = streamDescrPos;

        descrDirty = false;
        pos = 0;
        haveCurrentSpan = false;

        retrieveStruct(descrIO, descrPos, descr);

        if (descr.location != 0) {
            retrieveStruct(io, descr.location, firstSpan);
            setCurrentSpan(firstSpan, descr.location, 0);
        }
    }

    RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos,
            uint32_t reserveLength, uint64_t expectedSize) {
        this->repo = repo;
        this->io = repo->io;
        this->descrIO = streamDescrIO;
        this->descrPos = streamDescrPos;

        descrDirty = false;
        pos = 0;
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

    virtual ~RepositoryStream() {
        if (descrDirty)
            storeStruct(descrIO, descrPos, descr);
    }

    virtual uint64_t getSize() override {
        return descr.length;
    }

    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, uint64_t count) override {
        return setPos(pos), read(buffer, count) == count;
    }

    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, uint64_t count) override {
        return setPos(pos), write(buffer, count) == count;
    }

    virtual bool clearBytesAt(uint64_t pos, uint64_t count) override {
        setPos(pos);

        while (count--)
            if (!write("\x00", 1))
                return false;

        return true;
    }

    uint64_t getPos() {
        return pos;
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
            fprintf(stderr, "goto right for read\n");
            if (!gotoRightSpan())
                return readTotal;
        }

        for (; length > 0;) {
            // start by checking whether we currently are within any span

            const uint64_t remainingBytesInSpan = currentSpan.usedLength - posInCurrentSpan;
            //fprintf(stderr, "%llu = %u - %llu\n", remainingBytesInSpan, currentSpan.usedLength, posInCurrentSpan);
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
                fprintf(stderr, "goto right for write\n");
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

                currentSpan.usedLength = max(currentSpan.usedLength, posInCurrentSpan);

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

class RepositoryDirectory {
    Repository* repo;
    RepositoryStream* directoryStream;

public:
    RepositoryDirectory(Repository* repo, RepositoryStream* directoryStream) {
        this->repo = repo;
        this->directoryStream = directoryStream;
    }

    ~RepositoryDirectory() {
        delete directoryStream;
    }

    void getObjectContents1(const char* objectName, uint8_t*& contents_out, size_t& length_out) {
        // FIXME: error handling

        const size_t objectNameLength = strlen(objectName);

        // search for object
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
                size_t offset = ObjectEntryPrologueHeader_t::SIZE;

                if (prologueHeader.nameLength == objectNameLength) {
                    // compare entry names

                    uint8_t* name = (uint8_t*) alloca(prologueHeader.nameLength);

                    if (!getBytesAt(directoryStream, pos + offset, name, prologueHeader.nameLength))
                        return;

                    offset += prologueHeader.nameLength;

                    if (memcmp(name, objectName, prologueHeader.nameLength) == 0) {
                        // match, retrieve object

                        if (prologueHeader.flags & ObjectEntryPrologueHeader_t::HAS_INLINE_PAYLOAD) {
                            // FIXME: offset might be incorrect
                            length_out = prologueHeader.length - offset;
                            contents_out = (uint8_t*) malloc(length_out);

                            if (!getBytesAt(directoryStream, pos + offset, contents_out, length_out))
                                return;

                            return;
                        }
                        else {
                            // FIXME: offset might be incorrect
                            RepositoryStream stream(repo, directoryStream, pos + offset);

                            length_out = stream.getSize();
                            contents_out = (uint8_t*) malloc(length_out);

                            stream.read(contents_out, length_out);
                            return;
                        }
                    }
                }
            }

            pos += paddedEntryLength;
        }

        contents_out = nullptr;
        length_out = 0;
    }

    void setObjectEntry(const char* objectName, const uint8_t* objectEntryBytes, size_t objectEntryLength, size_t& objectEntryPos_out) {
        const size_t objectNameLength = strlen(objectName);
        const uint16_t paddedObjectEntryLength = align(objectEntryLength, 16);

        directoryStream->setPos(0);

        uint64_t pos = 0;

        //fprintf(stderr, "entering loop\n");

        while (pos < directoryStream->getSize()) {
            //fprintf(stderr, "testing at %llu\n", pos);
            ObjectEntryPrologueHeader_t prologueHeader;

            size_t offset = 0;

            if (!retrieveStruct(directoryStream, pos + offset, prologueHeader)) {
                return;
            }

            offset += ObjectEntryPrologueHeader_t::SIZE;

            assert((prologueHeader.length & ObjectEntryPrologueHeader_t::LENGTH_MASK) >= 6); // FIXME: not assert
            const uint16_t paddedEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::LENGTH_MASK, 16);

            if (prologueHeader.length & ObjectEntryPrologueHeader_t::IS_INVALIDATED) {
                // entry is invalidated, might be a candidate for overwriting
            }
            else {
                if (prologueHeader.nameLength == objectNameLength) {
                    // compare entry names

                    uint8_t* name = (uint8_t*) alloca(prologueHeader.nameLength);

                    if (!getBytesAt(directoryStream, pos + offset, name, prologueHeader.nameLength))
                        return;

                    offset += prologueHeader.nameLength;

                    if (memcmp(name, objectName, prologueHeader.nameLength) == 0) {
                        //fprintf(stderr, "match, original %u, new %u\n", paddedEntryLength, paddedObjectEntryLength);

                        // FIXME: the way we overwrite things here is obviously completely broken

                        if (paddedEntryLength >= paddedObjectEntryLength) {
                            // overwriting larger entry

                            offset = 0;

                            if (!setBytesAt(directoryStream, pos + offset, objectEntryBytes, objectEntryLength))
                                return;

                            offset += objectEntryLength;

                            // padding
                            if (!clearBytesAt(directoryStream, pos + offset, paddedObjectEntryLength - objectEntryLength))
                                return;

                            offset += paddedObjectEntryLength - objectEntryLength;

                            if (paddedObjectEntryLength < paddedEntryLength) {
                                ObjectEntryPrologueHeader_t invalidated;
                                invalidated.length = (paddedEntryLength - paddedObjectEntryLength) | ObjectEntryPrologueHeader_t::IS_INVALIDATED;
                                invalidated.flags = 0;
                                invalidated.nameLength = 0;

                                //fprintf(stderr, "reclaimed %u-byte entry @ %llu\n", (paddedEntryLength - paddedObjectEntryLength), pos);
                                if (!storeStruct(directoryStream, pos + offset, invalidated))
                                    return;
                            }

                            objectEntryPos_out = pos;
                            return;
                        }
                        else {
                            offset = 0;

                            prologueHeader.length |= ObjectEntryPrologueHeader_t::IS_INVALIDATED;

                            //fprintf(stderr, "invalidated %u-byte entry @ %llu\n", paddedEntryLength, pos);
                            if (!storeStruct(directoryStream, pos + offset, prologueHeader))
                                return;

                            pos = directoryStream->getSize();
                            break;
                        }
                    }
                }
            }

            pos += paddedEntryLength;
        }

        objectEntryPos_out = pos;
        //fprintf(stderr, "new object at %llu\n", pos);

        // no match found; create new entry
        if (!setBytesAt(directoryStream, pos, objectEntryBytes, objectEntryLength))
            return;

        pos += objectEntryLength;

        // padding
        if (!clearBytesAt(directoryStream, pos, paddedObjectEntryLength - objectEntryLength))
            return;
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

        ObjectEntryPrologueHeader_t objectPrologueHeader;
        objectPrologueHeader.length = objectEntryLength;
        objectPrologueHeader.flags = objectFlags;
        objectPrologueHeader.nameLength = objectNameLength;

        StreamDescriptor_t streamDescr;
        streamDescr.location = 0;
        streamDescr.length = 0;

        // build entry bytes
        uint8_t* entryBytes = repo->getEntryBuffer(objectEntryLength);
        size_t pos = 0;

        if (!storeStruct(entryBytes, pos, objectPrologueHeader))
            return;

        pos += ObjectEntryPrologueHeader_t::SIZE;

        if (!setBytesAt(entryBytes, pos, (const uint8_t*) objectName, objectNameLength))
            return;

        pos += objectNameLength;

        size_t streamDescrOffset;

        if (!useInlinePayload) {
            streamDescrOffset = pos;

            if (!storeStruct(entryBytes, pos, streamDescr))
                return;

            pos += StreamDescriptor_t::SIZE;
        }
        else {
            if (!setBytesAt(entryBytes, pos, contents, contentsLength))
                return;

            pos += contentsLength;
        }

        size_t objectEntryPos;

        // store in directory
        setObjectEntry(objectName, entryBytes, objectEntryLength, objectEntryPos);

        if (!useInlinePayload) {
            RepositoryStream stream(repo, directoryStream, objectEntryPos + streamDescrOffset, contentsLength, contentsLength);
            stream.write(contents, contentsLength);

            // FIXME: need to make sure entry will be already written at this point
            // (it would overwrite streamDescr otherwise)
        }
    }
};

Repository::Repository(ByteIO* io, bool canCreateNew, bool deleteIO) {
    this->io = io;
    this->deleteIO = deleteIO;

    this->canCreateNew = canCreateNew;

    this->entryBuffer = nullptr;
    this->entryBufferSize = 0;
}

Repository::~Repository() {
    close();

    free(entryBuffer);

    if (deleteIO)
        delete io;
}

bool Repository::open() {
    RepositoryPrologue_t prologue;

    const unsigned int cdsDescrLocation = RepositoryPrologue_t::SIZE;

    if (io->getSize() == 0) {
        //diagnostic("repo:\tRepository file is empty; must create new");

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

        auto cds = new RepositoryStream(this, io, cdsDescrLocation,
                contentDirectoryReserveLength,
                contentDirectoryExpectedSize);

        contentDirectory = new RepositoryDirectory(this, cds);
    }
    else {
        if (!retrieveStruct(io, 0, prologue))
            return false;

        if (memcmp(prologue.magic, prologueMagic, sizeof(prologueMagic)) != 0)
            return error("magic doesn't match"), false;

        if (prologue.formatVersion > 1)
            return error("version not recognized"), false;

        //diagnostic("repo:\tHeader: format version %d", prologue.formatVersion);

        auto cds = new RepositoryStream(this, io, cdsDescrLocation);
        contentDirectory = new RepositoryDirectory(this, cds);
    }

    isOpen = true;
    return true;
}

void Repository::close() {
    if (isOpen) {
        //diagnostic("repo:\tClosing Content Directory");
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

uint8_t* Repository::getEntryBuffer(size_t size) {
    if (size > entryBufferSize)
        entryBuffer = (uint8_t*) realloc(entryBuffer, align(size, 32));

    return entryBuffer;
}

void Repository::getObjectContents1(const char* objectName, uint8_t*& contents_out, size_t& length_out) {
    contentDirectory->getObjectContents1(objectName, contents_out, length_out);
}

void Repository::setObjectContents1(const char* objectName, const char* contents) {
    setObjectContents1(objectName, contents, strlen(contents));
}

void Repository::setObjectContents1(const char* objectName, const void* contents, size_t length) {
    contentDirectory->setObjectContents1(objectName, (const uint8_t*) contents, length,
            PREFER_INLINE_PAYLOAD, ObjectEntryPrologueHeader_t::IS_TEXT);
}
}
