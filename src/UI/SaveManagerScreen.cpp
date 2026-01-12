#include "SaveManagerScreen.h"
#include "SaveCreationScreen.h"
#include "TuiUtils.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include "../Map.h"
#include "../Settings.h"
#include "../MapGenInfrastructure/TerrainGeneratorFactory.h"
#include "../Controllers/TuiCoreController.h"
#include "../Utils/Logger.h"
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <sstream>
#include <iomanip>
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
}

SaveManagerScreen::SaveManagerScreen(Settings& settings)
    : settings(settings), surface(96, 32), menu({}, theme) {
    menu.setFrameStyle(kModernFrame);
    refreshList();
}

void SaveManagerScreen::show() {
    ensureAnsiEnabled();
    InputController input;
    input.start();
    bool running = true;
    Result result{};

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
                handleMouse(ev, running, result, input);
            } else if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Character) {
                    int ch = static_cast<int>(ev.ch);
                    if (ch == 13 || ch == '\n' || ch == '\r') {
                        handleKey(13, running, result, input);
                    } else {
                        handleKey(ch, running, result, input);
                    }
                } else if (ev.key == InputKey::Enter) {
                    handleKey(13, running, result, input);
                } else if (ev.key == InputKey::ArrowUp) {
                    handleKey(kArrowUp, running, result, input);
                } else if (ev.key == InputKey::ArrowDown) {
                    handleKey(kArrowDown, running, result, input);
                }
            }
            if (!running) break;
        }
    }

    painter.reset();
    input.stop();
}

void SaveManagerScreen::runGame(std::unique_ptr<Map> map) {
    if (!map) return;

    // 清屏：进入游戏主循环前做一次 ANSI 清屏
    std::cout << "\x1b[2J\x1b[H" << std::flush;

    try {
        TuiCoreController controller(*map, settings);
        LOG_INFO("SaveManager: TuiCoreController created.");

        controller.initialize();
        LOG_INFO("SaveManager: Controller initialized. Entering run loop.");

        controller.run();
        LOG_INFO("SaveManager: Game loop finished. Returning to save manager.");
    } catch (const std::exception& e) {
        LOG_ERROR("SaveManager: Exception during game loop: " + std::string(e.what()));
    }

    // 重新进入存档管理器后，可能需要再次清屏或由 renderFrame 处理
    std::cout << "\x1b[2J\x1b[H" << std::flush;
}

void SaveManagerScreen::refreshList() {
    std::filesystem::create_directories(settings.saveDirectory);

    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(settings.saveDirectory)) {
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
    menu.setFrameStyle(kModernFrame);
    menu.setTitle("Save Manager");
    menu.setSubtitle("Enter/click=load | E edit | D delete | R refresh | Q back");
    menu.setMarkerProvider([this](size_t idx, bool focus) {
        if (!focus) return std::string("  ");
        if (idx < saves.size()) return std::string("🌍");
        return std::string("▶ ");
    });
    infoCache.assign(saves.size(), SaveInfo{});
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

    lastPanelX = originX;
    lastPanelY = originY;
    lastPanelWidth = panelWidth;
    lastListStart = originY + 4;
    lastListCount = static_cast<int>(menu.getItems().size());

    menu.render(surface, originX, originY, panelWidth);

    renderInfoBar();

    std::string dirLabel = "Dir: " + settings.saveDirectory;
    surface.drawText(2, surface.getHeight() - 3, dirLabel, theme.hintFg, theme.background);
}

void SaveManagerScreen::renderInfoBar() {
    int barHeight = 3;
    int y = surface.getHeight() - (barHeight + 3);
    if (y < 0) return;
    surface.fillRect(0, y, surface.getWidth(), barHeight, theme.subtitle, theme.panel, " ");

    size_t idx = menu.getSelected();
    if (idx >= saves.size()) {
        surface.drawText(2, y, "Select a save to view details", theme.hintFg, theme.panel);
        return;
    }

    ensureInfo(idx);
    if (infoCache.size() <= idx || !infoCache[idx].ok) {
        surface.drawText(2, y, "Metadata unavailable for this save", theme.hintFg, theme.panel);
        return;
    }

    const auto& summary = infoCache[idx].summary;
    std::ostringstream line1;
    line1 << "🌍 " << saves[idx] << " | " << (summary.compressed ? ".tlwz" : ".tlwf")
          << " | " << formatBytes(summary.fileSize) << " | chunks: " << summary.chunkCount;
    std::string l1 = line1.str();
    l1 = TuiUtils::trimToUtf8VisualWidth(l1, surface.getWidth() - 4);
    surface.drawText(2, y, l1, theme.title, theme.panel);

    std::ostringstream line2;
    line2 << "Seed " << summary.metadata.seed
          << " | Freq " << std::fixed << std::setprecision(3) << summary.metadata.frequency
          << " | Noise " << summary.metadata.noiseType
          << " | Fractal " << summary.metadata.fractalType
          << " | Oct " << summary.metadata.octaves
          << " | Lac " << std::setprecision(2) << summary.metadata.lacunarity
          << " | Gain " << std::setprecision(2) << summary.metadata.gain;
    std::string l2 = line2.str();
    l2 = TuiUtils::trimToUtf8VisualWidth(l2, surface.getWidth() - 4);
    surface.drawText(2, y + 1, l2, theme.itemFg, theme.panel);

    surface.drawText(2, y + 2, "E: edit parameters for this world", theme.hintFg, theme.panel);
}

