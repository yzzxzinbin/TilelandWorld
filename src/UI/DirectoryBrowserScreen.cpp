#include "DirectoryBrowserScreen.h"
#include "TuiUtils.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <system_error>

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

std::string DirectoryBrowserScreen::show()
{
    InputController input(false); // keep VT mouse enabled for caller
    input.setRestoreOnExit(false);
    input.start();

    bool running = true;
    std::string result;

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
                handleMouse(ev, running, result);
                if (!running) break;
            }
            else if (ev.type == InputEvent::Type::Key)
            {
                if (ev.key == InputKey::Character) {
                    handleKey(static_cast<int>(ev.ch), running, result);
                } else if (ev.key == InputKey::Enter) {
                    handleKey(13, running, result);
                }

                // 方向键在 InputController 中是专用枚举，单独处理
                if (ev.key == InputKey::ArrowUp) handleKey(kArrowUp, running, result);
                else if (ev.key == InputKey::ArrowDown) handleKey(kArrowDown, running, result);
                else if (ev.key == InputKey::ArrowLeft) handleKey(kArrowLeft, running, result);
                else if (ev.key == InputKey::ArrowRight) handleKey(kArrowRight, running, result);
                if (!running) break;
            }
        }
    }

    painter.reset();
    input.stop();
    return result;
}

void DirectoryBrowserScreen::refreshEntries()
{
    entries.clear();

    // 当前目录占位，用于直接选择
    entries.push_back(Entry{"[Use this directory]", currentPath, true});

    // 上级目录
    if (!isRoot(currentPath))
    {
        entries.push_back(Entry{"..", currentPath.parent_path(), true});
    }

    std::vector<Entry> dirs;
    std::vector<Entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(currentPath))
    {
        if (entry.is_directory())
        {
            Entry e;
            e.name = entry.path().filename().string();
            e.fullPath = entry.path();
            e.isDir = true;
            dirs.push_back(std::move(e));
        }
        else if (showFilesMode && entry.is_regular_file())
        {
            if (extensionFilter.empty() || entry.path().extension() == extensionFilter) {
                Entry e;
                e.name = entry.path().filename().string();
                e.fullPath = entry.path();
                e.isDir = false;
                files.push_back(std::move(e));
            }
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const Entry& a, const Entry& b){ return a.name < b.name; });
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b){ return a.name < b.name; });

    entries.insert(entries.end(), dirs.begin(), dirs.end());
    entries.insert(entries.end(), files.begin(), files.end());
    clampSelection();
}

void DirectoryBrowserScreen::clampSelection()
{
    if (entries.empty()) { selected = 0; scrollOffset = 0; return; }
    if (selected >= entries.size()) selected = entries.size() - 1;
    int visible = std::max(1, listHeight - 2);
    if (selected < static_cast<size_t>(scrollOffset)) scrollOffset = static_cast<int>(selected);
    if (selected >= static_cast<size_t>(scrollOffset + visible)) scrollOffset = static_cast<int>(selected) - visible + 1;
    scrollOffset = std::max(0, std::min(scrollOffset, static_cast<int>(entries.size()) - visible));
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

    listWidth = std::max(40, surface.getWidth() - 8);
    listHeight = std::max(12, surface.getHeight() - 10);
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

    int visible = std::max(1, listHeight - 2);
    int start = scrollOffset;
    int end = std::min(static_cast<int>(entries.size()), start + visible);
    int rowY = listOriginY + 1;
    for (int i = start; i < end; ++i)
    {
        bool focus = static_cast<size_t>(i) == selected;
        RGBColor fg = focus ? theme.focusFg : theme.itemFg;
        RGBColor bg = focus ? theme.focusBg : theme.itemBg;
        std::string label = entries[static_cast<size_t>(i)].name;
        if (entries[static_cast<size_t>(i)].isDir && label != "[Use this directory]")
        {
            label = "[" + entries[static_cast<size_t>(i)].name + "]";
        }
        int areaWidth = listWidth - 4;
        size_t labelWidth = TuiUtils::calculateUtf8VisualWidth(label);
        if (labelWidth > static_cast<size_t>(areaWidth)) {
            label = TuiUtils::trimToUtf8VisualWidth(label, static_cast<size_t>(areaWidth));
            labelWidth = TuiUtils::calculateUtf8VisualWidth(label);
        }

        surface.drawText(listOriginX + 2, rowY + (i - start), label, fg, bg);
        int remain = areaWidth - static_cast<int>(labelWidth);
        if (remain > 0)
        {
            surface.fillRect(listOriginX + 2 + static_cast<int>(labelWidth), rowY + (i - start), remain, 1, fg, bg, " ");
        }
    }

    std::string hint = "Enter/Right: open | Space: choose | Backspace/Left: up | Q: cancel | Wheel/Click to navigate";
    surface.drawCenteredText(0, surface.getHeight() - 3, surface.getWidth(), hint, theme.hintFg, theme.background);
}

