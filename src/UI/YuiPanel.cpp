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

namespace TilelandWorld {
namespace UI {

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

    int toolbarY = y + 2;
    surface.fillRect(x + 1, toolbarY, w - 2, 1, theme.itemFg, theme.panel, " ");
    
    auto drawIcon = [&](int ix, const std::string& icon, bool hot) {
        RGBColor fg = hot ? theme.focusFg : theme.itemFg;
        RGBColor bg = hot ? theme.focusBg : theme.panel;
        surface.drawText(ix, toolbarY, icon, fg, bg);
    };

    drawIcon(x + 2, "＋", hoverLayerAdd);
    drawIcon(x + 5, "↧", hoverLayerImport);
    drawIcon(x + w - 6, "↿", hoverLayerUp);
    drawIcon(x + w - 3, "⇂", hoverLayerDown);

    int listStart = y + 4;
    int listRows = std::max(0, h - 9);
    int layerCount = static_cast<int>(working.getLayerCount());
    for (int row = 0; row < listRows && row < layerCount; ++row) {
        int layerIndex = layerCount - 1 - row;
        const auto& layer = working.getLayer(static_cast<size_t>(layerIndex));
        bool active = (layerIndex == working.getActiveLayerIndex());
        RGBColor bg = active ? theme.focusBg : theme.panel;
        RGBColor fg = active ? theme.focusFg : theme.itemFg;
        surface.fillRect(x + 1, listStart + row, w - 2, 1, fg, bg, " ");
        std::string vis = layer.isVisible() ? "◎" : "◌";
        surface.drawText(x + 2, listStart + row, vis, fg, bg);
        std::string name = TuiUtils::trimToUtf8VisualWidth(layer.getName(), static_cast<size_t>(std::max(0, w - 8)));
        surface.drawText(x + 5, listStart + row, name, fg, bg);
    }

    const auto& activeLayer = working.activeLayerRef();
    int infoY = y + h - 3;
    int barY = y + h - 2;
    
    surface.drawText(x + 2, infoY, "Opacity:", theme.itemFg, theme.panel);
    
    if (!dragLayerOpacity && !opacityInputState.focused) {
        int pct = static_cast<int>(std::round(activeLayer.getOpacity() * 100.0));
        opacityText = std::to_string(pct);
    }
    
    TextFieldStyle opStyle;
    opStyle.width = 6;
    opStyle.panelBg = theme.background;
    opStyle.focusBg = theme.focusBg;
    opStyle.focusFg = theme.focusFg;
    TextField::render(surface, x + 11, infoY, opacityText, opacityInputState, opStyle);
    surface.drawText(x + 17, infoY, "%", theme.itemFg, theme.panel);

    int barX = x + 2;
    int barW = std::max(1, w - 4);
    double displayOpacity = dragLayerOpacity ? pendingOpacity : activeLayer.getOpacity();
    
    RGBColor bgColor{40, 40, 40};
    RGBColor fgColor{220, 220, 220};
    surface.fillRect(barX, barY, barW, 1, bgColor, bgColor, " ");

