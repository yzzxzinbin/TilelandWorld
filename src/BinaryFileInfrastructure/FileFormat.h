#pragma once
#ifndef TILELANDWORLD_FILEFORMAT_H
#define TILELANDWORLD_FILEFORMAT_H

#include <cstdint>
#include <type_traits> // For std::is_trivially_copyable

namespace TilelandWorld {

    // --- 文件标识与版本 ---
    // 魔数 (Magic Number): 用于快速识别文件类型。选择不易与其他文件冲突的值。
    // 例如 "TLWF" (TileLand World File)
    constexpr uint32_t MAGIC_NUMBER = 0x544C5746; // ASCII for 'T','L','W','F' in little-endian
    constexpr uint16_t FORMAT_VERSION_MAJOR = 0;
    constexpr uint16_t FORMAT_VERSION_MINOR = 3; // 版本提升，因为文件头结构改变

    // --- 字节序标识 ---
    constexpr uint8_t ENDIANNESS_LITTLE = 0x01; // 小端字节序 (例如 x86, ARM little-endian)
    constexpr uint8_t ENDIANNESS_BIG    = 0x02; // 大端字节序 (例如 PowerPC, SPARC)

    // --- 校验和类型标识 ---
    constexpr uint8_t CHECKSUM_TYPE_NONE = 0x00; // 无校验
    constexpr uint8_t CHECKSUM_TYPE_XOR  = 0x01; // 简单 XOR 校验 (保留)
    constexpr uint8_t CHECKSUM_TYPE_CRC32 = 0x02; // CRC32 校验 (当前使用)

    // --- 文件头结构 ---
    // 文件开头写入的基本信息。
    #pragma pack(push, 1) // 确保结构体按 1 字节对齐，避免填充字节影响二进制布局
    struct FileHeader {
        uint32_t magicNumber;       // 魔数，用于文件类型验证
        uint16_t versionMajor;      // 主版本号
        uint16_t versionMinor;      // 次版本号
        uint8_t  endianness;        // 写入文件的系统的字节序 (见 ENDIANNESS_*)
        uint8_t  checksumType;      // 文件头和区块数据使用的校验和类型 (见 CHECKSUM_TYPE_*)
        uint16_t reserved;          // 保留字段，用于未来扩展或对齐 (凑齐到偶数字节)
        uint64_t metadataOffset;    // 元数据区域的文件偏移量 (如果需要)
        uint64_t indexOffset;       // 区块索引区域的文件偏移量
        uint64_t dataOffset;        // 实际区块数据区域的起始偏移量
        uint32_t headerChecksum;    // 文件头自身的校验和 (不包括此字段本身)
        // 可以添加其他全局标志或信息
    };
    #pragma pack(pop) // 恢复默认对齐

    // 确保 FileHeader 是 trivially copyable，以便直接读写
    static_assert(std::is_trivially_copyable_v<FileHeader>, "FileHeader must be trivially copyable");


    // --- 区块索引条目结构 ---
    #pragma pack(push, 1) // 确保结构体按 1 字节对齐
    struct ChunkIndexEntry {
        int32_t cx, cy, cz;   // 区块坐标 (使用固定大小类型)
        uint64_t offset;      // 区块数据在文件中的起始偏移量
        uint32_t size;        // 区块数据的压缩后/原始大小 (字节)
        uint32_t checksum;    // 区块数据的校验和 (使用 FileHeader.checksumType 指定的方法)
    };
    #pragma pack(pop) // 恢复默认对齐
    static_assert(std::is_trivially_copyable_v<ChunkIndexEntry>, "ChunkIndexEntry must be trivially copyable");


} // namespace TilelandWorld

#endif // TILELANDWORLD_FILEFORMAT_H
