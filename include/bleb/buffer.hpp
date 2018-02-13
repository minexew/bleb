#ifndef bleb_buffer_hpp
#define bleb_buffer_hpp

#include <cstdlib>
#include <memory>

namespace bleb {

// Faster than std::vector if you don't need to track size and capacity separately
template<typename T>
class Buffer {
public:
    Buffer() : memory(nullptr), capacity(0) {
    }

    Buffer(size_t capacity) : memory(nullptr), capacity(capacity) {
        if (capacity > 0) {
            this->memory = reinterpret_cast<T*>(malloc(capacity * sizeof(T)));
            if (!this->memory)
                throw std::bad_alloc();
        }
    }

    ~Buffer() {
        free(this->memory);
    }

    void reserve(size_t requiredCapacity) {
        if (requiredCapacity > capacity) {
            T* newMemory = reinterpret_cast<T*>(realloc(memory, requiredCapacity * sizeof(T)));
            if (!newMemory)
                throw std::bad_alloc();

            this->memory = newMemory;
            this->capacity = requiredCapacity;
        }
    }

    T& operator[](size_t index) {
        return memory[index];
    }

private:
    T* memory;
    size_t capacity;
};

}

#endif
