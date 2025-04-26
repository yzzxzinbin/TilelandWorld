#include "../Map.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include "../BinaryFileInfrastructure/BinaryReader.h"
#include "../BinaryFileInfrastructure/FileFormat.h"
#include "../BinaryFileInfrastructure/Checksum.h"
#include "../Constants.h"
#include "../Tile.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip> // For std::hex, std::setw, std::setfill
#include <cmath>   // For std::min, std::max
#include <limits>  // For std::numeric_limits
#include <cstring> // Include for memcpy

// Platform-specific includes and setup for virtual terminal processing
#ifdef _WIN32
#include <windows.h>
#endif

// 使用 TilelandWorld 命名空间
using namespace TilelandWorld;

// 定义测试文件名
const std::string testMapFilePath = "map_serializer_test.bin";

// --- Copied from TileMainTest.cpp ---
// Helper to generate ANSI 24-bit color escape codes
std::string formatTileForTerminal(const TilelandWorld::Tile& tile) {
    // Check if the tile is explored. If not, display a default "unexplored" representation.
    if (!tile.isExplored) {
        // Example: Dark gray background, slightly lighter gray foreground for '?'
        return "\x1b[48;2;50;50;50m\x1b[38;2;100;100;100m?\x1b[0m";
    }

    TilelandWorld::RGBColor fg = tile.getForegroundColor();
    TilelandWorld::RGBColor bg = tile.getBackgroundColor();
    std::string displayChar = tile.getDisplayChar();

    // ANSI escape codes for 24-bit color
    std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
    std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
    std::string resetCode = "\x1b[0m"; // Reset all attributes

    return bgCode + fgCode + displayChar + resetCode;
}
// --- End Copied Section ---

// Function to print a specific Z-layer of the map to the terminal with coordinates and chunk separators
void printMapLayerToTerminal(const Map& map, int zLayer, int startX, int startY, int width, int height) {
    std::cout << "\n--- Map Layer Z=" << zLayer
              << " (Area: X=" << startX << " to " << startX + width - 1
              << ", Y=" << startY << " to " << startY + height - 1 << ") ---" << std::endl;

    // Print Column Headers (X coordinates)
    std::cout << "    "; // Indent for row headers
    for (int x = startX; x < startX + width; ++x) {
        // Add separator before chunk boundaries (except the very first column)
        if (x != startX && x % CHUNK_WIDTH == 0) {
            std::cout << " "; // Chunk separator
        }
        // Print X coordinate, adjusting width for spacing
        std::cout << std::setw(2) << (x % 100); // Show last two digits of X coord
    }
    std::cout << std::endl;

    // Print Top Border Line
    std::cout << "    ";
    for (int x = startX; x < startX + width; ++x) {
        if (x != startX && x % CHUNK_WIDTH == 0) {
            std::cout << "+"; // Separator intersection
        }
        std::cout << "--"; // Two dashes per tile
    }
    std::cout << std::endl;


    for (int y = startY; y < startY + height; ++y) {
         // Add separator row before chunk boundaries (except the very first row)
        if (y != startY && y % CHUNK_HEIGHT == 0) {
            std::cout << "    "; // Indent
            for (int x = startX; x < startX + width; ++x) {
                 if (x != startX && x % CHUNK_WIDTH == 0) {
                     std::cout << "+"; // Intersection
                 }
                 std::cout << "--";
            }
            std::cout << std::endl;
        }

        // Print Row Header (Y coordinate)
        std::cout << std::setw(3) << y << "|"; // Print Y coord and separator

        for (int x = startX; x < startX + width; ++x) {
            // Add separator before chunk boundaries (except the very first column)
             if (x != startX && x % CHUNK_WIDTH == 0) {
                 std::cout << "|"; // Vertical chunk separator
             }
            try {
                const Tile& tile = map.getTile(x, y, zLayer);
                // Print two characters per tile for better aspect ratio
                std::cout << formatTileForTerminal(tile) << formatTileForTerminal(tile);
            } catch (const std::exception& e) {
                std::cerr << "EE"; // Print 'EE' for error accessing tile
            }
        }
        std::cout << "\n"; // Newline after each row
    }
    std::cout << "---------------------------------------" << std::endl; // Footer
}

