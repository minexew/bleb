#include <bleb/byteio.hpp>
#include <bleb/repository.hpp>

#include <cassert>

static uint64_t bioReadBytes = 0,
    bioReadOps = 0,
    bioWrittenBytes = 0,
    bioWrittenOps = 0;

class MyByteIO : public bleb::ByteIO {
public:
    MyByteIO(FILE* file) : file(file) {}

    virtual uint64_t getSize() override {
        fseek(file, 0, SEEK_END);
        return ftell(file);
    }

    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, uint64_t count) override {
        fseek(file, pos, SEEK_SET);
        assert(fread(buffer, 1, count, file) == count);
        return true;
    }

    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, uint64_t count) override {
        fseek(file, pos, SEEK_SET);
        assert(fwrite(buffer, 1, count, file) == count);
        return true;
    }

    virtual bool clearBytesAt(uint64_t pos, uint64_t count) override {
        static const uint8_t empty[256] = {};

        while (count > sizeof(empty)) {
            if (!setBytesAt(pos, empty, sizeof(empty)))
                return false;

            pos += sizeof(empty);
            count -= sizeof(empty);
        }

        if (!setBytesAt(pos, empty, count))
            return false;

        return true;
    }

    FILE* file;
};

FILE* getFile(const char* path) {
    FILE* f = fopen(path, "rb+");

    if (f == nullptr)
        f = fopen(path, "wb+");

    return f;
}

int main() {
    MyByteIO bio(getFile("new.repo"));
    bleb::Repository repo(&bio, true);
    repo.open();
}
