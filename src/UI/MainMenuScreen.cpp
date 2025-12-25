#include "MainMenuScreen.h"
#include <algorithm>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    constexpr int kArrowUp = 0x100 | 72;
    constexpr int kArrowDown = 0x100 | 80;
}

MainMenuScreen::MainMenuScreen()
    : surface(96, 32),
      menu({"Start Game", "Settings", "Quit"}, theme) {
    // 重新应用主题，确保菜单使用已初始化的 theme（成员顺序已调整）。
    menu.setTitle("Tileland World");
    menu.setSubtitle("Minimal ANSI TUI - arrow keys + Enter");
}

MainMenuScreen::Action MainMenuScreen::show() {
    ensureAnsiEnabled();

    bool running = true;
    Action result = Action::Quit;

    while (running) {
        renderFrame();
        painter.present(surface, true, 1, 1);

        int key = pollKey();
        if (key == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (key == kArrowUp || key == 'w' || key == 'W') {
            menu.moveUp();
        } else if (key == kArrowDown || key == 's' || key == 'S') {
            menu.moveDown();
        } else if (key == 13) { // Enter
            selectedIndex = menu.getSelected();
            if (selectedIndex == 0) result = Action::Start;
            else if (selectedIndex == 1) result = Action::Settings;
            else result = Action::Quit;
            running = false;
        } else if (key == 27 || key == 'q' || key == 'Q') { // Esc or Q
            running = false;
            result = Action::Quit;
        }
    }

    painter.reset();
    return result;
}

void MainMenuScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(40, info.srWindow.Right - info.srWindow.Left + 1);
        int consoleHeight = std::max(20, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(consoleWidth, consoleHeight);
    }
#endif

    surface.clear(theme.itemFg, theme.background, " ");

    // 顶部和底部的强调色条，营造现代简约感
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");

    int padding = 4;
    int panelWidth = std::max(32, surface.getWidth() - padding * 2);
    int originX = padding;
    int originY = std::max(2, surface.getHeight() / 5);

    menu.render(surface, originX, originY, panelWidth);

    // 底部提示
    surface.drawCenteredText(0, surface.getHeight() - 2, surface.getWidth(), "TilelandWorld CLI Prototype", theme.hintFg, theme.background);
}

int MainMenuScreen::pollKey() {
#ifdef _WIN32
    if (_kbhit()) {
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int ext = _getch();
            return 0x100 | ext;
        }
        return ch;
    }
    return -1;
#else
    return -1;
#endif
}

void MainMenuScreen::ensureAnsiEnabled() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

} // namespace UI
} // namespace TilelandWorld
