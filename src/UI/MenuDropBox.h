#ifndef TILELANDWORLD_UI_MENUDROPBOX_H
#define TILELANDWORLD_UI_MENUDROPBOX_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <functional>

namespace TilelandWorld {
namespace UI {

struct MenuDropBoxItem {
    std::string label;
    bool hasSubmenu = false;
    std::vector<MenuDropBoxItem> subItems;
    std::function<void()> action;
};

struct MenuDropBoxState {
    bool visible = false;
    int x = 0;
    int y = 0;
    int selectedIndex = -1;
    int subMenuIndex = -1; // Currently opened submenu index
    int subMenuX = 0;
    int subMenuY = 0;
    int subSelectedIndex = -1;
    int subSubMenuIndex = -1; // Third-level submenu index inside the active submenu
    int subSubMenuX = 0;
    int subSubMenuY = 0;
    int subSubSelectedIndex = -1;
    int width = 0;
    int subWidth = 0;
    int subSubWidth = 0;
};

class MenuDropBox {
public:
    static void render(TuiSurface& surface, const std::vector<MenuDropBoxItem>& items, const MenuDropBoxState& state, const TuiTheme& theme);
    
    // Returns index of the main item or a special code for sub-items.
    // For simplicity in this specific task:
    // - >= 0: Index of main item selected
    // - 1000 + i: Sub-item i of the active submenu selected
    static int handleInput(const InputEvent& ev, const std::vector<MenuDropBoxItem>& items, MenuDropBoxState& state, bool& requestClose);

    static int calculateWidth(const std::vector<MenuDropBoxItem>& items);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_MENUDROPBOX_H
