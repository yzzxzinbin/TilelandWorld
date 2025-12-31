#pragma once
#ifndef TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H
#define TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <filesystem>

namespace TilelandWorld {
namespace UI {

class DirectoryBrowserScreen {
public:
    explicit DirectoryBrowserScreen(std::string initialPath, bool showFiles = false, std::string extensionFilter = "");
    // 返回选中的目录或文件；若用户取消则返回空字符串
    std::string show();

private:
    struct Entry {
        std::string name;
        std::filesystem::path fullPath;
        bool isDir{true};
    };

    std::filesystem::path currentPath;
    std::vector<Entry> entries;
    size_t selected{0};
    int scrollOffset{0};
    
    bool showFilesMode{false};
    std::string extensionFilter; // e.g. ".bmp"

    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;

    // 布局缓存
    int listOriginX{4};
    int listOriginY{6};
    int listWidth{60};
    int listHeight{20};

    void refreshEntries();
    void clampSelection();
    void renderFrame();
    void handleKey(int ch, bool& running, std::string& result);
    void handleMouse(const InputEvent& ev, bool& running, std::string& result);
    void ensureConsoleSize();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H
