#include "YuiEditorScreen.h"
#include "TuiUtils.h"
#include "DirectoryBrowserScreen.h"
#include "TextField.h"
#include "../Utils/EnvConfig.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
    const TilelandWorld::UI::BoxStyle kFrame{"╭","╮","╰","╯","─","│"};
    TilelandWorld::RGBColor darken(const TilelandWorld::RGBColor& c, double factor) {
        double f = std::clamp(factor, 0.0, 1.0);
        return {
            static_cast<uint8_t>(std::max(0.0, c.r * f)),
            static_cast<uint8_t>(std::max(0.0, c.g * f)),
            static_cast<uint8_t>(std::max(0.0, c.b * f))
        };
    }
}

namespace TilelandWorld {
namespace UI {

YuiEditorScreen::YuiEditorScreen(AssetManager& manager_, std::string assetName_, YuiLayeredImage asset_)
    : manager(manager_), assetName(std::move(assetName_)), working(std::move(asset_)), surface(100, 40) {
}

void YuiEditorScreen::show() {
    input.setRestoreOnExit(false); // keep VT/mouse mode intact for caller
    input.start();
    bool running = true;
    while (running) {
        renderFrame();
        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        for (const auto& ev : events) {
            if (showLayerMenu) {
                bool close = false;
                std::vector<std::string> opts = {"Move Up", "Move Down", "Rename", "Delete"};
                int sel = ContextMenu::handleInput(ev, opts, layerMenuState, close);
                if (sel != -1) {
                    if (sel == 0) { // Move Up
                        if (layerMenuIdx < (int)working.getLayerCount() - 1)
                            working.moveLayer(layerMenuIdx, layerMenuIdx + 1);
                    } else if (sel == 1) { // Move Down
                        if (layerMenuIdx > 0)
                            working.moveLayer(layerMenuIdx, layerMenuIdx - 1);
                    } else if (sel == 2) { // Rename
                        std::string newName;
                        showLayerMenu = false;
                        if (openRenameDialog(working.getLayer(layerMenuIdx).getName(), newName)) {
                            working.getLayer(layerMenuIdx).setName(newName);
                        }
                    } else if (sel == 3) { // Delete
                        showLayerMenu = false;
                        if (working.getLayerCount() > 1) {
                            working.removeLayer(layerMenuIdx);
                        }
                    }
                    showLayerMenu = false;
                }
                if (close) showLayerMenu = false;
                if (ev.type == InputEvent::Type::Mouse && ev.pressed) {
                    // fall through if clicked outside but menu was visible? 
                    // No, ContextMenu::handleInput should consume most things.
                }
                continue;
            }
            if (ev.type == InputEvent::Type::Mouse) {
                handleMouse(ev, running);
            } else if (ev.type == InputEvent::Type::Key) {
                handleKey(ev, running);
            }
            if (!running) break;
        }
    }
    manager.saveLayeredAsset(working, assetName);
    painter.reset();
    input.stop();
}

void YuiEditorScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(60, static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1));
        int consoleHeight = std::max(24, static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1));
        surface.resize(consoleWidth, consoleHeight);
    }
#endif

    surface.clear(theme.itemFg, theme.background, " ");
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.drawText(2, 1, "Unicode Image Editor - " + assetName, {0,0,0}, theme.accent);
    drawToolbar();

    propPanelW = hasSelection ? 28 : 0;
    layerPanelW = showLayers ? 28 : 0;
    int rightPanels = 0;
    if (propPanelW > 0) rightPanels += propPanelW;
    if (layerPanelW > 0) rightPanels += layerPanelW;
    if (propPanelW > 0 && layerPanelW > 0) rightPanels += 1; // gap

    canvasX = 2;
    canvasY = 4;
    canvasW = std::max(10, surface.getWidth() - canvasX - 2 - rightPanels);
    canvasH = std::max(6, surface.getHeight() - canvasY - 2);

    if (layerPanelW > 0) {
        layerPanelX = surface.getWidth() - layerPanelW - 2;
    }
    if (propPanelW > 0) {
        propPanelX = (layerPanelW > 0) ? (layerPanelX - 1 - propPanelW) : (surface.getWidth() - propPanelW - 2);
    }

    surface.fillRect(canvasX, canvasY, canvasW, canvasH, theme.itemFg, theme.panel, " ");
    surface.drawFrame(canvasX, canvasY, canvasW, canvasH, kFrame, theme.itemFg, theme.panel);

    drawCanvas();
    drawScrollbars();
    if (hasSelection) {
        drawPropertyPanel();
    }
    if (showLayers) {
        drawLayerPanel();
    }

    if (showLayerMenu) {
        ContextMenu::render(surface, {"Move Up", "Move Down", "Rename", "Delete"}, layerMenuState);
    }

    surface.drawCenteredText(0, surface.getHeight() - 2, surface.getWidth(), 
        "Space: toggle tool | Mouse wheel: scroll | Drag (hand): pan | Q: save & back", 
        theme.hintFg, theme.background);
}

static constexpr int kToolbarY = 3; // leave a blank line above the toolbar

void YuiEditorScreen::drawToolbar() {
    int y = kToolbarY;
    std::string hand = activeTool == Tool::Hand ? "[ Hand ]" : "  Hand  ";
    std::string prop = activeTool == Tool::Property ? "[ Property ]" : "  Property  ";
    std::string layers = showLayers ? "[ Layers ]" : "  Layers  ";
    int x = 2;
    auto drawBtn = [&](const std::string& label, bool active) {
        RGBColor bg = active ? darken(theme.accent, 0.6) : theme.accent;
        RGBColor fg = active ? RGBColor{255,255,255} : theme.title;
        surface.drawText(x, y, label, fg, bg);
        x += static_cast<int>(label.size()) + 2;
    };
    drawBtn(hand, activeTool == Tool::Hand);
    drawBtn(prop, activeTool == Tool::Property);
    drawBtn(layers, showLayers);
}

