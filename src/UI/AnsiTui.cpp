#include "AnsiTui.h"
#include "TuiUtils.h"
#include <algorithm>
#include <cmath>
#include <utility>

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
    startSelectionChange(selected, 0.0);
}

void MenuView::setTitle(std::string text) {
    title = std::move(text);
}

void MenuView::setSubtitle(std::string text) {
    subtitle = std::move(text);
}

void MenuView::moveUp() {
    if (options.empty()) return;
    size_t next = (selected == 0) ? options.size() - 1 : selected - 1;
    startSelectionChange(next, 0.0);
}

void MenuView::moveDown() {
    if (options.empty()) return;
    size_t next = (selected + 1) % options.size();
    startSelectionChange(next, 0.0);
}

void MenuView::setSelectedWithOrigin(size_t idx, double originNorm) {
    if (idx >= options.size()) return;
    if (idx == selected) return;
    startSelectionChange(idx, originNorm);
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

    RGBColor titleBg = TuiUtils::blendColor(theme.accent, theme.panel, 0.45);
    surface.fillRect(x + 1, y + 1, safeWidth - 2, 1, theme.title, titleBg, " ");
    surface.drawCenteredText(x, y + 1, safeWidth, title, theme.title, titleBg);
    RGBColor subtitleBg = TuiUtils::blendColor(theme.panel, theme.background, 0.25);
    surface.fillRect(x + 1, y + 2, safeWidth - 2, 1, theme.subtitle, subtitleBg, " ");
    surface.drawCenteredText(x, y + 2, safeWidth, subtitle, theme.subtitle, subtitleBg);

    int listStart = y + 4;
    int areaWidth = safeWidth - 4;
    auto now = std::chrono::steady_clock::now();

    struct HighlightSpan { int start; int end; bool done; };
    auto spanFor = [&](bool isActive, bool isFading, double originNorm, std::chrono::steady_clock::time_point startTime, double duration) {
        double t = std::chrono::duration<double>(now - startTime).count();
        if (t < 0) t = 0;
        double progress = duration > 0 ? std::min(1.0, t / duration) : 1.0;
        double eased = easeOutCubic(progress);
        double radius = 0.0;
        if (isActive) {
            radius = eased * static_cast<double>(areaWidth);
        } else if (isFading) {
            radius = (1.0 - eased) * static_cast<double>(areaWidth);
        }
        bool done = duration <= 0 || t >= duration;
        if (radius <= 0.05) return HighlightSpan{-1, -1, done};
        double originPx = originNorm * static_cast<double>(areaWidth);
        double start = originPx - radius;
        double end = originPx + radius;
        int hStart = static_cast<int>(std::floor(std::max(0.0, start)));
        int hEnd = static_cast<int>(std::ceil(std::min(static_cast<double>(areaWidth), end)));
        if (hStart >= hEnd) return HighlightSpan{-1, -1, done};
        return HighlightSpan{hStart, hEnd, done};
    };

    auto drawRowTextSegmented = [&](int rowY, int rowX, const std::string& line,
                                    int highlightStart, int highlightEnd,
                                    const RGBColor& baseFg, const RGBColor& baseBg,
                                    const RGBColor& hiFg, const RGBColor& hiBg) {
        int cursorX = rowX;
        for (size_t pos = 0; pos < line.size();) {
            auto info = TuiUtils::nextUtf8Char(line, pos);
            if (info.length == 0) break;
            int relX = cursorX - rowX;
            bool inHighlight = relX >= highlightStart && relX < highlightEnd;
            const RGBColor& useBg = inHighlight ? hiBg : baseBg;
            const RGBColor& useFg = inHighlight ? hiFg : baseFg;
            if (TuiCell* cell = surface.editCell(cursorX, rowY)) {
                cell->glyph = line.substr(pos, info.length);
                cell->fg = useFg;
                cell->bg = useBg;
                cell->hasBg = true;
                cell->isContinuation = false;
            }
            if (info.visualWidth == 2) {
                if (TuiCell* cont = surface.editCell(cursorX + 1, rowY)) {
                    cont->glyph.clear();
                    cont->fg = useFg;
                    cont->bg = useBg;
                    cont->hasBg = true;
                    cont->isContinuation = true;
                }
            }
            cursorX += static_cast<int>(info.visualWidth);
            pos += info.length;
            if (relX > areaWidth) break;
        }
    };

    for (size_t i = 0; i < options.size(); ++i) {
        bool focus = i == selected;
        std::string marker = focus ? "▶ " : "  ";
        size_t markerWidth = TuiUtils::calculateUtf8VisualWidth(marker);
        std::string text = options[i];
        size_t textWidth = TuiUtils::calculateUtf8VisualWidth(text);
        if (markerWidth + textWidth > static_cast<size_t>(areaWidth)) {
            size_t trimWidth = static_cast<size_t>(areaWidth > static_cast<int>(markerWidth) ? areaWidth - static_cast<int>(markerWidth) : 0);
            text = TuiUtils::trimToUtf8VisualWidth(text, trimWidth);
            textWidth = TuiUtils::calculateUtf8VisualWidth(text);
        }

        std::string line = marker + text;
        double stripeBlend = 0.25 + (static_cast<int>(i) % 2) * 0.05;
        RGBColor rowBg = TuiUtils::blendColor(theme.panel, theme.background, stripeBlend);
        RGBColor rowFg = theme.itemFg;
        surface.fillRect(x + 2, listStart + static_cast<int>(i), areaWidth, 1, rowFg, rowBg, " ");

        RGBColor hiliteBaseBg = TuiUtils::blendColor(theme.focusBg, theme.accent, 0.35);
        RGBColor hiliteBaseFg = theme.focusFg;

        auto blendedBg = [&](int hStart, int hEnd, bool isActiveSpan) {
            if (hStart < 0 || hEnd <= hStart) return rowBg;
            double alpha = std::clamp(static_cast<double>(hEnd - hStart) / std::max(1, areaWidth), 0.0, 1.0);
            if (!isActiveSpan) {
                alpha *= 0.8;
            }
            return TuiUtils::blendColor(rowBg, hiliteBaseBg, alpha);
        };

        auto blendedFg = [&](int hStart, int hEnd, bool isActiveSpan) {
            if (hStart < 0 || hEnd <= hStart) return rowFg;
            double alpha = std::clamp(static_cast<double>(hEnd - hStart) / std::max(1, areaWidth), 0.0, 1.0);
            if (!isActiveSpan) alpha *= 0.6;
            return TuiUtils::blendColor(rowFg, hiliteBaseFg, alpha);
        };

        int highlightStart = -1;
        int highlightEnd = -1;
        RGBColor hBg = rowBg;
        RGBColor hFg = rowFg;
        if (hasFadeAnim && fadeRow == i) {
            auto span = spanFor(false, true, fadeOriginNorm, fadeStart, kFadeDuration);
            if (span.done) { hasFadeAnim = false; }
            if (span.start >= 0 && span.end > span.start) {
                highlightStart = span.start;
                highlightEnd = span.end;
                hBg = blendedBg(span.start, span.end, false);
                hFg = blendedFg(span.start, span.end, false);
            }
        }


        auto applyHighlightSpan = [&](int rowY, int hStart, int hEnd, const RGBColor& hiFg, const RGBColor& hiBg) {
            int start = std::max(0, hStart);
            int end = std::min(areaWidth, hEnd);
            if (start >= end) return;
            for (int px = start; px < end; ++px) {
                if (TuiCell* cell = surface.editCell(x + 2 + px, rowY)) {
                    cell->bg = hiBg;
                    cell->fg = hiFg;
                    cell->hasBg = true;
                }
            }
        };

        if (focus) {
            int focusStart = 0;
            int focusEnd = areaWidth;
            if (hasActiveAnim) {
                auto span = spanFor(true, false, activeOriginNorm, activeStart, kExpandDuration);
                if (span.done) { hasActiveAnim = false; }
                if (span.start >= 0 && span.end > span.start) {
                    focusStart = span.start;
                    focusEnd = span.end;
                }
            }
            highlightStart = focusStart;
            highlightEnd = focusEnd;
            hBg = blendedBg(focusStart, focusEnd, true);
            hFg = blendedFg(focusStart, focusEnd, true);
        }

        RGBColor markerFg = rowFg;
        int rowY = listStart + static_cast<int>(i);
        applyHighlightSpan(rowY, highlightStart, highlightEnd, hFg, hBg);
        drawRowTextSegmented(rowY, x + 2, marker, highlightStart, highlightEnd, markerFg, rowBg, hFg, hBg);
        drawRowTextSegmented(rowY, x + 2 + static_cast<int>(markerWidth), text,
                            highlightStart - static_cast<int>(markerWidth),
                            highlightEnd - static_cast<int>(markerWidth),
                            rowFg, rowBg, hFg, hBg);
    }
}

void MenuView::startSelectionChange(size_t newSel, double originNorm) {
    if (options.empty()) return;
    auto now = std::chrono::steady_clock::now();
    if (selected < options.size() && newSel != selected) {
        fadeRow = selected;
        fadeOriginNorm = activeOriginNorm;
        fadeStart = now;
        hasFadeAnim = true;
    }
    selected = newSel;
    activeOriginNorm = std::clamp(originNorm, 0.0, 1.0);
    activeStart = now;
    hasActiveAnim = true;
}

double MenuView::easeOutCubic(double t) {
    double clamped = std::clamp(t, 0.0, 1.0);
    double inv = 1.0 - clamped;
    return 1.0 - inv * inv * inv;
}

} // namespace UI
} // namespace TilelandWorld
