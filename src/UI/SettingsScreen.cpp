#include "SettingsScreen.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    double clampDouble(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
    int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

    constexpr int kArrowUp = 0x100 | 72;
    constexpr int kArrowDown = 0x100 | 80;
    constexpr int kArrowLeft = 0x100 | 75;
    constexpr int kArrowRight = 0x100 | 77;
}

SettingsScreen::SettingsScreen(Settings& settings)
    : target(settings), working(settings), surface(100, 40) {
    buildItems();
}

void SettingsScreen::buildItems() {
    items.clear();

    size_t idx = items.size();
    items.push_back(Item{
        "FPS limit",
        ItemType::Number,
        [this](int dir){ working.targetFpsLimit = clampDouble(working.targetFpsLimit + dir * 5.0, 30.0, 1440.0); },
        [this](){ return std::to_string(static_cast<int>(working.targetFpsLimit)); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.targetFpsLimit = clampDouble(v, 30.0, 1440.0); },
        30.0, 1440.0, 5.0, true
    });

    idx = items.size();
    items.push_back(Item{
        "Target TPS",
        ItemType::Number,
        [this](int dir){ working.targetTps = clampDouble(working.targetTps + dir * 1.0, 10.0, 240.0); },
        [this](){ return std::to_string(static_cast<int>(working.targetTps)); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.targetTps = clampDouble(v, 10.0, 240.0); },
        10.0, 240.0, 1.0, true
    });

    idx = items.size();
    items.push_back(Item{
        "Stats overlay alpha",
        ItemType::Float,
        [this](int dir){ working.statsOverlayAlpha = clampDouble(working.statsOverlayAlpha + dir * 0.05, 0.0, 1.0); },
        [this](){ std::ostringstream ss; ss << std::fixed << std::setprecision(2) << working.statsOverlayAlpha; return ss.str(); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.statsOverlayAlpha = clampDouble(v, 0.0, 1.0); },
        0.0, 1.0, 0.05, false
    });

    idx = items.size();
    items.push_back(Item{
        "Mouse cross alpha",
        ItemType::Float,
        [this](int dir){ working.mouseCrossAlpha = clampDouble(working.mouseCrossAlpha + dir * 0.05, 0.0, 1.0); },
        [this](){ std::ostringstream ss; ss << std::fixed << std::setprecision(2) << working.mouseCrossAlpha; return ss.str(); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.mouseCrossAlpha = clampDouble(v, 0.0, 1.0); },
        0.0, 1.0, 0.05, false
    });

    items.push_back(Item{
        "Show stats overlay",
        ItemType::Toggle,
        [this](int dir){ (void)dir; working.enableStatsOverlay = !working.enableStatsOverlay; },
        [this](){ return working.enableStatsOverlay ? "On" : "Off"; },
        [this](){ return working.enableStatsOverlay; },
        {}, {}, 0,0,0,false
    });

    items.push_back(Item{
        "Show mouse cross",
        ItemType::Toggle,
        [this](int dir){ (void)dir; working.enableMouseCross = !working.enableMouseCross; },
        [this](){ return working.enableMouseCross ? "On" : "Off"; },
        [this](){ return working.enableMouseCross; },
        {}, {},0,0,0,false
    });

    items.push_back(Item{
        "Diff-based rendering",
        ItemType::Toggle,
        [this](int dir){ (void)dir; working.enableDiffRendering = !working.enableDiffRendering; },
        [this](){ return working.enableDiffRendering ? "On" : "Off"; },
        [this](){ return working.enableDiffRendering; },
        {}, {},0,0,0,false
    });

    items.push_back(Item{
        "Auto view size",
        ItemType::Toggle,
        [this](int dir){ (void)dir; working.autoViewSize = !working.autoViewSize; },
        [this](){ return working.autoViewSize ? "On" : "Off"; },
        [this](){ return working.autoViewSize; },
        {}, {},0,0,0,false
    });

    items.push_back(Item{
        "Renderer API (fmt)",
        ItemType::Toggle,
        [this](int dir){ (void)dir; working.useFmtRenderer = !working.useFmtRenderer; },
        [this](){ return working.useFmtRenderer ? "fmt" : "std"; },
        [this](){ return working.useFmtRenderer; },
        {}, {},0,0,0,false
    });

    idx = items.size();
    items.push_back(Item{
        "View width",
        ItemType::Number,
        [this](int dir){ working.viewWidth = clampInt(working.viewWidth + dir * 2, 16, 200); },
        [this](){ return std::to_string(working.viewWidth); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.viewWidth = clampInt(static_cast<int>(v), 16, 200); },
        16.0, 200.0, 2.0, true
    });

    idx = items.size();
    items.push_back(Item{
        "View height",
        ItemType::Number,
        [this](int dir){ working.viewHeight = clampInt(working.viewHeight + dir * 2, 16, 120); },
        [this](){ return std::to_string(working.viewHeight); },
        {},
        [this, idx](){ startNumericEdit(idx); },
        [this](double v){ working.viewHeight = clampInt(static_cast<int>(v), 16, 120); },
        16.0, 120.0, 2.0, true
    });

    items.push_back(Item{
        "Save directory",
        ItemType::Directory,
        [this](int dir){ (void)dir; },
        [this](){ return working.saveDirectory; },
        {},
        [this](){ openDirectoryPicker(); },
        {}, 0,0,0,false
    });

    items.push_back(Item{
        "Asset directory",
        ItemType::Directory,
        [this](int dir){ (void)dir; },
        [this](){ return working.assetDirectory; },
        {},
        [this](){ openAssetDirectoryPicker(); },
        {}, 0,0,0,false
    });

    items.push_back(Item{
        "Log Level",
        ItemType::Dropdown,
        [this](int dir) {
            int current = static_cast<int>(working.minLogLevel);
            int next = current + dir;
            if (next < 0) next = 4;
            if (next > 4) next = 0;
            working.minLogLevel = static_cast<LogLevel>(next);
        },
        [this](){
            switch(working.minLogLevel) {
                case LogLevel::LOG_DEBUG: return "DEBUG";
                case LogLevel::LOG_INFO: return "INFO";
                case LogLevel::LOG_WARNING: return "WARN";
                case LogLevel::LOG_ERROR: return "ERROR";
                case LogLevel::LOG_NONE: return "NONE";
                default: return "INFO";
            }
        },
        {}, {}, {},
        0,0,0,false,
        [this](int idx){
            working.minLogLevel = static_cast<LogLevel>(idx);
        },
        {"DEBUG", "INFO", "WARN", "ERROR", "NONE"}
    });

    toggleStates.assign(items.size(), ToggleSwitchState{});
    dropdownStates.assign(items.size(), DropdownState{});
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].type == ItemType::Toggle && items[i].isOn) {
            toggleStates[i].previousOn = items[i].isOn();
        }
    }
}

