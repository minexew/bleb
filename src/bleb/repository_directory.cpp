
#include "repository_directory.hpp"
#include "repository_stream.hpp"

#ifdef _MSC_VER
// needed for alloca
#include <malloc.h>
#endif

#include <limits>
#include <memory>

namespace bleb {
RepositoryDirectory::RepositoryDirectory(Repository* repo, RepositoryStream* directoryStream) {
    this->repo = repo;
    this->directoryStream = directoryStream;
}

RepositoryDirectory::~RepositoryDirectory() {
    delete directoryStream;
}

/*
 *  Walk the directory and look for an object named `objectName`.
 *  If found, `pos_out` is set to its position within directory and `prologueHeader_out` will contain a copy of the
 *  entry's Prologue Header.
 *  Additionaly, if `newEntrySize` is non-zero, `newEntryPos_out` will be set to the position of an invalidated entry
 *  at least `newEntrySize` bytes in size (if any) or the end of the directory stream.
 *
 *  Return value:
 *      1   if the object was found
 *      0   if an error occured
 *      -1  if not found
 */
int RepositoryDirectory::findObjectByName(const char* objectName, size_t objectNameLength, uint64_t* pos_out,
            ObjectEntryPrologueHeader_t* prologueHeader_out, size_t newEntrySize, uint64_t* newEntryPos_out) {
    size_t newEntryPickedSize = std::numeric_limits<size_t>::max();         // pick any entry at first

    // start at the beginning of the directory stream
    directoryStream->setPos(0);

    uint64_t pos = 0;

    while (pos < directoryStream->getSize()) {
        ObjectEntryPrologueHeader_t prologueHeader;

        // read the entry's prologue header
        if (!retrieveStruct(directoryStream, pos, prologueHeader))
            return repo->error.readError(), false;

        // calculate actual entry length in bytes
        const uint16_t paddedEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask, 16);

        if (prologueHeader.length & ObjectEntryPrologueHeader_t::kIsInvalidated) {
            // entry is invalidated, might be a candidate for overwriting
            
            if (newEntrySize != 0) {
                prologueHeader.length &= ~ObjectEntryPrologueHeader_t::kIsInvalidated;

                // aim to return the smallest matching entry
                // TODO: we might want to modify this so that too big entries are not considered at all
                if (prologueHeader.length >= newEntrySize && prologueHeader.length < newEntryPickedSize) {
                    newEntryPickedSize = prologueHeader.length;
                    *newEntryPos_out = pos;
                }
            }
        }
        else {
            // entry is valid, let's have a look at it

            if ((prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask) < 6)
                return repo->error.repositoryCorruption("entry with invalid length (length < 6)"), false;

            size_t offset = ObjectEntryPrologueHeader_t::SIZE;

            // if the name length doesn't match, there's no point in investigating it further
            if (prologueHeader.nameLength == objectNameLength) {
                // read object name
                uint8_t* name = (uint8_t*) alloca(prologueHeader.nameLength);

                if (!getBytesAt(directoryStream, pos + offset, name, prologueHeader.nameLength))
                    return repo->error.readError(), false;

                offset += prologueHeader.nameLength;

                // compare entry names
                if (memcmp(name, objectName, prologueHeader.nameLength) == 0) {
                    // it's a match, we've found the object and our work here is done
                    *pos_out = pos;
                    *prologueHeader_out = prologueHeader;
                    return true;
                }
            }
        }

        pos += paddedEntryLength;
    }

    if (newEntrySize != 0 && newEntryPickedSize == std::numeric_limits<size_t>::max()) {
        // no suitable newEntryPos was found? use current pos (end of directory stream)
        *newEntryPos_out = pos;
    }

    return -1;
}

/*
 *  Retrieve object contents into a malloc-ed buffer.
 */
