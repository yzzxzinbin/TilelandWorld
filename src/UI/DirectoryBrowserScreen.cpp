#include "DirectoryBrowserScreen.h"
#include "TuiUtils.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <system_error>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    constexpr int kArrowUp = 0x100 | 72;
    constexpr int kArrowDown = 0x100 | 80;
    constexpr int kArrowLeft = 0x100 | 75;
    constexpr int kArrowRight = 0x100 | 77;
    const BoxStyle kModernFrame{"╭", "╮", "╰", "╯", "─", "│"};

    bool isRoot(const std::filesystem::path& p) {
        return p.has_parent_path() == false || p.parent_path() == p;
    }

}

DirectoryBrowserScreen::DirectoryBrowserScreen(std::string initialPath, bool showFiles, std::string extensionFilter)
    : showFilesMode(showFiles), extensionFilter(extensionFilter)
{
    selectionTime = std::chrono::steady_clock::now();
    try {
        currentPath = std::filesystem::weakly_canonical(std::filesystem::path(initialPath));
    } catch (...) {
        currentPath = std::filesystem::current_path();
    }
    if (!std::filesystem::exists(currentPath)) {
        std::error_code ec;
        std::filesystem::create_directories(currentPath, ec);
        if (ec) {
            currentPath = std::filesystem::current_path();
        }
    }
    refreshEntries();
}

DirectoryBrowserScreen::~DirectoryBrowserScreen() {
    calcThreadsRunning = false;
    taskSystem.stop();
}

std::string DirectoryBrowserScreen::formatSize(int64_t bytes) {
    if (bytes < 0) return "";
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double d = static_cast<double>(bytes);
    while (d >= 1024 && unit < 4) {
        d /= 1024;
        unit++;
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << d << units[unit];
    return ss.str();
}

std::string DirectoryBrowserScreen::getFileType(const std::filesystem::path& p) {
    if (std::filesystem::is_directory(p)) return "Folder";
    std::string ext = p.extension().string();
    if (ext.empty()) return "File";
    std::string type = ext.substr(1);
    for (auto& c : type) c = std::toupper(static_cast<unsigned char>(c));
    return type + " File";
}

std::string DirectoryBrowserScreen::formatTime(std::filesystem::file_time_type ftime) {
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        std::tm* lt = std::localtime(&tt);
        if (!lt) return "Unknown";
        std::stringstream ss;
        ss << std::put_time(lt, "%Y/%m/%d %H:%M");
        return ss.str();
    } catch (...) {
        return "Unknown";
    }
}

void DirectoryBrowserScreen::startSizeCalculation(size_t index) {
    std::filesystem::path path = entries[index].fullPath;
    
    taskSystem.submit([this, index, path]() {
        auto startTime = std::chrono::steady_clock::now();
        int64_t total = 0;
        bool timedOut = false;

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied)) {
                if (!calcThreadsRunning) return;

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() >= 5) {
                    timedOut = true;
                    break;
                }

                if (entry.is_regular_file()) {
                    total += static_cast<int64_t>(entry.file_size());
                }
            }
        } catch (...) {
            // Probably permission denied or disappeared
        }

        if (!calcThreadsRunning) return;

        std::lock_guard<std::mutex> lock(entriesMutex);
        if (index < entries.size() && entries[index].fullPath == path) {
            entries[index].sizePending = false;
            if (timedOut) {
                entries[index].sizeTimedOut = true;
                entries[index].sizeStr = ""; 
            } else {
                entries[index].sizeBytes = total;
                entries[index].sizeStr = formatSize(total);
            }
        }
    });
}

std::vector<std::string> DirectoryBrowserScreen::show()
{
    InputController input(false); // keep VT mouse enabled for caller
    input.setRestoreOnExit(false);
    input.start();

    bool running = true;
    std::vector<std::string> results;

    while (running)
    {
        ensureConsoleSize();
        renderFrame();
        painter.present(surface, true, 1, 1);

        // 小睡，避免忙等
        std::this_thread::sleep_for(std::chrono::milliseconds(12));

        // 处理事件（键盘 + 鼠标）
        auto events = input.pollEvents();
        for (const auto& ev : events)
        {
            if (ev.type == InputEvent::Type::Mouse)
            {
                handleMouse(ev, running, results);
                if (!running) break;
            }
            else if (ev.type == InputEvent::Type::Key)
            {
                handleKey(ev, running, results);
                if (!running) break;
            }
        }
    }

    painter.reset();
    input.stop();
    return results;
}

