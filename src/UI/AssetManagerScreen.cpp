#include "AssetManagerScreen.h"
#include "DirectoryBrowserScreen.h"
#include "TuiUtils.h"
#include "../ImgAssetsInfrastructure/ImageLoader.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

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
                if (ev.type == InputEvent::Type::Key) {
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
        int listW = std::max(24, w / 3);
        int listX = padding;
        int prevX = listX + listW + padding;
        int prevW = w - prevX - padding;
        if (prevW < 20) prevW = 20;

        // List panel (header strip to match other screens)
        surface.fillRect(listX, contentY, listW, contentH, theme.itemFg, theme.panel, " ");
        surface.drawFrame(listX, contentY, listW, contentH, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(listX + 1, contentY + 1, listW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(listX + 2, contentY + 1, "Assets", theme.title, theme.background);

        int listInnerY = contentY + 3;
        int listInnerH = contentH - 4;
        for (int i = 0; i < static_cast<int>(assets.size()) && i < listInnerH; ++i) {
            RGBColor fg = (i == selectedIndex) ? theme.focusFg : theme.itemFg;
            RGBColor bg = (i == selectedIndex) ? theme.focusBg : theme.panel;
            std::string name = assets[i].name;
            name = TuiUtils::trimToUtf8VisualWidth(name, static_cast<size_t>(listW - 4));
            surface.drawText(listX + 2, listInnerY + i, name, fg, bg);
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
        
        bool dialogRunning = true;
        input->start(); 

        while (dialogRunning) {
            // Render background (dimmed)
            drawMainUI();
            
            // Draw Dialog Box
            int w = surface.getWidth();
            int h = surface.getHeight();
            int dw = 40, dh = 12;
            int dx = (w - dw) / 2;
            int dy = (h - dh) / 2;
            
            // Modern border style
            BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.drawText(dx + 2, dy, " Import Settings ", theme.title, theme.panel);
            
            auto drawField = [&](int idx, const std::string& label, const std::string& val, int y) {
                RGBColor fg = (focusIdx == idx) ? theme.focusFg : theme.itemFg;
                RGBColor bg = (focusIdx == idx) ? theme.focusBg : theme.panel;
                surface.drawText(dx + 2, dy + y, label, theme.itemFg, theme.panel);
                surface.drawText(dx + 15, dy + y, " " + val + " ", fg, bg);
            };
            
            drawField(0, "Width:", widthStr, 3);
            drawField(1, "Quality:", (qualityIdx == 1 ? "High" : "Low"), 5);
            
            // Buttons
            RGBColor btnFg = (focusIdx == 2) ? theme.focusFg : theme.itemFg;
            RGBColor btnBg = (focusIdx == 2) ? theme.focusBg : theme.panel;
            surface.drawText(dx + 5, dy + 8, "[ Import ]", btnFg, btnBg);
            
            btnFg = (focusIdx == 3) ? theme.focusFg : theme.itemFg;
            btnBg = (focusIdx == 3) ? theme.focusBg : theme.panel;
            surface.drawText(dx + 20, dy + 8, "[ Cancel ]", btnFg, btnBg);
            
            painter.present(surface);
            
            // Input
            auto events = input->pollEvents();
            for (const auto& ev : events) {
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
                                
                                surface.drawText(dx + 2, dy + 10, "Converting...", theme.focusFg, theme.panel);
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
                            qualityIdx = 1 - qualityIdx; // Toggle 0/1
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        input->stop();
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
