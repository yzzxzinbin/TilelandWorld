#pragma once
#ifndef TILELANDWORLD_UI_SETTINGSSCREEN_H
#define TILELANDWORLD_UI_SETTINGSSCREEN_H

#include "AnsiTui.h"
#include "../Settings.h"
#include "../Controllers/InputController.h"
#include <functional>
#include <vector>
#include <chrono>

namespace TilelandWorld {
namespace UI {

class SettingsScreen {
public:
    explicit SettingsScreen(Settings& settings);
    // 返回 true 表示保存并应用，false 表示取消
    bool show();

private:
    enum class ItemType { Toggle, Number, Float, Directory };

    Settings& target;
    Settings working; // 可编辑副本

    TuiSurface surface;
    TuiPainter painter;

    struct Item {
        std::string label;
        ItemType type{ItemType::Number};
        std::function<void(int)> adjust; // dir: -1/1
        std::function<std::string()> value;
        std::function<bool()> isOn; // only for toggles
        std::function<void()> onActivate; // double-click or Enter while focused
        std::function<void(double)> setNumber; // for Number/Float edits
        double minVal{0.0};
        double maxVal{0.0};
        double step{1.0};
        bool isInt{false};
    };

    std::vector<Item> items;
    size_t selected{0};

    // 编辑状态
    int editingIndex{-1};
    std::string editingBuffer;
    double editMin{0.0};
    double editMax{0.0};
    bool editIsInt{false};

    // 布局缓存用于鼠标命中
    int listStartY{5};
    int listLabelX{4};
    int listValueX{0};

    // 双击判定
    size_t lastClickIndex{static_cast<size_t>(-1)};
    std::chrono::steady_clock::time_point lastClickTime{};

    // Toggle 动画闪烁时间戳
    std::vector<std::chrono::steady_clock::time_point> toggleFlash;
    std::vector<bool> togglePrevState;

    // 悬停状态
    size_t hoverToggleIndex{static_cast<size_t>(-1)};
    bool hoverToggleHot{false};

    // 当前活跃的输入控制器（用于嵌套窗口暂停/恢复）
    InputController* activeInput{nullptr};

    void buildItems();
    void renderFrame();
    void handleKey(int key, bool& running, bool& accepted);
    void handleMouse(const InputEvent& ev, bool& running, bool& accepted);
    void startNumericEdit(size_t idx);
    void commitEdit();
    void cancelEdit();
    void markToggleAnimation(size_t idx);
    void openDirectoryPicker();
    void apply();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_SETTINGSSCREEN_H
