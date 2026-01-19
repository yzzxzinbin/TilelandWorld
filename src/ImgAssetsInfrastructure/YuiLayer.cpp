#include "YuiLayer.h"
#include <fstream>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <limits>
#include <functional>
#include <unordered_set>

namespace TilelandWorld {

namespace {
    constexpr int kMaskSize = 8; // 8x8 subcells
    struct GlyphMask {
        std::array<uint8_t, kMaskSize * kMaskSize> data{};
        int onCount{0};
    };

    GlyphMask makeMask(const std::function<bool(int,int)>& test) {
        GlyphMask mask;
        int count = 0;
        for (int y = 0; y < kMaskSize; ++y) {
            for (int x = 0; x < kMaskSize; ++x) {
                bool on = test(x, y);
                mask.data[y * kMaskSize + x] = on ? 1 : 0;
                if (on) ++count;
            }
        }
        mask.onCount = count;
        return mask;
    }

    const std::unordered_map<std::string, GlyphMask>& glyphMasks() {
        static std::unordered_map<std::string, GlyphMask> masks = {
            {" ", makeMask([](int, int) { return false; })},
            {"█", makeMask([](int, int) { return true; })},
            {"▀", makeMask([](int, int y) { return y < 4; })},
            {"▄", makeMask([](int, int y) { return y >= 4; })},
            {"▌", makeMask([](int x, int) { return x < 4; })},
            {"▐", makeMask([](int x, int) { return x >= 4; })},

            // Quadrants (2x2 -> 4x4 blocks)
            {"▘", makeMask([](int x, int y) { return x < 4 && y < 4; })},
            {"▝", makeMask([](int x, int y) { return x >= 4 && y < 4; })},
            {"▖", makeMask([](int x, int y) { return x < 4 && y >= 4; })},
            {"▗", makeMask([](int x, int y) { return x >= 4 && y >= 4; })},
            {"▚", makeMask([](int x, int y) { return (x < 4 && y < 4) || (x >= 4 && y >= 4); })},
            {"▞", makeMask([](int x, int y) { return (x >= 4 && y < 4) || (x < 4 && y >= 4); })},
            {"▙", makeMask([](int x, int y) { return (x < 4 && y >= 4) || (x < 4 && y < 4) || (x >= 4 && y >= 4); })},
            {"▛", makeMask([](int x, int y) { return (x < 4 && y < 4) || (x >= 4 && y < 4) || (x < 4 && y >= 4); })},
            {"▜", makeMask([](int x, int y) { return (x < 4 && y < 4) || (x >= 4 && y < 4) || (x >= 4 && y >= 4); })},
            {"▟", makeMask([](int x, int y) { return (x >= 4 && y < 4) || (x < 4 && y >= 4) || (x >= 4 && y >= 4); })},

            // Left 1/8 blocks
            {"▏", makeMask([](int x, int) { return x < 1; })},
            {"▎", makeMask([](int x, int) { return x < 2; })},
            {"▍", makeMask([](int x, int) { return x < 3; })},
            {"▋", makeMask([](int x, int) { return x < 5; })},
            {"▊", makeMask([](int x, int) { return x < 6; })},
            {"▉", makeMask([](int x, int) { return x < 7; })},

            // Lower 1/8 blocks
            {"▁", makeMask([](int, int y) { return y >= 7; })},
            {"▂", makeMask([](int, int y) { return y >= 6; })},
            {"▃", makeMask([](int, int y) { return y >= 5; })},
            {"▅", makeMask([](int, int y) { return y >= 3; })},
            {"▆", makeMask([](int, int y) { return y >= 2; })},
            {"▇", makeMask([](int, int y) { return y >= 1; })}
        };
        return masks;
    }

    const GlyphMask& getMaskForGlyph(const std::string& glyph) {
        const auto& masks = glyphMasks();
        auto it = masks.find(glyph);
        if (it != masks.end()) return it->second;
        static GlyphMask full = makeMask([](int, int) { return true; });
        return full;
    }

    int requiredGridForGlyph(const std::string& glyph) {
        if (glyph.empty() || glyph == " " || glyph == "█") return 1;
        static const std::unordered_set<std::string> grid2 = {
            "▀","▄","▌","▐",
            "▘","▝","▖","▗","▚","▞","▙","▛","▜","▟"
        };
        static const std::unordered_set<std::string> grid8 = {
            "▏","▎","▍","▋","▊","▉",
            "▁","▂","▃","▅","▆","▇"
        };
        if (grid2.count(glyph)) return 2;
        if (grid8.count(glyph)) return 8;
        return 8;
    }

