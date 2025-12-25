#pragma once
#ifndef TILELANDWORLD_UI_SETTINGSSCREEN_H
#define TILELANDWORLD_UI_SETTINGSSCREEN_H

#include "AnsiTui.h"
#include "../Settings.h"
#include <functional>
#include <vector>

namespace TilelandWorld {
namespace UI {

class SettingsScreen {
public:
    explicit SettingsScreen(Settings& settings);
    // 返回 true 表示保存并应用，false 表示取消
    bool show();

private:
    Settings& target;
    Settings working; // 可编辑副本

    TuiSurface surface;
    TuiPainter painter;

    struct Item {
        std::string label;
        std::function<void(int)> adjust; // dir: -1/1
        std::function<std::string()> value;
    };

    std::vector<Item> items;
    size_t selected{0};

    void buildItems();
    void renderFrame();
    int pollKey();
    void apply();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_SETTINGSSCREEN_H
