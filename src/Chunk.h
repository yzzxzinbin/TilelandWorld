#pragma once
#ifndef TILELANDWORLD_CHUNK_H
#define TILELANDWORLD_CHUNK_H

#include "Tile.h"
#include "Constants.h"
#include <array>
#include <stdexcept> // 用于 std::out_of_range
#include <cassert>   // 用于 assert

namespace TilelandWorld {

    class Chunk {
    public:
        // 构造函数：使用其在区块网格中的坐标初始化区块。
        Chunk(int cx, int cy, int cz);

        // 获取区块的坐标。
        int getChunkX() const { return chunkX; }
        int getChunkY() const { return chunkY; }
        int getChunkZ() const { return chunkZ; }

        /**
         * @brief 使用局部坐标 (lx, ly, lz) 访问 Tile (非 const 版本)。
         * @param lx 局部 X 坐标 (0 到 CHUNK_WIDTH-1)。
         * @param ly 局部 Y 坐标 (0 到 CHUNK_HEIGHT-1)。
         * @param lz 局部 Z 坐标 (0 到 CHUNK_DEPTH-1)。
         * @return 对指定位置 Tile 的可修改引用 (Tile&)。
         * @throws std::out_of_range 如果坐标无效。
         * @details 此版本允许修改返回的 Tile 对象。
         */
        Tile& getLocalTile(int lx, int ly, int lz);

        /**
         * @brief 使用局部坐标 (lx, ly, lz) 访问 Tile (const 版本)。
         * @param lx 局部 X 坐标 (0 到 CHUNK_WIDTH-1)。
         * @param ly 局部 Y 坐标 (0 到 CHUNK_HEIGHT-1)。
         * @param lz 局部 Z 坐标 (0 到 CHUNK_DEPTH-1)。
         * @return 对指定位置 Tile 的只读引用 (const Tile&)。
         * @throws std::out_of_range 如果坐标无效。
         * @details 此版本不允许修改返回的 Tile 对象，用于 const 上下文。
         */
        const Tile& getLocalTile(int lx, int ly, int lz) const;

        // 辅助函数，检查局部坐标是否在边界内。
        static bool areLocalCoordsValid(int lx, int ly, int lz);

    private:
        int chunkX, chunkY, chunkZ; // 此区块在世界区块网格中的坐标
        std::array<Tile, CHUNK_VOLUME> tiles; // 存储 3D 区块数据的 1D 数组，Chunk层的核心

        /**
         * @brief 辅助函数，将 3D 局部坐标转换为 1D 数组索引。
         * @param lx 局部 X 坐标。
         * @param ly 局部 Y 坐标。
         * @param lz 局部 Z 坐标。
         * @return 对应于 tiles 数组的一维索引。
         * @details 这是内部实现细节，用于将三维逻辑坐标映射到一维物理存储。
         */
        static size_t localCoordsToIndex(int lx, int ly, int lz);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_CHUNK_H
