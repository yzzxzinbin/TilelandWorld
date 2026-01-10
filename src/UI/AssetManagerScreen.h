#pragma once
#ifndef TILELANDWORLD_UI_ASSETMANAGERSCREEN_H
#define TILELANDWORLD_UI_ASSETMANAGERSCREEN_H

#include "AnsiTui.h"
#include "../ImgAssetsInfrastructure/AssetManager.h"
#include "../ImgAssetsInfrastructure/AdvancedImageConverter.h"
#include "../Controllers/InputController.h"
#include "../Utils/TaskSystem.h"
#include "TextField.h"
#include "ContextMenu.h"
#include <memory>

namespace TilelandWorld {
namespace UI {

class AssetManagerScreen {
public:
    explicit AssetManagerScreen(const std::string& assetDir = "res/Assets");
    
    // Show the screen and block until user exits
    void show();

private:
    AssetManager manager;
    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;
    std::unique_ptr<InputController> input;
    TaskSystem taskSystem;

    std::vector<AssetManager::FileEntry> assets;
    std::vector<int> filteredIndices;
    int selectedIndex = 0; // Index within filteredIndices
    int listScrollOffset = 0;

    // Delete confirmation preference (per session)
    bool skipDeleteConfirm{false};

    // Search filter
    std::string searchQuery;
    TextFieldState searchState;
    
    // Preview
    ImageAsset currentPreview;
    bool previewLoaded = false;
    std::string lastPreviewName;

    // Persistence
    std::string lastImportPath = ".";

    // Layout cache for mouse hit-testing
    int listX{0}, listY{0}, listW{0}, listH{0};
    int searchFieldX{0}, searchFieldY{0}, searchFieldW{0};
    int buttonOpenX{0}, buttonRenameX{0}, buttonDeleteX{0}, buttonInfoX{0};

    // Context Menu
    ContextMenuState ctxMenuState;
    std::vector<std::string> ctxMenuItems = {"Open", "Rename", "Delete", "Info"};

    enum class HoverButton { None, Open, Rename, Delete, Info };
    int hoverRow{-1};
    HoverButton hoverButton{HoverButton::None};

    void refreshList(const std::string& preferredSelection = "");
    void applyFilter(const std::string& preferredSelection = "");
    std::string getSelectedAssetName() const;
    static bool isValidAssetName(const std::string& name);
    void ensureSelectionVisible();
    bool showRenameDialog(const std::string& currentName, std::string& outName);
    void drawMainUI(); // Renamed from render() to avoid confusion with present()
    void importAsset();
    void deleteCurrentAsset();
    bool showDeleteConfirmDialog(const std::string& assetName);
    void openInEditor();
    void renameCurrentAsset();
    void loadPreview();
    
    // Helper to draw the preview in a specific area
    void drawPreview(int x, int y, int w, int h);

    // Dialogs
    void showImportDialog(const std::string& filePath);
    // Show detailed info about current image asset
    void showInfoDialog(const ImageAsset& asset);
};

}
}

#endif