void DirectoryBrowserScreen::refreshEntries()
{
    std::lock_guard<std::mutex> lock(entriesMutex);
    entries.clear();

    // 当前目录占位，用于直接选择
    {
        Entry e{"[Use this directory]", currentPath, true};
        e.typeStr = "Control";
        e.dateStr = "";
        e.sizeStr = "";
        entries.push_back(std::move(e));
    }

    // 上级目录
    if (!isRoot(currentPath))
    {
        Entry e{"..", currentPath.parent_path(), true};
        e.typeStr = "Folder";
        e.dateStr = "";
        e.sizeStr = "";
        entries.push_back(std::move(e));
    }

    std::vector<Entry> dirs;
    std::vector<Entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(currentPath))
    {
        try {
            if (entry.is_directory())
            {
                Entry e;
                e.name = entry.path().filename().string();
                e.fullPath = entry.path();
                e.isDir = true;
                e.typeStr = "Folder";
                e.dateStr = formatTime(entry.last_write_time());
                e.sizeStr = "...";
                e.sizePending = true;
                dirs.push_back(std::move(e));
            }
            else if (showFilesMode && entry.is_regular_file())
            {
                if (extensionFilter.empty() || entry.path().extension() == extensionFilter) {
                    Entry e;
                    e.name = entry.path().filename().string();
                    e.fullPath = entry.path();
                    e.isDir = false;
                    e.typeStr = getFileType(entry.path());
                    e.dateStr = formatTime(entry.last_write_time());
                    e.sizeBytes = entry.file_size();
                    e.sizeStr = formatSize(e.sizeBytes);
                    files.push_back(std::move(e));
                }
            }
        } catch (...) {}
    }
    std::sort(dirs.begin(), dirs.end(), [](const Entry& a, const Entry& b){ return a.name < b.name; });
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b){ return a.name < b.name; });

    entries.insert(entries.end(), dirs.begin(), dirs.end());
    entries.insert(entries.end(), files.begin(), files.end());

    // Start size calculation for directories
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].isDir && entries[i].sizePending) {
            startSizeCalculation(i);
        }
    }

    clampSelection();
}

void DirectoryBrowserScreen::clampSelection()
{
    if (entries.empty()) { selected = 0; scrollOffset = 0; return; }
    if (selected >= entries.size()) selected = entries.size() - 1;
    int visible = std::max(1, listHeight - 4);
    
    // Ensure selected is visible
    if (selected < static_cast<size_t>(scrollOffset)) {
        scrollOffset = static_cast<int>(selected);
    }
    if (selected >= static_cast<size_t>(scrollOffset + visible)) {
        scrollOffset = static_cast<int>(selected) - visible + 1;
    }
    
    int maxOffset = std::max(0, static_cast<int>(entries.size()) - visible);
    scrollOffset = std::clamp(scrollOffset, 0, maxOffset);
}

void DirectoryBrowserScreen::renderFrame()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(60, info.srWindow.Right - info.srWindow.Left + 1);
        int consoleHeight = std::max(24, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(consoleWidth, consoleHeight);
    }