    bool glyphOnGrid(const std::string& glyph, int x, int y, int grid) {
        if (grid == 1) {
            return !(glyph.empty() || glyph == " ");
        }
        if (grid == 2) {
            if (glyph == "█") return true;
            if (glyph == " ") return false;
            if (glyph == "▀") return y == 0;
            if (glyph == "▄") return y == 1;
            if (glyph == "▌") return x == 0;
            if (glyph == "▐") return x == 1;
            if (glyph == "▘") return x == 0 && y == 0;
            if (glyph == "▝") return x == 1 && y == 0;
            if (glyph == "▖") return x == 0 && y == 1;
            if (glyph == "▗") return x == 1 && y == 1;
            if (glyph == "▚") return (x == 0 && y == 0) || (x == 1 && y == 1);
            if (glyph == "▞") return (x == 1 && y == 0) || (x == 0 && y == 1);
            if (glyph == "▙") return !(x == 1 && y == 0);
            if (glyph == "▛") return !(x == 1 && y == 1);
            if (glyph == "▜") return !(x == 0 && y == 1);
            if (glyph == "▟") return !(x == 0 && y == 0);
            return true; // unknowns treated as full
        }
        const auto& mask = getMaskForGlyph(glyph.empty() ? " " : glyph);
        return mask.data[y * kMaskSize + x] != 0;
    }

    const std::vector<std::string>& candidateGlyphs(int grid) {
        static std::vector<std::string> grid1 = {" ", "█"};
        static std::vector<std::string> grid2 = {
            " ", "█",
            "▀", "▄", "▌", "▐",
            "▘", "▝", "▖", "▗", "▚", "▞", "▙", "▛", "▜", "▟"
        };
        static std::vector<std::string> grid8 = {
            " ", "█",
            "▀", "▄", "▌", "▐",
            "▘", "▝", "▖", "▗", "▚", "▞", "▙", "▛", "▜", "▟",
            "▏", "▎", "▍", "▋", "▊", "▉",
            "▁", "▂", "▃", "▅", "▆", "▇"
        };
        if (grid <= 1) return grid1;
        if (grid == 2) return grid2;
        return grid8;
    }

    RGBColor avgColor(const std::array<RGBColor, kMaskSize * kMaskSize>& colors,
                      const std::array<uint8_t, kMaskSize * kMaskSize>& weights,
                      bool onMask, const std::string& glyph, int grid) {
        uint64_t sumR = 0, sumG = 0, sumB = 0, sumW = 0;
        int count = grid * grid;
        for (int i = 0; i < count; ++i) {
            int gx = i % grid;
            int gy = i / grid;
            if (glyphOnGrid(glyph, gx, gy, grid) != onMask) continue;
            uint8_t w = weights[i];
            sumR += static_cast<uint64_t>(colors[i].r) * w;
            sumG += static_cast<uint64_t>(colors[i].g) * w;
            sumB += static_cast<uint64_t>(colors[i].b) * w;
            sumW += w;
        }
        if (sumW == 0) {
            return {0, 0, 0};
        }
        return {
            static_cast<uint8_t>(std::clamp<int>(static_cast<int>(sumR / sumW), 0, 255)),
            static_cast<uint8_t>(std::clamp<int>(static_cast<int>(sumG / sumW), 0, 255)),
            static_cast<uint8_t>(std::clamp<int>(static_cast<int>(sumB / sumW), 0, 255))
        };
    }
}

ImageCell YuiLayer::emptyCell = {" ", {0,0,0}, {0,0,0}};

const ImageCell& YuiLayer::getCell(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return emptyCell;
    return cells[static_cast<size_t>(y) * width + x];
}

void YuiLayer::setCell(int x, int y, const ImageCell& cell) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    cells[static_cast<size_t>(y) * width + x] = cell;
}

void YuiLayer::resize(int newWidth, int newHeight) {
    newWidth = std::max(0, newWidth);
    newHeight = std::max(0, newHeight);
    std::vector<ImageCell> next(static_cast<size_t>(newWidth) * newHeight);
    for (int y = 0; y < std::min(height, newHeight); ++y) {
        for (int x = 0; x < std::min(width, newWidth); ++x) {
            next[static_cast<size_t>(y) * newWidth + x] = getCell(x, y);
        }
    }
    width = newWidth;
    height = newHeight;
    cells.swap(next);
}

void YuiLayer::clear(const ImageCell& fillCell) {
    std::fill(cells.begin(), cells.end(), fillCell);
}

YuiLayeredImage::YuiLayeredImage(int w, int h) : width(w), height(h) {
    layers.emplace_back(w, h, "Layer 1");
    activeLayer = 0;
    cacheDirty = true;
}

YuiLayer& YuiLayeredImage::getLayer(size_t index) {
    if (index >= layers.size()) {
        static YuiLayer fallback;
        return fallback;
    }
    return layers[index];
}

const YuiLayer& YuiLayeredImage::getLayer(size_t index) const {
    if (index >= layers.size()) {
        static YuiLayer fallback;
        return fallback;
    }
    return layers[index];
}

