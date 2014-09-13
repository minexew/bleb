#pragma once

#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

namespace bleb {
class RepositoryStream;

class RepositoryDirectory {
public:
    RepositoryDirectory(Repository* repo, RepositoryStream* directoryStream);
    ~RepositoryDirectory();

    void getObjectContents1(const char* objectName, uint8_t*& contents_out, size_t& length_out);
    void setObjectEntry(const char* objectName, const uint8_t* objectEntryBytes, size_t objectEntryLength,
            size_t& objectEntryPos_out);
    void setObjectContents1(const char* objectName, const uint8_t* contents, size_t contentsLength,
            unsigned int flags, unsigned int objectFlags);

private:
    Repository* repo;
    RepositoryStream* directoryStream;
};
}