// 运行地图序列化测试
bool runMapSerializerTests() {
    std::cout << "--- Running Map Serializer Tests ---" << std::endl;
    bool allTestsPassed = true;

#ifdef _WIN32
    // Enable virtual terminal processing on Windows for ANSI escape codes
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    // Set console code page to UTF-8 (optional but recommended)
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif // _WIN32

    const int mapSizeX = 32;
    const int mapSizeY = 32;
    const int mapSizeZ = 1; // Only one layer
    const int startX = 0; // Define the starting coordinates for printing
    const int startY = 0;

    // --- 1. Create and Populate Map ---
    std::cout << "Creating and populating map (" << mapSizeX << "x" << mapSizeY << "x" << mapSizeZ << ")..." << std::endl;
    Map map;
    const float maxCoordSum = static_cast<float>(mapSizeX - 1 + mapSizeY - 1);

    for (int y = 0; y < mapSizeY; ++y) {
        for (int x = 0; x < mapSizeX; ++x) {
            for (int z = 0; z < mapSizeZ; ++z) {
                // Set terrain to grass
                map.setTileTerrain(x, y, z, TerrainType::GRASS);

                // Apply light gradient
                float gradientPos = static_cast<float>(x + y);
                uint8_t currentLightLevel = static_cast<uint8_t>(
                    std::min(static_cast<float>(MAX_LIGHT_LEVEL), // Clamp to max
                             (gradientPos / maxCoordSum) * MAX_LIGHT_LEVEL)
                );

                Tile& tile = map.getTile(x, y, z);
                tile.lightLevel = currentLightLevel;
                tile.isExplored = true; // Mark as explored for potential later checks
            }
        }
    }
    std::cout << "Map populated." << std::endl;

    // --- 2. Save Map ---
    std::cout << "Saving map to '" << testMapFilePath << "'..." << std::endl;
    if (!MapSerializer::saveMap(map, testMapFilePath)) {
        std::cerr << "Failed to save map!" << std::endl;
        return false; // Critical failure
    }
    std::cout << "Map saved." << std::endl;

    // --- 3. Load Map (Optional Verification Step) ---
    std::cout << "Loading map back for verification..." << std::endl;
    auto loadedMap = MapSerializer::loadMap(testMapFilePath);
    if (!loadedMap) {
        std::cerr << "Failed to load map!" << std::endl;
        allTestsPassed = false;
    } else {
        std::cout << "Map loaded successfully." << std::endl;
        // Optional: Add checks here to compare loadedMap tiles with original map
        try {
             Tile& loadedTile = loadedMap->getTile(mapSizeX - 1, mapSizeY - 1, 0);
             assert(loadedTile.terrain == TerrainType::GRASS);
             assert(loadedTile.lightLevel == MAX_LIGHT_LEVEL); // Bottom-right should be max light
             Tile& loadedTileOrigin = loadedMap->getTile(0, 0, 0);
             assert(loadedTileOrigin.terrain == TerrainType::GRASS);
             assert(loadedTileOrigin.lightLevel == 0); // Top-left should be min light
             std::cout << "Basic verification of loaded map passed." << std::endl;
        } catch (const std::exception& e) {
             std::cerr << "Verification failed: " << e.what() << std::endl;
             allTestsPassed = false;
        }

        // --- Print Loaded Map ---
        // Use the new function with starting coordinates
        printMapLayerToTerminal(*loadedMap, 0, startX, startY, mapSizeX, mapSizeY);
    }

    // --- 4. Manually Read and Print File Contents ---
    std::cout << "\n--- Manually Reading and Printing File Contents ---" << std::endl;
    try {
        BinaryReader reader(testMapFilePath);
        if (!reader.good()) {
            throw std::runtime_error("Failed to open file for manual reading.");
        }

        std::cout << "File Size: " << reader.fileSize() << " bytes" << std::endl;

        // Read and Print Header
        std::cout << "\n[File Header]" << std::endl;
        FileHeader header = {};
        if (!reader.read(header)) { // Read the whole header struct at once
             throw std::runtime_error("Failed to read file header.");
        }
        std::cout << "  Magic Number: 0x" << std::hex << header.magicNumber << std::dec
                  << " (" << (header.magicNumber == MAGIC_NUMBER ? "OK" : "Mismatch!") << ")" << std::endl;
        std::cout << "  Version:      " << header.versionMajor << "." << header.versionMinor << std::endl;
        std::cout << "  Metadata Off: " << header.metadataOffset << std::endl;
        std::cout << "  Index Offset: " << header.indexOffset << std::endl;
        std::cout << "  Data Offset:  " << header.dataOffset << std::endl;
        std::cout << "  Header Checksum: 0x" << std::hex << header.headerChecksum << std::dec << std::endl;

        // Verify header checksum manually (as done in readAndValidateHeader)
        FileHeader tempHeader = header;
        tempHeader.headerChecksum = 0;
        uint32_t calculatedHeaderChecksum = calculateXORChecksum(&tempHeader, sizeof(FileHeader) - sizeof(uint32_t));
        std::cout << "  Calculated Hdr Checksum: 0x" << std::hex << calculatedHeaderChecksum << std::dec
                  << " (" << (calculatedHeaderChecksum == header.headerChecksum ? "OK" : "Mismatch!") << ")" << std::endl;
        assert(calculatedHeaderChecksum == header.headerChecksum);


        // Read and Print Index
        std::cout << "\n[Chunk Index (at offset " << header.indexOffset << ")]" << std::endl;
        if (header.indexOffset == 0 || !reader.seek(header.indexOffset)) {
             throw std::runtime_error("Invalid or failed to seek to index offset.");
        }
        size_t indexCount = 0;
        if (!reader.read(indexCount)) {
             throw std::runtime_error("Failed to read index count.");
        }
        std::cout << "  Index Count: " << indexCount << std::endl;
        assert(indexCount == 4); // Expecting 4 chunks for 32x32 map

        std::vector<ChunkIndexEntry> indexEntries(indexCount);
        if (indexCount > 0) {
             size_t indexBytesToRead = indexCount * sizeof(ChunkIndexEntry);
             size_t indexBytesRead = reader.readBytes(reinterpret_cast<char*>(indexEntries.data()), indexBytesToRead);
             if (indexBytesRead != indexBytesToRead) {
                 throw std::runtime_error("Failed to read all index entries.");
             }
        }

        for (size_t i = 0; i < indexEntries.size(); ++i) {
            const auto& entry = indexEntries[i];
            std::cout << "  Entry " << i << ":" << std::endl;
            std::cout << "    Coords: (" << entry.cx << ", " << entry.cy << ", " << entry.cz << ")" << std::endl;
            std::cout << "    Offset: " << entry.offset << std::endl;
            std::cout << "    Size:   " << entry.size << " bytes" << std::endl;
            std::cout << "    Checksum: 0x" << std::hex << entry.checksum << std::dec << std::endl;
            assert(entry.size == sizeof(Tile) * CHUNK_VOLUME); // Verify expected chunk size
        }

        // Read and Verify Chunk Data (Basic Verification)
        std::cout << "\n[Chunk Data Verification (at offset " << header.dataOffset << ")]" << std::endl;
        assert(header.dataOffset == sizeof(FileHeader)); // Check if data starts right after header

        for (size_t i = 0; i < indexEntries.size(); ++i) {
             const auto& entry = indexEntries[i];
             std::cout << "  Verifying Chunk (" << entry.cx << ", " << entry.cy << ", " << entry.cz << ") at offset " << entry.offset << ":" << std::endl;

             if (!reader.seek(entry.offset)) {
                 std::cerr << "    Failed to seek to chunk data offset!" << std::endl;
                 allTestsPassed = false;
                 continue;
             }

             // Read chunk data into a temporary buffer
             std::vector<char> chunkBuffer(entry.size);
             size_t chunkBytesRead = reader.readBytes(chunkBuffer.data(), entry.size);

             if (chunkBytesRead != entry.size) {
                 std::cerr << "    Failed to read complete chunk data!" << std::endl;
                 allTestsPassed = false;
                 continue;
             }

             // Verify checksum
             uint32_t calculatedDataChecksum = calculateXORChecksum(chunkBuffer.data(), entry.size);
             std::cout << "    Data Checksum: Expected=0x" << std::hex << entry.checksum
                       << ", Calculated=0x" << calculatedDataChecksum << std::dec
                       << " (" << (calculatedDataChecksum == entry.checksum ? "OK" : "Mismatch!") << ")" << std::endl;
             assert(calculatedDataChecksum == entry.checksum);

             // Optional: Verify first tile data
             if (entry.size >= sizeof(Tile)) {
                 Tile firstTile;
                 memcpy(&firstTile, chunkBuffer.data(), sizeof(Tile));
                 std::cout << "    First Tile Terrain: " << static_cast<int>(firstTile.terrain)
                           << " (Expected GRASS=" << static_cast<int>(TerrainType::GRASS) << ")" << std::endl;
                 assert(firstTile.terrain == TerrainType::GRASS);
                 // Light level check depends on which chunk this is
                 if (entry.cx == 0 && entry.cy == 0 && entry.cz == 0) {
                     std::cout << "    First Tile Light: " << static_cast<int>(firstTile.lightLevel) << " (Expected 0)" << std::endl;
                     assert(firstTile.lightLevel == 0);
                 }
             }
        }

        std::cout << "\nManual file reading finished." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Manual file reading failed with exception: " << e.what() << std::endl;
        allTestsPassed = false;
    }


    std::cout << "\n--- Map Serializer Tests " << (allTestsPassed ? "Passed" : "Failed") << " ---" << std::endl;
    return allTestsPassed;
}

int main() {
    if (runMapSerializerTests()) {
        return 0; // 成功
    } else {
        return 1; // 失败
    }
}
