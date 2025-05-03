#pragma once
#ifndef TILELANDWORLD_FASTNOISETERRAINGENERATOR_H
#define TILELANDWORLD_FASTNOISETERRAINGENERATOR_H

#include "TerrainGenerator.h"
#include "../Chunk.h" // 需要 Chunk 定义
#include "../Tile.h"  // 需要 Tile 和 TerrainType
#include <FastNoise/FastNoise.h> // 包含 FastNoise2 主头文件
#include <memory> // For std::unique_ptr

namespace TilelandWorld {

    /**
     * @brief 使用 FastNoise2 库生成地形的生成器。
     *
     * 通过配置 FastNoise 节点树来生成噪声，并将噪声值映射到地形类型。
     */
    class FastNoiseTerrainGenerator : public TerrainGenerator {
    public:
        /**
         * @brief 构造函数。
         * @param seed 噪声生成的种子。
         * @param frequency 噪声频率，影响细节程度。
         * @param noiseType 基础噪声类型 (例如 "Perlin", "OpenSimplex2").
         * @param fractalType 分形类型 (例如 "FBm", "Ridged").
         * @param octaves 分形计算的倍频程数。
         * @param lacunarity 分形计算的空隙度。
         * @param gain 分形计算的增益。
         */
        FastNoiseTerrainGenerator(
            int seed = 1337,
            float frequency = 0.02f,
            const char* noiseType = "Perlin", // 默认使用 Perlin
            const char* fractalType = "FBm",  // 默认使用 FBM 分形
            int octaves = 3,
            float lacunarity = 2.0f,
            float gain = 0.5f
        );

        // 禁用拷贝构造和赋值
        FastNoiseTerrainGenerator(const FastNoiseTerrainGenerator&) = delete;
        FastNoiseTerrainGenerator& operator=(const FastNoiseTerrainGenerator&) = delete;

        // 允许移动构造和赋值 (如果需要的话)
        FastNoiseTerrainGenerator(FastNoiseTerrainGenerator&&) = default;
        FastNoiseTerrainGenerator& operator=(FastNoiseTerrainGenerator&&) = default;


        /**
         * @brief 使用 FastNoise2 填充给定区块的地形数据。
         * @param chunk 需要被填充地形数据的区块引用。
         */
        void generateChunk(Chunk& chunk) const override;

    private:
        int seed;
        float frequency;
        // 可以添加更多配置参数，如阈值等

        // FastNoise 节点智能指针
        FastNoise::SmartNode<> noiseSource;

        /**
         * @brief 将噪声值映射到地形类型。
         * @param noiseValue 从 FastNoise 获取的噪声值 (通常在 -1 到 1 之间)。
         * @param worldZ 当前 Tile 的世界 Z 坐标。
         * @return 对应的地形类型。
         */
        TerrainType mapNoiseToTerrain(float noiseValue, int worldZ) const;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_FASTNOISETERRAINGENERATOR_H