void YuiEditorScreen::drawCanvas() {
    clampScroll();
    int viewW = canvasW - 2; // leave frame lines
    int viewH = canvasH - 2;
    int startX = scrollX;
    int startY = scrollY;
    hoverValid = hoverValid && hoverX >= startX && hoverY >= startY && hoverX < startX + viewW && hoverY < startY + viewH;

    for (int vy = 0; vy < viewH; ++vy) {
        int ay = startY + vy;
        for (int vx = 0; vx < viewW; ++vx) {
            int ax = startX + vx;
            RGBColor bg = theme.panel;
            RGBColor fg = theme.itemFg;
            std::string glyph = " ";
            if (ax >= 0 && ax < working.getWidth() && ay >= 0 && ay < working.getHeight()) {
                const auto cell = working.compositeCell(ax, ay);
                glyph = cell.character.empty() ? " " : cell.character;
                fg = cell.fg;
                bg = (cell.bgA == 0) ? theme.panel : cell.bg;
                if (hoverValid && ax == hoverX && ay == hoverY) {
                    fg = TuiUtils::blendColor(fg, {255,255,255}, 0.2);
                    bg = TuiUtils::blendColor(bg, {255,255,255}, 0.2);
                }
                if (hasSelection && ax == selX && ay == selY) {
                    bg = TuiUtils::blendColor(bg, theme.focusBg, 0.35);
                }
            }
            surface.drawText(canvasX + 1 + vx, canvasY + 1 + vy, glyph, fg, bg);
        }
    }
}

void YuiEditorScreen::drawScrollbars() {
    int viewW = canvasW - 2;
    int viewH = canvasH - 2;
    int contentW = std::max(1, working.getWidth());
    int contentH = std::max(1, working.getHeight());
    bool showH = contentW > viewW;
    bool showV = contentH > viewH;

    RGBColor trackColor{220, 220, 220};
    RGBColor thumbColor{140, 140, 140};
    RGBColor thumbActive{98, 98, 98};

    // Horizontal
    if (showH) {
        int barY = canvasY + canvasH - 1;
        int trackX = canvasX + 1;
        int trackW = viewW;
        surface.fillRect(trackX, barY, trackW, 1, trackColor, trackColor, " ");
        int thumbW = std::max(2, trackW * viewW / contentW);
        thumbW = std::min(thumbW, trackW); // avoid spill when very small diff
        int thumbX = trackX + (trackW - thumbW) * scrollX / std::max(1, contentW - viewW);
        RGBColor active = (draggingHThumb || hoverHThumb) ? thumbActive : thumbColor;
        surface.fillRect(thumbX, barY, thumbW, 1, active, active, " ");
    }

    // Vertical
    if (showV) {
        int barX = canvasX + canvasW - 1;
        int trackY = canvasY + 1;
        int trackH = viewH;
        surface.fillRect(barX, trackY, 1, trackH, trackColor, trackColor, " ");
        int thumbH = std::max(2, trackH * viewH / contentH);
        thumbH = std::min(thumbH, trackH);
        int thumbY = trackY + (trackH - thumbH) * scrollY / std::max(1, contentH - viewH);
        RGBColor active = (draggingVThumb || hoverVThumb) ? thumbActive : thumbColor;
        surface.fillRect(barX, thumbY, 1, thumbH, active, active, " ");
    }
}

void YuiEditorScreen::drawPropertyPanel() {
    int x = propPanelX;
    int y = canvasY;
    int w = propPanelW;
    int h = canvasH;
    surface.fillRect(x, y, w, h, theme.itemFg, theme.panel, " ");
    surface.drawFrame(x, y, w, h, kFrame, theme.itemFg, theme.panel);
    surface.fillRect(x + 1, y + 1, w - 2, 1, theme.title, theme.background, " ");
    surface.drawText(x + 2, y + 1, "Properties", theme.title, theme.background);

    const auto& cell = hasStaged ? stagedCell : working.getActiveCell(selX, selY);
    int line = y + 3;
    surface.drawText(x + 2, line++, "Pos: (" + std::to_string(selX) + "," + std::to_string(selY) + ")", theme.itemFg, theme.panel);
    surface.drawText(x + 2, line, "Glyph:", theme.itemFg, theme.panel);
    surface.drawText(x + 10, line++, " [" + (cell.character.empty()?" ":cell.character) + "] ", theme.itemFg, theme.panel);
    std::ostringstream fgss;
    fgss << "FG: " << (int)cell.fg.r << "," << (int)cell.fg.g << "," << (int)cell.fg.b;
    surface.drawText(x + 2, line++, fgss.str(), theme.itemFg, theme.panel);
    surface.drawText(x + 2, line++, "FG A: " + std::to_string((int)cell.fgA), theme.itemFg, theme.panel);
    std::ostringstream bgss;
    bgss << "BG: " << (int)cell.bg.r << "," << (int)cell.bg.g << "," << (int)cell.bg.b;
    surface.drawText(x + 2, line++, bgss.str(), theme.itemFg, theme.panel);
    surface.drawText(x + 2, line++, "BG A: " + std::to_string((int)cell.bgA), theme.itemFg, theme.panel);
    surface.drawText(x + 2, line++, "Click FG/BG to edit RGBA", theme.hintFg, theme.panel);
    surface.drawText(x + 2, line++, "Click glyph to change", theme.hintFg, theme.panel);

    // Buttons
    std::string okLabel = "[Confirm]";
    std::string cancelLabel = "[Cancel]";
    int btnY = y + h - 2;
    int okX = x + 2;
    int cancelX = x + w - 2 - static_cast<int>(cancelLabel.size());
    RGBColor cancelHoverBg{255, 192, 203};
    auto paintBtn = [&](int bx, const std::string& label, bool hover){
        bool isCancel = (label == cancelLabel);
        RGBColor fg = hover ? theme.background : theme.itemFg;
        RGBColor bg;
        if (hover && isCancel) {
            bg = cancelHoverBg;
        } else {
            bg = hover ? theme.focusBg : theme.panel;
        }
        surface.drawText(bx, btnY, label, fg, bg);
    };
    paintBtn(okX, okLabel, hoverConfirm);
    paintBtn(cancelX, cancelLabel, hoverCancel);
}

