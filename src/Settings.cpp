#include "Settings.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace TilelandWorld {

namespace {
    std::string trim(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
        return s.substr(start, end - start);
    }

    template <typename T>
    void parseValue(const std::string& text, T& out);

    template <>
    void parseValue<int>(const std::string& text, int& out) { out = std::stoi(text); }
    template <>
    void parseValue<double>(const std::string& text, double& out) { out = std::stod(text); }
    template <>
    void parseValue<bool>(const std::string& text, bool& out) {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        out = (t == "1" || t == "true" || t == "yes" || t == "on");
    }
    template <>
    void parseValue<std::string>(const std::string& text, std::string& out) { out = text; }

    template <typename T>
    void maybeSet(const std::string& key, const std::string& value, const std::string& targetKey, T& out) {
        if (key == targetKey) {
            try { parseValue<T>(value, out); } catch (...) {}
        }
    }
}

Settings SettingsManager::defaults() { return Settings{}; }

Settings SettingsManager::load(const std::string& path) {
    Settings cfg = defaults();

    std::ifstream in(path);
    if (!in.is_open()) {
        return cfg;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        maybeSet<double>(key, value, "targetTps", cfg.targetTps);
        maybeSet<double>(key, value, "statsOverlayAlpha", cfg.statsOverlayAlpha);
        maybeSet<double>(key, value, "mouseCrossAlpha", cfg.mouseCrossAlpha);
        maybeSet<bool>(key, value, "enableStatsOverlay", cfg.enableStatsOverlay);
        maybeSet<bool>(key, value, "enableMouseCross", cfg.enableMouseCross);

        maybeSet<int>(key, value, "viewWidth", cfg.viewWidth);
        maybeSet<int>(key, value, "viewHeight", cfg.viewHeight);

        maybeSet<int>(key, value, "noiseSeed", cfg.noiseSeed);
        maybeSet<double>(key, value, "noiseFrequency", cfg.noiseFrequency);
        maybeSet<std::string>(key, value, "noiseType", cfg.noiseType);
        maybeSet<std::string>(key, value, "noiseFractal", cfg.noiseFractal);
        maybeSet<int>(key, value, "noiseOctaves", cfg.noiseOctaves);
        maybeSet<double>(key, value, "noiseLacunarity", cfg.noiseLacunarity);
        maybeSet<double>(key, value, "noiseGain", cfg.noiseGain);
    }

    return cfg;
}

bool SettingsManager::save(const Settings& s, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# TilelandWorld settings\n";
    out << "targetTps=" << s.targetTps << "\n";
    out << "statsOverlayAlpha=" << s.statsOverlayAlpha << "\n";
    out << "mouseCrossAlpha=" << s.mouseCrossAlpha << "\n";
    out << "enableStatsOverlay=" << (s.enableStatsOverlay ? "1" : "0") << "\n";
    out << "enableMouseCross=" << (s.enableMouseCross ? "1" : "0") << "\n";

    out << "viewWidth=" << s.viewWidth << "\n";
    out << "viewHeight=" << s.viewHeight << "\n";

    out << "noiseSeed=" << s.noiseSeed << "\n";
    out << "noiseFrequency=" << s.noiseFrequency << "\n";
    out << "noiseType=" << s.noiseType << "\n";
    out << "noiseFractal=" << s.noiseFractal << "\n";
    out << "noiseOctaves=" << s.noiseOctaves << "\n";
    out << "noiseLacunarity=" << s.noiseLacunarity << "\n";
    out << "noiseGain=" << s.noiseGain << "\n";

    return true;
}

} // namespace TilelandWorld
