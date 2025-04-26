#pragma once
#ifndef TILELANDWORLD_FILEFORMAT_H
#define TILELANDWORLD_FILEFORMAT_H

#include <cstdint>

namespace TilelandWorld {

    // --- 文件标识与版本 ---
    // 魔数 (Magic Number): 用于快速识别文件类型。选择不易与其他文件冲突的值。
    // 例如 "TLWF" (TileLand World File)
    constexpr uint32_t MAGIC_NUMBER = 0x544C5746; // ASCII for 'T','L','W','F' in little-endian
    constexpr uint16_t FORMAT_VERSION_MAJOR = 0;
    constexpr uint16_t FORMAT_VERSION_MINOR = 1;

    // --- 文件头结构 ---
    // 文件开头写入的基本信息。
    #pragma pack(push, 1) // 确保结构体按 1 字节对齐，避免填充字节影响二进制布局
    struct FileHeader {
        uint32_t magicNumber;       // 魔数，用于文件类型验证
        uint16_t versionMajor;      // 主版本号
        uint16_t versionMinor;      // 次版本号
        uint64_t metadataOffset;    // 元数据区域的文件偏移量 (如果需要)
        uint64_t indexOffset;       // 区块索引区域的文件偏移量
        uint64_t dataOffset;        // 实际区块数据区域的起始偏移量
        uint32_t headerChecksum;    // 文件头自身的校验和 (不包括此字段本身)
        // 可以添加其他全局标志或信息
    };
    #pragma pack(pop) // 恢复默认对齐

    // 确保 FileHeader 是 trivially copyable，以便直接读写
    static_assert(std::is_trivially_copyable_v<FileHeader>, "FileHeader must be trivially copyable");


    // --- 区块索引条目结构 (示例) ---
    // #pragma pack(push, 1)
    // struct ChunkIndexEntry {
    //     int cx, cy, cz;       // 区块坐标
    //     uint64_t offset;      // 区块数据在文件中的起始偏移量
    //     uint32_t size;        // 区块数据的压缩后/原始大小
    //     uint32_t checksum;    // 区块数据的校验和
    // };
    // #pragma pack(pop)
    // static_assert(std::is_trivially_copyable_v<ChunkIndexEntry>, "ChunkIndexEntry must be trivially copyable");


} // namespace TilelandWorld

#endif // TILELANDWORLD_FILEFORMAT_H
