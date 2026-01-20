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

} // namespace UI
} // namespace TilelandWorld
