#include "../Map.h"
#include "../MapGenInfrastructure/FastNoiseTerrainGenerator.h" // 引入 FastNoise 生成器
#include "../Tile.h"
#include "../TerrainTypes.h"
#include "../Coordinates.h"
#include "../Constants.h"
#include "../Utils/Logger.h" // 引入日志记录器

#include <iostream>
#include <vector>
#include <string>
#include <iomanip> // 用于 std::setw
#include <memory>  // 用于 std::make_unique

// --- Platform-specific includes and setup for virtual terminal processing ---
#ifdef _WIN32
#include <windows.h>
#endif
// --- End Platform-specific ---

// --- ANSI 颜色代码 ---
const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
// ... (可以从其他测试文件复制更多颜色代码)

// 函数：将 RGB 转换为终端背景色 ANSI 代码
std::string formatBackgroundColor(const TilelandWorld::RGBColor& color) {
    return "\033[48;2;" + std::to_string(color.r) + ";" + std::to_string(color.g) + ";" + std::to_string(color.b) + "m";
}

// 函数：将 RGB 转换为终端前景色 ANSI 代码
std::string formatForegroundColor(const TilelandWorld::RGBColor& color) {
    return "\033[38;2;" + std::to_string(color.r) + ";" + std::to_string(color.g) + ";" + std::to_string(color.b) + "m";
}

// 函数：格式化单个 Tile 以在终端显示 (考虑光照和可见性)
std::string formatTileForTerminal(const TilelandWorld::Tile& tile) {
    const auto& props = TilelandWorld::getTerrainProperties(tile.terrain);

    // 如果 Tile 不可见 (例如 VOIDBLOCK)，则打印空格背景
    if (!props.isVisible) {
        // 使用一个默认的“天空”或“虚空”背景色
        return "\033[48;2;10;10;20m  " + RESET; // 深蓝色背景
    }

    // 根据光照调整颜色 (简单线性插值)
    float lightRatio = static_cast<float>(tile.lightLevel) / TilelandWorld::MAX_LIGHT_LEVEL;
    TilelandWorld::RGBColor fg = props.foregroundColor;
    TilelandWorld::RGBColor bg = props.backgroundColor;

    fg.r = static_cast<uint8_t>(fg.r * lightRatio);
    fg.g = static_cast<uint8_t>(fg.g * lightRatio);
    fg.b = static_cast<uint8_t>(fg.b * lightRatio);
    bg.r = static_cast<uint8_t>(bg.r * lightRatio);
    bg.g = static_cast<uint8_t>(bg.g * lightRatio);
    bg.b = static_cast<uint8_t>(bg.b * lightRatio);

    // 使用地形指定的字符，重复两次以获得更好的宽高比
    std::string displayStr = props.displayChar + props.displayChar;

    return formatBackgroundColor(bg) + formatForegroundColor(fg) + BOLD + displayStr + RESET;
}


// 函数：将指定 Z 层的地图区域打印到终端
void printMapLayerToTerminal(TilelandWorld::Map& map, int zLevel, int minX, int maxX, int minY, int maxY) {
    std::cout << "Map Layer Z = " << zLevel << std::endl;

    // 打印列坐标
    std::cout << "    "; // 留出 Y 坐标的空间
    for (int x = minX; x <= maxX; ++x) {
        std::cout << std::setw(2) << x % 100; // 只显示最后两位
    }
    std::cout << std::endl;

    // 打印上边框
    std::cout << "   +";
    for (int x = minX; x <= maxX; ++x) {
        std::cout << "--";
    }
    std::cout << "+" << std::endl;

    // 打印地图内容和行坐标
    for (int y = minY; y <= maxY; ++y) {
        std::cout << std::setw(3) << y << "|"; // 打印行坐标和边框
        for (int x = minX; x <= maxX; ++x) {
            try {
                // 获取 Tile，这会触发区块加载/生成（如果需要）
                // *** 注意：这里使用 const getTile，如果 Map::getTile 不是 const，需要修改 ***
                // 如果 Map::getTile 是非 const，则不需要修改 map 参数类型
                const TilelandWorld::Tile& tile = map.getTile(x, y, zLevel);
                if (tile.isExplored) {
                    std::cout << formatTileForTerminal(tile);
                } else {
                    // 理论上预生成后都是 explored，但也处理一下
                    std::cout << "??"; // 未探索区域
                }
            } catch (const std::exception& e) {
                std::cerr << "Error getting tile (" << x << "," << y << "," << zLevel << "): " << e.what() << std::endl;
                std::cout << "EE"; // 表示错误
            }
        }
        std::cout << "|" << std::endl; // 打印右边框
    }

    // 打印下边框
    std::cout << "   +";
    for (int x = minX; x <= maxX; ++x) {
        std::cout << "--";
    }
    std::cout << "+" << std::endl;
}

