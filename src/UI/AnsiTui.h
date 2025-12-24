#pragma once
#ifndef TILELANDWORLD_UI_ANSITUI_H
#define TILELANDWORLD_UI_ANSITUI_H

#include <string>
#include <vector>
#include <cstdint>
#include <ostream>
#include <iostream>
#include "../TerrainTypes.h"

namespace TilelandWorld {
namespace UI {

// 单元格：保存字符及其前景/背景色
struct TuiCell {
    std::string glyph{" "};
    RGBColor fg{255, 255, 255};
    RGBColor bg{0, 0, 0};
    bool hasBg{false}; // 标记该格子是否显式设置了背景
};

// 边框样式（使用 ASCII，兼容多数终端）
struct BoxStyle {
    char topLeft{'+'};
    char topRight{'+'};
    char bottomLeft{'+'};
    char bottomRight{'+'};
    char horizontal{'-'};
    char vertical{'|'};
};

// 画布：纯 CPU 缓冲，使用转义序列输出
class TuiSurface {
public:
    TuiSurface(int width = 80, int height = 24);

    void resize(int width, int height);
    void clear(const RGBColor& fg, const RGBColor& bg, const std::string& glyph = " ");
    void drawText(int x, int y, const std::string& text, const RGBColor& fg, const RGBColor& bg);
    void drawCenteredText(int x, int y, int width, const std::string& text, const RGBColor& fg, const RGBColor& bg);
    void fillRect(int x, int y, int w, int h, const RGBColor& fg, const RGBColor& bg, const std::string& glyph = " ");
    void drawFrame(int x, int y, int w, int h, const BoxStyle& style, const RGBColor& fg, const RGBColor& bg);

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    const std::vector<TuiCell>& data() const { return buffer; }
    // 返回可写指针，用于组合叠加层时修改单元格
    TuiCell* editCell(int x, int y);

private:
    int width;
    int height;
    std::vector<TuiCell> buffer;

    bool inBounds(int x, int y) const;
    TuiCell* at(int x, int y);
};

// 输出器：将缓冲转换为 ANSI 字符串，并一次性写出
class TuiPainter {
public:
    std::string buildAnsi(const TuiSurface& surface, bool hideCursor = true, int originX = 1, int originY = 1) const;
    void present(const TuiSurface& surface, bool hideCursor = true, int originX = 1, int originY = 1, std::ostream& os = std::cout) const;
    void reset(std::ostream& os = std::cout) const; // 复位颜色/光标
};

// 菜单主题
struct MenuTheme {
    RGBColor background{12, 14, 18};
    RGBColor panel{18, 21, 28};
    RGBColor accent{96, 140, 255};
    RGBColor title{220, 230, 255};
    RGBColor subtitle{160, 170, 190};
    RGBColor itemFg{210, 215, 224};
    RGBColor itemBg{18, 21, 28};
    RGBColor focusFg{0, 0, 0};
    RGBColor focusBg{200, 230, 255};
    RGBColor hintFg{140, 150, 170};
};

// 简易菜单视图，负责布局/绘制
class MenuView {
public:
    MenuView(std::vector<std::string> items, MenuTheme theme = MenuTheme{});

    void setTitle(std::string text);
    void setSubtitle(std::string text);

    void moveUp();
    void moveDown();
    size_t getSelected() const { return selected; }
    const std::vector<std::string>& getItems() const { return options; }

    void render(TuiSurface& surface, int originX, int originY, int width);

private:
    std::vector<std::string> options;
    MenuTheme theme;
    size_t selected{0};
    std::string title{"Tileland World"};
    std::string subtitle{"Arrow keys to navigate, Enter to confirm"};
    BoxStyle frame{};
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_ANSITUI_H
