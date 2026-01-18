#pragma once
#ifndef TILELANDWORLD_ASSETMANAGER_H
#define TILELANDWORLD_ASSETMANAGER_H

#include <string>
#include <vector>
#include "ImageAsset.h"
#include "YuiLayer.h"

namespace TilelandWorld {

    class AssetManager {
    public:
        explicit AssetManager(const std::string& assetDir);
        
        struct FileEntry {
            std::string name; // Filename without extension
            std::string path; // Full path
            std::string folder; // Folder name (empty for root)
        };

        struct FolderEntry {
            std::string name;
        };

        // List all .tlimg files in the asset directory
        std::vector<FileEntry> listAssets() const;

        // Folders management
        std::vector<FolderEntry> listFolders() const;
        bool createFolder(const std::string& folderName);
        bool deleteFolder(const std::string& folderName, bool deleteAssets);
        bool renameFolder(const std::string& oldName, const std::string& newName);
        bool moveAssetToFolder(const std::string& assetName, const std::string& folderName);

        // Import an image file (BMP, PNG, JPG, etc.), convert it, and save as .tlimg
        bool importImage(const std::string& imagePath, const std::string& assetName);

        // Save an asset directly
        bool saveAsset(const ImageAsset& asset, const std::string& assetName);

        // Delete an asset file
        bool deleteAsset(const std::string& assetName);

        // Rename an existing asset file (stem only, without extension)
        bool renameAsset(const std::string& oldName, const std::string& newName);

        // Load an asset by name
        ImageAsset loadAsset(const std::string& assetName) const;

        // Layer-aware asset APIs (TLIMG v2)
        bool saveLayeredAsset(const YuiLayeredImage& asset, const std::string& assetName);
        YuiLayeredImage loadLayeredAsset(const std::string& assetName) const;

        const std::string& getRootDir() const { return rootDir; }

    private:
        std::string rootDir;
    };

}

#endif
