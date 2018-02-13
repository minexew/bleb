#pragma once

#include <bleb/byteio.hpp>

#include <cstdint>
#include <vector>

namespace bleb {
class VectorByteIO : public ByteIO {
public:
    VectorByteIO(size_t reserveSize, bool allowExpansion) : allowExpansion(allowExpansion) {
        bytes.reserve(reserveSize);
    }

    uint64_t getSize() override {
        return bytes.size();
    }

    bool getBytesAt(uint64_t pos, uint8_t* buffer, size_t count) override {
        if (pos + count > bytes.size())
            return false;

        memcpy(buffer, &bytes[pos], count);
        return true;
    }

    bool setBytesAt(uint64_t pos, const uint8_t* buffer, size_t count) override {
        if (pos + count > bytes.size()) {
            if (pos + count <= bytes.capacity() || allowExpansion)
                bytes.resize(pos + count);
            else
                return false;
        }

        memcpy(&bytes[pos], buffer, count);
        return true;
    }

    bool clearBytesAt(uint64_t pos, uint64_t count) override {
        if (pos + count > bytes.size()) {
            if (pos + count <= bytes.capacity() || allowExpansion)
                bytes.resize(pos + count);
            else
                return false;
        }

        memset(&bytes[pos], 0, count);
        return true;
    }

private:
    std::vector<uint8_t> bytes;
    bool allowExpansion;
};
}