bool SettingsScreen::show() {
    bool running = true;
    bool accepted = false;
    InputController input;
    activeInput = &input;
    input.start();

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
                handleMouse(ev, running, accepted);
            } else if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Character) {
                    int ch = static_cast<int>(ev.ch);
                    if (ch == 13 || ch == '\n' || ch == '\r') ch = 13;
                    handleKey(ch, running, accepted);
                } else if (ev.key == InputKey::Enter) {
                    handleKey(13, running, accepted);
                } else if (ev.key == InputKey::ArrowUp) {
                    handleKey(kArrowUp, running, accepted);
                } else if (ev.key == InputKey::ArrowDown) {
                    handleKey(kArrowDown, running, accepted);
                } else if (ev.key == InputKey::ArrowLeft) {
                    handleKey(kArrowLeft, running, accepted);
                } else if (ev.key == InputKey::ArrowRight) {
                    handleKey(kArrowRight, running, accepted);
                }
            }
            if (!running) break;
        }
    }

    painter.reset();
    input.stop();
    activeInput = nullptr;

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
    surface.drawText(2, 3, "Mouse: hover/click | Double-click numbers to type | Enter: save | Q: cancel", {160, 170, 190}, {12, 14, 18});

    listStartY = 5;
    listLabelX = 4;
    listValueX = surface.getWidth() / 2 + 4;

    for (size_t i = 0; i < items.size(); ++i) {
        const bool focus = i == selected;
        bool editing = (static_cast<int>(i) == editingIndex);
        RGBColor rowFg = focus ? RGBColor{0, 0, 0} : RGBColor{210, 215, 224};
        RGBColor rowBg = focus ? RGBColor{200, 230, 255} : RGBColor{18, 21, 28};

        surface.fillRect(1, listStartY + static_cast<int>(i), surface.getWidth() - 2, 1, rowFg, rowBg, " ");
        surface.drawText(listLabelX, listStartY + static_cast<int>(i), items[i].label, rowFg, rowBg);

        std::string val = editing ? ("[ " + editingBuffer + " ]") : items[i].value();

        if (items[i].type == ItemType::Toggle && items[i].isOn) {
            ToggleSwitchStyle style{};
            ToggleSwitchState state = (toggleStates.size() > i) ? toggleStates[i] : ToggleSwitchState{};
            state.hover = (hoverToggleIndex == i);
            state.hot = (hoverToggleHot && hoverToggleIndex == i);
            ToggleSwitch::render(surface, listValueX, listStartY + static_cast<int>(i), items[i].isOn(), state, style);
        } else if (items[i].type == ItemType::Dropdown) {
            DropdownStyle style{};
            style.width = 16;
            style.focusFg = {0, 0, 0};
            style.focusBg = {200, 230, 255};
            style.panelBg = {18, 21, 28};
            style.itemFg = {210, 215, 224};
            style.accent = {96, 140, 255};
            
            int currentIdx = 0;
            if (items[i].label == "Log Level") {
                currentIdx = static_cast<int>(working.minLogLevel);
            }
            
            DropdownState& dState = dropdownStates[i];
            dState.focused = focus;
            Dropdown::render(surface, listValueX, listStartY + static_cast<int>(i), items[i].dropdownOptions, currentIdx, dState, style);
        } else {
            surface.drawText(listValueX, listStartY + static_cast<int>(i), val, rowFg, rowBg);
        }
    }
}

