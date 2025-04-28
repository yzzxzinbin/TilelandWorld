#include "MapPersistenceManager.h"
#include "MapSerializer.h"
#include "CompressedFileFormat.h"
#include "Checksum.h"
#include "../ZipFuncInfrastructure/zlib_wrapper.h" // 包含 zlib 封装
#include "BinaryReader.h"
#include "BinaryWriter.h"
#include "../Utils/Logger.h"
#include <vector>
#include <fstream>      // For std::ifstream to read whole file
#include <filesystem>   // For file operations like exists, remove
#include <stdexcept>    // For exceptions

// Helper to define Bytef if not already (should be in zlib.h)
// using Bytef = unsigned char; // Usually defined in zlib.h

namespace TilelandWorld {

    // --- Path Helpers ---
    std::string MapPersistenceManager::getTlwfPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwf")).string();
    }

    std::string MapPersistenceManager::getTlwzPath(const std::string& saveName, const std::string& directory) {
        std::filesystem::path dirPath(directory);
        return (dirPath / (saveName + ".tlwz")).string();
    }

    // --- saveMap Implementation (Renamed from saveGame) ---
    bool MapPersistenceManager::saveMap(const Map& map, const std::string& saveName, const std::string& savesDirectory, bool deleteTlwfAfterwards) {
        std::string tlwfPath = getTlwfPath(saveName, savesDirectory);
        std::string tlwzPath = getTlwzPath(saveName, savesDirectory);

        LOG_INFO("Starting save map process for '" + saveName + "'...");

        // 1. Save uncompressed map to .tlwf
        LOG_INFO("Saving uncompressed map to: " + tlwfPath);
        if (!MapSerializer::saveMap(map, tlwfPath)) {
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

        LOG_INFO("Save map process for '" + saveName + "' completed successfully.");
        return true;
    }

    // --- loadMapFromSave Implementation ---
    std::unique_ptr<Map> MapPersistenceManager::loadMapFromSave(const std::string& saveName, const std::string& savesDirectory) {
        std::string tlwfPath = getTlwfPath(saveName, savesDirectory);
        std::string tlwzPath = getTlwzPath(saveName, savesDirectory);

        LOG_INFO("Starting load map process for '" + saveName + "'...");

        // Attempt 1: Load .tlwf directly
        if (std::filesystem::exists(tlwfPath)) {
            LOG_INFO("Found .tlwf file: " + tlwfPath + ". Attempting direct load...");
            try {
                std::unique_ptr<Map> map = MapSerializer::loadMap(tlwfPath);
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

    // --- loadFromCompressedFile Helper ---
    std::unique_ptr<Map> MapPersistenceManager::loadFromCompressedFile(const std::string& tlwzPath, const std::string& tlwfPath) {
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
             std::unique_ptr<Map> map = MapSerializer::loadMap(tlwfPath);
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
