#pragma once
#ifndef TILELANDWORLD_UI_YUIEDITORSCREEN_H
#define TILELANDWORLD_UI_YUIEDITORSCREEN_H

#include "AnsiTui.h"
#include "TuiUtils.h"
#include "../Controllers/InputController.h"
#include "../ImgAssetsInfrastructure/AssetManager.h"
#include "../ImgAssetsInfrastructure/ImageAsset.h"
#include "../ImgAssetsInfrastructure/YuiLayer.h"
#include "ContextMenu.h"
#include "TextField.h"
#include <string>
#include <memory>

namespace TilelandWorld {
namespace UI {

class YuiEditorScreen {
public:
    YuiEditorScreen(AssetManager& manager, std::string assetName, YuiLayeredImage asset);
    void show();

private:
    enum class Tool { Hand, Property };

    AssetManager& manager;
    std::string assetName;
    YuiLayeredImage working;
    TuiSurface surface;
    TuiPainter painter;
    TuiTheme theme;
    InputController input;

    Tool activeMenu{Tool::Hand};
    int scrollX{0};
    int scrollY{0};
    bool dragging{false};
    int dragStartX{0};
    int dragStartY{0};
    int dragStartScrollX{0};
    int dragStartScrollY{0};
    bool draggingHThumb{false};
    bool draggingVThumb{false};
    bool hoverHThumb{false};
    bool hoverVThumb{false};
    int dragThumbStartX{0};
    int dragThumbStartY{0};
    int dragThumbStartOffsetX{0};
    int dragThumbStartOffsetY{0};

    // Canvas hover/selection
    bool hoverValid{false};
    int hoverX{0};
    int hoverY{0};
    bool hasSelection{false};
    int selX{0};
    int selY{0};
    ImageCell stagedCell{};
    ImageCell originalCell{};
    bool hasStaged{false};
    bool hoverConfirm{false};
    bool hoverCancel{false};

    // Layers panel
    bool showLayers{false};
    int layerPanelW{28};
    int layerPanelX{0};
    bool hoverLayerUp{false};
    bool hoverLayerDown{false};
    bool hoverLayerAdd{false};
    bool hoverLayerImport{false};
    bool dragLayerOpacity{false};
    double pendingOpacity{1.0};
    TextFieldState opacityInputState;
    std::string opacityText;

    // Context menu
    bool showLayerMenu{false};
    int layerMenuIdx{-1};
    ContextMenuState layerMenuState{};

    // Layout cache
    int canvasX{0};
    int canvasY{0};
    int canvasW{0};
    int canvasH{0};
    int propPanelW{0};
    int propPanelX{0};
    int rightPanelGap{0};

    void renderFrame();
    void drawMenubar();
    void drawSideToolbar();
    void drawCanvas();
    void drawScrollbars();
    void drawPropertyPanel();
    void drawLayerPanel();
    bool handleLayerPanelMouse(const InputEvent& ev);
    void handleMouse(const InputEvent& ev, bool& running);
    void handleKey(const InputEvent& ev, bool& running);
    void clampScroll();
    bool isInsideCanvas(int x, int y) const;
    bool openColorPicker(RGBColor initial, uint8_t initialA, RGBColor& outColor, uint8_t& outA);
    bool openGlyphDialog(const std::string& initial, std::string& outGlyph);
    bool openRenameDialog(const std::string& initial, std::string& outName);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_YUIEDITORSCREEN_H
