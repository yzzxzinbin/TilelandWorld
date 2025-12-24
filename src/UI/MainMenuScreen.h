#pragma once
#ifndef TILELANDWORLD_UI_MAINMENUSCREEN_H
#define TILELANDWORLD_UI_MAINMENUSCREEN_H

#include "AnsiTui.h"

namespace TilelandWorld {
namespace UI {

class MainMenuScreen {
public:
    MainMenuScreen();

    // 显示主菜单，返回 true 表示进入游戏，false 表示退出
    bool show();

    size_t getSelectedIndex() const { return selectedIndex; }

private:
    TuiSurface surface;
    TuiPainter painter;
    MenuView menu;
    MenuTheme theme;
    size_t selectedIndex{0};

    void renderFrame();
    int pollKey();
    void ensureAnsiEnabled();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_MAINMENUSCREEN_H
