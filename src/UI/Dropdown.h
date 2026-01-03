#pragma once
#ifndef TILELANDWORLD_UI_DROPDOWN_H
#define TILELANDWORLD_UI_DROPDOWN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <functional>

namespace TilelandWorld {
namespace UI {

struct DropdownStyle {
    int width{20};
    RGBColor focusFg{0, 0, 0};
    RGBColor focusBg{200, 230, 255};
    RGBColor panelBg{18, 21, 28};
    RGBColor itemFg{210, 215, 224};
    RGBColor accent{96, 140, 255};
    RGBColor trackBase{18, 21, 28}; // 背景底色，用于在行高亮时保持自身颜色
};

struct DropdownState {
    bool focused{false};
    bool expanded{false};
    int hoverIndex{-1};
    
    // 记录展开时的位置，用于鼠标点击判定
    int lastX{0};
    int lastY{0};
    int lastW{0};
};

class Dropdown {
public:
    /**
     * 渲染下拉框
     * @param surface 目标画布
     * @param x 坐标
     * @param y 坐标
     * @param options 选项列表
     * @param selectedIndex 当前选中项索引
     * @param state 运行时的状态
     * @param style 视觉样式
     */
    static void render(TuiSurface& surface, int x, int y, const std::vector<std::string>& options, int selectedIndex, DropdownState& state, const DropdownStyle& style);

    /**
     * 处理输入事件
     * @return 如果选中项发生了改变，返回 true
     */
    static bool handleInput(const InputEvent& ev, const std::vector<std::string>& options, int& selectedIndex, DropdownState& state);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_DROPDOWN_H
