#include "MemoryReader.h"

#include <algorithm>

namespace TilelandWorld {

MemoryReader::MemoryReader(const uint8_t* data, size_t size)
    : dataPtr(data), dataSize(size), position(0) {}

MemoryReader::MemoryReader(const std::vector<uint8_t>& buffer)
    : MemoryReader(buffer.data(), buffer.size()) {}

size_t MemoryReader::readBytes(char* buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    if (position >= dataSize) return 0;

    size_t remaining = dataSize - position;
    size_t toRead = std::min(size, remaining);
    std::memcpy(buffer, dataPtr + position, toRead);
    position += toRead;
    return toRead;
}

std::streampos MemoryReader::tell() const {
    return static_cast<std::streampos>(position);
}

bool MemoryReader::seek(std::streampos pos) {
    std::streamoff off = static_cast<std::streamoff>(pos);
    if (off < 0) return false;
    size_t newPos = static_cast<size_t>(off);
    if (newPos > dataSize) return false;
    position = newPos;
    return true;
}

bool MemoryReader::seek(std::streamoff off, std::ios_base::seekdir way) {
    std::streamoff base = 0;
    if (way == std::ios_base::beg) {
        base = 0;
    } else if (way == std::ios_base::cur) {
        base = static_cast<std::streamoff>(position);
    } else if (way == std::ios_base::end) {
        base = static_cast<std::streamoff>(dataSize);
    } else {
        return false;
    }

    std::streamoff target = base + off;
    if (target < 0) return false;
    size_t newPos = static_cast<size_t>(target);
    if (newPos > dataSize) return false;
    position = newPos;
    return true;
}

std::streampos MemoryReader::fileSize() const {
    return static_cast<std::streampos>(dataSize);
}

} // namespace TilelandWorld