#endif

    if (selected != lastSelectedIndex) {
        lastSelectedIndex = selected;
        selectionTime = std::chrono::steady_clock::now();
    }

    listWidth = std::max(60, surface.getWidth() - 8);
    listHeight = std::max(12, surface.getHeight() - 8);
    listOriginX = std::max(2, (surface.getWidth() - listWidth) / 2);
    listOriginY = 4;

    surface.clear(theme.itemFg, theme.background, " ");
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");

    std::string title = showFilesMode ? "Choose File" : "Choose Save Directory";
    surface.drawCenteredText(0, 1, surface.getWidth(), title, theme.title, theme.background);
    surface.drawCenteredText(0, 2, surface.getWidth(), currentPath.string(), theme.subtitle, theme.background);

    surface.fillRect(listOriginX, listOriginY, listWidth, listHeight, theme.itemFg, theme.panel, " ");
    surface.drawFrame(listOriginX, listOriginY, listWidth, listHeight, kModernFrame, theme.itemFg, theme.panel);

    // Columns proportions
    int innerWidth = listWidth - 4;
    int colName = std::max(20, (int)(innerWidth * 0.45));
    int colSize = (int)(innerWidth * 0.12);
    int colType = (int)(innerWidth * 0.18);
    int colDate = innerWidth - colName - colSize - colType;

    // Draw Headers
    auto drawHeader = [&](int x, const std::string& text, int w) {
        surface.drawText(x, listOriginY + 1, text, theme.accent, theme.panel);
    };
    drawHeader(listOriginX + 2, "Name", colName);
    drawHeader(listOriginX + 2 + colName, "Size", colSize);
    drawHeader(listOriginX + 2 + colName + colSize, "Type", colType);
    drawHeader(listOriginX + 2 + colName + colSize + colType, "Date", colDate);
    surface.fillRect(listOriginX + 1, listOriginY + 2, listWidth - 2, 1, theme.accent, theme.panel, kModernFrame.horizontal);
    
    // Add T-junctions to make the separator connect nicely with the frame
    if (TuiCell* leftJunc = surface.editCell(listOriginX, listOriginY + 2)) {
        leftJunc->glyph = "├";
        leftJunc->fg = theme.itemFg;
    }
    if (TuiCell* rightJunc = surface.editCell(listOriginX + listWidth - 1, listOriginY + 2)) {
        rightJunc->glyph = "┤";
        rightJunc->fg = theme.itemFg;
    }

    int visible = std::max(1, listHeight - 4);
    int start = scrollOffset;
    int end = 0;
    {
        std::lock_guard<std::mutex> lock(entriesMutex);
        end = std::min(static_cast<int>(entries.size()), start + visible);
    }

    int rowStartY = listOriginY + 3;
    auto now = std::chrono::steady_clock::now();

    for (int i = start; i < end; ++i)
    {
        bool focus = static_cast<size_t>(i) == selected;
        
        Entry e;
        {
            std::lock_guard<std::mutex> lock(entriesMutex);
            e = entries[i];
        }

        RGBColor fg = focus ? theme.focusFg : theme.itemFg;
        RGBColor bg = focus ? theme.focusBg : theme.itemBg;

        if (!focus && e.isSelected) {
            bg = RGBColor{ 199, 199, 199 };
            fg = RGBColor{ 30, 30, 30 }; // Dark text for light grey background
        }

        std::string label = e.name;
        if (e.isDir)
        {
            if (label == "[Use this directory]" || label == "..") {
                label = "     " + e.name; // 5 cells of padding to match others
            } else {
                // Align with file icons ([Marker:2] + [Icon:3] = 5 cells total)
                label = "  📁 " + e.name; 
            }
        } else if (!e.isDir) {
            std::string indicator = e.isSelected ? "● " : "○ ";
            label = indicator + "📄 " + e.name;
        }

        int y = rowStartY + (i - start);
        surface.fillRect(listOriginX + 1, y, listWidth - 2, 1, fg, bg, " ");

        // Name Column with Scrolling support
        size_t labelWidth = TuiUtils::calculateUtf8VisualWidth(label);
        std::string nameToDraw = label;
        if (labelWidth > static_cast<size_t>(colName - 2)) {
            if (focus) {
                // Scroll effect
                double elapsed = std::chrono::duration<double>(now - selectionTime).count();
                if (elapsed > 1.0) { // Start scrolling after 1s
                    size_t over = labelWidth - (colName - 2);
                    double scrollSpeed = 4.0; // visual cells per second
                    int totalVis = (int)(labelWidth + 4);
                    int scrollOffsetVis = (int)((elapsed - 1.0) * scrollSpeed) % totalVis;
                    
                    std::string combined = label + "    ";
                    size_t charOffset = 0;
                    size_t currentVis = 0;
                    while (charOffset < combined.size() && currentVis < (size_t)scrollOffsetVis) {
                        auto info = TuiUtils::nextUtf8Char(combined, charOffset);
                        if (info.length == 0) break;
                        charOffset += info.length;
                        currentVis += info.visualWidth;
                    }
                    
                    std::string scrolled = combined.substr(std::min(charOffset, combined.size())) + label;
                    nameToDraw = TuiUtils::trimToUtf8VisualWidth(scrolled, colName - 2);
                } else {
                    nameToDraw = TuiUtils::trimToUtf8VisualWidth(label, colName - 2);
                    if (labelWidth > (size_t)colName - 2) nameToDraw += ""; // Handled by trim
                }
            } else {
                nameToDraw = TuiUtils::trimToUtf8VisualWidth(label, colName - 3) + "…";
            }
        }
        surface.drawText(listOriginX + 2, y, nameToDraw, fg, bg);

        // Size Column
        surface.drawText(listOriginX + 2 + colName, y, e.sizeStr, fg, bg);

        // Type Column
        std::string typeToDraw = TuiUtils::trimToUtf8VisualWidth(e.typeStr, colType - 1);
        surface.drawText(listOriginX + 2 + colName + colSize, y, typeToDraw, fg, bg);

        // Date Column
        surface.drawText(listOriginX + 2 + colName + colSize + colType, y, e.dateStr, fg, bg);
    }

    // Scrollbar rendering
    int totalEntries = static_cast<int>(entries.size());
    if (totalEntries > visible) {
        scrollX = listOriginX + listWidth - 1;
        int rowStartY = listOriginY + 3;
        
        // Background track
        surface.fillRect(scrollX, rowStartY, 1, visible, theme.hintFg, theme.panel, "│");
        
        thumbH = std::max(1, (visible * visible) / totalEntries);
        int maxOffset = totalEntries - visible;
        thumbY = (maxOffset > 0) ? (scrollOffset * (visible - thumbH) / maxOffset) : 0;
        
        RGBColor thumbColor = theme.accent;
        if (hoverScroll || draggingScroll) {
            thumbColor = TuiUtils::blendColor(thumbColor, {255, 255, 255}, 0.3);
        }
        surface.fillRect(scrollX, rowStartY + thumbY, 1, thumbH, thumbColor, thumbColor, "█");
    } else {
        scrollX = -1;
    }

    std::string hint = "Enter/Right: open | Space: choose | Backspace/Left: up | Q: cancel | Wheel/Click to navigate";
    surface.drawCenteredText(0, surface.getHeight() - 3, surface.getWidth(), hint, theme.hintFg, theme.background);
}

