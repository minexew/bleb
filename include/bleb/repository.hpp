#pragma once

#include <cstdio>
#include <cstdlib>

namespace bleb {

struct SpanHeader_t;
class RepositoryDirectory;

enum {
    PREFER_INLINE_PAYLOAD = 1,
    //REQUIRE_INLINE_PAYLOAD = 2,
};

class Repository {
public:
    //enum { STREAM_CREATE = 1, STREAM_TRUNCATE = 2 };

    Repository(ByteIO* bio, bool canCreateNew);
    ~Repository();
    bool open();
    void close();

    //void openStream1(const char* objectName, int creationFlags);
    void setObjectContents1(const char* objectName, const char* contents);

private:
    bool allocateSpan(uint64_t& location_out, SpanHeader_t& header_out, uint64_t dataLength);
    void error(const char* what) { fprintf(stderr, "%s", what); exit(EXIT_FAILURE); }

    //void openStream1(const char* objectName);
    void setObjectContentsInDirectory1(RepositoryDirectory* dir, const char* objectName, const void* contents, size_t contentsLength,
            unsigned int flags);

    ByteIO* io;
    bool isOpen = false;
    bool canCreateNew;

    RepositoryDirectory* contentDirectory;

    enum { reserveAlignment = 64 };

    enum { contentDirectoryReserveLength = 240 };
    enum { contentDirectoryExpectedSize = 4096 };

    friend class RepositoryStream;
};

}
