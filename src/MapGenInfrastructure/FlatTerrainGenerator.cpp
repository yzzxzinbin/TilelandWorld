#include "FlatTerrainGenerator.h"
#include "../Constants.h" // For CHUNK dimensions
#include "../TerrainTypes.h" // Include for getTerrainProperties

namespace TilelandWorld {

    FlatTerrainGenerator::FlatTerrainGenerator(int groundLevel, TerrainType groundType, TerrainType airType)
        : groundLevel(groundLevel), groundType(groundType), airType(airType) {}

    void FlatTerrainGenerator::generateChunk(Chunk& chunk) const {
        // 获取区块的世界坐标基点
        int baseWX = chunk.getChunkX() * CHUNK_WIDTH;
        int baseWY = chunk.getChunkY() * CHUNK_HEIGHT;
        int baseWZ = chunk.getChunkZ() * CHUNK_DEPTH;

        // 遍历区块内的所有局部坐标
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            int currentWZ = baseWZ + lz; // 计算当前 Tile 的世界 Z 坐标
            TerrainType currentType = (currentWZ < groundLevel) ? groundType : airType;
            const auto& props = getTerrainProperties(currentType); // 获取地形属性

            for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
                for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                    Tile& tile = chunk.getLocalTile(lx, ly, lz);
                    tile.terrain = currentType;
                    // 设置 Tile 的默认属性基于地形
                    tile.canEnterSameLevel = props.allowEnterSameLevel;
                    tile.canStandOnTop = props.allowStandOnTop;
                    tile.movementCost = props.defaultMovementCost;
                    // 设置默认光照（或由光照系统处理）
                    tile.lightLevel = MAX_LIGHT_LEVEL; // 假设默认全亮
                    // *** 设置为已探索，以便在测试中可见 ***
                    tile.isExplored = true;
                }
            }
        }
    }

} // namespace TilelandWorld
