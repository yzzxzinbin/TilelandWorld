#pragma once
#ifndef TILELANDWORLD_IMAGECONVERTER_H
#define TILELANDWORLD_IMAGECONVERTER_H

#include "ImageLoader.h"
#include "ImageAsset.h"

namespace TilelandWorld {

    class ImageConverter {
    public:
        // Converts a raw image to a TUI asset.
        // Uses box sampling to resize the image to fit within maxWidth x maxHeight (cells).
        // If maxWidth/Height are 0, uses original size (1 pixel = 1 sub-block).
        static ImageAsset convert(const RawImage& img, int maxWidth = 0, int maxHeight = 0);
    };

}

#endif
