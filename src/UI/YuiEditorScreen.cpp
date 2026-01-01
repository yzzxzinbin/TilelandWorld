#include "YuiEditorScreen.h"
#include "TuiUtils.h"
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

    std::string encodeUtf8(char32_t cp) {
        std::string out;
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }
}

namespace TilelandWorld {
namespace UI {

YuiEditorScreen::YuiEditorScreen(AssetManager& manager_, std::string assetName_, ImageAsset asset_)
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
            if (ev.type == InputEvent::Type::Mouse) {
                handleMouse(ev, running);
            } else if (ev.type == InputEvent::Type::Key) {
                handleKey(ev, running);
            }
            if (!running) break;
        }
    }
    manager.saveAsset(working, assetName);
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
    canvasX = 2;
    canvasY = 5; // leave a blank row below the toolbar
    canvasW = std::max(10, surface.getWidth() - canvasX - 2 - propPanelW);
    canvasH = std::max(6, surface.getHeight() - canvasY - 1);

    surface.fillRect(canvasX, canvasY, canvasW, canvasH, theme.itemFg, theme.panel, " ");
    surface.drawFrame(canvasX, canvasY, canvasW, canvasH, kFrame, theme.itemFg, theme.panel);

    drawCanvas();
    drawScrollbars();
    if (hasSelection) {
        drawPropertyPanel();
    }
}

static constexpr int kToolbarY = 3; // leave a blank line above the toolbar

