#include "AssetManagerScreen.h"
#include "DirectoryBrowserScreen.h"
#include "ToggleSwitch.h"
#include "ProgressBar.h"
#include "TuiUtils.h"
#include "YuiEditorScreen.h"
#include "../ImgAssetsInfrastructure/ImageLoader.h"
#include "../Utils/EnvConfig.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
    const std::string kOpenBtnLabel = "[Open]";
    const std::string kRenameBtnLabel = "[Re]";
    const std::string kDeleteBtnLabel = "[Del]";
    const std::string kInfoBtnLabel = "[Inf]";
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

    static std::string toLowerCopy(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch){
            return static_cast<char>(std::tolower(ch));
        });
        return out;
    }

    AssetManagerScreen::AssetManagerScreen(const std::string& assetDir) 
        : manager(assetDir), 
          surface(80, 24), 
          painter(),
          input(std::make_unique<InputController>()),
          taskSystem(-1) // Use auto
    {
        lastImportPath = manager.getRootDir();
        refreshList();
        searchState.lastCaretToggle = std::chrono::steady_clock::now();
    }

    void AssetManagerScreen::show() {
        input->start();
        bool running = true;
        
        std::cout << "\x1b[?25l"; // Hide cursor

        while (running) {
            searchState.updateCaret();
            drawMainUI();
            
            if (ctxMenuState.visible) {
                ContextMenu::render(surface, ctxMenuItems, ctxMenuState);
            }

            painter.present(surface);
            
            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ctxMenuState.visible) {
                    bool requestClose = false;
                    int result = ContextMenu::handleInput(ev, ctxMenuItems, ctxMenuState, requestClose);
                    if (result >= 0) {
                        if (result == 0) openInEditor();
                        else if (result == 1) renameCurrentAsset();
                        else if (result == 2) deleteCurrentAsset();
                        else if (result == 3) {
                            if (!previewLoaded) loadPreview();
                            if (previewLoaded) showInfoDialog(getSelectedAssetName(), currentPreview);
                        }
                    }
                    if (requestClose) {
                        ctxMenuState.visible = false;
                    }
                    continue; 
                }

                if (ev.type == InputEvent::Type::Mouse) {
                    int filteredCount = static_cast<int>(filteredIndices.size());
                    if (filteredCount > 0 && ev.wheel != 0) {
                        int delta = (ev.wheel > 0) ? -1 : 1;
                        int maxIndex = std::max(0, filteredCount - 1);
                        selectedIndex = std::clamp(selectedIndex + delta, 0, maxIndex);
                        ensureSelectionVisible();
                        loadPreview();
                    }

                    bool onSearchField = (ev.y == searchFieldY && ev.x >= searchFieldX && ev.x < searchFieldX + searchFieldW);
                    searchState.hover = onSearchField;
                    if (ev.pressed && ev.button == 0) {
                        searchState.focused = onSearchField;
                    }

                    bool insideList = (ev.x >= listX && ev.x < listX + listW && ev.y >= listY && ev.y < listY + listH);
                    
                    // Handle Right Click for Context Menu
                    if (ev.pressed && ev.button == 2) {
                        if (insideList && filteredCount > 0) {
                            int row = listScrollOffset + (ev.y - listY);
                            if (row >= 0 && row < filteredCount) {
                                selectedIndex = row;
                                ensureSelectionVisible();
                                loadPreview();
                                
                                ctxMenuState.visible = true;
                                ctxMenuState.width = ContextMenu::calculateWidth(ctxMenuItems);
                                int menuH = static_cast<int>(ctxMenuItems.size()) + 2;
                                ctxMenuState.x = std::clamp(ev.x, 0, std::max(0, surface.getWidth() - ctxMenuState.width));
                                ctxMenuState.y = std::clamp(ev.y, 0, std::max(0, surface.getHeight() - menuH));
                                ctxMenuState.selectedIndex = 0;
                                continue;
                            }
                        }
                    }

                    hoverRow = -1;
                    hoverButton = HoverButton::None;
                    if (insideList && filteredCount > 0) {
                        int row = listScrollOffset + (ev.y - listY);
                        if (row >= 0 && row < filteredCount) {
                            hoverRow = row;
                            if (row != selectedIndex && ev.move) {
                                selectedIndex = row;
                                ensureSelectionVisible();
                                loadPreview();
                            }

                            bool onOpen = ev.x >= buttonOpenX && ev.x < buttonOpenX + static_cast<int>(kOpenBtnLabel.size());
                            bool onRename = ev.x >= buttonRenameX && ev.x < buttonRenameX + static_cast<int>(kRenameBtnLabel.size());
                            bool onDelete = ev.x >= buttonDeleteX && ev.x < buttonDeleteX + static_cast<int>(kDeleteBtnLabel.size());
                            bool onInfo = ev.x >= buttonInfoX && ev.x < buttonInfoX + static_cast<int>(kInfoBtnLabel.size());
                            if (onOpen) hoverButton = HoverButton::Open;
                            else if (onRename) hoverButton = HoverButton::Rename;
                            else if (onDelete) hoverButton = HoverButton::Delete;
                            else if (onInfo) hoverButton = HoverButton::Info;

                            if (ev.pressed) {
                                selectedIndex = row;
                                ensureSelectionVisible();
                                loadPreview();
                                if (onDelete) {
                                    deleteCurrentAsset();
                                } else if (onOpen) {
                                    openInEditor();
                                } else if (onRename) {
                                    renameCurrentAsset();
                                } else if (onInfo) {
                                    if (!previewLoaded) loadPreview();
                                    if (previewLoaded) showInfoDialog(getSelectedAssetName(), currentPreview);
                                }
                            }
                        }
                    } else {
                        hoverButton = HoverButton::None;
                        hoverRow = -1;
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (searchState.focused) {
                        // 此处不需要改变样式以及添加过滤器和转换回调
                        // 因此 handleInput() 使用默认构造的TextFieldStyle参数
                        if (TextField::handleInput(ev, searchQuery, searchState)) {
                            std::string prevName = getSelectedAssetName();
                            applyFilter(prevName);
                            continue;
                        }
                    }

                    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    } else if (ev.key == InputKey::ArrowUp) {
                        if (selectedIndex > 0) selectedIndex--;
                        ensureSelectionVisible();
                        loadPreview();
                    } else if (ev.key == InputKey::ArrowDown) {
                        if (selectedIndex < static_cast<int>(filteredIndices.size()) - 1) selectedIndex++;
                        ensureSelectionVisible();
                        loadPreview();
                    } else if (ev.key == InputKey::Character) {
                        if (ev.ch == 'i' || ev.ch == 'I') {
                            importAsset();
                        } else if (ev.ch == 'd' || ev.ch == 'D') {
                            deleteCurrentAsset();
                        } else if (ev.ch == 'r' || ev.ch == 'R') {
                            renameCurrentAsset();
                        } else if (ev.ch == 'o' || ev.ch == 'O') {
                            openInEditor();
                        }
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        input->stop();
        std::cout << "\x1b[?25h"; // Show cursor
    }

    void AssetManagerScreen::refreshList(const std::string& preferredSelection) {
        std::string desired = preferredSelection.empty() ? getSelectedAssetName() : preferredSelection;
        assets = manager.listAssets();
        applyFilter(desired);
    }

    void AssetManagerScreen::applyFilter(const std::string& preferredSelection) {
        filteredIndices.clear();
        std::string needle = toLowerCopy(searchQuery);

        for (size_t i = 0; i < assets.size(); ++i) {
            std::string hay = toLowerCopy(assets[i].name);
            if (needle.empty() || hay.find(needle) != std::string::npos) {
                filteredIndices.push_back(static_cast<int>(i));
            }
        }

        if (filteredIndices.empty()) {
            selectedIndex = 0;
            previewLoaded = false;
            lastPreviewName.clear();
            hoverRow = -1;
            hoverButton = HoverButton::None;
            listScrollOffset = 0;
            return;
        }

        hoverRow = -1;
        hoverButton = HoverButton::None;
        listScrollOffset = 0;

        if (!preferredSelection.empty()) {
            for (size_t i = 0; i < filteredIndices.size(); ++i) {
                if (assets[filteredIndices[i]].name == preferredSelection) {
                    selectedIndex = static_cast<int>(i);
                    loadPreview();
                    ensureSelectionVisible();
                    return;
                }
            }
        }

        selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(filteredIndices.size()) - 1);
        ensureSelectionVisible();
        loadPreview();
    }

    void AssetManagerScreen::ensureSelectionVisible() {
        int total = static_cast<int>(filteredIndices.size());
        selectedIndex = std::clamp(selectedIndex, 0, std::max(0, total - 1));
        int visible = std::max(1, listH);
        int maxOffset = std::max(0, total - visible);
        listScrollOffset = std::clamp(listScrollOffset, 0, maxOffset);
        if (selectedIndex < listScrollOffset) {
            listScrollOffset = selectedIndex;
        } else if (selectedIndex >= listScrollOffset + visible) {
            listScrollOffset = selectedIndex - visible + 1;
            listScrollOffset = std::clamp(listScrollOffset, 0, maxOffset);
        }
    }

    std::string AssetManagerScreen::getSelectedAssetName() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(filteredIndices.size())) return "";
        int assetIdx = filteredIndices[selectedIndex];
        if (assetIdx < 0 || assetIdx >= static_cast<int>(assets.size())) return "";
        return assets[assetIdx].name;
    }

    bool AssetManagerScreen::isValidAssetName(const std::string& name) {
        if (name.empty()) return false;
        for (char ch : name) {
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) {
                return false;
            }
        }
        return true;
    }

    void AssetManagerScreen::loadPreview() {
        if (filteredIndices.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(filteredIndices.size())) {
            previewLoaded = false;
            lastPreviewName.clear();
            return;
        }
        
        int assetIdx = filteredIndices[selectedIndex];
        if (assetIdx < 0 || assetIdx >= static_cast<int>(assets.size())) {
            previewLoaded = false;
            lastPreviewName.clear();
            return;
        }
        
        const auto& entry = assets[assetIdx];
        if (!previewLoaded || entry.name != lastPreviewName) {
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
        std::vector<std::string> paths = browser.show();
        
        if (!paths.empty()) {
            std::filesystem::path p(paths[0]);
            lastImportPath = p.parent_path().string(); // Remember path

            std::vector<std::string> imagePaths;
            for (const auto& path : paths) {
                std::string ext = std::filesystem::path(path).extension().string();
                std::string lowerExt = ext;
                std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                if (lowerExt == ".png" || lowerExt == ".jpg" || lowerExt == ".jpeg" || lowerExt == ".bmp" || lowerExt == ".tga") {
                    imagePaths.push_back(path);
                }
            }

            if (!imagePaths.empty()) {
                showImportDialog(imagePaths);
            }
        }
        
        input->start();
    }

    void AssetManagerScreen::deleteCurrentAsset() {
        std::string name = getSelectedAssetName();
        if (name.empty()) return;
        if (!skipDeleteConfirm) {
            if (!showDeleteConfirmDialog(name)) return;
        }
        manager.deleteAsset(name);
        refreshList();
    }

    void AssetManagerScreen::renameCurrentAsset() {
        std::string currentName = getSelectedAssetName();
        if (currentName.empty()) return;

        std::string newName;
        if (!showRenameDialog(currentName, newName)) return;
        if (newName == currentName) return;

        if (!manager.renameAsset(currentName, newName)) {
            refreshList(currentName);
            return;
        }

        refreshList(newName);
    }

    void AssetManagerScreen::openInEditor() {
        std::string name = getSelectedAssetName();
        if (name.empty()) return;
        input->stop();
        ImageAsset asset = manager.loadAsset(name);
        YuiEditorScreen editor(manager, name, asset);
        editor.show();
        refreshList(name);
        // Recreate input controller to avoid any lingering console mode state
        input.reset(); // ensure the previous controller restores before enabling a new one
        input = std::make_unique<InputController>();
        hoverButton = HoverButton::None;
        hoverRow = -1;
        input->start();
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
        surface.drawText(2, 3, "Up/Down: select | I: import | R: rename | D: delete | Q: back", theme.hintFg, theme.background);

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

        // Search row
        std::string searchLabel = "Search:";
        surface.fillRect(listInnerX, listInnerY, listInnerW, 1, theme.itemFg, theme.panel, " ");
        surface.drawText(listInnerX + 1, listInnerY, searchLabel, theme.itemFg, theme.panel);
        searchFieldX = listInnerX + 2 + static_cast<int>(searchLabel.size());
        searchFieldY = listInnerY;
        searchFieldW = std::max(10, listInnerW - (searchFieldX - listInnerX) - 1);
        
        TextFieldStyle searchStyle;
        searchStyle.width = searchFieldW;
        searchStyle.placeholder = "type to filter";
        searchStyle.focusFg = theme.focusFg;
        searchStyle.focusBg = theme.focusBg;
        searchStyle.panelBg = theme.panel;
        searchStyle.hintFg = theme.hintFg;
        
        TextField::render(surface, searchFieldX, searchFieldY, searchQuery, searchState, searchStyle);

        int rowsStartY = listInnerY + 2;
        listX = listInnerX;
        listY = rowsStartY;
        listW = listInnerW;
        listH = std::max(0, listInnerH - 2);
        ensureSelectionVisible();

        int buttonsWidth = static_cast<int>(kOpenBtnLabel.size() + 1 + kRenameBtnLabel.size() + 1 + kDeleteBtnLabel.size() + 1 + kInfoBtnLabel.size());
        int buttonsStart = std::max(listInnerX + 4, listInnerX + listInnerW - buttonsWidth);
        buttonOpenX = buttonsStart;
        buttonRenameX = buttonOpenX + static_cast<int>(kOpenBtnLabel.size()) + 1;
        buttonDeleteX = buttonRenameX + static_cast<int>(kRenameBtnLabel.size()) + 1;
        buttonInfoX = buttonDeleteX + static_cast<int>(kDeleteBtnLabel.size()) + 1;

        RGBColor white{255,255,255};
        RGBColor black{0,0,0};
        int totalRows = static_cast<int>(filteredIndices.size());
        int visibleRows = std::min(totalRows, listH);
        int start = std::min(listScrollOffset, std::max(0, totalRows - visibleRows));
        for (int i = 0; i < visibleRows; ++i) {
            int rowIndex = start + i;
            int rowY = listY + i;
            bool focused = (rowIndex == selectedIndex);
            RGBColor fg = focused ? black : theme.itemFg;
            RGBColor bg = focused ? white : theme.panel;
            surface.fillRect(listInnerX, rowY, listInnerW, 1, fg, bg, " ");

            int assetIdx = filteredIndices[rowIndex];
            int textLimit = std::max(0, buttonOpenX - (listInnerX + 1) - 1);
            std::string name = assets[assetIdx].name;
            name = TuiUtils::trimToUtf8VisualWidth(name, static_cast<size_t>(std::max(0, textLimit)));
            surface.drawText(listInnerX + 1, rowY, name, fg, bg);

            if (focused) {
                auto btnColor = [&](HoverButton hb) {
                    bool hot = (hoverRow == rowIndex && hoverButton == hb);
                    RGBColor baseBg = theme.accent;
                    RGBColor bbg = hot ? darken(baseBg, 0.6) : baseBg;
                    RGBColor bfg = hot ? RGBColor{255,255,255} : theme.title;
                    return std::make_pair(bfg, bbg);
                };
                auto [fgOpen, bgOpen] = btnColor(HoverButton::Open);
                auto [fgRename, bgRename] = btnColor(HoverButton::Rename);
                auto [fgDel, bgDel] = btnColor(HoverButton::Delete);
                auto [fgInfo, bgInfo] = btnColor(HoverButton::Info);
                surface.drawText(buttonOpenX, rowY, kOpenBtnLabel, fgOpen, bgOpen);
                surface.drawText(buttonRenameX, rowY, kRenameBtnLabel, fgRename, bgRename);
                surface.drawText(buttonDeleteX, rowY, kDeleteBtnLabel, fgDel, bgDel);
                surface.drawText(buttonInfoX, rowY, kInfoBtnLabel, fgInfo, bgInfo);
            }
        }

        if (visibleRows == 0) {
            surface.drawText(listInnerX + 1, listY, searchQuery.empty() ? "No assets found" : "No assets match filter", theme.hintFg, theme.panel);
        }

        // Preview panel
        surface.fillRect(prevX, contentY, prevW, contentH, theme.itemFg, theme.panel, " ");
        surface.drawFrame(prevX, contentY, prevW, contentH, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(prevX + 1, contentY + 1, prevW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(prevX + 2, contentY + 1, "Preview", theme.title, theme.background);

        if (previewLoaded) {
            drawPreview(prevX + 2, contentY + 3, prevW - 4, contentH - 5);
        } else {
            std::string previewMsg = filteredIndices.empty() ? "No assets to preview" : "No preview available";
            surface.drawText(prevX + 2, contentY + 3, previewMsg, theme.hintFg, theme.panel);
        }
    }

    bool AssetManagerScreen::showRenameDialog(const std::string& currentName, std::string& outName) {
        outName = currentName;
        bool running = true;
        bool confirmed = false;
        bool hoverOk = false, hoverCancel = false;
        bool fieldFocused = true;
        std::string errorMsg;
        int mouseX = -1, mouseY = -1;
        bool caretOn = true;
        auto caretLastToggle = std::chrono::steady_clock::now();

        int w = surface.getWidth();
        int h = surface.getHeight();
        int dw = 50;
        int dh = 10;
        int dx = (w - dw) / 2;
        int dy = (h - dh) / 2;
        BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
        auto clampDialog = [&]() {
            w = surface.getWidth();
            h = surface.getHeight();
            dx = std::clamp(dx, 0, std::max(0, w - dw));
            dy = std::clamp(dy, 0, std::max(0, h - dh));
        };
        clampDialog();
        bool dragging = false;
        int dragStartX = 0, dragStartY = 0;
        int dragOriginX = 0, dragOriginY = 0;

        while (running) {
            auto now = std::chrono::steady_clock::now();
            if (now - caretLastToggle >= std::chrono::milliseconds(500)) {
                caretOn = !caretOn;
                caretLastToggle = now;
            }
            clampDialog();
            drawMainUI();

            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Rename Asset", theme.title, theme.background);
            surface.drawText(dx + 2, dy + 2, "Current: " + currentName, theme.hintFg, theme.panel);

            int fieldX = dx + 2;
            int fieldY = dy + 4;
            int fieldW = dw - 4;
            RGBColor fg = fieldFocused ? theme.focusFg : theme.itemFg;
            RGBColor bg = fieldFocused ? theme.focusBg : theme.panel;
            surface.fillRect(fieldX, fieldY, fieldW, 1, fg, bg, " ");
            std::string displayName = outName;
            int maxChars = std::max(0, fieldW - 2);
            if (static_cast<int>(displayName.size()) > maxChars) {
                displayName = displayName.substr(displayName.size() - maxChars);
            }
            if (fieldFocused && caretOn) displayName.push_back('|');
            if (static_cast<int>(displayName.size()) > maxChars) {
                displayName = displayName.substr(displayName.size() - maxChars);
            }
            surface.drawText(fieldX + 1, fieldY, displayName, fg, bg);

            if (!errorMsg.empty()) {
                surface.drawText(dx + 2, fieldY + 2, errorMsg, theme.hintFg, theme.panel);
            }

            std::string okLbl = "[ OK ]";
            std::string cancelLbl = "[ Cancel ]";
            int okX = dx + (dw / 2) - static_cast<int>(okLbl.size()) - 1;
            int cancelX = dx + (dw / 2) + 1;
            int btnY = dy + dh - 2;
            auto drawBtn = [&](const std::string& lbl, int x, bool hot){
                RGBColor baseBg = theme.accent;
                RGBColor bgBtn = hot ? darken(baseBg, 0.6) : baseBg;
                RGBColor fgBtn = hot ? RGBColor{255,255,255} : theme.title;
                surface.drawText(x, btnY, lbl, fgBtn, bgBtn);
            };
            drawBtn(okLbl, okX, hoverOk);
            drawBtn(cancelLbl, cancelX, hoverCancel);

            auto updateHover = [&]() {
                if (mouseX < 0 || mouseY < 0) {
                    hoverOk = hoverCancel = false;
                    return;
                }
                hoverOk = (mouseX >= okX && mouseX < okX + static_cast<int>(okLbl.size()) && mouseY == btnY);
                hoverCancel = (mouseX >= cancelX && mouseX < cancelX + static_cast<int>(cancelLbl.size()) && mouseY == btnY);
            };
            updateHover();

            painter.present(surface);

            auto tryConfirm = [&]() {
                std::string trimmed = outName;
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
                outName = trimmed;

                if (!isValidAssetName(outName)) {
                    errorMsg = "Use letters, numbers, - or _";
                    return;
                }

                bool nameExists = std::any_of(assets.begin(), assets.end(), [&](const auto& e){
                    return e.name == outName && e.name != currentName;
                });
                if (nameExists) {
                    errorMsg = "Name already exists";
                    return;
                }

                errorMsg.clear();
                confirmed = true;
                running = false;
            };

            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;
                    if (dragging) {
                        dx = dragOriginX + (ev.x - dragStartX);
                        dy = dragOriginY + (ev.y - dragStartY);
                        clampDialog();
                    }
                    bool onField = (ev.y == fieldY && ev.x >= fieldX && ev.x < fieldX + fieldW);
                    bool onOk = (ev.x >= okX && ev.x < okX + static_cast<int>(okLbl.size()) && ev.y == btnY);
                    bool onCancel = (ev.x >= cancelX && ev.x < cancelX + static_cast<int>(cancelLbl.size()) && ev.y == btnY);
                    bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1);
                    if (ev.pressed && ev.button == 0) {
                        if (onTitle) {
                            dragging = true;
                            dragStartX = ev.x;
                            dragStartY = ev.y;
                            dragOriginX = dx;
                            dragOriginY = dy;
                        }
                        fieldFocused = onField;
                        if (onOk) {
                            tryConfirm();
                        } else if (onCancel) {
                            running = false;
                        }
                    }
                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false;
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    } else if (ev.key == InputKey::Enter) {
                        tryConfirm();
                    } else if (ev.key == InputKey::Character && fieldFocused) {
                        if (ev.ch == '\b') {
                            if (!outName.empty()) outName.pop_back();
                        } else if (std::isprint(static_cast<unsigned char>(ev.ch))) {
                            outName.push_back(ev.ch);
                        }
                    }
                }
            }
            updateHover();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        return confirmed;
    }

    bool AssetManagerScreen::showDeleteConfirmDialog(const std::string& assetName) {
        bool running = true;
        bool confirmed = false;
        bool hoverDelete = false, hoverCancel = false;
        int mouseX = -1, mouseY = -1;
        bool dontAskAgain = false;

        int w = surface.getWidth();
        int h = surface.getHeight();
        int dw = 54;
        int dh = 11;
        int dx = (w - dw) / 2;
        int dy = (h - dh) / 2;
        BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
        auto clampDialog = [&]() {
            w = surface.getWidth();
            h = surface.getHeight();
            dx = std::clamp(dx, 0, std::max(0, w - dw));
            dy = std::clamp(dy, 0, std::max(0, h - dh));
        };
        clampDialog();
        bool dragging = false;
        int dragStartX = 0, dragStartY = 0;
        int dragOriginX = 0, dragOriginY = 0;

        while (running) {
            clampDialog();
            drawMainUI();

            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Confirm Delete", theme.title, theme.background);
            surface.drawText(dx + 2, dy + 3, "Delete asset:", theme.itemFg, theme.panel);
            surface.drawText(dx + 2, dy + 4, "  " + assetName, theme.hintFg, theme.panel);

            std::string checkbox = dontAskAgain ? "[x] Don't ask again (session)" : "[ ] Don't ask again (session)";
            int checkY = dy + 6;
            int checkX = dx + 2;
            surface.drawText(checkX, checkY, checkbox, theme.itemFg, theme.panel);

            std::string delLbl = "[ Delete ]";
            std::string cancelLbl = "[ Cancel ]";
            int delX = dx + 6;
            int cancelX = dx + dw - static_cast<int>(cancelLbl.size()) - 6;
            int btnY = dy + dh - 2;
            auto drawBtn = [&](const std::string& lbl, int x, bool hot){
                RGBColor baseBg = theme.accent;
                RGBColor bg = hot ? darken(baseBg, 0.6) : baseBg;
                RGBColor fg = hot ? RGBColor{255,255,255} : theme.title;
                surface.drawText(x, btnY, lbl, fg, bg);
            };
            drawBtn(delLbl, delX, hoverDelete);
            drawBtn(cancelLbl, cancelX, hoverCancel);

            auto updateHover = [&](){
                if (mouseX < 0 || mouseY < 0) {
                    hoverDelete = hoverCancel = false;
                    return;
                }
                hoverDelete = (mouseX >= delX && mouseX < delX + static_cast<int>(delLbl.size()) && mouseY == btnY);
                hoverCancel = (mouseX >= cancelX && mouseX < cancelX + static_cast<int>(cancelLbl.size()) && mouseY == btnY);
            };
            updateHover();

            painter.present(surface);

            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    } else if (ev.key == InputKey::Enter) {
                        confirmed = true;
                        running = false;
                    } else if (ev.key == InputKey::Character && (ev.ch == ' ')) {
                        dontAskAgain = !dontAskAgain;
                    }
                } else if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;
                    if (dragging) {
                        dx = dragOriginX + (ev.x - dragStartX);
                        dy = dragOriginY + (ev.y - dragStartY);
                        clampDialog();
                    }
                    bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1);
                    bool onCheckbox = (ev.y == checkY && ev.x >= checkX && ev.x < checkX + static_cast<int>(checkbox.size()));
                    bool onDel = (ev.x >= delX && ev.x < delX + static_cast<int>(delLbl.size()) && ev.y == btnY);
                    bool onCancel = (ev.x >= cancelX && ev.x < cancelX + static_cast<int>(cancelLbl.size()) && ev.y == btnY);
                    if (ev.pressed && ev.button == 0) {
                        if (onTitle) {
                            dragging = true;
                            dragStartX = ev.x;
                            dragStartY = ev.y;
                            dragOriginX = dx;
                            dragOriginY = dy;
                        }
                        if (onCheckbox) {
                            dontAskAgain = !dontAskAgain;
                        }
                        if (onDel) {
                            confirmed = true;
                            running = false;
                        } else if (onCancel) {
                            running = false;
                        }
                    }
                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false;
                    }
                }
            }
            updateHover();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        if (confirmed && dontAskAgain) {
            skipDeleteConfirm = true;
        }
        return confirmed;
    }

    void AssetManagerScreen::showImportDialog(const std::vector<std::string>& filePaths) {
        if (filePaths.empty()) return;
        input->start(); 

        EnvConfig::getInstance().refresh();
        int defaultW = std::max(10, EnvConfig::getInstance().getRuntimeInfo().consoleCols - 6);
        std::string widthStr = std::to_string(defaultW);
        int qualityIdx = 1; // 0: Low, 1: High
        int focusIdx = 0; // 0:W, 1:Quality, 2:Import, 3:Cancel
        bool hoverImport = false, hoverCancel = false;
        ToggleSwitchState qualityToggleState{};
        
        bool dialogRunning = true;
        bool isImporting = false;
        double totalPct = 0.0;
        double itemPct = 0.0;
        int currentFileIdx = 0;
        int totalFileCount = static_cast<int>(filePaths.size());
        std::string currentItemName;
        std::string currentStage = "Starting...";
        auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
        std::future<void> importFuture;

        int mouseX = -1;
        int mouseY = -1;

        int dw = 48, dh = 13;
        int dx = (surface.getWidth() - dw) / 2;
        int dy = (surface.getHeight() - dh) / 2;
        bool dragging = false;
        int dragStartX = 0, dragStartY = 0;
        int dragOriginX = 0, dragOriginY = 0;

        auto clampDialog = [&]() {
            dx = std::clamp(dx, 0, surface.getWidth() - dw);
            dy = std::clamp(dy, 0, surface.getHeight() - dh);
        };

        auto startImport = [&]() {
            isImporting = true;
            cancelFlag->store(false);
            totalPct = 0.0;
            itemPct = 0.0;
            currentFileIdx = 0;
            
            // Capture needed state by value
            importFuture = std::async(std::launch::async, 
                [this, filePaths, widthStr, qualityIdx, defaultW, cancelFlag, &totalPct, &itemPct, &currentItemName, &currentStage, &currentFileIdx]() {
                int total = static_cast<int>(filePaths.size());
                for (int i = 0; i < total; ++i) {
                    if (cancelFlag->load()) break;
                    currentFileIdx = i + 1;
                    const auto& filePath = filePaths[i];
                    currentItemName = std::filesystem::path(filePath).filename().string();
                    
                    try {
                        RawImage raw = ImageLoader::load(filePath);
                        if (raw.valid) {
                            int tw = defaultW;
                            if (!widthStr.empty() && widthStr.back() == '%') {
                                try {
                                    double pct = std::stod(widthStr.substr(0, widthStr.size() - 1)) / 100.0;
                                    tw = std::max(1, static_cast<int>(std::round(raw.width * pct)));
                                } catch (...) {}
                            } else {
                                try { tw = std::stoi(widthStr); } catch (...) {}
                            }

                            AdvancedImageConverter::Options opts;
                            opts.targetWidth = tw;
                            double aspect = 0.5;
                            opts.targetHeight = std::max(1, (int)std::round((double)raw.height * tw * aspect / raw.width));
                            opts.quality = (qualityIdx == 1) ? AdvancedImageConverter::Options::Quality::High : AdvancedImageConverter::Options::Quality::Low;
                            
                            opts.onProgress = [&](double completed, double totalWork, const std::string& stage) {
                                if (cancelFlag->load()) return;
                                itemPct = std::clamp(completed / totalWork, 0.0, 1.0);
                                totalPct = std::clamp((i + itemPct) / total, 0.0, 1.0);
                                currentStage = stage;
                            };

                            AdvancedImageConverter converter;
                            ImageAsset asset = converter.convert(raw, opts, taskSystem, cancelFlag.get());
                            
                            if (!cancelFlag->load()) {
                                std::string name = std::filesystem::path(filePath).stem().string();
                                manager.saveAsset(asset, name);
                            }
                        }
                    } catch (...) {}
                }
            });
        };
   
        while (dialogRunning) {
            if (isImporting && importFuture.valid()) {
                if (importFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    importFuture.get(); 
                    isImporting = false;
                    refreshList();
                    dialogRunning = false;
                }
            }

            // Render background (dimmed)
            drawMainUI();
            
            // Draw Dialog Box
            clampDialog();
            
            BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            
            if (!isImporting) {
                std::string title = (filePaths.size() > 1) 
                    ? "Batch Import (" + std::to_string(filePaths.size()) + ")" 
                    : "Import Settings";
                surface.drawText(dx + 2, dy + 1, title, theme.title, theme.background);

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
                    bool active = hot || focus;
                    RGBColor bg = active ? darken(baseBg, 0.6) : baseBg;
                    RGBColor fg = active ? RGBColor{255,255,255} : theme.title;
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
            } else {
                // Progress UI
                surface.drawText(dx + 2, dy + 1, "Importing Assets...", theme.title, theme.background);

                // Cancel button with Red Highlight (moved right by 1 more to -10)
                bool cancelHover = (mouseX >= dx + dw - 10 && mouseX < dx + dw - 2 && mouseY == dy + 1);
                RGBColor cFg = cancelHover ? theme.title : theme.accent;
                RGBColor cBg = cancelHover ? RGBColor{255, 0, 0} : theme.background;
                surface.drawText(dx + dw - 10, dy + 1, "[Cancel]", cFg, cBg);

                ProgressBarStyle pstyle;
                pstyle.width = dw - 11;
                pstyle.fillFg = theme.accent;
                pstyle.fillBg = darken(theme.panel, 0.8);
                pstyle.showPercentage = true;

                std::string totalTitle = "Total Progress (" + std::to_string(currentFileIdx) + "/" + std::to_string(totalFileCount) + "):";
                surface.drawText(dx + 2, dy + 6, totalTitle, theme.title, theme.panel);
                ProgressBar::render(surface, dx + 2, dy + 7, totalPct, pstyle);

                int maxNameW = dw - 24; 
                std::string truncatedName = TuiUtils::trimToUtf8VisualWidth(currentItemName, maxNameW);
                std::string status = "Item: " + truncatedName + " (" + currentStage + ")";
                surface.drawText(dx + 2, dy + 9, status, theme.title, theme.panel);
                ProgressBar::render(surface, dx + 2, dy + 10, itemPct, pstyle);
            }
            
            painter.present(surface);
            
            // Input
            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;

                    bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1);
                    if (ev.button == 0 && ev.pressed && onTitle) {
                        dragging = true;
                        dragStartX = ev.x;
                        dragStartY = ev.y;
                        dragOriginX = dx;
                        dragOriginY = dy;
                    }

                    if (dragging) {
                        dx = dragOriginX + (ev.x - dragStartX);
                        dy = dragOriginY + (ev.y - dragStartY);
                        clampDialog();
                    }

                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false;
                    }

                    if (!isImporting) {
                        int importLblSize = 10; // "[ Import ]"
                        int cancelLblSize = 10; // "[ Cancel ]"
                        int btnY = dy + dh - 3;
                        int importX = dx + 6;
                        int cancelX = dx + dw - cancelLblSize - 6;

                        bool onImport = (ev.x >= importX && ev.x < importX + importLblSize && ev.y == btnY);
                        bool onCancel = (ev.x >= cancelX && ev.x < cancelX + cancelLblSize && ev.y == btnY);
                        
                        int labelWidth = 8;
                        int fieldX = dx + labelWidth + 6;
                        bool onWidth = (ev.y == dy + 3 && ev.x >= fieldX && ev.x < fieldX + static_cast<int>(widthStr.size()) + 2);
                        int lineY = dy + 4;
                        int toggleX = fieldX;
                        bool onToggle = (ev.y == lineY && ev.x >= toggleX && ev.x < toggleX + 11);

                        if (ev.button == 0 && ev.pressed) {
                            if (onWidth) {
                                focusIdx = 0;
                            } else if (onToggle) {
                                focusIdx = 1;
                                bool wasOn = (qualityIdx == 1);
                                qualityIdx = 1 - qualityIdx;
                                qualityToggleState.previousOn = wasOn;
                                qualityToggleState.lastToggle = std::chrono::steady_clock::now();
                            } else if (onImport) {
                                focusIdx = 2;
                                startImport();
                            } else if (onCancel) {
                                focusIdx = 3;
                                dialogRunning = false;
                            }
                        }
                    } else {
                        // Progress state mouse
                        bool onProgressCancel = (mouseX >= dx + dw - 10 && mouseX < dx + dw - 2 && mouseY == dy + 1);
                        if (ev.button == 0 && ev.pressed && onProgressCancel) {
                            cancelFlag->store(true);
                        }
                    }
                }

                if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        if (isImporting) {
                            cancelFlag->store(true);
                        } else {
                            dialogRunning = false;
                        }
                    } else if (!isImporting) {
                        if (ev.key == InputKey::Tab) {
                            focusIdx = (focusIdx + 1) % 4;
                        } else if (ev.key == InputKey::ArrowUp) {
                            focusIdx = (focusIdx - 1 + 4) % 4;
                        } else if (ev.key == InputKey::ArrowDown) {
                            focusIdx = (focusIdx + 1) % 4;
                        } else if (ev.key == InputKey::Enter) {
                            if (focusIdx == 2) { 
                                startImport();
                            } else if (focusIdx == 3) {
                                dialogRunning = false;
                            }
                        } else if (ev.key == InputKey::Character && ev.ch == '\b') {
                            if (focusIdx == 0 && !widthStr.empty()) widthStr.pop_back();
                        } else if (ev.key == InputKey::Character) {
                            if (isdigit(ev.ch) || ev.ch == '%') {
                                if (focusIdx == 0) widthStr += ev.ch;
                            }
                        } else if (ev.key == InputKey::ArrowLeft || ev.key == InputKey::ArrowRight) {
                            if (focusIdx == 1) {
                                bool wasOn = (qualityIdx == 1);
                                qualityIdx = 1 - qualityIdx;
                                qualityToggleState.previousOn = wasOn;
                                qualityToggleState.lastToggle = std::chrono::steady_clock::now();
                            }
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        input->stop();
    }

void AssetManagerScreen::showInfoDialog(const std::string& assetName, const ImageAsset& asset) {
    int w = surface.getWidth();
    int h = surface.getHeight();
    std::vector<std::string> lines;
    lines.push_back("Resource: " + assetName);
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
        int totalCells = wcells * hcells;
        lines.push_back("Cells: " + std::to_string(totalCells));
        lines.push_back("Unique glyphs: " + std::to_string((int)glyphs.size()));
        lines.push_back("Unique foreground colors: " + std::to_string((int)fgset.size()));
        lines.push_back("Unique background colors: " + std::to_string((int)bgset.size()));

        // glyphs by frequency (no ellipsis unless dialog would overflow screen)
        std::vector<std::pair<std::string,int>> glyphVec(glyphCount.begin(), glyphCount.end());
        std::sort(glyphVec.begin(), glyphVec.end(), [](const auto& a, const auto& b){
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        std::vector<std::string> glyphLines;
        glyphLines.reserve(glyphVec.size() + 1);
        glyphLines.push_back("Top glyphs (by cells):");
        for (const auto& kv : glyphVec) {
            double pct = totalCells > 0 ? (static_cast<double>(kv.second) * 100.0 / static_cast<double>(totalCells)) : 0.0;
            std::ostringstream ss; ss << "  '" << kv.first << "' x " << kv.second << " (" << std::fixed << std::setprecision(1) << pct << "%)";
            glyphLines.push_back(ss.str());
        }

        int maxDh = std::max(7, h - 4);
        int neededDhAll = static_cast<int>(lines.size() + glyphLines.size()) + 7;
        if (neededDhAll <= maxDh) {
            lines.insert(lines.end(), glyphLines.begin(), glyphLines.end());
        } else {
            int availableGlyphLines = std::max(0, maxDh - 7 - static_cast<int>(lines.size()));
            if (availableGlyphLines > 0) {
                int take = std::min(static_cast<int>(glyphLines.size()), availableGlyphLines);
                lines.insert(lines.end(), glyphLines.begin(), glyphLines.begin() + take);
                if (static_cast<int>(glyphLines.size()) > take) {
                    int remaining = static_cast<int>(glyphLines.size()) - take;
                    lines.push_back("  ..." + std::to_string(remaining) + " more glyphs");
                }
            }
        }
    }

    int dw = std::min(64, w - 6);
    int dh = std::min(h - 4, static_cast<int>(lines.size()) + 7);
    int dx = (w - dw) / 2;
    int dy = (h - dh) / 2;
    BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};

    auto clampDialog = [&]() {
        dx = std::clamp(dx, 0, std::max(0, w - dw));
        dy = std::clamp(dy, 0, std::max(0, h - dh));
    };
    clampDialog();

    bool running = true;
    bool hoverOk = false, hoverClose = false;
    int mouseX = -1;
    int mouseY = -1;
    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragOriginX = 0, dragOriginY = 0;
    while (running) {
        clampDialog();
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
            RGBColor bg = hot ? darken(baseBg, 0.6) : baseBg;
            RGBColor fg = hot ? RGBColor{255,255,255} : theme.title;
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
                if (ev.key == InputKey::Enter || (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q'))) {
                    running = false; break;
                }
            } else if (ev.type == InputEvent::Type::Mouse) {
                mouseX = ev.x;
                mouseY = ev.y;
                if (dragging) {
                    dx = dragOriginX + (ev.x - dragStartX);
                    dy = dragOriginY + (ev.y - dragStartY);
                    clampDialog();
                }
                bool onOk = (ev.x >= okx && ev.x < okx + static_cast<int>(ok.size()) && ev.y == btnY);
                bool onClose = (ev.x >= closex && ev.x < closex + static_cast<int>(close.size()) && ev.y == btnY);
                bool onTitle = (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1);
                if (ev.button == 0 && ev.pressed) {
                    if (onTitle) {
                        dragging = true;
                        dragStartX = ev.x;
                        dragStartY = ev.y;
                        dragOriginX = dx;
                        dragOriginY = dy;
                    }
                    if (onOk || onClose) { running = false; break; }
                }
                if (ev.button == 0 && !ev.pressed && !ev.move) {
                    dragging = false;
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
