#include "SaveManagerScreen.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iostream>
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

SaveManagerScreen::SaveManagerScreen(std::string saveDirectory)
    : directory(std::move(saveDirectory)), surface(96, 32), menu({}, theme) {
    refreshList();
}

SaveManagerScreen::Result SaveManagerScreen::show() {
    ensureAnsiEnabled();
    bool running = true;
    Result result{};

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
        } else if (key == 'r' || key == 'R') {
            refreshList();
        } else if (key == 'd' || key == 'D') {
            if (deleteSelected()) {
                refreshList();
            }
        } else if (key == 13) { // Enter
            size_t idx = menu.getSelected();
            if (idx < saves.size()) {
                result.action = Result::Action::Load;
                result.saveName = saves[idx];
                running = false;
            } else if (idx == saves.size()) { // New
                result = handleCreateNew();
                if (result.action != Result::Action::Back) {
                    running = false;
                }
            } else { // Back
                result.action = Result::Action::Back;
                running = false;
            }
        } else if (key == 27 || key == 'q' || key == 'Q') { // Esc / Q
            result.action = Result::Action::Back;
            running = false;
        }
    }

    painter.reset();
    return result;
}

void SaveManagerScreen::refreshList() {
    std::filesystem::create_directories(directory);

    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext == ".tlwf" || ext == ".tlwz") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    saves = names;

    std::vector<std::string> items = saves;
    items.push_back("New Save");
    items.push_back("Back");
    menu = MenuView(items, theme);
    menu.setTitle("Save Manager");
    menu.setSubtitle("Enter=load/new, D=delete, R=refresh, Esc=back");
}

void SaveManagerScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(40, info.srWindow.Right - info.srWindow.Left + 1);
        int consoleHeight = std::max(20, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(consoleWidth, consoleHeight);
    }
#endif

    surface.clear(theme.itemFg, theme.background, " ");
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");

    int padding = 4;
    int panelWidth = std::max(32, surface.getWidth() - padding * 2);
    int originX = padding;
    int originY = std::max(2, surface.getHeight() / 6);

    menu.render(surface, originX, originY, panelWidth);

    std::string dirLabel = "Dir: " + directory;
    surface.drawText(2, surface.getHeight() - 3, dirLabel, theme.hintFg, theme.background);
}

int SaveManagerScreen::pollKey() {
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

void SaveManagerScreen::ensureAnsiEnabled() {
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

SaveManagerScreen::Result SaveManagerScreen::handleCreateNew() {
    Result res{};
    res.action = Result::Action::Back;

    painter.reset();
    std::cout << "\x1b[2J\x1b[H";
    std::cout << "Enter save name (letters/numbers). Leave empty for generated: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        name = "world-" + std::to_string(static_cast<long long>(now & 0xFFFFFFFF));
    }
    // Sanitize simple
    std::replace_if(name.begin(), name.end(), [](char c){ return c == '/' || c == '\\' || c == ' ' || c == ':' || c == '*'; }, '_');

    WorldMetadata meta{};
    auto seedNow = std::chrono::steady_clock::now().time_since_epoch().count();
    meta.seed = static_cast<int64_t>(seedNow & 0x7FFFFFFF);

    res.action = Result::Action::CreateNew;
    res.saveName = name;
    res.metadata = meta;
    return res;
}

bool SaveManagerScreen::deleteSelected() {
    size_t idx = menu.getSelected();
    if (idx >= saves.size()) return false;
    std::string name = saves[idx];
    auto tlwf = MapSerializer::getTlwfPath(name, directory);
    auto tlwz = MapSerializer::getTlwzPath(name, directory);
    bool removed = false;
    try {
        removed = std::filesystem::remove(tlwf) || removed;
        removed = std::filesystem::remove(tlwz) || removed;
    } catch (...) {
        return false;
    }
    return removed;
}

} // namespace UI
} // namespace TilelandWorld
