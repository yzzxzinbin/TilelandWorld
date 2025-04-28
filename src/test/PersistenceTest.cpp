#include "../Map.h"
#include "../BinaryFileInfrastructure/MapPersistenceManager.h"
#include "../MapGenInfrastructure/FlatTerrainGenerator.h"
#include "../Utils/Logger.h"
#include "../Chunk.h" // Needed for CHUNK constants and comparing chunks
#include "../Tile.h"   // Needed for comparing tiles
#include "../TerrainTypes.h" // <-- Include for getTerrainProperties
#include "../BinaryFileInfrastructure/FileFormat.h" // <-- Include for FileHeader
#include "../BinaryFileInfrastructure/MapSerializer.h" // <-- Include MapSerializer
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <filesystem> // For cleaning up test files
#include <sstream> // Include for std::stringstream
#include <iomanip> // <-- Include for std::setw
#include <fstream> // For file manipulation in tests

// Platform-specific includes and setup for virtual terminal processing
#ifdef _WIN32
#include <windows.h>
#endif

using namespace TilelandWorld;

// --- Copied Visualization Helpers ---
// Helper to generate ANSI 24-bit color escape codes
std::string formatTileForTerminal(const TilelandWorld::Tile& tile) {
    const auto& props = getTerrainProperties(tile.terrain);

    if (!props.isVisible) {
        return "  \x1b[0m";
    }

    if (!tile.isExplored) {
        return "\x1b[48;2;50;50;50m\x1b[38;2;100;100;100m??\x1b[0m";
    }

    TilelandWorld::RGBColor fg = tile.getForegroundColor();
    TilelandWorld::RGBColor bg = tile.getBackgroundColor();
    std::string displayChar = props.displayChar;

    std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
    std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
    std::string resetCode = "\x1b[0m";

    return bgCode + fgCode + displayChar + displayChar + resetCode;
}

// Function to print a specific Z-layer of the map to the terminal with coordinates and chunk separators
void printMapLayerToTerminal(Map& map, int zLayer, int startX, int startY, int width, int height) {
    std::cout << "\n--- Map Layer Z=" << zLayer
              << " (Area: X=" << startX << " to " << startX + width - 1
              << ", Y=" << startY << " to " << startY + height - 1 << ") ---" << std::endl;

    std::cout << "    ";
    for (int x = startX; x < startX + width; ++x) {
        if (x != startX && x % CHUNK_WIDTH == 0) {
            std::cout << " ";
        }
        std::cout << std::setw(2) << (x % 100);
    }
    std::cout << std::endl;

    std::cout << "    ";
    for (int x = startX; x < startX + width; ++x) {
        if (x != startX && x % CHUNK_WIDTH == 0) {
            std::cout << "+";
        }
        std::cout << "--";
    }
    std::cout << std::endl;

    for (int y = startY; y < startY + height; ++y) {
        if (y != startY && y % CHUNK_HEIGHT == 0) {
            std::cout << "    ";
            for (int x = startX; x < startX + width; ++x) {
                if (x != startX && x % CHUNK_WIDTH == 0) {
                    std::cout << "+";
                }
                std::cout << "--";
            }
            std::cout << std::endl;
        }

        std::cout << std::setw(3) << y << "|";

        for (int x = startX; x < startX + width; ++x) {
            if (x != startX && x % CHUNK_WIDTH == 0) {
                std::cout << "|";
            }
            try {
                Tile& tile = map.getTile(x, y, zLayer);
                std::cout << formatTileForTerminal(tile);
            } catch (const std::exception& e) {
                std::cerr << "EE";
            }
        }
        std::cout << "\n";
    }
    std::cout << "---------------------------------------" << std::endl;
}