void YuiEditorScreen::drawToolbar() {
    int y = kToolbarY;
    std::string hand = activeTool == Tool::Hand ? "[ Hand ]" : "  Hand  ";
    std::string prop = activeTool == Tool::Property ? "[ Property ]" : "  Property  ";
    int x = 2;
    auto drawBtn = [&](const std::string& label, bool active) {
        RGBColor bg = active ? darken(theme.accent, 0.6) : theme.accent;
        RGBColor fg = active ? RGBColor{255,255,255} : theme.title;
        surface.drawText(x, y, label, fg, bg);
        x += static_cast<int>(label.size()) + 2;
    };
    drawBtn(hand, activeTool == Tool::Hand);
    drawBtn(prop, activeTool == Tool::Property);
    surface.drawText(x, y, "Space: toggle tool | Mouse wheel: scroll | Drag (hand): pan | Q: save & back", theme.hintFg, theme.background);
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
                const auto& cell = working.getCell(ax, ay);
                glyph = cell.character.empty() ? " " : cell.character;
                fg = cell.fg;
                bg = cell.bg;
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
    int x = surface.getWidth() - propPanelW - 2;
    int y = canvasY;
    int w = propPanelW;
    int h = canvasH;
    surface.fillRect(x, y, w, h, theme.itemFg, theme.panel, " ");
    surface.drawFrame(x, y, w, h, kFrame, theme.itemFg, theme.panel);
    surface.fillRect(x + 1, y + 1, w - 2, 1, theme.title, theme.background, " ");
    surface.drawText(x + 2, y + 1, "Properties", theme.title, theme.background);

    const auto& cell = hasStaged ? stagedCell : working.getCell(selX, selY);
    int line = y + 3;
    surface.drawText(x + 2, line++, "Pos: (" + std::to_string(selX) + "," + std::to_string(selY) + ")", theme.itemFg, theme.panel);
    surface.drawText(x + 2, line, "Glyph:", theme.itemFg, theme.panel);
    surface.drawText(x + 10, line++, " [" + (cell.character.empty()?" ":cell.character) + "] ", theme.itemFg, theme.panel);
    std::ostringstream fgss;
    fgss << "FG: " << (int)cell.fg.r << "," << (int)cell.fg.g << "," << (int)cell.fg.b;
    surface.drawText(x + 2, line++, fgss.str(), theme.itemFg, theme.panel);
    std::ostringstream bgss;
    bgss << "BG: " << (int)cell.bg.r << "," << (int)cell.bg.g << "," << (int)cell.bg.b;
    surface.drawText(x + 2, line++, bgss.str(), theme.itemFg, theme.panel);
    surface.drawText(x + 2, line++, "Click FG/BG to edit (HSV)", theme.hintFg, theme.panel);
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

void YuiEditorScreen::handleMouse(const InputEvent& ev, bool& running) {
    int mx = ev.x;
    int my = ev.y;
    hoverHThumb = false;
    hoverVThumb = false;
    hoverConfirm = false;
    hoverCancel = false;
    // Toolbar button hit-test
    if (my == kToolbarY) {
        int x = 2;
        std::string handLabel = activeTool == Tool::Hand ? "[ Hand ]" : "  Hand  ";
        std::string propLabel = activeTool == Tool::Property ? "[ Property ]" : "  Property  ";
        int handStart = x;
        int handEnd = handStart + static_cast<int>(handLabel.size());
        x = handEnd + 2;
        int propStart = x;
        int propEnd = propStart + static_cast<int>(propLabel.size());

        if (ev.button == 0 && ev.pressed) {
            if (mx >= handStart && mx < handEnd) {
                activeTool = Tool::Hand;
                dragging = false;
            } else if (mx >= propStart && mx < propEnd) {
                activeTool = Tool::Property;
                dragging = false;
            }
        }
        // Do not treat toolbar clicks as canvas interactions
        if (ev.button == 0 && ev.pressed) return;
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
                    originalCell = working.getCell(selX, selY);
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
        int px = surface.getWidth() - propPanelW - 2;
        int py = canvasY;
        if (mx >= px && mx < px + propPanelW && my >= py && my < py + canvasH) {
            int lineGlyph = py + 4;
            int lineFg = lineGlyph + 1;
            int lineBg = lineFg + 1;
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
                    working.setCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == lineFg) {
                RGBColor newColor = stagedCell.fg;
                if (openColorPicker(newColor, newColor)) {
                    stagedCell.fg = newColor;
                    hasStaged = true;
                    working.setCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == lineBg) {
                RGBColor newColor = stagedCell.bg;
                if (openColorPicker(newColor, newColor)) {
                    stagedCell.bg = newColor;
                    hasStaged = true;
                    working.setCell(selX, selY, stagedCell);
                }
            } else if (ev.button == 0 && ev.pressed && my == btnY) {
                if (mx >= okX && mx < okX + static_cast<int>(okLabel.size())) {
                    // Confirm
                    if (hasStaged) {
                        working.setCell(selX, selY, stagedCell);
                        originalCell = stagedCell;
                    }
                    hasSelection = false;
                    hasStaged = false;
                } else if (mx >= cancelX && mx < cancelX + static_cast<int>(cancelLabel.size())) {
                    // Cancel -> revert staged and working to original
                    stagedCell = originalCell;
                    working.setCell(selX, selY, originalCell);
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
            originalCell = working.getCell(selX, selY);
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

bool YuiEditorScreen::openColorPicker(RGBColor initial, RGBColor& outColor) {
    double h=0, s=0, v=0;
    TuiUtils::rgbToHsv(initial, h, s, v);
    bool running = true;
    bool accepted = false;
    const int svW = 64;
    const int svH = 28;
    const int boxW = svW + 16; // room for hue bar + padding
    const int boxH = svH + 8;  // header + preview/RGB/hint lines
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
        surface.drawText(dx + 2, dy + 1, "HSV Picker", theme.title, theme.background);

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
        int markX = svX + static_cast<int>(std::round(s * (svW - 1)));
        int markY = svY + static_cast<int>(std::round((1.0 - v) * (svH - 1)));
        surface.drawText(markX, markY, "+", {0,0,0}, {255,255,255});

        // Hue bar on the right
        int hueX = svX + svW + 2;
        int hueW = 4;
        for (int py = 0; py < svH; ++py) {
            double hh = 360.0 * static_cast<double>(py) / std::max(1, svH - 1);
            RGBColor c = TuiUtils::hsvToRgb(hh, 1.0, 1.0);
            surface.fillRect(hueX, svY + py, hueW, 1, c, c, " ");
        }
        int hueMarkY = svY + static_cast<int>(std::round(h / 360.0 * (svH - 1)));
        RGBColor curHueColor = TuiUtils::hsvToRgb(h, 1.0, 1.0);
        surface.drawText(hueX, hueMarkY, " << ", {255,255,255}, curHueColor);

        RGBColor preview = TuiUtils::hsvToRgb(h, s, v);
        int previewY = dy + svH + 4;
        surface.drawText(dx + 2, previewY, "Preview", theme.itemFg, theme.panel);
        int swatchX = dx + 12;
        int swatchW = std::max(0, boxW - (swatchX - dx) - 2 - 6); // shorten preview bar by 6 cells
        surface.fillRect(swatchX, previewY, swatchW, 1, preview, preview, " ");
        std::ostringstream rgbss;
        rgbss << "RGB: " << (int)preview.r << "," << (int)preview.g << "," << (int)preview.b;
        surface.drawText(dx + 2, dy + svH + 5, rgbss.str(), theme.itemFg, theme.panel);
        surface.drawText(dx + 2, dy + svH + 6, "Click square: S/V | Click bar: H | Wheel: H | Enter: OK | Q: cancel", theme.hintFg, theme.panel);

        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Enter) { accepted = true; running = false; break; }
                if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) { running = false; break; }
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
                    if (ev.x >= svX && ev.x < svX + svW && ev.y >= svY && ev.y < svY + svH) {
                        s = static_cast<double>(ev.x - svX) / std::max(1, svW - 1);
                        v = 1.0 - static_cast<double>(ev.y - svY) / std::max(1, svH - 1);
                    } else if (ev.x >= hueX && ev.x < hueX + hueW && ev.y >= svY && ev.y < svY + svH) {
                        h = 360.0 * static_cast<double>(ev.y - svY) / std::max(1, svH - 1);
                    }
                }
                if (ev.wheel != 0) {
                    h = std::fmod(h + ev.wheel * 6.0 + 360.0, 360.0);
                }
                if (ev.button == 0 && !ev.pressed && !ev.move) {
                    dragging = false;
                }
            }
        }
    }
    if (accepted) {
        outColor = TuiUtils::hsvToRgb(h, s, v);
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
                        std::string enc = encodeUtf8(ev.ch);
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

} // namespace UI
} // namespace TilelandWorld
