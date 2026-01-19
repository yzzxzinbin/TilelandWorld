#include "AssetManager.h"
#include "ImageLoader.h"
#include "ImageConverter.h"
#include "YuiLayer.h"
#include <filesystem>
#include <algorithm>
#include <system_error>
#include <fstream>
#include <sstream>
#include <map>

namespace fs = std::filesystem;

namespace TilelandWorld {

    namespace {
        struct Metadata {
            std::vector<std::string> folders;
            std::map<std::string, std::string> assetToFolder;
        };

        Metadata loadMetadata(const std::string& rootDir) {
            Metadata meta;
            fs::path p = fs::path(rootDir) / "folders.cfg";
            if (!fs::exists(p)) return meta;

            std::ifstream in(p);
            std::string line, section;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                if (line[0] == '[' && line.back() == ']') {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }
                if (section == "Folders") {
                    meta.folders.push_back(line);
                } else if (section == "Assignments") {
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        meta.assetToFolder[line.substr(0, colon)] = line.substr(colon + 1);
                    }
                }
            }
            return meta;
        }

        void saveMetadata(const std::string& rootDir, const Metadata& meta) {
            fs::path p = fs::path(rootDir) / "folders.cfg";
            std::ofstream out(p);
            out << "[Folders]\n";
            for (const auto& f : meta.folders) out << f << "\n";
            out << "[Assignments]\n";
            for (const auto& kv : meta.assetToFolder) out << kv.first << ":" << kv.second << "\n";
        }
    }

    AssetManager::AssetManager(const std::string& assetDir) : rootDir(assetDir) {
        if (!fs::exists(rootDir)) {
            fs::create_directories(rootDir);
        }
    }

    std::vector<AssetManager::FileEntry> AssetManager::listAssets() const {
        std::vector<FileEntry> entries;
        if (!fs::exists(rootDir)) return entries;

        Metadata meta = loadMetadata(rootDir);

        for (const auto& entry : fs::directory_iterator(rootDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tlimg") {
                std::string name = entry.path().stem().string();
                std::string folder = "";
                if (meta.assetToFolder.count(name)) {
                    folder = meta.assetToFolder.at(name);
                }
                entries.push_back({name, entry.path().string(), folder});
            }
        }
        return entries;
    }

    std::vector<AssetManager::FolderEntry> AssetManager::listFolders() const {
        Metadata meta = loadMetadata(rootDir);
        std::vector<FolderEntry> out;
        for (const auto& f : meta.folders) out.push_back({f});
        return out;
    }

    bool AssetManager::createFolder(const std::string& folderName) {
        if (folderName.empty()) return false;
        Metadata meta = loadMetadata(rootDir);
        if (std::find(meta.folders.begin(), meta.folders.end(), folderName) != meta.folders.end()) return false;
        meta.folders.push_back(folderName);
        saveMetadata(rootDir, meta);
        return true;
    }

    bool AssetManager::deleteFolder(const std::string& folderName, bool deleteAssets) {
        Metadata meta = loadMetadata(rootDir);
        auto it = std::find(meta.folders.begin(), meta.folders.end(), folderName);
        if (it == meta.folders.end()) return false;
        meta.folders.erase(it);

        std::vector<std::string> toRemove;
        for (auto& kv : meta.assetToFolder) {
            if (kv.second == folderName) {
                if (deleteAssets) {
                    this->deleteAsset(kv.first);
                }
                toRemove.push_back(kv.first);
            }
        }
        for (const auto& name : toRemove) meta.assetToFolder.erase(name);

        saveMetadata(rootDir, meta);
        return true;
    }

    bool AssetManager::renameFolder(const std::string& oldName, const std::string& newName) {
        if (newName.empty() || oldName == newName) return false;
        Metadata meta = loadMetadata(rootDir);
        auto it = std::find(meta.folders.begin(), meta.folders.end(), oldName);
        if (it == meta.folders.end()) return false;
        
        // check if new name exists
        if (std::find(meta.folders.begin(), meta.folders.end(), newName) != meta.folders.end()) return false;

        *it = newName;
        for (auto& kv : meta.assetToFolder) {
            if (kv.second == oldName) kv.second = newName;
        }
        saveMetadata(rootDir, meta);
        return true;
    }

    bool AssetManager::moveAssetToFolder(const std::string& assetName, const std::string& folderName) {
        Metadata meta = loadMetadata(rootDir);
        if (!folderName.empty() && std::find(meta.folders.begin(), meta.folders.end(), folderName) == meta.folders.end()) {
            return false;
        }
        if (folderName.empty()) {
            meta.assetToFolder.erase(assetName);
        } else {
            meta.assetToFolder[assetName] = folderName;
        }
        saveMetadata(rootDir, meta);
        return true;
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

    bool AssetManager::saveLayeredAsset(const YuiLayeredImage& asset, const std::string& assetName, int px, int py, int pw, int ph) {
        fs::path outPath = fs::path(rootDir) / (assetName + ".tlimg");
        return asset.save(outPath.string(), px, py, pw, ph);
    }

    bool AssetManager::deleteAsset(const std::string& assetName) {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        bool removed = false;
        if (fs::exists(p)) {
            removed = fs::remove(p);
        }
        
        Metadata meta = loadMetadata(rootDir);
        if (meta.assetToFolder.count(assetName)) {
            meta.assetToFolder.erase(assetName);
            saveMetadata(rootDir, meta);
        }
        
        return removed;
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
        if (ec) return false;

        Metadata meta = loadMetadata(rootDir);
        if (meta.assetToFolder.count(oldName)) {
            std::string folder = meta.assetToFolder[oldName];
            meta.assetToFolder.erase(oldName);
            meta.assetToFolder[newName] = folder;
            saveMetadata(rootDir, meta);
        }

        return true;
    }

    ImageAsset AssetManager::loadAsset(const std::string& assetName) const {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        return ImageAsset::load(p.string());
    }

    ImageAsset AssetManager::loadPreview(const std::string& assetName) const {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        return YuiLayeredImage::loadPreview(p.string());
    }

    YuiImageMetadata AssetManager::loadImageMetadata(const std::string& assetName) const {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        return YuiLayeredImage::loadImageMetadata(p.string());
    }

    YuiLayeredImage AssetManager::loadLayeredAsset(const std::string& assetName) const {
        fs::path p = fs::path(rootDir) / (assetName + ".tlimg");
        return YuiLayeredImage::load(p.string());
    }

}
