#include "Tile.h"
#include "Constants.h" // <--- 包含 Constants.h 以获取 MAX_LIGHT_LEVEL
#include <cmath>
#include <algorithm>
#include <string> // For std::string literal ""

namespace TilelandWorld {

    // --- Helper function for color scaling ---
    namespace {
        RGBColor scaleColorByLight(const RGBColor& baseColor, uint8_t lightLevel) {
            if (lightLevel >= MAX_LIGHT_LEVEL) {
                return baseColor;
            }
            if (lightLevel == 0) {
                 const float minBrightnessFactor = 0.1f;
                 return {
                     static_cast<uint8_t>(baseColor.r * minBrightnessFactor),
                     static_cast<uint8_t>(baseColor.g * minBrightnessFactor),
                     static_cast<uint8_t>(baseColor.b * minBrightnessFactor)
                 };
            }
            const float minBrightnessFactor = 0.1f;
            float scale = minBrightnessFactor + (1.0f - minBrightnessFactor) * (static_cast<float>(lightLevel) / MAX_LIGHT_LEVEL);
            RGBColor scaledColor;
            scaledColor.r = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, baseColor.r * scale)));
            scaledColor.g = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, baseColor.g * scale)));
            scaledColor.b = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, baseColor.b * scale)));
            return scaledColor;
        }
    } 
    // end anonymous namespace

    // --- Tile Member Function Implementations ---

    const std::string& Tile::getDisplayChar() const {
        return getTerrainProperties(terrain).displayChar;
    }

    RGBColor Tile::getForegroundColor() const {
        const RGBColor& baseColor = getTerrainProperties(terrain).foregroundColor;
        return scaleColorByLight(baseColor, lightLevel);
    }

    RGBColor Tile::getBackgroundColor() const {
        const RGBColor& baseColor = getTerrainProperties(terrain).backgroundColor;
        return scaleColorByLight(baseColor, lightLevel);
    }

    // 其他未来可能添加的 Tile 成员函数实现...
    /*
    void Tile::update(float deltaTime) {
        // ...
    }
    */

} // namespace TilelandWorld
