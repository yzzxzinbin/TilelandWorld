#pragma once
#ifndef TILELANDWORLD_YUILAYER_H
#define TILELANDWORLD_YUILAYER_H

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "ImageAsset.h"

namespace TilelandWorld {

class YuiLayer {
public:
    YuiLayer() = default;
    YuiLayer(int w, int h, std::string name_ = "Layer")
        : name(std::move(name_)), width(w), height(h), cells(w * h) {}

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    const std::string& getName() const { return name; }
    void setName(const std::string& newName) { name = newName; }

    double getOpacity() const { return opacity; }
    void setOpacity(double value) { opacity = std::clamp(value, 0.0, 1.0); }

    bool isVisible() const { return visible; }
    void setVisible(bool value) { visible = value; }

    const ImageCell& getCell(int x, int y) const;
    void setCell(int x, int y, const ImageCell& cell);

    void resize(int newWidth, int newHeight);
    void clear(const ImageCell& fillCell = ImageCell{});

private:
    std::string name{"Layer"};
    int width{0};
    int height{0};
    std::vector<ImageCell> cells;
    double opacity{1.0};
    bool visible{true};
    static ImageCell emptyCell;
};

class YuiLayeredImage {
public:
    YuiLayeredImage() = default;
    YuiLayeredImage(int w, int h);

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    size_t getLayerCount() const { return layers.size(); }
    const std::vector<YuiLayer>& getLayers() const { return layers; }

    YuiLayer& getLayer(size_t index);
    const YuiLayer& getLayer(size_t index) const;

    int getActiveLayerIndex() const { return activeLayer; }
    void setActiveLayerIndex(int index);
    YuiLayer& activeLayerRef();
    const YuiLayer& activeLayerRef() const;

    void addLayer(const YuiLayer& layer);
    void insertLayer(int index, const YuiLayer& layer);
    void removeLayer(int index);
    void moveLayer(int from, int to);

    ImageCell getActiveCell(int x, int y) const;
    void setActiveCell(int x, int y, const ImageCell& cell);

    ImageCell compositeCell(int x, int y) const;
    ImageAsset flatten() const;

    static YuiLayeredImage fromImageAsset(const ImageAsset& asset);
    static YuiLayeredImage load(const std::string& path);
    bool save(const std::string& path) const;

private:
    int width{0};
    int height{0};
    std::vector<YuiLayer> layers;
    int activeLayer{0};

    static void blendOver(RGBColor& dstColor, uint8_t& dstAlpha, const RGBColor& srcColor, uint8_t srcAlpha);
    static RGBColor blendToBackground(const RGBColor& bg, const RGBColor& fg, uint8_t fgAlpha);
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_YUILAYER_H
