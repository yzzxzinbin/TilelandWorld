#ifndef TILELANDWORLD_UI_CONTEXTMENU_H
#define TILELANDWORLD_UI_CONTEXTMENU_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <functional>

namespace TilelandWorld {
namespace UI {

struct ContextMenuTheme {
    RGBColor panelBg{45, 45, 48};
    RGBColor itemFg{220, 220, 220};
    RGBColor focusBg{60, 60, 62};
    RGBColor focusFg{255, 255, 255};
    RGBColor accent{0, 122, 204};
    RGBColor border{80, 80, 80};
};

struct ContextMenuState {
    int selectedIndex = -1;
    bool visible = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class ContextMenu {
public:
    static void render(TuiSurface& surface, const std::vector<std::string>& items, const ContextMenuState& state, const ContextMenuTheme& theme = ContextMenuTheme());
    
    // Returns the index of the selected item if clicked or Enter pressed, -1 otherwise.
    // Also updates state.selectedIndex if hovered.
    // Set running to false if Esc is pressed or click outside.
    static int handleInput(const InputEvent& ev, const std::vector<std::string>& items, ContextMenuState& state, bool& requestClose);

    static int calculateWidth(const std::vector<std::string>& items);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_CONTEXTMENU_H
