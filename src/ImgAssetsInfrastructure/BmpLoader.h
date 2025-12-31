#pragma once
#ifndef TILELANDWORLD_BMPLOADER_H
#define TILELANDWORLD_BMPLOADER_H

#include <string>
#include <vector>
#include <cstdint>

namespace TilelandWorld {

    struct RawImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> data; // RGBRGB... (3 bytes per pixel)
        bool valid = false;
    };

    class BmpLoader {
    public:
        static RawImage load(const std::string& path);
    };

}

#endif
