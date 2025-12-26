#include "AnsiTui.h"
#include "TuiUtils.h"
#include <algorithm>

namespace TilelandWorld {
namespace UI {

namespace {
    bool sameColor(const RGBColor& a, const RGBColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }
}

TuiSurface::TuiSurface(int width_, int height_) : width(width_), height(height_) {
    buffer.resize(std::max(1, width) * std::max(1, height));
}

void TuiSurface::resize(int newWidth, int newHeight) {
    width = std::max(1, newWidth);
    height = std::max(1, newHeight);
    buffer.assign(width * height, TuiCell{});
}

bool TuiSurface::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

TuiCell* TuiSurface::at(int x, int y) {
    if (!inBounds(x, y)) return nullptr;
    return &buffer[static_cast<size_t>(y) * width + x];
}

TuiCell* TuiSurface::editCell(int x, int y) {
    return at(x, y);
}

void TuiSurface::clear(const RGBColor& fg, const RGBColor& bg, const std::string& glyph) {
    fillRect(0, 0, width, height, fg, bg, glyph);
}

void TuiSurface::drawText(int x, int y, const std::string& text, const RGBColor& fg, const RGBColor& bg) {
    if (y < 0 || y >= height) return;
    int cursorX = x;
    for (size_t i = 0; i < text.size();) {
        auto info = TuiUtils::nextUtf8Char(text, i);
        if (cursorX >= width) break;
        if (info.visualWidth == 2 && cursorX + 1 >= width) break; // not enough space for wide glyph

        if (cursorX >= 0) {
            if (TuiCell* cell = at(cursorX, y)) {
                cell->glyph = text.substr(i, info.length);
                cell->fg = fg;
                cell->bg = bg;
                cell->hasBg = true;
                cell->isContinuation = false;
            }
            if (info.visualWidth == 2) {
                if (TuiCell* cont = at(cursorX + 1, y)) {
                    cont->glyph.clear();
                    cont->fg = fg;
                    cont->bg = bg;
                    cont->hasBg = true;
                    cont->isContinuation = true;
                }
            }
        }
        cursorX += static_cast<int>(info.visualWidth);
        i += info.length;
    }
}

void TuiSurface::drawCenteredText(int x, int y, int areaWidth, const std::string& text, const RGBColor& fg, const RGBColor& bg) {
    int safeWidth = std::max(0, areaWidth);
    int visualWidth = static_cast<int>(TuiUtils::calculateUtf8VisualWidth(text));
    int startX = x + std::max(0, (safeWidth - visualWidth) / 2);
    drawText(startX, y, text, fg, bg);
}

void TuiSurface::fillRect(int x, int y, int w, int h, const RGBColor& fg, const RGBColor& bg, const std::string& glyph) {
    int startX = std::max(0, x);
    int startY = std::max(0, y);
    int endX = std::min(width, x + w);
    int endY = std::min(height, y + h);
    if (startX >= endX || startY >= endY) return;

    for (int yy = startY; yy < endY; ++yy) {
        for (int xx = startX; xx < endX; ++xx) {
            TuiCell& cell = buffer[static_cast<size_t>(yy) * width + xx];
            cell.glyph = glyph.empty() ? " " : glyph.substr(0, 1);
            cell.fg = fg;
            cell.bg = bg;
            cell.hasBg = true;
            cell.isContinuation = false;
        }
    }
}

void TuiSurface::drawFrame(int x, int y, int w, int h, const BoxStyle& style, const RGBColor& fg, const RGBColor& bg) {
    if (w < 2 || h < 2) return;

    auto setGlyph = [&](int px, int py, const std::string& glyph) {
        if (TuiCell* cell = at(px, py)) {
            cell->glyph = glyph.empty() ? " " : glyph;
            cell->fg = fg;
            cell->bg = bg;
            cell->hasBg = true;
            cell->isContinuation = false;
        }
    };

    fillRect(x, y, w, h, fg, bg, " ");

    // 顶部和底部
    for (int xx = 1; xx < w - 1; ++xx) {
        setGlyph(x + xx, y, style.horizontal);
        setGlyph(x + xx, y + h - 1, style.horizontal);
    }
    // 左右
    for (int yy = 1; yy < h - 1; ++yy) {
        setGlyph(x, y + yy, style.vertical);
        setGlyph(x + w - 1, y + yy, style.vertical);
    }
    // 角
    setGlyph(x, y, style.topLeft);
    setGlyph(x + w - 1, y, style.topRight);
    setGlyph(x, y + h - 1, style.bottomLeft);
    setGlyph(x + w - 1, y + h - 1, style.bottomRight);
}

std::string TuiPainter::buildAnsi(const TuiSurface& surface, bool hideCursor, int originX, int originY) const {
    const auto& cells = surface.data();
    std::string output;
    size_t estimated = static_cast<size_t>(surface.getWidth() * surface.getHeight() * 24) + 64;
    output.reserve(estimated);

    if (hideCursor) output.append("\x1b[?25l");
    output.append("\x1b[0m");

    RGBColor currentFg{0, 0, 0};
    RGBColor currentBg{0, 0, 0};
    bool hasColor = false;

    for (int y = 0; y < surface.getHeight(); ++y) {
        output.append("\x1b[");
        output.append(std::to_string(originY + y));
        output.append(";");
        output.append(std::to_string(originX));
        output.append("H");

        for (int x = 0; x < surface.getWidth(); ++x) {
            const TuiCell& cell = cells[static_cast<size_t>(y) * surface.getWidth() + x];
            if (cell.isContinuation) continue;
            if (!hasColor || !sameColor(cell.fg, currentFg) || !sameColor(cell.bg, currentBg)) {
                output.append("\x1b[48;2;");
                output.append(std::to_string(cell.bg.r));
                output.push_back(';');
                output.append(std::to_string(cell.bg.g));
                output.push_back(';');
                output.append(std::to_string(cell.bg.b));
                output.append("m\x1b[38;2;");
                output.append(std::to_string(cell.fg.r));
                output.push_back(';');
                output.append(std::to_string(cell.fg.g));
                output.push_back(';');
                output.append(std::to_string(cell.fg.b));
                output.append("m");
                currentFg = cell.fg;
                currentBg = cell.bg;
                hasColor = true;
            }
            output.append(cell.glyph.empty() ? " " : cell.glyph);
        }
    }

    output.append("\x1b[0m");
    return output;
}

void TuiPainter::present(const TuiSurface& surface, bool hideCursor, int originX, int originY, std::ostream& os) const {
    std::string data = buildAnsi(surface, hideCursor, originX, originY);
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();
}

void TuiPainter::reset(std::ostream& os) const {
    os << "\x1b[0m\x1b[?25h" << std::flush;
}

MenuView::MenuView(std::vector<std::string> items, MenuTheme theme_) : options(std::move(items)), theme(theme_) {
    if (options.empty()) {
        options.push_back("Start");
    }
    selected = 0;
}

void MenuView::setTitle(std::string text) {
    title = std::move(text);
}

void MenuView::setSubtitle(std::string text) {
    subtitle = std::move(text);
}

void MenuView::moveUp() {
    if (options.empty()) return;
    if (selected == 0) {
        selected = options.size() - 1;
    } else {
        --selected;
    }
}

void MenuView::moveDown() {
    if (options.empty()) return;
    selected = (selected + 1) % options.size();
}

void MenuView::render(TuiSurface& surface, int originX, int originY, int width) {
    int safeWidth = std::max(20, width);
    int panelHeight = static_cast<int>(options.size()) + 8;
    int safeHeight = std::min(surface.getHeight() - originY, panelHeight);
    if (safeHeight < 6) return;

    int x = std::max(0, originX);
    int y = std::max(0, originY);

    surface.fillRect(x, y, safeWidth, safeHeight, theme.itemFg, theme.panel, " ");
    surface.drawFrame(x, y, safeWidth, safeHeight, frame, theme.itemFg, theme.panel);

    surface.drawCenteredText(x, y + 1, safeWidth, title, theme.title, theme.panel);
    surface.drawCenteredText(x, y + 2, safeWidth, subtitle, theme.subtitle, theme.panel);

    int listStart = y + 4;
    for (size_t i = 0; i < options.size(); ++i) {
        bool focus = i == selected;
        RGBColor fg = focus ? theme.focusFg : theme.itemFg;
        RGBColor bg = focus ? theme.focusBg : theme.itemBg;
        std::string marker = focus ? "> " : "  ";
        int areaWidth = safeWidth - 4;
        size_t markerWidth = TuiUtils::calculateUtf8VisualWidth(marker);
        std::string text = options[i];
        size_t textWidth = TuiUtils::calculateUtf8VisualWidth(text);
        if (markerWidth + textWidth > static_cast<size_t>(areaWidth)) {
            size_t trimWidth = static_cast<size_t>(areaWidth > static_cast<int>(markerWidth) ? areaWidth - static_cast<int>(markerWidth) : 0);
            text = TuiUtils::trimToUtf8VisualWidth(text, trimWidth);
            textWidth = TuiUtils::calculateUtf8VisualWidth(text);
        }

        std::string line = marker + text;
        surface.drawText(x + 2, listStart + static_cast<int>(i), line, fg, bg);
        int consumed = static_cast<int>(markerWidth + textWidth);
        int remaining = areaWidth - consumed;
        if (remaining > 0) {
            surface.fillRect(x + 2 + consumed, listStart + static_cast<int>(i), remaining, 1, fg, bg, " ");
        }
    }

    surface.drawCenteredText(x, y + safeHeight - 2, safeWidth, "Enter: confirm | Q: quit", theme.hintFg, theme.panel);
}

} // namespace UI
} // namespace TilelandWorld
