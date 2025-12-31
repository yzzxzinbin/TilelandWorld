#pragma once
#ifndef TILELANDWORLD_ASSETMANAGER_H
#define TILELANDWORLD_ASSETMANAGER_H

#include <string>
#include <vector>
#include "ImageAsset.h"

namespace TilelandWorld {

    class AssetManager {
    public:
        explicit AssetManager(const std::string& assetDir);
        
        struct FileEntry {
            std::string name; // Filename without extension
            std::string path; // Full path
        };

        // List all .tlimg files in the asset directory
        std::vector<FileEntry> listAssets() const;

        // Import an image file (BMP, PNG, JPG, etc.), convert it, and save as .tlimg
        bool importImage(const std::string& imagePath, const std::string& assetName);

        // Save an asset directly
        bool saveAsset(const ImageAsset& asset, const std::string& assetName);

        // Delete an asset file
        bool deleteAsset(const std::string& assetName);

        // Load an asset by name
        ImageAsset loadAsset(const std::string& assetName) const;

        const std::string& getRootDir() const { return rootDir; }

    private:
        std::string rootDir;
    };

}

#endif
