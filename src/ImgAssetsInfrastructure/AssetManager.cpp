#include "AssetManager.h"
#include "ImageLoader.h"
#include "ImageConverter.h"
#include <filesystem>
#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace TilelandWorld {

    AssetManager::AssetManager(const std::string& assetDir) : rootDir(assetDir) {
        if (!fs::exists(rootDir)) {
            fs::create_directories(rootDir);
        }
    }

    std::vector<AssetManager::FileEntry> AssetManager::listAssets() const {
        std::vector<FileEntry> entries;
        if (!fs::exists(rootDir)) return entries;

        for (const auto& entry : fs::directory_iterator(rootDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tlimg") {
                entries.push_back({entry.path().stem().string(), entry.path().string()});
            }
        }
        return entries;
    }

    bool AssetManager::importImage(const std::string& imagePath, const std::string& assetName) {
        RawImage raw = ImageLoader::load(imagePath);
        if (!raw.valid) return false;

        // Limit to reasonable TUI size, e.g., 120x80 cells
        ImageAsset asset = ImageConverter::convert(raw, 120, 80);
        
        return saveAsset(asset, assetName);
    }

    bool AssetManager::saveAsset(const ImageAsset& asset, const std::string& assetName) {
        fs::path outPath = fs::path(rootDir) / (assetName + ".tlimg");
        return asset.save(outPath.string());
    }

    bool AssetManager::deleteAsset(const std::string& assetName) {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        if (fs::exists(p)) {
            return fs::remove(p);
        }
        return false;
    }

    bool AssetManager::renameAsset(const std::string& oldName, const std::string& newName) {
        if (oldName.empty() || newName.empty()) return false;
        if (oldName == newName) return true;

        fs::path oldPath = fs::path(rootDir) / (oldName + ".tlimg");
        fs::path newPath = fs::path(rootDir) / (newName + ".tlimg");

        if (!fs::exists(oldPath) || fs::exists(newPath)) {
            return false;
        }

        std::error_code ec;
        fs::rename(oldPath, newPath, ec);
        return !ec;
    }

    ImageAsset AssetManager::loadAsset(const std::string& assetName) const {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        return ImageAsset::load(p.string());
    }

}