void SettingsScreen::apply() {
    target = working;
    // 将需要在确认时才生效的运行时设置应用到系统
    TilelandWorld::Logger::getInstance().setLogLevel(target.minLogLevel);
}

void SettingsScreen::handleKey(int key, bool& running, bool& accepted) {
    if (!running) return;

    // 如果当前选中的是 Dropdown 且处于展开状态，优先处理
    if (selected < items.size() && items[selected].type == ItemType::Dropdown && dropdownStates[selected].expanded) {
        InputEvent ev;
        ev.type = InputEvent::Type::Key;
        if (key == kArrowUp) ev.key = InputKey::ArrowUp;
        else if (key == kArrowDown) ev.key = InputKey::ArrowDown;
        else if (key == 13) ev.key = InputKey::Enter;
        else if (key == 27) ev.key = InputKey::Escape;
        
        int dummyIdx = 0;
        if (items[selected].label == "Log Level") dummyIdx = static_cast<int>(working.minLogLevel);
        
        if (Dropdown::handleInput(ev, items[selected].dropdownOptions, dummyIdx, dropdownStates[selected])) {
            if (items[selected].setDropdown) items[selected].setDropdown(dummyIdx);
        }
        return;
    }

    // 编辑模式：仅处理数字输入/退格/提交
    if (editingIndex >= 0) {
        if (key == 8 || key == 0x7F) {
            if (!editingBuffer.empty()) editingBuffer.pop_back();
        } else if (key == 13) {
            commitEdit();
            editingIndex = -1;
        } else if (key == 'q' || key == 'Q') {
            cancelEdit();
        } else if (key >= '0' && key <= '9') {
            editingBuffer.push_back(static_cast<char>(key));
        } else if (!editIsInt && (key == '.' || key == '-')) {
            editingBuffer.push_back(static_cast<char>(key));
        }
        return;
    }

    if (key == kArrowUp || key == 'w' || key == 'W') {
        if (selected == 0) selected = items.size() - 1; else --selected;
    } else if (key == kArrowDown || key == 's' || key == 'S') {
        selected = (selected + 1) % items.size();
    } else if (key == kArrowLeft || key == 'a' || key == 'A') {
        if (items[selected].adjust) {
            items[selected].adjust(-1);
            markToggleAnimation(selected);
        }
    } else if (key == kArrowRight || key == 'd' || key == 'D' || key == ' ') {
        if (items[selected].adjust) {
            items[selected].adjust(1);
            markToggleAnimation(selected);
        }
    } else if (key == 13) { // Enter 保存
        accepted = true;
        running = false;
    } else if (key == 'q' || key == 'Q') {
        accepted = false;
        running = false;
    } else if (key == 'e' || key == 'E') {
        if (items[selected].type == ItemType::Number || items[selected].type == ItemType::Float) {
            startNumericEdit(selected);
        }
    }
}