void DirectoryBrowserScreen::handleKey(int ch, bool& running, std::string& result)
{
    if (!running) return;

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
            selected = 0;
            scrollOffset = 0;
            refreshEntries();
        }
    }
    else if (ch == kArrowRight)
    {
        // open
        if (selected < entries.size())
        {
            auto p = entries[selected].fullPath;
            if (entries[selected].isDir)
            {
                currentPath = p;
                selected = 0;
                scrollOffset = 0;
                refreshEntries();
            }
        }
    }
    else if (ch == ' ')
    {
        if (selected < entries.size())
        {
            result = entries[selected].fullPath.string();
            running = false;
        }
    }
    else if (ch == 13)
    {
        if (selected < entries.size())
        {
            if (entries[selected].name == "[Use this directory]")
            {
                result = currentPath.string();
                running = false;
            }
            else if (entries[selected].isDir)
            {
                currentPath = entries[selected].fullPath;
                selected = 0;
                scrollOffset = 0;
                refreshEntries();
            }
            else if (showFilesMode)
            {
                result = entries[selected].fullPath.string();
                running = false;
            }
        }
    }
    else if (ch == 'q' || ch == 'Q')
    {
        running = false;
    }
}

void DirectoryBrowserScreen::handleMouse(const InputEvent& ev, bool& running, std::string& result)
{
    if (!running) return;

    // Wheel scroll
    if (ev.wheel != 0)
    {
        int visible = std::max(1, listHeight - 2);
        scrollOffset = std::max(0, std::min(scrollOffset - ev.wheel * 2, std::max(0, static_cast<int>(entries.size()) - visible)));
        clampSelection();
        return;
    }

    if (!ev.pressed && !ev.move)
    {
        // release without press
        return;
    }

    int relY = ev.y - listOriginY - 1;
    int relX = ev.x - listOriginX;
    int visible = std::max(1, listHeight - 2);

    if (relX >= 0 && relX < listWidth && relY >= 0 && relY < visible)
    {
        size_t idx = static_cast<size_t>(scrollOffset + relY);
        if (idx < entries.size())
        {
            // 悬停高亮
            selected = idx;
            clampSelection();

            if (ev.button == 0 && ev.pressed)
            {
                // 单击选中；双击进入目录（或选择当前目录）
                static size_t lastIdx = static_cast<size_t>(-1);
                static auto lastTick = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                bool doubleClick = (idx == lastIdx) && (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count() < 500);
                lastIdx = idx;
                lastTick = now;

                if (entries[idx].name == "[Use this directory]")
                {
                    result = currentPath.string();
                    running = false;
                }
                else if (doubleClick && entries[idx].isDir)
                {
                    currentPath = entries[idx].fullPath;
                    selected = 0;
                    scrollOffset = 0;
                    refreshEntries();
                }
                else if (doubleClick && !entries[idx].isDir && showFilesMode)
                {
                    result = entries[idx].fullPath.string();
                    running = false;
                }
            }
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
