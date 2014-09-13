#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "internal.hpp"
#include "on_disk_structures.hpp"
#include "repository_directory.hpp"
#include "repository_stream.hpp"

namespace bleb {
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

Repository::Repository(ByteIO* io, bool deleteIO) {
    this->io = io;
    this->deleteIO = deleteIO;

    this->entryBuffer = nullptr;
    this->entryBufferSize = 0;
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
            return error("can't create new"), false;

        memcpy(prologue.magic, prologueMagic, sizeof(prologueMagic));
        prologue.formatVersion = 1;
        prologue.flags = 0;
        prologue.infoFlags = 0;

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

        if (prologue.formatVersion > 1 || (prologue.flags & ~(0)) != 0)
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

    // FIXME: ensure dataLength fits in header.reservedLength

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
            kPreferInlinePayload, ObjectEntryPrologueHeader_t::kIsText);
}
}
