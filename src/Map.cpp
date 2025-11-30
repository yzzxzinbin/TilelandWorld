#include "Map.h"
#include "Constants.h"
#include "MapGenInfrastructure/FlatTerrainGenerator.h"
#include "Utils/Logger.h" // <-- 包含 Logger
#include <stdexcept>      // For exceptions
#include <utility>        // For std::move

#ifdef _WIN32
#include <windows.h> // For QueryPerformanceCounter
#endif

namespace TilelandWorld
{

    // 修改构造函数实现
    Map::Map(std::unique_ptr<TerrainGenerator> generator)
    {
        if (generator)
        {
            terrainGenerator = std::move(generator);
        }
        else
        {
            // 如果没有提供生成器，创建一个默认的（例如 FlatTerrainGenerator）
            terrainGenerator = std::make_unique<FlatTerrainGenerator>(0); // 地面高度为 0
        }
    }

    // --- 坐标转换实现 ---
    ChunkCoord Map::mapToChunkCoords(int wx, int wy, int wz)
    {
        return {
            floorDiv(wx, CHUNK_WIDTH),
            floorDiv(wy, CHUNK_HEIGHT),
            floorDiv(wz, CHUNK_DEPTH)};
    }

    void Map::mapToLocalCoords(int wx, int wy, int wz, int &lx, int &ly, int &lz)
    {
        lx = floorMod(wx, CHUNK_WIDTH);
        ly = floorMod(wy, CHUNK_HEIGHT);
        lz = floorMod(wz, CHUNK_DEPTH);
    }

    // --- 区块管理实现 ---
    Chunk *Map::getOrLoadChunk(int cx, int cy, int cz)
    {
        ChunkCoord coord = {cx, cy, cz};
        auto it = loadedChunks.find(coord);
        if (it != loadedChunks.end())
        {
            return it->second.get(); // 区块已加载，返回指针
        }
        else
        {
            // 复用 createChunkIsolated 逻辑，但这里是在同步调用中
            auto newChunk = createChunkIsolated(cx, cy, cz);
            Chunk *chunkPtr = newChunk.get();
            addChunk(std::move(newChunk));
            return chunkPtr;
        }
    }

    // 新增：独立生成区块 (不加锁，不修改 Map 状态)
    std::unique_ptr<Chunk> Map::createChunkIsolated(int cx, int cy, int cz) const
    {
        auto newChunk = std::make_unique<Chunk>(cx, cy, cz);

        // *** 使用地形生成器填充新区块 ***
        if (terrainGenerator)
        {
            #ifdef _WIN32
            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);
            #endif

            terrainGenerator->generateChunk(*newChunk);

            #ifdef _WIN32
            QueryPerformanceCounter(&end);
            double elapsedMs = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
            // 记录生成时间
            LOG_INFO("Generated Chunk (" + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cz) + 
                        ") in " + std::to_string(elapsedMs) + " ms.");
            #endif
        }
        
        return newChunk;
    }

    // 新增：将区块加入地图
    void Map::addChunk(std::unique_ptr<Chunk> chunk)
    {
        if (!chunk) return;
        ChunkCoord coord = {chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ()};
        
        // 再次检查是否存在 (防止多线程竞争)
        if (loadedChunks.find(coord) == loadedChunks.end()) {
            loadedChunks.emplace(coord, std::move(chunk));
        } else {
            LOG_WARNING("Attempted to add existing chunk (" + std::to_string(coord.cx) + "," + std::to_string(coord.cy) + "," + std::to_string(coord.cz) + ")");
        }
    }

    const Chunk *Map::getChunk(int cx, int cy, int cz) const
    {
        ChunkCoord coord = {cx, cy, cz};
        auto it = loadedChunks.find(coord);
        if (it != loadedChunks.end())
        {
            return it->second.get();
        }
        else
        {
            return nullptr; // 区块未加载
        }
    }

    // --- Tile 访问实现 ---
    Tile &Map::getTile(int wx, int wy, int wz)
    {
        ChunkCoord chunkCoord = mapToChunkCoords(wx, wy, wz);
        Chunk *chunk = getOrLoadChunk(chunkCoord.cx, chunkCoord.cy, chunkCoord.cz);

        if (!chunk)
        {
            // 如果 getOrLoadChunk 返回 nullptr (未来可能发生)，则抛出异常
            throw std::runtime_error("Failed to get or load chunk for world coordinates.");
        }

        int lx, ly, lz;
        mapToLocalCoords(wx, wy, wz, lx, ly, lz);
        return chunk->getLocalTile(lx, ly, lz); // 可能因无效局部坐标抛出 out_of_range
    }

    const Tile &Map::getTile(int wx, int wy, int wz) const
    {
        ChunkCoord chunkCoord = mapToChunkCoords(wx, wy, wz);
        const Chunk *chunk = getChunk(chunkCoord.cx, chunkCoord.cy, chunkCoord.cz); // 只获取已加载的

        if (!chunk)
        {
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

    void Map::setTile(int wx, int wy, int wz, const Tile &tile)
    {
        // 获取 Tile 的可修改引用，然后赋值
        getTile(wx, wy, wz) = tile;
    }

    void Map::setTileTerrain(int wx, int wy, int wz, TerrainType terrainType)
    {
        // 获取 Tile 的可修改引用，然后修改其地形类型
        // 注意：这不会自动更新 Tile 的其他属性（如通行性、移动成本）
        // 可能需要一个更复杂的 Tile::setTerrain 方法来处理这个
        Tile &targetTile = getTile(wx, wy, wz);
        targetTile.terrain = terrainType;
        // TODO: Consider updating other tile properties based on the new terrain type
        // const auto& props = getTerrainProperties(terrainType);
        // targetTile.canEnterSameLevel = props.allowEnterSameLevel;
        // targetTile.canStandOnTop = props.allowStandOnTop;
        // targetTile.movementCost = props.defaultMovementCost;
        // 可以添加日志记录地形变化
        // LOG_INFO("Set terrain at (" + std::to_string(wx) + "," + std::to_string(wy) + "," + std::to_string(wz) + ") to " + std::to_string(static_cast<int>(terrainType)));
    }

    void Map::setTerrainGenerator(std::unique_ptr<TerrainGenerator> generator)
    {
        if (generator)
        {
            terrainGenerator = std::move(generator);
            LOG_INFO("Map terrain generator updated.");
        }
        else
        {
            LOG_WARNING("Attempted to set a null terrain generator. Keeping the existing one.");
        }
    }

} // namespace TilelandWorld