bool RepositoryDirectory::getObjectContents(const char* objectName, uint8_t*& contents_out, size_t& length_out) {
    const size_t objectNameLength = strlen(objectName);

    uint64_t pos;
    ObjectEntryPrologueHeader_t prologueHeader;

    // look for the object
    int find = findObjectByName(objectName, objectNameLength, &pos, &prologueHeader);

    if (!find || find < 0) {
        // the object was not found or an error occured; we don't care, the caller will check

        contents_out = nullptr;
        length_out = 0;

        return false;
    }

    // we have a match

    size_t offset = ObjectEntryPrologueHeader_t::SIZE + prologueHeader.nameLength;

    if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasStreamDescr) {
        // FIXME: offset might be incorrect due to other descriptors
        RepositoryStream stream(repo, directoryStream, pos + offset);

        if (stream.getSize() > std::numeric_limits<size_t>::max())
            return repo->error(errNotEnoughMemory, "the requested object is too big to fit into memory"), false;

        length_out = (size_t) stream.getSize();
        contents_out = (uint8_t*) malloc(length_out);

        // if this fails now, it must be because of an error and it will be set by DirectoryStream
        return stream.read(contents_out, length_out) == length_out;
    }
    else if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasInlinePayload) {
        // FIXME: offset might be incorrect due to other descriptors
        length_out = prologueHeader.length - offset;
        contents_out = (uint8_t*) malloc(length_out);

        // read Inline Payload
        if (!getBytesAt(directoryStream, pos + offset, contents_out, length_out))
            return repo->error.readError(), false;

        return true;
    }
    else {
        assert(false);
        return repo->error.repositoryCorruption("object doesn't have any kind of payload"), false;
    }
}

/*
 *  Walk the directory and look for an object named `objectName`.
 *  If found, open it as an I/O stream.
 *  If not found and `streamCreationMode` includes `kStreamCreate`, a new object will be created.
 *  If `streamCreationMode` includes `kStreamTruncate`, the returned stream (if any) will have a length of 0.
 *  `reserveLength` is a hint used to determine the initial allocation size when creating a new object.
 *
 *  If the object was not found nor created OR an error occured, nullptr is returned.
 */
ByteIO* RepositoryDirectory::openStream(const char* objectName, int streamCreationMode, uint32_t reserveLength) {
    // first of all, calculate the entry size in case we need to create a new one
    // findObjectByName will use this to remember any suitable spot to place it

    const size_t objectNameLength = strlen(objectName);
    const uint16_t prologueLength = objectEntryPrologueLength(objectNameLength);

    // figure out all the flags we will be using
    uint16_t objectEntryLength;

    int objectFlags = ObjectEntryPrologueHeader_t::kHasStreamDescr;
    objectEntryLength = prologueLength + StreamDescriptor_t::SIZE;

    // these will be useful later
    uint8_t* contents = nullptr;
    size_t contentsLength = 0;

    //
    uint64_t objectEntryPos;
    ObjectEntryPrologueHeader_t prologueHeader;

    int find = findObjectByName(objectName, objectNameLength, &objectEntryPos, &prologueHeader, objectEntryLength,
            &objectEntryPos);

    if (!find)
        return nullptr;     // error

    if (find > 0) {
        // we've found a matching object, now we need to figure out what to do with it
        uint64_t pos = objectEntryPos;
        size_t offset = ObjectEntryPrologueHeader_t::SIZE + prologueHeader.nameLength;

        if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasStreamDescr) {
            // object already has a stream, we'll reuse it
            // TODO: if the stream reserved size is laughably small, we should drop it and start anew

            // FIXME: offset might be incorrect due to other descriptors
            auto stream = new RepositoryStream(repo, directoryStream, pos + offset);

            if (streamCreationMode & kStreamTruncate)
                stream->setLength(0);

            return stream;
        }
        else if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasInlinePayload) {
            // get the existing inline payload and trash the entry (depending on its size, we'll reuse or invalidate it)

            if (!(streamCreationMode & kStreamTruncate)) {
                // FIXME: offset might be incorrect due to other descriptors
                contentsLength = prologueHeader.length - offset;
                contents = (uint8_t*) malloc(contentsLength);

                if (!getBytesAt(directoryStream, pos + offset, contents, contentsLength))
                    return repo->error.readError(), nullptr;
            }

            // calculate actual entry length to determine if it's big enough
            const uint16_t paddedObjectEntryLength = align(objectEntryLength, 16);
            const uint16_t paddedOldEntryLength = align(prologueHeader.length
                    & ObjectEntryPrologueHeader_t::kLengthMask, 16);

            if (paddedOldEntryLength < paddedObjectEntryLength) {
                // too small, invalidate and create a new one at the end of the directory
                invalidateEntryAt(pos, prologueHeader);

                objectEntryPos = directoryStream->getSize();
            }
        }
        else {
            assert(false);
            return repo->error.repositoryCorruption("object doesn't have any kind of payload"), false;
        }
    }
    else {
        if (!(streamCreationMode & kStreamCreate))
            return nullptr;
    }

    // from now on, we're creating a new object entry

    // prepare object entry
    ObjectEntryPrologueHeader_t objectPrologueHeader;
    objectPrologueHeader.length = objectEntryLength;
    objectPrologueHeader.flags = objectFlags;
    objectPrologueHeader.nameLength = objectNameLength;

    StreamDescriptor_t streamDescr;
    streamDescr.location = 0;
    streamDescr.length = 0;

    // serialize entry data
    uint8_t* entryBytes = repo->getEntryBuffer(objectEntryLength);
    size_t pos = 0;

    storeStruct(entryBytes, pos, objectPrologueHeader);
    pos += ObjectEntryPrologueHeader_t::SIZE;

    setBytesAt(entryBytes, pos, (const uint8_t*) objectName, objectNameLength);
    pos += objectNameLength;

    size_t streamDescrOffset = pos;

    storeStruct(entryBytes, pos, streamDescr);
    pos += StreamDescriptor_t::SIZE;

    // store in directory now
    if (!overwriteObjectEntryAt(objectEntryPos, entryBytes, objectEntryLength))
        return false;

    // allocate stream
    std::unique_ptr<RepositoryStream> stream(new RepositoryStream(
            repo, directoryStream, objectEntryPos + streamDescrOffset, reserveLength, reserveLength));

    if (contents) {
        // if there was an inline payload (and we're not truncating), write it into the stream now
        if (!stream->write(contents, contentsLength)) {
            free(contents);
            return nullptr;
        }

        stream->setPos(0);
        free(contents);
    }

    return stream.release();
}

