#pragma once
#ifndef TILELANDWORLD_UI_YUIEDITORSCREEN_H
#define TILELANDWORLD_UI_YUIEDITORSCREEN_H

#include "AnsiTui.h"
#include "TuiUtils.h"
#include "../Controllers/InputController.h"
#include "../ImgAssetsInfrastructure/AssetManager.h"
#include "../ImgAssetsInfrastructure/ImageAsset.h"
#include <string>
#include <memory>

namespace TilelandWorld {
namespace UI {

class YuiEditorScreen {
public:
    YuiEditorScreen(AssetManager& manager, std::string assetName, ImageAsset asset);
    void show();

private:
    enum class Tool { Hand, Property };

    AssetManager& manager;
    std::string assetName;
    ImageAsset working;
    TuiSurface surface;
    TuiPainter painter;
    TuiTheme theme;
    InputController input;

    Tool activeTool{Tool::Hand};
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

    // Layout cache
    int canvasX{0};
    int canvasY{0};
    int canvasW{0};
    int canvasH{0};
    int propPanelW{0};

    void renderFrame();
    void drawToolbar();
    void drawCanvas();
    void drawScrollbars();
    void drawPropertyPanel();
    void handleMouse(const InputEvent& ev, bool& running);
    void handleKey(const InputEvent& ev, bool& running);
    void clampScroll();
    bool isInsideCanvas(int x, int y) const;
    bool openColorPicker(RGBColor initial, RGBColor& outColor);
    bool openGlyphDialog(const std::string& initial, std::string& outGlyph);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_YUIEDITORSCREEN_H
