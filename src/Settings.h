#pragma once
#ifndef TILELANDWORLD_SETTINGS_H
#define TILELANDWORLD_SETTINGS_H

#include <string>

namespace TilelandWorld {

struct Settings {
    // Timing / performance
    double targetTps{60.0};
    double statsOverlayAlpha{0.10};

    // Viewport
    int viewWidth{64};
    int viewHeight{48};

    // UI overlays
    double mouseCrossAlpha{0.10};
    bool enableStatsOverlay{true};
    bool enableMouseCross{true};

    // Saves
    std::string saveDirectory{"saves"};
};

class SettingsManager {
public:
    static Settings load(const std::string& path);
    static bool save(const Settings& settings, const std::string& path);
    static Settings defaults();
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_SETTINGS_H
