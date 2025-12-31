#include "BmpLoader.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace TilelandWorld {

#pragma pack(push, 1)
    struct BMPHeader {
        uint16_t bfType;      // "BM"
        uint32_t bfSize;
        uint16_t bfReserved1;
        uint16_t bfReserved2;
        uint32_t bfOffBits;
    };

    struct BMPInfoHeader {
        uint32_t biSize;
        int32_t  biWidth;
        int32_t  biHeight;
        uint16_t biPlanes;
        uint16_t biBitCount;
        uint32_t biCompression;
        uint32_t biSizeImage;
        int32_t  biXPelsPerMeter;
        int32_t  biYPelsPerMeter;
        uint32_t biClrUsed;
        uint32_t biClrImportant;
    };
#pragma pack(pop)

    RawImage BmpLoader::load(const std::string& path) {
        RawImage img;
        std::ifstream file(path, std::ios::binary);
        if (!file) return img;

        BMPHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (header.bfType != 0x4D42) { // "BM"
            return img;
        }

        BMPInfoHeader infoHeader;
        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

        if (infoHeader.biBitCount != 24 && infoHeader.biBitCount != 32) {
            // Only support 24/32 bit for simplicity
            return img;
        }

        if (infoHeader.biCompression != 0) { // BI_RGB
            return img;
        }

        int width = infoHeader.biWidth;
        int height = std::abs(infoHeader.biHeight);
        bool topDown = (infoHeader.biHeight < 0);

        img.width = width;
        img.height = height;
        img.data.resize(width * height * 3);

        file.seekg(header.bfOffBits, std::ios::beg);

        int rowPadding = (4 - (width * (infoHeader.biBitCount / 8)) % 4) % 4;
        int bytesPerPixel = infoHeader.biBitCount / 8;

        std::vector<uint8_t> rowBuffer(width * bytesPerPixel + rowPadding);

        for (int y = 0; y < height; ++y) {
            file.read(reinterpret_cast<char*>(rowBuffer.data()), rowBuffer.size());
            
            int targetY = topDown ? y : (height - 1 - y);
            
            for (int x = 0; x < width; ++x) {
                int srcIdx = x * bytesPerPixel;
                int dstIdx = (targetY * width + x) * 3;

                // BMP is BGR
                uint8_t b = rowBuffer[srcIdx];
                uint8_t g = rowBuffer[srcIdx + 1];
                uint8_t r = rowBuffer[srcIdx + 2];

                img.data[dstIdx] = r;
                img.data[dstIdx + 1] = g;
                img.data[dstIdx + 2] = b;
            }
        }

        img.valid = true;
        return img;
    }

}
