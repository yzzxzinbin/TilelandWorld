#pragma once
#ifndef TILELANDWORLD_MAP_H
#define TILELANDWORLD_MAP_H

#include "Chunk.h"
#include "Coordinates.h"
#include "Tile.h"
#include "MapGenInfrastructure/TerrainGenerator.h" // 包含生成器基类
#include <unordered_map>
#include <memory> // For std::unique_ptr

namespace TilelandWorld {

    // 前向声明 MapSerializer，以便在 Map 中声明友元
    class MapSerializer;

    class Map {
        // 将 MapSerializer 声明为友元，允许它访问私有成员 (如 loadedChunks)
        friend class MapSerializer;

    public:
        // 修改构造函数以接受生成器，或提供默认生成器
        explicit Map(std::unique_ptr<TerrainGenerator> generator = nullptr);

        // --- 坐标转换 ---
        static ChunkCoord mapToChunkCoords(int wx, int wy, int wz);
        static void mapToLocalCoords(int wx, int wy, int wz, int& lx, int& ly, int& lz);

        // --- 区块管理 ---
        // 获取指定坐标的区块，如果未加载则创建（或未来加载）。
        // 返回指向区块的指针，如果无法创建/加载则可能返回 nullptr。
        Chunk* getOrLoadChunk(int cx, int cy, int cz);
        const Chunk* getChunk(int cx, int cy, int cz) const; // 只获取已加载的区块

        // --- Tile 访问与设置 (使用世界坐标) ---
        // 获取 Tile 的引用。如果区块未加载，会尝试加载/创建。
        // 如果无法访问（例如，区块加载失败），可能抛出异常或返回特定值/指针。
        // 为简化，这里先实现总是尝试加载/创建并返回引用（可能抛出）。
        Tile& getTile(int wx, int wy, int wz);
        const Tile& getTile(int wx, int wy, int wz) const;
        void setTile(int wx, int wy, int wz, const Tile& tile);
        void setTileTerrain(int wx, int wy, int wz, TerrainType terrainType);

        // --- Iteration over loaded chunks ---
        // Provide const iterators to allow reading loaded chunk data without exposing the map itself.
        using LoadedChunksConstIterator = std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>::const_iterator;

        LoadedChunksConstIterator begin() const { return loadedChunks.cbegin(); }
        LoadedChunksConstIterator end() const { return loadedChunks.cend(); }
        size_t getLoadedChunkCount() const { return loadedChunks.size(); }

        void setTerrainGenerator(std::unique_ptr<TerrainGenerator> generator);

    private:
        // 存储已加载的区块，使用区块坐标作为键，是MAP层的核心
        std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> loadedChunks;
        // 地形生成器
        std::unique_ptr<TerrainGenerator> terrainGenerator;
        // (未来可能添加：区块加载器、生成器、卸载逻辑等)
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_MAP_H
