#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "internal.hpp"
#include "on_disk_structures.hpp"
#include "repository_directory.hpp"
#include "repository_stream.hpp"

#include <limits>

namespace bleb {
template <typename T> static T roundUpBlockLength(T streamLengthHint, T blockLength, T allocationGranularity) {
    // minimal rounding is 32, beyond that always align to
    // L / 8 where L is streamLengthHint rounded up to a power of 2

    if (streamLengthHint < blockLength)
        streamLengthHint = blockLength;

    // https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    streamLengthHint--;
    streamLengthHint |= streamLengthHint >> 1;
    streamLengthHint |= streamLengthHint >> 2;
    streamLengthHint |= streamLengthHint >> 4;
    streamLengthHint |= streamLengthHint >> 8;
    streamLengthHint |= streamLengthHint >> 16;

    if (sizeof(streamLengthHint) >= 8)
        streamLengthHint |= streamLengthHint >> 32;

    streamLengthHint++;

    streamLengthHint /= 8;

    if (streamLengthHint < allocationGranularity)
        streamLengthHint = allocationGranularity;

    return align(blockLength, streamLengthHint);
}

Repository::Repository(ByteIO* io) {
    this->io = io;
    this->deleteIO = false;

    this->entryBuffer = nullptr;
    this->entryBufferSize = 0;

    this->allocationGranularity = 32;
}

Repository::Repository(ByteIO* io, bool deleteIO) {
    this->io = io;
    this->deleteIO = deleteIO;

    this->entryBuffer = nullptr;
    this->entryBufferSize = 0;

    this->allocationGranularity = 32;
}

Repository::~Repository() {
    close();

    free(entryBuffer);

    if (deleteIO)
        delete io;
}

bool Repository::open(bool canCreateNew) {
    RepositoryPrologue_t prologue;

    const unsigned int cdsDescrLocation = RepositoryPrologue_t::SIZE;

    if (io->getSize() == 0) {
        //diagnostic("repo:\tRepository file is empty; must create new");

        // Must create new
        if (!canCreateNew)
            return error(errNotAllowed, "not allowed to initialize a new repository"), false;

        memcpy(prologue.magic, prologueMagic, sizeof(prologueMagic));
        prologue.formatVersion = 1;
        prologue.flags = 0;
        prologue.infoFlags = 0;

        if (!storeStruct(io, 0, prologue)
            || !clearBytesAt(io, RepositoryPrologue_t::SIZE, StreamDescriptor_t::SIZE))
            return error.writeError(), false;

        // create Content Directory
        // cds = Content Directory Stream

        auto cds = std::make_unique<RepositoryStream>(this, io, cdsDescrLocation,
                contentDirectoryReserveLength,
                contentDirectoryExpectedSize);

        // Stream allocation might have failed
        if (!cds->hasFirstSpan())
            return error.writeError(), false;

        contentDirectory = std::make_unique<RepositoryDirectory>(this, std::move(cds));
    }
    else {
        if (!retrieveStruct(io, 0, prologue))
            return false;

        if (memcmp(prologue.magic, prologueMagic, sizeof(prologueMagic)) != 0)
            return error(errNotABlebRepository, "magic value doesn't match"), false;

        if (prologue.formatVersion > 1 || (prologue.flags & ~(0)) != 0)
            return error(errNotSupported, "repository format version not recognized"), false;

        //diagnostic("repo:\tHeader: format version %d", prologue.formatVersion);

        auto cds = std::make_unique<RepositoryStream>(this, io, cdsDescrLocation);
        contentDirectory = std::make_unique<RepositoryDirectory>(this, std::move(cds));
    }

    isOpen = true;
    return true;
}

void Repository::close() {
    if (isOpen) {
        //diagnostic("repo:\tClosing Content Directory");
        contentDirectory.reset();

        isOpen = false;
    }

    if (io) {
        io->close();
        io = nullptr;
    }
}

bool Repository::allocateSpan(uint64_t& location_out, SpanHeader_t& header_out, uint64_t streamLengthHint,
        uint64_t spanLength) {
    spanLength = roundUpBlockLength(streamLengthHint, spanLength, allocationGranularity);

    uint64_t pos = align(io->getSize(), /*reserveAlignment*/ 1);
    diagnostic("allocating %u-byte span @ %u (end at %u)", (unsigned) spanLength, (unsigned) pos,
            (unsigned) (pos + StreamDescriptor_t::SIZE + spanLength));

    assert(spanLength <= std::numeric_limits<uint32_t>::max());

    // initialize span
    SpanHeader_t header;
    header.reservedLength = (uint32_t) spanLength;
    header.usedLength = 0;
    header.nextSpanLocation = 0;

    if (!clearBytesAt(io, io->getSize(), pos - io->getSize())       // alignment
            || !storeStruct(io, pos, header)                        // header
            || !clearBytesAt(io, pos + StreamDescriptor_t::SIZE, spanLength))   // data
        return error.writeError(), false;

    location_out = pos;
    header_out = header;
    return true;
}

uint8_t* Repository::getEntryBuffer(size_t size) {
    if (size > entryBufferSize)
        entryBuffer = (uint8_t*) realloc(entryBuffer, align(size, 32));

    return entryBuffer;
}

void Repository::getObjectContents(const char* objectName, uint8_t*& contents_out, size_t& length_out) {
    contentDirectory->getObjectContents(objectName, contents_out, length_out);
}

std::unique_ptr<ByteIO> Repository::openStream(const char* objectName, int streamCreationMode) {
    return contentDirectory->openStream(objectName, streamCreationMode, 0);
}

void Repository::setObjectContents(const char* objectName, const char* contents, int flags) {
    setObjectContents(objectName, contents, strlen(contents), flags);
}

void Repository::setObjectContents(const char* objectName, const void* contents, size_t length, int flags) {
    contentDirectory->setObjectContents(objectName, (const uint8_t*) contents, length,
            flags, ObjectEntryPrologueHeader_t::kIsText);
}
}
