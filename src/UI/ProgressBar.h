#pragma once
#ifndef TILELANDWORLD_UI_PROGRESSBAR_H
#define TILELANDWORLD_UI_PROGRESSBAR_H

#include "AnsiTui.h"
#include <string>
#include <vector>

namespace TilelandWorld {
namespace UI {

struct ProgressBarStyle {
    int width{20};
    RGBColor fillFg{96, 140, 255};
    RGBColor fillBg{30, 35, 45};
    RGBColor emptyFg{60, 65, 75};
    bool showPercentage{true};
    std::string prefix{""};
};

class ProgressBar {
public:
    static void render(TuiSurface& surface, int x, int y, double progress, const ProgressBarStyle& style);

private:
    static const std::vector<std::string> kBlocks;
};

}
}

#endif
