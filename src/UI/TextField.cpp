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

void TextField::render(TuiSurface& surface, int x, int y, const std::string& text, TextFieldState& state, const TextFieldStyle& style) {
    state.renderX = x;
    state.renderY = y;
    state.renderW = style.width;

    bool active = (state.focused || state.hover);
    RGBColor bg = style.focusBg;
    RGBColor fg = style.focusFg;
    RGBColor drawFg = fg;

    // 绘制背景
    surface.fillRect(x, y, style.width, 1, style.panelBg, style.panelBg, " ");
    surface.fillRect(x, y, style.width, 1, fg, bg, " ");

    int maxChars = std::max(0, style.width - 2);
    bool hasSelection = state.hasSelection();

    // Ensure caretIndex is valid
    state.caretIndex = std::clamp(state.caretIndex, 0, (int)text.size());

    // Update scrollOffset to keep caretIndex in view
    if (state.caretIndex < state.scrollOffset) {
        state.scrollOffset = state.caretIndex;
    } else if (state.caretIndex >= state.scrollOffset + maxChars) {
        state.scrollOffset = state.caretIndex - maxChars + (state.mode == CursorMode::Block ? 1 : 0);
    }
    state.scrollOffset = std::clamp(state.scrollOffset, 0, std::max(0, (int)text.size() - maxChars + 1));
    state.lastRenderScrollOffset = state.scrollOffset;

    int startIdx = state.scrollOffset;
    std::string visibleText = "";
    if (startIdx < (int)text.size()) {
        visibleText = text.substr(startIdx, maxChars);
    }
    int relCaret = state.caretIndex - startIdx;

    if (!active && text.empty()) {
        surface.drawText(x + 1, y, style.placeholder, style.hintFg, bg);
    } else {
        // Draw visible text
        for (int i = 0; i < (int)visibleText.size(); ++i) {
            bool isSelected = false;
            if (hasSelection) {
                int absIdx = startIdx + i;
                int s = std::min(state.selectionStart, state.selectionEnd);
                int e = std::max(state.selectionStart, state.selectionEnd);
                if (absIdx >= s && absIdx < e) isSelected = true;
            }

            RGBColor charFg = drawFg;
            RGBColor charBg = bg;

            if (isSelected) {
                charFg = bg;
                charBg = fg;
            }

            std::string glyph(1, visibleText[i]);

            // Caret logic
            if (state.focused && state.caretOn && i == relCaret) {
                if (state.mode == CursorMode::Block) {
                    // Block cursor swaps colors
                    charFg = bg;
                    charBg = fg;
                } else if (!state.hasSelection()) {
                    // IBeam navigation: Use the separator character instead of inverting the cell
                    glyph = std::string(1, style.caretChar);
                    charFg = style.focusFg;
                    charBg = bg;
                }
            }

            surface.drawText(x + 1 + i, y, glyph, charFg, charBg);
        }

        // Draw caret if at the end
        if (state.focused && state.caretOn) {
            if (relCaret == (int)visibleText.size() && relCaret < maxChars) {
                if (state.mode == CursorMode::IBeam) {
                    surface.drawText(x + 1 + relCaret, y, std::string(1, style.caretChar), style.focusFg, bg);
                } else {
                    surface.drawText(x + 1 + relCaret, y, " ", bg, fg);
                }
            }
        }
    }
}