/*
 *  Mark an existing entry as invalidated.
 */
bool RepositoryDirectory::invalidateEntryAt(uint64_t pos, ObjectEntryPrologueHeader_t prologueHeader) {
    size_t offset = 0;

    prologueHeader.length |= ObjectEntryPrologueHeader_t::kIsInvalidated;

    //diagnostic("invalidated %u-byte entry @ %llu\n", paddedEntryLength, pos);
    if (!storeStruct(directoryStream, pos + offset, prologueHeader))
        return repo->error.writeError(), false;

    return true;
}

/*
 *  Store an object entry, possibly overwriting a previous entry at the specified position in the directory stream.
 *  The caller must ensure that the entry doesn't own any outstanding resources (such as a stream)
 *  If the original entry is bigger, the trailing part will be correctly marked as invalidated.
 */
bool RepositoryDirectory::overwriteObjectEntryAt(uint64_t pos, const uint8_t* entryBytes, size_t entryLength) {
    const uint16_t paddedEntryLength = align(entryLength, 16);

    // retrieve original object prologue header (if any)
    bool oldEntryExists = (pos < directoryStream->getSize());

    ObjectEntryPrologueHeader_t prologueHeader;

    size_t offset = 0;

    if (oldEntryExists && !retrieveStruct(directoryStream, pos + offset, prologueHeader)) {
        return repo->error.readError(), false;
    }

    // entry data
    if (!setBytesAt(directoryStream, pos + offset, entryBytes, entryLength))
        return repo->error.writeError(), false;

    offset += entryLength;

    // padding
    if (!clearBytesAt(directoryStream, pos + offset, paddedEntryLength - entryLength))
        return repo->error.writeError(), false;

    // if there was no entry in the first place (e.g. at the end of a directory), we're done here
    if (!oldEntryExists)
        return true;

    offset += paddedEntryLength - entryLength;

    const uint16_t paddedOldEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask, 16);

    if (paddedEntryLength < paddedOldEntryLength) {
        ObjectEntryPrologueHeader_t invalidated;
        invalidated.length = (paddedOldEntryLength - paddedEntryLength) | ObjectEntryPrologueHeader_t::kIsInvalidated;
        invalidated.flags = 0;
        invalidated.nameLength = 0;

        //diagnostic("reused %u-byte entry @ %llu\n", (paddedEntryLength - paddedObjectEntryLength), pos);
        if (!storeStruct(directoryStream, pos + offset, invalidated))
            return repo->error.writeError(), false;
    }

    return true;
}

