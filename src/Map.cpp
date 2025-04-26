#include "Map.h"
#include "Constants.h"
#include "MapGenInfrastructure/FlatTerrainGenerator.h" // 包含默认生成器实现
#include <stdexcept> // For exceptions
#include <utility> // For std::move
#include <iostream> // For error reporting (temporary)

namespace TilelandWorld {

    // 修改构造函数实现
    Map::Map(std::unique_ptr<TerrainGenerator> generator) {
        if (generator) {
            terrainGenerator = std::move(generator);
        } else {
            // 如果没有提供生成器，创建一个默认的（例如 FlatTerrainGenerator）
            terrainGenerator = std::make_unique<FlatTerrainGenerator>(0); // 地面高度为 0
        }
    }

    // --- 坐标转换实现 ---
    ChunkCoord Map::mapToChunkCoords(int wx, int wy, int wz) {
        return {
            floorDiv(wx, CHUNK_WIDTH),
            floorDiv(wy, CHUNK_HEIGHT),
            floorDiv(wz, CHUNK_DEPTH)
        };
    }

    void Map::mapToLocalCoords(int wx, int wy, int wz, int& lx, int& ly, int& lz) {
        lx = floorMod(wx, CHUNK_WIDTH);
        ly = floorMod(wy, CHUNK_HEIGHT);
        lz = floorMod(wz, CHUNK_DEPTH);
    }

    // --- 区块管理实现 ---
    Chunk* Map::getOrLoadChunk(int cx, int cy, int cz) {
        ChunkCoord coord = {cx, cy, cz};
        auto it = loadedChunks.find(coord);
        if (it != loadedChunks.end()) {
            return it->second.get(); // 区块已加载，返回指针
        } else {
            // 区块未加载，创建新区块
            auto newChunk = std::make_unique<Chunk>(cx, cy, cz);
            Chunk* chunkPtr = newChunk.get(); // 获取原始指针

            // *** 使用地形生成器填充新区块 ***
            if (terrainGenerator) {
                terrainGenerator->generateChunk(*newChunk);
            } else {
                // 如果没有生成器（理论上构造函数会保证有），可以记录错误或填充默认值
                std::cerr << "Warning: No terrain generator available for chunk ("
                          << cx << "," << cy << "," << cz << ")" << std::endl;
                // newChunk 默认构造时已填充 VOIDBLOCK，所以这里可以不处理
            }

            loadedChunks.emplace(coord, std::move(newChunk)); // 插入新区块
            return chunkPtr;
        }
    }

    const Chunk* Map::getChunk(int cx, int cy, int cz) const {
        ChunkCoord coord = {cx, cy, cz};
        auto it = loadedChunks.find(coord);
        if (it != loadedChunks.end()) {
            return it->second.get();
        } else {
            return nullptr; // 区块未加载
        }
    }

    // --- Tile 访问实现 ---
    Tile& Map::getTile(int wx, int wy, int wz) {
        ChunkCoord chunkCoord = mapToChunkCoords(wx, wy, wz);
        Chunk* chunk = getOrLoadChunk(chunkCoord.cx, chunkCoord.cy, chunkCoord.cz);

        if (!chunk) {
            // 如果 getOrLoadChunk 返回 nullptr (未来可能发生)，则抛出异常
            throw std::runtime_error("Failed to get or load chunk for world coordinates.");
        }

        int lx, ly, lz;
        mapToLocalCoords(wx, wy, wz, lx, ly, lz);
        return chunk->getLocalTile(lx, ly, lz); // 可能因无效局部坐标抛出 out_of_range
    }

    const Tile& Map::getTile(int wx, int wy, int wz) const {
        ChunkCoord chunkCoord = mapToChunkCoords(wx, wy, wz);
        const Chunk* chunk = getChunk(chunkCoord.cx, chunkCoord.cy, chunkCoord.cz); // 只获取已加载的

        if (!chunk) {
            // 如果只读访问时区块未加载，我们不能创建它。
            // 这里可以选择：
            // 1. 抛出异常
            // 2. 返回一个代表“虚空”或“未加载”的静态 const Tile 对象
            // 我们先抛出异常
            throw std::runtime_error("Attempted to access tile in unloaded chunk via const Map reference.");
            // 或者:
            // static const Tile unloadedTile(TerrainType::VOIDBLOCK); // Or UNKNOWN
            // return unloadedTile;
        }

        int lx, ly, lz;
        mapToLocalCoords(wx, wy, wz, lx, ly, lz);
        return chunk->getLocalTile(lx, ly, lz);
    }

    void Map::setTile(int wx, int wy, int wz, const Tile& tile) {
        // 获取 Tile 的可修改引用，然后赋值
        getTile(wx, wy, wz) = tile;
    }

     void Map::setTileTerrain(int wx, int wy, int wz, TerrainType terrainType) {
        // 获取 Tile 的可修改引用，然后修改其地形类型
        // 注意：这不会自动更新 Tile 的其他属性（如通行性、移动成本）
        // 可能需要一个更复杂的 Tile::setTerrain 方法来处理这个
        Tile& targetTile = getTile(wx, wy, wz);
        targetTile.terrain = terrainType;
        // TODO: Consider updating other tile properties based on the new terrain type
        // const auto& props = getTerrainProperties(terrainType);
        // targetTile.canEnterSameLevel = props.allowEnterSameLevel;
        // targetTile.canStandOnTop = props.allowStandOnTop;
        // targetTile.movementCost = props.defaultMovementCost;
     }


} // namespace TilelandWorld