bool TextField::handleInput(const InputEvent& ev, std::string& text, TextFieldState& state, const TextFieldStyle& style) {
    if (ev.type == InputEvent::Type::Mouse) {
        bool inBounds = (ev.x >= state.renderX && ev.x < state.renderX + state.renderW && ev.y == state.renderY);
        state.hover = inBounds;

        if (ev.pressed && ev.button == 0) {
            if (inBounds) {
                bool firstFocus = !state.focused;
                state.focused = true;
                state.forceCaretOn();
                
                if (firstFocus) {
                    state.caretIndex = (int)text.size();
                    state.clearSelection();
                } else {
                    int clickPos = ev.x - state.renderX - 1;
                    state.caretIndex = std::clamp(state.lastRenderScrollOffset + clickPos, 0, (int)text.size());

                    if (!ev.shift) {
                        state.selectionStart = state.caretIndex;
                        state.selectionEnd = state.caretIndex;
                    } else {
                        state.selectionEnd = state.caretIndex;
                    }
                }
                state.dragging = true;
                return true;
            } else {
                state.focused = false;
            }
        } else if (ev.move && state.dragging) {
            int clickPos = ev.x - state.renderX - 1;
            state.caretIndex = std::clamp(state.lastRenderScrollOffset + clickPos, 0, (int)text.size());
            state.selectionEnd = state.caretIndex;
            return true;
        } else if (!ev.pressed && state.dragging) {
            state.dragging = false;
        }
        return false;
    }

    if (!state.focused) return false;
    if (ev.type != InputEvent::Type::Key) return false;

    state.forceCaretOn();
    bool changed = false;

    auto deleteSelection = [&]() {
        if (state.hasSelection()) {
            int s = std::min(state.selectionStart, state.selectionEnd);
            int e = std::max(state.selectionStart, state.selectionEnd);
            text.erase(s, e - s);
            state.caretIndex = s;
            state.clearSelection();
            return true;
        }
        return false;
    };

    if (ev.key == InputKey::Character) {
        if (ev.ctrl) {
            char c = std::tolower((char)ev.ch);
            if (c == 'a') {
                state.selectAll((int)text.size());
                return true;
            } else if (c == 'c') {
                if (state.hasSelection()) {
                    int s = std::min(state.selectionStart, state.selectionEnd);
                    int e = std::max(state.selectionStart, state.selectionEnd);
                    setClipboardText(text.substr(s, e - s));
                }
                return true;
            } else if (c == 'v') {
                std::string clip = getClipboardText();
                if (!clip.empty()) {
                    deleteSelection();
                    
                    std::string filtered;
                    for (char ch : clip) {
                        if (style.charFilter && !style.charFilter((char32_t)ch)) continue;
                        filtered.push_back(ch);
                    }
                    text.insert(state.caretIndex, filtered);
                    state.caretIndex += (int)filtered.size();
                    changed = true;
                }
            } else if (c == 'x') {
                if (state.hasSelection()) {
                    int s = std::min(state.selectionStart, state.selectionEnd);
                    int e = std::max(state.selectionStart, state.selectionEnd);
                    setClipboardText(text.substr(s, e - s));
                    deleteSelection();
                    changed = true;
                }
            }
        } else if (ev.ch == '\b') {
            if (!deleteSelection()) {
                if (state.caretIndex > 0) {
                    text.erase(state.caretIndex - 1, 1);
                    state.caretIndex--;
                }
            }
            changed = true;
        } else if (ev.ch >= 32) {
            if (!style.charFilter || style.charFilter(ev.ch)) {
                deleteSelection();
                std::string s(1, (char)ev.ch);
                text.insert(state.caretIndex, s);
                state.caretIndex++;
                changed = true;
            }
        }
    } else if (ev.key == InputKey::F12) {
        state.mode = (state.mode == CursorMode::IBeam) ? CursorMode::Block : CursorMode::IBeam;
        state.clearSelection();
        return true;
    } else if (ev.key == InputKey::Delete) {
        if (!deleteSelection()) {
            if (state.caretIndex < (int)text.size()) {
                text.erase(state.caretIndex, 1);
                changed = true;
            }
        } else {
            changed = true;
        }
    } else if (ev.key == InputKey::ArrowLeft) {
        if (state.caretIndex > 0) {
            state.caretIndex--;
            if (ev.shift) {
                if (state.selectionStart == -1) state.selectionStart = state.caretIndex + 1;
                state.selectionEnd = state.caretIndex;
            } else {
                state.clearSelection();
            }
        }
        return true;
    } else if (ev.key == InputKey::ArrowRight) {
        if (state.caretIndex < (int)text.size()) {
            state.caretIndex++;
            if (ev.shift) {
                if (state.selectionStart == -1) state.selectionStart = state.caretIndex - 1;
                state.selectionEnd = state.caretIndex;
            } else {
                state.clearSelection();
            }
        }
        return true;
    } else if (ev.key == InputKey::Home) {
        state.caretIndex = 0;
        state.clearSelection();
        return true;
    } else if (ev.key == InputKey::End) {
        state.caretIndex = (int)text.size();
        state.clearSelection();
        return true;
    } else if (ev.key == InputKey::Escape) {
        state.clearSelection();
        state.focused = false;
        return true; // Consume Escape to unfocus
    } else if (ev.key == InputKey::Enter) {
        // Return false so screen level logic can use Enter for confirm/jump
        return false;
    }

    if (changed) {
        if (style.maxChars > 0 && text.size() > style.maxChars) {
            text = text.substr(0, style.maxChars);
            state.caretIndex = std::min(state.caretIndex, (int)text.size());
        }
        if (style.transform) {
            text = style.transform(text);
        }
    }

    return changed;
}

} // namespace UI
} // namespace TilelandWorld
