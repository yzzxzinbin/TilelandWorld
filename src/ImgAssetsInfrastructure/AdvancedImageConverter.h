#pragma once
#ifndef TILELANDWORLD_ADVANCEDIMAGECONVERTER_H
#define TILELANDWORLD_ADVANCEDIMAGECONVERTER_H

#include "ImageLoader.h"
#include "ImageAsset.h"
#include "../Utils/TaskSystem.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace TilelandWorld {

    // Structure to hold high-resolution resampled data (SoA layout)
    struct BlockPlanes {
        int width = 0;
        int height = 0;
        std::vector<int> r;
        std::vector<int> g;
        std::vector<int> b;
    };

    class AdvancedImageConverter {
    public:
        struct Options {
            int targetWidth = 120;
            int targetHeight = 80;
            int pruneThreshold = 24; // Color difference threshold for pruning
            enum class Quality { Low, High } quality = Quality::High;
            // Progress callback: (completedWork, totalWork, stageName)
            std::function<void(double, double, const std::string&)> onProgress;
        };

        // Main entry point: Convert raw image to ImageAsset using advanced logic
        static ImageAsset convert(const RawImage& img, const Options& opts, TaskSystem& taskSystem);

    private:
        // Resampling logic (Integral Image based)
        static BlockPlanes resampleToPlanes(const RawImage& img, int outW, int outH, TaskSystem& taskSystem, 
                                            const std::function<void(double)>& stageProgress = nullptr);

        // Rendering logic (Glyph matching)
        static ImageAsset renderToAsset(const BlockPlanes& highres, int outW, int outH, const Options& opts, TaskSystem& taskSystem,
                                         const std::function<void(double)>& stageProgress = nullptr);
        
        // Low quality rendering (Solid blocks)
        static ImageAsset renderLow(const BlockPlanes& highres, int outW, int outH, TaskSystem& taskSystem,
                                     const std::function<void(double)>& stageProgress = nullptr);
    };

}

#endif
