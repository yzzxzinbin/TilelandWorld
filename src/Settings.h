#pragma once
#ifndef TILELANDWORLD_SETTINGS_H
#define TILELANDWORLD_SETTINGS_H

#include <string>
#include "Utils/Logger.h"

namespace TilelandWorld {

struct Settings {
    // Timing / performance
    double targetFpsLimit{360.0};
    double targetTps{60.0};
    double statsOverlayAlpha{0.10};

    // Logging
    LogLevel minLogLevel{LogLevel::LOG_INFO};

    // Viewport
    int viewWidth{64};
    int viewHeight{48};

    // UI overlays
    double mouseCrossAlpha{0.10};
    bool enableStatsOverlay{true};
    bool enableMouseCross{true};

    // Rendering optimizations
    bool enableDiffRendering{false};

    // Rendering backend
    bool useFmtRenderer{false};

    // Saves
    std::string saveDirectory{"saves"};

    // Assets
    std::string assetDirectory{"res/Assets"};
    
    // View sizing
    bool autoViewSize{false};
};

class SettingsManager {
public:
    static Settings load(const std::string& path);
    static bool save(const Settings& settings, const std::string& path);
    static Settings defaults();
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_SETTINGS_H
