#include "../Map.h"
#include "../Controllers/TuiCoreController.h"
#include "../MapGenInfrastructure/TerrainGeneratorFactory.h"
#include "../Utils/Logger.h"
#include "../Utils/EnvConfig.h"
#include "../UI/MainMenuScreen.h"
#include "../UI/SettingsScreen.h"
#include "../UI/SaveManagerScreen.h"
#include "../UI/AssetManagerScreen.h"
#include "../UI/UnicodeTableScreen.h"
#include "../UI/AboutScreen.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include "../Settings.h"
#include <iostream>
#include <memory>
#include <filesystem>
#include <sstream>

using namespace TilelandWorld;

int main() {
    // 0.1 初始化日志系统
    if (!Logger::getInstance().initialize("tui_test.log")) {
        std::cerr << "Failed to initialize logger." << std::endl;
        return 1;
    }
    LOG_INFO("Starting TUI Controller Test...");

    // 0.2 初始化环境配置
    auto& envCfg = EnvConfig::getInstance();
    envCfg.initialize();
    const auto& envStatic = envCfg.getStaticInfo();
    std::ostringstream envLine;
    envLine << "Env init: " << envStatic.envName
            << ", scaling=" << envStatic.scaling
            << ", font(win)=" << envStatic.fontWidthWin << "x" << envStatic.fontHeightWin ;
    LOG_INFO(envLine.str());

    try {
        // 1. 设置加载与主菜单
        std::string cfgPath = "settings.cfg";
        Settings settings = SettingsManager::load(cfgPath);
        Logger::getInstance().setLogLevel(settings.minLogLevel); // 应用日志等级设置

        while (true) {
            TilelandWorld::UI::MainMenuScreen mainMenu;
            auto action = mainMenu.show();
            if (action == TilelandWorld::UI::MainMenuScreen::Action::Start) {
                LOG_INFO("Main menu: start game.");
                
                // 打开存档管理器（它现在内部处理游戏启动）
                TilelandWorld::UI::SaveManagerScreen saveScreen(settings);
                saveScreen.show();
                continue;
            }
            if (action == TilelandWorld::UI::MainMenuScreen::Action::Quit) {
                LOG_INFO("User exited from main menu.");
                Logger::getInstance().shutdown();
                return 0;
            }
            if (action == TilelandWorld::UI::MainMenuScreen::Action::AssetManager) {
                TilelandWorld::UI::AssetManagerScreen assetScreen(settings.assetDirectory);
                assetScreen.show();
                continue;
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
            if (action == TilelandWorld::UI::MainMenuScreen::Action::UnicodeTable) {
                TilelandWorld::UI::UnicodeTableScreen unicodeScreen;
                unicodeScreen.show();
                continue;
            }
            if (action == TilelandWorld::UI::MainMenuScreen::Action::About) {
                TilelandWorld::UI::AboutScreen about;
                about.show();
                continue;
            }
        }
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
