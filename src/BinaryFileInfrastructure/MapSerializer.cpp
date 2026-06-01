#include "MapSerializer.h"
#include "../Constants.h" // For CHUNK_VOLUME, etc.
#include "Checksum.h" // Include Checksum header
#include "MemoryReader.h"
#include "MemoryWriter.h"
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <iostream> // For std::cout in success messages
#include "SaveMetadata.h"
#include "../MapGenInfrastructure/TerrainGeneratorFactory.h"
#include <vector>
#include <cstring> // For memcpy in checksum calculation
#include <algorithm> // For std::min
#include <stdexcept> // For std::runtime_error
#include <fstream>      // For std::ifstream to read whole file
#include <filesystem>   // For file operations like exists, remove
#include "../ZipFuncInfrastructure/zlib_wrapper.h" // 包含 zlib 封装
#include "CompressedFileFormat.h" // For compressed header
 
namespace TilelandWorld {

    // 辅助函数：检测当前系统的字节序 (运行时)
    bool isLittleEndianRuntime() {
        uint16_t test = 1;
        return (*reinterpret_cast<uint8_t*>(&test) == 1);
    }

    namespace {
        inline std::string trimNullTerminated(const char* data, size_t len) {
            size_t realLen = 0;
            while (realLen < len && data[realLen] != '\0') ++realLen;
            return std::string(data, realLen);
        }

        bool readSummaryFromBuffer(const uint8_t* data, size_t size, MapSerializer::SaveSummary& out) {
            if (data == nullptr || size < sizeof(FileHeader)) return false;

            FileHeader header{};
            std::memcpy(&header, data, sizeof(header));
            if (header.magicNumber != MAGIC_NUMBER) return false;

            size_t metaOffset = static_cast<size_t>(header.metadataOffset);
            if (metaOffset == 0 || metaOffset + sizeof(MetadataBlock) > size) return false;
 
            MetadataBlock block{};
            std::memcpy(&block, data + metaOffset, sizeof(block));
            out.metadata.seed = block.seed;
            out.metadata.frequency = block.frequency;
            out.metadata.noiseType = trimNullTerminated(block.noiseType, sizeof(block.noiseType));
            out.metadata.fractalType = trimNullTerminated(block.fractalType, sizeof(block.fractalType));
            out.metadata.octaves = block.octaves;
            out.metadata.lacunarity = block.lacunarity;
            out.metadata.gain = block.gain;

            out.chunkCount = 0;
            size_t indexOffset = static_cast<size_t>(header.indexOffset);
            if (indexOffset != 0 && indexOffset + sizeof(size_t) <= size) {
                size_t count = 0;
                std::memcpy(&count, data + indexOffset, sizeof(size_t));
                out.chunkCount = count;
            }

            return true;
        }

        bool readSummaryFromBuffer(const std::vector<uint8_t>& buffer, MapSerializer::SaveSummary& out) {
            return readSummaryFromBuffer(buffer.data(), buffer.size(), out);
        }

        bool applyMetadataToBuffer(std::vector<uint8_t>& buffer, const WorldMetadata& meta) {
            if (buffer.size() < sizeof(FileHeader)) return false;
            FileHeader header{};
            std::memcpy(&header, buffer.data(), sizeof(header));
            size_t metaOffset = static_cast<size_t>(header.metadataOffset);
            if (metaOffset == 0 || metaOffset + sizeof(MetadataBlock) > buffer.size()) return false;

            MetadataBlock block{};
            block.seed = meta.seed;
            block.frequency = meta.frequency;
            std::memset(block.noiseType, 0, sizeof(block.noiseType));
            std::memset(block.fractalType, 0, sizeof(block.fractalType));
            std::strncpy(block.noiseType, meta.noiseType.c_str(), sizeof(block.noiseType) - 1);
            std::strncpy(block.fractalType, meta.fractalType.c_str(), sizeof(block.fractalType) - 1);
            block.octaves = meta.octaves;
            block.lacunarity = meta.lacunarity;
            block.gain = meta.gain;

            std::memcpy(buffer.data() + metaOffset, &block, sizeof(block));
            return true;
        }

        bool readSummaryFromTlwf(const std::string& path, MapSerializer::SaveSummary& out) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;

            // 只读取文件头，不需要读完整文件
            FileHeader header{};
            if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
            if (header.magicNumber != MAGIC_NUMBER) return false;

            // 获取文件大小
            file.seekg(0, std::ios::end);
            std::streamsize fileSize = file.tellg();
            if (fileSize <= 0) return false;

            // 直接跳转到元数据区读取
            if (header.metadataOffset == 0 ||
                static_cast<std::streamoff>(header.metadataOffset) + sizeof(MetadataBlock) > fileSize) return false;

            file.seekg(static_cast<std::streamoff>(header.metadataOffset));
            MetadataBlock block{};
            if (!file.read(reinterpret_cast<char*>(&block), sizeof(block))) return false;

