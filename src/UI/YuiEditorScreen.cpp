#include "YuiEditorScreen.h"
#include "YuiUtils.h"
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

namespace TilelandWorld {
namespace UI {

YuiEditorScreen::YuiEditorScreen(AssetManager& manager_, std::string assetName_, YuiLayeredImage asset_)
    : manager(manager_), assetName(std::move(assetName_)), working(std::move(asset_)), surface(100, 40) {
    pendingOpacity = working.getLayer(working.getActiveLayerIndex()).getOpacity();
    opacityText = std::to_string(static_cast<int>(pendingOpacity * 100));
}

void YuiEditorScreen::show() {
    input.setRestoreOnExit(false); // keep VT/mouse mode intact for caller
    input.start();
    bool running = true;
    while (running) {
        EnvConfig::getInstance().refresh();
        if (dragLayerOpacity) {
            auto runtime = EnvConfig::getInstance().getRuntimeInfo();
            double preciseX = runtime.mouseCellWin.x - 1.0;
            int barX = layerPanelX + 2;
            int barW = std::max(1, layerPanelW - 4);
            double t = (barW > 0) ? (preciseX - barX) / barW : 0.0;
            pendingOpacity = std::clamp(t, 0.0, 1.0);
            if (!opacityInputState.focused) {
                opacityText = std::to_string(static_cast<int>(std::round(pendingOpacity * 100.0)));
            }
        }
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
                std::vector<std::string> opts;
                if (layerMenuIdx == -1) {
                    opts = {"New Layer", "Import Layer"};
                } else {
                    opts = {"Move Up", "Move Down", "Top", "Bottom", "Rename", "Delete"};
                }
                int sel = ContextMenu::handleInput(ev, opts, layerMenuState, close);
                if (sel != -1) {
                    if (layerMenuIdx == -1) {
                        if (sel == 0) { // New Layer
                             int idx = static_cast<int>(working.getLayerCount()) + 1;
                             YuiLayer layer(working.getWidth(), working.getHeight(), "Layer " + std::to_string(idx));
                             working.addLayer(layer);
                        } else if (sel == 1) { // Import Layer
                             input.stop();
                             DirectoryBrowserScreen browser(manager.getRootDir(), true, ".tlimg");
                             auto paths = browser.show();
                             if (!paths.empty()) {
                                 for (const auto& path : paths) {
                                     YuiLayeredImage imported = YuiLayeredImage::load(path);
                                     for (size_t i = 0; i < imported.getLayerCount(); ++i) {
                                         const auto& srcLayer = imported.getLayer(i);
                                         YuiLayer newLayer(working.getWidth(), working.getHeight(), srcLayer.getName());
                                         newLayer.setOpacity(srcLayer.getOpacity());
                                         newLayer.setVisible(srcLayer.isVisible());

                                         int maxW = std::min(srcLayer.getWidth(), working.getWidth());
                                         int maxH = std::min(srcLayer.getHeight(), working.getHeight());
                                         for (int yy = 0; yy < maxH; ++yy) {
                                             for (int xx = 0; xx < maxW; ++xx) {
                                                 newLayer.setCell(xx, yy, srcLayer.getCell(xx, yy));
                                             }
                                         }
                                         working.addLayer(newLayer);
                                     }
                                 }
                             }
                             input.start();
                        }
                    } else {
                        if (sel == 0) { // Move Up
                            if (layerMenuIdx < (int)working.getLayerCount() - 1)
                                working.moveLayer(layerMenuIdx, layerMenuIdx + 1);
                        } else if (sel == 1) { // Move Down
                            if (layerMenuIdx > 0)
                                working.moveLayer(layerMenuIdx, layerMenuIdx - 1);
                        } else if (sel == 2) { // Top
                            working.moveLayer(layerMenuIdx, (int)working.getLayerCount() - 1);
                        } else if (sel == 3) { // Bottom
                            working.moveLayer(layerMenuIdx, 0);
                        } else if (sel == 4) { // Rename
                            std::string newName;
                            showLayerMenu = false;
                            if (openRenameDialog(working.getLayer(layerMenuIdx).getName(), newName)) {
                                working.getLayer(layerMenuIdx).setName(newName);
                            }
                        } else if (sel == 5) { // Delete
                            showLayerMenu = false;
                            if (working.getLayerCount() > 1) {
                                working.removeLayer(layerMenuIdx);
                            }
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
    manager.saveLayeredAsset(working, assetName, scrollX, scrollY, std::max(0, canvasW - 2), std::max(0, canvasH - 2));
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

    if (showLayers) {
        opacityInputState.updateCaret();
    }

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
        std::vector<std::string> opts;
        if (layerMenuIdx == -1) {
            opts = {"New Layer", "Import Layer"};
        } else {
            opts = {"Move Up", "Move Down", "Top", "Bottom", "Rename", "Delete"};
        }
        ContextMenu::render(surface, opts, layerMenuState);
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
        RGBColor bg = active ? YuiUtils::darken(theme.accent, 0.6) : theme.accent;
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
                if (showLayers) {
                     pendingOpacity = working.getLayer(working.getActiveLayerIndex()).getOpacity();
                     opacityText = std::to_string(static_cast<int>(pendingOpacity * 100));
                }
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
    if (opacityInputState.focused) {
        if (ev.key == InputKey::Enter || ev.key == InputKey::Escape) {
            opacityInputState.focused = false;
            try {
                int val = std::stoi(opacityText);
                double opacity = std::clamp(val / 100.0, 0.0, 1.0);
                working.setLayerOpacity(working.getActiveLayerIndex(), opacity);
                pendingOpacity = opacity;
            } catch (...) {}
            return;
        }
        TextFieldStyle opStyle;
        opStyle.charFilter = [](char32_t c) { return c >= '0' && c <= '9'; };
        TextField::handleInput(ev, opacityText, opacityInputState, opStyle);
        // Live update pendingOpacity if possible
        try {
            int val = std::stoi(opacityText);
            pendingOpacity = std::clamp(val / 100.0, 0.0, 1.0);
        } catch (...) {}
        return;
    }

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

} // namespace UI
} // namespace TilelandWorld
