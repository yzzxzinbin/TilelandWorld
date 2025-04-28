#pragma once
#ifndef TILELANDWORLD_COMPRESSEDFILEFORMAT_H
#define TILELANDWORLD_COMPRESSEDFILEFORMAT_H

#include <cstdint>
#include <type_traits> // For std::is_trivially_copyable

namespace TilelandWorld {

    // 魔数 (Magic Number) for compressed file: "TLWZ"
    constexpr uint32_t COMPRESSED_MAGIC_NUMBER = 0x544C575A; // ASCII for 'T','L','W','Z' in little-endian
    constexpr uint16_t COMPRESSED_FORMAT_VERSION_MAJOR = 0;
    constexpr uint16_t COMPRESSED_FORMAT_VERSION_MINOR = 1;

    // 压缩类型标识 (为未来可能支持多种压缩算法预留)
    constexpr uint8_t COMPRESSION_TYPE_ZLIB = 0x01;

    #pragma pack(push, 1)
    struct CompressedFileHeader {
        uint32_t magicNumber;           // 魔数，用于文件类型验证 (COMPRESSED_MAGIC_NUMBER)
        uint16_t versionMajor;          // 主版本号
        uint16_t versionMinor;          // 次版本号
        uint8_t  compressionType;       // 使用的压缩算法 (见 COMPRESSION_TYPE_*)
        uint8_t  reserved1;             // 保留字段
        uint16_t reserved2;             // 保留字段
        uint64_t uncompressedSize;      // 原始未压缩数据的大小 (字节)
        uint32_t uncompressedChecksum;  // 原始未压缩数据的 CRC32 校验和
        uint64_t compressedSize;        // 压缩后数据块的大小 (字节)
        uint32_t compressedChecksum;    // 压缩后数据块的 CRC32 校验和 (可选但推荐)
    };
    #pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<CompressedFileHeader>, "CompressedFileHeader must be trivially copyable");

} // namespace TilelandWorld

#endif // TILELANDWORLD_COMPRESSEDFILEFORMAT_H
