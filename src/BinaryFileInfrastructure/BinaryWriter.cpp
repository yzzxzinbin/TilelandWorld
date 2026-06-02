#include "BinaryWriter.h"
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <stdexcept> // For potential exceptions
#include <filesystem>

namespace TilelandWorld {

    void BinaryWriter::cleanupTemp() noexcept {
        if (!tempPath.empty()) {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
        }
    }

    bool BinaryWriter::atomicReplace(const std::string& from, const std::string& to) {
        std::error_code ec;
#ifdef _WIN32
        // Windows 下 rename 不能覆盖已存在文件，先删除目标
        if (std::filesystem::exists(to, ec)) {
            std::filesystem::remove(to, ec);
        }
#endif
        std::filesystem::rename(from, to, ec);
        return !ec;
    }

    BinaryWriter::BinaryWriter(const std::string& filepath, bool atomic)
        : targetPath(filepath), atomicMode(atomic), committed(false), hasError(false)
    {
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        try {
            if (atomicMode) {
                tempPath = filepath + ".tmp";
                cleanupTemp(); // 清理上一次残留
                stream.open(tempPath, std::ios::binary | std::ios::trunc);
            } else {
                stream.open(filepath, std::ios::binary | std::ios::trunc);
            }
        } catch (const std::ios_base::failure& e) {
            hasError = true;
            throw std::runtime_error("BinaryWriter: Failed to open file for writing: "
                + (atomicMode ? tempPath : filepath) + " - " + e.what());
        }
    }

    BinaryWriter::~BinaryWriter() {
        if (stream.is_open()) {
            try { stream.close(); } catch (...) {}
        }
        if (atomicMode && !committed) {
            cleanupTemp(); // 未提交则回滚
        }
    }

    bool BinaryWriter::commit() {
        if (hasError || !stream.is_open()) return false;
        try {
            stream.flush();
            stream.close();
        } catch (...) {
            cleanupTemp();
            return false;
        }
        if (atomicMode) {
            if (!atomicReplace(tempPath, targetPath)) {
                cleanupTemp();
                return false;
            }
        }
        committed = true;
        return true;
    }

    bool BinaryWriter::good() const {
        return stream.good();
    }

    bool BinaryWriter::writeBytes(const char* data, size_t size) {
        if (!data || size == 0) return true;
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
        } catch (const std::ios_base::failure& e) { // <-- Catch specific I/O failure first
             throw std::runtime_error("BinaryWriter::writeString failed (ios_base::failure): " + std::string(e.what()));
        } catch (const std::runtime_error& e) { // <-- Catch more general runtime_error later
             throw; // 重新抛出，让上层处理
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