int main() {
    // --- Windows Console Setup ---
#ifdef _WIN32
    // Enable virtual terminal processing on Windows for ANSI escape codes
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    // Set console code page to UTF-8 (important for non-ASCII characters)
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    // --- End Windows Console Setup ---


    // 初始化日志记录器
    if (!TilelandWorld::Logger::getInstance().initialize("persistence_test.log")) {
        // 如果日志初始化失败，仍然尝试运行，但打印错误到 stderr
        std::cerr << "Failed to initialize logger!" << std::endl;
        // return 1; // 或者根据需要决定是否终止
    }
    LOG_INFO("--- FastNoise Map Generation Test Started ---");

    try { // 将主要逻辑放入 try-catch 块
        // 1. 创建 FastNoise 地形生成器实例
        auto noiseGenerator = std::make_unique<TilelandWorld::FastNoiseTerrainGenerator>(
            1337,           // Seed
            0.025f,         // Frequency
            "OpenSimplex2", // Base noise type
            "FBm",          // Fractal type
            5,              // Octaves
            2.0f,           // Lacunarity
            0.5f            // Gain
        );
        // 检查生成器是否成功创建 (虽然构造函数应该抛出异常，但多一层保险)
        if (!noiseGenerator) {
             throw std::runtime_error("Failed to create FastNoiseTerrainGenerator instance.");
        }
        LOG_INFO("FastNoiseTerrainGenerator created.");

        // 2. 创建地图实例，传入生成器
        TilelandWorld::Map gameMap(std::move(noiseGenerator));
        LOG_INFO("Map object created with FastNoise generator.");

        // 3. 定义要显示和生成的区域范围
        int minX = -10, maxX = 25;
        int minY = -8, maxY = 22;
        int zLevelToDisplay = 0; // 我们将显示 Z=0 层

        // 4. 预先访问区域内的 Tile 以触发生成
        LOG_INFO("Pre-generating chunks in the target area...");
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                try {
                    (void)gameMap.getTile(x, y, zLevelToDisplay);
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception during pre-generation at (" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(zLevelToDisplay) + "): " + e.what());
                }
            }
        }
         LOG_INFO("Chunk pre-generation attempt complete.");


        // 5. 打印指定 Z 层的地图到终端
        LOG_INFO("Printing map layer Z=" + std::to_string(zLevelToDisplay));
        // *** 将 gameMap 传递给 printMapLayerToTerminal ***
        printMapLayerToTerminal(gameMap, zLevelToDisplay, minX, maxX, minY, maxY);

        LOG_INFO("--- FastNoise Map Generation Test Finished ---");

    } catch (const std::exception& e) {
        // 捕获并记录在 main 函数中发生的任何异常
        LOG_ERROR("An exception occurred in main: " + std::string(e.what()));
        std::cerr << "An exception occurred in main: " << e.what() << std::endl;
        TilelandWorld::Logger::getInstance().shutdown();
        return 1; // 返回错误码
    } catch (...) {
        // 捕获未知异常
        LOG_ERROR("An unknown exception occurred in main.");
        std::cerr << "An unknown exception occurred in main." << std::endl;
        TilelandWorld::Logger::getInstance().shutdown();
        return 1; // 返回错误码
    }

    TilelandWorld::Logger::getInstance().shutdown();
    return 0; // 成功
}