void DirectoryBrowserScreen::handleKey(const InputEvent& ev, bool& running, std::vector<std::string>& results)
{
    if (!running) return;

    // Convert InputKey to common codes for internal logic
    int ch = 0;
    if (ev.key == InputKey::Character) ch = static_cast<int>(ev.ch);
    else if (ev.key == InputKey::Enter) ch = 13;
    else if (ev.key == InputKey::ArrowUp) ch = kArrowUp;
    else if (ev.key == InputKey::ArrowDown) ch = kArrowDown;
    else if (ev.key == InputKey::ArrowLeft) ch = kArrowLeft;
    else if (ev.key == InputKey::ArrowRight) ch = kArrowRight;
    else if (ev.key == InputKey::Escape) ch = 'q';

    if (ch == 0) return;

    if (ch == kArrowUp || ch == 'w' || ch == 'W')
    {
        if (selected > 0) { --selected; clampSelection(); }
    }
    else if (ch == kArrowDown || ch == 's' || ch == 'S')
    {
        if (selected + 1 < entries.size()) { ++selected; clampSelection(); }
    }
    else if (ch == kArrowLeft || ch == 8 /*Backspace*/)
    {
        if (!isRoot(currentPath))
        {
            currentPath = currentPath.parent_path();
            {
                std::lock_guard<std::mutex> lock(entriesMutex);
                selected = 0;
                scrollOffset = 0;
            }
            refreshEntries();
        }
    }
    else if (ch == kArrowRight)
    {
        // open
        bool shouldRefresh = false;
        {
            std::lock_guard<std::mutex> lock(entriesMutex);
            if (selected < entries.size())
            {
                auto p = entries[selected].fullPath;
                if (entries[selected].isDir)
                {
                    currentPath = p;
                    selected = 0;
                    scrollOffset = 0;
                    shouldRefresh = true;
                }
            }
        }
        if (shouldRefresh) refreshEntries();
    }
    else if (ch == ' ' || (ch == 13 && ev.ctrl))
    {
        // Toggle selection
        std::lock_guard<std::mutex> lock(entriesMutex);
        if (selected < entries.size() && !entries[selected].isDir)
        {
            entries[selected].isSelected = !entries[selected].isSelected;
        }
    }
    else if (ch == 13)
    {
        bool shouldRefresh = false;
        {
            std::lock_guard<std::mutex> lock(entriesMutex);
            if (selected < entries.size())
            {
                // If not CTRL+Enter, handle single selection logic
                if (!ev.ctrl && !entries[selected].isDir && entries[selected].name != "[Use this directory]" && entries[selected].name != "..") {
                    if (!entries[selected].isSelected) {
                        // Not selected yet, click selects it and clears others
                        for (auto& e : entries) e.isSelected = false;
                        entries[selected].isSelected = true;
                        return; // Selection changed, don't confirm yet
                    }
                    // If already selected, we proceed to selection confirmation below
                }

                if (entries[selected].name == "[Use this directory]")
                {
                    if (showFilesMode) {
                        // Select ALL files in current directory (not recursive)
                        results.clear();
                        for (const auto& entry : entries) {
                            if (!entry.isDir && entry.name != ".." && entry.name != "[Use this directory]") {
                                results.push_back(entry.fullPath.string());
                            }
                        }
                    } else {
                        results.push_back(currentPath.string());
                    }
                    running = false;
                }
                else if (entries[selected].isDir)
                {
                    currentPath = entries[selected].fullPath;
                    selected = 0;
                    scrollOffset = 0;
                    shouldRefresh = true;
                }
                else if (showFilesMode)
                {
                    // Check if we have multiple selected
                    bool hasMulti = false;
                    for (const auto& entry : entries) {
                        if (entry.isSelected) { hasMulti = true; break; }
                    }

                    if (hasMulti) {
                        results.clear();
                        for (const auto& entry : entries) {
                            if (entry.isSelected) results.push_back(entry.fullPath.string());
                        }
                    } else {
                        results.push_back(entries[selected].fullPath.string());
                    }
                    running = false;
                }
            }
        }
        if (shouldRefresh) refreshEntries();
    }
    else if (ch == 'q' || ch == 'Q')
    {
        running = false;
    }
}

