#pragma once
#ifndef TILELANDWORLD_UI_TOGGLESWITCH_H
#define TILELANDWORLD_UI_TOGGLESWITCH_H

#include "AnsiTui.h"
#include <chrono>
#include <string>

namespace TilelandWorld {
namespace UI {

struct ToggleSwitchStyle {
    int trackLen{16};
    int indicatorWidth{4};
    RGBColor trackBase{32, 36, 48};
    RGBColor indicatorOn{64, 150, 220};
    RGBColor indicatorOff{210, 70, 70};
    RGBColor labelDim{80, 85, 100};
    RGBColor labelBright{255, 255, 255};
    std::string offLabel{"OFF"};
    std::string onLabel{"ON"};
    double moveDurationMs{200.0};
    double colorDelayMs{80.0};
    double colorDurationMs{220.0};
};

struct ToggleSwitchState {
    bool previousOn{false};
    std::chrono::steady_clock::time_point lastToggle{};
    bool hover{false};
    bool hot{false};
};

class ToggleSwitch {
public:
    static void render(TuiSurface& surface, int x, int y, bool on, const ToggleSwitchState& state, const ToggleSwitchStyle& style = ToggleSwitchStyle{});
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_TOGGLESWITCH_H