void YuiLayeredImage::setActiveLayerIndex(int index) {
    if (layers.empty()) {
        activeLayer = 0;
        return;
    }
    activeLayer = std::clamp(index, 0, static_cast<int>(layers.size()) - 1);
}

YuiLayer& YuiLayeredImage::activeLayerRef() {
    if (layers.empty()) {
        layers.emplace_back(width, height, "Layer 1");
        activeLayer = 0;
    }
    return layers[activeLayer];
}

const YuiLayer& YuiLayeredImage::activeLayerRef() const {
    if (layers.empty()) {
        static YuiLayer fallback;
        return fallback;
    }
    return layers[activeLayer];
}

void YuiLayeredImage::addLayer(const YuiLayer& layer) {
    layers.push_back(layer);
    setActiveLayerIndex(static_cast<int>(layers.size()) - 1);
    markDirty();
}

void YuiLayeredImage::insertLayer(int index, const YuiLayer& layer) {
    index = std::clamp(index, 0, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + index, layer);
    setActiveLayerIndex(index);
    markDirty();
}

void YuiLayeredImage::removeLayer(int index) {
    if (layers.empty()) return;
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers.erase(layers.begin() + index);
    if (layers.empty()) {
        activeLayer = 0;
        return;
    }
    if (activeLayer >= static_cast<int>(layers.size())) {
        activeLayer = static_cast<int>(layers.size()) - 1;
    }
    markDirty();
}

void YuiLayeredImage::moveLayer(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(layers.size()) || to >= static_cast<int>(layers.size())) return;
    if (from == to) return;
    YuiLayer layer = layers[from];
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, layer);
    activeLayer = to;
    markDirty();
}

void YuiLayeredImage::setLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers[static_cast<size_t>(index)].setVisible(visible);
    markDirty();
}

void YuiLayeredImage::setLayerOpacity(int index, double opacity) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return;
    layers[static_cast<size_t>(index)].setOpacity(opacity);
    markDirty();
}

ImageCell YuiLayeredImage::getActiveCell(int x, int y) const {
    return activeLayerRef().getCell(x, y);
}

void YuiLayeredImage::setActiveCell(int x, int y, const ImageCell& cell) {
    activeLayerRef().setCell(x, y, cell);
    markDirty();
}

void YuiLayeredImage::markDirty() {
    cacheDirty = true;
}

void YuiLayeredImage::ensureCompositeCache() const {
    if (compositeCache.getWidth() != width || compositeCache.getHeight() != height) {
        compositeCache = ImageAsset(width, height);
        cacheDirty = true;
    }
    if (!cacheDirty) return;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            compositeCache.setCell(x, y, compositeCellInternal(x, y));
        }
    }
    cacheDirty = false;
}

void YuiLayeredImage::blendOver(RGBColor& dstColor, uint8_t& dstAlpha, const RGBColor& srcColor, uint8_t srcAlpha) {
    if (srcAlpha == 0) return;
    double sa = srcAlpha / 255.0;
    double da = dstAlpha / 255.0;
    double outA = sa + da * (1.0 - sa);
    if (outA <= 0.0) {
        dstAlpha = 0;
        dstColor = {0, 0, 0};
        return;
    }
    auto blendChannel = [&](uint8_t dst, uint8_t src) -> uint8_t {
        double out = (src * sa + dst * da * (1.0 - sa)) / outA;
        return static_cast<uint8_t>(std::clamp(static_cast<int>(out + 0.5), 0, 255));
    };
    dstColor = { blendChannel(dstColor.r, srcColor.r), blendChannel(dstColor.g, srcColor.g), blendChannel(dstColor.b, srcColor.b) };
    dstAlpha = static_cast<uint8_t>(std::clamp(static_cast<int>(outA * 255.0 + 0.5), 0, 255));
}

RGBColor YuiLayeredImage::blendToBackground(const RGBColor& bg, const RGBColor& fg, uint8_t fgAlpha) {
    double a = fgAlpha / 255.0;
    auto blendChannel = [&](uint8_t b, uint8_t f) -> uint8_t {
        double out = f * a + b * (1.0 - a);
        return static_cast<uint8_t>(std::clamp(static_cast<int>(out + 0.5), 0, 255));
    };
    return { blendChannel(bg.r, fg.r), blendChannel(bg.g, fg.g), blendChannel(bg.b, fg.b) };
}

