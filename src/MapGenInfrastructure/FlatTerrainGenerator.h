#pragma once
#ifndef TILELANDWORLD_FLATTERRAINGENERATOR_H
#define TILELANDWORLD_FLATTERRAINGENERATOR_H

#include "TerrainGenerator.h"
#include "../Chunk.h" // 需要包含 Chunk 定义以操作 Tile
#include "../Tile.h"  // 需要 Tile 和 TerrainType

namespace TilelandWorld {

    /**
     * @brief 一个简单的平坦地形生成器。
     *
     * 在指定高度以下生成一种地形，以上生成另一种。
     */
    class FlatTerrainGenerator : public TerrainGenerator {
    public:
        /**
         * @brief 构造函数。
         * @param groundLevel Z 坐标，低于此高度（不含）将被视为地面。
         * @param groundType 地面使用的地形类型。
         * @param airType 地面以上使用的地形类型。
         */
        FlatTerrainGenerator(int groundLevel = 0,
                             TerrainType groundType = TerrainType::GRASS,
                             TerrainType airType = TerrainType::VOIDBLOCK);

        void generateChunk(Chunk& chunk) const override;

    private:
        int groundLevel;
        TerrainType groundType;
        TerrainType airType;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_FLATTERRAINGENERATOR_H
