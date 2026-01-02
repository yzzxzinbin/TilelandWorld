#include "TextField.h"
#include "TuiUtils.h"
#include "../Controllers/InputController.h"
#include <algorithm>
#include <iostream>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
#ifdef _WIN32
    void setClipboardText(const std::string& text) {
        if (!OpenClipboard(nullptr)) return;
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (!hGlob) { CloseClipboard(); return; }
        memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);
        GlobalUnlock(hGlob);
        SetClipboardData(CF_TEXT, hGlob);
        CloseClipboard();
    }

    std::string getClipboardText() {
        if (!OpenClipboard(nullptr)) return "";
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (!hData) { CloseClipboard(); return ""; }
        char* ptr = (char*)GlobalLock(hData);
        std::string text = ptr ? ptr : "";
        GlobalUnlock(hData);
        CloseClipboard();
        return text;
    }
#else
    void setClipboardText(const std::string& text) {
        // OSC 52: ESC ] 52 ; c ; <base64> BEL
        std::string b64 = TuiUtils::base64Encode(text);
        std::cout << "\x1b]52;c;" << b64 << "\x07" << std::flush;
    }
    std::string getClipboardText() { 
        // OSC 52 读取较为复杂且通常被终端禁用，暂不实现
        return ""; 
    }
#endif
}

void TextField::render(TuiSurface& surface, int x, int y, const std::string& text, const TextFieldState& state, const TextFieldStyle& style) {
    bool active = (state.focused || state.hover);
    RGBColor bg = style.focusBg;
    RGBColor fg = style.focusFg;
    RGBColor drawFg = fg;

    // 绘制背景
    surface.fillRect(x, y, style.width, 1, fg, bg, " ");

    std::string displayStr = text;
    int maxChars = std::max(0, style.width - 2);

    bool hasSelection = state.hasSelection();

    if (active && state.caretOn && !hasSelection) {
        displayStr.push_back(style.caretChar);
    } else if (!active && displayStr.empty()) {
        displayStr = style.placeholder;
        drawFg = style.hintFg;
    }

    // 截断处理（如果文本过长，显示末尾部分）
    // 注意：如果有选择，截断逻辑可能会让选择区域不可见，这里简化处理
    if (static_cast<int>(displayStr.size()) > maxChars) {
        displayStr = displayStr.substr(displayStr.size() - maxChars);
    }

    if (hasSelection && active) {
        // 简单的全选高亮处理：如果全选了，反转颜色
        // 在 TUI 中，我们可以通过绘制背景色来实现
        surface.drawText(x + 1, y, displayStr, bg, fg); 
    } else {
        surface.drawText(x + 1, y, displayStr, drawFg, bg);
    }
}

bool TextField::handleInput(const InputEvent& ev, std::string& text, TextFieldState& state, const TextFieldStyle& style) {
    if (!state.focused) return false;

    if (ev.type != InputEvent::Type::Key) return false;

    bool changed = false;

    if (ev.key == InputKey::Character) {
        if (ev.ctrl) {
            char c = std::tolower((char)ev.ch);
            if (c == 'a') {
                state.selectAll((int)text.size());
                return true;
            } else if (c == 'c') {
                if (state.hasSelection()) {
                    setClipboardText(text);
                }
                return true;
            } else if (c == 'v') {
                std::string clip = getClipboardText();
                if (!clip.empty()) {
                    std::string filtered;
                    for (char ch : clip) {
                        if (style.charFilter && !style.charFilter((char32_t)ch)) continue;
                        filtered.push_back(ch);
                    }

                    if (state.hasSelection()) {
                        text = filtered;
                        state.clearSelection();
                    } else {
                        text += filtered;
                    }
                    changed = true;
                }
            } else if (c == 'x') {
                if (state.hasSelection()) {
                    setClipboardText(text);
                    text.clear();
                    state.clearSelection();
                    changed = true;
                }
            }
        } else if (ev.ch == '\b') {
            if (state.hasSelection()) {
                text.clear();
                state.clearSelection();
                changed = true;
            } else if (!text.empty()) {
                text.pop_back();
                changed = true;
            }
        } else if (ev.ch >= 32) {
            if (!style.charFilter || style.charFilter(ev.ch)) {
                if (state.hasSelection()) {
                    text = (char)ev.ch;
                    state.clearSelection();
                    changed = true;
                } else {
                    text.push_back((char)ev.ch);
                    changed = true;
                }
            }
        }
    } else if (ev.key == InputKey::Enter || ev.key == InputKey::Escape) {
        state.clearSelection();
        state.focused = false;
        return true;
    }

    if (changed) {
        if (style.maxChars > 0 && text.size() > style.maxChars) {
            text = text.substr(0, style.maxChars);
        }
        if (style.transform) {
            text = style.transform(text);
        }
    }

    return changed;
}

} // namespace UI
} // namespace TilelandWorld
