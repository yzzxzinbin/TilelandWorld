#include "MapSerializer.h"
#include <iostream> // For error reporting (temporary)
#include <vector>
#include <cstring> // For memcpy in checksum calculation

namespace TilelandWorld {

    bool MapSerializer::writeHeader(BinaryWriter& writer, const FileHeader& header) {
        // 写入除校验和之外的部分
        if (!writer.write(header.magicNumber)) return false;
        if (!writer.write(header.versionMajor)) return false;
        if (!writer.write(header.versionMinor)) return false;
        if (!writer.write(header.metadataOffset)) return false;
        if (!writer.write(header.indexOffset)) return false;
        if (!writer.write(header.dataOffset)) return false;

        // 计算校验和 (不包括 checksum 字段本身)
        FileHeader tempHeader = header; // 复制一份用于计算
        tempHeader.headerChecksum = 0; // 清零校验和字段
        uint32_t checksum = calculateXORChecksum(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));

        // 写入计算出的校验和
        if (!writer.write(checksum)) return false;

        return writer.good();
    }

    bool MapSerializer::readAndValidateHeader(BinaryReader& reader, FileHeader& header) {
        // 读取除校验和之外的部分
        if (!reader.read(header.magicNumber)) return false;
        if (!reader.read(header.versionMajor)) return false;
        if (!reader.read(header.versionMinor)) return false;
        if (!reader.read(header.metadataOffset)) return false;
        if (!reader.read(header.indexOffset)) return false;
        if (!reader.read(header.dataOffset)) return false;

        // 读取文件中记录的校验和
        uint32_t storedChecksum = 0;
        if (!reader.read(storedChecksum)) return false;

        // 验证魔数
        if (header.magicNumber != MAGIC_NUMBER) {
            std::cerr << "Error: Invalid magic number!" << std::endl;
            return false;
        }

        // 验证版本 (简单示例：只支持当前版本)
        if (header.versionMajor != FORMAT_VERSION_MAJOR || header.versionMinor > FORMAT_VERSION_MINOR) {
             std::cerr << "Error: Unsupported file version!" << std::endl;
             return false;
        }

        // 重新计算校验和进行验证
        FileHeader tempHeader = header; // 复制一份用于计算
        tempHeader.headerChecksum = 0; // 清零校验和字段
        uint32_t calculatedChecksum = calculateXORChecksum(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));

        if (calculatedChecksum != storedChecksum) {
            std::cerr << "Error: Header checksum mismatch!" << std::endl;
            return false;
        }

        // 校验通过后，将读取到的校验和存入 header 对象（虽然计算时清零了）
        header.headerChecksum = storedChecksum;

        return true; // Header is valid
    }


    bool MapSerializer::saveMap(const Map& map, const std::string& filepath) {
        try {
            BinaryWriter writer(filepath);
            if (!writer.good()) return false;

            // 1. 准备文件头 (偏移量暂时设为 0，后续更新)
            FileHeader header = {};
            header.magicNumber = MAGIC_NUMBER;
            header.versionMajor = FORMAT_VERSION_MAJOR;
            header.versionMinor = FORMAT_VERSION_MINOR;
            header.metadataOffset = 0; // TODO
            header.indexOffset = 0;    // TODO
            header.dataOffset = sizeof(FileHeader); // 数据紧随其后 (暂定)

            // 写入占位符头，稍后回来更新
            if (!writer.seek(0)) return false;
            if (!writer.writeBytes(reinterpret_cast<const char*>(&header), sizeof(header))) return false;

            // 2. TODO: 写入元数据 (如果需要)
            // header.metadataOffset = writer.tell();
            // ... write metadata ...

            // 3. TODO: 遍历 Map 中的区块，写入区块数据
            // header.dataOffset = writer.tell();
            // std::vector<ChunkIndexEntry> index;
            // for (const auto& pair : map.loadedChunks) { // 需要访问 map 的内部成员，或者 map 提供迭代器
            //     const Chunk& chunk = *pair.second;
            //     ChunkIndexEntry entry;
            //     entry.cx = chunk.getChunkX();
            //     // ... fill entry ...
            //     entry.offset = writer.tell();
            //     if (!saveChunkData(writer, chunk)) return false;
            //     entry.size = static_cast<uint32_t>(writer.tell() - entry.offset);
            //     // entry.checksum = calculateChecksum(...);
            //     index.push_back(entry);
            // }

            // 4. TODO: 写入区块索引
            // header.indexOffset = writer.tell();
            // if (!writeIndex(writer, index)) return false;

            // 5. 回到文件开头，更新带有正确偏移量和校验和的文件头
            std::streampos finalPos = writer.tell(); // 记录当前位置，以防 seek 失败
            if (!writer.seek(0)) return false;
            // header.headerChecksum 需要在 writeHeader 内部计算
            if (!writeHeader(writer, header)) return false;

            // (可选) 恢复到文件末尾？通常不需要。
            // writer.seek(finalPos);

            std::cout << "Map saved successfully (header only for now)." << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error saving map: " << e.what() << std::endl;
            return false;
        }
    }

    std::unique_ptr<Map> MapSerializer::loadMap(const std::string& filepath) {
         try {
            BinaryReader reader(filepath);
            if (!reader.good()) return nullptr;

            // 1. 读取并验证文件头
            FileHeader header = {};
            if (!readAndValidateHeader(reader, header)) {
                return nullptr; // Header 无效或读取失败
            }

            // 2. TODO: 读取元数据 (如果需要)
            // if (header.metadataOffset > 0) {
            //     if (!reader.seek(header.metadataOffset)) return nullptr;
            //     // ... read metadata ...
            // }

            // 3. TODO: 读取区块索引
            // std::vector<ChunkIndexEntry> index;
            // if (header.indexOffset > 0) {
            //     if (!reader.seek(header.indexOffset)) return nullptr;
            //     if (!readIndex(reader, index)) return nullptr;
            // } else {
            //     // 可能需要从数据区扫描？或者不支持无索引？
            // }

            // 4. 创建 Map 对象
            auto map = std::make_unique<Map>();

            // 5. TODO: 根据索引加载区块数据 (按需或全部加载)
            // for (const auto& entry : index) {
            //     if (!reader.seek(entry.offset)) return nullptr;
            //     // 需要一种方式将加载的区块放入 map 中
            //     // auto newChunk = std::make_unique<Chunk>(entry.cx, entry.cy, entry.cz);
            //     // if (!loadChunkData(reader, *newChunk)) return nullptr;
            //     // map->loadedChunks[{entry.cx, entry.cy, entry.cz}] = std::move(newChunk); // 需要 friend 或 public access/method
            // }

            std::cout << "Map loaded successfully (header only for now)." << std::endl;
            return map;

        } catch (const std::exception& e) {
            std::cerr << "Error loading map: " << e.what() << std::endl;
            return nullptr;
        }
    }

    // --- TODO: Implement Chunk/Index serialization/deserialization ---
    // bool MapSerializer::saveChunkData(BinaryWriter& writer, const Chunk& chunk) { ... }
    // bool MapSerializer::loadChunkData(BinaryReader& reader, Chunk& chunk) { ... }
    // bool MapSerializer::writeIndex(BinaryWriter& writer, const std::vector<ChunkIndexEntry>& index) { ... }
    // bool MapSerializer::readIndex(BinaryReader& reader, std::vector<ChunkIndexEntry>& index) { ... }


} // namespace TilelandWorld
