#pragma once
#ifndef TILELANDWORLD_UI_TEXTFIELD_H
#define TILELANDWORLD_UI_TEXTFIELD_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include <string>
#include <chrono>
#include <functional>

namespace TilelandWorld {
namespace UI {

enum class CursorMode {
    IBeam,      // Classic line cursor before character
    Block       // Background color block on character
};

struct TextFieldStyle {
    int width{20};
    std::string placeholder{""};
    RGBColor focusFg{0, 0, 0};
    RGBColor focusBg{200, 230, 255};
    RGBColor panelBg{18, 21, 28};
    RGBColor hintFg{140, 150, 170};
    char caretChar{'|'};
    int blinkIntervalMs{500};

    // 校验与约束
    size_t maxChars{0}; // 0 表示无限制
    std::function<bool(char32_t)> charFilter{nullptr}; // 字符过滤器，返回 false 则丢弃该字符
    std::function<std::string(const std::string&)> transform{nullptr}; // 整体变换（如转大写）
};

struct TextFieldState {
    bool focused{false};
    bool hover{false};
    bool caretOn{true};
    std::chrono::steady_clock::time_point lastCaretToggle{};

    // Cursor state
    int caretIndex{0}; // Position in UTF-8 string (byte index or char index? Let's use char index for simplicity if possible, but std::string is bytes)
    CursorMode mode{CursorMode::IBeam};
    int scrollOffset{0}; // Index of first visible character
    int lastRenderScrollOffset{0}; // Used for stable mouse interaction

    // Selection state: indices into the string. -1 means no selection.
    int selectionStart{-1};
    int selectionEnd{-1};

    // Layout info for mouse interaction
    int renderX{0}, renderY{0}, renderW{0};
    bool dragging{false};

    // 辅助函数：更新光标闪烁状态
    void updateCaret() {
        if (hasSelection()) {
            caretOn = true;
            return;
        }
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaretToggle).count();
        if (ms >= 500) { // 默认 500ms 闪烁一次
            caretOn = !caretOn;
            lastCaretToggle = now;
        }
    }

    void forceCaretOn() {
        caretOn = true;
        lastCaretToggle = std::chrono::steady_clock::now();
    }

    bool hasSelection() const {
        return selectionStart != -1 && selectionEnd != -1 && selectionStart != selectionEnd;
    }

    void clearSelection() {
        selectionStart = -1;
        selectionEnd = -1;
    }

    void selectAll(int length) {
        if (length > 0) {
            selectionStart = 0;
            selectionEnd = length;
        } else {
            clearSelection();
        }
    }
};

class TextField {
public:
    /**
     * 渲染文本框
     * @param surface 目标画布
     * @param x 坐标
     * @param y 坐标
     * @param text 当前输入的文本内容
     * @param state 运行时的状态（聚焦、悬停、光标）
     * @param style 视觉样式
     */
    static void render(TuiSurface& surface, int x, int y, const std::string& text, TextFieldState& state, const TextFieldStyle& style);

    /**
     * 处理输入事件（如字符输入、退格、CTRL+A等）
     * @return 如果文本内容发生了改变，返回 true
     */
    static bool handleInput(const InputEvent& ev, std::string& text, TextFieldState& state, const TextFieldStyle& style = TextFieldStyle{});
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_TEXTFIELD_H