ImageCell YuiLayeredImage::compositeCellInternal(int x, int y) const {
    int grid = 1;
    for (const auto& layer : layers) {
        if (!layer.isVisible()) continue;
        const auto& cell = layer.getCell(x, y);
        int need = requiredGridForGlyph(cell.character);
        if (need > grid) grid = need;
        if (grid == 8) break;
    }

    std::array<RGBColor, kMaskSize * kMaskSize> subColors;
    std::array<uint8_t, kMaskSize * kMaskSize> subAlpha{};
    for (int i = 0; i < kMaskSize * kMaskSize; ++i) {
        subColors[i] = {0, 0, 0};
        subAlpha[i] = 0;
    }

    int count = grid * grid;

    for (const auto& layer : layers) {
        if (!layer.isVisible()) continue;
        const auto& cell = layer.getCell(x, y);
        const std::string& glyph = cell.character.empty() ? " " : cell.character;
        double layerOpacity = layer.getOpacity();
        uint8_t fgA = static_cast<uint8_t>(std::clamp(static_cast<int>(cell.fgA * layerOpacity + 0.5), 0, 255));
        uint8_t bgA = static_cast<uint8_t>(std::clamp(static_cast<int>(cell.bgA * layerOpacity + 0.5), 0, 255));

        for (int i = 0; i < count; ++i) {
            int gx = i % grid;
            int gy = i / grid;
            const bool on = glyphOnGrid(glyph, gx, gy, grid);
            const RGBColor srcColor = on ? cell.fg : cell.bg;
            const uint8_t srcAlpha = on ? fgA : bgA;
            blendOver(subColors[i], subAlpha[i], srcColor, srcAlpha);
        }
    }

    const auto& candidates = candidateGlyphs(grid);
    double bestScore = std::numeric_limits<double>::max();
    std::string bestGlyph = " ";
    RGBColor bestFg{0, 0, 0};
    RGBColor bestBg{0, 0, 0};

    for (const auto& glyph : candidates) {
        RGBColor fg = avgColor(subColors, subAlpha, true, glyph, grid);
        RGBColor bg = avgColor(subColors, subAlpha, false, glyph, grid);

        double score = 0.0;
        for (int i = 0; i < count; ++i) {
            int gx = i % grid;
            int gy = i / grid;
            const RGBColor& target = subColors[i];
            const RGBColor& ref = glyphOnGrid(glyph, gx, gy, grid) ? fg : bg;
            double w = subAlpha[i] / 255.0;
            double dr = static_cast<double>(target.r) - ref.r;
            double dg = static_cast<double>(target.g) - ref.g;
            double db = static_cast<double>(target.b) - ref.b;
            score += (dr * dr + dg * dg + db * db) * w;
        }

        if (score < bestScore) {
            bestScore = score;
            bestGlyph = glyph;
            bestFg = fg;
            bestBg = bg;
        }
    }

    ImageCell out;
    out.character = bestGlyph;
    out.fg = bestFg;
    out.bg = bestBg;
    out.fgA = 255;
    out.bgA = 255;
    return out;
}

ImageCell YuiLayeredImage::compositeCell(int x, int y) const {
    ensureCompositeCache();
    return compositeCache.getCell(x, y);
}

ImageAsset YuiLayeredImage::flatten() const {
    ensureCompositeCache();
    return compositeCache;
}

YuiImageMetadata YuiLayeredImage::calculateMetadata() const {
    YuiImageMetadata stats;
    stats.width = width;
    stats.height = height;
    
    ImageAsset flat = flatten();
    std::unordered_map<std::string, int> glyphUsage;
    std::unordered_set<uint32_t> colorSet;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto& cell = flat.getCell(x, y);
            if (!cell.character.empty()) {
                glyphUsage[cell.character]++;
            } else {
                glyphUsage[" "]++;
            }
            uint32_t fgC = (static_cast<uint32_t>(cell.fg.r) << 16) | (static_cast<uint32_t>(cell.fg.g) << 8) | static_cast<uint32_t>(cell.fg.b);
            uint32_t bgC = (static_cast<uint32_t>(cell.bg.r) << 16) | (static_cast<uint32_t>(cell.bg.g) << 8) | static_cast<uint32_t>(cell.bg.b);
            colorSet.insert(fgC);
            colorSet.insert(bgC);
        }
    }

    stats.uniqueGlyphs = static_cast<int>(glyphUsage.size());
    stats.uniqueColors = static_cast<int>(colorSet.size());

    std::vector<std::pair<std::string, int>> glyphs(glyphUsage.begin(), glyphUsage.end());
    std::sort(glyphs.begin(), glyphs.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    for (size_t i = 0; i < std::min<size_t>(glyphs.size(), 10); ++i) {
        stats.topGlyphs.push_back(glyphs[i]);
    }

    return stats;
}

