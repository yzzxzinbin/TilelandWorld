#include "Dropdown.h"
#include "TuiUtils.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

void Dropdown::render(TuiSurface& surface, int x, int y, const std::vector<std::string>& options, int selectedIndex, DropdownState& state, const DropdownStyle& style) {
    state.lastX = x;
    state.lastY = y;
    state.lastW = style.width;

    // 绘制主框
    RGBColor bg = style.trackBase;
    if (state.focused) {
        bg = TuiUtils::lightenColor(bg, 0.05);
    }
    RGBColor fg = style.itemFg;
    
    surface.fillRect(x, y, style.width, 1, fg, bg, " ");
    
    std::string currentText = (selectedIndex >= 0 && selectedIndex < (int)options.size()) ? options[selectedIndex] : "Select...";
    // 使用视觉宽度截断（考虑多字节/全宽字符），保证右侧留出空列和箭头位置
    std::string arrow = state.expanded ? "▲" : "▼";
    int arrowW = std::max(1, static_cast<int>(TuiUtils::calculateUtf8VisualWidth(arrow)));
    // 留出 1 列左边距、1 列右侧空白，以及箭头宽度
    int avail = std::max(0, style.width - 2 - arrowW);
    currentText = TuiUtils::trimToUtf8VisualWidth(currentText, static_cast<size_t>(avail));
    
    surface.drawText(x + 1, y, currentText, fg, bg);
    // 绘制展开箭头：向左移动一格，使右侧保留一列空白
    surface.drawText(x + style.width - arrowW - 1, y, arrow, style.accent, bg);

    // 如果展开，绘制下拉列表
    if (state.expanded) {
        int listH = (int)options.size();
        // 简单的层级处理：直接在 surface 上覆盖绘制
        for (int i = 0; i < listH; ++i) {
            int ly = y + 1 + i;
            bool isHover = (state.hoverIndex == i);
            bool isSelected = (selectedIndex == i);
            
            RGBColor lbg = isHover ? style.focusBg : style.panelBg;
            RGBColor lfg = isHover ? style.focusFg : (isSelected ? style.accent : style.itemFg);
            
            surface.fillRect(x, ly, style.width, 1, lfg, lbg, " ");
            std::string itemText = TuiUtils::trimToUtf8VisualWidth(options[i], static_cast<size_t>(std::max(0, style.width - 2)));
            surface.drawText(x + 1, ly, itemText, lfg, lbg);
        }
    }
}

bool Dropdown::handleInput(const InputEvent& ev, const std::vector<std::string>& options, int& selectedIndex, DropdownState& state) {
    if (!state.focused && ev.type != InputEvent::Type::Mouse) return false;

    if (ev.type == InputEvent::Type::Mouse) {
        bool inMain = (ev.x >= state.lastX && ev.x < state.lastX + state.lastW && ev.y == state.lastY);
        
        if (state.expanded) {
            bool inList = (ev.x >= state.lastX && ev.x < state.lastX + state.lastW && 
                           ev.y > state.lastY && ev.y <= state.lastY + (int)options.size());
            
            if (inList) {
                state.hoverIndex = ev.y - state.lastY - 1;
                if (ev.pressed && ev.button == 0) {
                    selectedIndex = state.hoverIndex;
                    state.expanded = false;
                    return true;
                }
                return false;
            } else if (ev.pressed) {
                state.expanded = false;
                if (!inMain) state.focused = false;
                return false;
            }
        }

        if (inMain) {
            if (ev.pressed && ev.button == 0) {
                state.focused = true;
                state.expanded = !state.expanded;
                return false;
            }
        } else if (ev.pressed) {
            state.focused = false;
            state.expanded = false;
        }
        return false;
    }

    if (ev.type == InputEvent::Type::Key) {
        if (!state.expanded) {
            if (ev.key == InputKey::Enter || (ev.key == InputKey::Character && ev.ch == ' ')) {
                state.expanded = true;
                state.hoverIndex = selectedIndex;
                return false;
            }
        } else {
            if (ev.key == InputKey::ArrowUp) {
                state.hoverIndex = (state.hoverIndex <= 0) ? (int)options.size() - 1 : state.hoverIndex - 1;
                return false;
            } else if (ev.key == InputKey::ArrowDown) {
                state.hoverIndex = (state.hoverIndex >= (int)options.size() - 1) ? 0 : state.hoverIndex + 1;
                return false;
            } else if (ev.key == InputKey::Enter) {
                if (state.hoverIndex >= 0 && state.hoverIndex < (int)options.size()) {
                    selectedIndex = state.hoverIndex;
                    state.expanded = false;
                    return true;
                }
            } else if (ev.key == InputKey::Escape) {
                state.expanded = false;
                return false;
            }
        }
    }

    return false;
}

} // namespace UI
} // namespace TilelandWorld
