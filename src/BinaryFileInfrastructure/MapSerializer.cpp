#include "MapSerializer.h"
#include "../Constants.h" // For CHUNK_VOLUME, etc.
#include "Checksum.h" // Include Checksum header
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <iostream> // For std::cout in success messages
#include <vector>
#include <cstring> // For memcpy in checksum calculation
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
    bool MapSerializer::saveMap(const Map& map, const std::string& filepath, const std::unordered_set<ChunkCoord, ChunkCoordHash>* modifiedChunks) {
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
            // 预估大小，如果过滤则可能小于 loadedChunks.size()
            index.reserve(modifiedChunks ? modifiedChunks->size() : map.loadedChunks.size());

            for (const auto& pair : map.loadedChunks) {
                // 4. "只保存修改区块"逻辑：查找修改表，跳过不需要保存的项目
                if (modifiedChunks != nullptr) {
                    if (modifiedChunks->find(pair.first) == modifiedChunks->end()) {
                        continue; // 该区块未被修改，跳过保存
                    }
                }

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

    // --- Path Helpers (moved from MapPersistenceManager) ---
    std::string MapSerializer::getTlwfPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwf")).string();
    }

    std::string MapSerializer::getTlwzPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwz")).string();
    }

    // --- saveCompressedMap Implementation (moved from MapPersistenceManager) ---
    bool MapSerializer::saveCompressedMap(const Map& map, const std::string& saveName, const std::string& directory, bool deleteTlwfAfterwards) {
        std::string tlwfPath = getTlwfPath(saveName, directory);
        std::string tlwzPath = getTlwzPath(saveName, directory);

        LOG_INFO("Starting save compressed map process for '" + saveName + "'...");

        // 1. Save uncompressed map to .tlwf
        LOG_INFO("Saving uncompressed map to: " + tlwfPath);
        if (!saveMap(map, tlwfPath)) {
            LOG_ERROR("Failed to save uncompressed map to .tlwf file.");
            return false;
        }
        LOG_INFO("Uncompressed map saved successfully.");

        // 2. Read the entire .tlwf file
        std::vector<Bytef> uncompressedData;
        try {
            std::ifstream tlwfFile(tlwfPath, std::ios::binary | std::ios::ate);
            if (!tlwfFile) {
                throw std::runtime_error("Failed to open .tlwf file for reading.");
            }
            std::streamsize size = tlwfFile.tellg();
            tlwfFile.seekg(0, std::ios::beg);
            uncompressedData.resize(static_cast<size_t>(size)); // Resize vector
            if (!tlwfFile.read(reinterpret_cast<char*>(uncompressedData.data()), size)) {
                 throw std::runtime_error("Failed to read data from .tlwf file.");
            }
            LOG_INFO("Read " + std::to_string(uncompressedData.size()) + " bytes from " + tlwfPath);
        } catch (const std::exception& e) {
            LOG_ERROR("Error reading .tlwf file: " + std::string(e.what()));
            return false;
        }

        if (uncompressedData.empty()) {
             LOG_WARNING(".tlwf file is empty. Skipping compression.");
             // Decide if an empty tlwz should be created or not. Let's skip for now.
             // If deleteTlwfAfterwards is true, the empty tlwf might be deleted later.
             return true; // Or false depending on desired behavior for empty maps
        }

        // 3. Calculate uncompressed checksum
        uint32_t uncompressedChecksum = calculateCRC32(uncompressedData.data(), uncompressedData.size());
        LOG_INFO("Calculated uncompressed CRC32: 0x" + std::to_string(uncompressedChecksum)); // Consider hex formatting

        // 4. Compress data
        std::vector<Bytef> compressedData;
        LOG_INFO("Compressing data using SimpZlib...");
        SimpZlib::Status compressStatus = SimpZlib::compress(uncompressedData, compressedData); // Use default level

        if (compressStatus != SimpZlib::Status::OK) {
            LOG_ERROR("Compression failed with status: " + std::to_string(static_cast<int>(compressStatus)));
            return false;
        }
        LOG_INFO("Compression successful. Compressed size: " + std::to_string(compressedData.size()) + " bytes.");

        // 5. Calculate compressed checksum (Optional but recommended)
        uint32_t compressedChecksum = calculateCRC32(compressedData.data(), compressedData.size());
        LOG_INFO("Calculated compressed CRC32: 0x" + std::to_string(compressedChecksum)); // Consider hex formatting

        // 6. Write .tlwz file
        LOG_INFO("Writing compressed data to: " + tlwzPath);
        try {
            BinaryWriter writer(tlwzPath);

            // Prepare header
            CompressedFileHeader header = {};
            header.magicNumber = COMPRESSED_MAGIC_NUMBER;
            header.versionMajor = COMPRESSED_FORMAT_VERSION_MAJOR;
            header.versionMinor = COMPRESSED_FORMAT_VERSION_MINOR;
            header.compressionType = COMPRESSION_TYPE_ZLIB;
            header.uncompressedSize = uncompressedData.size();
            header.uncompressedChecksum = uncompressedChecksum;
            header.compressedSize = compressedData.size();
            header.compressedChecksum = compressedChecksum; // Write the compressed checksum

            // Write header
            if (!writer.write(header)) {
                throw std::runtime_error("Failed to write compressed file header.");
            }

            // Write compressed data
            if (!writer.writeBytes(reinterpret_cast<const char*>(compressedData.data()), compressedData.size())) {
                 throw std::runtime_error("Failed to write compressed data.");
            }

            LOG_INFO("Compressed save file (.tlwz) written successfully.");

        } catch (const std::exception& e) {
            LOG_ERROR("Error writing .tlwz file: " + std::string(e.what()));
            // Attempt to clean up potentially incomplete .tlwz file
            try { std::filesystem::remove(tlwzPath); } catch(...) {}
            return false;
        }

        // 7. (Optional) Delete .tlwf file
        if (deleteTlwfAfterwards) {
            LOG_INFO("Deleting temporary .tlwf file: " + tlwfPath);
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

    // --- loadMapFromSave Implementation (moved from MapPersistenceManager) ---
    std::unique_ptr<Map> MapSerializer::loadMapFromSave(const std::string& saveName, const std::string& directory) {
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
                 return loadFromCompressedFile(tlwzPath, tlwfPath);
             } catch (const std::exception& e) {
                 LOG_ERROR("Failed to load from .tlwz file: " + std::string(e.what()));
                 return nullptr; // Loading from .tlwz failed
             }
        } else {
             LOG_ERROR("Save file not found. Neither .tlwf nor .tlwz exists for save name '" + saveName + "'.");
             return nullptr; // No save file found
        }
    }

    // --- loadFromCompressedFile Helper (moved from MapPersistenceManager) ---
    std::unique_ptr<Map> MapSerializer::loadFromCompressedFile(const std::string& tlwzPath, const std::string& tlwfPath) {
        std::vector<Bytef> compressedData;
        std::vector<Bytef> decompressedData;
        CompressedFileHeader header = {};

        // 1. Read .tlwz header and compressed data
        try {
            BinaryReader reader(tlwzPath);

            // Read and validate header
            if (!reader.read(header)) {
                throw std::runtime_error("Failed to read compressed file header.");
            }
            if (header.magicNumber != COMPRESSED_MAGIC_NUMBER) {
                throw std::runtime_error("Invalid magic number in compressed file.");
            }
            if (header.versionMajor != COMPRESSED_FORMAT_VERSION_MAJOR || header.versionMinor > COMPRESSED_FORMAT_VERSION_MINOR) {
                 throw std::runtime_error("Unsupported compressed file version.");
            }
            if (header.compressionType != COMPRESSION_TYPE_ZLIB) {
                 throw std::runtime_error("Unsupported compression type in header.");
            }
            LOG_INFO("Compressed header validated. Uncompressed size: " + std::to_string(header.uncompressedSize)
                     + ", Compressed size: " + std::to_string(header.compressedSize));

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

        // 5. Write decompressed data to .tlwf
        LOG_INFO("Writing decompressed data to .tlwf file: " + tlwfPath);
        try {
            BinaryWriter writer(tlwfPath); // Opens in trunc mode, overwriting if exists
            if (!writer.writeBytes(reinterpret_cast<const char*>(decompressedData.data()), decompressedData.size())) {
                 throw std::runtime_error("Failed to write decompressed data to .tlwf file.");
            }
            LOG_INFO(".tlwf file created/updated from decompressed data.");
        } catch (const std::exception& e) {
            LOG_ERROR("Error writing decompressed data to .tlwf: " + std::string(e.what()));
            return nullptr;
        }

        // 6. Load map from the newly created .tlwf
        LOG_INFO("Attempting to load map from the generated .tlwf file...");
        try {
             std::unique_ptr<Map> map = loadMap(tlwfPath);
             if (map) {
                 LOG_INFO("Successfully loaded map from decompressed .tlwf file.");
                 return map;
             } else {
                 LOG_ERROR("Failed to load map from the generated .tlwf file even after successful decompression and write.");
                 return nullptr;
             }
        } catch (const std::exception& e) {
             LOG_ERROR("Exception during final map load from generated .tlwf: " + std::string(e.what()));
             return nullptr;
        }
    }

} // namespace TilelandWorld