ImageAsset YuiLayeredImage::loadPreview(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return ImageAsset(0, 0);

    char magic[5] = {0};
    in.read(magic, 5);
    if (std::string(magic, 5) != "TLIMG") return ImageAsset(0, 0);

    uint16_t ver = 0;
    in.read(reinterpret_cast<char*>(&ver), sizeof(ver));

    if (ver == 4) {
        in.seekg(8, std::ios::cur); // skip statsOffset
        uint64_t previewOffset = 0;
        in.read(reinterpret_cast<char*>(&previewOffset), 8);
        if (previewOffset == 0) return ImageAsset(0, 0);

        in.seekg(previewOffset);
        uint16_t pw = 0, ph = 0;
        in.read(reinterpret_cast<char*>(&pw), 2);
        in.read(reinterpret_cast<char*>(&ph), 2);
        if (pw == 0 || ph == 0) return ImageAsset(0, 0);

        ImageAsset preview(pw, ph);
        for (int i = 0; i < pw * ph; ++i) {
            uint8_t len = 0;
            in.read(reinterpret_cast<char*>(&len), 1);
            std::string ch;
            if (len > 0) {
                ch.resize(len);
                in.read(&ch[0], len);
            }
            RGBColor fg{}, bg{};
            uint8_t fgA = 255, bgA = 255;
            in.read(reinterpret_cast<char*>(&fg.r), 1);
            in.read(reinterpret_cast<char*>(&fg.g), 1);
            in.read(reinterpret_cast<char*>(&fg.b), 1);
            in.read(reinterpret_cast<char*>(&fgA), 1);
            in.read(reinterpret_cast<char*>(&bg.r), 1);
            in.read(reinterpret_cast<char*>(&bg.g), 1);
            in.read(reinterpret_cast<char*>(&bg.b), 1);
            in.read(reinterpret_cast<char*>(&bgA), 1);

            ImageCell cell{ch, fg, bg, fgA, bgA};
            preview.setCell(i % pw, i / pw, cell);
        }
        return preview;
    }

    if (ver < 3) return ImageAsset(0, 0);

    uint16_t w = 0, h = 0;
    in.read(reinterpret_cast<char*>(&w), sizeof(w));
    in.read(reinterpret_cast<char*>(&h), sizeof(h));

    uint16_t layerCount = 0;
    in.read(reinterpret_cast<char*>(&layerCount), sizeof(layerCount));

    // Skip layers
    for (uint16_t li = 0; li < layerCount; ++li) {
        uint16_t layerIndex = 0;
        in.read(reinterpret_cast<char*>(&layerIndex), sizeof(layerIndex));
        uint8_t nameLen = 0;
        in.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) in.seekg(nameLen, std::ios::cur);
        in.seekg(2, std::ios::cur); // opacity, visible

        for (int i = 0; i < w * h; ++i) {
            uint8_t len = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len > 0) in.seekg(len, std::ios::cur);
            in.seekg(8, std::ios::cur); // fgRGBA, bgRGBA
        }
    }

    // Read Preview block
    uint16_t previewIdx = 0;
    in.read(reinterpret_cast<char*>(&previewIdx), sizeof(previewIdx));
    uint8_t pNameLen = 0;
    in.read(reinterpret_cast<char*>(&pNameLen), sizeof(pNameLen));
    if (pNameLen > 0) in.seekg(pNameLen, std::ios::cur);
    in.seekg(2, std::ios::cur); // opacity, visible

    uint16_t pw = 0, ph = 0;
    in.read(reinterpret_cast<char*>(&pw), sizeof(pw));
    in.read(reinterpret_cast<char*>(&ph), sizeof(ph));

    if (pw == 0 || ph == 0) return ImageAsset(0, 0);

    ImageAsset preview(pw, ph);
    for (int i = 0; i < pw * ph; ++i) {
        uint8_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string ch;
        if (len > 0) {
            ch.resize(len);
            in.read(&ch[0], len);
        }
        RGBColor fg{}, bg{};
        uint8_t fgA = 255, bgA = 255;
        in.read(reinterpret_cast<char*>(&fg.r), 1);
        in.read(reinterpret_cast<char*>(&fg.g), 1);
        in.read(reinterpret_cast<char*>(&fg.b), 1);
        in.read(reinterpret_cast<char*>(&fgA), 1);
        in.read(reinterpret_cast<char*>(&bg.r), 1);
        in.read(reinterpret_cast<char*>(&bg.g), 1);
        in.read(reinterpret_cast<char*>(&bg.b), 1);
        in.read(reinterpret_cast<char*>(&bgA), 1);

        ImageCell cell;
        cell.character = ch;
        cell.fg = fg; cell.bg = bg;
        cell.fgA = fgA; cell.bgA = bgA;
        preview.setCell(i % pw, i / pw, cell);
    }

    return preview;
}