            out.metadata.seed = block.seed;
            out.metadata.frequency = block.frequency;
            out.metadata.noiseType = trimNullTerminated(block.noiseType, sizeof(block.noiseType));
            out.metadata.fractalType = trimNullTerminated(block.fractalType, sizeof(block.fractalType));
            out.metadata.octaves = block.octaves;
            out.metadata.lacunarity = block.lacunarity;
            out.metadata.gain = block.gain;

            // 直接跳转到索引区读取区块数量
            out.chunkCount = 0;
            if (header.indexOffset != 0 &&
                static_cast<std::streamoff>(header.indexOffset) + sizeof(size_t) <= fileSize) {
                file.seekg(static_cast<std::streamoff>(header.indexOffset));
                size_t count = 0;
                if (file.read(reinterpret_cast<char*>(&count), sizeof(size_t))) {
                    out.chunkCount = count;
                }
            }

            out.path = path;
            out.compressed = false;
            out.fileSize = static_cast<size_t>(fileSize);
            return true;
        }

        bool writeBufferToFile(const std::string& path, const std::vector<uint8_t>& buffer) {
            BinaryWriter writer(path);
            return writer.writeBytes(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        }

        bool writeTlwzV2(const std::vector<uint8_t>& rawTlwfBuffer,
                         const MetadataBlock& metaBlock,
                         size_t chunkCount,
                         const std::string& tlwzPath) {
            std::vector<Bytef> source(rawTlwfBuffer.begin(), rawTlwfBuffer.end());
            std::vector<Bytef> compressedData;
            auto status = SimpZlib::compress(source, compressedData);
            if (status != SimpZlib::Status::OK) return false;

            CompressedFileHeader header{};
            header.magicNumber = COMPRESSED_MAGIC_NUMBER;
            header.versionMajor = COMPRESSED_FORMAT_VERSION_MAJOR;
            header.versionMinor = COMPRESSED_FORMAT_VERSION_MINOR;
            header.compressionType = COMPRESSION_TYPE_ZLIB;
            header.uncompressedSize = rawTlwfBuffer.size();
            header.uncompressedChecksum = calculateCRC32(rawTlwfBuffer.data(), rawTlwfBuffer.size());
            header.compressedSize = compressedData.size();
            header.compressedChecksum = calculateCRC32(compressedData.data(), compressedData.size());
            header.metadataOffset = sizeof(CompressedFileHeader);
            header.metadataSize = sizeof(MetadataBlock);
            header.chunkCount = static_cast<uint32_t>(chunkCount);
            header.compressedDataOffset = sizeof(CompressedFileHeader) + sizeof(MetadataBlock);

            BinaryWriter writer(tlwzPath);
            if (!writer.write(header)) return false;
            if (!writer.writeBytes(reinterpret_cast<const char*>(&metaBlock), sizeof(MetadataBlock))) return false;
            return writer.writeBytes(reinterpret_cast<const char*>(compressedData.data()), compressedData.size());
        }
    }

    template <typename Writer>
    bool MapSerializer::writeHeaderToWriter(Writer& writer, FileHeader& header) {
        header.endianness = isLittleEndianRuntime() ? ENDIANNESS_LITTLE : ENDIANNESS_BIG;
        header.checksumType = CHECKSUM_TYPE_CRC32;
        header.reserved = 0;

        FileHeader tempHeader = header;
        tempHeader.headerChecksum = 0;
        header.headerChecksum = calculateCRC32(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));

