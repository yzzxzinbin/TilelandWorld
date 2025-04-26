#pragma once
#ifndef TILELANDWORLD_CHECKSUM_H
#define TILELANDWORLD_CHECKSUM_H

#include <cstdint>
#include <cstddef> // For size_t

namespace TilelandWorld {

    /**
     * @brief 计算给定数据块的简单 XOR 校验和。
     * @param data 指向数据块的指针。
     * @param size 数据块的大小（字节）。
     * @return 计算出的 32 位 XOR 校验和。
     * @note 这是一个非常基础的校验和，容易发生碰撞，主要用于演示。
     *       生产环境中建议使用 CRC32 或更强的哈希算法。
     */
    inline uint32_t calculateXORChecksum(const void* data, size_t size) {
        if (!data || size == 0) {
            return 0;
        }

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t checksum = 0;
        uint32_t current_word = 0;
        size_t i = 0;

        // 处理大部分数据（按 4 字节块）
        while (i + 3 < size) {
            current_word = (static_cast<uint32_t>(bytes[i]) << 24) |
                           (static_cast<uint32_t>(bytes[i + 1]) << 16) |
                           (static_cast<uint32_t>(bytes[i + 2]) << 8) |
                           static_cast<uint32_t>(bytes[i + 3]);
            checksum ^= current_word;
            i += 4;
        }

        // 处理剩余的字节 (0-3 字节)
        current_word = 0;
        size_t remaining = size - i;
        if (remaining > 0) {
             current_word |= (static_cast<uint32_t>(bytes[i]) << 24);
        }
        if (remaining > 1) {
             current_word |= (static_cast<uint32_t>(bytes[i+1]) << 16);
        }
        if (remaining > 2) {
             current_word |= (static_cast<uint32_t>(bytes[i+2]) << 8);
        }
        // 最后一个字节不需要移位，但这里为了统一处理，即使只有一个字节也放入最高位
        // 如果需要严格按顺序，需要调整移位

        if (remaining > 0) {
             checksum ^= current_word;
        }


        return checksum;
    }

    // TODO: 未来可以考虑替换为 CRC32 实现
    // uint32_t calculateCRC32(const void* data, size_t size);

} // namespace TilelandWorld

#endif // TILELANDWORLD_CHECKSUM_H
