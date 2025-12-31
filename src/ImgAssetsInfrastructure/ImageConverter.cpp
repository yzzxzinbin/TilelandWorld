#include "ImageConverter.h"
#include <algorithm>
#include <cmath>

namespace TilelandWorld {

    ImageAsset ImageConverter::convert(const RawImage& img, int maxWidth, int maxHeight) {
        if (!img.valid || img.width == 0 || img.height == 0) return ImageAsset(0, 0);

        // 1. Calculate target dimensions (in sub-blocks/pixels)
        // Each TUI cell has 2 vertical sub-blocks.
        // So target height in sub-blocks = cell_height * 2.
        
        int targetW = img.width;
        int targetH = img.height;

        if (maxWidth > 0 && maxHeight > 0) {
            double scaleW = (double)maxWidth / img.width;
            double scaleH = (double)(maxHeight * 2) / img.height; // *2 because 2 sub-blocks per cell
            double scale = std::min(scaleW, scaleH);

            if (scale < 1.0) {
                targetW = static_cast<int>(img.width * scale);
                targetH = static_cast<int>(img.height * scale);
            }
        }
        
        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;

        // Ensure height is even for full cell coverage (optional, but good for 1x2 blocks)
        if (targetH % 2 != 0) targetH++;

        int cellsW = targetW;
        int cellsH = targetH / 2;

        ImageAsset asset(cellsW, cellsH);

        // 2. Resample (Box Filter)
        auto sampleColor = [&](int x, int y) -> RGBColor {
            // Map (x, y) in target [0, targetW)x[0, targetH) to source [0, img.width)x[0, img.height)
            // Box area in source:
            float srcX = (float)x * img.width / targetW;
            float srcY = (float)y * img.height / targetH;
            float srcW = (float)img.width / targetW;
            float srcH = (float)img.height / targetH;

            int startX = static_cast<int>(srcX);
            int startY = static_cast<int>(srcY);
            int endX = static_cast<int>(srcX + srcW);
            int endY = static_cast<int>(srcY + srcH);
            
            if (endX >= img.width) endX = img.width;
            if (endY >= img.height) endY = img.height;
            if (endX <= startX) endX = startX + 1;
            if (endY <= startY) endY = startY + 1;

            long r = 0, g = 0, b = 0;
            int count = 0;

            for (int sy = startY; sy < endY; ++sy) {
                for (int sx = startX; sx < endX; ++sx) {
                    int idx = (sy * img.width + sx) * 3;
                    r += img.data[idx];
                    g += img.data[idx+1];
                    b += img.data[idx+2];
                    count++;
                }
            }

            if (count == 0) return {0, 0, 0};
            return {static_cast<uint8_t>(r/count), static_cast<uint8_t>(g/count), static_cast<uint8_t>(b/count)};
        };

        for (int cy = 0; cy < cellsH; ++cy) {
            for (int cx = 0; cx < cellsW; ++cx) {
                // Top sub-block (Background)
                RGBColor bg = sampleColor(cx, cy * 2);
                // Bottom sub-block (Foreground)
                RGBColor fg = sampleColor(cx, cy * 2 + 1);

                asset.setCell(cx, cy, {"▄", fg, bg});
            }
        }

        return asset;
    }

}
