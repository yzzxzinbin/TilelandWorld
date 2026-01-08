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
    // 0. 初始化日志
    if (!Logger::getInstance().initialize("tui_test.log")) {
        std::cerr << "Failed to initialize logger." << std::endl;
        return 1;
    }
    LOG_INFO("Starting TUI Controller Test...");

    auto& envCfg = EnvConfig::getInstance();
    envCfg.initialize();
    const auto& envStatic = envCfg.getStaticInfo();
    std::ostringstream envLine;
    envLine << "Env init: " << envStatic.envName
            << ", scaling=" << envStatic.scaling
            << ", font(win)=" << envStatic.fontWidthWin << "x" << envStatic.fontHeightWin
            << ", vt cells=" << envStatic.vtCols << "x" << envStatic.vtRows
            << ", vt px=" << envStatic.vtPixW << "x" << envStatic.vtPixH;
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
                
                bool inSaveManager = true;
                while (inSaveManager) {
                    // 2. 打开存档管理器
                    TilelandWorld::UI::SaveManagerScreen saveScreen(settings.saveDirectory);
                    auto sel = saveScreen.show();
                    if (sel.action == TilelandWorld::UI::SaveManagerScreen::Result::Action::Back) {
                        LOG_INFO("Save manager: user backed out.");
                        inSaveManager = false;
                        break; // 返回主菜单
                    }

                    std::unique_ptr<Map> map;
                    if (sel.action == TilelandWorld::UI::SaveManagerScreen::Result::Action::Load) {
                        std::string chosenDir = sel.saveDirectory.empty() ? settings.saveDirectory : sel.saveDirectory;
                        map = MapSerializer::loadMapFromSave(sel.saveName, chosenDir);
                        if (!map) {
                            LOG_ERROR("Failed to load save '" + sel.saveName + "'. Returning to save manager.");
                            continue;
                        }
                        LOG_INFO("Loaded save '" + sel.saveName + "'.");
                    } else if (sel.action == TilelandWorld::UI::SaveManagerScreen::Result::Action::CreateNew) {
                        std::string chosenDir = sel.saveDirectory.empty() ? settings.saveDirectory : sel.saveDirectory;
                        std::filesystem::create_directories(chosenDir);
                        settings.saveDirectory = chosenDir;
                        map = std::make_unique<Map>(createTerrainGeneratorFromMetadata(sel.metadata));
                        map->setWorldMetadata(sel.metadata);
                        MapSerializer::saveCompressedMap(*map, sel.saveName, chosenDir, false);
                        LOG_INFO("Created new save '" + sel.saveName + "' in '" + chosenDir + "'.");
                    }

                    // 清屏：从主界面进入游戏主循环时做一次 ANSI 清屏
                    std::cout << "\x1b[2J\x1b[H" << std::flush;

                    // 3. 创建 TUI 控制器
                    TuiCoreController controller(*map, settings);
                    LOG_INFO("TuiCoreController created.");

                    // 4. 初始化控制器 (设置控制台等)
                    controller.initialize();
                    LOG_INFO("Controller initialized. Entering run loop.");

                    // 5. 运行主循环
                    controller.run();
                    LOG_INFO("Game loop finished. Returning to save manager.");
                }
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