void YuiEditorScreen::drawLayerPanel() {
    int x = layerPanelX;
    int y = canvasY;
    int w = layerPanelW;
    int h = canvasH;
    surface.fillRect(x, y, w, h, theme.itemFg, theme.panel, " ");
    surface.drawFrame(x, y, w, h, kFrame, theme.itemFg, theme.panel);
    surface.fillRect(x + 1, y + 1, w - 2, 1, theme.title, theme.background, " ");
    surface.drawText(x + 2, y + 1, "Layers", theme.title, theme.background);

    int upX = x + w - 4;
    int downX = x + w - 2;
    RGBColor upFg = hoverLayerUp ? theme.focusFg : theme.title;
    RGBColor upBg = hoverLayerUp ? theme.focusBg : theme.background;
    RGBColor downFg = hoverLayerDown ? theme.focusFg : theme.title;
    RGBColor downBg = hoverLayerDown ? theme.focusBg : theme.background;
    surface.drawText(upX, y + 1, "↿", upFg, upBg);
    surface.drawText(downX, y + 1, "⇂", downFg, downBg);

    int listStart = y + 3;
    int listRows = std::max(0, h - 8);
    int layerCount = static_cast<int>(working.getLayerCount());
    for (int row = 0; row < listRows && row < layerCount; ++row) {
        int layerIndex = layerCount - 1 - row;
        const auto& layer = working.getLayer(static_cast<size_t>(layerIndex));
        bool active = (layerIndex == working.getActiveLayerIndex());
        RGBColor bg = active ? theme.focusBg : theme.panel;
        RGBColor fg = active ? theme.focusFg : theme.itemFg;
        surface.fillRect(x + 1, listStart + row, w - 2, 1, fg, bg, " ");
        std::string vis = layer.isVisible() ? "[V]" : "[ ]";
        surface.drawText(x + 2, listStart + row, vis, fg, bg);
        std::string name = TuiUtils::trimToUtf8VisualWidth(layer.getName(), static_cast<size_t>(std::max(0, w - 8)));
        surface.drawText(x + 6, listStart + row, name, fg, bg);
    }

    int btnY = y + h - 5;
    std::string addLabel = "[+ New]";
    std::string importLabel = "[Import]";
    int addX = x + 2;
    int importX = x + w - 2 - static_cast<int>(importLabel.size());
    RGBColor addFg = hoverLayerAdd ? theme.background : theme.itemFg;
    RGBColor addBg = hoverLayerAdd ? theme.focusBg : theme.panel;
    RGBColor impFg = hoverLayerImport ? theme.background : theme.itemFg;
    RGBColor impBg = hoverLayerImport ? theme.focusBg : theme.panel;
    surface.drawText(addX, btnY, addLabel, addFg, addBg);
    surface.drawText(importX, btnY, importLabel, impFg, impBg);

    const auto& activeLayer = working.activeLayerRef();
    int infoY = y + h - 3;
    int barY = y + h - 2;
    int opacityPct = static_cast<int>(std::round(activeLayer.getOpacity() * 100.0));
    surface.drawText(x + 2, infoY, "Opacity: " + std::to_string(opacityPct) + "%", theme.itemFg, theme.panel);

    int barX = x + 2;
    int barW = std::max(1, w - 4);
    int filled = static_cast<int>(std::round(activeLayer.getOpacity() * (barW - 1)));
    filled = std::clamp(filled, 0, std::max(0, barW - 1));
    surface.fillRect(barX, barY, barW, 1, theme.itemFg, theme.panel, " ");
    if (barW > 0) {
        surface.fillRect(barX, barY, filled + 1, 1, theme.focusFg, theme.focusBg, " ");
    }
}

bool YuiEditorScreen::handleLayerPanelMouse(const InputEvent& ev) {
    if (!showLayers) return false;
    int x = layerPanelX;
    int y = canvasY;
    int w = layerPanelW;
    int h = canvasH;

    if (ev.button == 0 && !ev.pressed && !ev.move) {
        dragLayerOpacity = false;
    }

    if (ev.x < x || ev.x >= x + w || ev.y < y || ev.y >= y + h) {
        return false;
    }

    int titleY = y + 1;
    int upX = x + w - 4;
    int downX = x + w - 2;

    if (ev.move) {
        hoverLayerUp = (ev.y == titleY && ev.x == upX);
        hoverLayerDown = (ev.y == titleY && ev.x == downX);
    }

    if (ev.button == 0 && ev.pressed && ev.y == titleY) {
        int idx = working.getActiveLayerIndex();
        int count = static_cast<int>(working.getLayerCount());
        if (ev.x == upX && idx < count - 1) {
            working.moveLayer(idx, idx + 1);
        } else if (ev.x == downX && idx > 0) {
            working.moveLayer(idx, idx - 1);
        }
        return true;
    }

    int listStart = y + 3;
    int listRows = std::max(0, h - 8);
    int layerCount = static_cast<int>(working.getLayerCount());
    if (ev.y >= listStart && ev.y < listStart + listRows) {
        int row = ev.y - listStart;
        if (row < layerCount) {
            int layerIndex = layerCount - 1 - row;
            if (ev.button == 2 && ev.pressed) {
                showLayerMenu = true;
                layerMenuIdx = layerIndex;
                layerMenuState.visible = true;
                layerMenuState.x = ev.x;
                layerMenuState.y = ev.y;
                layerMenuState.selectedIndex = 0;
                layerMenuState.width = ContextMenu::calculateWidth({"Move Up", "Move Down", "Rename", "Delete"});
                return true;
            }
            if (ev.button == 0 && ev.pressed) {
                if (ev.x >= x + 2 && ev.x < x + 5) {
                    const auto& layer = working.getLayer(static_cast<size_t>(layerIndex));
                    working.setLayerVisible(layerIndex, !layer.isVisible());
                } else {
                    working.setActiveLayerIndex(layerIndex);
                }
            }
            return true;
        }
    }

    int btnY = y + h - 5;
    std::string addLabel = "[+ New]";
    std::string importLabel = "[Import]";
    int addX = x + 2;
    int importX = x + w - 2 - static_cast<int>(importLabel.size());
    if (ev.move && ev.y == btnY) {
        hoverLayerAdd = (ev.x >= addX && ev.x < addX + static_cast<int>(addLabel.size()));
        hoverLayerImport = (ev.x >= importX && ev.x < importX + static_cast<int>(importLabel.size()));
    }
    if (ev.button == 0 && ev.pressed && ev.y == btnY) {
        if (ev.x >= addX && ev.x < addX + static_cast<int>(addLabel.size())) {
            int idx = static_cast<int>(working.getLayerCount()) + 1;
            YuiLayer layer(working.getWidth(), working.getHeight(), "Layer " + std::to_string(idx));
            working.addLayer(layer);
            return true;
        }
        if (ev.x >= importX && ev.x < importX + static_cast<int>(importLabel.size())) {
            input.stop();
            DirectoryBrowserScreen browser(manager.getRootDir(), true, ".tlimg");
            auto paths = browser.show();
            if (!paths.empty()) {
                for (const auto& path : paths) {
                    ImageAsset asset = ImageAsset::load(path);
                    YuiLayer layer(working.getWidth(), working.getHeight(), "Imported");
                    int maxW = std::min(asset.getWidth(), working.getWidth());
                    int maxH = std::min(asset.getHeight(), working.getHeight());
                    for (int yy = 0; yy < maxH; ++yy) {
                        for (int xx = 0; xx < maxW; ++xx) {
                            layer.setCell(xx, yy, asset.getCell(xx, yy));
                        }
                    }
                    working.addLayer(layer);
                }
            }
            input.start();
            return true;
        }
    }

    int barY = y + h - 2;
    int barX = x + 2;
    int barW = std::max(1, w - 4);
    if ((ev.button == 0 && ev.pressed && ev.y == barY) || (ev.move && dragLayerOpacity)) {
        double t = (barW > 1) ? (static_cast<double>(ev.x - barX) / (barW - 1)) : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        working.setLayerOpacity(working.getActiveLayerIndex(), t);
        dragLayerOpacity = true;
        return true;
    }

    return true;
}

