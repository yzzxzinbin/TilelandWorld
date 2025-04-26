#include "Chunk.h"
#include "TerrainTypes.h" // 包含默认 Tile 类型 (例如 VOIDBLOCK)

namespace TilelandWorld
{
    // 构造函数：初始化区块中的所有 Tile，填充为 VOIDBLOCK 或基于生成逻辑。
    Chunk::Chunk(int cx, int cy, int cz) : chunkX(cx), chunkY(cy), chunkZ(cz)
    {
        tiles.fill(Tile(TerrainType::VOIDBLOCK));
    }

    // 检查局部坐标是否在区块的有效范围内 (lx, ly 对应 XY 平面, lz 对应 Z 层级)。
    bool Chunk::areLocalCoordsValid(int lx, int ly, int lz)
    {
        return lx >= 0 && lx < CHUNK_WIDTH &&
               ly >= 0 && ly < CHUNK_HEIGHT &&
               lz >= 0 && lz < CHUNK_DEPTH;
    }

    // 将区块内的 3D 局部坐标 (lx, ly, lz) 转换为 1D 数组 tiles 的索引。
    // 这是必要的，因为我们将 3D 数据线性存储在 1D 数组中以提高效率。
    size_t Chunk::localCoordsToIndex(int lx, int ly, int lz)
    {
        // 在计算索引之前，也要确保坐标在非调试版本中有效，
        // 或者依赖调用者 (getLocalTile) 进行检查。断言 (Assert) 对调试很有用。
        assert(areLocalCoordsValid(lx, ly, lz) && "局部坐标超出范围。");

        // 索引计算：假设 XY 是平面，Z 是垂直层级。
        // X 变化最快，然后是 Y，然后是 Z。
        // 可以将其视为层 (Z)，每层是一个 XY 平面 (AREA)。
        return static_cast<size_t>(lx) +                  // X 偏移
               static_cast<size_t>(ly) * CHUNK_WIDTH +    // Y 偏移 (在当前层内)
               static_cast<size_t>(lz) * CHUNK_AREA;     // Z 偏移 (跳到对应层)
                                                         // CHUNK_AREA = CHUNK_WIDTH * CHUNK_HEIGHT
    }

    Tile &Chunk::getLocalTile(int lx, int ly, int lz)
    {
        if (!areLocalCoordsValid(lx, ly, lz))
        {
            // 抛出异常，因为局部区块坐标超出范围。
            throw std::out_of_range("Local chunk coordinates out of range.");
        }
        return tiles[localCoordsToIndex(lx, ly, lz)];
    }

    const Tile &Chunk::getLocalTile(int lx, int ly, int lz) const
    {
        if (!areLocalCoordsValid(lx, ly, lz))
        {
            // 抛出异常，因为局部区块坐标超出范围。
            throw std::out_of_range("Local chunk coordinates out of range.");
        }
        return tiles[localCoordsToIndex(lx, ly, lz)];
    }

} // namespace TilelandWorld