YuiImageMetadata YuiLayeredImage::loadImageMetadata(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    char magic[5] = {0};
    in.read(magic, 5);
    if (std::string(magic, 5) != "TLIMG") return {};

    uint16_t ver = 0;
    in.read(reinterpret_cast<char*>(&ver), sizeof(ver));

    if (ver == 4) {
        uint64_t statsOffset = 0;
        in.read(reinterpret_cast<char*>(&statsOffset), 8);
        if (statsOffset == 0) return {};

        in.seekg(statsOffset);
        YuiImageMetadata stats;
        in.read(reinterpret_cast<char*>(&stats.width), 4);
        in.read(reinterpret_cast<char*>(&stats.height), 4);
        in.read(reinterpret_cast<char*>(&stats.uniqueGlyphs), 4);
        in.read(reinterpret_cast<char*>(&stats.uniqueColors), 4);
        uint16_t topCount = 0;
        in.read(reinterpret_cast<char*>(&topCount), 2);
        for (int i = 0; i < topCount; ++i) {
            uint8_t gl = 0;
            in.read(reinterpret_cast<char*>(&gl), 1);
            std::string s(gl, ' ');
            if (gl > 0) in.read(&s[0], gl);
            int count = 0;
            in.read(reinterpret_cast<char*>(&count), 4);
            stats.topGlyphs.push_back({s, count});
        }
        return stats;
    }

    // Older versions: Load and calculate
    in.close();
    YuiLayeredImage layered = load(path);
    if (layered.getWidth() == 0) return {};
    return layered.calculateMetadata();
}

YuiLayeredImage YuiLayeredImage::fromImageAsset(const ImageAsset& asset) {
    YuiLayeredImage layered(asset.getWidth(), asset.getHeight());
    YuiLayer& layer = layered.activeLayerRef();
    for (int y = 0; y < asset.getHeight(); ++y) {
        for (int x = 0; x < asset.getWidth(); ++x) {
            layer.setCell(x, y, asset.getCell(x, y));
        }
    }
    return layered;
}

YuiLayeredImage YuiLayeredImage::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return YuiLayeredImage(0, 0);

    char magic[5] = {0};
    in.read(magic, 5);
    if (std::string(magic, 5) != "TLIMG") return YuiLayeredImage(0, 0);

    uint16_t ver = 0;
    in.read(reinterpret_cast<char*>(&ver), sizeof(ver));

    if (ver == 4) {
        uint64_t statsOffset = 0, previewOffset = 0;
        uint16_t layerCount = 0;
        in.read(reinterpret_cast<char*>(&statsOffset), 8);
        in.read(reinterpret_cast<char*>(&previewOffset), 8);
        in.read(reinterpret_cast<char*>(&layerCount), 2);

        std::vector<uint64_t> metaOffsets(layerCount);
        std::vector<uint64_t> dataOffsets(layerCount);
        for (int i = 0; i < layerCount; ++i) in.read(reinterpret_cast<char*>(&metaOffsets[i]), 8);
        for (int i = 0; i < layerCount; ++i) in.read(reinterpret_cast<char*>(&dataOffsets[i]), 8);

        // Read Width/Height from stats block
        in.seekg(statsOffset);
        int w = 0, h = 0;
        in.read(reinterpret_cast<char*>(&w), 4);
        in.read(reinterpret_cast<char*>(&h), 4);

        YuiLayeredImage layered(w, h);
        layered.layers.clear();
        layered.layers.resize(layerCount);

        for (int i = 0; i < layerCount; ++i) {
            in.seekg(metaOffsets[i]);
            uint16_t idx = 0;
            in.read(reinterpret_cast<char*>(&idx), 2);
            uint8_t nameLen = 0;
            in.read(reinterpret_cast<char*>(&nameLen), 1);
            std::string name(nameLen, ' ');
            if (nameLen > 0) in.read(&name[0], nameLen);
            uint8_t opacity = 255, visible = 1;
            in.read(reinterpret_cast<char*>(&opacity), 1);
            in.read(reinterpret_cast<char*>(&visible), 1);

            YuiLayer layer(w, h, name);
            layer.setOpacity(opacity / 255.0);
            layer.setVisible(visible != 0);

            in.seekg(dataOffsets[i]);
            for (int cellIdx = 0; cellIdx < w * h; ++cellIdx) {
                uint8_t len = 0;
                in.read(reinterpret_cast<char*>(&len), 1);
                std::string ch;
                if (len > 0) {
                    ch.resize(len);
                    in.read(&ch[0], len);
                }
                RGBColor fg{}, bg{};
                uint8_t fgA = 255, bgA = 255;
                in.read(reinterpret_cast<char*>(&fg.r), 1);
                in.read(reinterpret_cast<char*>(&fg.g), 1);
                in.read(reinterpret_cast<char*>(&fg.b), 1);
                in.read(reinterpret_cast<char*>(&fgA), 1);
                in.read(reinterpret_cast<char*>(&bg.r), 1);
                in.read(reinterpret_cast<char*>(&bg.g), 1);
                in.read(reinterpret_cast<char*>(&bg.b), 1);
                in.read(reinterpret_cast<char*>(&bgA), 1);

                ImageCell cell{ch, fg, bg, fgA, bgA};
                layer.setCell(cellIdx % w, cellIdx / w, cell);
            }
            layered.layers[idx < layerCount ? idx : i] = std::move(layer);
        }
        layered.setActiveLayerIndex(0);
        return layered;
    }

    uint16_t w = 0, h = 0;
    in.read(reinterpret_cast<char*>(&w), sizeof(w));
    in.read(reinterpret_cast<char*>(&h), sizeof(h));

    if (ver == 1) {
        ImageAsset flat(w, h);
        for (int i = 0; i < w * h; ++i) {
            uint8_t len = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string ch;
            if (len > 0) {
                ch.resize(len);
                in.read(&ch[0], len);
            }
            RGBColor fg{}, bg{};
            in.read(reinterpret_cast<char*>(&fg.r), 1);
            in.read(reinterpret_cast<char*>(&fg.g), 1);
            in.read(reinterpret_cast<char*>(&fg.b), 1);
            in.read(reinterpret_cast<char*>(&bg.r), 1);
            in.read(reinterpret_cast<char*>(&bg.g), 1);
            in.read(reinterpret_cast<char*>(&bg.b), 1);
            ImageCell cell;
            cell.character = ch;
            cell.fg = fg;
            cell.bg = bg;
            cell.fgA = 255;
            cell.bgA = 255;
            flat.setCell(i % w, i / w, cell);
        }
        return fromImageAsset(flat);
    }

    if (ver != 2 && ver != 3) return YuiLayeredImage(0, 0);

    uint16_t layerCount = 0;
    in.read(reinterpret_cast<char*>(&layerCount), sizeof(layerCount));

    YuiLayeredImage layered(w, h);
    layered.layers.clear();
    layered.layers.resize(layerCount);

    for (uint16_t li = 0; li < layerCount; ++li) {
        uint16_t layerIndex = 0;
        in.read(reinterpret_cast<char*>(&layerIndex), sizeof(layerIndex));
        uint8_t nameLen = 0;
        in.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        std::string name = "Layer";
        if (nameLen > 0) {
            name.resize(nameLen);
            in.read(&name[0], nameLen);
        }
        uint8_t opacityByte = 255;
        uint8_t visibleByte = 1;
        in.read(reinterpret_cast<char*>(&opacityByte), sizeof(opacityByte));
        in.read(reinterpret_cast<char*>(&visibleByte), sizeof(visibleByte));

        YuiLayer layer(w, h, name);
        layer.setOpacity(opacityByte / 255.0);
        layer.setVisible(visibleByte != 0);

        for (int i = 0; i < w * h; ++i) {
            uint8_t len = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string ch;
            if (len > 0) {
                ch.resize(len);
                in.read(&ch[0], len);
            }
            RGBColor fg{}, bg{};
            uint8_t fgA = 255, bgA = 255;
            in.read(reinterpret_cast<char*>(&fg.r), 1);
            in.read(reinterpret_cast<char*>(&fg.g), 1);
            in.read(reinterpret_cast<char*>(&fg.b), 1);
            in.read(reinterpret_cast<char*>(&fgA), 1);
            in.read(reinterpret_cast<char*>(&bg.r), 1);
            in.read(reinterpret_cast<char*>(&bg.g), 1);
            in.read(reinterpret_cast<char*>(&bg.b), 1);
            in.read(reinterpret_cast<char*>(&bgA), 1);

            ImageCell cell;
            cell.character = ch;
            cell.fg = fg;
            cell.bg = bg;
            cell.fgA = fgA;
            cell.bgA = bgA;
            layer.setCell(i % w, i / w, cell);
        }

        uint16_t targetIndex = layerIndex < layerCount ? layerIndex : li;
        layered.layers[targetIndex] = layer;
    }

    layered.setActiveLayerIndex(0);
    return layered;
}