void SettingsScreen::handleMouse(const InputEvent& ev, bool& running, bool& accepted) {
    if (!running) return;

    hoverToggleIndex = static_cast<size_t>(-1);
    hoverToggleHot = false;
    for (auto& st : toggleStates) { st.hover = false; st.hot = false; }

    if (ev.wheel != 0) {
        if (ev.wheel > 0) {
            if (selected == 0) selected = items.size() - 1; else --selected;
        } else {
            selected = (selected + 1) % items.size();
        }
        return;
    }

    if (!ev.pressed && !ev.move) return;

    int relY = ev.y - listStartY;
    
    // 优先处理 Dropdown 的鼠标事件
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].type == ItemType::Dropdown) {
            int dummyIdx = 0;
            if (items[i].label == "Log Level") dummyIdx = static_cast<int>(working.minLogLevel);
            if (Dropdown::handleInput(ev, items[i].dropdownOptions, dummyIdx, dropdownStates[i])) {
                if (items[i].setDropdown) items[i].setDropdown(dummyIdx);
                return;
            }
            // 如果 Dropdown 展开了，拦截所有其他鼠标事件
            if (dropdownStates[i].expanded) return;
        }
    }

    if (relY < 0 || relY >= static_cast<int>(items.size())) return;
    size_t idx = static_cast<size_t>(relY);

    selected = idx; // 悬停高亮

    if (items[idx].type == ItemType::Toggle) {
        int trackLen = 16;
        int trackX = listValueX;
        if (toggleStates.size() > idx) {
            if (ev.x >= trackX && ev.x < trackX + trackLen) {
                hoverToggleIndex = idx;
                hoverToggleHot = true;
                toggleStates[idx].hover = true;
                toggleStates[idx].hot = true;
            } else {
                hoverToggleIndex = idx;
                toggleStates[idx].hover = true;
                toggleStates[idx].hot = false;
            }
        }
    }

    if (ev.button == 0 && ev.pressed) {
        auto now = std::chrono::steady_clock::now();
        bool doubleClick = (lastClickIndex == idx) && (now - lastClickTime < std::chrono::milliseconds(400));
        lastClickIndex = idx;
        lastClickTime = now;

        auto& it = items[idx];

        if (doubleClick) {
            if (it.type == ItemType::Number || it.type == ItemType::Float) {
                startNumericEdit(idx);
            } else if (it.type == ItemType::Directory && it.onActivate) {
                it.onActivate();
            } else if (it.type == ItemType::Toggle) {
                it.adjust(1);
                markToggleAnimation(idx);
            }
            return;
        }

        if (it.type == ItemType::Toggle) {
            it.adjust(1);
            markToggleAnimation(idx);
        } else if (it.type == ItemType::Directory && it.onActivate) {
            it.onActivate();
        } else if (it.type == ItemType::Number || it.type == ItemType::Float) {
            // single click just selects; use keys/wheel or double-click to edit
        }
    }
}

void SettingsScreen::startNumericEdit(size_t idx) {
    if (idx >= items.size()) return;
    auto& it = items[idx];
    if (it.type != ItemType::Number && it.type != ItemType::Float) return;
    editingIndex = static_cast<int>(idx);
    editingBuffer = it.value();
    editMin = it.minVal;
    editMax = it.maxVal;
    editIsInt = it.isInt;
}

void SettingsScreen::commitEdit() {
    if (editingIndex < 0 || static_cast<size_t>(editingIndex) >= items.size()) {
        editingIndex = -1;
        return;
    }
    auto& it = items[static_cast<size_t>(editingIndex)];
    double val = 0.0;
    try {
        if (editIsInt) {
            val = static_cast<double>(std::stoi(editingBuffer));
        } else {
            val = std::stod(editingBuffer);
        }
    } catch (...) {
        editingIndex = -1;
        return;
    }
    val = clampDouble(val, editMin, editMax);
    if (it.setNumber) {
        it.setNumber(val);
    }
    editingIndex = -1;
}

void SettingsScreen::cancelEdit() {
    editingIndex = -1;
    editingBuffer.clear();
}

void SettingsScreen::openDirectoryPicker() {
    painter.reset();
    if (activeInput) {
        activeInput->stop();
    }

    DirectoryBrowserScreen browser(working.saveDirectory);
    std::string chosen = browser.show();
    if (!chosen.empty()) {
        working.saveDirectory = chosen;
    }

    if (activeInput) {
        activeInput->start();
    }
}

void SettingsScreen::openAssetDirectoryPicker() {
    painter.reset();
    if (activeInput) {
        activeInput->stop();
    }

    DirectoryBrowserScreen browser(working.assetDirectory);
    std::string chosen = browser.show();
    if (!chosen.empty()) {
        working.assetDirectory = chosen;
    }

    if (activeInput) {
        activeInput->start();
    }
}

void SettingsScreen::markToggleAnimation(size_t idx) {
    if (idx >= items.size()) return;
    if (items[idx].type != ItemType::Toggle) return;
    if (toggleStates.size() < items.size()) toggleStates.resize(items.size());
    toggleStates[idx].previousOn = !items[idx].isOn();
    toggleStates[idx].lastToggle = std::chrono::steady_clock::now();
}

} // namespace UI
} // namespace TilelandWorld
