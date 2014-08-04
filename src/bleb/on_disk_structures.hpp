#pragma once

#include <cstdint>
#include <cstring>

namespace bleb {

static const uint8_t prologueMagic[4] = {0x089, 'b', 'l', 'e'};

struct RepositoryPrologue_t {
    enum { SIZE = 8 };

    uint8_t magic[4];
    uint32_t formatVersion;
};

struct StreamDescriptor_t {
    enum { SIZE = 16 };

    uint64_t location;
    uint64_t length;
};

struct SpanHeader_t {
    enum { SIZE = 16 };

    uint32_t reservedLength;
    uint32_t usedLength;
    uint64_t nextSpanLocation;
};

struct ObjectEntryPrologueHeader_t {
    enum { SIZE = 4 };

    uint16_t flags;
    uint16_t nameLength;
};

inline unsigned int objectEntryPrologueLength(size_t nameLength) {
    assert(nameLength < 0x7fff);        // FIXME

    unsigned int length = ObjectEntryPrologueHeader_t::SIZE + (unsigned int) nameLength;
    length = (length + 15) & ~15;       // round up to 16-byte boundary
    return length;
}

template <typename T> inline void serializeLE(T value, uint8_t*& value_bytes) {
    // FIXME: Big-Endian support
    memcpy(value_bytes, &value, sizeof(T));
    value_bytes += sizeof(T);
}

template <typename T> inline void deserializeLE(T& value_out, const uint8_t*& value_bytes) {
    // FIXME: Big-Endian support
    memcpy(&value_out, value_bytes, sizeof(T));
    value_bytes += sizeof(T);
}

static void deserialize(RepositoryPrologue_t& s, const uint8_t* buffer) {
    memcpy(s.magic, buffer, 4);
    buffer += 4;

    deserializeLE(s.formatVersion, buffer);
}

static void serialize(const RepositoryPrologue_t& s, uint8_t* buffer) {
    memcpy(buffer, s.magic, 4);
    buffer += 4;

    serializeLE(s.formatVersion, buffer);
}

static void deserialize(StreamDescriptor_t& s, const uint8_t* buffer) {
    deserializeLE(s.location, buffer);
    deserializeLE(s.length, buffer);
}

static void serialize(const StreamDescriptor_t& s, uint8_t* buffer) {
    serializeLE(s.location, buffer);
    serializeLE(s.length, buffer);
}

static void deserialize(SpanHeader_t& s, const uint8_t* buffer) {
    deserializeLE(s.reservedLength, buffer);
    deserializeLE(s.usedLength, buffer);
    deserializeLE(s.nextSpanLocation, buffer);
}

static void serialize(const SpanHeader_t& s, uint8_t* buffer) {
    serializeLE(s.reservedLength, buffer);
    serializeLE(s.usedLength, buffer);
    serializeLE(s.nextSpanLocation, buffer);
}
}