/*
 *  Set object contents, overwriting any existing entry with the same name.
 */
bool RepositoryDirectory::setObjectContents(const char* objectName, const uint8_t* contents, size_t contentsLength,
        unsigned int flags, unsigned int objectFlags) {
    // Look through directory to see if object already exists
    // If it does, replace it (reuse its stream, if any)
    // If not, build a new entry

    // first of all, calculate the entry size in case we need to create a new one
    // findObjectByName will use this to remember any suitable spot to place it
    const size_t objectNameLength = strlen(objectName);
    const uint16_t prologueLength = objectEntryPrologueLength(objectNameLength);

    // figure out all the flags we will be using
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

    //
    uint64_t objectEntryPos;
    ObjectEntryPrologueHeader_t prologueHeader;

    int find = findObjectByName(objectName, objectNameLength, &objectEntryPos, &prologueHeader, objectEntryLength,
            &objectEntryPos);

    if (!find)
        return false;           // error

    if (find > 0) {
        uint64_t pos = objectEntryPos;

        if (prologueHeader.flags & ObjectEntryPrologueHeader_t::kHasStreamDescr) {
            // object already has a stream, we'll reuse it
            // TODO: if the stream reserved size is laughably small, we should drop it and start anew

            size_t offset = ObjectEntryPrologueHeader_t::SIZE + prologueHeader.nameLength;

            // FIXME: offset might be incorrect due to other descriptors
            RepositoryStream stream(repo, directoryStream, pos + offset);

            stream.write(contents, contentsLength);
            stream.setLength(contentsLength);
            return true;
        }
        else {
            // check if object is big enough for overwriting
            // if not, invalidate it and create a new one at the end of directory

            const uint16_t paddedObjectEntryLength = align(objectEntryLength, 16);
            const uint16_t paddedOldEntryLength = align(prologueHeader.length & ObjectEntryPrologueHeader_t::kLengthMask, 16);

            if (paddedOldEntryLength < paddedObjectEntryLength) {
                // too small, invalidate and create a new one at the end of the directory
                invalidateEntryAt(pos, prologueHeader);

                objectEntryPos = directoryStream->getSize();
            }
        }
    }

    // prepare object entry
    ObjectEntryPrologueHeader_t objectPrologueHeader;
    objectPrologueHeader.length = objectEntryLength;
    objectPrologueHeader.flags = objectFlags;
    objectPrologueHeader.nameLength = objectNameLength;

    StreamDescriptor_t streamDescr;
    streamDescr.location = 0;
    streamDescr.length = 0;

    // serialize entry data
    uint8_t* entryBytes = repo->getEntryBuffer(objectEntryLength);
    size_t pos = 0;

    storeStruct(entryBytes, pos, objectPrologueHeader);
    pos += ObjectEntryPrologueHeader_t::SIZE;

    setBytesAt(entryBytes, pos, (const uint8_t*) objectName, objectNameLength);
    pos += objectNameLength;

    size_t streamDescrOffset;

    if (!useInlinePayload) {
        streamDescrOffset = pos;

        storeStruct(entryBytes, pos, streamDescr);
        pos += StreamDescriptor_t::SIZE;
    }
    else {
        setBytesAt(entryBytes, pos, contents, contentsLength);
        pos += contentsLength;
    }

    // store in directory now
    if (!overwriteObjectEntryAt(objectEntryPos, entryBytes, objectEntryLength))
        return false;

    if (!useInlinePayload) {
        RepositoryStream stream(repo, directoryStream, objectEntryPos + streamDescrOffset, contentsLength, contentsLength);
        
        if (!stream.write(contents, contentsLength))
            return false;

        //  TODO: need to make sure the entry will be already written at this point - it would overwrite the now-correct
        //        streamDescr otherwise
    }

    return true;
}
}
