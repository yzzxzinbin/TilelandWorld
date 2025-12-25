#include "SettingsScreen.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif
#include "DirectoryBrowserScreen.h"

namespace TilelandWorld {
namespace UI {

namespace {
    double clampDouble(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
    int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
}

SettingsScreen::SettingsScreen(Settings& settings)
    : target(settings), working(settings), surface(100, 40) {
    buildItems();
}

void SettingsScreen::buildItems() {
    items.clear();

    items.push_back(Item{"FPS limit", [this](int dir){ working.targetFpsLimit = clampDouble(working.targetFpsLimit + dir * 5.0, 30.0, 1440.0); }, [this](){ return std::to_string(static_cast<int>(working.targetFpsLimit)); }});
    items.push_back(Item{"Target TPS", [this](int dir){ working.targetTps = clampDouble(working.targetTps + dir * 1.0, 10.0, 240.0); }, [this](){ return std::to_string(working.targetTps); }});
    items.push_back(Item{"Stats overlay alpha", [this](int dir){ working.statsOverlayAlpha = clampDouble(working.statsOverlayAlpha + dir * 0.05, 0.0, 1.0); }, [this](){ return std::to_string(working.statsOverlayAlpha); }});
    items.push_back(Item{"Mouse cross alpha", [this](int dir){ working.mouseCrossAlpha = clampDouble(working.mouseCrossAlpha + dir * 0.05, 0.0, 1.0); }, [this](){ return std::to_string(working.mouseCrossAlpha); }});
    items.push_back(Item{"Show stats overlay", [this](int dir){ (void)dir; working.enableStatsOverlay = !working.enableStatsOverlay; }, [this](){ return working.enableStatsOverlay ? "On" : "Off"; }});
    items.push_back(Item{"Show mouse cross", [this](int dir){ (void)dir; working.enableMouseCross = !working.enableMouseCross; }, [this](){ return working.enableMouseCross ? "On" : "Off"; }});
    items.push_back(Item{"Diff-based rendering", [this](int dir){ (void)dir; working.enableDiffRendering = !working.enableDiffRendering; }, [this](){ return working.enableDiffRendering ? "On" : "Off"; }});

    items.push_back(Item{"View width", [this](int dir){ working.viewWidth = clampInt(working.viewWidth + dir * 2, 16, 200); }, [this](){ return std::to_string(working.viewWidth); }});
    items.push_back(Item{"View height", [this](int dir){ working.viewHeight = clampInt(working.viewHeight + dir * 2, 16, 120); }, [this](){ return std::to_string(working.viewHeight); }});

    items.push_back(Item{"Save directory", [this](int dir){
        (void)dir;
        painter.reset();
        DirectoryBrowserScreen browser(working.saveDirectory);
        std::string chosen = browser.show();
        if (!chosen.empty()) {
            working.saveDirectory = chosen;
        }
    }, [this](){ return working.saveDirectory; }});
}

bool SettingsScreen::show() {
    bool running = true;
    bool accepted = false;

    while (running) {
        renderFrame();
        painter.present(surface, true, 1, 1);

        int key = pollKey();
        if (key == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }

        constexpr int kArrowUp = 0x100 | 72;
        constexpr int kArrowDown = 0x100 | 80;
        constexpr int kArrowLeft = 0x100 | 75;
        constexpr int kArrowRight = 0x100 | 77;

        if (key == kArrowUp || key == 'w' || key == 'W') {
            if (selected == 0) selected = items.size() - 1; else --selected;
        } else if (key == kArrowDown || key == 's' || key == 'S') {
            selected = (selected + 1) % items.size();
        } else if (key == kArrowLeft || key == 'a' || key == 'A') {
            items[selected].adjust(-1);
        } else if (key == kArrowRight || key == 'd' || key == 'D') {
            items[selected].adjust(1);
        } else if (key == ' ' ) {
            items[selected].adjust(1);
        } else if (key == 13) { // Enter
            accepted = true;
            running = false;
        } else if (key == 27 || key == 'q' || key == 'Q') { // Esc / Q
            accepted = false;
            running = false;
        }
    }

    painter.reset();

    if (accepted) {
        apply();
    } else {
        working = target; // 放弃修改
    }

    return accepted;
}

void SettingsScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(60, info.srWindow.Right - info.srWindow.Left + 1);
        int consoleHeight = std::max(20, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(consoleWidth, consoleHeight);
    }
#endif
    surface.clear({220, 230, 240}, {12, 14, 18}, " ");

    surface.fillRect(0, 0, surface.getWidth(), 1, {96, 140, 255}, {96, 140, 255}, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, {96, 140, 255}, {96, 140, 255}, " ");

    surface.drawText(2, 1, "Settings", {0, 0, 0}, {96, 140, 255});
    surface.drawText(2, 3, "Arrow/WASD: navigate | Space/Left/Right: adjust | Enter: save | Esc/Q: cancel", {160, 170, 190}, {12, 14, 18});

    int startY = 5;
    int labelX = 4;
    int valueX = surface.getWidth() / 2 + 4;

    for (size_t i = 0; i < items.size(); ++i) {
        const bool focus = i == selected;
        RGBColor fg = focus ? RGBColor{0, 0, 0} : RGBColor{210, 215, 224};
        RGBColor bg = focus ? RGBColor{200, 230, 255} : RGBColor{18, 21, 28};
        surface.fillRect(1, startY + static_cast<int>(i), surface.getWidth() - 2, 1, fg, bg, " ");
        surface.drawText(labelX, startY + static_cast<int>(i), items[i].label, fg, bg);
        std::string val = items[i].value();
        surface.drawText(valueX, startY + static_cast<int>(i), val, fg, bg);
    }
}

int SettingsScreen::pollKey() {
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

void SettingsScreen::apply() {
    target = working;
}

} // namespace UI
} // namespace TilelandWorld
