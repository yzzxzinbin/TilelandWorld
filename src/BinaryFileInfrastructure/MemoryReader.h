#pragma once
#ifndef TILELANDWORLD_MEMORYREADER_H
#define TILELANDWORLD_MEMORYREADER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <string>
#include <type_traits>
#include <vector>

namespace TilelandWorld {

class MemoryReader {
public:
    MemoryReader(const uint8_t* data, size_t size);
    explicit MemoryReader(const std::vector<uint8_t>& buffer);

    bool good() const { return true; }
    bool eof() const { return position >= dataSize; }

    template <typename T,
              typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
    bool read(T& out) {
        if (position + sizeof(T) > dataSize) return false;
        std::memcpy(&out, dataPtr + position, sizeof(T));
        position += sizeof(T);
        return true;
    }

    size_t readBytes(char* buffer, size_t size);

    std::streampos tell() const;
    bool seek(std::streampos pos);
    bool seek(std::streamoff off, std::ios_base::seekdir way);

    std::streampos fileSize() const;

private:
    const uint8_t* dataPtr{nullptr};
    size_t dataSize{0};
    size_t position{0};
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_MEMORYREADER_H
