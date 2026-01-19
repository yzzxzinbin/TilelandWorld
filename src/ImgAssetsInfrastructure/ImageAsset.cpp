#include "ImageAsset.h"
#include "YuiLayer.h"
#include <fstream>
#include <iostream>

namespace TilelandWorld {

    ImageCell ImageAsset::emptyCell = {" ", {0,0,0}, {0,0,0}};

    bool ImageAsset::save(const std::string& path) const {
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

        uint16_t layerCount = 1;
        out.write(reinterpret_cast<const char*>(&layerCount), sizeof(layerCount));

        uint16_t layerIndex = 0;
        out.write(reinterpret_cast<const char*>(&layerIndex), sizeof(layerIndex));
        const std::string layerName = "Layer 1";
        uint8_t nameLen = static_cast<uint8_t>(layerName.size());
        out.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) {
            out.write(layerName.data(), nameLen);
        }
        uint8_t opacityByte = 255;
        uint8_t visibleByte = 1;
        out.write(reinterpret_cast<const char*>(&opacityByte), sizeof(opacityByte));
        out.write(reinterpret_cast<const char*>(&visibleByte), sizeof(visibleByte));

        for (const auto& cell : cells) {
            uint8_t len = static_cast<uint8_t>(cell.character.size());
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

        return out.good();
    }

    ImageAsset ImageAsset::load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return ImageAsset(0, 0);

        char magic[5];
        in.read(magic, 5);
        if (std::string(magic, 5) != "TLIMG") return ImageAsset(0, 0);

        uint16_t ver;
        in.read(reinterpret_cast<char*>(&ver), sizeof(ver));
        if (ver == 2 || ver == 3) {
            in.close();
            YuiLayeredImage layered = YuiLayeredImage::load(path);
            return layered.flatten();
        }
        if (ver != 1) return ImageAsset(0, 0);

        uint16_t w, h;
        in.read(reinterpret_cast<char*>(&w), sizeof(w));
        in.read(reinterpret_cast<char*>(&h), sizeof(h));

        ImageAsset asset(w, h);
        for (int i = 0; i < w * h; ++i) {
            uint8_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            
            std::string ch;
            if (len > 0) {
                ch.resize(len);
                in.read(&ch[0], len);
            }

            RGBColor fg, bg;
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
            asset.setCell(i % w, i / w, cell);
        }

        return asset;
    }

}
