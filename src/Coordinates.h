#pragma once
#ifndef TILELANDWORLD_COORDINATES_H
#define TILELANDWORLD_COORDINATES_H

#include <functional> // For std::hash
#include <cmath>      // For std::floor

namespace TilelandWorld {

    // 世界坐标结构体
    struct WorldCoord {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const WorldCoord& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    // 区块坐标结构体
    struct ChunkCoord {
        int cx = 0;
        int cy = 0;
        int cz = 0;

        bool operator==(const ChunkCoord& other) const {
            return cx == other.cx && cy == other.cy && cz == other.cz;
        }
    };

    // 为 ChunkCoord 提供哈希函数，以便用于 std::unordered_map
    struct ChunkCoordHash {
        std::size_t operator()(const ChunkCoord& c) const {
            // 一个简单的组合哈希函数
            std::size_t h1 = std::hash<int>{}(c.cx);
            std::size_t h2 = std::hash<int>{}(c.cy);
            std::size_t h3 = std::hash<int>{}(c.cz);
            // 使用位移和异或组合哈希值，减少碰撞概率
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    // --- 坐标转换辅助函数 ---

    // 整数除法，向负无穷取整 (Floor Division)
    inline int floorDiv(int a, int b) {
        int d = a / b;
        int r = a % b;
        // 如果余数不为0且被除数和除数异号，则商减1
        return (r != 0 && (a < 0) != (b < 0)) ? d - 1 : d;
    }

    // 整数取模，结果与除数同号 (Floor Modulo)
    inline int floorMod(int a, int b) {
        int r = a % b;
        // 如果余数不为0且被除数和除数异号，则余数加上除数
        return (r != 0 && (a < 0) != (b < 0)) ? r + b : r;
        // 或者更简洁: return (a % b + b) % b; (但需注意 % 对负数的处理可能依赖实现)
        // 上述 floorMod 实现更明确可靠
    }

} // namespace TilelandWorld

#endif // TILELANDWORLD_COORDINATES_H
