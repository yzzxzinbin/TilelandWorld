#include "ProgressBar.h"
#include "TuiUtils.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace TilelandWorld {
namespace UI {

const std::vector<std::string> ProgressBar::kBlocks = {
    " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"
};

void ProgressBar::render(TuiSurface& surface, int x, int y, double progress, const ProgressBarStyle& style) {
    double p = std::clamp(progress, 0.0, 1.0);
    int totalSteps = style.width * 8;
    int filledSteps = static_cast<int>(std::round(p * totalSteps));

    int fullChars = filledSteps / 8;
    int partialStep = filledSteps % 8;

    int cursorX = x;

    // Draw prefix if any
    if (!style.prefix.empty()) {
        surface.drawText(cursorX, y, style.prefix, style.fillFg, style.fillBg);
        cursorX += TuiUtils::calculateUtf8VisualWidth(style.prefix) + 1;
    }

    // Draw the bar
    for (int i = 0; i < style.width; ++i) {
        std::string glyph;
        RGBColor fg = style.fillFg;
        RGBColor bg = style.fillBg;

        if (i < fullChars) {
            glyph = kBlocks[8]; // █
        } else if (i == fullChars && partialStep > 0) {
            glyph = kBlocks[partialStep];
            // For partial blocks, the background is the empty color
            bg = style.fillBg;
        } else {
            glyph = " ";
            bg = style.fillBg;
        }

        if (TuiCell* cell = surface.editCell(cursorX + i, y)) {
            cell->glyph = glyph;
            cell->fg = fg;
            cell->bg = bg;
            cell->hasBg = true;
        }
    }

    // Percentage
    if (style.showPercentage) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (p * 100.0) << "%";
        std::string pstr = ss.str();
        // Pad to ensure consistent width (max "100.0%" is 6 chars, but let's use 6 for the number + %)
        while (pstr.size() < 6) pstr = " " + pstr;
        surface.drawText(cursorX + style.width + 1, y, pstr, style.fillFg, style.fillBg); 
    }
}

}
}
