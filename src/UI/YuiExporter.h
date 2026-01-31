#ifndef TILELANDWORLD_UI_YUIEXPORTER_H
#define TILELANDWORLD_UI_YUIEXPORTER_H

#include "../ImgAssetsInfrastructure/YuiLayer.h"
#include <string>
#include <vector>
#include <cstdint>

namespace TilelandWorld {
namespace UI {

class YuiExporter {
public:
    enum class Mode {
        BlockToPixel, // 1x2 pixels per cell
        BlockToBlock  // Pixel dimensions from EnvConfig
    };

    enum class Format {
        BMP,
        PNG,
        JPG
    };

    static bool exportToImage(const YuiLayeredImage& image, const std::string& filename, Mode mode, Format format = Format::BMP);

private:
    static bool saveBmp(const std::string& filename, int w, int h, const std::vector<uint32_t>& pixels);
    static bool saveWithEncoder(const std::string& filename, int w, int h, const std::vector<uint32_t>& pixels, Format format);
    static std::vector<uint8_t> loadFontData(const std::string& fontName);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_YUIEXPORTER_H
