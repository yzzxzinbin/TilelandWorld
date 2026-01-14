#include "AssetManagerScreen.h"
#include "DirectoryBrowserScreen.h"
#include "ToggleSwitch.h"
#include "ProgressBar.h"
#include "TuiUtils.h"
#include "YuiEditorScreen.h"
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
    const std::string kNewFolderBtnLabel = "[+Folder]";
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
                std::vector<std::string> currentItems = ctxMenuItems;
                if (selectedIndex >= 0 && selectedIndex < (int)displayList.size()) {
                    if (displayList[selectedIndex].type == ListItem::Folder) {
                        bool isCollapsed = collapsedFolders.count(displayList[selectedIndex].name);
                        currentItems = {isCollapsed ? "Expand" : "Collapse", "Rename", "Delete", "Move to...", "Move in..."};
                    }
                }
                ContextMenu::render(surface, currentItems, ctxMenuState);
            }

            painter.present(surface);
            
            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (ctxMenuState.visible) {
                    bool requestClose = false;
                    std::vector<std::string> currentItems = ctxMenuItems;
                    bool isFolder = false;
                    if (selectedIndex >= 0 && selectedIndex < (int)displayList.size()) {
                        if (displayList[selectedIndex].type == ListItem::Folder) {
                            isFolder = true;
                            bool isCollapsed = collapsedFolders.count(displayList[selectedIndex].name);
                            currentItems = {isCollapsed ? "Expand" : "Collapse", "Rename", "Delete", "Move to...", "Move in..."};
                        }
                    }

                    int result = ContextMenu::handleInput(ev, currentItems, ctxMenuState, requestClose);
                    if (result >= 0) {
                        if (isFolder) {
                            if (result == 0) {
                                std::string fName = displayList[selectedIndex].name;
                                if (collapsedFolders.count(fName)) collapsedFolders.erase(fName);
                                else collapsedFolders.insert(fName);
                                applyFilter(fName);
                            }
                            else if (result == 1) renameCurrent();
                            else if (result == 2) deleteCurrentAssetOrFolder();
                            else if (result == 3) moveCurrentAsset();
                            else if (result == 4) moveInFromSystem();
                        } else {
                            if (result == 0) openInEditor();
                            else if (result == 1) renameCurrent();
                            else if (result == 2) deleteCurrentAssetOrFolder();
                            else if (result == 3) {
                                if (!previewLoaded) loadPreview();
                                if (previewLoaded) showInfoDialog(getSelectedAssetName(), currentPreview);
                            } else if (result == 4) {
                                moveCurrentAsset();
                            } else if (result == 5) {
                                moveInFromSystem();
                            }
                        }
                    }

                    if (requestClose) {
                        ctxMenuState.visible = false;
                    }
                    continue; 
                }

                // Let TextField handle its events first
                if (TextField::handleInput(ev, searchQuery, searchState, {searchFieldW})) {
                    if (ev.type == InputEvent::Type::Key) {
                        std::string prevName = getSelectedAssetName();
                        applyFilter(prevName);
                    }
                    continue; 
                }

                if (ev.type == InputEvent::Type::Mouse) {
                    int listCount = static_cast<int>(displayList.size());
                    if (listCount > 0 && ev.wheel != 0) {
                        int delta = (ev.wheel > 0) ? -1 : 1;
                        int maxIndex = std::max(0, listCount - 1);
                        selectedIndex = std::clamp(selectedIndex + delta, 0, maxIndex);
                        ensureSelectionVisible();
                        loadPreview();
                    }

                    bool onNewFolderBtn = (ev.y == btnNewFolderY && ev.x >= btnNewFolderX && ev.x < btnNewFolderX + btnNewFolderW);

                    if (ev.pressed && ev.button == 0) {
                        if (onNewFolderBtn) {
                            createNewFolder();
                        }
                    }

                    bool insideList = (ev.x >= listX && ev.x < listX + listW && ev.y >= listY && ev.y < listY + listH);
                    
                    // Handle Right Click for Context Menu
                    if (ev.pressed && ev.button == 2) {
                        if (insideList && listCount > 0) {
                            int row = listScrollOffset + (ev.y - listY);
                            if (row >= 0 && row < listCount) {
                                selectedIndex = row;
                                ensureSelectionVisible();
                                loadPreview();
                                
                                // Adjust menu items based on selection
                                std::vector<std::string> items = ctxMenuItems;
                                if (displayList[selectedIndex].type == ListItem::Folder) {
                                    bool isCollapsed = collapsedFolders.count(displayList[selectedIndex].name);
                                    items = {isCollapsed ? "Expand" : "Collapse", "Rename", "Delete", "Move to...", "Move in..."};
                                }

                                ctxMenuState.visible = true;
                                ctxMenuState.width = ContextMenu::calculateWidth(items);
                                int menuH = static_cast<int>(items.size()) + 2;
                                ctxMenuState.x = std::clamp(ev.x, 0, std::max(0, surface.getWidth() - ctxMenuState.width));
                                ctxMenuState.y = std::clamp(ev.y, 0, std::max(0, surface.getHeight() - menuH));
                                ctxMenuState.selectedIndex = 0;
                                continue;
                            }
                        }
                    }

                    hoverRow = -1;
                    hoverButton = HoverButton::None;
                    if (onNewFolderBtn) hoverButton = HoverButton::NewFolder;

                    if (insideList && listCount > 0) {
                        int row = listScrollOffset + (ev.y - listY);
                        if (row >= 0 && row < listCount) {
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
                            
                            if (displayList[row].type == ListItem::Asset) {
                                if (onOpen) hoverButton = HoverButton::Open;
                                else if (onRename) hoverButton = HoverButton::Rename;
                                else if (onDelete) hoverButton = HoverButton::Delete;
                                else if (onInfo) hoverButton = HoverButton::Info;
                            }

                            if (ev.pressed && ev.button == 0) {
                                selectedIndex = row;
                                ensureSelectionVisible();
                                loadPreview();

                                if (displayList[row].type == ListItem::Folder) {
                                    // Folder click: toggle expansion only (buttons removed)
                                    std::string fName = displayList[row].name;
                                    if (collapsedFolders.count(fName)) {
                                        collapsedFolders.erase(fName);
                                    } else {
                                        collapsedFolders.insert(fName);
                                    }
                                    applyFilter(fName);
                                } else {
                                    // Asset click
                                    if (onDelete) {
                                        deleteCurrentAssetOrFolder();
                                    } else if (onOpen) {
                                        openInEditor();
                                    } else if (onRename) {
                                        renameCurrent();
                                    } else if (onInfo) {
                                        if (!previewLoaded) loadPreview();
                                        if (previewLoaded) showInfoDialog(getSelectedAssetName(), currentPreview);
                                    }
                                }
                            }
                        }
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    } else if (ev.key == InputKey::Tab) {
                        int listCount = static_cast<int>(displayList.size());
                        if (listCount > 0 && selectedIndex >= 0 && selectedIndex < listCount) {
                            std::vector<std::string> items = ctxMenuItems;
                            if (displayList[selectedIndex].type == ListItem::Folder) {
                                bool isCollapsed = collapsedFolders.count(displayList[selectedIndex].name);
                                items = {isCollapsed ? "Expand" : "Collapse", "Rename", "Delete", "Move to...", "Move in..."};
                            }
                            ctxMenuState.visible = true;
                            ctxMenuState.width = ContextMenu::calculateWidth(items);
                            int menuH = static_cast<int>(items.size()) + 2;
                            
                            // Center on selected row
                            int rowY = listY + (selectedIndex - listScrollOffset);
                            ctxMenuState.x = std::clamp(listX + 4, 0, std::max(0, surface.getWidth() - ctxMenuState.width));
                            ctxMenuState.y = std::clamp(rowY, 0, std::max(0, surface.getHeight() - menuH));
                            ctxMenuState.selectedIndex = 0;
                        }
                    } else if (ev.key == InputKey::ArrowUp) {
                        if (selectedIndex > 0) selectedIndex--;
                        ensureSelectionVisible();
                        loadPreview();
                    } else if (ev.key == InputKey::ArrowDown) {
                        if (selectedIndex < static_cast<int>(displayList.size()) - 1) selectedIndex++;
                        ensureSelectionVisible();
                        loadPreview();
                    } else if (ev.key == InputKey::Enter) {
                       if (selectedIndex >= 0 && selectedIndex < (int)displayList.size()) {
                           auto& item = displayList[selectedIndex];
                           if (item.type == ListItem::Folder) {
                               if (collapsedFolders.count(item.name)) collapsedFolders.erase(item.name);
                               else collapsedFolders.insert(item.name);
                               applyFilter(item.name);
                           }
                       }
                    } else if (ev.key == InputKey::Character) {
                        if (ev.ch == 'i' || ev.ch == 'I') {
                            importAsset();
                        } else if (ev.ch == 'd' || ev.ch == 'D') {
                            deleteCurrentAssetOrFolder();
                        } else if (ev.ch == 'r' || ev.ch == 'R') {
                            renameCurrent();
                        } else if (ev.ch == 'o' || ev.ch == 'O') {
                            openInEditor();
                        } else if (ev.ch == 'n' || ev.ch == 'N') {
                            createNewFolder();
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
        folders = manager.listFolders();
        applyFilter(desired);
    }

    void AssetManagerScreen::applyFilter(const std::string& preferredSelection) {
        displayList.clear();
        std::string needle = toLowerCopy(searchQuery);

        // Group assets by folder
        std::map<std::string, std::vector<size_t>> folderContent;
        std::vector<size_t> rootAssets;
        for (size_t i = 0; i < assets.size(); ++i) {
            if (assets[i].folder.empty()) {
                rootAssets.push_back(i);
            } else {
                folderContent[assets[i].folder].push_back(i);
            }
        }

        auto matchesSearch = [&](const std::string& name) {
            if (needle.empty()) return true;
            return toLowerCopy(name).find(needle) != std::string::npos;
        };

        // Folders and their contents
        for (size_t i = 0; i < folders.size(); ++i) {
            bool folderMatches = matchesSearch(folders[i].name);
            std::vector<size_t> matchingAssets;
            if (folderContent.count(folders[i].name)) {
                for (size_t assetIdx : folderContent[folders[i].name]) {
                    if (matchesSearch(assets[assetIdx].name)) {
                        matchingAssets.push_back(assetIdx);
                    }
                }
            }

            // If searching, only show folders that match or have matching assets
            if (!needle.empty() && !folderMatches && matchingAssets.empty()) continue;

            bool isCollapsed = (collapsedFolders.find(folders[i].name) != collapsedFolders.end());
            bool forceExpand = !needle.empty();
            displayList.push_back({ListItem::Folder, i, !isCollapsed || forceExpand, folders[i].name, ""});

            if (!isCollapsed || forceExpand) { 
                for (size_t assetIdx : matchingAssets) {
                    displayList.push_back({ListItem::Asset, assetIdx, false, assets[assetIdx].name, folders[i].name});
                }
            }
        }

        // Root assets
        for (size_t assetIdx : rootAssets) {
            if (matchesSearch(assets[assetIdx].name)) {
                displayList.push_back({ListItem::Asset, assetIdx, false, assets[assetIdx].name, ""});
            }
        }

        if (displayList.empty()) {
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
            for (size_t i = 0; i < displayList.size(); ++i) {
                if (displayList[i].type == ListItem::Asset && displayList[i].name == preferredSelection) {
                    selectedIndex = static_cast<int>(i);
                    loadPreview();
                    ensureSelectionVisible();
                    return;
                } else if (displayList[i].type == ListItem::Folder && displayList[i].name == preferredSelection) {
                    selectedIndex = static_cast<int>(i);
                    ensureSelectionVisible();
                    return;
                }
            }
        }

        selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(displayList.size()) - 1);
        ensureSelectionVisible();
        loadPreview();
    }

    void AssetManagerScreen::ensureSelectionVisible() {
        int total = static_cast<int>(displayList.size());
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

    AssetManagerScreen::ListItem AssetManagerScreen::getSelectedItem() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(displayList.size())) {
            return {ListItem::Asset, 0, false, "", ""};
        }
        return displayList[selectedIndex];
    }

    std::string AssetManagerScreen::getSelectedAssetName() const {
        auto item = getSelectedItem();
        if (item.type == ListItem::Asset && !item.name.empty()) return item.name;
        return "";
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
        if (displayList.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(displayList.size())) {
            previewLoaded = false;
            lastPreviewName.clear();
            return;
        }
        
        const auto& item = displayList[selectedIndex];
        if (item.type == ListItem::Folder) {
            previewLoaded = false;
            lastPreviewName.clear();
            return;
        }

        const auto& entry = assets[item.index];
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

    void AssetManagerScreen::deleteCurrentAssetOrFolder() {
        auto item = getSelectedItem();
        if (item.name.empty()) return;
        
        bool isFolder = (item.type == ListItem::Folder);
        if (!skipDeleteConfirm) {
            if (!showDeleteConfirmDialog(item.name, isFolder)) return;
        }

        if (isFolder) {
            manager.deleteFolder(item.name, false); // Keep assets, move to root
        } else {
            manager.deleteAsset(item.name);
        }
        refreshList();
    }

    void AssetManagerScreen::renameCurrent() {
        auto item = getSelectedItem();
        if (item.name.empty()) return;

        std::string newName;
        if (!showRenameDialog(item.name, newName)) return;
        if (newName == item.name) return;

        if (item.type == ListItem::Folder) {
            if (!manager.renameFolder(item.name, newName)) {
                refreshList(item.name);
                return;
            }
        } else {
            if (!manager.renameAsset(item.name, newName)) {
                refreshList(item.name);
                return;
            }
        }

        refreshList(newName);
    }

    void AssetManagerScreen::createNewFolder() {
        std::string name;
        if (showCreateFolderDialog(name)) {
            if (manager.createFolder(name)) {
                refreshList(name);
            }
        }
    }

    void AssetManagerScreen::moveCurrentAsset() {
        auto item = getSelectedItem();
        if (item.name.empty()) return;

        std::string folderName;
        if (showMoveToFolderDialog(item.name, folderName)) {
            if (item.type == ListItem::Folder) {
                // Move all assets in this folder to the target
                auto allAssets = manager.listAssets();
                for (const auto& entry : allAssets) {
                    if (entry.folder == item.name) {
                        manager.moveAssetToFolder(entry.name, folderName);
                    }
                }
                refreshList();
            } else {
                if (manager.moveAssetToFolder(item.name, folderName)) {
                    refreshList(item.name);
                }
            }
        }
    }

    void AssetManagerScreen::moveInFromSystem() {
        auto item = getSelectedItem();
        std::string targetFolder = (item.type == ListItem::Folder) ? item.name : item.folderName;

        // Stop our input while browser runs
        input->stop();
        
        DirectoryBrowserScreen browser(lastImportPath, true, ".tlimg");
        auto selectedFiles = browser.show();
        
        // Restore state
        std::cout << "\x1b[?25l"; // Hide cursor
        input.reset();
        input = std::make_unique<InputController>();
        input->start();
        hoverButton = HoverButton::None;
        hoverRow = -1;

        if (selectedFiles.empty()) return;

        lastImportPath = std::filesystem::path(selectedFiles[0]).parent_path().string();

        for (const auto& filePath : selectedFiles) {
            std::filesystem::path p(filePath);
            if (p.extension() == ".tlimg") {
                std::string assetName = p.stem().string();
                std::filesystem::path destPath = std::filesystem::path(manager.getRootDir()) / p.filename();
                
                // If the file is not already in rootDir, copy it there
                bool ok = true;
                if (!std::filesystem::exists(destPath) || !std::filesystem::equivalent(p, destPath)) {
                    try {
                        std::filesystem::copy_file(p, destPath, std::filesystem::copy_options::overwrite_existing);
                    } catch (...) {
                        ok = false;
                    }
                }
                
                if (ok) {
                    manager.moveAssetToFolder(assetName, targetFolder);
                }
            }
        }

        refreshList();
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
        
        int availableForSearch = listInnerW - 2 - (int)searchLabel.size() - (int)kNewFolderBtnLabel.size() - 2;
        searchFieldX = listInnerX + 2 + static_cast<int>(searchLabel.size());
        searchFieldY = listInnerY;
        searchFieldW = std::max(5, availableForSearch);
        
        btnNewFolderX = searchFieldX + searchFieldW + 1;
        btnNewFolderY = searchFieldY;
        btnNewFolderW = (int)kNewFolderBtnLabel.size();

        TextFieldStyle searchStyle;
        searchStyle.width = searchFieldW;
        searchStyle.placeholder = "filter";
        searchStyle.focusFg = theme.focusFg;
        searchStyle.focusBg = theme.focusBg;
        searchStyle.panelBg = theme.panel;
        searchStyle.hintFg = theme.hintFg;
        
        TextField::render(surface, searchFieldX, searchFieldY, searchQuery, searchState, searchStyle);

        // Draw [+Folder] button
        {
            bool hot = hoverButton == HoverButton::NewFolder;
            RGBColor bfg = hot ? RGBColor{255,255,255} : theme.title;
            RGBColor bbg = hot ? darken(theme.accent, 0.8) : theme.accent;
            surface.drawText(btnNewFolderX, btnNewFolderY, kNewFolderBtnLabel, bfg, bbg);
        }

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
        RGBColor folderFg{255, 215, 0}; // Gold for folders

        int totalRows = static_cast<int>(displayList.size());
        int visibleRows = std::min(totalRows, listH);
        int start = std::min(listScrollOffset, std::max(0, totalRows - visibleRows));
        for (int i = 0; i < visibleRows; ++i) {
            int rowIndex = start + i;
            int rowY = listY + i;
            bool focused = (rowIndex == selectedIndex);
            RGBColor fg = focused ? black : theme.itemFg;
            RGBColor bg = focused ? white : theme.panel;
            
            const auto& item = displayList[rowIndex];

            surface.fillRect(listInnerX, rowY, listInnerW, 1, fg, bg, " ");

            int indent = (item.type == ListItem::Asset) ? 3 : 1;
            std::string prefix = "";
            if (item.type == ListItem::Folder) {
                prefix = (collapsedFolders.count(item.name) ? "▶ " : "▼ ");
                if (!focused) fg = folderFg;
            } else if (item.type == ListItem::Asset && !item.folderName.empty()) {
                bool isLast = true;
                if (rowIndex + 1 < totalRows) {
                    if (displayList[rowIndex + 1].type == ListItem::Asset && displayList[rowIndex + 1].folderName == item.folderName) {
                        isLast = false;
                    }
                }
                prefix = isLast ? "╰─" : "├─";
                indent = 1;
            }

            int textLimit = std::max(0, buttonOpenX - (listInnerX + indent) - 3);
            
            std::string displayName = prefix + item.name;
            displayName = TuiUtils::trimToUtf8VisualWidth(displayName, static_cast<size_t>(std::max(0, textLimit)));
            surface.drawText(listInnerX + indent, rowY, displayName, fg, bg);

            if (focused) {
                auto btnColor = [&](HoverButton hb) {
                    bool hot = (hoverRow == rowIndex && hoverButton == hb);
                    RGBColor baseBg = theme.accent;
                    RGBColor bbg = hot ? darken(baseBg, 0.6) : baseBg;
                    RGBColor bfg = hot ? RGBColor{255,255,255} : theme.title;
                    return std::make_pair(bfg, bbg);
                };

                if (item.type == ListItem::Asset) {
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
        }

        if (visibleRows == 0) {
            surface.drawText(listInnerX + 1, listY, searchQuery.empty() ? "No items found" : "No items match filter", theme.hintFg, theme.panel);
        }

        // Preview panel
        surface.fillRect(prevX, contentY, prevW, contentH, theme.itemFg, theme.panel, " ");
        surface.drawFrame(prevX, contentY, prevW, contentH, modernFrame, theme.itemFg, theme.panel);
        surface.fillRect(prevX + 1, contentY + 1, prevW - 2, 1, theme.title, theme.background, " ");
        surface.drawText(prevX + 2, contentY + 1, "Preview", theme.title, theme.background);

        if (previewLoaded) {
            drawPreview(prevX + 2, contentY + 3, prevW - 4, contentH - 5);
        } else {
            std::string previewMsg = displayList.empty() ? "No items to preview" : "No preview available";
            surface.drawText(prevX + 2, contentY + 3, previewMsg, theme.hintFg, theme.panel);
        }
    }

    bool AssetManagerScreen::showRenameDialog(const std::string& currentName, std::string& outName) {
        outName = currentName;
        bool running = true;
        bool confirmed = false;
        bool hoverOk = false, hoverCancel = false;
        TextFieldState inputState;
        inputState.focused = true;
        inputState.caretIndex = (int)outName.size();
        inputState.mode = CursorMode::IBeam;
        std::string errorMsg;
        int mouseX = -1, mouseY = -1;

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
            inputState.updateCaret();
            clampDialog();
            drawMainUI();

            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Rename Asset", theme.title, theme.background);
            
            std::string currentDisp = currentName;
            if (TuiUtils::calculateUtf8VisualWidth(currentDisp) > 33) {
                currentDisp = TuiUtils::trimToUtf8VisualWidth(currentDisp, 30) + "...";
            }
            surface.drawText(dx + 2, dy + 2, "Current: " + currentDisp, theme.hintFg, theme.panel);

            int fieldX = dx + 2;
            int fieldY = dy + 4;
            int fieldW = dw - 4;
            
            TextFieldStyle fieldStyle;
            fieldStyle.width = fieldW;
            fieldStyle.focusBg = theme.focusBg;
            fieldStyle.focusFg = theme.focusFg;
            fieldStyle.panelBg = theme.panel;
            fieldStyle.caretChar = '|';
            
            TextField::render(surface, fieldX, fieldY, outName, inputState, fieldStyle);

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
                if (TextField::handleInput(ev, outName, inputState, fieldStyle)) {
                    continue;
                }

                if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;
                    if (dragging) {
                        dx = dragOriginX + (ev.x - dragStartX);
                        dy = dragOriginY + (ev.y - dragStartY);
                        clampDialog();
                    }
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
                    if (ev.key == InputKey::Enter) {
                        tryConfirm();
                    } else if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    }
                }
            }
            updateHover();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        return confirmed;
    }

    bool AssetManagerScreen::showDeleteConfirmDialog(const std::string& name, bool isFolder) {
        bool running = true;
        bool confirmed = false;
        bool hoverDelete = false, hoverCancel = false;
        bool localSkipConfirm = skipDeleteConfirm;
        int mouseX = -1, mouseY = -1;

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
            
            surface.fillRect(dx, dy, dw, dh, theme.itemFg, theme.panel, " ");
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, isFolder ? "Delete Folder" : "Delete Image Asset", theme.title, theme.background);
            surface.drawText(dx + dw - 5, dy + 1, "[q]", RGBColor{200, 200, 200}, theme.background);

            std::string displayName = name;
            if (TuiUtils::calculateUtf8VisualWidth(displayName) > 33) {
                displayName = TuiUtils::trimToUtf8VisualWidth(displayName, 30) + "...";
            }
            std::string msg = isFolder ? "Delete folder " + displayName + "?" : "Delete asset " + displayName + "?";
            surface.drawText(dx + 4, dy + 3, msg, theme.itemFg, theme.panel);
            surface.drawText(dx + 4, dy + 5, isFolder ? "Assets will NOT be deleted (moved to root)." : "This action cannot be undone.", theme.hintFg, theme.panel);
            
            std::string squelchLabel = (localSkipConfirm ? "[x] Don't ask again" : "[ ] Don't ask again");
            surface.drawText(dx + 4, dy + 7, squelchLabel, theme.itemFg, theme.panel);

            std::string delBtn = "[ Delete ]";
            std::string cancelBtn = "[ Cancel ]";
            int delX = dx + 6;
            int cancelX = dx + dw - 16;
            int btnY = dy + 9;

            auto drawBtn = [&](const std::string& lbl, int x, bool hot, bool danger) {
                RGBColor baseBg = danger ? RGBColor{180, 20, 20} : theme.accent;
                RGBColor bbg = hot ? (danger ? RGBColor{255, 30, 30} : darken(baseBg, 0.6)) : baseBg;
                RGBColor bfg = hot ? RGBColor{255, 255, 255} : theme.title;
                surface.drawText(x, btnY, lbl, bfg, bbg);
            };

            hoverDelete = (mouseX >= delX && mouseX < delX + (int)delBtn.size() && mouseY == btnY);
            hoverCancel = (mouseX >= cancelX && mouseX < cancelX + (int)cancelBtn.size() && mouseY == btnY);
            bool hoverSquelch = (mouseX >= dx + 4 && mouseX < dx + 20 && mouseY == dy + 7);

            drawBtn(delBtn, delX, hoverDelete, true);
            drawBtn(cancelBtn, cancelX, hoverCancel, false);

            painter.present(surface);

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
                    if (ev.button == 0 && ev.pressed) {
                        if (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1) {
                            dragging = true;
                            dragStartX = ev.x;
                            dragStartY = ev.y;
                            dragOriginX = dx;
                            dragOriginY = dy;
                        } else if (hoverDelete) {
                            confirmed = true; running = false;
                        } else if (hoverCancel) {
                            confirmed = false; running = false;
                        } else if (hoverSquelch) {
                            localSkipConfirm = !localSkipConfirm;
                        }
                    }
                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false; 
                    }
                } else if (ev.type == InputEvent::Type::Key && ev.key == InputKey::Character && (ev.ch == 'y' || ev.ch == 'Y')) { 
                    confirmed = true; running = false; 
                } else if (ev.type == InputEvent::Type::Key && ev.key == InputKey::Enter) { confirmed = true; running = false; }
                else if (ev.type == InputEvent::Type::Key && ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) { confirmed = false; running = false; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        if (confirmed) {
            skipDeleteConfirm = localSkipConfirm;
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
                    if (!isImporting && focusIdx == 0 && ev.key == InputKey::Character) {
                        if (ev.ch == '\b') {
                            if (!widthStr.empty()) widthStr.pop_back();
                        } else if (std::isdigit(static_cast<unsigned char>(ev.ch)) || ev.ch == '%' || ev.ch == 'q' || ev.ch == 'Q') {
                            widthStr += static_cast<char>(ev.ch);
                        }
                        continue;
                    }

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
    
    std::string assetDisp = assetName;
    // max dw is 64, so available width is roughly 50
    if (TuiUtils::calculateUtf8VisualWidth(assetDisp) > 50) {
        assetDisp = TuiUtils::trimToUtf8VisualWidth(assetDisp, 47) + "...";
    }
    lines.push_back("Resource: " + assetDisp);

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

    bool AssetManagerScreen::showCreateFolderDialog(std::string& outName) {
        int w = surface.getWidth();
        int h = surface.getHeight();
        int dw = 40;
        int dh = 10;
        int dx = (w - dw) / 2;
        int dy = (h - dh) / 2;

        auto clampDialog = [&]() {
            w = surface.getWidth();
            h = surface.getHeight();
            dx = std::clamp(dx, 0, std::max(0, w - dw));
            dy = std::clamp(dy, 0, std::max(0, h - dh));
        };

        std::string inputStr = "";
        TextFieldState inputState;
        inputState.focused = true;
        inputState.caretIndex = 0; // It's empty anyway
        inputState.mode = CursorMode::IBeam;

        bool running = true;
        bool confirmed = false;
        bool dragging = false;
        int dragStartX = 0, dragStartY = 0;
        int dragOriginX = 0, dragOriginY = 0;
        int mouseX = -1, mouseY = -1;

        while (running) {
            inputState.updateCaret();
            clampDialog();
            drawMainUI();
            BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
            surface.fillRect(dx, dy, dw, dh, theme.itemFg, theme.panel, " ");
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Create New Folder", theme.title, theme.background);
            surface.drawText(dx + dw - 5, dy + 1, "[q]", RGBColor{200, 200, 200}, theme.background);

            surface.drawText(dx + 2, dy + 3, "Folder Name:", theme.itemFg, theme.panel);
            
            TextFieldStyle style;
            style.width = dw - 4;
            style.focusFg = theme.focusFg;
            style.focusBg = theme.focusBg;
            style.panelBg = theme.panel;

            TextField::render(surface, dx + 2, dy + 4, inputStr, inputState, style);

            std::string okBtn = "[ Create ]";
            std::string cancelBtn = "[ Cancel ]";
            int okX = dx + 4;
            int cancelX = dx + dw - 14;
            int btnY = dy + 7;

            auto drawBtn = [&](const std::string& lbl, int x, bool hot) {
                RGBColor bbg = hot ? darken(theme.accent, 0.6) : theme.accent;
                RGBColor bfg = hot ? RGBColor{255, 255, 255} : theme.title;
                surface.drawText(x, btnY, lbl, bfg, bbg);
            };

            bool hoverOk = (mouseX >= okX && mouseX < okX + (int)okBtn.size() && mouseY == btnY);
            bool hoverCancel = (mouseX >= cancelX && mouseX < cancelX + (int)cancelBtn.size() && mouseY == btnY);
            
            drawBtn(okBtn, okX, hoverOk);
            drawBtn(cancelBtn, cancelX, hoverCancel);

            painter.present(surface);

            auto events = input->pollEvents();
            for (const auto& ev : events) {
                if (TextField::handleInput(ev, inputStr, inputState, style)) {
                    continue;
                }
                if (ev.type == InputEvent::Type::Mouse) {
                    mouseX = ev.x;
                    mouseY = ev.y;
                    if (dragging) {
                        dx = dragOriginX + (ev.x - dragStartX);
                        dy = dragOriginY + (ev.y - dragStartY);
                        clampDialog();
                    }
                    if (ev.button == 0 && ev.pressed) {
                        if (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1) {
                            dragging = true;
                            dragStartX = ev.x;
                            dragStartY = ev.y;
                            dragOriginX = dx;
                            dragOriginY = dy;
                        } else if (hoverOk) {
                            if (isValidAssetName(inputStr)) {
                                outName = inputStr; confirmed = true; running = false;
                            }
                        } else if (hoverCancel) {
                            confirmed = false; running = false;
                        }
                    }
                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false; 
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::Enter) {
                        if (isValidAssetName(inputStr)) {
                            outName = inputStr; confirmed = true; running = false;
                        }
                    } else if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return confirmed;
    }

    bool AssetManagerScreen::showMoveToFolderDialog(const std::string& assetName, std::string& outFolderName) {
        int w = surface.getWidth();
        int h = surface.getHeight();
        int dw = 40;
        int dh = std::min(h - 4, (int)folders.size() + 10);
        int dx = (w - dw) / 2;
        int dy = (h - dh) / 2;

        auto clampDialog = [&]() {
            w = surface.getWidth();
            h = surface.getHeight();
            dx = std::clamp(dx, 0, std::max(0, w - dw));
            dy = std::clamp(dy, 0, std::max(0, h - dh));
        };

        int selectedFolderIdx = -1; // -1 means Root
        int scroll = 0;

        bool running = true;
        bool confirmed = false;
        bool dragging = false;
        int dragStartX = 0, dragStartY = 0;
        int dragOriginX = 0, dragOriginY = 0;
        int mouseX = -1, mouseY = -1;

        while (running) {
            clampDialog();
            drawMainUI();
            BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
            surface.fillRect(dx, dy, dw, dh, theme.itemFg, theme.panel, " ");
            surface.drawFrame(dx, dy, dw, dh, modernFrame, theme.itemFg, theme.panel);
            surface.fillRect(dx + 1, dy + 1, dw - 2, 1, theme.title, theme.background, " ");
            surface.drawText(dx + 2, dy + 1, "Move Asset", theme.title, theme.background);
            surface.drawText(dx + dw - 5, dy + 1, "[q]", RGBColor{200, 200, 200}, theme.background);

            std::string assetDisp = assetName;
            if (TuiUtils::calculateUtf8VisualWidth(assetDisp) > 25) {
                assetDisp = TuiUtils::trimToUtf8VisualWidth(assetDisp, 22) + "...";
            }
            surface.drawText(dx + 2, dy + 2, "Move " + assetDisp + " to:", theme.hintFg, theme.panel);

            int listYStart = dy + 3;
            int listHVisible = dh - 6;
            
            auto drawRow = [&](int idx, const std::string& name, bool focused) {
                int ry = listYStart + (idx + 1) - scroll;
                if (ry < listYStart || ry >= listYStart + listHVisible) return;
                RGBColor fg = focused ? RGBColor{0,0,0} : theme.itemFg;
                RGBColor bg = focused ? RGBColor{255,255,255} : theme.panel;
                surface.fillRect(dx + 1, ry, dw - 2, 1, fg, bg, " ");
                
                std::string nameDisp = name;
                if (TuiUtils::calculateUtf8VisualWidth(nameDisp) > dw - 4) {
                    nameDisp = TuiUtils::trimToUtf8VisualWidth(nameDisp, dw - 7) + "...";
                }
                surface.drawText(dx + 2, ry, nameDisp, fg, bg);
            };

            drawRow(-1, "[ Root ]", selectedFolderIdx == -1);
            for (int i = 0; i < (int)folders.size(); ++i) {
                drawRow(i, folders[i].name, selectedFolderIdx == i);
            }

            std::string okBtn = "[ Move ]";
            std::string cancelBtn = "[ Cancel ]";
            int okX = dx + 4;
            int cancelX = dx + dw - 14;
            int btnY = dy + dh - 2;

            auto drawBtn = [&](const std::string& lbl, int x, bool hot) {
                RGBColor bbg = hot ? darken(theme.accent, 0.6) : theme.accent;
                RGBColor bfg = hot ? RGBColor{255, 255, 255} : theme.title;
                surface.drawText(x, btnY, lbl, bfg, bbg);
            };

            bool hoverOk = (mouseX >= okX && mouseX < okX + (int)okBtn.size() && mouseY == btnY);
            bool hoverCancel = (mouseX >= cancelX && mouseX < cancelX + (int)cancelBtn.size() && mouseY == btnY);

            drawBtn(okBtn, okX, hoverOk);
            drawBtn(cancelBtn, cancelX, hoverCancel);

            painter.present(surface);

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
                    if (ev.button == 0 && ev.pressed) {
                        if (ev.y == dy + 1 && ev.x >= dx + 1 && ev.x < dx + dw - 1) {
                            dragging = true;
                            dragStartX = ev.x;
                            dragStartY = ev.y;
                            dragOriginX = dx;
                            dragOriginY = dy;
                        } else if (hoverOk) {
                            outFolderName = (selectedFolderIdx == -1) ? "" : folders[selectedFolderIdx].name;
                            confirmed = true; running = false;
                        } else if (hoverCancel) {
                            confirmed = false; running = false;
                        } else if (mouseY >= listYStart && mouseY < listYStart + listHVisible) {
                            int clickedIdx = scroll + (mouseY - listYStart) - 1;
                            if (clickedIdx >= -1 && clickedIdx < (int)folders.size()) {
                                selectedFolderIdx = clickedIdx;
                            }
                        }
                    }
                    if (ev.button == 0 && !ev.pressed && !ev.move) {
                        dragging = false; 
                    }
                    if (ev.wheel != 0) {
                        scroll = std::clamp(scroll + (ev.wheel > 0 ? -1 : 1), 0, std::max(0, (int)folders.size() + 1 - listHVisible));
                    }
                } else if (ev.type == InputEvent::Type::Key) {
                    if (ev.key == InputKey::ArrowUp) {
                        if (selectedFolderIdx > -1) selectedFolderIdx--;
                        if (selectedFolderIdx - scroll < -1) scroll = selectedFolderIdx + 1;
                    } else if (ev.key == InputKey::ArrowDown) {
                        if (selectedFolderIdx < (int)folders.size() - 1) selectedFolderIdx++;
                        if (selectedFolderIdx - scroll >= listHVisible - 1) scroll = selectedFolderIdx - listHVisible + 2;
                    } else if (ev.key == InputKey::Enter) {
                        outFolderName = (selectedFolderIdx == -1) ? "" : folders[selectedFolderIdx].name;
                        confirmed = true; running = false;
                    } else if (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q')) {
                        running = false;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return confirmed;
    }
}
}
