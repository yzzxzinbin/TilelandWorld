#include "AssetManagerScreen.h"
#include "DirectoryBrowserScreen.h"
#include "ToggleSwitch.h"
#include "TuiUtils.h"
#include "../ImgAssetsInfrastructure/ImageLoader.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <sstream>
#include <iomanip>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
    const std::string kOpenBtnLabel = "[Open]";
    const std::string kDeleteBtnLabel = "[Delete]";
    const std::string kInfoBtnLabel = "[Info]";
}

namespace TilelandWorld {
namespace UI {

    static RGBColor darken(const RGBColor& c, double factor) {
        double f = std::max(0.0, std::min(1.0, factor));
        return RGBColor{
            static_cast<uint8_t>(std::max(0.0, c.r * f)),
            static_cast<uint8_t>(std::max(0.0, c.g * f)),
            static_cast<uint8_t>(std::max(0.0, c.b * f))
        };
    }

    AssetManagerScreen::AssetManagerScreen() 
        : manager("res/Assets"), 
          surface(80, 24), 
          painter(),
          input(std::make_unique<InputController>()),
          taskSystem(4) // Use 4 threads or auto
    {
        refreshList();
    }

    void AssetManagerScreen::show() {
        input->start();
        bool running = true;
        
        std::cout << "\x1b[?25l"; // Hide cursor

        while (running) {
            drawMainUI();
            painter.present(surface);
            
            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Mouse) {
                    if (!assets.empty() && ev.wheel != 0) {
                        int delta = (ev.wheel > 0) ? -1 : 1;
                        int maxIndex = std::max(0, static_cast<int>(assets.size()) - 1);
                        selectedIndex = std::clamp(selectedIndex + delta, 0, maxIndex);
                        loadPreview();
                    }

                    bool insideList = (ev.x >= listX && ev.x < listX + listW && ev.y >= listY && ev.y < listY + listH);
                    hoverRow = -1;
                    hoverButton = HoverButton::None;
                    if (insideList && !assets.empty()) {
                        int row = ev.y - listY;
                        if (row >= 0 && row < static_cast<int>(assets.size())) {
                            hoverRow = row;
                            if (row != selectedIndex && ev.move) {
                                selectedIndex = row;
                                loadPreview();
                            }
                            bool onOpen = ev.x >= buttonOpenX && ev.x < buttonOpenX + static_cast<int>(kOpenBtnLabel.size());
                            bool onDelete = ev.x >= buttonDeleteX && ev.x < buttonDeleteX + static_cast<int>(kDeleteBtnLabel.size());
                            bool onInfo = ev.x >= buttonInfoX && ev.x < buttonInfoX + static_cast<int>(kInfoBtnLabel.size());
                            if (onOpen) hoverButton = HoverButton::Open;
                            else if (onDelete) hoverButton = HoverButton::Delete;
                            else if (onInfo) hoverButton = HoverButton::Info;

                            if (ev.pressed) {
                                selectedIndex = row;
                                loadPreview();
                                if (onDelete) {
                                    deleteCurrentAsset();
                                } else if (onOpen) {
                                    // Stub for open: currently just reload preview
                                    loadPreview();
                                } else if (onInfo) {
                                    if (!previewLoaded) loadPreview();
                                    if (previewLoaded) showInfoDialog(currentPreview);
                                }
                            }
                        }
                    } else {
                        hoverButton = HoverButton::None;
                        hoverRow = -1;
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Escape || (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q'))) {
                        running = false;
                    } else if (ev.key == InputKey::ArrowUp) {
                        if (selectedIndex > 0) selectedIndex--;
                        loadPreview();
                    } else if (ev.key == InputKey::ArrowDown) {
                        if (selectedIndex < static_cast<int>(assets.size()) - 1) selectedIndex++;
                        loadPreview();
                    } else if (ev.key == InputKey::Character) {
                        if (ev.ch == 'i' || ev.ch == 'I') {
                            importAsset();
                        } else if (ev.ch == 'd' || ev.ch == 'D') {
                            deleteCurrentAsset();
                        }
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        input->stop();
        std::cout << "\x1b[?25h"; // Show cursor
    }

    void AssetManagerScreen::refreshList() {
        assets = manager.listAssets();
        if (selectedIndex >= static_cast<int>(assets.size())) {
            selectedIndex = std::max(0, static_cast<int>(assets.size()) - 1);
        }
        loadPreview();
    }

    void AssetManagerScreen::loadPreview() {
        if (assets.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(assets.size())) {
            previewLoaded = false;
            return;
        }
        
        const auto& entry = assets[selectedIndex];
        if (entry.name != lastPreviewName) {
            currentPreview = manager.loadAsset(entry.name);
            lastPreviewName = entry.name;
            previewLoaded = true;
        }
    }

    void AssetManagerScreen::importAsset() {
        // Stop input to let DirectoryBrowser use it
        input->stop();
        
        // Allow common image formats
        DirectoryBrowserScreen browser(lastImportPath, true, ""); 
        std::string path = browser.show();
        
        if (!path.empty()) {
            std::filesystem::path p(path);
            lastImportPath = p.parent_path().string(); // Remember path

            std::string ext = p.extension().string();
            // Simple check for image extensions
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                showImportDialog(path);
            }
        }
        
        input->start();
    }

    void AssetManagerScreen::deleteCurrentAsset() {
        if (assets.empty() || selectedIndex < 0) return;
        manager.deleteAsset(assets[selectedIndex].name);
        refreshList();
    }

    void AssetManagerScreen::drawMainUI() {
        // Resize surface if needed
        #ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
            int consoleWidth = std::max(60, static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1));
            int consoleHeight = std::max(24, static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1));
            if (surface.getWidth() != consoleWidth || surface.getHeight() != consoleHeight) {
                surface.resize(consoleWidth, consoleHeight);
            }
        }
        #endif
        
