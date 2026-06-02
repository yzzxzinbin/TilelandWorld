#pragma once
#ifndef TILELANDWORLD_BINARYWRITER_H
#define TILELANDWORLD_BINARYWRITER_H

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <type_traits> // For std::is_trivially_copyable
#include <stdexcept> // For std::runtime_error
#include "../Utils/Logger.h" // <-- 包含 Logger

namespace TilelandWorld {

    class BinaryWriter {
    public:
        // 构造函数：打开指定文件用于二进制写入。
        // 如果文件已存在，默认会覆盖。
        // atomic = false: 直接打开目标文件（默认，向后兼容）
        // atomic = true:  写入临时文件 .tmp，调用 commit() 后原子替换为目标文件
        explicit BinaryWriter(const std::string& filepath, bool atomic = false);

        // 析构函数：关闭文件流。
        ~BinaryWriter();

        // 检查流是否处于良好状态。
        bool good() const;

        // 原子模式：flush -> close -> 原子替换临时文件为目标文件。
        // 非原子模式：flush + close。
        // 返回 true 表示提交成功；失败时自动清理临时文件。
        bool commit();

        // 写入一个 POD (Plain Old Data) 类型的数据。
        // 使用 SFINAE 约束 T 必须是 trivially copyable。
        // 现在流会因 failbit/badbit 抛出异常。
        template <typename T,
                  typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
        bool write(const T& data) {
            try {
                stream.write(reinterpret_cast<const char*>(&data), sizeof(T));
                return true; // 成功（无异常）
            } catch (const std::ios_base::failure& e) {
                // 包装异常
                throw std::runtime_error("BinaryWriter::write<T> failed: " + std::string(e.what()));
            }
        }

        // 写入原始字节数据。
        bool writeBytes(const char* data, size_t size);

        // 写入 std::string (通常先写入长度，再写入字符数据)。
        bool writeString(const std::string& str);

        // 获取当前写入位置。
        std::streampos tell();

        // 移动写入位置。
        bool seek(std::streampos pos);
        bool seek(std::streamoff off, std::ios_base::seekdir way);

    private:
        std::ofstream stream;
        std::string targetPath;   // 最终目标路径
        std::string tempPath;     // 临时文件路径（仅 atomic 模式）
        bool atomicMode;
        bool committed;
        bool hasError;

        // 清理残留的临时文件
        void cleanupTemp() noexcept;

        // 跨平台原子替换：from -> to
        static bool atomicReplace(const std::string& from, const std::string& to);

        // 禁用拷贝构造和赋值
        BinaryWriter(const BinaryWriter&) = delete;
        BinaryWriter& operator=(const BinaryWriter&) = delete;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_BINARYWRITER_H
