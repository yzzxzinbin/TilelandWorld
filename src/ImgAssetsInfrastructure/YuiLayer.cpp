#include "YuiLayer.h"
#include <fstream>
#include <algorithm>

namespace TilelandWorld {

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
}

void YuiLayeredImage::insertLayer(int index, const YuiLayer& layer) {
    index = std::clamp(index, 0, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + index, layer);
    setActiveLayerIndex(index);
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
}

void YuiLayeredImage::moveLayer(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(layers.size()) || to >= static_cast<int>(layers.size())) return;
    if (from == to) return;
    YuiLayer layer = layers[from];
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, layer);
    activeLayer = to;
}

ImageCell YuiLayeredImage::getActiveCell(int x, int y) const {
    return activeLayerRef().getCell(x, y);
}

void YuiLayeredImage::setActiveCell(int x, int y, const ImageCell& cell) {
    activeLayerRef().setCell(x, y, cell);
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

ImageCell YuiLayeredImage::compositeCell(int x, int y) const {
    RGBColor bg{0, 0, 0};
    uint8_t bgA = 0;

    for (const auto& layer : layers) {
        if (!layer.isVisible()) continue;
        const auto& cell = layer.getCell(x, y);
        double layerOpacity = layer.getOpacity();
        uint8_t effectiveBgA = static_cast<uint8_t>(std::clamp(static_cast<int>(cell.bgA * layerOpacity + 0.5), 0, 255));
        blendOver(bg, bgA, cell.bg, effectiveBgA);
    }

    std::string glyph = " ";
    RGBColor fg = bg;
    uint8_t fgA = 0;

    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
        const auto& layer = layers[static_cast<size_t>(i)];
        if (!layer.isVisible()) continue;
        const auto& cell = layer.getCell(x, y);
        if (cell.character.empty() || cell.character == " ") continue;
        double layerOpacity = layer.getOpacity();
        uint8_t effectiveFgA = static_cast<uint8_t>(std::clamp(static_cast<int>(cell.fgA * layerOpacity + 0.5), 0, 255));
        if (effectiveFgA == 0) continue;
        glyph = cell.character;
        fg = blendToBackground(bg, cell.fg, effectiveFgA);
        fgA = 255;
        break;
    }

    ImageCell out;
    out.character = glyph;
    out.fg = fg;
    out.bg = bg;
    out.fgA = fgA;
    out.bgA = bgA;
    return out;
}

ImageAsset YuiLayeredImage::flatten() const {
    ImageAsset asset(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            ImageCell c = compositeCell(x, y);
            c.fgA = 255;
            c.bgA = 255;
            asset.setCell(x, y, c);
        }
    }
    return asset;
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

    if (ver != 2) return YuiLayeredImage(0, 0);

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

bool YuiLayeredImage::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const char magic[] = "TLIMG";
    out.write(magic, 5);

    uint16_t ver = 2;
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));

    uint16_t w = static_cast<uint16_t>(width);
    uint16_t h = static_cast<uint16_t>(height);
    out.write(reinterpret_cast<const char*>(&w), sizeof(w));
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    uint16_t layerCount = static_cast<uint16_t>(layers.size());
    out.write(reinterpret_cast<const char*>(&layerCount), sizeof(layerCount));

    for (uint16_t i = 0; i < layerCount; ++i) {
        const auto& layer = layers[i];
        uint16_t layerIndex = i;
        out.write(reinterpret_cast<const char*>(&layerIndex), sizeof(layerIndex));

        uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(layer.getName().size(), 255));
        out.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) {
            out.write(layer.getName().data(), nameLen);
        }

        uint8_t opacityByte = static_cast<uint8_t>(std::clamp(static_cast<int>(layer.getOpacity() * 255.0 + 0.5), 0, 255));
        uint8_t visibleByte = layer.isVisible() ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&opacityByte), sizeof(opacityByte));
        out.write(reinterpret_cast<const char*>(&visibleByte), sizeof(visibleByte));

        for (int cellIndex = 0; cellIndex < width * height; ++cellIndex) {
            int x = cellIndex % width;
            int y = cellIndex / width;
            const auto& cell = layer.getCell(x, y);
            uint8_t len = static_cast<uint8_t>(std::min<size_t>(cell.character.size(), 255));
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            if (len > 0) {
                out.write(cell.character.data(), len);
            }
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

    return out.good();
}

} // namespace TilelandWorld