        int w = surface.getWidth();
        int h = surface.getHeight();
        BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};

        // Base layers and chrome (aligned with Settings/SaveCreation)
        surface.clear(theme.itemFg, theme.background, " ");
        surface.fillRect(0, 0, w, 1, theme.title, theme.accent, " ");
        surface.fillRect(0, h - 1, w, 1, theme.title, theme.accent, " ");
        surface.drawText(2, 1, "Image Assets", {0, 0, 0}, theme.accent);
        surface.drawText(w - 18, 1, "Tileland World", {0, 0, 0}, theme.accent);
        surface.drawText(2, 3, "Up/Down: select | I: import | D: delete | Esc: back", theme.hintFg, theme.background);
        surface.drawText(2, h - 1, "[I] Import  [D] Delete  [Esc] Back", theme.hintFg, theme.accent);

        // Layout
        int padding = 2;
        int contentY = 5; // leave room for title + subtitle
        int contentH = h - contentY - 2;
        int listWOuter = std::max(24, w / 3);
        int listXOuter = padding;
        int prevX = listXOuter + listWOuter + padding;
        int prevW = w - prevX - padding;
        if (prevW < 20) prevW = 20;

        // List panel (header strip to match other screens)
        surface.fillRect(listXOuter, contentY, listWOuter, contentH, theme.itemFg, theme.panel, " ");
        surface.drawFrame(listXOuter, contentY, listWOuter, contentH, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(listXOuter + 1, contentY + 1, listWOuter - 2, 1, theme.title, theme.background, " ");
        surface.drawText(listXOuter + 2, contentY + 1, "Assets", theme.title, theme.background);

        int listInnerX = listXOuter + 1;
        int listInnerY = contentY + 3;
        int listInnerW = std::max(0, listWOuter - 2);
        int listInnerH = std::max(0, contentH - 4);

        listX = listInnerX;
        listY = listInnerY;
        listW = listInnerW;
        listH = listInnerH;

        int buttonsWidth = static_cast<int>(kOpenBtnLabel.size() + 1 + kDeleteBtnLabel.size() + 1 + kInfoBtnLabel.size());
        int buttonsStart = std::max(listInnerX + 4, listInnerX + listInnerW - buttonsWidth);
        buttonOpenX = buttonsStart;
        buttonDeleteX = buttonOpenX + static_cast<int>(kOpenBtnLabel.size()) + 1;
        buttonInfoX = buttonDeleteX + static_cast<int>(kDeleteBtnLabel.size()) + 1;

        RGBColor white{255,255,255};
        RGBColor black{0,0,0};
        for (int i = 0; i < static_cast<int>(assets.size()) && i < listInnerH; ++i) {
            int rowY = listInnerY + i;
            bool focused = (i == selectedIndex);
            RGBColor fg = focused ? black : theme.itemFg;
            RGBColor bg = focused ? white : theme.panel;
            surface.fillRect(listInnerX, rowY, listInnerW, 1, fg, bg, " ");

            int textLimit = std::max(0, buttonOpenX - (listInnerX + 1) - 1);
            std::string name = assets[i].name;
            name = TuiUtils::trimToUtf8VisualWidth(name, static_cast<size_t>(std::max(0, textLimit)));
            surface.drawText(listInnerX + 1, rowY, name, fg, bg);

            if (focused) {
                auto btnColor = [&](HoverButton hb) {
                    bool hot = (hoverRow == i && hoverButton == hb);
                    RGBColor baseBg = theme.accent;
                    RGBColor bbg = hot ? darken(baseBg, 0.9) : baseBg;
                    RGBColor bfg = theme.title;
                    return std::make_pair(bfg, bbg);
                };
                auto [fgOpen, bgOpen] = btnColor(HoverButton::Open);
                auto [fgDel, bgDel] = btnColor(HoverButton::Delete);
                auto [fgInfo, bgInfo] = btnColor(HoverButton::Info);
                surface.drawText(buttonOpenX, rowY, kOpenBtnLabel, fgOpen, bgOpen);
                surface.drawText(buttonDeleteX, rowY, kDeleteBtnLabel, fgDel, bgDel);
                surface.drawText(buttonInfoX, rowY, kInfoBtnLabel, fgInfo, bgInfo);
            }
        }

        // Preview panel
        surface.fillRect(prevX, contentY, prevW, contentH, theme.itemFg, theme.panel, " ");
        surface.drawFrame(prevX, contentY, prevW, contentH, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(prevX + 1, contentY + 1, prevW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(prevX + 2, contentY + 1, "Preview", theme.title, theme.background);

        if (previewLoaded) {
            drawPreview(prevX + 2, contentY + 3, prevW - 4, contentH - 5);
        } else {
            surface.drawText(prevX + 2, contentY + 3, "No preview available", theme.hintFg, theme.panel);
        }
    }

    void AssetManagerScreen::showImportDialog(const std::string& filePath) {
        std::string widthStr = "80";
        int qualityIdx = 1; // 0: Low, 1: High
        int focusIdx = 0; // 0:W, 1:Quality, 2:Import, 3:Cancel
        bool hoverImport = false, hoverCancel = false;
        ToggleSwitchState qualityToggleState{};
        
        bool dialogRunning = true;
        int mouseX = -1;
        int mouseY = -1;
        input->start(); 

        while (dialogRunning) {
            // Render background (dimmed)
            drawMainUI();
            
            // Draw Dialog Box
            int w = surface.getWidth();
            int h = surface.getHeight();
            int dw = 44, dh = 12;
            int dx = (w - dw) / 2;
            int dy = (h - dh) / 2;
            
            BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Import Settings", theme.title, theme.background);
            int labelWidth = 8;
            int labelX = dx + 2;
            int fieldX = dx + labelWidth + 6;
            auto drawLabel = [&](const std::string& label, int y){
                int pad = std::max(0, labelWidth - static_cast<int>(label.size()));
                surface.drawText(labelX, y, std::string(pad, ' ') + label + ":", theme.itemFg, theme.panel);
            };
            auto drawField = [&](int idx, const std::string& val, int y) {
                RGBColor fg = (focusIdx == idx) ? theme.focusFg : theme.itemFg;
                RGBColor bg = (focusIdx == idx) ? theme.focusBg : theme.panel;
                surface.drawText(fieldX, y, " " + val + " ", fg, bg);
            };
            int lineY = dy + 3;
            drawLabel("Width", lineY);
            drawField(0, widthStr, lineY);
            lineY++;
            drawLabel("Quality", lineY);
            int toggleX = fieldX;
            ToggleSwitchStyle tstyle{};
            tstyle.offLabel = "LOW";
            tstyle.onLabel = "HIGH";
            ToggleSwitch::render(surface, toggleX, lineY, qualityIdx == 1, qualityToggleState, tstyle);
            
            // Buttons
            std::string importLbl = "[ Import ]";
            std::string cancelLbl = "[ Cancel ]";
            int importX = dx + 6;
            int cancelX = dx + dw - static_cast<int>(cancelLbl.size()) - 6;
            int btnY = dy + dh - 3;
            auto drawBtn = [&](const std::string& lbl, int x, bool hot, bool focus){
                RGBColor baseBg = theme.accent;
                RGBColor bg = hot || focus ? darken(baseBg, 0.9) : baseBg;
                RGBColor fg = theme.title;
                surface.drawText(x, btnY, lbl, fg, bg);
            };
            drawBtn(importLbl, importX, hoverImport, focusIdx == 2);
            drawBtn(cancelLbl, cancelX, hoverCancel, focusIdx == 3);

            auto updateHover = [&](){
                if (mouseX < 0 || mouseY < 0) {
                    hoverImport = hoverCancel = false;
                    qualityToggleState.hover = qualityToggleState.hot = false;
                    return;
                }
                hoverImport = (mouseX >= importX && mouseX < importX + static_cast<int>(importLbl.size()) && mouseY == btnY);
                hoverCancel = (mouseX >= cancelX && mouseX < cancelX + static_cast<int>(cancelLbl.size()) && mouseY == btnY);
                bool onToggle = (mouseY == lineY && mouseX >= toggleX && mouseX < toggleX + tstyle.trackLen + 2);
                qualityToggleState.hover = onToggle;
                qualityToggleState.hot = onToggle;
            };
            updateHover();
            
            painter.present(surface);
            
            // Input
            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;
                    bool onImport = (ev.x >= importX && ev.x < importX + static_cast<int>(importLbl.size()) && ev.y == btnY);
                    bool onCancel = (ev.x >= cancelX && ev.x < cancelX + static_cast<int>(cancelLbl.size()) && ev.y == btnY);
                    bool onWidth = (ev.y == dy + 3 && ev.x >= fieldX && ev.x < fieldX + static_cast<int>(widthStr.size()) + 2);
                    bool onToggle = (ev.y == lineY && ev.x >= toggleX && ev.x < toggleX + tstyle.trackLen + 2);
                    // focus text/toggle by click
                    if (ev.button == 0 && ev.pressed) {
                        if (onWidth) {
                            focusIdx = 0;
                        } else if (onToggle) {
                            focusIdx = 1;
                            bool wasOn = (qualityIdx == 1);
                            qualityIdx = 1 - qualityIdx;
                            qualityToggleState.previousOn = wasOn;
                            qualityToggleState.lastToggle = std::chrono::steady_clock::now();
                        }
                    }
                    if (ev.pressed && ev.button == 0) {
                        if (onImport) {
                            focusIdx = 2;
                        } else if (onCancel) {
                            focusIdx = 3;
                        }
                    }
                    if (ev.pressed && ev.button == 0) {
                        if (onImport) {
                            // trigger import same as Enter on Import
                            try {
                                int tw = std::stoi(widthStr);
                                surface.drawText(dx + 2, dy + dh - 2, "Converting...", theme.focusFg, theme.panel);
                                painter.present(surface);
                                RawImage raw = ImageLoader::load(filePath);
                                if (raw.valid) {
                                    AdvancedImageConverter::Options opts;
                                    opts.targetWidth = tw;
                                    double aspect = 0.5;
                                    opts.targetHeight = std::max(1, (int)std::round((double)raw.height * tw * aspect / raw.width));
                                    opts.quality = (qualityIdx == 1) ? AdvancedImageConverter::Options::Quality::High : AdvancedImageConverter::Options::Quality::Low;
                                    AdvancedImageConverter converter;
                                    ImageAsset asset = converter.convert(raw, opts, taskSystem);
                                    std::string name = std::filesystem::path(filePath).stem().string();
                                    manager.saveAsset(asset, name);
                                    refreshList();
                                }
                            } catch (...) {}
                            dialogRunning = false;
                        } else if (onCancel) {
                            dialogRunning = false;
                        }
                    }
                }
                updateHover();
                if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Escape) {
                        dialogRunning = false;
                    } else if (ev.key == InputKey::Tab) {
                        focusIdx = (focusIdx + 1) % 4;
                    } else if (ev.key == InputKey::ArrowUp) {
                        focusIdx = (focusIdx - 1 + 4) % 4;
                    } else if (ev.key == InputKey::ArrowDown) {
                        focusIdx = (focusIdx + 1) % 4;
                    } else if (ev.key == InputKey::Enter) {
                        if (focusIdx == 2) { // Import
                            try {
                                int tw = std::stoi(widthStr);
                                
                                surface.drawText(dx + 2, dy + dh - 2, "Converting...", theme.focusFg, theme.panel);
                                painter.present(surface);
                                
                                RawImage raw = ImageLoader::load(filePath);
                                if (raw.valid) {
                                    AdvancedImageConverter::Options opts;
                                    opts.targetWidth = tw;
                                    // Calculate height based on aspect ratio (0.5 for TUI cells)
                                    double aspect = 0.5;
                                    opts.targetHeight = std::max(1, (int)std::round((double)raw.height * tw * aspect / raw.width));
                                    opts.quality = (qualityIdx == 1) ? AdvancedImageConverter::Options::Quality::High : AdvancedImageConverter::Options::Quality::Low;
                                    
                                    AdvancedImageConverter converter;
                                    ImageAsset asset = converter.convert(raw, opts, taskSystem);
                                    
                                    std::string name = std::filesystem::path(filePath).stem().string();
                                    manager.saveAsset(asset, name);
                                    refreshList();
                                }
                            } catch (...) {}
                            dialogRunning = false;
                        } else if (focusIdx == 3) { // Cancel
                            dialogRunning = false;
                        }
                    } else if (ev.key == InputKey::Character && ev.ch == '\b') {
                        if (focusIdx == 0 && !widthStr.empty()) widthStr.pop_back();
                    } else if (ev.key == InputKey::Character) {
                        if (isdigit(ev.ch)) {
                            if (focusIdx == 0) widthStr += ev.ch;
                        }
                    } else if (ev.key == InputKey::ArrowLeft || ev.key == InputKey::ArrowRight) {
                        if (focusIdx == 1) {
                            bool wasOn = (qualityIdx == 1);
                            qualityIdx = 1 - qualityIdx; // Toggle 0/1
                            qualityToggleState.previousOn = wasOn;
                            qualityToggleState.lastToggle = std::chrono::steady_clock::now();
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        input->stop();
    }

void AssetManagerScreen::showInfoDialog(const ImageAsset& asset) {
    int w = surface.getWidth();
    int h = surface.getHeight();
    std::vector<std::string> lines;
    if (asset.getWidth() <= 0 || asset.getHeight() <= 0) {
        lines.push_back("No image loaded");
    } else {
        lines.push_back("Width: " + std::to_string(asset.getWidth()));
        lines.push_back("Height: " + std::to_string(asset.getHeight()));
        int wcells = asset.getWidth();
        int hcells = asset.getHeight();
        std::set<std::string> glyphs;
        std::set<std::string> fgset;
        std::set<std::string> bgset;
        std::map<std::string,int> glyphCount;
        for (int y = 0; y < hcells; ++y) {
            for (int x = 0; x < wcells; ++x) {
                const auto& c = asset.getCell(x, y);
                glyphs.insert(c.character);
                glyphCount[c.character]++;
                fgset.insert(std::to_string(c.fg.r) + "," + std::to_string(c.fg.g) + "," + std::to_string(c.fg.b));
                bgset.insert(std::to_string(c.bg.r) + "," + std::to_string(c.bg.g) + "," + std::to_string(c.bg.b));
            }
        }
        lines.push_back("Unique glyphs: " + std::to_string((int)glyphs.size()));
        lines.push_back("Unique foreground colors: " + std::to_string((int)fgset.size()));
        lines.push_back("Unique background colors: " + std::to_string((int)bgset.size()));

        // top glyphs
        lines.push_back("Top glyphs:");
        int shown = 0;
        for (auto it = glyphCount.begin(); it != glyphCount.end() && shown < 5; ++it) {
            std::ostringstream ss; ss << "  '" << it->first << "' x " << it->second;
            lines.push_back(ss.str());
            ++shown;
        }
    }

    int dw = std::min(64, w - 6);
    int dh = std::min(h - 4, static_cast<int>(lines.size()) + 7);
    int dx = (w - dw) / 2;
    int dy = (h - dh) / 2;
    BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};

    bool running = true;
    bool hoverOk = false, hoverClose = false;
    int mouseX = -1;
    int mouseY = -1;
    while (running) {
        drawMainUI();
        surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
        surface.drawText(dx + 2, dy + 1, "Image Info", theme.title, theme.background);
        int ly = dy + 3;
        for (const auto& L : lines) {
            if (ly >= dy + dh - 3) break;
            surface.drawText(dx + 2, ly++, L, theme.itemFg, theme.panel);
        }
        // Buttons
        std::string ok = "[ OK ]";
        std::string close = "[ Close ]";
        int okx = dx + (dw / 2) - static_cast<int>(ok.size()) - 1;
        int closex = dx + (dw / 2) + 1;
        int btnY = dy + dh - 2;
        auto drawBtn = [&](const std::string& lbl, int x, bool hot){
            RGBColor baseBg = theme.accent;
            RGBColor bg = hot ? darken(baseBg, 0.9) : baseBg;
            RGBColor fg = theme.title;
            surface.drawText(x, btnY, lbl, fg, bg);
        };
        drawBtn(ok, okx, hoverOk);
        drawBtn(close, closex, hoverClose);
        auto updateHover = [&](){
            if (mouseX < 0 || mouseY < 0) {
                hoverOk = hoverClose = false;
                return;
            }
            hoverOk = (mouseX >= okx && mouseX < okx + static_cast<int>(ok.size()) && mouseY == btnY);
            hoverClose = (mouseX >= closex && mouseX < closex + static_cast<int>(close.size()) && mouseY == btnY);
        };
        updateHover();
        painter.present(surface);

        auto events = input->pollEvents();
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Enter || ev.key == InputKey::Escape || (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q'))) {
                    running = false; break;
                }
            } else if (ev.type == InputEvent::Type::Mouse) {
                mouseX = ev.x;
                mouseY = ev.y;
                bool onOk = (ev.x >= okx && ev.x < okx + static_cast<int>(ok.size()) && ev.y == btnY);
                bool onClose = (ev.x >= closex && ev.x < closex + static_cast<int>(close.size()) && ev.y == btnY);
                if (ev.button == 0 && ev.pressed) {
                    if (onOk || onClose) { running = false; break; }
                }
            }
        }
        updateHover();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}


    void AssetManagerScreen::drawPreview(int x, int y, int w, int h) {
        // Center the image
        int imgW = currentPreview.getWidth();
        int imgH = currentPreview.getHeight();
        
        int drawX = x + (w - imgW) / 2;
        int drawY = y + (h - imgH) / 2;
        
        for (int iy = 0; iy < imgH; ++iy) {
            if (iy + drawY < y || iy + drawY >= y + h) continue;
            for (int ix = 0; ix < imgW; ++ix) {
                if (ix + drawX < x || ix + drawX >= x + w) continue;
                
                const auto& cell = currentPreview.getCell(ix, iy);
                TuiCell* tuiCell = surface.editCell(drawX + ix, drawY + iy);
                if (tuiCell) {
                    tuiCell->glyph = cell.character;
                    tuiCell->fg = cell.fg;
                    tuiCell->bg = cell.bg;
                }
            }
        }
    }

}
}
