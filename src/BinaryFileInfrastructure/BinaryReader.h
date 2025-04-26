#pragma once
#ifndef TILELANDWORLD_BINARYREADER_H
#define TILELANDWORLD_BINARYREADER_H

#include <fstream>
#include <string>
#include <vector>
#include <type_traits> // For std::is_trivially_copyable

namespace TilelandWorld {

    class BinaryReader {
    public:
        // 构造函数：打开指定文件用于二进制读取。
        explicit BinaryReader(const std::string& filepath);

        // 析构函数：关闭文件流。
        ~BinaryReader();

        // 检查流是否处于良好状态 (包括是否到达文件末尾)。
        bool good() const;
        // 检查是否到达文件末尾。
        bool eof() const;

        // 读取一个 POD (Plain Old Data) 类型的数据。
        // 使用 SFINAE 约束 T 必须是 trivially copyable。
        template <typename T,
                  typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
        bool read(T& data) {
            if (!stream.good() || stream.peek() == EOF) return false; // 检查是否可读
            stream.read(reinterpret_cast<char*>(&data), sizeof(T));
            // 检查读取操作是否成功以及读取的字节数是否足够
            return stream.good() || (stream.eof() && stream.gcount() == sizeof(T));
        }

        // 读取指定大小的原始字节数据。
        // 返回实际读取的字节数。
        size_t readBytes(char* buffer, size_t size);

        // 读取 std::string (先读取长度，再读取字符数据)。
        bool readString(std::string& str);

        // 获取当前读取位置。
        std::streampos tell();

        // 移动读取位置。
        bool seek(std::streampos pos);
        bool seek(std::streamoff off, std::ios_base::seekdir way);

        // 获取文件大小。
        std::streampos fileSize();

    private:
        std::ifstream stream;
        std::string filepath;
        std::streampos streamSize = -1; // 缓存文件大小

        // 禁用拷贝构造和赋值
        BinaryReader(const BinaryReader&) = delete;
        BinaryReader& operator=(const BinaryReader&) = delete;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_BINARYREADER_H