    const std::string hBlocks[] = {"▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    double totalLevel = displayOpacity * (barW * 8.0);
    int fullCells = static_cast<int>(totalLevel) / 8;
    int partialLevel = static_cast<int>(totalLevel) % 8;

    for (int bx = 0; bx < barW; ++bx) {
        if (bx < fullCells) {
            surface.drawText(barX + bx, barY, "█", fgColor, bgColor);
        } else if (bx == fullCells && partialLevel > 0) {
            surface.drawText(barX + bx, barY, hBlocks[partialLevel - 1], fgColor, bgColor);
        }
    }
}

bool YuiEditorScreen::handleLayerPanelMouse(const InputEvent& ev) {
    if (!showLayers) return false;
    int x = layerPanelX;
    int y = canvasY;
    int w = layerPanelW;
    int h = canvasH;

    if (ev.button == 0 && !ev.pressed && !ev.move) {
        if (dragLayerOpacity) {
            working.setLayerOpacity(working.getActiveLayerIndex(), pendingOpacity);
            dragLayerOpacity = false;
        }
    }

    if (ev.x < x || ev.x >= x + w || ev.y < y || ev.y >= y + h) {
        return false;
    }

    int toolbarY = y + 2;
    int addX = x + 2;
    int impX = x + 5;
    int upX = x + w - 6;
    int downX = x + w - 3;

    if (ev.move) {
        hoverLayerAdd = (ev.y == toolbarY && ev.x >= addX && ev.x < addX + 2);
        hoverLayerImport = (ev.y == toolbarY && ev.x >= impX && ev.x < impX + 2);
        hoverLayerUp = (ev.y == toolbarY && ev.x >= upX && ev.x < upX + 2);
        hoverLayerDown = (ev.y == toolbarY && ev.x >= downX && ev.x < downX + 2);
    }

    if (ev.button == 0 && ev.pressed && ev.y == toolbarY) {
        if (ev.x >= addX && ev.x < addX + 2) {
             int idx = static_cast<int>(working.getLayerCount()) + 1;
             YuiLayer layer(working.getWidth(), working.getHeight(), "Layer " + std::to_string(idx));
             working.addLayer(layer);
             return true;
        }
        if (ev.x >= impX && ev.x < impX + 2) {
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
             return true;
        }

        int idx = working.getActiveLayerIndex();
        int count = static_cast<int>(working.getLayerCount());
        if (ev.x >= upX && ev.x < upX + 2 && idx < count - 1) {
            working.moveLayer(idx, idx + 1);
        } else if (ev.x >= downX && ev.x < downX + 2 && idx > 0) {
            working.moveLayer(idx, idx - 1);
        }
        return true;
    }

    int listStart = y + 4;
    int listRows = std::max(0, h - 9);
    int layerCount = static_cast<int>(working.getLayerCount());
    
    if (ev.button == 2 && ev.pressed) {
        bool inList = (ev.y >= listStart && ev.y < listStart + listRows);
        int row = ev.y - listStart;
        if (inList && row < layerCount) {
             int layerIndex = layerCount - 1 - row;
             showLayerMenu = true;
             layerMenuIdx = layerIndex;
             layerMenuState.visible = true;
             layerMenuState.x = ev.x;
             layerMenuState.y = ev.y;
             layerMenuState.selectedIndex = 0;
             layerMenuState.width = ContextMenu::calculateWidth({"Move Up", "Move Down", "Top", "Bottom", "Rename", "Delete"});
        } else {
             showLayerMenu = true;
             layerMenuIdx = -1;
             layerMenuState.visible = true;
             layerMenuState.x = ev.x;
             layerMenuState.y = ev.y;
             layerMenuState.selectedIndex = 0;
             layerMenuState.width = ContextMenu::calculateWidth({"New Layer", "Import Layer"});
        }
        return true;
    }

    if (ev.y >= listStart && ev.y < listStart + listRows) {
        int row = ev.y - listStart;
        if (row < layerCount) {
            int layerIndex = layerCount - 1 - row;
            if (ev.button == 0 && ev.pressed) {
                if (ev.x >= x + 2 && ev.x < x + 5) {
                    const auto& layer = working.getLayer(static_cast<size_t>(layerIndex));
                    working.setLayerVisible(layerIndex, !layer.isVisible());
                } else {
                    working.setActiveLayerIndex(layerIndex);
                    pendingOpacity = working.getLayer(layerIndex).getOpacity();
                    if (!opacityInputState.focused) {
                        opacityText = std::to_string(static_cast<int>(pendingOpacity * 100));
                    }
                }
            }
            return true;
        }
    }

    int infoY = y + h - 3;
    int barY = y + h - 2;

    TextFieldStyle opStyle;
    opStyle.width = 6;
    if (TextField::handleInput(ev, opacityText, opacityInputState, opStyle)) {
        return true;
    }

    if ((ev.button == 0 && ev.pressed && ev.y == barY) || (ev.move && dragLayerOpacity)) {
        dragLayerOpacity = true;
        return true;
    }

    if (ev.button == 0 && ev.pressed) {
        opacityInputState.focused = false;
    }

    return true;
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

    auto clampDialogLocal = [&]() {
        int sw = surface.getWidth();
        int sh = surface.getHeight();
        dx = std::clamp(dx, 0, std::max(0, sw - boxW));
        dy = std::clamp(dy, 0, std::max(0, sh - boxH));
    };
    clampDialogLocal();

    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;

    const std::string blocks[] = {" ","▂","▃","▄","▅","▆","▇","█"};

    while (running) {
        EnvConfig::getInstance().refresh();
        auto runtime = EnvConfig::getInstance().getRuntimeInfo();
        double preciseX = runtime.mouseCellWin.x - 1.0;
        double preciseY = runtime.mouseCellWin.y - 1.0;

        clampDialogLocal();
        renderFrame();
        surface.drawFrame(dx, dy, boxW, boxH, kFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, boxW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, "HSV Picker", theme.title, theme.background);

        bool isHoverCancel = (preciseY >= dy + 1 && preciseY < dy + 2 && preciseX >= dx + boxW - 10 && preciseX < dx + boxW - 2);
        RGBColor cancelFg = isHoverCancel ? RGBColor{255, 255, 255} : theme.title;
        RGBColor cancelBg = isHoverCancel ? RGBColor{200, 50, 50} : theme.background;
        surface.drawText(dx + boxW - 10, dy + 1, "[CANCEL]", cancelFg, cancelBg);

        int svX = dx + 2;
        int svY = dy + 3;
        for (int py = 0; py < svH; ++py) {
            double vv = 1.0 - static_cast<double>(py) / std::max(1, svH - 1);
            for (int px = 0; px < svW; ++px) {
                double ss = static_cast<double>(px) / std::max(1, svW - 1);
                RGBColor c = TuiUtils::hsvToRgb(h, ss, vv);
                surface.drawText(svX + px, svY + py, " ", c, c);
            }
        }

        int markX = svX + static_cast<int>(s * (svW - 1));
        int markY = svY + static_cast<int>((1.0 - v) * (svH - 1));
        surface.drawText(markX, markY, "+", {0,0,0}, {255,255,255});

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
                            running = false;
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
        "▁","▂","▃","▄","▅","▆","▇","█",
        "▏","▎","▍","▌","▋","▊","▉","█",
        "▘","▝","▖","▗","▚","▞"
    };
    const int cols = 8;
    const int cellW = 3;
    const int slotW = cellW + 1;
    int rows = static_cast<int>((presets.size() + cols - 1) / cols);
    int gridW = cols * slotW - 1;
    int boxW = std::max(48, gridW + 4);
    int boxH = 8 + rows;
    int dx = (surface.getWidth() - boxW) / 2;
    int dy = (surface.getHeight() - boxH) / 2;
    auto clampDialogLocal = [&]() {
        int sw = surface.getWidth();
        int sh = surface.getHeight();
        dx = std::clamp(dx, 0, std::max(0, sw - boxW));
        dy = std::clamp(dy, 0, std::max(0, sh - boxH));
    };
    clampDialogLocal();
    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;
    while (running) {
        clampDialogLocal();
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
                    clampDialogLocal();
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

bool YuiEditorScreen::openRenameDialog(const std::string& initial, std::string& outName, const std::string& title) {
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

    auto clampDialogLocal = [&]() {
        dx = std::clamp(dx, 0, std::max(0, surface.getWidth() - boxW));
        dy = std::clamp(dy, 0, std::max(0, surface.getHeight() - boxH));
    };

    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;
    bool hoverOk = false, hoverCancel = false;

    while (running) {
        inputState.updateCaret();
        clampDialogLocal();
        renderFrame();
        
        surface.drawFrame(dx, dy, boxW, boxH, kFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, boxW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, title, theme.title, theme.background);
        
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
            RGBColor bg = hot ? YuiUtils::darken(theme.accent, 0.8) : theme.accent;
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
                    clampDialogLocal();
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
