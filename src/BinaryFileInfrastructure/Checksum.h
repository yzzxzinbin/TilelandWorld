#pragma once
#ifndef TILELANDWORLD_CHECKSUM_H
#define TILELANDWORLD_CHECKSUM_H

#include <cstdint>
#include <cstddef> // For size_t
#include <array>   // For std::array

namespace TilelandWorld {

    /**
     * @brief 计算给定数据块的简单 XOR 校验和。
     * @param data 指向数据块的指针。
     * @param size 数据块的大小（字节）。
     * @return 计算出的 32 位 XOR 校验和。
     * @note 保留用于比较或旧格式。
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

    // --- CRC32 Calculation with Lookup Table ---

    namespace Detail {
        // CRC32 polynomial (IEEE 802.3) - reversed
        constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;

        // Function to generate the CRC32 lookup table
        inline std::array<uint32_t, 256> generateCRC32Table() {
            std::array<uint32_t, 256> table = {};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j) {
                    if (crc & 1) {
                        crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
                    } else {
                        crc >>= 1;
                    }
                }
                table[i] = crc;
            }
            return table;
        }

        // Helper function to get the initialized table (thread-safe since C++11)
        inline const std::array<uint32_t, 256>& getCRC32TableInstance() {
            // Static local variable initialization is guaranteed to happen only once.
            static const std::array<uint32_t, 256> crcTable = generateCRC32Table();
            return crcTable;
        }
    } // namespace Detail

    /**
     * @brief 计算给定数据块的 CRC32 校验和 (IEEE 802.3 polynomial)。
     * @param data 指向数据块的指针。
     * @param size 数据块的大小（字节）。
     * @return 计算出的 32 位 CRC32 校验和。
     * @note 使用查找表优化。
     */
    inline uint32_t calculateCRC32(const void* data, size_t size) {
        if (!data || size == 0) {
            return 0;
        }

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF; // Initial value

        // Get the precomputed lookup table
        const auto& table = Detail::getCRC32TableInstance();

        // Process data byte by byte using the lookup table
        for (size_t i = 0; i < size; ++i) {
            crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 0xFF];
        }

        return ~crc; // Final bit inversion
    }

} // namespace TilelandWorld

#endif // TILELANDWORLD_CHECKSUM_H
