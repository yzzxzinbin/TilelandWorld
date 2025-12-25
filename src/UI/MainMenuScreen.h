#pragma once
#ifndef TILELANDWORLD_UI_MAINMENUSCREEN_H
#define TILELANDWORLD_UI_MAINMENUSCREEN_H

#include "AnsiTui.h"

namespace TilelandWorld {
namespace UI {

class MainMenuScreen {
public:
    MainMenuScreen();

    enum class Action { Start, Settings, Quit };

    // 显示主菜单，返回用户选择
    Action show();

    size_t getSelectedIndex() const { return selectedIndex; }

private:
    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;
    MenuView menu;
    size_t selectedIndex{0};

    void renderFrame();
    int pollKey();
    void ensureAnsiEnabled();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_MAINMENUSCREEN_H
