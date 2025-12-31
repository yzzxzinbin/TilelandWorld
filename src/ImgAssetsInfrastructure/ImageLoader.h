#pragma once
#ifndef TILELANDWORLD_IMAGELOADER_H
#define TILELANDWORLD_IMAGELOADER_H

#include <string>
#include <vector>
#include <cstdint>

namespace TilelandWorld {

    struct RawImage {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<uint8_t> data; // RGBRGB... or RGBARGBA...
        bool valid = false;
    };

    class ImageLoader {
    public:
        static RawImage load(const std::string& path);
    };

}

#endif
