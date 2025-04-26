#pragma once
#ifndef TILELANDWORLD_CONSTANTS_H
#define TILELANDWORLD_CONSTANTS_H

#include <cstdint>

namespace TilelandWorld {

    // --- 区块维度 (XY 平面, Z 层级) ---
    constexpr int CHUNK_WIDTH = 16;  // X 轴大小 (平面宽度)
    constexpr int CHUNK_HEIGHT = 16; // Y 轴大小 (平面高度)
    constexpr int CHUNK_DEPTH = 16;  // Z 轴大小 (垂直层级/深度)

    constexpr int CHUNK_AREA = CHUNK_WIDTH * CHUNK_HEIGHT; // 每个 Z 层级的 Tile 数量 (XY平面面积)
    constexpr int CHUNK_VOLUME = CHUNK_AREA * CHUNK_DEPTH; // 每个区块总 Tile 数量

    // --- 光照常量 ---
    constexpr uint8_t MAX_LIGHT_LEVEL = 255; // 最大/自然光照等级 (0-255)

    // --- 其他常量 ---
    // ...

} // namespace TilelandWorld

#endif // TILELANDWORLD_CONSTANTS_H
