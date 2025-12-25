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

    // Noise generator
    int noiseSeed{1337};
    double noiseFrequency{0.025};
    std::string noiseType{"OpenSimplex2"};
    std::string noiseFractal{"FBm"};
    int noiseOctaves{5};
    double noiseLacunarity{2.0};
    double noiseGain{0.5};
};

class SettingsManager {
public:
    static Settings load(const std::string& path);
    static bool save(const Settings& settings, const std::string& path);
    static Settings defaults();
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_SETTINGS_H
