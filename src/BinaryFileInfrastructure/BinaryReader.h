#pragma once
#ifndef TILELANDWORLD_BINARYREADER_H
#define TILELANDWORLD_BINARYREADER_H

#include <fstream>
#include <string>
#include <vector>
#include <type_traits> // For std::is_trivially_copyable
#include <stdexcept>   // For std::runtime_error
#include <iostream>    // For std::cerr
#include "../Utils/Logger.h" // <-- 包含 Logger

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
        // 现在流会因 failbit/badbit 抛出异常。
        template <typename T,
                  typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
        bool read(T& data) {
            // 在读取前检查 EOF，因为 peek() 不会设置 failbit/badbit
            if (stream.peek() == EOF) {
                 return false; // 已经到文件末尾，无法读取
            }
            try {
                 stream.read(reinterpret_cast<char*>(&data), sizeof(T));
                 // 如果 read 成功或因 EOF 而部分成功但读满了 sizeof(T)，则返回 true
                 // 如果因 EOF 导致读取字节 < sizeof(T)，会抛出异常 (failbit 被设置)
                 // 如果因其他错误 (badbit)，也会抛出异常
                 return true;
            } catch (const std::ios_base::failure& e) {
                 // 检查是否是因为 EOF 导致读取不足 sizeof(T)
                 if (stream.eof() && stream.gcount() < sizeof(T)) {
                     // 这是文件提前结束的情况
                     LOG_ERROR("BinaryReader::read<T> failed due to unexpected EOF. Read "
                               + std::to_string(stream.gcount()) + "/" + std::to_string(sizeof(T)) + " bytes.");
                     return false;
                 } else if (!stream.eof()) {
                     // 如果不是 EOF 错误，重新包装并抛出
                     throw std::runtime_error("BinaryReader::read<T> failed: " + std::string(e.what()));
                 } else {
                     // 如果是 EOF 但 gcount() == sizeof(T)，read 应该成功了，不应进入 catch？
                     // 但以防万一，这里也认为是失败
                     LOG_ERROR("BinaryReader::read<T> encountered EOF state unexpectedly after read.");
                     return false;
                 }
            }
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
