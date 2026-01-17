#pragma once
#ifndef TILELANDWORLD_UI_SAVEMANAGERSCREEN_H
#define TILELANDWORLD_UI_SAVEMANAGERSCREEN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include "../SaveMetadata.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include <string>
#include <vector>
#include <memory>

namespace TilelandWorld {
    class Map;
    struct Settings;

namespace UI {

class SaveManagerScreen {
public:
    explicit SaveManagerScreen(Settings& settings);

    // 显示存档管理器，并在其中启动游戏主循环
    void show();

private:
    struct Result {
        enum class Action { Load, CreateNew, Back };
        Action action{Action::Back};
        std::string saveName;
        std::string saveDirectory;
        std::unique_ptr<Map> map;
    };

    Settings& settings;
    TuiSurface surface;
    TuiPainter painter;
    TuiTheme theme;
    MenuView menu;
    size_t selectedIndex{0};
    std::vector<std::string> saves;
    struct SaveInfo { bool loaded{false}; bool ok{false}; MapSerializer::SaveSummary summary{}; };
    std::vector<SaveInfo> infoCache;

    // 布局缓存供鼠标命中
    int lastPanelX{0};
    int lastPanelY{0};
    int lastPanelWidth{0};
    int lastListStart{0};
    int lastListCount{0};

    void refreshList();
    void renderFrame();
    void handleKey(int key, bool& running, Result& result, InputController& input);
    void handleMouse(const InputEvent& ev, bool& running, Result& result, InputController& input);
    void ensureAnsiEnabled();
    void ensureInfo(size_t idx);
    void renderInfoBar();
    std::string formatBytes(size_t bytes) const;
    bool editSave(size_t idx, InputController& input);

    void runGame(std::unique_ptr<Map> map);
    bool deleteSelected();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_SAVEMANAGERSCREEN_H
