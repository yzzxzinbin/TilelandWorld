#pragma once
#ifndef TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H
#define TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include "../Utils/TaskSystem.h"
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <mutex>
#include <set>

namespace TilelandWorld {
namespace UI {

class DirectoryBrowserScreen {
public:
    explicit DirectoryBrowserScreen(std::string initialPath, bool showFiles = false, std::string extensionFilter = "");
    ~DirectoryBrowserScreen(); 
    // 返回选中的目录或文件（支持多选）
    std::vector<std::string> show();

private:
    struct Entry {
        std::string name;
        std::filesystem::path fullPath;
        bool isDir{true};

        std::string sizeStr;
        std::string typeStr;
        std::string dateStr;
        int64_t sizeBytes{0};
        bool sizePending{false};
        bool sizeTimedOut{false};
        bool isSelected{false}; // For multi-selection
    };

    std::filesystem::path currentPath;
    std::vector<Entry> entries;
    size_t selected{0};
    int scrollOffset{0};
    
    bool showFilesMode{false};
    std::string extensionFilter; // e.g. ".bmp"
    std::string lastErrorMessage;

    TuiSurface surface;
    TuiPainter painter;
    TuiTheme theme;

    // 布局缓存
    int listOriginX{4};
    int listOriginY{6};
    int listWidth{60};
    int listHeight{20};

    // Scrollbar state
    int scrollX{-1};
    int thumbY{0};
    int thumbH{0};
    bool hoverScroll{false};
    bool draggingScroll{false};

    // Scrolling filename support
    size_t lastSelectedIndex{static_cast<size_t>(-1)};
    std::chrono::steady_clock::time_point selectionTime;

    // Async size calculation
    std::mutex entriesMutex;
    TaskSystem taskSystem;
    std::atomic<bool> calcThreadsRunning{true};
    void startSizeCalculation(size_t index);
    
    // Formatting helpers
    std::string formatSize(int64_t bytes);
    std::string getFileType(const std::filesystem::path& p);
    std::string formatTime(std::filesystem::file_time_type ftime);

    void refreshEntries();
    void clampSelection();
    void renderFrame();
    void handleKey(const InputEvent& ev, bool& running, std::vector<std::string>& results);
    void handleMouse(const InputEvent& ev, bool& running, std::vector<std::string>& results);
    void ensureConsoleSize();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_DIRECTORYBROWSERSCREEN_H
