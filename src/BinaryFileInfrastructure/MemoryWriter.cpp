#include "MemoryWriter.h"

#include <cstring>
#include <limits>

namespace TilelandWorld {

bool MemoryWriter::writeBytes(const char* dataPtr, size_t size) {
    if (size == 0) return true;
    if (!dataPtr) {
        throw std::runtime_error("MemoryWriter::writeBytes received null data.");
    }
    if (position > std::numeric_limits<size_t>::max() - size) {
        throw std::runtime_error("MemoryWriter::writeBytes size overflow.");
    }
    size_t end = position + size;
    if (end > data.size()) {
        data.resize(end, 0);
    }
    std::memcpy(data.data() + position, dataPtr, size);
    position = end;
    return true;
}

bool MemoryWriter::writeString(const std::string& str) {
    size_t len = str.length();
    write(len);
    if (len > 0) {
        writeBytes(str.data(), len);
    }
    return true;
}

std::streampos MemoryWriter::tell() const {
    return static_cast<std::streampos>(position);
}

bool MemoryWriter::seek(std::streampos pos) {
    std::streamoff off = static_cast<std::streamoff>(pos);
    if (off < 0) return false;
    size_t newPos = static_cast<size_t>(off);
    if (newPos > data.size()) {
        data.resize(newPos, 0);
    }
    position = newPos;
    return true;
}

bool MemoryWriter::seek(std::streamoff off, std::ios_base::seekdir way) {
    std::streamoff base = 0;
    if (way == std::ios_base::beg) {
        base = 0;
    } else if (way == std::ios_base::cur) {
        base = static_cast<std::streamoff>(position);
    } else if (way == std::ios_base::end) {
        base = static_cast<std::streamoff>(data.size());
    } else {
        return false;
    }

    std::streamoff target = base + off;
    if (target < 0) return false;
    size_t newPos = static_cast<size_t>(target);
    if (newPos > data.size()) {
        data.resize(newPos, 0);
    }
    position = newPos;
    return true;
}

std::vector<uint8_t> MemoryWriter::takeBuffer() {
    position = 0;
    return std::move(data);
}

} // namespace TilelandWorld
