#pragma once
#ifndef TILELANDWORLD_UI_UNICODETABLESCREEN_H
#define TILELANDWORLD_UI_UNICODETABLESCREEN_H

#include "AnsiTui.h"
#include "TextField.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <memory>

namespace TilelandWorld {
namespace UI {

struct UnicodeBlock {
    std::string name;
    char32_t start;
    char32_t end;
};

class UnicodeTableScreen {
public:
    UnicodeTableScreen();
    void show();

private:
    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;
    std::unique_ptr<InputController> input;

    std::vector<UnicodeBlock> blocks;
    int selectedBlockIdx{0};
    int blockScrollOffset{0};
    
    char32_t gridStartChar{0};
    int gridScrollOffset{0}; // in rows
    
    std::string searchQuery;
    TextFieldState searchState;

    // Layout cache
    int blockListX{0}, blockListY{0}, blockListW{0}, blockListH{0};
    int gridX{0}, gridY{0}, gridW{0}, gridH{0};
    int searchX{0}, searchY{0}, searchW{0};

    void initBlocks();
    void renderFrame();
    void drawBlockList();
    void drawCharGrid();
    void handleKey(const InputEvent& ev, bool& running);
    void handleMouse(const InputEvent& ev, bool& running);
    
    // Helper to set a cell with an anchored glyph (absolute positioning)
    void setAnchored(int px, int py, const std::string& g, const RGBColor& fg, const RGBColor& bg, int vW = 1);

    void ensureBlockVisible();
    void jumpToCode(char32_t code);
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_UNICODETABLESCREEN_H
