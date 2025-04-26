#include "BinaryReader.h"
#include <stdexcept> // For potential exceptions
#include <vector>    // For reading string

namespace TilelandWorld {

    BinaryReader::BinaryReader(const std::string& filepath) : filepath(filepath) {
        stream.open(filepath, std::ios::binary | std::ios::ate); // 二进制模式，初始定位到末尾以获取大小
        if (!stream.is_open()) {
            throw std::runtime_error("BinaryReader: Failed to open file for reading: " + filepath);
        }
        streamSize = stream.tellg(); // 获取文件大小
        stream.seekg(0, std::ios::beg); // 重置到文件开头
    }

    BinaryReader::~BinaryReader() {
        if (stream.is_open()) {
            stream.close();
        }
    }

    bool BinaryReader::good() const {
        // good() 检查 failbit 和 badbit，但不检查 eofbit
        return stream.good();
    }

     bool BinaryReader::eof() const {
        return stream.eof();
     }

    size_t BinaryReader::readBytes(char* buffer, size_t size) {
        if (!stream.good() || !buffer || size == 0 || stream.peek() == EOF) return 0;
        stream.read(buffer, size);
        // 返回实际读取的字节数
        return static_cast<size_t>(stream.gcount());
    }

    bool BinaryReader::readString(std::string& str) {
        if (!stream.good() || stream.peek() == EOF) return false;
        // 1. 读取字符串长度
        size_t len = 0;
        if (!read(len)) { // 使用模板化的 read 方法读取长度
             // 如果连长度都读不了（比如文件结尾），则失败
            return false;
        }

        if (len == 0) {
            str.clear();
            return true; // 空字符串读取成功
        }

        // 检查请求的长度是否超过剩余文件大小 (简单检查)
        std::streampos currentPos = tell();
        if (currentPos != -1 && streamSize != -1 &&
            static_cast<std::streamoff>(len) > (streamSize - currentPos)) {
             stream.setstate(std::ios::failbit); // 设置失败状态
             return false; // 数据不足
        }


        // 2. 读取字符串数据
        // 使用 vector 作为临时缓冲区更安全
        std::vector<char> buffer(len);
        size_t bytesRead = readBytes(buffer.data(), len);

        if (bytesRead == len) {
            str.assign(buffer.data(), len);
            return true;
        } else {
            // 读取的字节数不足，说明文件提前结束或发生错误
            return false;
        }
    }

     std::streampos BinaryReader::tell() {
        return stream.tellg();
    }

    bool BinaryReader::seek(std::streampos pos) {
         if (!stream.good() && !stream.eof()) return false; // eof 时允许 seek
         stream.clear(); // 清除可能的 eof 状态位，以便 seek
         stream.seekg(pos);
         return stream.good();
    }

    bool BinaryReader::seek(std::streamoff off, std::ios_base::seekdir way) {
         if (!stream.good() && !stream.eof()) return false;
         stream.clear();
         stream.seekg(off, way);
         return stream.good();
    }

    std::streampos BinaryReader::fileSize() {
        return streamSize;
    }


} // namespace TilelandWorld
