#pragma once

#include <cstdio>
#include <cstdlib>

namespace bleb {

class RepositoryDirectory;

class Repository {
public:
    //enum { STREAM_CREATE = 1, STREAM_TRUNCATE = 2 };

    Repository(ByteIO* bio, bool canCreateNew);
    bool open();
    void close();

    //void openStream1(const char* objectName, int creationFlags);
    void setObjectContents1(const char* objectName, const char* contents);

private:
    uint64_t reserveSpan(uint64_t reserveLength, uint64_t expectedSize);
    void error(const char* what) { fprintf(stderr, "%s", what); exit(EXIT_FAILURE); }

    //void openStream1(const char* objectName);
    void setObjectContentsInDirectory1(RepositoryDirectory* dir, const char* objectName, const void* contents, size_t length);

    ByteIO* io;
    bool isOpen = false;
    bool canCreateNew;

    RepositoryDirectory* contentDirectory;

    enum { reserveAlignment = 64 - 1 };

    enum { contentDirectoryReserveLength = 240 };
    enum { contentDirectoryExpectedSize = 4096 };

    friend class RepositoryStream;
};

}
