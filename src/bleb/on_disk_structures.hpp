#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>

namespace bleb {

static const uint8_t prologueMagic[7] = {0x89, 'b', 'l', 'e', 'b', '\r', '\n'};

struct RepositoryPrologue_t {
    enum { SIZE = 16 };
    enum { FORMAT_VERSION_1 = 0x01 };

    uint8_t magic[7];
    uint8_t formatVersion;
    uint32_t flags;
    uint32_t infoFlags;
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
    enum { SIZE = 6 };

    enum {
        LENGTH_MASK = 0x7FFF,
        IS_INVALIDATED = 0x8000,
    };

    enum {
        IS_DIRECTORY = 0x0001,
        HAS_STREAM_DESCR = 0x0002,
        HAS_STORAGE_DESCR = 0x0004,
        HAS_HASH128 = 0x0008,
        HAS_INLINE_PAYLOAD = 0x0010,
        IS_TEXT = 0x1001
    };

    uint16_t length;
    uint16_t flags;
    uint16_t nameLength;
};

template <typename T, typename T2> T align(T value, T2 alignment) {
    assert(alignment && !(alignment & (alignment - 1)));

    return (value + alignment - 1) & ~(alignment - 1);
}

inline unsigned int objectEntryPrologueLength(size_t nameLength) {
    assert(nameLength < 0x7fff);        // FIXME

    unsigned int length = ObjectEntryPrologueHeader_t::SIZE + (unsigned int) nameLength;
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
    memcpy(s.magic, buffer, sizeof(s.magic));
    buffer += sizeof(s.magic);

    deserializeLE(s.formatVersion, buffer);
    deserializeLE(s.flags, buffer);
    deserializeLE(s.infoFlags, buffer);
}

static void serialize(const RepositoryPrologue_t& s, uint8_t* buffer) {
    memcpy(buffer, s.magic, sizeof(s.magic));
    buffer += sizeof(s.magic);

    serializeLE(s.formatVersion, buffer);
    serializeLE(s.flags, buffer);
    serializeLE(s.infoFlags, buffer);
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

static void deserialize(ObjectEntryPrologueHeader_t& s, const uint8_t* buffer) {
    deserializeLE(s.length, buffer);
    deserializeLE(s.flags, buffer);
    deserializeLE(s.nameLength, buffer);
}

static void serialize(const ObjectEntryPrologueHeader_t& s, uint8_t* buffer) {
    serializeLE(s.length, buffer);
    serializeLE(s.flags, buffer);
    serializeLE(s.nameLength, buffer);
}
}