void SaveManagerScreen::handleKey(int key, bool& running, Result&, InputController& input) {
    if (!running) return;

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
    } else if (key == 'e' || key == 'E') {
        size_t idx = menu.getSelected();
        if (idx < saves.size()) {
            editSave(idx, input);
        }
    } else if (key == 13) { // Enter
        size_t idx = menu.getSelected();
        if (idx < saves.size()) {
            std::string saveName = saves[idx];
            auto map = MapSerializer::loadMapFromSave(saveName, settings.saveDirectory);
            if (map) {
                LOG_INFO("SaveManager: Loaded save '" + saveName + "'. Starting game.");
                input.stop();
                runGame(std::move(map));
                input.start();
                refreshList();
            } else {
                LOG_ERROR("SaveManager: Failed to load save '" + saveName + "'.");
            }
        } else if (idx == saves.size()) { // New
            input.stop();
            SaveCreationScreen creator(settings.saveDirectory);
            auto form = creator.show();
            if (form.accepted) {
                if (!form.saveDirectory.empty()) {
                    settings.saveDirectory = form.saveDirectory;
                }
                std::filesystem::create_directories(settings.saveDirectory);

                auto map = std::make_unique<Map>(createTerrainGeneratorFromMetadata(form.metadata));
                map->setWorldMetadata(form.metadata);
                MapSerializer::saveCompressedMap(*map, form.saveName, settings.saveDirectory, false);

                LOG_INFO("SaveManager: Created new save '" + form.saveName + "'. Starting game.");
                runGame(std::move(map));
                refreshList();
            }
            input.start();
        } else { // Back
            running = false;
        }
    } else if (key == 'q' || key == 'Q') {
        running = false;
    }
}

void SaveManagerScreen::handleMouse(const InputEvent& ev, bool& running, Result& result, InputController& input) {
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
        double originNorm = areaWidth > 0 ? static_cast<double>(localX) / static_cast<double>(areaWidth) : 0.0;
        menu.setSelectedWithOrigin(idx, originNorm);

        if (ev.button == 0 && ev.pressed) {
            handleKey(13, running, result, input); // 左键单击激活
        }
    }
}

void SaveManagerScreen::ensureInfo(size_t idx) {
    if (idx >= saves.size()) return;
    if (infoCache.size() != saves.size()) {
        infoCache.assign(saves.size(), SaveInfo{});
    }
    auto& slot = infoCache[idx];
    if (slot.loaded) return;
    slot.loaded = true;
    MapSerializer::SaveSummary summary{};
    if (MapSerializer::readSaveSummary(saves[idx], settings.saveDirectory, summary)) {
        slot.ok = true;
        slot.summary = std::move(summary);
    } else {
        slot.ok = false;
    }
}

std::string SaveManagerScreen::formatBytes(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << " " << units[unit];
    return oss.str();
}

bool SaveManagerScreen::editSave(size_t idx, InputController& input) {
    if (idx >= saves.size()) return false;
    ensureInfo(idx);
    if (infoCache.size() <= idx || !infoCache[idx].ok) return false;

    auto meta = infoCache[idx].summary.metadata;
    SaveCreationScreen editor(settings.saveDirectory, meta, saves[idx], true, true);

    input.stop();
    auto form = editor.show();
    input.start();

    if (!form.accepted) return false;

    bool updated = MapSerializer::updateMetadata(saves[idx], settings.saveDirectory, form.metadata);
    if (updated) {
        if (infoCache.size() > idx) {
            infoCache[idx].loaded = false;
            infoCache[idx].ok = false;
        }
    }
    return updated;
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

bool SaveManagerScreen::deleteSelected() {
    size_t idx = menu.getSelected();
    if (idx >= saves.size()) return false;
    std::string name = saves[idx];
    auto tlwf = MapSerializer::getTlwfPath(name, settings.saveDirectory);
    auto tlwz = MapSerializer::getTlwzPath(name, settings.saveDirectory);
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
