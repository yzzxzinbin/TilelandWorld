#include "../Map.h"
#include "../Controllers/TuiCoreController.h"
#include "../MapGenInfrastructure/FastNoiseTerrainGenerator.h"
#include "../Utils/Logger.h"
#include "../UI/MainMenuScreen.h"
#include "../UI/SettingsScreen.h"
#include "../Settings.h"
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
        // 0. 设置加载与主菜单
        std::string cfgPath = "settings.cfg";
        Settings settings = SettingsManager::load(cfgPath);

        while (true) {
            TilelandWorld::UI::MainMenuScreen mainMenu;
            auto action = mainMenu.show();
            if (action == TilelandWorld::UI::MainMenuScreen::Action::Start) {
                LOG_INFO("Main menu: start game.");
                break;
            }
            if (action == TilelandWorld::UI::MainMenuScreen::Action::Quit) {
                LOG_INFO("User exited from main menu.");
                Logger::getInstance().shutdown();
                return 0;
            }
            if (action == TilelandWorld::UI::MainMenuScreen::Action::Settings) {
                TilelandWorld::UI::SettingsScreen settingsScreen(settings);
                bool applied = settingsScreen.show();
                if (applied) {
                    SettingsManager::save(settings, cfgPath);
                    LOG_INFO("Settings saved.");
                }
                continue;
            }
        }
        LOG_INFO("Main menu accepted, continuing to map setup.");

        // 清屏：从主界面进入游戏主循环时做一次 ANSI 清屏
        std::cout << "\x1b[2J\x1b[H" << std::flush;

        // 2. 创建地图
        // 使用 FastNoise 生成器以获得有趣的地形
        // 参数: seed, frequency, noiseType, fractalType, octaves, lacunarity, gain
        auto generator = std::make_unique<FastNoiseTerrainGenerator>(
            settings.noiseSeed,
            static_cast<float>(settings.noiseFrequency),
            settings.noiseType,
            settings.noiseFractal,
            settings.noiseOctaves,
            static_cast<float>(settings.noiseLacunarity),
            static_cast<float>(settings.noiseGain)
        );
        
        auto map = std::make_unique<Map>(std::move(generator));
        LOG_INFO("Map created with FastNoiseTerrainGenerator.");

        // 3. 创建 TUI 控制器
        TuiCoreController controller(*map, settings);
        LOG_INFO("TuiCoreController created.");

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
