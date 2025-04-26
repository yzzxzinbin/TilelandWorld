#pragma once
#ifndef TILELANDWORLD_BINARYWRITER_H
#define TILELANDWORLD_BINARYWRITER_H

#include <fstream>
#include <string>
#include <vector>
#include <type_traits> // For std::is_trivially_copyable

namespace TilelandWorld {

    class BinaryWriter {
    public:
        // 构造函数：打开指定文件用于二进制写入。
        // 如果文件已存在，默认会覆盖。
        explicit BinaryWriter(const std::string& filepath);

        // 析构函数：关闭文件流。
        ~BinaryWriter();

        // 检查流是否处于良好状态。
        bool good() const;

        // 写入一个 POD (Plain Old Data) 类型的数据。
        // 使用 SFINAE 约束 T 必须是 trivially copyable。
        template <typename T,
                  typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
        bool write(const T& data) {
            if (!stream.good()) return false;
            stream.write(reinterpret_cast<const char*>(&data), sizeof(T));
            return stream.good();
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
        std::string filepath;

        // 禁用拷贝构造和赋值
        BinaryWriter(const BinaryWriter&) = delete;
        BinaryWriter& operator=(const BinaryWriter&) = delete;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_BINARYWRITER_H
