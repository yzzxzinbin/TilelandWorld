#include "ContextMenu.h"
#include "TuiUtils.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

void ContextMenu::render(TuiSurface& surface, const std::vector<std::string>& items, const ContextMenuState& state, const ContextMenuTheme& theme) {
    if (!state.visible || items.empty()) return;

    int w = state.width;
    int h = static_cast<int>(items.size()) + 2; // +2 for border top/bottom

    // Ensure it doesn't go off-screen
    int drawX = std::clamp(state.x, 0, std::max(0, surface.getWidth() - w));
    int drawY = std::clamp(state.y, 0, std::max(0, surface.getHeight() - h));

    // Draw background and border
    surface.fillRect(drawX, drawY, w, h, theme.itemFg, theme.panelBg, " ");
    
    // Draw modernFrame
    BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
    surface.drawFrame(drawX, drawY, w, h, modernFrame, theme.border, theme.panelBg);

    for (size_t i = 0; i < items.size(); ++i) {
        int ry = drawY + 1 + static_cast<int>(i);
        bool isSelected = (state.selectedIndex == static_cast<int>(i));
        
        RGBColor bg = isSelected ? theme.focusBg : theme.panelBg;
        RGBColor fg = isSelected ? theme.focusFg : theme.itemFg;
        
        surface.fillRect(drawX + 1, ry, w - 2, 1, fg, bg, " ");
        
        std::string label = items[i];
        // Trim if too long (though usually we calculateWidth beforehand)
        label = TuiUtils::trimToUtf8VisualWidth(label, static_cast<size_t>(w - 4));
        surface.drawText(drawX + 2, ry, label, fg, bg);

        if (isSelected) {
            // Indicator
            surface.drawText(drawX + 1, ry, ">", theme.accent, bg);
        }
    }
}

int ContextMenu::handleInput(const InputEvent& ev, const std::vector<std::string>& items, ContextMenuState& state, bool& requestClose) {
    if (!state.visible) return -1;

    int w = state.width;
    int h = static_cast<int>(items.size()) + 2;
    // We assume state.x and state.y are where it's intended to be, 
    // but render() clamps them. For input, we should use the same logic if we don't store clamped pos.
    // Better: let the caller decide or store actual drawn pos in state.
    int drawX = state.x; 
    int drawY = state.y;

    if (ev.type == InputEvent::Type::Mouse) {
        bool inMenu = (ev.x >= drawX && ev.x < drawX + w &&
                       ev.y >= drawY && ev.y < drawY + h);
        
        if (inMenu) {
            int relY = ev.y - drawY - 1;
            if (relY >= 0 && relY < static_cast<int>(items.size())) {
                state.selectedIndex = relY;
                if (ev.pressed && ev.button == 0) { // Left click
                    requestClose = true;
                    return state.selectedIndex;
                }
            } else {
                // Border area
                // Possibly don't clear selection if just hovering border
            }
            return -1; // Consumed mouse event
        } else {
            if (ev.pressed) {
                requestClose = true;
                // If it was a right-click outside, we might want to handle it in caller...
                // but usually context menu closes on any click outside.
            }
            return -1;
        }
    } else if (ev.type == InputEvent::Type::Key) {
        if (ev.key == InputKey::ArrowUp) {
            state.selectedIndex = (state.selectedIndex <= 0) ? static_cast<int>(items.size()) - 1 : state.selectedIndex - 1;
        } else if (ev.key == InputKey::ArrowDown) {
            state.selectedIndex = (state.selectedIndex >= static_cast<int>(items.size()) - 1) ? 0 : state.selectedIndex + 1;
        } else if (ev.key == InputKey::Enter) {
            if (state.selectedIndex >= 0 && state.selectedIndex < static_cast<int>(items.size())) {
                requestClose = true;
                return state.selectedIndex;
            }
        } else if (ev.key == InputKey::Escape || ev.key == InputKey::Tab) {
            requestClose = true;
        }
    }

    return -1;
}

int ContextMenu::calculateWidth(const std::vector<std::string>& items) {
    size_t maxW = 0;
    for (const auto& s : items) {
        maxW = std::max(maxW, TuiUtils::calculateUtf8VisualWidth(s));
    }
    return static_cast<int>(maxW) + 6; // Padding + border + indicator
}

} // namespace UI
} // namespace TilelandWorld
