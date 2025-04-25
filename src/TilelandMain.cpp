#include "Tile.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm> // For std::min

// Platform-specific includes and setup for virtual terminal processing
#ifdef _WIN32
#include <windows.h>
#endif

// Helper to generate ANSI 24-bit color escape codes
// 注意：此函数现在直接使用 Tile 返回的原始信息。
// 实际渲染时，需要在此函数外部或渲染循环中检查 tile.isExplored。
std::string formatTileForTerminal(const TilelandWorld::Tile& tile) {
    TilelandWorld::RGBColor fg = tile.getForegroundColor();
    TilelandWorld::RGBColor bg = tile.getBackgroundColor();
    std::string displayChar = tile.getDisplayChar();

    // ANSI escape codes for 24-bit color
    std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
    std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
    std::string resetCode = "\x1b[0m"; // Reset all attributes

    return bgCode + fgCode + displayChar + resetCode;
}

// 注意：此函数现在打印 Tile 的原始信息，忽略 isExplored 对显示的影响。
void printTileInfo(const TilelandWorld::Tile& tile, const std::string& name) {
    TilelandWorld::RGBColor fg = tile.getForegroundColor();
    TilelandWorld::RGBColor bg = tile.getBackgroundColor();

    std::cout << "--- Tile Info: " << name << " ---\n";
    std::cout << "  Terrain Type: " << static_cast<int>(tile.terrain) << "\n"; // Print enum value for now
    std::cout << "  Display Char: '" << tile.getDisplayChar() << "'\n";
    std::cout << "  Foreground RGB: (" << (int)fg.r << "," << (int)fg.g << "," << (int)fg.b << ")\n";
    std::cout << "  Background RGB: (" << (int)bg.r << "," << (int)bg.g << "," << (int)bg.b << ")\n";
    std::cout << "  Light Level: " << (int)tile.lightLevel << "/" << (int)TilelandWorld::MAX_LIGHT_LEVEL << "\n";
    std::cout << "  Is Explored: " << (tile.isExplored ? "Yes" : "No") << "\n";
    std::cout << "  Can Enter Same Level: " << (tile.canEnterSameLevel ? "Yes" : "No") << "\n";
    std::cout << "  Can Stand On Top: " << (tile.canStandOnTop ? "Yes" : "No") << "\n";
    std::cout << "  Movement Cost: " << tile.movementCost << "\n";
    std::cout << "  Terminal Output: " << formatTileForTerminal(tile) << "\n";
    std::cout << "-------------------------\n\n";
}

int main() {
    using namespace TilelandWorld;

#ifdef _WIN32
    // Enable virtual terminal processing on Windows for ANSI escape codes
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        std::cerr << "Error getting standard output handle." << std::endl;
        return GetLastError();
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        std::cerr << "Error getting console mode." << std::endl;
        return GetLastError();
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) {
        std::cerr << "Error setting console mode for virtual terminal processing." << std::endl;
        // Continue even if this fails, but colors might not work
    }

    // Set console code page to UTF-8 (65001) for correct character display
    if (!SetConsoleOutputCP(65001)) {
        std::cerr << "Warning: Failed to set console output code page to UTF-8." << std::endl;
    }
    if (!SetConsoleCP(65001)) {
         std::cerr << "Warning: Failed to set console input code page to UTF-8." << std::endl;
    }
    // UTF-8 should now be active for console I/O.
#endif // _WIN32

    // 1. Create some tiles
    Tile grassTile(TerrainType::GRASS);
    Tile waterTile(TerrainType::WATER);
    Tile wallTile(TerrainType::WALL);
    Tile floorTile(TerrainType::FLOOR);
    Tile voidTile(TerrainType::VOIDBLOCK);
    Tile unknownTile; // Defaults to UNKNOWN

    // 2. Modify some properties
    grassTile.isExplored = true;

    waterTile.isExplored = true;
    waterTile.lightLevel = 5; // Dimly lit water

    wallTile.isExplored = false; // Unexplored wall

    floorTile.isExplored = true;
    floorTile.lightLevel = 0; // Explored but completely dark floor

    // 3. Print info
    // 输出现在将显示未探索区域的实际地形信息（字符和根据光照调整的颜色），
    // 因为 Tile 内部不再根据 isExplored 进行屏蔽。
    printTileInfo(grassTile, "Explored Grass (Full Light)");
    printTileInfo(waterTile, "Explored Water (Dim Light)");
    printTileInfo(wallTile, "Unexplored Wall (Raw Data)"); // 输出会显示墙的字符和颜色
    printTileInfo(floorTile, "Explored Floor (Dark)");
    printTileInfo(voidTile, "Void Tile (Default State)");
    printTileInfo(unknownTile, "Unknown Tile (Default State)");

    // Example of printing a small grid
    std::cout << "--- Mini Grid Example ---\n";
    std::vector<Tile> gridRow = {grassTile, waterTile, wallTile, floorTile};
    for (const auto& tile : gridRow) {
        // 渲染循环需要检查 isExplored
        // if (tile.isExplored) {
             std::cout << formatTileForTerminal(tile);
        // } else {
        //     // 打印未探索的样式
        //     std::cout << "\x1b[48;2;30;30;30m\x1b[38;2;80;80;80m \x1b[0m"; // 例如：深灰背景，灰色空格
        // }
    }
    std::cout << "\n-------------------------\n";

    // Example of printing a 2D light level gradient grid
    std::cout << "--- 2D Light Level Gradient Example (Grass, 0-255) ---\n";
    const int gridSize = 64; // Define the size of the grid (16x16)
    const float maxCoordSum = static_cast<float>(2 * (gridSize - 1)); // Max value of x + y

    for (int y = 0; y < gridSize; ++y) {
        for (int x = 0; x < gridSize; ++x) {
            // Calculate light level based on position (diagonal gradient)
            float gradientPos = static_cast<float>(x + y);
            uint8_t currentLightLevel = static_cast<uint8_t>(
                std::min(static_cast<float>(MAX_LIGHT_LEVEL), // Clamp to max
                         (gradientPos / maxCoordSum) * MAX_LIGHT_LEVEL)
            );

            Tile gradientTile(TerrainType::GRASS);
            gradientTile.lightLevel = currentLightLevel;
            gradientTile.isExplored = true; // Ensure it's visible
            std::cout << formatTileForTerminal(gradientTile);
        }
        std::cout << "\n"; // Newline after each row
    }
    std::cout << "-------------------------\n";

    return 0;
}
