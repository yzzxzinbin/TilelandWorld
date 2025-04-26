#pragma once
#ifndef TILELANDWORLD_MAPSERIALIZER_H
#define TILELANDWORLD_MAPSERIALIZER_H

#include "Map.h" // 需要访问 Map 类
#include "BinaryWriter.h"
#include "BinaryReader.h"
#include "FileFormat.h"
#include "Checksum.h"
#include <string>
#include <vector>

namespace TilelandWorld {

    class MapSerializer {
    public:
        // 保存地图数据到文件
        static bool saveMap(const Map& map, const std::string& filepath);

        // 从文件加载地图数据
        // 返回 unique_ptr<Map>，如果加载失败则返回 nullptr
        static std::unique_ptr<Map> loadMap(const std::string& filepath);

    private:
        // 内部辅助函数
        static bool writeHeader(BinaryWriter& writer, const FileHeader& header);
        static bool readAndValidateHeader(BinaryReader& reader, FileHeader& header);

        // TODO: 实现区块数据的序列化和反序列化
        // static bool saveChunkData(BinaryWriter& writer, const Chunk& chunk);
        // static bool loadChunkData(BinaryReader& reader, Chunk& chunk);

        // TODO: 实现索引的写入和读取
        // static bool writeIndex(BinaryWriter& writer, const std::vector<ChunkIndexEntry>& index);
        // static bool readIndex(BinaryReader& reader, std::vector<ChunkIndexEntry>& index);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_MAPSERIALIZER_H
