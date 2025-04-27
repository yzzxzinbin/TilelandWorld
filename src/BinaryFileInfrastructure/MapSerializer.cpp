#include "MapSerializer.h"
#include "../Constants.h" // For CHUNK_VOLUME, etc.
#include "Checksum.h" // Include Checksum header
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <iostream> // For std::cout in success messages
#include <vector>
#include <cstring> // For memcpy in checksum calculation
#include <stdexcept> // For std::runtime_error

namespace TilelandWorld {

    // 辅助函数：检测当前系统的字节序 (运行时)
    bool isLittleEndianRuntime() {
        uint16_t test = 1;
        return (*reinterpret_cast<uint8_t*>(&test) == 1);
    }

    // --- 文件头读写 ---
    bool MapSerializer::writeHeader(BinaryWriter& writer, FileHeader& header) {
        header.endianness = isLittleEndianRuntime() ? ENDIANNESS_LITTLE : ENDIANNESS_BIG;
        header.checksumType = CHECKSUM_TYPE_CRC32;
        header.reserved = 0;

        FileHeader tempHeader = header;
        tempHeader.headerChecksum = 0;
        header.headerChecksum = calculateCRC32(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));

        return writer.write(header);
    }

    void MapSerializer::readAndValidateHeader(BinaryReader& reader, FileHeader& header) {
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

    // --- 区块数据序列化/反序列化 ---
    bool MapSerializer::saveChunkData(BinaryWriter& writer, const Chunk& chunk, uint32_t& outChecksum) {
        const void* dataPtr = chunk.tiles.data();
        size_t dataSize = sizeof(Tile) * CHUNK_VOLUME;

        outChecksum = calculateCRC32(dataPtr, dataSize);

        return writer.writeBytes(static_cast<const char*>(dataPtr), dataSize);
    }

    void MapSerializer::loadChunkData(BinaryReader& reader, Chunk& chunk, uint32_t expectedSize, uint32_t expectedChecksum) {
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

    // --- 索引序列化/反序列化 ---
    bool MapSerializer::writeIndex(BinaryWriter& writer, const std::vector<ChunkIndexEntry>& index) {
        size_t count = index.size();
        if (!writer.write(count)) return false;

        if (count > 0) {
            return writer.writeBytes(reinterpret_cast<const char*>(index.data()), count * sizeof(ChunkIndexEntry));
        }
        return true;
    }

    void MapSerializer::readIndex(BinaryReader& reader, std::vector<ChunkIndexEntry>& index) {
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

    // --- saveMap / loadMap 实现 ---
    bool MapSerializer::saveMap(const Map& map, const std::string& filepath) {
        try {
            BinaryWriter writer(filepath);

            FileHeader header = {};
            header.magicNumber = MAGIC_NUMBER;
            header.versionMajor = FORMAT_VERSION_MAJOR;
            header.versionMinor = FORMAT_VERSION_MINOR;

            if (!writer.seek(0)) return false;
            writer.write(header);

            header.metadataOffset = 0;

            header.dataOffset = writer.tell();
            std::vector<ChunkIndexEntry> index;
            index.reserve(map.loadedChunks.size());

            for (const auto& pair : map.loadedChunks) {
                const Chunk& chunk = *pair.second;
                ChunkIndexEntry entry = {};
                entry.cx = chunk.getChunkX();
                entry.cy = chunk.getChunkY();
                entry.cz = chunk.getChunkZ();

                entry.offset = writer.tell();
                std::streampos startPos = entry.offset;
                if (!saveChunkData(writer, chunk, entry.checksum)) {
                    LOG_ERROR("Failed to save chunk (" + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ") data.");
                    return false;
                }
                std::streampos endPos = writer.tell();
                entry.size = static_cast<uint32_t>(static_cast<uint64_t>(endPos) - static_cast<uint64_t>(startPos));

                index.push_back(entry);
            }

            header.indexOffset = writer.tell();
            if (!writeIndex(writer, index)) {
                LOG_ERROR("Failed to write chunk index.");
                return false;
            }

            std::streampos finalPos = writer.tell();
            if (!writer.seek(0)) return false;
            if (!writeHeader(writer, header)) {
                LOG_ERROR("Failed to write final file header.");
                return false;
            }

            std::cout << "Map saved successfully. Chunk count: " << index.size() << std::endl;
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Exception occurred during map saving: " + std::string(e.what()));
            return false;
        }
    }

    std::unique_ptr<Map> MapSerializer::loadMap(const std::string& filepath) {
        try {
            BinaryReader reader(filepath);

            FileHeader header = {};
            readAndValidateHeader(reader, header);

            std::vector<ChunkIndexEntry> index;
            if (header.indexOffset == 0 || header.indexOffset >= reader.fileSize()) {
                throw std::runtime_error("Invalid or missing index offset in file header.");
            }
            if (!reader.seek(header.indexOffset)) {
                throw std::runtime_error("Failed to seek to index offset.");
            }
            readIndex(reader, index);

            auto map = std::make_unique<Map>();

            for (const auto& entry : index) {
                if (entry.offset == 0 || entry.offset >= reader.fileSize() || (entry.offset + entry.size) > reader.fileSize()) {
                    throw std::runtime_error("Invalid data offset or size for chunk ("
                        + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ")");
                }
                if (!reader.seek(entry.offset)) {
                    throw std::runtime_error("Failed to seek to data offset for chunk ("
                        + std::to_string(entry.cx) + "," + std::to_string(entry.cy) + "," + std::to_string(entry.cz) + ")");
                }

                auto newChunk = std::make_unique<Chunk>(entry.cx, entry.cy, entry.cz);
                loadChunkData(reader, *newChunk, entry.size, entry.checksum);

                map->loadedChunks.emplace(ChunkCoord{entry.cx, entry.cy, entry.cz}, std::move(newChunk));
            }

            std::cout << "Map loaded successfully. Loaded chunk count: " << index.size() << std::endl;
            return map;

        } catch (const std::exception& e) {
            LOG_ERROR("Exception occurred during map loading: " + std::string(e.what()));
            return nullptr;
        }
    }

} // namespace TilelandWorld
