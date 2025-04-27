#include "BinaryReader.h"
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <stdexcept> // For potential exceptions
#include <vector>    // For reading string

namespace TilelandWorld {

    BinaryReader::BinaryReader(const std::string& filepath) : filepath(filepath) {
        // 启用异常：当 failbit 或 badbit 被设置时，流将抛出 std::ios_base::failure
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        try {
            stream.open(filepath, std::ios::binary | std::ios::ate); // 二进制模式，初始定位到末尾以获取大小
            streamSize = stream.tellg(); // 获取文件大小
            stream.seekg(0, std::ios::beg); // 重置到文件开头
        } catch (const std::ios_base::failure& e) {
            // 包装底层异常，提供更清晰的上下文
            throw std::runtime_error("BinaryReader: Failed to open or setup file for reading: " + filepath + " - " + e.what());
        }
    }

    BinaryReader::~BinaryReader() {
        if (stream.is_open()) {
            stream.close();
        }
    }

    bool BinaryReader::good() const {
        // good() 仍然有用，但异常机制提供了更主动的错误处理
        return stream.good();
    }

    bool BinaryReader::eof() const {
        // eof() 检查 eofbit，它通常不包含在 exceptions() 中，所以需要单独检查
        return stream.eof();
    }

    size_t BinaryReader::readBytes(char* buffer, size_t size) {
        if (!buffer || size == 0) return 0;
        // 在读取前检查 EOF，因为 peek() 不会设置 failbit/badbit
        if (stream.peek() == EOF) {
            // 如果已经到文件末尾，不尝试读取，返回 0
            // 注意：如果文件为空，第一次 peek 也是 EOF
            return 0;
        }
        try {
            stream.read(buffer, size);
            return static_cast<size_t>(stream.gcount()); // 返回实际读取的字节数
        } catch (const std::ios_base::failure& e) {
            if (!stream.eof()) {
                // 重新包装异常，不再记录日志，让上层处理
                throw std::runtime_error("BinaryReader::readBytes failed: " + std::string(e.what()));
            }
            // 到达 EOF，返回实际读取的字节数 (正常情况)
            return static_cast<size_t>(stream.gcount());
        }
    }

    bool BinaryReader::readString(std::string& str) {
        // 1. 读取字符串长度
        size_t len = 0;
        try {
            if (!read(len)) {
                return false; // 无法读取长度
            }
        } catch (const std::ios_base::failure& e) { // <-- Catch specific I/O failure first
            LOG_ERROR("BinaryReader::readString failed to read length (ios_base::failure): " + std::string(e.what()));
            return false;
        } catch (const std::runtime_error& e) { // <-- Catch more general runtime_error later
            LOG_ERROR("BinaryReader::readString failed to read length: " + std::string(e.what()));
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
            LOG_ERROR("BinaryReader::readString requested length " + std::to_string(len) + " exceeds remaining file size.");
            return false; // 数据不足
        }

        // 2. 读取字符串数据
        std::vector<char> buffer(len);
        size_t bytesRead = 0;
        try {
            bytesRead = readBytes(buffer.data(), len);
        } catch (const std::runtime_error& e) {
            LOG_ERROR("BinaryReader::readString failed during readBytes: " + std::string(e.what()));
            return false;
        }

        if (bytesRead == len) {
            str.assign(buffer.data(), len);
            return true;
        } else {
            LOG_ERROR("BinaryReader::readString failed: unexpected EOF. Expected " + std::to_string(len) + " bytes, got " + std::to_string(bytesRead));
            return false;
        }
    }

    std::streampos BinaryReader::tell() {
        return stream.tellg();
    }

    bool BinaryReader::seek(std::streampos pos) {
        // 清除状态位，特别是 eofbit，否则 seekg 可能失败
        stream.clear();
        try {
            stream.seekg(pos);
            return !stream.fail(); // 检查 seekg 是否成功
        } catch (const std::ios_base::failure& e) {
            LOG_ERROR("BinaryReader::seek failed: " + std::string(e.what()));
            return false;
        }
    }

    bool BinaryReader::seek(std::streamoff off, std::ios_base::seekdir way) {
        stream.clear();
        try {
            stream.seekg(off, way);
            return !stream.fail();
        } catch (const std::ios_base::failure& e) {
            LOG_ERROR("BinaryReader::seek failed: " + std::string(e.what()));
            return false;
        }
    }

    std::streampos BinaryReader::fileSize() {
        return streamSize;
    }

} // namespace TilelandWorld
