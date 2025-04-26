#pragma once
#ifndef TILELANDWORLD_TERRAINGENERATOR_H
#define TILELANDWORLD_TERRAINGENERATOR_H

// 前向声明 Chunk 类，避免循环包含
namespace TilelandWorld {
    class Chunk;
}

namespace TilelandWorld {

    /**
     * @brief 地形生成器的抽象基类。
     *
     * 定义了所有地形生成器必须实现的接口，主要用于填充一个区块的内容。
     */
    class TerrainGenerator {
    public:
        virtual ~TerrainGenerator() = default; // 虚析构函数

        /**
         * @brief 填充给定区块的地形数据。
         * @param chunk 需要被填充地形数据的区块引用。
         * @note 此方法应该根据区块坐标 (chunk.getChunkX/Y/Z()) 和生成算法
         *       来确定性地填充 chunk 内的所有 Tile。
         */
        virtual void generateChunk(Chunk& chunk) const = 0;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TERRAINGENERATOR_H