bool YuiLayeredImage::save(const std::string& path, int previewX, int previewY, int previewW, int previewH) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const char magic[] = "TLIMG";
    out.write(magic, 5);
    uint16_t ver = 4;
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));

    uint16_t layerCount = static_cast<uint16_t>(layers.size());
    std::streampos indexPos = out.tellp();
    
    // Reserve index space (8 + 8 + 2 + 8 * layerCount + 8 * layerCount)
    uint64_t dummy = 0;
    out.write(reinterpret_cast<const char*>(&dummy), 8); // imageStatsOffset
    out.write(reinterpret_cast<const char*>(&dummy), 8); // previewOffset
    out.write(reinterpret_cast<const char*>(&layerCount), 2);
    for (int i = 0; i < layerCount * 2; ++i) {
        out.write(reinterpret_cast<const char*>(&dummy), 8);
    }

    uint64_t imageStatsOffset = static_cast<uint64_t>(out.tellp());
    YuiImageMetadata stats = calculateMetadata();
    out.write(reinterpret_cast<const char*>(&stats.width), 4);
    out.write(reinterpret_cast<const char*>(&stats.height), 4);
    out.write(reinterpret_cast<const char*>(&stats.uniqueGlyphs), 4);
    out.write(reinterpret_cast<const char*>(&stats.uniqueColors), 4);
    uint16_t topCount = static_cast<uint16_t>(stats.topGlyphs.size());
    out.write(reinterpret_cast<const char*>(&topCount), 2);
    for (const auto& tg : stats.topGlyphs) {
        uint8_t gl = static_cast<uint8_t>(std::min<size_t>(tg.first.size(), 255));
        out.write(reinterpret_cast<const char*>(&gl), 1);
        out.write(tg.first.data(), gl);
        out.write(reinterpret_cast<const char*>(&tg.second), 4);
    }

    uint64_t previewOffset = 0;
    if (previewW > 0 && previewH > 0) {
        previewOffset = static_cast<uint64_t>(out.tellp());
        int actualPX = std::clamp(previewX, 0, width - 1);
        int actualPY = std::clamp(previewY, 0, height - 1);
        int actualPW = std::min(previewW, width - actualPX);
        int actualPH = std::min(previewH, height - actualPY);

        uint16_t pw = static_cast<uint16_t>(actualPW);
        uint16_t ph = static_cast<uint16_t>(actualPH);
        out.write(reinterpret_cast<const char*>(&pw), 2);
        out.write(reinterpret_cast<const char*>(&ph), 2);

        ensureCompositeCache();
        for (int py = 0; py < actualPH; ++py) {
            for (int px = 0; px < actualPW; ++px) {
                const auto& cell = compositeCache.getCell(actualPX + px, actualPY + py);
                uint8_t len = static_cast<uint8_t>(std::min<size_t>(cell.character.size(), 255));
                out.write(reinterpret_cast<const char*>(&len), 1);
                if (len > 0) out.write(cell.character.data(), len);
                out.write(reinterpret_cast<const char*>(&cell.fg.r), 1);
                out.write(reinterpret_cast<const char*>(&cell.fg.g), 1);
                out.write(reinterpret_cast<const char*>(&cell.fg.b), 1);
                out.write(reinterpret_cast<const char*>(&cell.fgA), 1);
                out.write(reinterpret_cast<const char*>(&cell.bg.r), 1);
                out.write(reinterpret_cast<const char*>(&cell.bg.g), 1);
                out.write(reinterpret_cast<const char*>(&cell.bg.b), 1);
                out.write(reinterpret_cast<const char*>(&cell.bgA), 1);
            }
        }
    }

    std::vector<uint64_t> layerMetaOffsets(layerCount);
    for (uint16_t i = 0; i < layerCount; ++i) {
        layerMetaOffsets[i] = static_cast<uint64_t>(out.tellp());
        const auto& layer = layers[i];
        uint16_t lIdx = i;
        out.write(reinterpret_cast<const char*>(&lIdx), 2);
        uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(layer.getName().size(), 255));
        out.write(reinterpret_cast<const char*>(&nameLen), 1);
        out.write(layer.getName().data(), nameLen);
        uint8_t opacity = static_cast<uint8_t>(layer.getOpacity() * 255.0 + 0.5);
        uint8_t visible = layer.isVisible() ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&opacity), 1);
        out.write(reinterpret_cast<const char*>(&visible), 1);
    }

    std::vector<uint64_t> layerDataOffsets(layerCount);
    for (uint16_t i = 0; i < layerCount; ++i) {
        layerDataOffsets[i] = static_cast<uint64_t>(out.tellp());
        const auto& layer = layers[i];
        for (int cellIndex = 0; cellIndex < width * height; ++cellIndex) {
            const auto& cell = layer.getCell(cellIndex % width, cellIndex / width);
            uint8_t len = static_cast<uint8_t>(std::min<size_t>(cell.character.size(), 255));
            out.write(reinterpret_cast<const char*>(&len), 1);
            if (len > 0) out.write(cell.character.data(), len);
            out.write(reinterpret_cast<const char*>(&cell.fg.r), 1);
            out.write(reinterpret_cast<const char*>(&cell.fg.g), 1);
            out.write(reinterpret_cast<const char*>(&cell.fg.b), 1);
            out.write(reinterpret_cast<const char*>(&cell.fgA), 1);
            out.write(reinterpret_cast<const char*>(&cell.bg.r), 1);
            out.write(reinterpret_cast<const char*>(&cell.bg.g), 1);
            out.write(reinterpret_cast<const char*>(&cell.bg.b), 1);
            out.write(reinterpret_cast<const char*>(&cell.bgA), 1);
        }
    }

    // Write indexes
    out.seekp(indexPos);
    out.write(reinterpret_cast<const char*>(&imageStatsOffset), 8);
    out.write(reinterpret_cast<const char*>(&previewOffset), 8);
    out.write(reinterpret_cast<const char*>(&layerCount), 2);
    for (uint64_t off : layerMetaOffsets) out.write(reinterpret_cast<const char*>(&off), 8);
    for (uint64_t off : layerDataOffsets) out.write(reinterpret_cast<const char*>(&off), 8);

    return out.good();
}

} // namespace TilelandWorld
