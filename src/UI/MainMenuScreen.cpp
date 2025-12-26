#include "MainMenuScreen.h"
#include "TuiUtils.h"
#include <algorithm>
#include <thread>
#include <vector>
#include <string>
#include "../Controllers/InputController.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    constexpr int kArrowUp = 0x100 | 72;
    constexpr int kArrowDown = 0x100 | 80;
    const BoxStyle kModernFrame{"╭", "╮", "╰", "╯", "─", "│"};
    const std::vector<std::string> kBannerLines = {
        "████████╗██╗██╗     ███████╗██╗      █████╗ ███╗   ██╗██████╗     ██╗    ██╗ ██████╗ ██████╗ ██╗     ██████╗ ",
        "╚══██╔══╝██║██║     ██╔════╝██║     ██╔══██╗████╗  ██║██╔══██╗    ██║    ██║██╔═══██╗██╔══██╗██║     ██╔══██╗",
        "   ██║   ██║██║     █████╗  ██║     ███████║██╔██╗ ██║██║  ██║    ██║ █╗ ██║██║   ██║██████╔╝██║     ██║  ██║",
        "   ██║   ██║██║     ██╔══╝  ██║     ██╔══██║██║╚██╗██║██║  ██║    ██║███╗██║██║   ██║██╔══██╗██║     ██║  ██║",
        "   ██║   ██║███████╗███████╗███████╗██║  ██║██║ ╚████║██████╔╝    ╚███╔███╔╝╚██████╔╝██║  ██║███████╗██████╔╝",
        "   ╚═╝   ╚═╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝      ╚══╝╚══╝  ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═════╝ "
    };
}

MainMenuScreen::MainMenuScreen()
    : surface(96, 32),
      menu({"Start Game", "Settings", "Quit"}, theme) {
    // 重新应用主题，确保菜单使用已初始化的 theme（成员顺序已调整）。
    menu.setTitle("Tileland World");
    menu.setSubtitle("Click or arrows + Enter · Q exits");
    menu.setFrameStyle(kModernFrame);
}

MainMenuScreen::Action MainMenuScreen::show() {
    ensureAnsiEnabled();

    InputController input;
    input.start();

    bool running = true;
    Action result = Action::Quit;

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
                handleMouse(ev, running, result);
            } else if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Character) {
                    int ch = static_cast<int>(ev.ch);
                    // 把回车字符也视为 Enter，避免多按一次
                    if (ch == 13 || ch == '\n' || ch == '\r') {
                        handleKey(13, running, result);
                    } else {
                        handleKey(ch, running, result);
                    }
                } else if (ev.key == InputKey::Enter) {
                    handleKey(13, running, result);
                } else if (ev.key == InputKey::ArrowUp) {
                    handleKey(kArrowUp, running, result);
                } else if (ev.key == InputKey::ArrowDown) {
                    handleKey(kArrowDown, running, result);
                }
            }
            if (!running) break;
        }
    }

    painter.reset();
    input.stop();
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

    int bannerStartY = 2;
    int bannerHeight = static_cast<int>(kBannerLines.size());
    for (int i = 0; i < bannerHeight; ++i) {
        double fade = static_cast<double>(i) / std::max(1, bannerHeight - 1);
        RGBColor rowBg = TuiUtils::blendColor(theme.accent, theme.panel, 0.35 + fade * 0.15);
        RGBColor rowFg = TuiUtils::blendColor(theme.title, theme.focusBg, 0.4 + fade * 0.1);
        surface.fillRect(0, bannerStartY + i, surface.getWidth(), 1, rowFg, rowBg, " ");
        std::string line = kBannerLines[static_cast<size_t>(i)];
        size_t vis = TuiUtils::calculateUtf8VisualWidth(line);
        int maxVis = surface.getWidth();
        if (static_cast<int>(vis) > maxVis) {
            line = TuiUtils::trimToUtf8VisualWidth(line, static_cast<size_t>(maxVis));
        }
        surface.drawCenteredText(0, bannerStartY + i, surface.getWidth(), line, rowFg, rowBg);
    }

    // 顶部和底部的强调色条，营造现代简约感
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");

    int padding = 4;
    int panelWidth = std::max(32, surface.getWidth() - padding * 2);
    int originX = padding;
    int bannerBottom = bannerStartY + bannerHeight;
    int originY = std::max(bannerBottom + 2, surface.getHeight() / 4);

    lastPanelX = originX;
    lastPanelY = originY;
    lastPanelWidth = panelWidth;
    lastListStart = originY + 4;
    lastListCount = static_cast<int>(menu.getItems().size());

    menu.render(surface, originX, originY, panelWidth);

    int infoHeight = 2;
    int infoOriginY = surface.getHeight() - infoHeight - 2;
    surface.fillRect(0, infoOriginY, surface.getWidth(), infoHeight, theme.panel, theme.panel, " ");
    surface.drawCenteredText(0, infoOriginY, surface.getWidth(), "Procedural walls, endless plains", theme.subtitle, theme.panel);
    std::string versionLine = "Version " + std::string(kVersion) + " • build " + kBuildTimestamp;
    surface.drawCenteredText(0, infoOriginY + 1, surface.getWidth(), versionLine, theme.hintFg, theme.panel);
}

void MainMenuScreen::handleKey(int key, bool& running, Action& result) {
    if (!running) return;
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
    } else if (key == 'q' || key == 'Q') {
        running = false;
        result = Action::Quit;
    }
}

void MainMenuScreen::handleMouse(const InputEvent& ev, bool& running, Action& result) {
    if (!running) return;
    if (ev.wheel != 0) {
        if (ev.wheel > 0) menu.moveUp(); else menu.moveDown();
        return;
    }

    int relY = ev.y - lastListStart;
    int relX = ev.x - lastPanelX;
    if (relX < 0 || relX >= lastPanelWidth) return;
    if (relY < 0 || relY >= lastListCount) return;

    size_t idx = static_cast<size_t>(relY);
    if (idx < menu.getItems().size()) {
        int areaWidth = std::max(0, lastPanelWidth - 4);
        int localX = std::clamp(relX - 2, 0, areaWidth);
        double originNorm = (areaWidth > 0) ? static_cast<double>(localX) / static_cast<double>(areaWidth) : 0.0;
        menu.setSelectedWithOrigin(idx, originNorm);

        if (ev.button == 0 && ev.pressed) {
            handleKey(13, running, result); // 左键单击激活
        }
    }
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