void YuiEditorScreen::handleMouse(const InputEvent& ev, bool& running) {
    int mx = ev.x;
    int my = ev.y;
    hoverHThumb = false;
    hoverVThumb = false;
    hoverConfirm = false;
    hoverCancel = false;
    hoverLayerUp = false;
    hoverLayerDown = false;
    hoverLayerAdd = false;
    hoverLayerImport = false;
    // Toolbar button hit-test
    if (my == kToolbarY) {
        int x = 2;
        std::string handLabel = activeTool == Tool::Hand ? "[ Hand ]" : "  Hand  ";
        std::string propLabel = activeTool == Tool::Property ? "[ Property ]" : "  Property  ";
        std::string layersLabel = showLayers ? "[ Layers ]" : "  Layers  ";
        int handStart = x;
        int handEnd = handStart + static_cast<int>(handLabel.size());
        x = handEnd + 2;
        int propStart = x;
        int propEnd = propStart + static_cast<int>(propLabel.size());
        x = propEnd + 2;
        int layersStart = x;
        int layersEnd = layersStart + static_cast<int>(layersLabel.size());

        if (ev.button == 0 && ev.pressed) {
            if (mx >= handStart && mx < handEnd) {
                activeTool = Tool::Hand;
                dragging = false;
            } else if (mx >= propStart && mx < propEnd) {
                activeTool = Tool::Property;
                dragging = false;
            } else if (mx >= layersStart && mx < layersEnd) {
                showLayers = !showLayers;
                dragging = false;
            }
        }
        // Do not treat toolbar clicks as canvas interactions
        if (ev.button == 0 && ev.pressed) return;
    }

    if (handleLayerPanelMouse(ev)) {
        return;
    }
    if (isInsideCanvas(mx, my)) {
        int localX = mx - (canvasX + 1);
        int localY = my - (canvasY + 1);
        hoverX = scrollX + localX;
        hoverY = scrollY + localY;
        hoverValid = true;
    } else {
        hoverValid = false;
    }

    if (ev.wheel != 0) {
        scrollY -= ev.wheel * 3;
        clampScroll();
    }

    int viewW = canvasW - 2;
    int viewH = canvasH - 2;
    int contentW = std::max(1, working.getWidth());
    int contentH = std::max(1, working.getHeight());
    bool showH = contentW > viewW;
    bool showV = contentH > viewH;
    if (!showH) draggingHThumb = false;
    if (!showV) draggingVThumb = false;

    if (!ev.pressed && ev.button == 0 && ev.move) {
        if (dragging && activeTool == Tool::Hand) {
            scrollX = dragStartScrollX - (mx - dragStartX);
            scrollY = dragStartScrollY - (my - dragStartY);
            clampScroll();
        } else if (draggingHThumb && showH) {
            int trackW = viewW;
            int trackX = canvasX + 1;
            int thumbW = std::max(2, trackW * viewW / contentW);
            thumbW = std::min(thumbW, trackW);
            int trackSpan = std::max(1, trackW - thumbW);
            int delta = mx - dragThumbStartX;
            int newThumbX = std::clamp(dragThumbStartOffsetX + delta, 0, trackSpan);
            scrollX = newThumbX * std::max(1, contentW - viewW) / trackSpan;
            clampScroll();
        } else if (draggingVThumb && showV) {
            int trackH = viewH;
            int trackY = canvasY + 1;
            int thumbH = std::max(2, trackH * viewH / contentH);
            thumbH = std::min(thumbH, trackH);
            int trackSpan = std::max(1, trackH - thumbH);
            int delta = my - dragThumbStartY;
            int newThumbY = std::clamp(dragThumbStartOffsetY + delta, 0, trackSpan);
            scrollY = newThumbY * std::max(1, contentH - viewH) / trackSpan;
            clampScroll();
        }
        return;
    }

    if (ev.button == 0 && ev.pressed) {
        if (isInsideCanvas(mx, my)) {
            int localX = mx - (canvasX + 1);
            int localY = my - (canvasY + 1);
            int ax = scrollX + localX;
            int ay = scrollY + localY;
            if (activeTool == Tool::Hand) {
                dragging = true;
                dragStartX = mx;
                dragStartY = my;
                dragStartScrollX = scrollX;
                dragStartScrollY = scrollY;
            } else if (activeTool == Tool::Property) {
                if (ax >= 0 && ax < working.getWidth() && ay >= 0 && ay < working.getHeight()) {
                    selX = ax;
                    selY = ay;
                    hasSelection = true;
                    originalCell = working.getActiveCell(selX, selY);
                    stagedCell = originalCell;
                    hasStaged = true;
                }
            }
        } else {
            dragging = false;
        }
    }

    if (ev.button == 0 && !ev.pressed) {
        dragging = false;
        draggingHThumb = false;
        draggingVThumb = false;
    }

    if (hasSelection) {
        // check property panel interactions
        int px = propPanelX;
        int py = canvasY;
        if (mx >= px && mx < px + propPanelW && my >= py && my < py + canvasH) {
            int lineGlyph = py + 4;
            int lineFg = lineGlyph + 1;
            int lineBg = lineFg + 2;
            int btnY = py + canvasH - 2;
            std::string okLabel = "[Confirm]";
            std::string cancelLabel = "[Cancel]";
            int okX = px + 2;
            int cancelX = px + propPanelW - 2 - static_cast<int>(cancelLabel.size());
            if (ev.move) {
                if (my == btnY) {
                    hoverConfirm = (mx >= okX && mx < okX + static_cast<int>(okLabel.size()));
                    hoverCancel = (mx >= cancelX && mx < cancelX + static_cast<int>(cancelLabel.size()));
                }
            }
            if (ev.button == 0 && ev.pressed && my == lineGlyph && mx >= px + 10 && mx < px + propPanelW - 2) {
                std::string glyph = stagedCell.character;
                if (openGlyphDialog(glyph, glyph)) {
                    stagedCell.character = glyph.empty() ? " " : glyph;
                    hasStaged = true;
                    working.setActiveCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == lineFg) {
                RGBColor newColor = stagedCell.fg;
                uint8_t newAlpha = stagedCell.fgA;
                if (openColorPicker(newColor, newAlpha, newColor, newAlpha)) {
                    stagedCell.fg = newColor;
                    stagedCell.fgA = newAlpha;
                    hasStaged = true;
                    working.setActiveCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == lineBg) {
                RGBColor newColor = stagedCell.bg;
                uint8_t newAlpha = stagedCell.bgA;
                if (openColorPicker(newColor, newAlpha, newColor, newAlpha)) {
                    stagedCell.bg = newColor;
                    stagedCell.bgA = newAlpha;
                    hasStaged = true;
                    working.setActiveCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == btnY) {
                if (mx >= okX && mx < okX + static_cast<int>(okLabel.size())) {
                    // Confirm
                    if (hasStaged) {
                        working.setActiveCell(selX, selY, stagedCell);
                        originalCell = stagedCell;
                    }
                    hasSelection = false;
                    hasStaged = false;
                } else if (mx >= cancelX && mx < cancelX + static_cast<int>(cancelLabel.size())) {
                    // Cancel -> revert staged and working to original
                    stagedCell = originalCell;
                    working.setActiveCell(selX, selY, originalCell);
                    hasSelection = false;
                    hasStaged = false;
                }
                return;
            }
        }
    }

    // Scrollbar interactions
    int barY = canvasY + canvasH - 1;
    int trackX = canvasX + 1;
    int trackW = viewW;
    int barX = canvasX + canvasW - 1;
    int trackY = canvasY + 1;
    int trackH = viewH;

    if (ev.move) {
        if (showH && my == barY && mx >= trackX && mx < trackX + trackW) {
            int thumbW = std::max(2, trackW * viewW / contentW);
            thumbW = std::min(thumbW, trackW);
            int thumbX = trackX + (trackW - thumbW) * scrollX / std::max(1, contentW - viewW);
            hoverHThumb = (mx >= thumbX && mx < thumbX + thumbW);
        }
        if (showV && mx == barX && my >= trackY && my < trackY + trackH) {
            int thumbH = std::max(2, trackH * viewH / contentH);
            thumbH = std::min(thumbH, trackH);
            int thumbY = trackY + (trackH - thumbH) * scrollY / std::max(1, contentH - viewH);
            hoverVThumb = (my >= thumbY && my < thumbY + thumbH);
        }
    }

    if (ev.button == 0 && ev.pressed) {
        // Horizontal bar
        if (showH && my == barY && mx >= trackX && mx < trackX + trackW) {
            int thumbW = std::max(2, trackW * viewW / contentW);
            thumbW = std::min(thumbW, trackW);
            int thumbX = trackX + (trackW - thumbW) * scrollX / std::max(1, contentW - viewW);
            if (mx >= thumbX && mx < thumbX + thumbW) {
                draggingHThumb = true;
                dragThumbStartX = mx;
                dragThumbStartOffsetX = thumbX - trackX;
            } else {
                int trackSpan = std::max(1, trackW - thumbW);
                int pos = std::clamp(mx - trackX - thumbW / 2, 0, trackSpan);
                scrollX = pos * std::max(1, contentW - viewW) / trackSpan;
                clampScroll();
            }
        }
        // Vertical bar
        if (showV && mx == barX && my >= trackY && my < trackY + trackH) {
            int thumbH = std::max(2, trackH * viewH / contentH);
            thumbH = std::min(thumbH, trackH);
            int thumbY = trackY + (trackH - thumbH) * scrollY / std::max(1, contentH - viewH);
            if (my >= thumbY && my < thumbY + thumbH) {
                draggingVThumb = true;
                dragThumbStartY = my;
                dragThumbStartOffsetY = thumbY - trackY;
            } else {
                int trackSpan = std::max(1, trackH - thumbH);
                int pos = std::clamp(my - trackY - thumbH / 2, 0, trackSpan);
                scrollY = pos * std::max(1, contentH - viewH) / trackSpan;
                clampScroll();
            }
        }
    }
}

void YuiEditorScreen::handleKey(const InputEvent& ev, bool& running) {
    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
        running = false;
        return;
    }

    if (ev.key == InputKey::Character && ev.ch == ' ') {
        activeTool = (activeTool == Tool::Hand) ? Tool::Property : Tool::Hand;
        return;
    }

    if (activeTool == Tool::Property) {
        bool moved = false;
        if (!hasSelection) {
            int viewW = canvasW - 2;
            int viewH = canvasH - 2;
            selX = std::clamp(scrollX + viewW / 2, 0, working.getWidth() - 1);
            selY = std::clamp(scrollY + viewH / 2, 0, working.getHeight() - 1);
            hasSelection = true;
            moved = true;
        }

        if (ev.key == InputKey::ArrowUp) { selY--; moved = true; }
        else if (ev.key == InputKey::ArrowDown) { selY++; moved = true; }
        else if (ev.key == InputKey::ArrowLeft) { selX--; moved = true; }
        else if (ev.key == InputKey::ArrowRight) { selX++; moved = true; }

        if (moved) {
            selX = std::clamp(selX, 0, working.getWidth() - 1);
            selY = std::clamp(selY, 0, working.getHeight() - 1);
            
            // Update staged cell
            originalCell = working.getActiveCell(selX, selY);
            stagedCell = originalCell;
            hasStaged = true;

            // Sync hover for the "highlight" effect requested
            hoverX = selX;
            hoverY = selY;
            hoverValid = true;

            // Ensure visible
            int viewW = canvasW - 2;
            int viewH = canvasH - 2;
            if (selX < scrollX) scrollX = selX;
            else if (selX >= scrollX + viewW) scrollX = selX - viewW + 1;
            if (selY < scrollY) scrollY = selY;
            else if (selY >= scrollY + viewH) scrollY = selY - viewH + 1;
            clampScroll();
        }
    } else {
        if (ev.key == InputKey::ArrowUp) { scrollY -= 2; clampScroll(); }
        if (ev.key == InputKey::ArrowDown) { scrollY += 2; clampScroll(); }
        if (ev.key == InputKey::ArrowLeft) { scrollX -= 2; clampScroll(); }
        if (ev.key == InputKey::ArrowRight) { scrollX += 2; clampScroll(); }
    }
}

void YuiEditorScreen::clampScroll() {
    int viewW = canvasW - 2;
    int viewH = canvasH - 2;
    int maxX = std::max(0, working.getWidth() - viewW);
    int maxY = std::max(0, working.getHeight() - viewH);
    scrollX = std::clamp(scrollX, 0, maxX);
    scrollY = std::clamp(scrollY, 0, maxY);
}

bool YuiEditorScreen::isInsideCanvas(int x, int y) const {
    return x >= canvasX + 1 && x < canvasX + canvasW - 1 && y >= canvasY + 1 && y < canvasY + canvasH - 1;
}

bool YuiEditorScreen::openColorPicker(RGBColor initial, uint8_t initialA, RGBColor& outColor, uint8_t& outA) {
    double h=0, s=0, v=0;
    TuiUtils::rgbToHsv(initial, h, s, v);
    uint8_t currentA = initialA;
    bool running = true;
    bool accepted = false;
    const int svW = 64;
    const int svH = 28;
    const int boxW = svW + 16 + 6;
    const int boxH = svH + 7;
    int dx = (surface.getWidth() - boxW) / 2;
    int dy = (surface.getHeight() - boxH) / 2;

    enum class ColorDragMode { None, Window, SV, Hue, Red, Green, Blue, Alpha };
    ColorDragMode dragMode = ColorDragMode::None;

    auto clampDialog = [&]() {
        int sw = surface.getWidth();
        int sh = surface.getHeight();
        dx = std::clamp(dx, 0, std::max(0, sw - boxW));
        dy = std::clamp(dy, 0, std::max(0, sh - boxH));
    };
    clampDialog();

    bool dragging = false; // Legacy window dragging
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;

    const std::string blocks[] = {" ","▂","▃","▄","▅","▆","▇","█"};

    while (running) {
        EnvConfig::getInstance().refresh();
        auto runtime = EnvConfig::getInstance().getRuntimeInfo();
        double preciseX = runtime.mouseCellWin.x - 1.0;
        double preciseY = runtime.mouseCellWin.y - 1.0;

        clampDialog();
        renderFrame();
        surface.drawFrame(dx, dy, boxW, boxH, kFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, boxW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, "HSV Picker", theme.title, theme.background);

        // Cancel button in title bar
        bool isHoverCancel = (preciseY >= dy + 1 && preciseY < dy + 2 && preciseX >= dx + boxW - 10 && preciseX < dx + boxW - 2);
        RGBColor cancelFg = isHoverCancel ? RGBColor{255, 255, 255} : theme.title;
        RGBColor cancelBg = isHoverCancel ? RGBColor{200, 50, 50} : theme.background;
        surface.drawText(dx + boxW - 10, dy + 1, "[CANCEL]", cancelFg, cancelBg);

        int svX = dx + 2;
        int svY = dy + 3;
        // Draw SV plane for current hue
        for (int py = 0; py < svH; ++py) {
            double vv = 1.0 - static_cast<double>(py) / std::max(1, svH - 1);
            for (int px = 0; px < svW; ++px) {
                double ss = static_cast<double>(px) / std::max(1, svW - 1);
                RGBColor c = TuiUtils::hsvToRgb(h, ss, vv);
                surface.drawText(svX + px, svY + py, " ", c, c);
            }
        }

        // Marker on SV plane
        int markX = svX + static_cast<int>(s * (svW - 1));
        int markY = svY + static_cast<int>((1.0 - v) * (svH - 1));
        surface.drawText(markX, markY, "+", {0,0,0}, {255,255,255});

        // Hue bar
        int hueX = svX + svW + 2;
        int hueW = 4;
        for (int py = 0; py < svH; ++py) {
            double hh = 360.0 * static_cast<double>(py) / std::max(1, svH - 1);
            RGBColor c = TuiUtils::hsvToRgb(hh, 1.0, 1.0);
            surface.fillRect(hueX, svY + py, hueW, 1, c, c, " ");
        }
        int hueMarkY = svY + static_cast<int>(h / 360.0 * (svH - 1));
        RGBColor curHueColor = TuiUtils::hsvToRgb(h, 1.0, 1.0);
        surface.drawText(hueX, hueMarkY, " << ", {255,255,255}, curHueColor);

        // RGB Bars
        RGBColor currentRGB = TuiUtils::hsvToRgb(h, s, v);
        int rX = hueX + hueW + 1;
        int gX = rX + 3;
        int bX = gX + 3;
        int aX = bX + 3;

        auto drawComponentBar = [&](int x, uint8_t val, RGBColor fg, RGBColor bg) {
            double totalLevel = (val / 255.0) * (svH * 8.0);
            int fullCells = static_cast<int>(totalLevel) / 8;
            int partialLevel = static_cast<int>(totalLevel) % 8;

            for (int py = 0; py < svH; ++py) {
                int iy = (svH - 1) - py;
                if (iy < fullCells) {
                    surface.fillRect(x, svY + py, 2, 1, fg, fg, "█");
                } else if (iy == fullCells && partialLevel > 0) {
                    surface.fillRect(x, svY + py, 2, 1, fg, bg, blocks[partialLevel - 1]);
                } else {
                    surface.fillRect(x, svY + py, 2, 1, bg, bg, " ");
                }
            }
            // Value marker/text could be added here if needed
        };

        drawComponentBar(rX, currentRGB.r, {255, 60, 60}, {60, 0, 0});
        drawComponentBar(gX, currentRGB.g, {60, 255, 60}, {0, 60, 0});
        drawComponentBar(bX, currentRGB.b, {60, 60, 255}, {0, 0, 60});
        drawComponentBar(aX, currentA, {220, 220, 220}, {40, 40, 40});

        RGBColor preview = currentRGB;
        int previewY = dy + svH + 4;
        surface.drawText(dx + 2, previewY, "Preview", theme.itemFg, theme.panel);
        int swatchX = dx + 12;
        int swatchW = std::max(0, boxW - (swatchX - dx) - 2 - 1);
        surface.fillRect(swatchX, previewY, swatchW, 1, preview, preview, " ");
        
        // Dynamic binarization for text color and combined info string 
        RGBColor textColor = (preview.r * 299 + preview.g * 587 + preview.b * 114 > 128000) 
                             ? RGBColor{0, 0, 0} : RGBColor{255, 255, 255};
        std::ostringstream infoss;
         infoss << " RGB: " << (int)preview.r << "," << (int)preview.g << "," << (int)preview.b
             << " A: " << (int)currentA
               << "  HSV: " << (int)std::round(h) << "° " 
               << (int)std::round(s * 100) << "% " 
               << (int)std::round(v * 100) << "%";
        surface.drawCenteredText(swatchX, previewY, swatchW, infoss.str(), textColor, preview);

        surface.drawText(dx + 2, dy + svH + 5, "Click/Drag segments: Adjust | Wheel: Hue | Enter: OK | Q: Cancel", theme.hintFg, theme.panel);

        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Enter) { accepted = true; running = false; break; }
                if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) { running = false; break; }
            } else if (ev.type == InputEvent::Type::Mouse) {
                if (ev.button == 0) {
                    if (ev.pressed) {
                        bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + boxW - 1);
                        if (ev.y == dy + 1 && ev.x >= dx + boxW - 10 && ev.x < dx + boxW - 2) {
                            running = false; // Clicked Cancel
                            break;
                        } else if (onTitle) {
                            dragMode = ColorDragMode::Window;
                            dragStartX = ev.x; dragStartY = ev.y;
                            dragOriginX = dx; dragOriginY = dy;
                        } else if (ev.x >= svX && ev.x < svX + svW && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::SV;
                        } else if (ev.x >= hueX && ev.x < hueX + hueW && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::Hue;
                        } else if (ev.x >= rX && ev.x < rX + 2 && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::Red;
                        } else if (ev.x >= gX && ev.x < gX + 2 && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::Green;
                        } else if (ev.x >= bX && ev.x < bX + 2 && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::Blue;
                        } else if (ev.x >= aX && ev.x < aX + 2 && ev.y >= svY && ev.y < svY + svH) {
                            dragMode = ColorDragMode::Alpha;
                        }
                    } else if (!ev.move) {
                        dragMode = ColorDragMode::None;
                    }
                }

                if (dragMode == ColorDragMode::Window && ev.move) {
                    dx = dragOriginX + (ev.x - dragStartX);
                    dy = dragOriginY + (ev.y - dragStartY);
                }

                if (ev.wheel != 0) {
                    h = std::fmod(h + ev.wheel * 6.0 + 360.0, 360.0);
                }
            }
        }

        // Apply continuous high-precision updates while dragging
        if (dragMode != ColorDragMode::None && dragMode != ColorDragMode::Window) {
            if (dragMode == ColorDragMode::SV) {
                s = std::clamp(preciseX - svX, 0.0, static_cast<double>(svW) - 1.0) / std::max(1, svW - 1);
                v = 1.0 - std::clamp(preciseY - svY, 0.0, static_cast<double>(svH) - 1.0) / std::max(1, svH - 1);
            } else if (dragMode == ColorDragMode::Hue) {
                h = 360.0 * std::clamp(preciseY - svY, 0.0, static_cast<double>(svH) - 1.0) / std::max(1, svH - 1);
            } else if (dragMode == ColorDragMode::Alpha) {
                double factor = 1.0 - std::clamp(preciseY - svY, 0.0, static_cast<double>(svH)) / static_cast<double>(svH);
                currentA = static_cast<uint8_t>(std::clamp(factor * 255.0, 0.0, 255.0));
            } else {
                // RGB Adjust - Use continuous range for better precision with 8-level blocks
                double factor = 1.0 - std::clamp(preciseY - svY, 0.0, static_cast<double>(svH)) / static_cast<double>(svH);
                uint8_t newVal = static_cast<uint8_t>(std::clamp(factor * 255.0, 0.0, 255.0));
                RGBColor rgb = TuiUtils::hsvToRgb(h, s, v);
                if (dragMode == ColorDragMode::Red) rgb.r = newVal;
                else if (dragMode == ColorDragMode::Green) rgb.g = newVal;
                else if (dragMode == ColorDragMode::Blue) rgb.b = newVal;
                TuiUtils::rgbToHsv(rgb, h, s, v);
            }
        }

        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if (accepted) {
        outColor = TuiUtils::hsvToRgb(h, s, v);
        outA = currentA;
    }
    return accepted;
}

