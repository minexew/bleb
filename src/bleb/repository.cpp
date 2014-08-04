#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "internal.hpp"
#include "on_disk_structures.hpp"

namespace bleb {

class RepositoryStream /*: IOStream*/ {
    Repository* repo;
    ByteIO* io;

    uint64_t descrLocation;
    StreamDescriptor_t descr;

    uint64_t pos;

    bool haveCurrentSpan;
    SpanHeader_t currentSpan;
    uint64_t posInCurrentSpan, currentSpanPosInStream;

public:
    RepositoryStream(Repository* repo, uint64_t streamDescriptorLocation) {
        this->repo = repo;
        this->io = repo->io;
        this->descrLocation = streamDescriptorLocation;

        haveCurrentSpan = false;

        retrieveStruct(io, descrLocation, descr);

        if (descr.location != 0)
            retrieveStruct(io, descr.location, firstSpan);
    }

    RepositoryStream(Repository* repo, uint64_t streamDescriptorLocation, uint32_t reserveLength,
            uint64_t expectedSize) {
        this->repo = repo;
        this->io = repo->io;
        this->descrLocation = streamDescriptorLocation;

        haveCurrentSpan = false;    // fixme?

        // create a new stream
        uint64_t spanLocation = repo->reserveSpan(reserveLength, expectedSize);

        // initialize span
        SpanHeader_t header;
        header.reservedLength = reserveLength;
        header.usedLength = 0;
        header.nextSpanLocation = 0;

        storeStruct(io, spanLocation, header);
        firstSpan = header;

        // initialize Stream Descriptor
        this->descr.location = spanLocation;
        this->descr.length = 0;

        storeStruct(io, descrLocation, this->descr);
    }

    uint64_t getSize() {
        return descr.length;
    }

    void setPos(uint64_t pos) {
        this->pos = pos;
    }

    size_t read(void* buffer_in, size_t length) {



        if (!haveCurrentSpan)
            gotoRightSpan();
    }

