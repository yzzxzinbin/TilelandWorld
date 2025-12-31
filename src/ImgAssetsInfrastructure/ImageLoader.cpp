#include "ImageLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include <iostream>

namespace TilelandWorld {

    RawImage ImageLoader::load(const std::string& path) {
        RawImage img;
        int w, h, c;
        // Force 3 channels (RGB)
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &c, 3);
        
        if (!data) {
            // std::cerr << "Failed to load image: " << path << " Reason: " << stbi_failure_reason() << std::endl;
            return img;
        }

        img.width = w;
        img.height = h;
        img.channels = 3;
        img.data.assign(data, data + (w * h * 3));
        img.valid = true;

        stbi_image_free(data);
        return img;
    }

}