bool YuiEditorScreen::openGlyphDialog(const std::string& initial, std::string& outGlyph) {
    std::string glyph = initial.empty() ? " " : initial;
    bool running = true;
    bool accepted = false;
    const std::vector<std::string> presets = {
        // Horizontal 1/8 blocks
        "▁","▂","▃","▄","▅","▆","▇","█",
        // Vertical 1/8 blocks
        "▏","▎","▍","▌","▋","▊","▉","█",
        // Quadrant blocks (six)
        "▘","▝","▖","▗","▚","▞"
    };
    const int cols = 8;
    const int cellW = 3;
    const int slotW = cellW + 1;
    int rows = static_cast<int>((presets.size() + cols - 1) / cols);
    int gridW = cols * slotW - 1;
    int boxW = std::max(48, gridW + 4);
    int boxH = 8 + rows; // title + presets + custom + hint
    int dx = (surface.getWidth() - boxW) / 2;
    int dy = (surface.getHeight() - boxH) / 2;
    auto clampDialog = [&]() {
        int sw = surface.getWidth();
        int sh = surface.getHeight();
        dx = std::clamp(dx, 0, std::max(0, sw - boxW));
        dy = std::clamp(dy, 0, std::max(0, sh - boxH));
    };
    clampDialog();
    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;
    while (running) {
        clampDialog();
        renderFrame();
        surface.drawFrame(dx, dy, boxW, boxH, kFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, boxW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, "Edit Glyph", theme.title, theme.background);
        surface.drawText(dx + 2, dy + 3, "Presets (click to select):", theme.itemFg, theme.panel);

        int gx = dx + 2;
        int gy = dy + 4;
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            int cx = gx + (i % cols) * slotW;
            int cy = gy + (i / cols);
            bool isCur = (glyph == presets[i]);
            RGBColor fg = isCur ? theme.background : theme.itemFg;
            RGBColor bg = isCur ? theme.title : theme.panel;
            surface.drawText(cx, cy, " " + presets[i] + " ", fg, bg);
        }

        int customY = gy + rows + 1;
        surface.drawText(dx + 2, customY, "Custom: [" + glyph + "]", theme.itemFg, theme.panel);
        surface.drawText(dx + 2, customY + 1, "Enter: OK | Esc/Q: cancel", theme.hintFg, theme.panel);

        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Escape) { running = false; break; }
                if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) { running = false; break; }
                if (ev.key == InputKey::Enter) { accepted = true; running = false; break; }
                if (ev.key == InputKey::Character) {
                    if (ev.ch == '\b') {
                        glyph = " ";
                    } else {
                        std::string enc = TuiUtils::encodeUtf8(ev.ch);
                        if (!enc.empty()) glyph = enc;
                    }
                }
            } else if (ev.type == InputEvent::Type::Mouse) {
                if (dragging) {
                    dx = dragOriginX + (ev.x - dragStartX);
                    dy = dragOriginY + (ev.y - dragStartY);
                    clampDialog();
                }
                bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + boxW - 1);
                if (ev.button == 0 && ev.pressed) {
                    if (onTitle) {
                        dragging = true;
                        dragStartX = ev.x;
                        dragStartY = ev.y;
                        dragOriginX = dx;
                        dragOriginY = dy;
                    }
                    int gx0 = dx + 2;
                    int gy0 = dy + 4;
                    int gridH = rows;
                    if (ev.x >= gx0 && ev.x < gx0 + gridW && ev.y >= gy0 && ev.y < gy0 + gridH) {
                        int col = (ev.x - gx0) / slotW;
                        int row = (ev.y - gy0);
                        if (col >= 0 && col < cols && row >= 0 && row < rows) {
                            int idx = row * cols + col;
                            if (idx >= 0 && idx < static_cast<int>(presets.size())) {
                                glyph = presets[static_cast<size_t>(idx)];
                            }
                        }
                    }
                }
                if (ev.button == 0 && !ev.pressed && !ev.move) {
                    dragging = false;
                }
            }
        }
    }
    if (accepted) {
        outGlyph = glyph;
    }
    return accepted;
}

