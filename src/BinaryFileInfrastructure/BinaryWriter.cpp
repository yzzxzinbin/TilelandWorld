#include "BinaryWriter.h"
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <stdexcept> // For potential exceptions

namespace TilelandWorld {

    BinaryWriter::BinaryWriter(const std::string& filepath) : filepath(filepath) {
        // 启用异常：当 failbit 或 badbit 被设置时，流将抛出 std::ios_base::failure
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        try {
            stream.open(filepath, std::ios::binary | std::ios::trunc); // 二进制模式，覆盖写入
        } catch (const std::ios_base::failure& e) {
            // 包装底层异常
            throw std::runtime_error("BinaryWriter: Failed to open file for writing: " + filepath + " - " + e.what());
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
        if (!data || size == 0) return true; // 写入 0 字节总是成功
        // write 操作现在会在失败时抛出异常
        try {
            stream.write(data, size);
            return true; // 如果没有抛出异常，则写入成功
        } catch (const std::ios_base::failure& e) {
            // 包装异常，不再记录日志
            throw std::runtime_error("BinaryWriter::writeBytes failed: " + std::string(e.what()));
        }
    }

    bool BinaryWriter::writeString(const std::string& str) {
        // 1. 写入字符串长度 (例如使用 size_t)
        size_t len = str.length();
        try {
            write(len); // 直接调用，让异常传播
            // 2. 写入字符串数据 (如果长度大于0)
            if (len > 0) {
                writeBytes(str.c_str(), len); // 直接调用，让异常传播
            }
            return true; // 如果没有抛出异常，则成功
        } catch (const std::runtime_error& e) { // 捕获来自 write<T> 或 writeBytes 的异常
             throw; // 重新抛出，让上层处理
        } catch (const std::ios_base::failure& e) { // 以防万一捕获底层 I/O 错误
             throw std::runtime_error("BinaryWriter::writeString failed (ios_base::failure): " + std::string(e.what()));
        }
    }

    std::streampos BinaryWriter::tell() {
        return stream.tellp();
    }

    bool BinaryWriter::seek(std::streampos pos) {
         stream.clear(); // 清除可能的状态位
         try {
             stream.seekp(pos);
             return !stream.fail();
         } catch (const std::ios_base::failure& e) {
             LOG_ERROR("BinaryWriter::seek failed: " + std::string(e.what()));
             return false;
         }
    }

    bool BinaryWriter::seek(std::streamoff off, std::ios_base::seekdir way) {
         stream.clear();
         try {
             stream.seekp(off, way);
             return !stream.fail();
         } catch (const std::ios_base::failure& e) {
             LOG_ERROR("BinaryWriter::seek failed: " + std::string(e.what()));
             return false;
         }
    }

} // namespace TilelandWorld
