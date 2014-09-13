#pragma once

#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include "on_disk_structures.hpp"

namespace bleb {
class RepositoryStream : public ByteIO {
public:
    RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos);
    RepositoryStream(Repository* repo, ByteIO* streamDescrIO, uint64_t streamDescrPos, uint32_t reserveLength,
            uint64_t expectedSize);
    virtual ~RepositoryStream();

    virtual uint64_t getSize() override {
        return descr.length;
    }

    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, size_t count) override {
        return setPos(pos), read(buffer, count) == count;
    }

    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, size_t count) override {
        return setPos(pos), write(buffer, count) == count;
    }

    virtual bool clearBytesAt(uint64_t pos, uint64_t count) override;

    uint64_t getPos() {
        return pos;
    }

    void setPos(uint64_t pos);

    size_t read(void* buffer_in, size_t length);
    size_t write(const void* buffer_in, size_t length);

private:
    bool gotoRightSpan();
    void setCurrentSpan(const SpanHeader_t& span, uint64_t spanLocation, uint64_t spanPosInStream);

    Repository* repo;
    ByteIO* io;
    bool isReadOnly;

    ByteIO* descrIO;
    uint64_t descrPos;
    StreamDescriptor_t descr;
    bool descrDirty;

    uint64_t pos;

    bool haveCurrentSpan;
    SpanHeader_t firstSpan, currentSpan;
    uint64_t currentSpanLocation, currentSpanPosInStream;
    uint32_t posInCurrentSpan;
};
}
