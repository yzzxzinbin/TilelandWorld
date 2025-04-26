#include "BinaryWriter.h"
#include <stdexcept> // For potential exceptions

namespace TilelandWorld {

    BinaryWriter::BinaryWriter(const std::string& filepath) : filepath(filepath) {
        stream.open(filepath, std::ios::binary | std::ios::trunc); // 二进制模式，覆盖写入
        if (!stream.is_open()) {
            throw std::runtime_error("BinaryWriter: Failed to open file for writing: " + filepath);
        }
    }

    BinaryWriter::~BinaryWriter() {
        if (stream.is_open()) {
            stream.close();
        }
    }

    bool BinaryWriter::good() const {
        return stream.good();
    }

    bool BinaryWriter::writeBytes(const char* data, size_t size) {
        if (!stream.good() || !data || size == 0) return false;
        stream.write(data, size);
        return stream.good();
    }

    bool BinaryWriter::writeString(const std::string& str) {
        if (!stream.good()) return false;
        // 1. 写入字符串长度 (例如使用 size_t)
        size_t len = str.length();
        if (!write(len)) { // 使用模板化的 write 方法写入长度
            return false;
        }
        // 2. 写入字符串数据 (如果长度大于0)
        if (len > 0) {
            if (!writeBytes(str.c_str(), len)) {
                return false;
            }
        }
        return stream.good();
    }

    std::streampos BinaryWriter::tell() {
        return stream.tellp();
    }

    bool BinaryWriter::seek(std::streampos pos) {
         if (!stream.good()) return false;
         stream.seekp(pos);
         return stream.good();
    }

    bool BinaryWriter::seek(std::streamoff off, std::ios_base::seekdir way) {
         if (!stream.good()) return false;
         stream.seekp(off, way);
         return stream.good();
    }

} // namespace TilelandWorld
