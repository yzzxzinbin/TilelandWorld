#pragma once
#ifndef TILELANDWORLD_IMAGEASSET_H
#define TILELANDWORLD_IMAGEASSET_H

#include <vector>
#include <string>
#include <cstdint>
#include "../UI/AnsiTui.h" // For RGBColor

namespace TilelandWorld {

    struct ImageCell {
        std::string character; // UTF-8 character (e.g., "▄")
        RGBColor fg;
        RGBColor bg;
    };

    class ImageAsset {
    public:
        ImageAsset() = default;
        ImageAsset(int w, int h) : width(w), height(h), cells(w * h) {}

        int getWidth() const { return width; }
        int getHeight() const { return height; }

        const ImageCell& getCell(int x, int y) const {
            if (x < 0 || x >= width || y < 0 || y >= height) return emptyCell;
            return cells[y * width + x];
        }

        void setCell(int x, int y, const ImageCell& cell) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                cells[y * width + x] = cell;
            }
        }

        // Serialization
        bool save(const std::string& path) const;
        static ImageAsset load(const std::string& path);

    private:
        int width = 0;
        int height = 0;
        std::vector<ImageCell> cells;
        static ImageCell emptyCell;
    };

}

#endif
