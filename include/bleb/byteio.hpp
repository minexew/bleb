#pragma once

#include <cstdint>

namespace bleb {

class ByteIO {
public:
    virtual void close() {}
    virtual uint64_t getSize() = 0;
    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, uint64_t count) = 0;
    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, uint64_t count) = 0;
    virtual bool clearBytesAt(uint64_t pos, uint64_t count) = 0;
};

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

}