// Helper function to compare two maps (Updated to use iterators)
bool compareMaps(const Map& map1, const Map& map2) {
    if (map1.getLoadedChunkCount() != map2.getLoadedChunkCount()) {
        LOG_ERROR("Map comparison failed: Different number of loaded chunks (" 
                  + std::to_string(map1.getLoadedChunkCount()) + " vs " 
                  + std::to_string(map2.getLoadedChunkCount()) + ")");
        return false;
    }

    for (auto it1 = map1.begin(); it1 != map1.end(); ++it1) {
        const ChunkCoord& coord = it1->first;
        const Chunk* chunk1 = it1->second.get();

        const Chunk* chunk2 = map2.getChunk(coord.cx, coord.cy, coord.cz);
        if (chunk2 == nullptr) {
            LOG_ERROR("Map comparison failed: Chunk (" + std::to_string(coord.cx) + "," 
                      + std::to_string(coord.cy) + "," + std::to_string(coord.cz) 
                      + ") exists in map1 but not in map2.");
            return false;
        }

        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
                for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                    const Tile& tile1 = chunk1->getLocalTile(lx, ly, lz);
                    const Tile& tile2 = chunk2->getLocalTile(lx, ly, lz);
                    if (tile1.terrain != tile2.terrain || 
                        tile1.lightLevel != tile2.lightLevel || 
                        tile1.isExplored != tile2.isExplored)
                    {
                        LOG_ERROR("Map comparison failed: Tile mismatch at local (" 
                                  + std::to_string(lx) + "," + std::to_string(ly) + "," + std::to_string(lz) 
                                  + ") in chunk (" + std::to_string(coord.cx) + "," 
                                  + std::to_string(coord.cy) + "," + std::to_string(coord.cz) + ")");
                        LOG_ERROR("  Map1: Terrain=" + std::to_string(static_cast<int>(tile1.terrain)) + ", Light=" + std::to_string(tile1.lightLevel) + ", Explored=" + std::to_string(tile1.isExplored));
                        LOG_ERROR("  Map2: Terrain=" + std::to_string(static_cast<int>(tile2.terrain)) + ", Light=" + std::to_string(tile2.lightLevel) + ", Explored=" + std::to_string(tile2.isExplored));
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Renamed the original test function
bool runSaveLoadCycleTest() {
    LOG_INFO("--- Running Save/Load Cycle Test ---");
    const std::string saveName = "saveload_cycle_test";
    const std::string saveDir = ".";
    const std::string tlwfPath = MapPersistenceManager::getTlwfPath(saveName, saveDir);
    const std::string tlwzPath = MapPersistenceManager::getTlwzPath(saveName, saveDir);

    // Helper lambda for cleanup at the end of this specific test
    auto cleanupTestFiles = [&]() {
        LOG_INFO("Cleaning up cycle test files...");
        try { std::filesystem::remove(tlwfPath); } catch(...) {}
        try { std::filesystem::remove(tlwzPath); } catch(...) {}
    };

    // Cleanup before test
    cleanupTestFiles();

    // 1. Create Original Map
    LOG_INFO("Creating original map...");
    auto originalMap = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(0));
    try {
        originalMap->getTile(0, 0, 0);
        originalMap->setTileTerrain(1, 1, 1, TerrainType::WATER);
        originalMap->getTile(1, 1, 1).isExplored = true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed during original map creation/population: " + std::string(e.what()));
        cleanupTestFiles(); // Clean up before returning
        return false;
    }
    LOG_INFO("Original map populated. Count: " + std::to_string(originalMap->getLoadedChunkCount()));

    // 2. Save Map (deleteTlwf = true)
    LOG_INFO("Saving map (deleteTlwf=true)...");
    bool deleteTlwf = true;
    bool saveSuccess = MapPersistenceManager::saveMap(*originalMap, saveName, saveDir, deleteTlwf);
    if (!saveSuccess) {
        LOG_ERROR("saveMap failed!");
        cleanupTestFiles();
        return false;
    }
    assert(std::filesystem::exists(tlwzPath));
    assert(!std::filesystem::exists(tlwfPath)); // TLWF should be deleted

    // 3. Load Map (should load from TLWZ, recreate TLWF)
    LOG_INFO("Loading map (should use TLWZ)...");
    std::unique_ptr<Map> loadedMap = MapPersistenceManager::loadMapFromSave(saveName, saveDir);
    if (!loadedMap) {
        LOG_ERROR("loadMapFromSave failed!");
        cleanupTestFiles();
        return false;
    }
    // After loading from TLWZ, TLWF should now exist
    assert(std::filesystem::exists(tlwfPath));

    // 4. Compare
    LOG_INFO("Comparing maps...");
    bool mapsMatch = compareMaps(*originalMap, *loadedMap);
    if (!mapsMatch) {
        LOG_ERROR("Map comparison failed!");
        cleanupTestFiles();
        return false;
    }

    // 5. Clean up test files
    cleanupTestFiles();

    // Fix LOG_INFO usage
    std::stringstream ss;
    ss << "--- Save/Load Cycle Test Passed ---";
    LOG_INFO(ss.str());
    return true; // Test passed
}

// New test function for startup simulation
bool runStartupLoadTest() {
    LOG_INFO("--- Running Startup Load Test ---");
    bool overallSuccess = true; // Track overall success across scenarios
    const std::string saveName = "startup_test";
    const std::string saveDir = ".";
    const std::string tlwfPath = MapPersistenceManager::getTlwfPath(saveName, saveDir);
    const std::string tlwzPath = MapPersistenceManager::getTlwzPath(saveName, saveDir);

    // Helper lambda for cleanup between scenarios
    auto cleanupFiles = [&]() {
        try { std::filesystem::remove(tlwfPath); } catch(...) {}
        try { std::filesystem::remove(tlwzPath); } catch(...) {}
    };

    // --- Scenario 1: No Save Exists ---
    { // Scope for scenario 1 variables
        LOG_INFO("[Startup Test Scenario 1: No Save Exists]");
        cleanupFiles(); // Ensure no files exist
        bool scenarioPassed = true;
        std::unique_ptr<Map> mapScenario1 = MapPersistenceManager::loadMapFromSave(saveName, saveDir);
        if (mapScenario1 != nullptr) {
            LOG_ERROR("Scenario 1 FAILED: Loaded a map when no save should exist.");
            scenarioPassed = false;
        } else {
            LOG_INFO("Scenario 1: loadMapFromSave correctly returned nullptr.");
            LOG_INFO("Scenario 1: Generating new map...");
            mapScenario1 = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(1)); // Ground at Z=0
            if (mapScenario1 == nullptr) {
                LOG_ERROR("Scenario 1 FAILED: Failed to create new map after load failed.");
                scenarioPassed = false;
            } else {
                LOG_INFO("Scenario 1: New map generated successfully.");
                try {
                    assert(mapScenario1->getTile(0,0,0).terrain == TerrainType::GRASS);
                    LOG_INFO("Scenario 1 PASSED.");
                } catch(...) {
                    LOG_ERROR("Scenario 1 FAILED: Verification of new map failed.");
                    scenarioPassed = false;
                }
            }
        }
        if (!scenarioPassed) overallSuccess = false;
    } // End Scenario 1 scope
    cleanupFiles(); // Clean up for next scenario

    // --- Scenario 2: Only .tlwz Exists ---
    { // Scope for scenario 2 variables
        LOG_INFO("[Startup Test Scenario 2: Only .tlwz Exists]");
        cleanupFiles();
        bool scenarioPassed = true;
        // Create a save with only .tlwz
        {
            auto tempMap = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(0));
            tempMap->setTileTerrain(5, 5, -1, TerrainType::WATER); // Add unique data
            if (!MapPersistenceManager::saveMap(*tempMap, saveName, saveDir, true)) { // deleteTlwf = true
                 LOG_ERROR("Scenario 2 FAILED: Could not create initial .tlwz save.");
                 scenarioPassed = false;
            } else {
                 assert(!std::filesystem::exists(tlwfPath));
                 assert(std::filesystem::exists(tlwzPath));
            }
        }
        // Attempt to load if save creation succeeded
        if (scenarioPassed) {
            std::unique_ptr<Map> mapScenario2 = MapPersistenceManager::loadMapFromSave(saveName, saveDir);
            if (mapScenario2 == nullptr) {
                LOG_ERROR("Scenario 2 FAILED: Failed to load map from existing .tlwz.");
                scenarioPassed = false;
            } else {
                LOG_INFO("Scenario 2: Map loaded successfully from .tlwz.");
                try {
                    assert(mapScenario2->getTile(5, 5, -1).terrain == TerrainType::WATER);
                    LOG_INFO("Scenario 2 PASSED.");
                } catch (...) {
                    LOG_ERROR("Scenario 2 FAILED: Verification of loaded map failed.");
                    scenarioPassed = false;
                }
                assert(std::filesystem::exists(tlwfPath));
                LOG_INFO("Scenario 2: .tlwf file was correctly recreated during load from .tlwz.");
            }
        }
        if (!scenarioPassed) overallSuccess = false;
    } // End Scenario 2 scope
    cleanupFiles();

    // --- Scenario 3: Only .tlwf Exists ---
    { // Scope for scenario 3 variables
        LOG_INFO("[Startup Test Scenario 3: Only .tlwf Exists]");
        cleanupFiles();
        bool scenarioPassed = true;
         // Create a save with only .tlwf by calling MapSerializer directly
        {
            auto tempMap = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(0));
            tempMap->setTileTerrain(6, 6, -1, TerrainType::FLOOR); // Unique data

            // Directly call MapSerializer::saveMap to create only the .tlwf file
            if (!MapSerializer::saveMap(*tempMap, tlwfPath)) { // Pass the full path
                 LOG_ERROR("Scenario 3 FAILED: Could not create initial .tlwf save using MapSerializer.");
                 scenarioPassed = false;
            } else {
                 // Verify that only .tlwf exists
                 assert(std::filesystem::exists(tlwfPath));
                 assert(!std::filesystem::exists(tlwzPath)); // This assertion should now pass
                 LOG_INFO("Scenario 3: Setup complete, only .tlwf exists.");
            }
        }
         // Attempt to load if save creation succeeded
        if (scenarioPassed) {
            // MapPersistenceManager::loadMapFromSave should find and load the .tlwf directly
            std::unique_ptr<Map> mapScenario3 = MapPersistenceManager::loadMapFromSave(saveName, saveDir);
            if (mapScenario3 == nullptr) {
                LOG_ERROR("Scenario 3 FAILED: Failed to load map from existing .tlwf.");
                scenarioPassed = false;
            } else {
                LOG_INFO("Scenario 3: Map loaded successfully from .tlwf.");
                try {
                    assert(mapScenario3->getTile(6, 6, -1).terrain == TerrainType::FLOOR);
                    LOG_INFO("Scenario 3 PASSED.");
                } catch (...) {
                    LOG_ERROR("Scenario 3 FAILED: Verification of loaded map failed.");
                    scenarioPassed = false;
                }
            }
        }
        if (!scenarioPassed) overallSuccess = false;
    } // End Scenario 3 scope
    cleanupFiles();

    // --- Scenario 4: Corrupted .tlwz (fallback to generate new) ---
    { // Scope for scenario 4 variables
        LOG_INFO("[Startup Test Scenario 4: Corrupted .tlwz]");
        cleanupFiles();
        bool scenarioPassed = true;
        // Create a valid .tlwz
        {
            auto tempMap = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(0));
            if (!MapPersistenceManager::saveMap(*tempMap, saveName, saveDir, true)) {
                LOG_ERROR("Scenario 4 FAILED: Could not create initial .tlwz save.");
                scenarioPassed = false;
            }
        }
        // Corrupt the .tlwz file if created
        if (scenarioPassed) {
            try {
                std::fstream file(tlwzPath, std::ios::binary | std::ios::in | std::ios::out);
                if (file.is_open()) {
                    // Go past header (use sizeof(FileHeader) from included FileFormat.h)
                    file.seekp(sizeof(FileHeader) + 10, std::ios::beg);
                    char garbage[] = { 'G', 'A', 'R', 'B', 'A', 'G', 'E' };
                    file.write(garbage, sizeof(garbage));
                    LOG_INFO("Scenario 4: Corrupted .tlwz file.");
                } else {
                     LOG_ERROR("Scenario 4 FAILED: Could not open .tlwz to corrupt it.");
                     scenarioPassed = false;
                }
            } catch(const std::exception& e) {
                 LOG_ERROR("Scenario 4 FAILED: Exception while corrupting .tlwz: " + std::string(e.what()));
                 scenarioPassed = false;
            }
        }

        // Attempt to load (should fail) if corruption step was reached
        if (scenarioPassed) {
            std::unique_ptr<Map> mapScenario4 = MapPersistenceManager::loadMapFromSave(saveName, saveDir);
             if (mapScenario4 != nullptr) {
                LOG_ERROR("Scenario 4 FAILED: Loaded a map from corrupted .tlwz.");
                scenarioPassed = false;
            } else {
                LOG_INFO("Scenario 4: loadMapFromSave correctly returned nullptr for corrupted .tlwz.");
                // Now, generate a new map
                LOG_INFO("Scenario 4: Generating new map...");
                mapScenario4 = std::make_unique<Map>(std::make_unique<FlatTerrainGenerator>(1));
                if (mapScenario4 == nullptr) {
                    LOG_ERROR("Scenario 4 FAILED: Failed to create new map after load failed.");
                    scenarioPassed = false;
                } else {
                    LOG_INFO("Scenario 4 PASSED.");
                }
            }
        }
        if (!scenarioPassed) overallSuccess = false;
    } // End Scenario 4 scope

    cleanupFiles(); // Final cleanup after all scenarios

    // Fix LOG_INFO usage
    std::stringstream ss;
    ss << "--- Startup Load Test " << (overallSuccess ? "Passed" : "Failed") << " ---";
    LOG_INFO(ss.str());
    return overallSuccess;
}

int main() {
    if (!TilelandWorld::Logger::getInstance().initialize("persistence_test.log")) {
        return 1;
    }

#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    LOG_INFO("Starting Persistence Tests...");
    bool success1 = runSaveLoadCycleTest(); // Run the original cycle test
    bool success2 = runStartupLoadTest();   // Run the new startup simulation test
    LOG_INFO("Persistence Tests finished.");

    TilelandWorld::Logger::getInstance().shutdown();

    return (success1 && success2) ? 0 : 1;
}
