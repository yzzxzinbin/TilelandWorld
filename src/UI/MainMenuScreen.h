#pragma once
#ifndef TILELANDWORLD_UI_MAINMENUSCREEN_H
#define TILELANDWORLD_UI_MAINMENUSCREEN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include "BuildInfo.h"

namespace TilelandWorld {
namespace UI {

class MainMenuScreen {
public:
    MainMenuScreen();

    enum class Action { Start, Settings, AssetManager, UnicodeTable, Quit };

    // 显示主菜单，返回用户选择
    Action show();

    size_t getSelectedIndex() const { return selectedIndex; }

private:
    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;
    MenuView menu;
    size_t selectedIndex{0};
    inline static constexpr const char* kVersion = TILELAND_BUILD_VERSION;
    inline static constexpr const char* kBuildTimestamp = TILELAND_BUILD_TIMESTAMP;

    void renderFrame();
    void handleKey(int key, bool& running, Action& result);
    void handleMouse(const InputEvent& ev, bool& running, Action& result);
    void ensureAnsiEnabled();

    // 布局缓存用于鼠标命中
    int lastPanelX{0};
    int lastPanelY{0};
    int lastPanelWidth{0};
    int lastListStart{0};
    int lastListCount{0};
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_MAINMENUSCREEN_H