    size_t write(const void* buffer_in, size_t length) {
        if (isReadOnly || count == 0)
            return 0;

        const uint8_t* buffer = reinterpret_cast<const uint8_t*>(buffer_in);

        size_t writtenTotal = 0;

        for (;;) {
            // start by checking whether we currently are within any span

            if (haveCurrentSpan) {
                if (descr.length == 0) {
                    // the block is empty; allocate initial span

                    if (!AllocateSpan(&first_span, count))
                        return writtenTotal;

                    setCurrentSpan(firstSpan);
                    currentSpanPosInStream = 0;

                    // we'll have to update first_span in block desc
                    updateDesc = true;
                }
                else
                    // seek; will fail if pos > length
                    if (!gotoRightSpan())
                        return writtenTotal;
            }

            // we now have a valid span to write into
            // if the span has a chaining tag on its end, the usable capacity will be 8 bytes less
            // in that case we might have to fetch or allocate more spans

            // how many bytes from current pos till the end of this span?
            //const uint64_t maxRemainingBytesInSpan = curr_span.sect_count * mf->sectorSize - curr_span_pos;

            //const uint64_t newLength = li::maximum<uint64_t>(length, pos + count);

            // are there/will there be any more spans beyond this one? if so, last 8 bytes are used for chaining
            //const int64_t remainingBytesInSpan = maxRemainingBytesInSpan
            //    - (curr_span_in_stream + curr_span.sect_count * mf->sectorSize < newLength ? 8 : 0);

            const int64_t remainingBytesInSpan = currentSpan.reservedLength - posInCurrentSpan;

            if (remainingBytesInSpan > 0) {
                const size_t written = min(remainingBytesInSpan, count);

                if (!io->setBytesAt(currentSpanLocation + SpanHeader_t::SIZE + posInCurrentSpan, buffer_in, written))
                    return false;

                curr_span_pos += written;
                pos += written;
                written_total += written;

                if (pos > descr.length) {
                    descr.length = pos;
                    updateDesc = true;
                }

                ////if (written < (unsigned int) affectedBytesInSpan)
                    return written_total;

                buffer_in += written;
                count -= written;
            }

            if (count > 0) {
                // we'll have to go beyond this span
                // if there is a chaining tag already, we'll take the jump,
                // otherwise we have to allocate a new span

                // will any more data fit in this span?

                else
                    mf->file->setPos(curr_span.sect_first * mf->sectorSize + curr_span_pos + affectedBytesInSpan);

                if (tailLength > 8)
                {
                    zmfSpan_t next_span;

                    // read the tag for next span
                    if (!mf->file->readLE<uint32_t>(&next_span.sect_first)
                        || !mf->file->readLE<uint32_t>(&next_span.sect_count))
                        return written_total;

                    if (!CanJumpTo(curr_span, next_span))
                        return false;

                    // loop for next span
                    curr_span_in_stream += curr_span.sect_count * mf->sectorSize - 8;
                    SetCurrent(&next_span);
                }
                else
                {
                    // current data ends within this span; we might have to copy a couple of byte
                    // if they are to be rewritten with the chaining tag

                    uint8_t tail[8];

                    if (tailLength > 0)
                        if (mf->file->read(tail, (size_t) tailLength) != (size_t) tailLength)
                            return written_total;

                    zmfSpan_t new_span;

                    // allocate a new span
                    if (!AllocateSpan(&new_span, count))
                        return written_total;

                    // write chaining tag
                    if (!mf->file->setPos((curr_span.sect_first + curr_span.sect_count) * mf->sectorSize - 8)
                        || !mf->file->writeLE<uint32_t>(new_span.sect_first)
                        || !mf->file->writeLE<uint32_t>(new_span.sect_count))
                        return written_total;

                    // jump to new span
                    curr_span_in_stream += curr_span.sect_count * mf->sectorSize - 8;
                    SetCurrent(&new_span);

                    // flush saved data (if any) and loop for the newly allocated span
                    if (tailLength > 0)
                    {
                        if (!mf->file->setPos(curr_span.sect_first * mf->sectorSize)
                            || mf->file->write(tail, (size_t) tailLength) != (size_t) tailLength)
                            return written_total;

                        curr_span_pos = tailLength;
                    }
                }
            }
        }
    }
    }

private:
    bool gotoRightSpan() {
        // FIXME: this is highly unoptimal right now (and possibly broken)
        // we're always starting from the block beginning and going span-by-span

        if (descr.location == 0)        // we don't actually "exist"
            assert(false);

        SetCurrent(&first_span);
        currentSpanPosInStream = 0;

        if (pos > length)
            return false;

        while (pos != currentSpanPosInStream) {
            // this should never happen
            if (currentSpanPosInStream > length)
                assert(false);

            // is the 'pos' we're looking for within this span?
            if (pos < currentSpanPosInStream + currentSpan.reservedLength) {
                posInCurrentSpan = pos - currentSpanPosInStream;
                break;
            }

            //const uint64_t maxPossibleBytesInSpan = curr_span.sect_count * mf->sectorSize;

            // does this span have an 8-byte tag at its end?
            // we can find out by checking whether it contains the rest of the block
            //const uint64_t bytesInSpan = maxPossibleBytesInSpan
            //    - (currentSpanPosInStream + maxPossibleBytesInSpan < length ? 8 : 0);

            // is the 'pos' we're looking for within this span?
            //if (pos <= currentSpanPosInStream + bytesInSpan) {
            //    posInCurrentSpan = pos - currentSpanPosInStream;
            //    break;
            //}

            SpanHeader_t nextSpan;

            if (!retrieveStruct(io, currentSpan.nextSpanLocation, nextSpan))
                return false;

            // read the tag for next span
            /*if (!mf->file->setPos(curr_span.sect_first * mf->sectorSize + bytesInSpan)
                || !mf->file->readLE<uint32_t>(&next_span.sect_first)
                || !mf->file->readLE<uint32_t>(&next_span.sect_count))
                return false;*/

            setCurrentSpan(&nextSpan);
            currentSpanPosInStream += bytesInSpan;
        }

        return true;
    }

    void setCurrentSpan(const SpanHeader_t& span) {
        currentSpan = span;
        posInCurrentSpan = 0;
    }
};

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
};

Repository::Repository(ByteIO* bio, bool canCreateNew) {
    this->io = bio;

    this->canCreateNew = canCreateNew;
}

bool Repository::open() {
    RepositoryPrologue_t prologue;

    const unsigned int cdsDescrLocation = RepositoryPrologue_t::SIZE;

    if (io->getSize() == 0) {
        diagnostic("repo:\tRepository file is empty; must create new");

        // Must create new
        if (!canCreateNew)
            return error("can't create new"), false;

        memcpy(prologue.magic, prologueMagic, 4);
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

        if (memcmp(prologue.magic, prologueMagic, 4) != 0)
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
        isOpen = false;
    }

    io->close();
    io = nullptr;
}

uint64_t Repository::reserveSpan(uint64_t reserveLength, uint64_t expectedSize) {
    uint64_t pos = (io->getSize() + reserveAlignment) & ~reserveAlignment;
    diagnostic("clearing (pre, dat...)");
    clearBytesAt(io, io->getSize(), pos - io->getSize());
    clearBytesAt(io, pos, StreamDescriptor_t::SIZE + reserveLength);
    return pos;
}

void Repository::setObjectContents1(const char* objectName, const char* contents) {
    this->setObjectContentsInDirectory1(contentDirectory, objectName, contents, strlen(contents))
}

void Repository::setObjectContentsInDirectory1(RepositoryDirectory* dir, const char* objectName, const void* contents, size_t length) {
    // Browse the directory and look for the object already existing
    // Also mark any nice usable spot to put it in

    const size_t objectNameLength = strlen(objectName);

    const auto newEntryLength = objectEntryPrologueLength(objectNameLength);

    ;
}
}