bool YuiEditorScreen::openRenameDialog(const std::string& initial, std::string& outName) {
    std::string name = initial;
    bool running = true;
    bool accepted = false;
    int boxW = 50;
    int boxH = 9;
    int dx = (surface.getWidth() - boxW) / 2;
    int dy = (surface.getHeight() - boxH) / 2;
    
    TextFieldState inputState;
    inputState.focused = true;
    inputState.caretIndex = (int)name.size();
    inputState.mode = CursorMode::IBeam;

    auto clampDialog = [&]() {
        dx = std::clamp(dx, 0, std::max(0, surface.getWidth() - boxW));
        dy = std::clamp(dy, 0, std::max(0, surface.getHeight() - boxH));
    };

    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;
    bool hoverOk = false, hoverCancel = false;

    while (running) {
        inputState.updateCaret();
        clampDialog();
        renderFrame();
        
        surface.drawFrame(dx, dy, boxW, boxH, kFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, boxW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, "Rename Layer", theme.title, theme.background);
        
        surface.drawText(dx + 2, dy + 3, "New Name:", theme.itemFg, theme.panel);
        
        TextFieldStyle fieldStyle;
        fieldStyle.width = boxW - 4;
        fieldStyle.focusBg = theme.focusBg;
        fieldStyle.focusFg = theme.focusFg;
        fieldStyle.panelBg = theme.panel;
        fieldStyle.caretChar = '|';
        TextField::render(surface, dx + 2, dy + 4, name, inputState, fieldStyle);
        
        std::string okLbl = "[ OK ]";
        std::string cancelLbl = "[ Cancel ]";
        int okX = dx + (boxW / 2) - static_cast<int>(okLbl.size()) - 1;
        int cancelX = dx + (boxW / 2) + 1;
        int btnY = dy + boxH - 2;

        auto drawBtn = [&](const std::string& lbl, int x, bool hot) {
            RGBColor bg = hot ? darken(theme.accent, 0.8) : theme.accent;
            surface.drawText(x, btnY, lbl, theme.title, bg);
        };
        drawBtn(okLbl, okX, hoverOk);
        drawBtn(cancelLbl, cancelX, hoverCancel);

        surface.drawText(dx + 2, btnY - 1, "Enter: OK | Esc: cancel", theme.hintFg, theme.panel);

        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        for (const auto& ev : events) {
            if (TextField::handleInput(ev, name, inputState, fieldStyle)) {
                continue;
            }

            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Escape) { running = false; break; }
                if (ev.key == InputKey::Enter) {
                    if (!name.empty()) {
                        accepted = true; 
                        running = false; 
                    }
                    break; 
                }
            } else if (ev.type == InputEvent::Type::Mouse) {
                if (dragging) {
                    dx = dragOriginX + (ev.x - dragStartX);
                    dy = dragOriginY + (ev.y - dragStartY);
                    clampDialog();
                }
                
                hoverOk = (ev.y == btnY && ev.x >= okX && ev.x < okX + (int)okLbl.size());
                hoverCancel = (ev.y == btnY && ev.x >= cancelX && ev.x < cancelX + (int)cancelLbl.size());

                bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + boxW - 1);
                if (ev.button == 0 && ev.pressed) {
                    if (onTitle) {
                        dragging = true;
                        dragStartX = ev.x;
                        dragStartY = ev.y;
                        dragOriginX = dx;
                        dragOriginY = dy;
                    }
                    if (hoverOk) {
                        if (!name.empty()) {
                            accepted = true;
                            running = false;
                        }
                    }
                    if (hoverCancel) {
                        running = false;
                    }
                }
                if (ev.button == 0 && !ev.pressed && !ev.move) {
                    dragging = false;
                }
            }
        }
    }
    if (accepted) {
        outName = name;
    }
    return accepted;
}

} // namespace UI
} // namespace TilelandWorld
