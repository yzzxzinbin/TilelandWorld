#include "../Map.h"
#include "../Tile.h"
#include "../Constants.h"
#include "../MapGenInfrastructure/FlatTerrainGenerator.h" // 虽然 Map 默认创建，但包含可能有助于理解
#include <iostream>
#include <string>
#include <vector>
#include <iomanip> // For std::setw
#include <stdexcept> // For std::exception

// Platform-specific includes and setup for virtual terminal processing
#ifdef _WIN32
#include <windows.h>
#endif

// 使用 TilelandWorld 命名空间
using namespace TilelandWorld;

// --- Copied from MapSerializerTest.cpp ---
// Helper to generate ANSI 24-bit color escape codes
std::string formatTileForTerminal(const TilelandWorld::Tile& tile) {
    // Check if the tile is explored. If not, display a default "unexplored" representation.
    // NOTE: FlatTerrainGenerator currently doesn't set isExplored, so we might see '?'
    // We should probably set isExplored = true in the generator or after generation for testing.
    // Let's assume for now the generator *should* set isExplored for visibility in tests.
    // Or, we can modify this test to manually explore tiles after generation.
    // For simplicity, let's modify the generator to mark as explored for now.

    if (!tile.isExplored) {
        // Example: Dark gray background, slightly lighter gray foreground for '?'
        return "\x1b[48;2;50;50;50m\x1b[38;2;100;100;100m??\x1b[0m"; // Use two '?' for aspect ratio
    }

    TilelandWorld::RGBColor fg = tile.getForegroundColor();
    TilelandWorld::RGBColor bg = tile.getBackgroundColor();
    std::string displayChar = tile.getDisplayChar();

    // ANSI escape codes for 24-bit color
    std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
    std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
    std::string resetCode = "\x1b[0m"; // Reset all attributes

    // Print two characters for aspect ratio
    return bgCode + fgCode + displayChar + displayChar + resetCode;
}

// Function to print a specific Z-layer of the map to the terminal with coordinates and chunk separators
// 修改：接收非 const Map 引用以允许触发生成
void printMapLayerToTerminal(Map& map, int zLayer, int startX, int startY, int width, int height) {
    std::cout << "\n--- Map Layer Z=" << zLayer
              << " (Area: X=" << startX << " to " << startX + width - 1
              << ", Y=" << startY << " to " << startY + height - 1 << ") ---" << std::endl;

    // Print Column Headers (X coordinates)
    std::cout << "    "; // Indent for row headers
    for (int x = startX; x < startX + width; ++x) {
        if (x != startX && x % CHUNK_WIDTH == 0) {
            std::cout << " "; // Chunk separator
        }
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

        std::cout << std::setw(3) << y << "|"; // Print Y coord and separator

        for (int x = startX; x < startX + width; ++x) {
             if (x != startX && x % CHUNK_WIDTH == 0) {
                 std::cout << "|"; // Vertical chunk separator
             }
            try {
                // 现在调用非 const getTile，会触发 getOrLoadChunk -> generateChunk
                Tile& tile = map.getTile(x, y, zLayer); // 使用非 const getTile
                std::cout << formatTileForTerminal(tile);
            } catch (const std::exception& e) {
                // Handle potential errors during generation or access
                 std::cerr << "EE"; // Print 'EE' for error accessing tile
            }
        }
        std::cout << "\n"; // Newline after each row
    }
    std::cout << "---------------------------------------" << std::endl; // Footer
}
// --- End Copied Section ---


int main() {
    std::cout << "--- Running Plain Map Generation Test ---" << std::endl;

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

    try {
        // 1. Create a Map object. It will use the default FlatTerrainGenerator.
        //    The FlatTerrainGenerator is set to groundLevel=0 by default in Map.cpp.
        std::cout << "Creating Map object (using default FlatTerrainGenerator)..." << std::endl;
        Map map; // map 现在是非 const
        std::cout << "Map object created." << std::endl;

        // 2. Define the area to display
        const int displayWidth = 32;
        const int displayHeight = 32;
        const int displayStartX = -16; // Start slightly off-center to test negative coords
        const int displayStartY = -16;
        const int displayZLayer = -1; // Display the layer just below ground level (should be grass)

        // 3. Print the map layer.
        //    Calling map.getTile inside printMapLayerToTerminal will trigger
        //    getOrLoadChunk, which in turn calls the terrain generator for needed chunks.
        std::cout << "Printing map layer..." << std::endl;
        // 调用修改后的函数，传入非 const map
        printMapLayerToTerminal(map, displayZLayer, displayStartX, displayStartY, displayWidth, displayHeight);

        // Print another layer (above ground)
        printMapLayerToTerminal(map, 0, displayStartX, displayStartY, displayWidth, displayHeight); // Z=0 should be VOIDBLOCK

        std::cout << "\n--- Plain Map Generation Test Finished Successfully ---" << std::endl;
        return 0; // Success

    } catch (const std::exception& e) {
        std::cerr << "\nError during Plain Map Generation Test: " << e.what() << std::endl;
        return 1; // Failure
    }
}
