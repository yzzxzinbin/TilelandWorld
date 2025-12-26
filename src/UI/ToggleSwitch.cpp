#include "ToggleSwitch.h"
#include "TuiUtils.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

namespace {
    using Clock = std::chrono::steady_clock;
}

void ToggleSwitch::render(TuiSurface& surface, int x, int y, bool on, const ToggleSwitchState& state, const ToggleSwitchStyle& style) {
    auto now = Clock::now();
    double moveProgress = 1.0;
    double colorProgress = 1.0;
    if (state.lastToggle.time_since_epoch().count() != 0) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastToggle).count();
        moveProgress = std::clamp(ms / style.moveDurationMs, 0.0, 1.0);
        colorProgress = std::clamp((ms - style.colorDelayMs) / style.colorDurationMs, 0.0, 1.0);
    }

    int trackLen = style.trackLen;
    int indicatorWidth = style.indicatorWidth;
    int leftBound = x + 1;
    int rightBound = x + trackLen - indicatorWidth - 1;
    int span = std::max(0, rightBound - leftBound);
    double pos = on ? moveProgress : (1.0 - moveProgress);
    int indicatorX = leftBound + static_cast<int>(pos * span + 0.5);

    RGBColor trackBase = style.trackBase;
    RGBColor startColor = state.previousOn ? style.indicatorOn : style.indicatorOff;
    RGBColor targetColor = on ? style.indicatorOn : style.indicatorOff;
    RGBColor indicatorColor = TuiUtils::blendColor(startColor, targetColor, colorProgress);

    if (state.hover) {
        trackBase = TuiUtils::lightenColor(trackBase, state.hot ? 0.05 : 0.025);
        indicatorColor = TuiUtils::lightenColor(indicatorColor, state.hot ? 0.05 : 0.025);
    }

    surface.fillRect(x, y, trackLen, 1, trackBase, trackBase, " ");

    surface.drawText(x + 1, y, "OFF", style.labelDim, trackBase);
    surface.drawText(x + trackLen - 4, y, "ON", style.labelDim, trackBase);

    surface.fillRect(indicatorX, y, indicatorWidth, 1, indicatorColor, indicatorColor, " ");

    if (indicatorX <= x + 1 && indicatorX + indicatorWidth > x + 1) {
        surface.drawText(x + 1, y, "OFF", style.labelBright, indicatorColor);
    }
    if (indicatorX <= x + trackLen - 4 && indicatorX + indicatorWidth > x + trackLen - 4) {
        surface.drawText(x + trackLen - 4, y, "ON", style.labelBright, indicatorColor);
    }
}

} // namespace UI
} // namespace TilelandWorld
