#pragma once

#include <cstdint>
#include <cstring>

namespace bleb {

class ByteIO {
public:
    virtual ~ByteIO() {}
    virtual void close() {}
    virtual uint64_t getSize() = 0;
    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, uint64_t count) = 0;
    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, uint64_t count) = 0;
    virtual bool clearBytesAt(uint64_t pos, uint64_t count) = 0;
};

// ByteIO

inline bool getBytesAt(ByteIO* io, uint64_t pos, uint8_t* buffer, uint64_t count) {
    return io->getBytesAt(pos, buffer, count);
}

inline bool setBytesAt(ByteIO* io, uint64_t pos, const uint8_t* buffer, uint64_t count) {
    return io->setBytesAt(pos, buffer, count);
}

inline bool clearBytesAt(ByteIO* io, uint64_t pos, uint64_t count) {
    return io->clearBytesAt(pos, count);
}

template <class Struct>
bool retrieveStruct(ByteIO* io, uint64_t pos, Struct& st) {
    uint8_t buffer[Struct::SIZE];

    if (!getBytesAt(io, pos, buffer, sizeof(buffer)))
        return false;

    deserialize(st, buffer);
    return true;
}

template <class Struct>
bool storeStruct(ByteIO* io, uint64_t pos, const Struct& st) {
    uint8_t buffer[Struct::SIZE];

    serialize(st, buffer);
    return setBytesAt(io, pos, buffer, sizeof(buffer));
}

// raw memory

inline bool getBytesAt(const uint8_t* bytes, uint64_t pos, uint8_t* buffer, uint64_t count) {
    memcpy(buffer, bytes + pos, count);
    return true;
}

inline bool setBytesAt(uint8_t* bytes, uint64_t pos, const uint8_t* buffer, uint64_t count) {
    memcpy(bytes + pos, buffer, count);
    return true;
}

inline bool clearBytesAt(uint8_t* bytes, uint64_t pos, uint64_t count) {
    memset(bytes + pos, 0, count);
    return true;
}

template <class Struct>
inline bool retrieveStruct(const uint8_t* bytes, uint64_t pos, Struct& st) {
    deserialize(st, bytes + pos);
    return true;
}

template <class Struct>
inline bool storeStruct(uint8_t* bytes, uint64_t pos, const Struct& st) {
    serialize(st, bytes + pos);
    return true;
}

}
