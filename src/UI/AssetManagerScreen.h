#pragma once
#ifndef TILELANDWORLD_UI_ASSETMANAGERSCREEN_H
#define TILELANDWORLD_UI_ASSETMANAGERSCREEN_H

#include "AnsiTui.h"
#include "../ImgAssetsInfrastructure/AssetManager.h"
#include "../ImgAssetsInfrastructure/AdvancedImageConverter.h"
#include "../Controllers/InputController.h"
#include "../Utils/TaskSystem.h"
#include <memory>

namespace TilelandWorld {
namespace UI {

class AssetManagerScreen {
public:
    AssetManagerScreen();
    
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
    int selectedIndex = 0;
    
    // Preview
    ImageAsset currentPreview;
    bool previewLoaded = false;
    std::string lastPreviewName;

    // Persistence
    std::string lastImportPath = ".";

    // Layout cache for mouse hit-testing
    int listX{0}, listY{0}, listW{0}, listH{0};
    int buttonOpenX{0}, buttonDeleteX{0}, buttonInfoX{0};

    void refreshList();
    void drawMainUI(); // Renamed from render() to avoid confusion with present()
    void importAsset();
    void deleteCurrentAsset();
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
