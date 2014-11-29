#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <memory>

namespace bleb {

struct SpanHeader_t;
class ByteIO;
class Repository;
class RepositoryDirectory;

typedef uint64_t SizeType;

enum {
    kPreferInlinePayload = 1,
    //REQUIRE_INLINE_PAYLOAD = 2,
};

enum ErrorKind {
    errNoError = 0,
    errInternal,
    errNotAllowed,
    errIO,
    errUnexpectedEOF,
    errRepositoryCorruption,
    errNotABlebRepository,
    errNotEnoughMemory,
    errNotSupported,
};

enum StreamCreationMode {
    kStreamCreate = 1,
    kStreamTruncate = 2
};

struct ErrorStruct_ {
    ErrorStruct_() : errorKind(errNoError), errorDesc(nullptr) {}
    ~ErrorStruct_() { free(errorDesc); }

    void operator()(ErrorKind kind, const char* desc);
    void readError();
    void repositoryCorruption(const char* hint);
    void unexpectedEndOfStream();
    void writeError();

    ErrorKind errorKind;
    char* errorDesc;
};

class DirectoryIterator {
public:
DirectoryIterator(Repository* repo, RepositoryDirectory* dir, SizeType pos);

bool operator != (const DirectoryIterator& other) const
{
    return repo != other.repo || dir != other.dir || pos != other.pos;
}

const char* operator*() const
{
    return objectName;
}

DirectoryIterator& operator++()
{
    readNext();
    return *this;
}

private:
    bool readNext();

    Repository* repo;
    RepositoryDirectory* dir;
    SizeType pos;
    char* objectName;
};

class Repository {
public:
    Repository(ByteIO* io, bool deleteIO);
    ~Repository();
    bool open(bool canCreateNew);
    void close();

    ErrorKind getErrorKind() const { return error.errorKind; }
    const char* getErrorDesc() const { return error.errorDesc; }

    std::unique_ptr<ByteIO> openStream(const char* objectName, int streamCreationMode);
    void getObjectContents(const char* objectName, uint8_t*& contents_out, size_t& length_out);
    void setObjectContents(const char* objectName, const char* contents, int flags);
    void setObjectContents(const char* objectName, const void* contents, size_t length, int flags);

    void setAllocationGranularity(SizeType value) { this->allocationGranularity = value; }

    DirectoryIterator begin() { return DirectoryIterator(this, contentDirectory, 0); }
    DirectoryIterator end() { return DirectoryIterator(this, contentDirectory, (SizeType) -1); }

private:
    Repository(const Repository&) = delete;

    bool allocateSpan(uint64_t& location_out, SpanHeader_t& header_out, uint64_t streamLengthHint, uint64_t spanLength);

    uint8_t* getEntryBuffer(size_t size);

    //void openStream1(const char* objectName);
    void setObjectContentsInDirectory1(RepositoryDirectory* dir, const char* objectName, const void* contents,
            size_t contentsLength, unsigned int flags);

    ByteIO* io;
    bool deleteIO;
    bool isOpen = false;

    RepositoryDirectory* contentDirectory;

    // entry buffer
    uint8_t* entryBuffer;
    size_t entryBufferSize;

    ErrorStruct_ error;

    // tuning
    SizeType allocationGranularity;

    enum { reserveAlignment = 64 };

    enum { contentDirectoryReserveLength = 192 };
    enum { contentDirectoryExpectedSize = 192 };

    friend class DirectoryIterator;
    friend class RepositoryDirectory;
    friend class RepositoryStream;
};

}
