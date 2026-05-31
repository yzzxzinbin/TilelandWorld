#pragma once
#ifndef TILELANDWORLD_MEMORYWRITER_H
#define TILELANDWORLD_MEMORYWRITER_H

#include <vector>
#include <string>
#include <type_traits>
#include <stdexcept>
#include <ios>
#include <cstdint>

namespace TilelandWorld {

class MemoryWriter {
public:
    MemoryWriter() = default;

    bool good() const { return true; }

    template <typename T,
              typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
    bool write(const T& data) {
        return writeBytes(reinterpret_cast<const char*>(&data), sizeof(T));
    }

    bool writeBytes(const char* data, size_t size);
    bool writeString(const std::string& str);

    std::streampos tell() const;
    bool seek(std::streampos pos);
    bool seek(std::streamoff off, std::ios_base::seekdir way);

    const std::vector<uint8_t>& buffer() const { return data; }
    std::vector<uint8_t> takeBuffer();

private:
    std::vector<uint8_t> data;
    size_t position{0};
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_MEMORYWRITER_H
