#include "MapSerializer.h"
#include "../Constants.h" // For CHUNK_VOLUME, etc.
#include "Checksum.h" // Include Checksum header
#include <iostream> // For error reporting (temporary)
#include <vector>
#include <cstring> // For memcpy in checksum calculation

namespace TilelandWorld {

    // --- Header Read/Write (writeHeader 现在接收非 const 引用以更新) ---
    bool MapSerializer::writeHeader(BinaryWriter& writer, FileHeader& header) { // header 现在是非 const
        // 计算校验和 (不包括 checksum 字段本身) - 使用 CRC32
        FileHeader tempHeader = header; // 复制一份用于计算
        tempHeader.headerChecksum = 0; // 清零校验和字段
        header.headerChecksum = calculateCRC32(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t)); // 计算并存储 CRC32

        // 写入完整的文件头 (包括刚计算的校验和)
        return writer.write(header); // 直接写入整个结构体
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

        // 重新计算校验和进行验证 - 使用 CRC32
        FileHeader tempHeader = header; // 复制一份用于计算
        tempHeader.headerChecksum = 0; // 清零校验和字段
        uint32_t calculatedChecksum = calculateCRC32(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));

        if (calculatedChecksum != storedChecksum) {
            std::cerr << "Error: Header checksum mismatch! Expected 0x" << std::hex << storedChecksum
                      << ", Calculated 0x" << calculatedChecksum << std::dec << std::endl;
            return false;
        }

        // 校验通过后，将读取到的校验和存入 header 对象（虽然计算时清零了）
        header.headerChecksum = storedChecksum;

        return true; // Header is valid
    }

    // --- Chunk Data Serialization/Deserialization ---

    // 将单个 Chunk 的 Tile 数据写入流，并计算校验和
    bool MapSerializer::saveChunkData(BinaryWriter& writer, const Chunk& chunk, uint32_t& outChecksum) {
        const void* dataPtr = chunk.tiles.data(); // 获取指向 tiles 数组数据的指针
        size_t dataSize = sizeof(Tile) * CHUNK_VOLUME;

        // 计算校验和 - 使用 CRC32
        outChecksum = calculateCRC32(dataPtr, dataSize);

        // 写入数据
        return writer.writeBytes(static_cast<const char*>(dataPtr), dataSize);
    }

    // 从流中读取数据填充 Chunk，并验证大小和校验和
    bool MapSerializer::loadChunkData(BinaryReader& reader, Chunk& chunk, uint32_t expectedSize, uint32_t expectedChecksum) {
        size_t requiredSize = sizeof(Tile) * CHUNK_VOLUME;

        // 验证预期大小是否匹配
        if (expectedSize != requiredSize) {
            std::cerr << "Error: Chunk data size mismatch. Expected " << requiredSize << ", got " << expectedSize << std::endl;
            return false;
        }

        // 读取数据到 Chunk 的 tiles 数组
        void* dataPtr = chunk.tiles.data(); // 获取指向 tiles 数组数据的指针
        size_t bytesRead = reader.readBytes(static_cast<char*>(dataPtr), requiredSize);

        if (bytesRead != requiredSize) {
            std::cerr << "Error: Failed to read complete chunk data. Read " << bytesRead << "/" << requiredSize << std::endl;
            return false;
        }

        // 验证校验和 - 使用 CRC32
        uint32_t calculatedChecksum = calculateCRC32(dataPtr, requiredSize);
        if (calculatedChecksum != expectedChecksum) {
            std::cerr << "Error: Chunk data checksum mismatch! Expected 0x" << std::hex << expectedChecksum
                      << ", Calculated 0x" << calculatedChecksum << std::dec << std::endl;
            return false;
        }

        return true;
    }

    // --- Index Serialization/Deserialization ---

    // 将索引写入流 (数量 + 条目)
    bool MapSerializer::writeIndex(BinaryWriter& writer, const std::vector<ChunkIndexEntry>& index) {
        // 1. 写入索引条目的数量
        size_t count = index.size();
        if (!writer.write(count)) return false;

        // 2. 写入每个索引条目
        if (count > 0) {
            // 直接写入整个 vector 的底层数据 (因为 ChunkIndexEntry 是 POD)
            return writer.writeBytes(reinterpret_cast<const char*>(index.data()), count * sizeof(ChunkIndexEntry));
        }
        return true; // 写入 0 个条目也算成功
    }

    // 从流中读取索引
    bool MapSerializer::readIndex(BinaryReader& reader, std::vector<ChunkIndexEntry>& index) {
        index.clear();
        // 1. 读取索引条目的数量
        size_t count = 0;
        if (!reader.read(count)) return false;

        if (count > 0) {
            // 2. 读取所有索引条目
            index.resize(count); // 调整 vector 大小以容纳数据
            size_t bytesToRead = count * sizeof(ChunkIndexEntry);
            size_t bytesRead = reader.readBytes(reinterpret_cast<char*>(index.data()), bytesToRead);

            if (bytesRead != bytesToRead) {
                std::cerr << "Error: Failed to read complete index data." << std::endl;
                index.clear(); // 读取失败，清空索引
                return false;
            }
        }
        return true; // 读取成功 (即使 count 为 0)
    }

    // --- saveMap / loadMap Implementation ---

    bool MapSerializer::saveMap(const Map& map, const std::string& filepath) {
        try {
            BinaryWriter writer(filepath);
            if (!writer.good()) return false;

            // 1. 准备文件头 (偏移量稍后更新)
            FileHeader header = {};
            header.magicNumber = MAGIC_NUMBER;
            header.versionMajor = FORMAT_VERSION_MAJOR;
            header.versionMinor = FORMAT_VERSION_MINOR;
            // 偏移量将在写入相应部分后设置

            // 写入占位符头
            if (!writer.seek(0)) return false;
            if (!writer.write(header)) return false; // 直接写入结构体占位

            // 2. 写入元数据 (如果需要) - 暂跳过
            header.metadataOffset = 0; // 无元数据

            // 3. 写入区块数据并构建索引
            header.dataOffset = writer.tell(); // 数据区从当前位置开始
            std::vector<ChunkIndexEntry> index;
            index.reserve(map.loadedChunks.size()); // 预分配空间

            for (const auto& pair : map.loadedChunks) { // 访问 map 的 loadedChunks (需要友元)
                const Chunk& chunk = *pair.second;
                ChunkIndexEntry entry = {}; // 初始化条目
                entry.cx = chunk.getChunkX();
                entry.cy = chunk.getChunkY();
                entry.cz = chunk.getChunkZ();

                entry.offset = writer.tell(); // 记录当前偏移量作为区块数据起点
                std::streampos startPos = entry.offset; // Store start position
                if (!saveChunkData(writer, chunk, entry.checksum)) { // 写入数据并获取校验和
                     std::cerr << "Error saving chunk data for (" << entry.cx << "," << entry.cy << "," << entry.cz << ")" << std::endl;
                     return false;
                }
                std::streampos endPos = writer.tell(); // Get end position
                // Explicitly cast both to uint64_t before subtraction
                entry.size = static_cast<uint32_t>(static_cast<uint64_t>(endPos) - static_cast<uint64_t>(startPos)); // 计算写入的大小

                index.push_back(entry); // 添加到索引
            }

            // 4. 写入区块索引
            header.indexOffset = writer.tell(); // 索引区从当前位置开始
            if (!writeIndex(writer, index)) {
                 std::cerr << "Error writing chunk index." << std::endl;
                 return false;
            }

            // 5. 回到文件开头，更新并写入最终的文件头
            std::streampos finalPos = writer.tell();
            if (!writer.seek(0)) return false;
            if (!writeHeader(writer, header)) { // writeHeader 会计算并写入校验和
                 std::cerr << "Error writing final header." << std::endl;
                 return false;
            }

            // writer.seek(finalPos); // 通常不需要

            std::cout << "Map saved successfully. Chunks: " << index.size() << std::endl;
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

            // 2. 读取元数据 (如果需要) - 暂跳过
            // ...

            // 3. 读取区块索引
            std::vector<ChunkIndexEntry> index;
            if (header.indexOffset == 0 || header.indexOffset >= reader.fileSize()) {
                 std::cerr << "Error: Invalid or missing index offset in header." << std::endl;
                 return nullptr;
            }
            if (!reader.seek(header.indexOffset)) {
                 std::cerr << "Error: Failed to seek to index offset." << std::endl;
                 return nullptr;
            }
            if (!readIndex(reader, index)) {
                 std::cerr << "Error: Failed to read chunk index." << std::endl;
                 return nullptr;
            }

            // 4. 创建 Map 对象
            auto map = std::make_unique<Map>();

            // 5. 根据索引加载区块数据
            for (const auto& entry : index) {
                if (entry.offset == 0 || entry.offset >= reader.fileSize() || (entry.offset + entry.size) > reader.fileSize()) {
                     std::cerr << "Error: Invalid data offset or size for chunk (" << entry.cx << "," << entry.cy << "," << entry.cz << ")" << std::endl;
                     return nullptr; // 数据偏移或大小无效
                }
                if (!reader.seek(entry.offset)) {
                     std::cerr << "Error: Failed to seek to chunk data offset for (" << entry.cx << "," << entry.cy << "," << entry.cz << ")" << std::endl;
                     return nullptr;
                }

                // 创建新区块并尝试加载数据
                auto newChunk = std::make_unique<Chunk>(entry.cx, entry.cy, entry.cz);
                if (!loadChunkData(reader, *newChunk, entry.size, entry.checksum)) {
                     std::cerr << "Error: Failed to load data for chunk (" << entry.cx << "," << entry.cy << "," << entry.cz << ")" << std::endl;
                     return nullptr; // 加载或校验失败
                }

                // 将加载成功的区块添加到 Map 中 (需要友元访问)
                map->loadedChunks.emplace(ChunkCoord{entry.cx, entry.cy, entry.cz}, std::move(newChunk));
            }

            std::cout << "Map loaded successfully. Chunks loaded: " << index.size() << std::endl;
            return map;

        } catch (const std::exception& e) {
            std::cerr << "Error loading map: " << e.what() << std::endl;
            return nullptr;
        }
    }

} // namespace TilelandWorld
