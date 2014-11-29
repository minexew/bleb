#pragma once

#include <bleb/byteio.hpp>

#include <cassert>
#include <cstdio>

// FIXME: replace assert

namespace bleb {
class StdioFileByteIO : public ByteIO {
public:
	static FILE* getFile(const char* path, bool canCreateNew) {
	    FILE* f = fopen(path, "rb+");

	    if (f == nullptr && canCreateNew)
	        f = fopen(path, "wb+");

	    return f;
	}

    StdioFileByteIO(FILE* file, bool close) : file(file), close(close) {}

 	~StdioFileByteIO() {
 		if (close)
 			fclose(file);
 	}

    virtual uint64_t getSize() override {
        fseek(file, 0, SEEK_END);
        return ftell(file);
    }

    virtual bool getBytesAt(uint64_t pos, uint8_t* buffer, size_t count) override {
        fseek(file, pos, SEEK_SET);
        return (fread(buffer, 1, count, file) == count);
    }

    virtual bool setBytesAt(uint64_t pos, const uint8_t* buffer, size_t count) override {
        fseek(file, pos, SEEK_SET);
        return (fwrite(buffer, 1, count, file) == count);
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
    bool close;
};
}
