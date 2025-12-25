#pragma once
#ifndef TILELANDWORLD_MAPSERIALIZER_H
#define TILELANDWORLD_MAPSERIALIZER_H

#include "Map.h" // 需要访问 Map 类
#include "BinaryWriter.h"
#include "BinaryReader.h"
#include "FileFormat.h"
#include "Checksum.h"
#include "SaveMetadata.h"
#include <string>
#include <vector>
#include <memory> // For std::unique_ptr
#include <unordered_set> // For std::unordered_set

namespace TilelandWorld {

    // 固定大小的元数据块，便于向后兼容与扩展。
    struct MetadataBlock
    {
        int64_t seed;
        float frequency;
        char noiseType[32];
        char fractalType[32];
        int32_t octaves;
        float lacunarity;
        float gain;
        uint8_t reserved[32]{}; // 预留空间
    };

    class MapSerializer {
    public:
        // 保存地图数据到文件
        // modifiedChunks: 可选参数。如果提供，则只保存集合中存在的区块 (用于增量保存或只保存修改过的部分)。
        static bool saveMap(const Map& map, const std::string& filepath, const std::unordered_set<ChunkCoord, ChunkCoordHash>* modifiedChunks = nullptr);

        // 从文件加载地图数据
        // 返回 unique_ptr<Map>，如果加载失败则返回 nullptr
        static std::unique_ptr<Map> loadMap(const std::string& filepath);

        // 保存压缩地图数据到 .tlwz 文件
        static bool saveCompressedMap(const Map& map, const std::string& saveName, const std::string& directory = ".", bool deleteTlwfAfterwards = true);

        // 从存档加载地图（自动处理 .tlwf 或 .tlwz）
        static std::unique_ptr<Map> loadMapFromSave(const std::string& saveName, const std::string& directory = ".");

        // 获取 .tlwf 和 .tlwz 的完整路径
        static std::string getTlwfPath(const std::string& saveName, const std::string& directory);
        static std::string getTlwzPath(const std::string& saveName, const std::string& directory);

    private:
        // 内部辅助函数
        static bool writeHeader(BinaryWriter& writer, FileHeader& header);
        static void readAndValidateHeader(BinaryReader& reader, FileHeader& header);

        // 实现区块数据的序列化和反序列化
        static bool saveChunkData(BinaryWriter& writer, const Chunk& chunk, uint32_t& outChecksum);
        static void loadChunkData(BinaryReader& reader, Chunk& chunk, uint32_t expectedSize, uint32_t expectedChecksum);

        // 实现索引的写入和读取
        static bool writeIndex(BinaryWriter& writer, const std::vector<ChunkIndexEntry>& index);
        static void readIndex(BinaryReader& reader, std::vector<ChunkIndexEntry>& index);

        // 压缩加载辅助函数
        static std::unique_ptr<Map> loadFromCompressedFile(const std::string& tlwzPath, const std::string& tlwfPath);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_MAPSERIALIZER_H
