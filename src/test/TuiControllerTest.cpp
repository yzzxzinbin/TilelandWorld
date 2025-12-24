#include "../Map.h"
#include "../Controllers/TuiChunkController.h"
#include "../MapGenInfrastructure/FastNoiseTerrainGenerator.h"
#include "../Utils/Logger.h"
#include "../UI/MainMenuScreen.h"
#include <iostream>
#include <memory>

using namespace TilelandWorld;

int main() {
    // 1. 初始化日志
    if (!Logger::getInstance().initialize("tui_test.log")) {
        std::cerr << "Failed to initialize logger." << std::endl;
        return 1;
    }
    LOG_INFO("Starting TUI Controller Test...");

    try {
        // 0. 主菜单：在进入游戏循环前给用户一个入口
        TilelandWorld::UI::MainMenuScreen mainMenu;
        bool startGame = mainMenu.show();
        if (!startGame) {
            LOG_INFO("User exited from main menu.");
            Logger::getInstance().shutdown();
            return 0;
        }
        LOG_INFO("Main menu accepted, continuing to map setup.");

        // 2. 创建地图
        // 使用 FastNoise 生成器以获得有趣的地形
        // 参数: seed, frequency, noiseType, fractalType, octaves, lacunarity, gain
        auto generator = std::make_unique<FastNoiseTerrainGenerator>(
            1337,      // Seed
            0.025f,      // Frequency
            "OpenSimplex2",   // Noise Type
            "FBm",      // Fractal Type
            5,          // Octaves
            2.0f,       // Lacunarity
            0.5f        // Gain
        );
        
        auto map = std::make_unique<Map>(std::move(generator));
        LOG_INFO("Map created with FastNoiseTerrainGenerator.");

        // 3. 创建 TUI 控制器
        TuiChunkController controller(*map);
        LOG_INFO("TuiChunkController created.");

        // 4. 初始化控制器 (设置控制台等)
        controller.initialize();
        LOG_INFO("Controller initialized. Entering run loop.");

        // 5. 运行主循环
        // 控制权交给控制器，直到用户按 'Q' 退出
        controller.run();

    } catch (const std::exception& e) {
        LOG_ERROR("Unhandled exception: " + std::string(e.what()));
        // 尝试恢复光标，以防在 TUI 模式下崩溃
        std::cout << "\x1b[?25h" << std::flush;
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        Logger::getInstance().shutdown();
        return 1;
    }

    LOG_INFO("Test finished successfully.");
    Logger::getInstance().shutdown();
    return 0;
}
