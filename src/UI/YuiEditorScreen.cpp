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
    drawMenubar();

    propPanelW = hasSelection ? 28 : 0;
    layerPanelW = showLayers ? 28 : 0;
    int rightPanels = 0;
    if (propPanelW > 0) rightPanels += propPanelW;
    if (layerPanelW > 0) rightPanels += layerPanelW;
    if (propPanelW > 0 && layerPanelW > 0) rightPanels += 1; // gap

    canvasX = 4;
    canvasY = 4;
    canvasW = std::max(10, surface.getWidth() - canvasX - 2 - rightPanels);
    canvasH = std::max(6, surface.getHeight() - canvasY - 2);

    if (layerPanelW > 0) {
        layerPanelX = surface.getWidth() - layerPanelW - 2;
    }
    if (propPanelW > 0) {
        propPanelX = (layerPanelW > 0) ? (layerPanelX - 1 - propPanelW) : (surface.getWidth() - propPanelW - 2);
    }

    drawSideToolbar();
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

    if (canvasMenuState.visible) {
        ContextMenu::render(surface, canvasMenuItems, canvasMenuState);
    }

    surface.drawCenteredText(0, surface.getHeight() - 2, surface.getWidth(), 
        "Space: switch tool | Mouse wheel: scroll | Drag (hand): pan | Rect: drag select | Q: back", 
        theme.hintFg, theme.background);
}

static constexpr int kToolbarY = 3; // leave a blank line above the toolbar

void YuiEditorScreen::drawMenubar() {
    int y = kToolbarY;
    std::string fileLabel = "  File  ";
    std::string layers = showLayers ? "[ Layers ]" : "  Layers  ";
    int x = 5;
    auto drawBtn = [&](const std::string& label, bool active) {
        RGBColor bg = active ? YuiUtils::darken(theme.accent, 0.6) : theme.accent;
        RGBColor fg = active ? RGBColor{255,255,255} : theme.title;
        surface.drawText(x, y, label, fg, bg);
        x += static_cast<int>(label.size()) + 2;
    };
    drawBtn(fileLabel, false);
    drawBtn(layers, showLayers);
}

