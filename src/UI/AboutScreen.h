#pragma once
#ifndef TILELANDWORLD_UI_ABOUTSCREEN_H
#define TILELANDWORLD_UI_ABOUTSCREEN_H

#include "AnsiTui.h"
#include "../Utils/EnvConfig.h"
#include "../Controllers/InputController.h"
#include <memory>
#include <vector>
#include <string>

namespace TilelandWorld {
namespace UI {

class AboutScreen {
public:
    AboutScreen();
    void show();

private:
    TuiSurface surface;
    TuiPainter painter;
    std::unique_ptr<InputController> input;

    int scrollOffset{0};
    int listStartY{0};
    int listHeight{0};

    struct Entry { std::string label; std::string value; };

    void render(const std::vector<Entry>& entries, int maxLabelWidth);
    std::vector<Entry> buildEntries();
    int computeMaxLabel(const std::vector<Entry>& entries) const;
    void clampScroll(int totalRows);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_ABOUTSCREEN_H
