
#include "internal.hpp"
#include "repository_stream.hpp"

#include <algorithm>

namespace bleb {
    RepositoryStream::RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos) {
        this->repo = repo;
        this->io = repo->io;
        this->isReadOnly = false;
        this->descrIO = streamDescrIO;
        this->descrPos = streamDescrPos;

        descrDirty = false;
        pos = 0;
        haveCurrentSpan = false;

        initialLengthHint = 0;

        retrieveStruct(descrIO, descrPos, descr);

        if (descr.location != 0) {
            retrieveStruct(io, descr.location, firstSpan);
            setCurrentSpan(firstSpan, descr.location, 0);
        }
    }

    RepositoryStream::RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos,
            uint32_t reserveLength, uint64_t expectedSize) {
        this->repo = repo;
        this->io = repo->io;
        this->isReadOnly = false;
        this->descrIO = streamDescrIO;
        this->descrPos = streamDescrPos;

        descrDirty = false;
        pos = 0;
        haveCurrentSpan = false;

        initialLengthHint = 0;

        // create a new stream
        uint64_t firstSpanLocation;
        if (reserveLength > 0) {
            bool alloc = repo->allocateSpan(firstSpanLocation, firstSpan, expectedSize, reserveLength);
            assert(alloc);
        }
        else
            firstSpanLocation = 0;

        // initialize Stream Descriptor
        descr.location = firstSpanLocation;
        descr.length = 0;
        descrDirty = true;

        if (firstSpanLocation != 0)
            setCurrentSpan(firstSpan, firstSpanLocation, 0);
    }

    RepositoryStream::~RepositoryStream() {
        if (descrDirty)
            storeStruct(descrIO, descrPos, descr);
    }

    bool RepositoryStream::clearBytesAt(uint64_t pos, uint64_t count) {
        setPos(pos);

        while (count--)
            if (!write("\x00", 1))
                return false;

        return true;
    }

    bool RepositoryStream::gotoRightSpan() {
        // FIXME: this is highly unoptimal right now (and possibly broken)
        // we're always starting from the block beginning and going span-by-span

        if (descr.location == 0) {      // the stream doesn't even exist
            diagnostic("warning: trying to from read an unallocated stream");
            return false;
        }

        if (pos > descr.length)
            return false;

        setCurrentSpan(firstSpan, descr.location, 0);

        while (pos != currentSpanPosInStream) {
            // this should never happen
            if (currentSpanPosInStream > descr.length)
                assert(false);

            // is the 'pos' we're looking for within this span?
            if (pos <= currentSpanPosInStream + currentSpan.reservedLength) {
                posInCurrentSpan = (uint32_t)(pos - currentSpanPosInStream);
                break;
            }

            SpanHeader_t nextSpan;
            uint64_t nextSpanLocation = currentSpan.nextSpanLocation;

            if (nextSpanLocation != 0) {
                if (!retrieveStruct(io, nextSpanLocation, nextSpan))
                    return error.readError(), false;
            }
            else
                return error.unexpectedEndOfStream(), false;

            setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
        }

        return true;
    }

    size_t RepositoryStream::read(void* buffer_in, size_t length) {
        if (length == 0)
            return 0;

        uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_in);

        size_t readTotal = 0;

        if (!haveCurrentSpan) {
            if (!gotoRightSpan())
                return readTotal;
        }

        for (; length > 0;) {
            // start by checking whether we currently are within any span

            const uint64_t remainingBytesInSpan = currentSpan.reservedLength - posInCurrentSpan;

            //diagnostic("%llu = %u - %llu\n", remainingBytesInSpan, currentSpan.usedLength, posInCurrentSpan);
            if (remainingBytesInSpan > 0) {
                if (currentSpan.nextSpanLocation && currentSpan.usedLength < currentSpan.reservedLength)
                    return error.repositoryCorruption("span not fully utilized"), readTotal;

                const size_t read = (size_t) std::min<uint64_t>(remainingBytesInSpan, length);

                if (!io->getBytesAt(currentSpanLocation + SpanHeader_t::SIZE + posInCurrentSpan, buffer, read))
                    return error.readError(), readTotal;

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
                        return error.readError(), readTotal;
                }
                else
                    return error.unexpectedEndOfStream(), readTotal;

                setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
            }
        }

        return readTotal;
    }

    void RepositoryStream::setCurrentSpan(const SpanHeader_t& span, uint64_t spanLocation, uint64_t spanPosInStream) {
        currentSpan = span;

        currentSpanLocation = spanLocation;
        currentSpanPosInStream = spanPosInStream;
        posInCurrentSpan = 0;

        haveCurrentSpan = true;
    }

    void RepositoryStream::setLength(uint64_t length) {
        // TODO: release unneeded spans

        descr.length = length;
        descrDirty = true;
    }

    void RepositoryStream::setPos(uint64_t pos) {
        if (this->pos != pos) {
            this->pos = pos;

            if (haveCurrentSpan
                    && pos >= currentSpanPosInStream
                    && pos < currentSpanPosInStream + currentSpan.reservedLength) {
                posInCurrentSpan = (uint32_t)(pos - currentSpanPosInStream);
            }
            else {
                // FIXME: haveCurrentSpan x currentSpanIsTheOne
                haveCurrentSpan = false;
            }
        }
    }

    size_t RepositoryStream::write(const void* buffer_in, size_t length) {
        if (isReadOnly || length == 0)
            return 0;

        const uint8_t* buffer = reinterpret_cast<const uint8_t*>(buffer_in);

        size_t writtenTotal = 0;

        if (!haveCurrentSpan) {
            if (descr.location == 0) {
                // the block is empty; allocate initial span
                uint64_t firstSpanLocation;

                if (!repo->allocateSpan(firstSpanLocation, firstSpan, initialLengthHint, length))
                    return writtenTotal;

                setCurrentSpan(firstSpan, firstSpanLocation, 0);

                descr.location = firstSpanLocation;
                descrDirty = true;
            }
            else {
                // seek; will fail if pos > length
                if (!gotoRightSpan())
                    return writtenTotal;
            }
        }

        for (; length > 0;) {
            // start by checking whether we currently are within any span

            const uint64_t remainingBytesInSpan = currentSpan.reservedLength - posInCurrentSpan;

            if (remainingBytesInSpan > 0) {
                const size_t written = (size_t) std::min<uint64_t>(remainingBytesInSpan, length);

                if (!io->setBytesAt(currentSpanLocation + SpanHeader_t::SIZE + posInCurrentSpan, buffer, written))
                    return error.writeError(), false;

                posInCurrentSpan += written;
                pos += written;
                writtenTotal += written;

                if (pos > descr.length) {
                    descr.length = pos;
                    descrDirty = true;
                }

                buffer += written;
                length -= written;

                currentSpan.usedLength = std::max(currentSpan.usedLength, posInCurrentSpan);

                if (!storeStruct(io, currentSpanLocation, currentSpan))
                    return error.writeError(), writtenTotal;

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
                        return error.readError(), writtenTotal;
                }
                else {
                    // allocate a new span to hold the rest of the data

                    if (!repo->allocateSpan(nextSpanLocation, nextSpan, descr.length, length))
                        return writtenTotal;

                    // update CURRENT span to point to the NEW span
                    currentSpan.nextSpanLocation = nextSpanLocation;

                    if (!storeStruct(io, currentSpanLocation, currentSpan))
                        return error.writeError(), writtenTotal;

                    // we cache firstSpan, so it might need to be updated
                    if (currentSpanPosInStream == 0)
                        firstSpan = currentSpan;
                }

                setCurrentSpan(nextSpan, nextSpanLocation, currentSpanPosInStream + currentSpan.reservedLength);
            }
        }

        return writtenTotal;
    }
}