void DirectoryBrowserScreen::handleMouse(const InputEvent& ev, bool& running, std::vector<std::string>& results)
{
    if (!running) return;

    int visible = std::max(1, listHeight - 4);
    int rowStartY = listOriginY + 3;

    // Wheel scroll - update both offset and selected to stay in view
    if (ev.wheel != 0)
    {
        scrollOffset = std::clamp(scrollOffset - ev.wheel * 2, 0, std::max(0, static_cast<int>(entries.size()) - visible));
        // Force selection into view
        if (selected < static_cast<size_t>(scrollOffset)) selected = static_cast<size_t>(scrollOffset);
        if (selected >= static_cast<size_t>(scrollOffset + visible)) selected = static_cast<size_t>(scrollOffset + visible - 1);
        return;
    }

    // Scrollbar interaction
    if (scrollX > 0) {
        // Hover detection for the whole scroll column area
        if (ev.x == scrollX && ev.y >= rowStartY && ev.y < rowStartY + visible) {
            hoverScroll = true;
        } else {
            hoverScroll = false;
        }

        if (ev.pressed && ev.button == 0 && hoverScroll) {
            draggingScroll = true;
        } else if (!ev.pressed && !ev.move && ev.wheel == 0) {
            // This is a button release
            draggingScroll = false;
        }

        if (draggingScroll) {
            int totalRows = static_cast<int>(entries.size());
            if (totalRows > visible) {
                double ratio = static_cast<double>(ev.y - rowStartY) / std::max(1, visible - 1);
                scrollOffset = std::clamp(static_cast<int>(ratio * (totalRows - visible)), 0, totalRows - visible);
                // Move selection along to keep it in view during drag
                if (selected < static_cast<size_t>(scrollOffset)) selected = static_cast<size_t>(scrollOffset);
                if (selected >= static_cast<size_t>(scrollOffset + visible)) selected = static_cast<size_t>(scrollOffset + visible - 1);
            }
            return;
        }
    } else {
        hoverScroll = false;
        draggingScroll = false;
    }

    if (!ev.pressed && !ev.move) return;

    int relY = ev.y - rowStartY;
    int relX = ev.x - listOriginX;

    if (relX >= 0 && relX < listWidth && relY >= 0 && relY < visible)
    {
        size_t idx = static_cast<size_t>(scrollOffset + relY);
        bool shouldRefresh = false;

        {
            std::lock_guard<std::mutex> lock(entriesMutex);
            if (idx < entries.size())
            {
                selected = idx;
                
                if (ev.button == 0 && ev.pressed)
                {
                    // Track clicks for double-click detection
                    static size_t lastIdx = static_cast<size_t>(-1);
                    static auto lastTick = std::chrono::steady_clock::now();

                    // CTRL+Click toggles selection
                    if (ev.ctrl && !entries[idx].isDir && entries[idx].name != "[Use this directory]" && entries[idx].name != "..") {
                        entries[idx].isSelected = !entries[idx].isSelected;
                        return;
                    }

                    // If not CTRL+Click, handle single selection logic
                    if (!ev.ctrl && !entries[idx].isDir && entries[idx].name != "[Use this directory]" && entries[idx].name != "..") {
                        if (!entries[idx].isSelected) {
                            // First click on unselected item: clear others and select it
                            for (auto& entry : entries) entry.isSelected = false;
                            entries[idx].isSelected = true;
                            // Still update double click tracking
                            lastIdx = idx;
                            lastTick = std::chrono::steady_clock::now();
                            return;
                        }
                        // If already selected (isSelected == true), we fall through to the confirmation logic below
                        // (which treats it like a double-click confirm)
                    }

                    // 单击选中；双击进入目录（或选择当前目录）
                    auto now = std::chrono::steady_clock::now();
                    bool doubleClick = (idx == lastIdx) && (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count() < 500);
                    
                    // If we reached here without returning, and it was already selected (or it's a dir/control),
                    // we might want to confirm if it's already selected.
                    bool forceConfirm = (!ev.ctrl && !entries[idx].isDir && entries[idx].isSelected);

                    lastIdx = idx;
                    lastTick = now;

                    if (entries[idx].name == "[Use this directory]")
                    {
                        if (showFilesMode) {
                            results.clear();
                            for (const auto& entry : entries) {
                                if (!entry.isDir && entry.name != ".." && entry.name != "[Use this directory]") {
                                    results.push_back(entry.fullPath.string());
                                }
                            }
                        } else {
                            results.push_back(currentPath.string());
                        }
                        running = false;
                    }
                    else if (doubleClick && entries[idx].isDir)
                    {
                        currentPath = entries[idx].fullPath;
                        selected = 0;
                        scrollOffset = 0;
                        shouldRefresh = true;
                    }
                    else if ((doubleClick || forceConfirm) && !entries[idx].isDir && showFilesMode)
                    {
                        // Check if we have multiple selected
                        bool hasMulti = false;
                        for (const auto& entry : entries) {
                            if (entry.isSelected) { hasMulti = true; break; }
                        }

                        if (hasMulti) {
                            results.clear();
                            for (const auto& entry : entries) {
                                if (entry.isSelected) results.push_back(entry.fullPath.string());
                            }
                        } else {
                            results.push_back(entries[idx].fullPath.string());
                        }
                        running = false;
                    }
                }
            }
        }

        if (shouldRefresh) {
            refreshEntries();
        }
    }
}

void DirectoryBrowserScreen::ensureConsoleSize()
{
#ifdef _WIN32
    // no-op; size read in renderFrame
#else
    (void)listWidth;
#endif
}

} // namespace UI
} // namespace TilelandWorld
