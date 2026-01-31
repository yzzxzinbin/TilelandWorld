#include "MenuDropBox.h"
#include "TuiUtils.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

void MenuDropBox::render(TuiSurface& surface, const std::vector<MenuDropBoxItem>& items, const MenuDropBoxState& state, const TuiTheme& theme) {
    if (!state.visible || items.empty()) return;

    auto drawMenu = [&](int x, int y, int w, const std::vector<MenuDropBoxItem>& menuItems, int selectedIdx) {
        int h = static_cast<int>(menuItems.size());
        // No border as requested
        surface.fillRect(x, y, w, h, theme.itemFg, theme.panel, " ");
        
        for (size_t i = 0; i < menuItems.size(); ++i) {
            bool isSelected = (selectedIdx == static_cast<int>(i));
            RGBColor bg = isSelected ? theme.focusBg : theme.panel;
            RGBColor fg = isSelected ? theme.focusFg : theme.itemFg;
            
            surface.fillRect(x, y + (int)i, w, 1, fg, bg, " ");
            std::string label = menuItems[i].label;
            surface.drawText(x + 1, y + (int)i, label, fg, bg);
            
            if (menuItems[i].hasSubmenu) {
                surface.drawText(x + w - 2, y + (int)i, "▶", fg, bg);
            }
        }
    };

    drawMenu(state.x, state.y, state.width, items, state.selectedIndex);

    if (state.subMenuIndex >= 0 && state.subMenuIndex < (int)items.size()) {
        const auto& subItems = items[state.subMenuIndex].subItems;
        if (!subItems.empty()) {
            drawMenu(state.subMenuX, state.subMenuY, state.subWidth, subItems, state.subSelectedIndex);
        }
    }
}

int MenuDropBox::handleInput(const InputEvent& ev, const std::vector<MenuDropBoxItem>& items, MenuDropBoxState& state, bool& requestClose) {
    if (!state.visible) return -1;

    int mainW = state.width;
    int mainH = (int)items.size();
    
    // Bounds for main menu
    bool inMain = (ev.x >= state.x && ev.x < state.x + mainW && ev.y >= state.y && ev.y < state.y + mainH);
    
    // Bounds for submenu
    bool inSub = false;
    if (state.subMenuIndex >= 0) {
        int subW = state.subWidth;
        int subH = (int)items[state.subMenuIndex].subItems.size();
        inSub = (ev.x >= state.subMenuX && ev.x < state.subMenuX + subW && ev.y >= state.subMenuY && ev.y < state.subMenuY + subH);
    }

    if (ev.type == InputEvent::Type::Mouse) {
        if (inMain) {
            int idx = ev.y - state.y;
            state.selectedIndex = idx;
            if (items[idx].hasSubmenu) {
                state.subMenuIndex = idx;
                state.subMenuX = state.x + mainW;
                state.subMenuY = state.y + idx;
                state.subWidth = calculateWidth(items[idx].subItems);
                state.subSelectedIndex = -1;
            } else {
                state.subMenuIndex = -1;
            }

            if (ev.pressed && ev.button == 0 && !items[idx].hasSubmenu) {
                requestClose = true;
                return idx;
            }
            return -2; // Handled
        } else if (inSub) {
            int sidx = ev.y - state.subMenuY;
            state.subSelectedIndex = sidx;
            if (ev.pressed && ev.button == 0) {
                requestClose = true;
                return 1000 + sidx;
            }
            return -2;
        } else {
            if (ev.pressed) {
                requestClose = true;
            }
            return -1;
        }
    } else if (ev.type == InputEvent::Type::Key) {
        if (ev.key == InputKey::Escape) {
            requestClose = true;
            return -1;
        }
        // Basic arrow navigation could be added here if needed, but mouse-centric for now
    }

    return -1;
}

int MenuDropBox::calculateWidth(const std::vector<MenuDropBoxItem>& items) {
    size_t maxW = 0;
    for (const auto& item : items) {
        size_t w = TuiUtils::calculateUtf8VisualWidth(item.label) + (item.hasSubmenu ? 4 : 2);
        maxW = std::max(maxW, w);
    }
    return (int)maxW;
}

} // namespace UI
} // namespace TilelandWorld