        return writer.write(header);
    }

    template <typename Writer>
    bool MapSerializer::writeChunkDataToWriter(Writer& writer, const Chunk& chunk, uint32_t& outChecksum) {
        const void* dataPtr = chunk.tiles.data();
        size_t dataSize = sizeof(Tile) * CHUNK_VOLUME;

        outChecksum = calculateCRC32(dataPtr, dataSize);

        return writer.writeBytes(static_cast<const char*>(dataPtr), dataSize);
    }

    template <typename Writer>
    bool MapSerializer::writeIndexToWriter(Writer& writer, const std::vector<ChunkIndexEntry>& index) {
        size_t count = index.size();
        if (!writer.write(count)) return false;

        if (count > 0) {
            return writer.writeBytes(reinterpret_cast<const char*>(index.data()), count * sizeof(ChunkIndexEntry));
        }
        return true;
    }

    template <typename Writer>
    bool MapSerializer::serializeMapToWriter(const Map& map, Writer& writer,
                                             const std::unordered_set<ChunkCoord, ChunkCoordHash>* modifiedChunks,
                                             size_t* outChunkCount) {
        FileHeader header = {};
        header.magicNumber = MAGIC_NUMBER;
        header.versionMajor = FORMAT_VERSION_MAJOR;
        header.versionMinor = FORMAT_VERSION_MINOR;
        header.metadataOffset = 0;
        if (!writer.seek(0)) return false;
        if (!writer.write(header)) return false;

        header.metadataOffset = 0;
        header.dataOffset = writer.tell();

        std::vector<ChunkIndexEntry> index;
        index.reserve(modifiedChunks ? modifiedChunks->size() : map.getLoadedChunkCount());

        for (auto it = map.begin(); it != map.end(); ++it) {
            if (modifiedChunks != nullptr && modifiedChunks->find(it->first) == modifiedChunks->end()) {
                continue;
            }

            const Chunk& chunk = *it->second;
            ChunkIndexEntry entry = {};
            entry.cx = chunk.getChunkX();
            entry.cy = chunk.getChunkY();
            entry.cz = chunk.getChunkZ();

            entry.offset = writer.tell();
            std::streampos startPos = entry.offset;
            if (!writeChunkDataToWriter(writer, chunk, entry.checksum)) {
                LOG_ERROR("Failed to save chunk (" + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ") data.");
                return false;
            }
            std::streampos endPos = writer.tell();
            entry.size = static_cast<uint32_t>(static_cast<uint64_t>(endPos) - static_cast<uint64_t>(startPos));

            index.push_back(entry);
        }

        header.indexOffset = writer.tell();
        if (!writeIndexToWriter(writer, index)) {
            LOG_ERROR("Failed to write chunk index.");
            return false;
        }

        header.metadataOffset = writer.tell();
        MetadataBlock metaBlock{};
        const WorldMetadata& meta = map.getWorldMetadata();
        metaBlock.seed = meta.seed;
        metaBlock.frequency = meta.frequency;
        std::memset(metaBlock.noiseType, 0, sizeof(metaBlock.noiseType));
        std::memset(metaBlock.fractalType, 0, sizeof(metaBlock.fractalType));
        std::strncpy(metaBlock.noiseType, meta.noiseType.c_str(), sizeof(metaBlock.noiseType) - 1);
        std::strncpy(metaBlock.fractalType, meta.fractalType.c_str(), sizeof(metaBlock.fractalType) - 1);
        metaBlock.octaves = meta.octaves;
        metaBlock.lacunarity = meta.lacunarity;
        metaBlock.gain = meta.gain;

        if (!writer.write(metaBlock.seed)) return false;
        if (!writer.write(metaBlock.frequency)) return false;
        if (!writer.writeBytes(reinterpret_cast<const char*>(metaBlock.noiseType), sizeof(metaBlock.noiseType))) return false;
        if (!writer.writeBytes(reinterpret_cast<const char*>(metaBlock.fractalType), sizeof(metaBlock.fractalType))) return false;
        if (!writer.write(metaBlock.octaves)) return false;
        if (!writer.write(metaBlock.lacunarity)) return false;
        if (!writer.write(metaBlock.gain)) return false;
        if (!writer.writeBytes(reinterpret_cast<const char*>(metaBlock.reserved), sizeof(metaBlock.reserved))) return false;

        if (!writer.seek(0)) return false;
        if (!writeHeaderToWriter(writer, header)) {
            LOG_ERROR("Failed to write final file header.");
            return false;
        }

        if (outChunkCount) {
            *outChunkCount = index.size();
        }
        return true;
    }

    template <typename Reader>
    void MapSerializer::readAndValidateHeaderFromReader(Reader& reader, FileHeader& header) {
        std::streampos startPos = reader.tell();
        if (startPos == -1) {
            throw std::runtime_error("Failed to get initial stream position for header validation.");
        }

        if (!reader.read(header)) {
            throw std::runtime_error("Failed to read file header data.");
        }

        std::streampos endPos = reader.tell();
        if (endPos == -1) {
            throw std::runtime_error("Failed to get stream position after reading header.");
        }

        if (header.magicNumber != MAGIC_NUMBER) {
            throw std::runtime_error("Invalid magic number in file header.");
        }

        if (header.versionMajor != FORMAT_VERSION_MAJOR || header.versionMinor > FORMAT_VERSION_MINOR) {
            throw std::runtime_error("Unsupported file version. File: "
                + std::to_string(header.versionMajor) + "." + std::to_string(header.versionMinor)
                + ", Supported: " + std::to_string(FORMAT_VERSION_MAJOR) + "." + std::to_string(FORMAT_VERSION_MINOR));
        }

        uint8_t currentSystemEndianness = isLittleEndianRuntime() ? ENDIANNESS_LITTLE : ENDIANNESS_BIG;
        if (header.endianness != currentSystemEndianness) {
            LOG_WARNING("File endianness (" + std::to_string(header.endianness)
                + ") differs from system endianness (" + std::to_string(currentSystemEndianness)
                + "). Byte swapping not implemented.");
        }

        if (header.checksumType != CHECKSUM_TYPE_CRC32) {
            throw std::runtime_error("Unsupported checksum type (" + std::to_string(header.checksumType) + "). Requires CRC32 ("
                + std::to_string(CHECKSUM_TYPE_CRC32) + ").");
        }

        size_t headerSizeWithoutChecksum = sizeof(FileHeader) - sizeof(uint32_t);
        std::vector<char> headerBytes(headerSizeWithoutChecksum);

        if (!reader.seek(startPos)) {
            throw std::runtime_error("Failed to seek back for header checksum verification.");
        }
        if (reader.readBytes(headerBytes.data(), headerSizeWithoutChecksum) != headerSizeWithoutChecksum) {
            throw std::runtime_error("Failed to re-read header bytes for checksum verification.");
        }
        if (!reader.seek(endPos)) {
            throw std::runtime_error("Failed to seek past header after verification.");
        }

        uint32_t calculatedChecksum = calculateCRC32(headerBytes.data(), headerSizeWithoutChecksum);
        uint32_t storedChecksum = header.headerChecksum;

        if (calculatedChecksum != storedChecksum) {
            std::stringstream ss;
            ss << "Header checksum mismatch! Expected 0x" << std::hex << storedChecksum
                << ", Calculated 0x" << calculatedChecksum << std::dec;
            throw std::runtime_error(ss.str());
        }
    }

    template <typename Reader>
    void MapSerializer::loadChunkDataFromReader(Reader& reader, Chunk& chunk, uint32_t expectedSize, uint32_t expectedChecksum) {
        size_t requiredSize = sizeof(Tile) * CHUNK_VOLUME;

        if (expectedSize != requiredSize) {
            throw std::runtime_error("Chunk data size mismatch. Expected " + std::to_string(requiredSize) + ", Got " + std::to_string(expectedSize));
        }

        void* dataPtr = chunk.tiles.data();
        size_t bytesRead = reader.readBytes(static_cast<char*>(dataPtr), requiredSize);

        if (bytesRead != requiredSize) {
            throw std::runtime_error("Failed to read complete chunk data. Read " + std::to_string(bytesRead) + "/" + std::to_string(requiredSize));
        }

        uint32_t calculatedChecksum = calculateCRC32(dataPtr, requiredSize);
        if (calculatedChecksum != expectedChecksum) {
            std::stringstream ss;
            ss << "Chunk data checksum mismatch! Expected 0x" << std::hex << expectedChecksum
                << ", Calculated 0x" << calculatedChecksum << std::dec;
            throw std::runtime_error(ss.str());
        }
    }

    template <typename Reader>
    void MapSerializer::readIndexFromReader(Reader& reader, std::vector<ChunkIndexEntry>& index) {
        index.clear();
        size_t count = 0;
        if (!reader.read(count)) {
            throw std::runtime_error("Failed to read index count.");
        }

        if (count > 0) {
            index.resize(count);
            size_t bytesToRead = count * sizeof(ChunkIndexEntry);
            size_t bytesRead = reader.readBytes(reinterpret_cast<char*>(index.data()), bytesToRead);

            if (bytesRead != bytesToRead) {
                index.clear();
                throw std::runtime_error("Failed to read complete index data. Read " + std::to_string(bytesRead) + "/" + std::to_string(bytesToRead));
            }
        }
    }

    template <typename Reader>
    std::unique_ptr<Map> MapSerializer::loadMapFromReader(Reader& reader) {
        FileHeader header = {};
        readAndValidateHeaderFromReader(reader, header);

        auto sizePos = reader.fileSize();
        bool hasSize = (sizePos != std::streampos(-1));
        uint64_t fileSize = hasSize ? static_cast<uint64_t>(sizePos) : 0;

        std::vector<ChunkIndexEntry> index;
        if (header.indexOffset == 0 || (hasSize && header.indexOffset >= fileSize)) {
            throw std::runtime_error("Invalid or missing index offset in file header.");
        }
        if (!reader.seek(static_cast<std::streampos>(header.indexOffset))) {
            throw std::runtime_error("Failed to seek to index offset.");
        }
        readIndexFromReader(reader, index);

        WorldMetadata worldMeta{};
        if (header.metadataOffset != 0 && (!hasSize || header.metadataOffset < fileSize)) {
            if (!reader.seek(static_cast<std::streampos>(header.metadataOffset))) {
                throw std::runtime_error("Failed to seek to metadata offset.");
            }

            MetadataBlock metaBlock{};
            if (!reader.read(metaBlock.seed)) {
                throw std::runtime_error("Failed to read metadata seed.");
            }
            if (!reader.read(metaBlock.frequency)) {
                throw std::runtime_error("Failed to read metadata frequency.");
            }

            size_t noiseRead = reader.readBytes(reinterpret_cast<char*>(metaBlock.noiseType), sizeof(metaBlock.noiseType));
            if (noiseRead != sizeof(metaBlock.noiseType)) {
                throw std::runtime_error("Failed to read metadata noiseType.");
            }
            size_t fractalRead = reader.readBytes(reinterpret_cast<char*>(metaBlock.fractalType), sizeof(metaBlock.fractalType));
            if (fractalRead != sizeof(metaBlock.fractalType)) {
                throw std::runtime_error("Failed to read metadata fractalType.");
            }

            if (!reader.read(metaBlock.octaves)) {
                throw std::runtime_error("Failed to read metadata octaves.");
            }
            if (!reader.read(metaBlock.lacunarity)) {
                throw std::runtime_error("Failed to read metadata lacunarity.");
            }
            if (!reader.read(metaBlock.gain)) {
                throw std::runtime_error("Failed to read metadata gain.");
            }
            size_t reservedRead = reader.readBytes(reinterpret_cast<char*>(metaBlock.reserved), sizeof(metaBlock.reserved));
            if (reservedRead != sizeof(metaBlock.reserved)) {
                throw std::runtime_error("Failed to read metadata reserved padding.");
            }

            worldMeta.seed = metaBlock.seed;
            worldMeta.frequency = metaBlock.frequency;
            worldMeta.noiseType = std::string(metaBlock.noiseType);
            worldMeta.fractalType = std::string(metaBlock.fractalType);
            worldMeta.octaves = metaBlock.octaves;
            worldMeta.lacunarity = metaBlock.lacunarity;
            worldMeta.gain = metaBlock.gain;
        }

        auto map = std::make_unique<Map>();
        map->setWorldMetadata(worldMeta);
        map->setTerrainGenerator(createTerrainGeneratorFromMetadata(worldMeta));

        for (const auto& entry : index) {
            if (entry.offset == 0 || (hasSize && (entry.offset >= fileSize || (entry.offset + entry.size) > fileSize))) {
                throw std::runtime_error("Invalid data offset or size for chunk ("
                    + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ")");
            }
            if (!reader.seek(static_cast<std::streampos>(entry.offset))) {
                throw std::runtime_error("Failed to seek to data offset for chunk ("
                    + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ")");
            }

            auto newChunk = std::make_unique<Chunk>(entry.cx, entry.cy, entry.cz);
            loadChunkDataFromReader(reader, *newChunk, entry.size, entry.checksum);

            map->loadedChunks.emplace(ChunkCoord{entry.cx, entry.cy, entry.cz}, std::move(newChunk));
        }

        std::cout << "Map loaded successfully. Loaded chunk count: " << index.size() << std::endl;
        return map;
    }

    // --- saveMap / loadMap 实现 ---
    bool MapSerializer::saveMap(const Map& map, const std::string& filepath, const std::unordered_set<ChunkCoord, ChunkCoordHash>* modifiedChunks) {
        try {
            BinaryWriter writer(filepath);
            size_t chunkCount = 0;
            if (!serializeMapToWriter(map, writer, modifiedChunks, &chunkCount)) {
                LOG_ERROR("Failed to serialize map to file: " + filepath);
                return false;
            }

            std::cout << "Map saved successfully. Chunk count: " << chunkCount << std::endl;
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Exception occurred during map saving: " + std::string(e.what()));
            return false;
        }
    }

    std::unique_ptr<Map> MapSerializer::loadMap(const std::string& filepath) {
        try {
            BinaryReader reader(filepath);
            return loadMapFromReader(reader);

        } catch (const std::exception& e) {
            LOG_ERROR("Exception occurred during map loading: " + std::string(e.what()));
            return nullptr;
        }
    }

    // --- Path Helpers   ---
    std::string MapSerializer::getTlwfPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwf")).string();
    }

    std::string MapSerializer::getTlwzPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwz")).string();
    }

    bool MapSerializer::readSaveSummary(const std::string& saveName, const std::string& directory, SaveSummary& outSummary) {
        std::string tlwfPath = getTlwfPath(saveName, directory);
        std::string tlwzPath = getTlwzPath(saveName, directory);

        if (std::filesystem::exists(tlwfPath)) {
            if (readSummaryFromTlwf(tlwfPath, outSummary)) {
                return true;
            }
        }

        if (!std::filesystem::exists(tlwzPath)) return false;

        try {
            BinaryReader reader(tlwzPath);

            CompressedFileHeader header{};
            if (!reader.read(header)) return false;
            if (header.magicNumber != COMPRESSED_MAGIC_NUMBER) return false;
            if (header.versionMajor != COMPRESSED_FORMAT_VERSION_MAJOR) return false;
            if (header.compressionType != COMPRESSION_TYPE_ZLIB) return false;

            if (header.metadataOffset == 0 || header.metadataSize < sizeof(MetadataBlock)) return false;

            reader.seek(static_cast<std::streampos>(header.metadataOffset));
            MetadataBlock block{};
            if (reader.readBytes(reinterpret_cast<char*>(&block), sizeof(MetadataBlock)) != sizeof(MetadataBlock)) return false;

            outSummary.metadata.seed = block.seed;
            outSummary.metadata.frequency = block.frequency;
            outSummary.metadata.noiseType = trimNullTerminated(block.noiseType, sizeof(block.noiseType));
            outSummary.metadata.fractalType = trimNullTerminated(block.fractalType, sizeof(block.fractalType));
            outSummary.metadata.octaves = block.octaves;
            outSummary.metadata.lacunarity = block.lacunarity;
            outSummary.metadata.gain = block.gain;
            outSummary.chunkCount = header.chunkCount;
            outSummary.path = tlwzPath;
            outSummary.compressed = true;
            outSummary.fileSize = std::filesystem::file_size(tlwzPath);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool MapSerializer::updateMetadata(const std::string& saveName, const std::string& directory, const WorldMetadata& metadata) {
        std::string tlwfPath = getTlwfPath(saveName, directory);
        std::string tlwzPath = getTlwzPath(saveName, directory);
        bool updated = false;

        if (std::filesystem::exists(tlwfPath)) {
            std::ifstream file(tlwfPath, std::ios::binary | std::ios::ate);
            if (file) {
                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> buffer(static_cast<size_t>(size));
                if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
                    if (applyMetadataToBuffer(buffer, metadata) && writeBufferToFile(tlwfPath, buffer)) {
                        updated = true;
                        if (std::filesystem::exists(tlwzPath)) {
                            // 提取元数据块和区块数量用于重写 tlwz
                            MetadataBlock metaBlock{};
                            FileHeader fileHeader{};
                            std::memcpy(&fileHeader, buffer.data(), sizeof(fileHeader));
                            size_t metaOffset = static_cast<size_t>(fileHeader.metadataOffset);
                            if (metaOffset != 0 && metaOffset + sizeof(MetadataBlock) <= buffer.size()) {
                                std::memcpy(&metaBlock, buffer.data() + metaOffset, sizeof(MetadataBlock));
                            }
                            size_t chunkCount = 0;
                            size_t indexOffset = static_cast<size_t>(fileHeader.indexOffset);
                            if (indexOffset != 0 && indexOffset + sizeof(size_t) <= buffer.size()) {
                                std::memcpy(&chunkCount, buffer.data() + indexOffset, sizeof(size_t));
                            }
                            writeTlwzV2(buffer, metaBlock, chunkCount, tlwzPath);
                        }
                    }
                }
            }
        } else if (std::filesystem::exists(tlwzPath)) {
            try {
                BinaryReader reader(tlwzPath);

                CompressedFileHeader header{};
                if (!reader.read(header)) return false;
                if (header.magicNumber != COMPRESSED_MAGIC_NUMBER) return false;
                if (header.versionMajor != COMPRESSED_FORMAT_VERSION_MAJOR) return false;
                if (header.compressionType != COMPRESSION_TYPE_ZLIB) return false;

                std::vector<Bytef> compressedData(static_cast<size_t>(header.compressedSize));
                reader.seek(static_cast<std::streampos>(header.compressedDataOffset));
                size_t bytesRead = reader.readBytes(reinterpret_cast<char*>(compressedData.data()), compressedData.size());
                if (bytesRead != header.compressedSize) return false;
                if (calculateCRC32(compressedData.data(), compressedData.size()) != header.compressedChecksum) return false;

                std::vector<Bytef> decompressed;
                auto status = SimpZlib::uncompress(compressedData, decompressed, header.uncompressedSize);
                if (status != SimpZlib::Status::OK || decompressed.size() != header.uncompressedSize) return false;
                if (calculateCRC32(decompressed.data(), decompressed.size()) != header.uncompressedChecksum) return false;

                std::vector<uint8_t> buffer(decompressed.begin(), decompressed.end());
                size_t chunkCount = header.chunkCount;

                if (!applyMetadataToBuffer(buffer, metadata)) return false;

                // 提取更新后的元数据块
                MetadataBlock metaBlock{};
                FileHeader fileHeader{};
                std::memcpy(&fileHeader, buffer.data(), sizeof(fileHeader));
                size_t metaOffset = static_cast<size_t>(fileHeader.metadataOffset);
                if (metaOffset != 0 && metaOffset + sizeof(MetadataBlock) <= buffer.size()) {
                    std::memcpy(&metaBlock, buffer.data() + metaOffset, sizeof(MetadataBlock));
                }

                updated = writeTlwzV2(buffer, metaBlock, chunkCount, tlwzPath);
            } catch (...) {
                return false;
            }
        }

        return updated;
    }

    // --- saveCompressedMap Implementation   ---
    bool MapSerializer::saveCompressedMap(const Map& map, const std::string& saveName, const std::string& directory, bool deleteTlwfAfterwards) {
        std::string tlwfPath = getTlwfPath(saveName, directory);
        std::string tlwzPath = getTlwzPath(saveName, directory);

        LOG_INFO("Starting save compressed map process for '" + saveName + "'...");

        // 1. Serialize map to memory buffer
        MemoryWriter memoryWriter;
        if (!serializeMapToWriter(map, memoryWriter, nullptr, nullptr)) {
            LOG_ERROR("Failed to serialize map to memory for compression.");
            return false;
        }

        const auto& rawBuffer = memoryWriter.buffer();
        if (rawBuffer.empty()) {
            LOG_WARNING("Serialized map buffer is empty. Skipping compression.");
            if (!deleteTlwfAfterwards) {
                if (!writeBufferToFile(tlwfPath, rawBuffer)) {
                    LOG_ERROR("Failed to write empty .tlwf file.");
                    return false;
                }
            }
            return true;
        }

        if (!deleteTlwfAfterwards) {
            if (!writeBufferToFile(tlwfPath, rawBuffer)) {
                LOG_ERROR("Failed to write .tlwf file from memory buffer.");
                return false;
            }
        }

        // 从序列化的缓冲区中提取元数据块和区块数量 (用于写入 tlwz V2 头部)
        MetadataBlock metaBlock{};
        size_t chunkCount = 0;
        {
            FileHeader fileHeader{};
            std::memcpy(&fileHeader, rawBuffer.data(), std::min(sizeof(FileHeader), rawBuffer.size()));
            size_t metaOffset = static_cast<size_t>(fileHeader.metadataOffset);
            if (metaOffset != 0 && metaOffset + sizeof(MetadataBlock) <= rawBuffer.size()) {
                std::memcpy(&metaBlock, rawBuffer.data() + metaOffset, sizeof(MetadataBlock));
            }
            size_t indexOffset = static_cast<size_t>(fileHeader.indexOffset);
            if (indexOffset != 0 && indexOffset + sizeof(size_t) <= rawBuffer.size()) {
                std::memcpy(&chunkCount, rawBuffer.data() + indexOffset, sizeof(size_t));
            }
        }

        // 压缩并使用 V2 格式写入 .tlwz 文件 (元数据以未压缩形式存储在头部之后)
        LOG_INFO("Writing compressed data to: " + tlwzPath);
        if (!writeTlwzV2(rawBuffer, metaBlock, chunkCount, tlwzPath)) {
            LOG_ERROR("Failed to write .tlwz file.");
            try { std::filesystem::remove(tlwzPath); } catch(...) {}
            return false;
        }
        LOG_INFO("Compressed save file (.tlwz) written successfully.");

        // 7. (Optional) Delete .tlwf file
        if (deleteTlwfAfterwards) {
            LOG_INFO("Removing .tlwf file: " + tlwfPath);
            try {
                if (!std::filesystem::remove(tlwfPath)) {
                    LOG_WARNING("Failed to delete .tlwf file (it might not exist or is locked).");
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Exception while deleting .tlwf file: " + std::string(e.what()));
            }
        }

        LOG_INFO("Save compressed map process for '" + saveName + "' completed successfully.");
        return true;
    }

    // --- loadMapFromSave Implementation   ---
    std::unique_ptr<Map> MapSerializer::loadMapFromSave(const std::string& saveName, const std::string& directory, bool* outUsedCompressed) {
        std::string tlwfPath = getTlwfPath(saveName, directory);
        std::string tlwzPath = getTlwzPath(saveName, directory);

        LOG_INFO("Starting load map process for '" + saveName + "'...");

        // Attempt 1: Load .tlwf directly
        if (std::filesystem::exists(tlwfPath)) {
            LOG_INFO("Found .tlwf file: " + tlwfPath + ". Attempting direct load...");
            try {
                std::unique_ptr<Map> map = loadMap(tlwfPath);
                if (map) {
                    LOG_INFO("Successfully loaded map directly from .tlwf file.");
                    if (outUsedCompressed) *outUsedCompressed = false;
                    return map;
                } else {
                    LOG_WARNING(".tlwf file exists but failed to load (possibly corrupted). Will attempt to load from .tlwz.");
                    // Proceed to attempt loading from .tlwz
                }
            } catch (const std::exception& e) {
                 LOG_WARNING("Exception during direct .tlwf load: " + std::string(e.what()) + ". Will attempt to load from .tlwz.");
                 // Proceed to attempt loading from .tlwz
            }
        } else {
             LOG_INFO(".tlwf file not found. Will attempt to load from .tlwz.");
        }

        // Attempt 2: Load from .tlwz
        if (std::filesystem::exists(tlwzPath)) {
             LOG_INFO("Found .tlwz file: " + tlwzPath + ". Attempting to load and decompress...");
             try {
                 std::unique_ptr<Map> map = loadFromCompressedFile(tlwzPath);
                 if (map && outUsedCompressed) *outUsedCompressed = true;
                 return map;
             } catch (const std::exception& e) {
                 LOG_ERROR("Failed to load from .tlwz file: " + std::string(e.what()));
                 return nullptr; // Loading from .tlwz failed
             }
        } else {
             LOG_ERROR("Save file not found. Neither .tlwf nor .tlwz exists for save name '" + saveName + "'.");
             return nullptr; // No save file found
        }
    }

    // --- loadFromCompressedFile Helper   ---
    std::unique_ptr<Map> MapSerializer::loadFromCompressedFile(const std::string& tlwzPath) {
        std::vector<Bytef> compressedData;
        std::vector<Bytef> decompressedData;
        CompressedFileHeader header = {};

        // 1. Read .tlwz header and compressed data
        try {
            BinaryReader reader(tlwzPath);

            if (!reader.read(header)) {
                throw std::runtime_error("Failed to read compressed file header.");
            }
            if (header.magicNumber != COMPRESSED_MAGIC_NUMBER) {
                throw std::runtime_error("Invalid magic number in compressed file.");
            }
            if (header.versionMajor != COMPRESSED_FORMAT_VERSION_MAJOR) {
                throw std::runtime_error("Unsupported compressed file version: "
                    + std::to_string(header.versionMajor) + "." + std::to_string(header.versionMinor));
            }
            if (header.compressionType != COMPRESSION_TYPE_ZLIB) {
                 throw std::runtime_error("Unsupported compression type in header.");
            }
            LOG_INFO("Compressed header validated. Uncompressed size: " + std::to_string(header.uncompressedSize)
                     + ", Compressed size: " + std::to_string(header.compressedSize));

            reader.seek(static_cast<std::streampos>(header.compressedDataOffset));

            // Read compressed data
            compressedData.resize(static_cast<size_t>(header.compressedSize));
            size_t bytesRead = reader.readBytes(reinterpret_cast<char*>(compressedData.data()), compressedData.size());
            if (bytesRead != header.compressedSize) {
                 throw std::runtime_error("Failed to read complete compressed data. Expected "
                                          + std::to_string(header.compressedSize) + ", got " + std::to_string(bytesRead));
            }
            LOG_INFO("Read " + std::to_string(bytesRead) + " bytes of compressed data.");

        } catch (const std::exception& e) {
            LOG_ERROR("Error reading .tlwz file: " + std::string(e.what()));
            return nullptr;
        }

        // 2. Verify compressed data checksum (Optional but recommended)
        uint32_t calculatedCompressedChecksum = calculateCRC32(compressedData.data(), compressedData.size());
        if (calculatedCompressedChecksum != header.compressedChecksum) {
             LOG_ERROR("Compressed data checksum mismatch! Expected 0x" + std::to_string(header.compressedChecksum)
                       + ", Calculated 0x" + std::to_string(calculatedCompressedChecksum)); // Consider hex
             return nullptr;
        }
        LOG_INFO("Compressed data checksum verified.");

        // 3. Decompress data
        LOG_INFO("Decompressing data...");
        SimpZlib::Status decompressStatus = SimpZlib::uncompress(compressedData, decompressedData, header.uncompressedSize);

        if (decompressStatus != SimpZlib::Status::OK) {
             LOG_ERROR("Decompression failed with status: " + std::to_string(static_cast<int>(decompressStatus)));
             return nullptr;
        }
        if (decompressedData.size() != header.uncompressedSize) {
             // This check might be redundant if SimpZlib::uncompress already ensures size match
             LOG_ERROR("Decompressed size mismatch. Expected " + std::to_string(header.uncompressedSize)
                       + ", got " + std::to_string(decompressedData.size()));
             return nullptr;
        }
        LOG_INFO("Decompression successful. Decompressed size: " + std::to_string(decompressedData.size()) + " bytes.");

        // 4. Verify uncompressed data checksum
        uint32_t calculatedUncompressedChecksum = calculateCRC32(decompressedData.data(), decompressedData.size());
        if (calculatedUncompressedChecksum != header.uncompressedChecksum) {
             LOG_ERROR("Uncompressed data checksum mismatch! Expected 0x" + std::to_string(header.uncompressedChecksum)
                       + ", Calculated 0x" + std::to_string(calculatedUncompressedChecksum)); // Consider hex
             return nullptr;
        }
        LOG_INFO("Uncompressed data checksum verified.");

        // 5. Load map from the decompressed buffer
        LOG_INFO("Loading map from decompressed buffer...");
        try {
             MemoryReader reader(reinterpret_cast<const uint8_t*>(decompressedData.data()), decompressedData.size());
             std::unique_ptr<Map> map = loadMapFromReader(reader);
             if (map) {
                 LOG_INFO("Successfully loaded map from decompressed buffer.");
                 return map;
             } else {
                 LOG_ERROR("Failed to load map from decompressed buffer.");
                 return nullptr;
             }
        } catch (const std::exception& e) {
             LOG_ERROR("Exception during final map load from decompressed buffer: " + std::string(e.what()));
             return nullptr;
        }
    }

} // namespace TilelandWorld
