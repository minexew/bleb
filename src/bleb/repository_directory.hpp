#pragma once

#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "on_disk_structures.hpp"

namespace bleb {
class RepositoryStream;

class RepositoryDirectory {
public:
    RepositoryDirectory(Repository* repo, RepositoryStream* directoryStream);
    ~RepositoryDirectory();

    bool getObjectContents(const char* objectName, uint8_t*& contents_out, size_t& length_out);
    bool setObjectContents(const char* objectName, const uint8_t* contents, size_t contentsLength,
            unsigned int flags, unsigned int objectFlags);

    ByteIO* openStream(const char* objectName, int streamCreationMode, uint32_t reserveLength);

private:
    RepositoryDirectory(const RepositoryDirectory&) = delete;

    int findObjectByName(const char* objectName, size_t objectNameLength, uint64_t* pos_out,
            ObjectEntryPrologueHeader_t* prologueHeader_out, size_t newEntrySize = 0,
            uint64_t* newEntryPos_out = nullptr);

    bool invalidateEntryAt(uint64_t pos, ObjectEntryPrologueHeader_t prologueHeader);
    bool overwriteObjectEntryAt(uint64_t pos, const uint8_t* entryBytes, size_t entryLength);

    Repository* repo;
    RepositoryStream* directoryStream;
};
}