void YuiEditorScreen::drawSideToolbar() {
    int tbX = 1;
    int tbW = 2;
    int tbY = canvasY + 1;
    int tbH = canvasH - 1;
    
    // Draw background for the sidebar
    surface.fillRect(tbX, tbY, tbW, tbH, theme.panel, theme.panel, " ");
    
    auto drawTool = [&](int y, const std::string& icon, Tool tool) {
        bool active = activeMenu == tool;
        RGBColor bg = active ? YuiUtils::darken(theme.accent, 0.6) : theme.panel;
        RGBColor fg = active ? RGBColor{255,255,255} : theme.itemFg;
        surface.fillRect(tbX, y, tbW, 1, bg, bg, " ");
        surface.drawText(tbX, y, icon, fg, bg);
    };

    drawTool(tbY, "👆", Tool::Hand);
    drawTool(tbY + 1, "\xEF\x84\xA9\xEF\x84\xA9", Tool::Property);
    drawTool(tbY + 2, "\xEF\x80\x89\xEF\x80\x89", Tool::RectSelect); // f009 (Rectangle)
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
            if (ax < 0 || ax >= working.getWidth() || ay < 0 || ay >= working.getHeight()) continue;

            ImageCell cell = working.compositeCell(ax, ay);
            
            // If movement is active or confirmed, we overlay selectionBuffer over this cell
            if (hasRectSelection && rectSelectionConfirmed) {
                int x1 = rectSelStartX, y1 = rectSelStartY, x2 = rectSelEndX, y2 = rectSelEndY;
                if (x1 > x2) std::swap(x1, x2);
                if (y1 > y2) std::swap(y1, y2);
                if (ax >= x1 && ax <= x2 && ay >= y1 && ay <= y2) {
                    int bx = ax - x1;
                    int by = ay - y1;
                    if (bx >= 0 && bx < selectionBufW && by >= 0 && by < selectionBufH) {
                        cell = selectionBuffer[by * selectionBufW + bx];
                    }
                }
            }

            glyph = cell.character.empty() ? " " : cell.character;
            fg = cell.fg;
            bg = (cell.bgA == 0) ? theme.panel : cell.bg;
            if (hoverValid && ax == hoverX && ay == hoverY) {
                if (!movingSelection && (!rectSelectionConfirmed || activeMenu != Tool::Hand)) {
                    fg = TuiUtils::blendColor(fg, {255,255,255}, 0.2);
                    bg = TuiUtils::blendColor(bg, {255,255,255}, 0.2);
                }
            }
            if (hasSelection && ax == selX && ay == selY) {
                bg = TuiUtils::blendColor(bg, theme.focusBg, 0.35);
            }
            surface.drawText(canvasX + 1 + vx, canvasY + 1 + vy, glyph, fg, bg);
        }
    }

    if (hasRectSelection) {
        int x1 = rectSelStartX, y1 = rectSelStartY;
        int x2 = rectSelEndX, y2 = rectSelEndY;
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);

        auto drawBorderCell = [&](int ax, int ay, const std::string& drawGlyph, const std::string& maskGlyph, bool swap, int interval = 1) {
            if (ax < 0 || ax >= working.getWidth() || ay < 0 || ay >= working.getHeight()) return;
            int vx = ax - scrollX;
            int vy = ay - scrollY;
            if (vx < 0 || vx >= viewW || vy < 0 || vy >= viewH) return;
            
            ImageCell cell = working.compositeCell(ax, ay);
            RGBColor bgBase = getPerspectiveColor(cell, maskGlyph);
            
            // Animated dashed effect (marching ants)
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            bool isAlt = (((ax / interval) + ay + (rectSelectionConfirmed ? 0 : (ms / 300))) % 2 == 0);
            RGBColor lineCol = isAlt ? RGBColor{255, 255, 255} : RGBColor{60, 60, 60};
            
            if (swap) {
                // For characters like ▇ (7/8 lower), we set background as the line color 
                // and foreground as the perspective color to produce an inverted top-strip effect.
                surface.drawText(canvasX + 1 + vx, canvasY + 1 + vy, drawGlyph, bgBase, lineCol);
            } else {
                surface.drawText(canvasX + 1 + vx, canvasY + 1 + vy, drawGlyph, lineCol, bgBase);
            }
        };

        // Top/Bottom edges: using 1/8 blocks
        for (int x = x1; x <= x2; x++) {
            drawBorderCell(x, y1 - 1, "▁", "▄", false, 2); // Top edge: lower 1/8 block, interval 2
            drawBorderCell(x, y2 + 1, "▇", "▀", true, 2);  // Bottom edge: upper 1/8 (7/8 block swap), interval 2
        }
        // Left/Right edges: using 1/4 blocks
        for (int y = y1; y <= y2; y++) {
            drawBorderCell(x1 - 1, y, "▊", "▐", true);  // Left edge: right 1/4 (3/4 block swap)
            drawBorderCell(x2 + 1, y, "▎", "▌", false); // Right edge: left 1/4
        }
        // Corners: Keep 2x2 series characters
        drawBorderCell(x1 - 1, y1 - 1, "▗", "▗", false);
        drawBorderCell(x2 + 1, y1 - 1, "▖", "▖", false);
        drawBorderCell(x1 - 1, y2 + 1, "▝", "▝", false);
        drawBorderCell(x2 + 1, y2 + 1, "▘", "▘", false);
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

    if (canvasMenuState.visible) {
        bool requestClose = false;
        int choice = ContextMenu::handleInput(ev, canvasMenuItems, canvasMenuState, requestClose);
        if (choice >= 0) {
            if (canvasMenuType == 1) { // Selection Menu
                if (choice == 0) { // Copy
                    clipboardBuffer = selectionBuffer;
                    clipboardW = selectionBufW;
                    clipboardH = selectionBufH;
                    // Restore original cells if they were cut
                    if (cutOriginX >= 0) {
                        for (int i = 0; i < (int)originalRectData.size(); ++i) {
                            int tx = cutOriginX + (i % selectionBufW);
                            int ty = cutOriginY + (i / selectionBufW);
                            if (tx >= 0 && ty >= 0 && tx < working.getWidth() && ty < working.getHeight())
                                working.setActiveCell(tx, ty, originalRectData[i]);
                        }
                        cutOriginX = -1; // No longer a "Cut" selection, now a "Stamp"
                    }
                } else if (choice == 1) { // Cut
                    clipboardBuffer = selectionBuffer;
                    clipboardW = selectionBufW;
                    clipboardH = selectionBufH;
                    hasRectSelection = false;
                    rectSelectionConfirmed = false;
                    selectionBuffer.clear();
                } else if (choice == 2) { // New Layer
                    working.addLayer(YuiLayer(working.getWidth(), working.getHeight(), "New Layer " + std::to_string(working.getLayerCount() + 1)));
                    working.setActiveLayerIndex(working.getLayerCount() - 1);
                } else if (choice == 3) { // Delete
                    hasRectSelection = false;
                    rectSelectionConfirmed = false;
                    selectionBuffer.clear();
                }
            } else if (canvasMenuType == 2) { // General Menu
                if (choice == 0) { // Paste
                    if (hasRectSelection && rectSelectionConfirmed) {
                        // Restore previous selection if it was a Cut
                        if (cutOriginX >= 0) {
                            for (int i = 0; i < (int)originalRectData.size(); ++i) {
                                int tx = cutOriginX + (i % selectionBufW);
                                int ty = cutOriginY + (i / selectionBufW);
                                if (tx >= 0 && ty >= 0 && tx < working.getWidth() && ty < working.getHeight())
                                    working.setActiveCell(tx, ty, originalRectData[i]);
                            }
                        }
                    }
                    hasRectSelection = false;
                    rectSelectionConfirmed = false;
                    selectionBuffer.clear();

                    selectionBuffer = clipboardBuffer;
                    selectionBufW = clipboardW;
                    selectionBufH = clipboardH;
                    rectSelStartX = canvasMenuAX;
                    rectSelStartY = canvasMenuAY;
                    rectSelEndX = rectSelStartX + selectionBufW - 1;
                    rectSelEndY = rectSelStartY + selectionBufH - 1;
                    
                    // Paste starts as a Stamp (no Cut origin)
                    cutOriginX = -1;
                    originalRectData.clear();

                    hasRectSelection = true;
                    rectSelectionConfirmed = true;
                    activeMenu = Tool::Hand;
                    movingSelection = false;
                }
            }
        }
        if (requestClose) canvasMenuState.visible = false;
        if (ev.pressed) return;
        if (ev.move && canvasMenuState.visible) return;
    }

    hoverHThumb = false;
    hoverVThumb = false;
    hoverConfirm = false;
    hoverCancel = false;
    hoverLayerUp = false;
    hoverLayerDown = false;
    hoverLayerAdd = false;
    hoverLayerImport = false;

    // Side toolbar hit-test
    if (mx >= 1 && mx <= 2 && my >= 5 && my < 8) {
        if (ev.pressed && ev.button == 0) {
            int idx = my - 5;
            if (idx == 0) {
                activeMenu = Tool::Hand;
            } else if (idx == 1) {
                activeMenu = Tool::Property;
            } else if (idx == 2) {
                activeMenu = Tool::RectSelect;
            }
        }
        return;
    }

    // Toolbar button hit-test
    if (my == kToolbarY) {
        int x = 4;
        std::string fileLabel = "  File  ";
        std::string layersLabel = showLayers ? "[ Layers ]" : "  Layers  ";
        int fileStart = x;
        int fileEnd = fileStart + static_cast<int>(fileLabel.size());
        x = fileEnd + 2;
        int layersStart = x;
        int layersEnd = layersStart + static_cast<int>(layersLabel.size());

        if (ev.button == 0 && ev.pressed) {
            if (mx >= fileStart && mx < fileEnd) {
                // File menu placeholder
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
        if (movingSelection) {
            int dx = hoverX - selDragStartAX;
            int dy = hoverY - selDragStartAY;
            rectSelStartX += dx;
            rectSelEndX += dx;
            rectSelStartY += dy;
            rectSelEndY += dy;
            selDragStartAX = hoverX;
            selDragStartAY = hoverY;
        } else if (dragging && activeMenu == Tool::Hand) {
            scrollX = dragStartScrollX - (mx - dragStartX);
            scrollY = dragStartScrollY - (my - dragStartY);
            clampScroll();
        } else if (isRectSelecting) {
            rectSelEndX = hoverX;
            rectSelEndY = hoverY;
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

    if (ev.button == 2 && ev.pressed) {
        if (isInsideCanvas(mx, my)) {
            int localX = mx - (canvasX + 1);
            int localY = my - (canvasY + 1);
            int ax = scrollX + localX;
            int ay = scrollY + localY;
            canvasMenuAX = ax;
            canvasMenuAY = ay;

            bool insideSelection = false;
            if (hasRectSelection && rectSelectionConfirmed) {
                int x1 = rectSelStartX, y1 = rectSelStartY, x2 = rectSelEndX, y2 = rectSelEndY;
                if (x1 > x2) std::swap(x1, x2);
                if (y1 > y2) std::swap(y1, y2);
                if (ax >= x1 && ax <= x2 && ay >= y1 && ay <= y2) insideSelection = true;
            }

            if (insideSelection) {
                canvasMenuType = 1;
                canvasMenuItems = {"复制 (Copy)", "剪切 (Cut)", "创建新图层 (New Layer)", "删除 (Delete)"};
                canvasMenuState.visible = true;
                canvasMenuState.x = mx;
                canvasMenuState.y = my;
                canvasMenuState.selectedIndex = 0;
                canvasMenuState.width = ContextMenu::calculateWidth(canvasMenuItems);
            } else {
                canvasMenuItems.clear();
                if (!clipboardBuffer.empty()) {
                    canvasMenuItems.push_back("粘贴 (Paste)");
                }
                if (!canvasMenuItems.empty()) {
                    canvasMenuType = 2;
                    canvasMenuState.visible = true;
                    canvasMenuState.x = mx;
                    canvasMenuState.y = my;
                    canvasMenuState.selectedIndex = 0;
                    canvasMenuState.width = ContextMenu::calculateWidth(canvasMenuItems);
                }
            }
            return;
        }
    }

    if (ev.button == 0 && ev.pressed) {
        if (isInsideCanvas(mx, my)) {
            int localX = mx - (canvasX + 1);
            int localY = my - (canvasY + 1);
            int ax = scrollX + localX;
            int ay = scrollY + localY;
            if (activeMenu == Tool::Hand) {
                if (rectSelectionConfirmed && hasRectSelection) {
                    int x1 = rectSelStartX, y1 = rectSelStartY, x2 = rectSelEndX, y2 = rectSelEndY;
                    if (x1 > x2) std::swap(x1, x2);
                    if (y1 > y2) std::swap(y1, y2);
                    if (ax >= x1 && ax <= x2 && ay >= y1 && ay <= y2) {
                        movingSelection = true;
                        selDragStartAX = ax;
                        selDragStartAY = ay;
                        return;
                    }
                }
                dragging = true;
                dragStartX = mx;
                dragStartY = my;
                dragStartScrollX = scrollX;
                dragStartScrollY = scrollY;
            } else if (activeMenu == Tool::RectSelect) {
                if (hasRectSelection && !rectSelectionConfirmed && !isRectSelecting) {
                    int x1 = rectSelStartX, y1 = rectSelStartY, x2 = rectSelEndX, y2 = rectSelEndY;
                    if (x1 > x2) std::swap(x1, x2);
                    if (y1 > y2) std::swap(y1, y2);
                    if (ax >= x1 && ax <= x2 && ay >= y1 && ay <= y2) {
                        rectSelectionConfirmed = true;
                        activeMenu = Tool::Hand;
                        
                        // Normalize selection range
                        rectSelStartX = x1;
                        rectSelStartY = y1;
                        rectSelEndX = x2;
                        rectSelEndY = y2;

                        // Fill selectionBuffer once
                        selectionBufW = x2 - x1 + 1;
                        selectionBufH = y2 - y1 + 1;
                        selectionBuffer.clear();
                        selectionBuffer.reserve(selectionBufW * selectionBufH);
                        originalRectData.clear();
                        originalRectData.reserve(selectionBufW * selectionBufH);
                        cutOriginX = x1;
                        cutOriginY = y1;

                        auto& layer = working.activeLayerRef();
                        for (int ty = rectSelStartY; ty <= rectSelEndY; ++ty) {
                            for (int tx = rectSelStartX; tx <= rectSelEndX; ++tx) {
                                ImageCell cell = layer.getCell(tx, ty);
                                selectionBuffer.push_back(cell);
                                originalRectData.push_back(cell);
                                working.setActiveCell(tx, ty, {" ", {0,0,0}, {0,0,0}, 0, 0});
                            }
                        }
                        return;
                    }
                }
                isRectSelecting = true;
                rectSelStartX = ax;
                rectSelStartY = ay;
                rectSelEndX = ax;
                rectSelEndY = ay;
                hasRectSelection = true;
                rectSelectionConfirmed = false;
                selectionBuffer.clear();
            } else if (activeMenu == Tool::Property) {
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
        if (movingSelection) {
            movingSelection = false;
        }
        dragging = false;
        draggingHThumb = false;
        draggingVThumb = false;
        isRectSelecting = false;
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

    if (ev.key == InputKey::Enter) {
        if (hasRectSelection && rectSelectionConfirmed) {
            int x1 = rectSelStartX, y1 = rectSelStartY, x2 = rectSelEndX, y2 = rectSelEndY;
            if (x1 > x2) std::swap(x1, x2);
            if (y1 > y2) std::swap(y1, y2);
            
            for (int by = 0; by < selectionBufH; ++by) {
                for (int bx = 0; bx < selectionBufW; ++bx) {
                    working.setActiveCell(x1 + bx, y1 + by, selectionBuffer[by * selectionBufW + bx]);
                }
            }
            hasRectSelection = false;
            rectSelectionConfirmed = false;
            movingSelection = false;
            selectionBuffer.clear();
            return;
        } else if (hasRectSelection) {
            hasRectSelection = false;
            rectSelectionConfirmed = false;
            isRectSelecting = false;
            selectionBuffer.clear();
            return;
        }
    }

    if (ev.key == InputKey::Escape) {
        if (hasRectSelection && rectSelectionConfirmed) {
            // Revert cut logic ONLY if it was a Cut (cutOriginX >= 0)
            if (cutOriginX >= 0) {
                for (int i = 0; i < (int)originalRectData.size(); ++i) {
                    int tx = cutOriginX + (i % selectionBufW);
                    int ty = cutOriginY + (i / selectionBufW);
                    if (tx >= 0 && ty >= 0 && tx < working.getWidth() && ty < working.getHeight())
                        working.setActiveCell(tx, ty, originalRectData[i]);
                }
            }
            hasRectSelection = false;
            rectSelectionConfirmed = false;
            movingSelection = false;
            selectionBuffer.clear();
            return;
        } else if (hasRectSelection) {
            // Discard unconfirmed selection
            hasRectSelection = false;
            isRectSelecting = false;
            return;
        }
    }

    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
        running = false;
        return;
    }

    if (ev.key == InputKey::Character && ev.ch == ' ') {
        if (activeMenu == Tool::Hand) activeMenu = Tool::Property;
        else if (activeMenu == Tool::Property) activeMenu = Tool::RectSelect;
        else activeMenu = Tool::Hand;
        return;
    }

    if (activeMenu == Tool::Property) {
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
