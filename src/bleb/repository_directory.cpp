
#include "repository_directory.hpp"
#include "repository_stream.hpp"

#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace bleb {
RepositoryDirectory::RepositoryDirectory(Repository* repo, RepositoryStream* directoryStream) {
    this->repo = repo;
    this->directoryStream = directoryStream;
}

RepositoryDirectory::~RepositoryDirectory() {
    delete directoryStream;
}

void RepositoryDirectory::getObjectContents1(const char* objectName, uint8_t*& contents_out, size_t& length_out) {
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

        const uint16_t paddedEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask, 16);

        if (prologueHeader.length & ObjectEntryPrologueHeader_t::kIsInvalidated) {
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

                    if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasInlinePayload) {
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

                        // TODO: check if stream.getSize() < size_t::MAX

                        length_out = (size_t) stream.getSize();
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

void RepositoryDirectory::setObjectEntry(const char* objectName, const uint8_t* objectEntryBytes,
        size_t objectEntryLength, size_t& objectEntryPos_out) {
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

        assert((prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask) >= 6); // FIXME: not assert
        const uint16_t paddedEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask, 16);

        if (prologueHeader.length & ObjectEntryPrologueHeader_t::kIsInvalidated) {
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
                        if (!clearBytesAt(directoryStream, pos + offset, paddedObjectEntryLength
                                - objectEntryLength))
                            return;

                        offset += paddedObjectEntryLength - objectEntryLength;

                        if (paddedObjectEntryLength < paddedEntryLength) {
                            ObjectEntryPrologueHeader_t invalidated;
                            invalidated.length = (paddedEntryLength - paddedObjectEntryLength)
                                    | ObjectEntryPrologueHeader_t::kIsInvalidated;
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

                        prologueHeader.length |= ObjectEntryPrologueHeader_t::kIsInvalidated;

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

void RepositoryDirectory::setObjectContents1(const char* objectName, const uint8_t* contents, size_t contentsLength,
        unsigned int flags, unsigned int objectFlags) {
    // Look through directory to see if object already exists
    // If it does, replace it (reuse stream, if any)
    // If not, build new entry


    // FIXME: error handling
    // Browse the directory and look for the object already existing
    // Also mark any nice usable spot to put it in

    const size_t objectNameLength = strlen(objectName);
    const uint16_t prologueLength = objectEntryPrologueLength(objectNameLength);

    // Figure out all the flags we will be using
    bool useInlinePayload = false;

    if (flags & kPreferInlinePayload) {
        if (prologueLength + contentsLength < ObjectEntryPrologueHeader_t::kLengthMask)
            useInlinePayload = true;
    }

    uint16_t objectEntryLength;

    if (!useInlinePayload) {
        objectFlags |= ObjectEntryPrologueHeader_t::kHasStreamDescr;
        objectEntryLength = prologueLength + StreamDescriptor_t::SIZE;
    }
    else {
        objectFlags |= ObjectEntryPrologueHeader_t::kHasInlinePayload;
        objectEntryLength = prologueLength + contentsLength;
    }

    // Prepare object entry
    ObjectEntryPrologueHeader_t objectPrologueHeader;
    objectPrologueHeader.length = objectEntryLength;
    objectPrologueHeader.flags = objectFlags;
    objectPrologueHeader.nameLength = objectNameLength;

    StreamDescriptor_t streamDescr;
    streamDescr.location = 0;
    streamDescr.length = 0;

    // Serialize entry data into bytes
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

    // Store in directory now
    setObjectEntry(objectName, entryBytes, objectEntryLength, objectEntryPos);

    if (!useInlinePayload) {
        RepositoryStream stream(repo, directoryStream, objectEntryPos + streamDescrOffset, contentsLength, contentsLength);
        stream.write(contents, contentsLength);

        // FIXME: need to make sure entry will be already written at this point
        // (it would overwrite streamDescr otherwise)
    }
}
}
