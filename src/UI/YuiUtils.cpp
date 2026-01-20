#include "YuiUtils.h"
#include "YuiEditorScreen.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

namespace YuiUtils {
    RGBColor darken(const RGBColor& c, double factor) {
        double f = std::clamp(factor, 0.0, 1.0);
        return {
            static_cast<uint8_t>(std::max(0.0, c.r * f)),
            static_cast<uint8_t>(std::max(0.0, c.g * f)),
            static_cast<uint8_t>(std::max(0.0, c.b * f))
        };
    }
}

void YuiEditorScreen::clampScroll() {
    int viewW = canvasW - 2;
    int viewH = canvasH - 2;
    int maxX = std::max(0, working.getWidth() - viewW);
    int maxY = std::max(0, working.getHeight() - viewH);
    scrollX = std::clamp(scrollX, 0, maxX);
    scrollY = std::clamp(scrollY, 0, maxY);
}

bool YuiEditorScreen::isInsideCanvas(int x, int y) const {
    return x >= canvasX + 1 && x < canvasX + canvasW - 1 && y >= canvasY + 1 && y < canvasY + canvasH - 1;
}

RGBColor YuiEditorScreen::getPerspectiveColor(const ImageCell& cell, const std::string& maskGlyph) {
    auto getCoverage = [](const std::string& g) -> std::vector<int> {
        if (g == "█") return {0,1,2,3};
        if (g == "▀") return {0,1};
        if (g == "▄") return {2,3};
        if (g == "▌") return {0,2};
        if (g == "▐") return {1,3};
        if (g == "▘") return {0};
        if (g == "▝") return {1};
        if (g == "▖") return {2};
        if (g == "▗") return {3};
        if (g == "▚") return {0,3};
        if (g == "▞") return {1,2};
        if (g == "▛") return {0,1,2};
        if (g == "▜") return {0,1,3};
        if (g == "▙") return {0,2,3};
        if (g == "▟") return {1,2,3};
        return {}; 
    };

    std::vector<int> fgIndices = getCoverage(cell.character);
    bool isFG[4] = {false, false, false, false};
    for(int idx : fgIndices) isFG[idx] = true;
    
    // Handle precision characters by approximation if needed.
    // For now, if getCoverage returned empty but it's not a space, we can assume it's a specialty block.
    if (fgIndices.empty() && cell.character != " ") {
        // Simple heuristic: if it contains vertical line chars, assume left/right bias.
        // But let's keep it simple for now as per the "2x2 series" request.
    }

    std::vector<int> maskIndices = getCoverage(maskGlyph);
    bool isMask[4] = {false, false, false, false};
    for(int idx : maskIndices) isMask[idx] = true;

    long r = 0, g = 0, b = 0;
    int count = 0;
    for(int i = 0; i < 4; ++i) {
        if(!isMask[i]) {
            RGBColor c = isFG[i] ? cell.fg : cell.bg;
            r += c.r; g += c.g; b += c.b;
            count++;
        }
    }
    
    if(count == 0) return cell.bg;
    return { (uint8_t)(r/count), (uint8_t)(g/count), (uint8_t)(b/count) };
}

} // namespace UI
} // namespace TilelandWorld
