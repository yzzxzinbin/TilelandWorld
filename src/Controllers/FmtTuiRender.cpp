#include "TuiRenderer.h"
#include "../TerrainTypes.h"
#include <fmt/format.h>
#include <fmt/printf.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <iterator>

namespace TilelandWorld {

namespace {
    // 预计算 0-255 的字符串，避免循环内重复 std::to_string
    const std::string& numStr(uint8_t n)
    {
        static std::array<std::string, 256> table{};
        static bool initialized = false;
        if (!initialized)
        {
            for (int i = 0; i < 256; ++i)
            {
                table[static_cast<size_t>(i)] = std::to_string(i);
            }
            initialized = true;
        }
        return table[n];
    }

    inline bool isNonBlack(const RGBColor& c)
    {
        return (static_cast<int>(c.r) | static_cast<int>(c.g) | static_cast<int>(c.b)) != 0;
    }

    inline uint8_t blendComp(uint8_t top, uint8_t bottom, uint8_t alpha)
    {
        int a = static_cast<int>(alpha);
        return static_cast<uint8_t>((static_cast<int>(top) * a + static_cast<int>(bottom) * (255 - a) + 127) / 255);
    }

    inline std::uint64_t fnv1aHash(const std::vector<std::string>& lines, const std::string& status)
    {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const std::string& s)
        {
            for (unsigned char c : s)
            {
                h ^= static_cast<std::uint64_t>(c);
                h *= 1099511628211ull;
            }
            h ^= 0xFFu;
            h *= 1099511628211ull;
        };

        for (const auto& line : lines)
        {
            mix(line);
        }
        mix(status);
        return h;
    }
}

void TuiRenderer::drawToConsoleFmt(const ViewState& state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha)
{
    bool useOverlay = overlay && overlayAlpha > 0.0001;
    int overlayWidth = 0;
    int overlayHeight = 0;
    if (useOverlay)
    {
        overlayWidth = overlay->getWidth();
        overlayHeight = overlay->getHeight();
    }

    uint8_t overlayAlphaFixed = static_cast<uint8_t>(std::clamp(overlayAlpha, 0.0, 1.0) * 255.0 + 0.5);

    std::vector<std::string> frameLines(static_cast<size_t>(state.height));

    for (int y = 0; y < state.height; ++y)
    {
        fmt::memory_buffer line;
        fmt::format_to(std::back_inserter(line), "\x1b[{};1H", y + 1);

        RGBColor lastFg{0, 0, 0};
        RGBColor lastBg{0, 0, 0};
        bool colorSet = false;

        const UI::TuiCell* overlayRow = nullptr;
        if (useOverlay && y < overlayHeight)
        {
            overlayRow = &overlay->data()[static_cast<size_t>(y) * overlayWidth];
        }

        for (int x = 0; x < state.width; ++x)
        {
            const Tile& tile = tileBuffer[y * state.width + x];
            const auto& props = getTerrainProperties(tile.terrain);

            RGBColor mapFg = tile.getForegroundColor();
            RGBColor mapBg = tile.getBackgroundColor();
            std::string mapGlyph = props.displayChar.empty() ? " " : props.displayChar;

            auto emitGlyph = [&](const RGBColor& fg, const RGBColor& bg, const std::string& glyph)
            {
                if (!colorSet || fg.r != lastFg.r || fg.g != lastFg.g || fg.b != lastFg.b || bg.r != lastBg.r || bg.g != lastBg.g || bg.b != lastBg.b)
                {
                    fmt::format_to(
                        std::back_inserter(line),
                        "\x1b[48;2;{};{};{}m\x1b[38;2;{};{};{}m",
                        numStr(bg.r), numStr(bg.g), numStr(bg.b),
                        numStr(fg.r), numStr(fg.g), numStr(fg.b));
                    colorSet = true;
                    lastFg = fg;
                    lastBg = bg;
                }
                fmt::format_to(std::back_inserter(line), "{}", glyph);
            };

            if (!props.isVisible)
            {
                emitGlyph(mapFg, mapBg, "  ");
                continue;
            }

            if (!useOverlay)
            {
                emitGlyph(mapFg, mapBg, mapGlyph);
                emitGlyph(mapFg, mapBg, mapGlyph);
                continue;
            }

            bool tileHasOverlay = false;
            if (overlayRow)
            {
                const UI::TuiCell& c1 = overlayRow[x * 2];
                const UI::TuiCell& c2 = overlayRow[x * 2 + 1];
                if (c1.hasBg || c2.hasBg || isNonBlack(c1.bg) || isNonBlack(c2.bg) || (!c1.glyph.empty() && c1.glyph != " ") || (!c2.glyph.empty() && c2.glyph != " "))
                {
                    tileHasOverlay = true;
                }
            }

            if (!tileHasOverlay)
            {
                emitGlyph(mapFg, mapBg, mapGlyph);
                emitGlyph(mapFg, mapBg, mapGlyph);
                continue;
            }

            for (int slot = 0; slot < 2; ++slot)
            {
                int uiX = x * 2 + slot;
                RGBColor finalFg = mapFg;
                RGBColor finalBg = mapBg;
                std::string finalGlyph = mapGlyph;

                if (overlayRow && uiX < overlayWidth)
                {
                    const UI::TuiCell& cell = overlayRow[uiX];

                    if (!cell.glyph.empty() && cell.glyph != " ")
                    {
                        finalGlyph = cell.glyph;
                        finalFg = cell.fg;
                    }
                    else if (cell.hasBg)
                    {
                        finalGlyph = " ";
                        finalFg = cell.fg;
                    }

                    if ((cell.hasBg || isNonBlack(cell.bg)) && overlayAlphaFixed > 0)
                    {
                        finalBg = RGBColor{
                            blendComp(cell.bg.r, mapBg.r, overlayAlphaFixed),
                            blendComp(cell.bg.g, mapBg.g, overlayAlphaFixed),
                            blendComp(cell.bg.b, mapBg.b, overlayAlphaFixed)
                        };
                    }
                }

                emitGlyph(finalFg, finalBg, finalGlyph);
            }
        }

        std::string reset = "\x1b[0m";
        line.append(reset.data(), reset.data() + reset.size());
        frameLines[static_cast<size_t>(y)] = fmt::to_string(line);
    }

    auto appendStr = [](fmt::memory_buffer& buf, const std::string& s) {
        buf.append(s.data(), s.data() + s.size());
    };

    fmt::memory_buffer status; // kept empty: renderer no longer emits bottom status text
    std::string statusLine = fmt::to_string(status);

    if (enableDiffOutput.load())
    {
        std::uint64_t frameHash = fnv1aHash(frameLines, statusLine);
        if (frameHash == lastFrameHash)
        {
            return;
        }

        drawDiffToConsoleFmt(frameLines, statusLine);
        lastFrameHash = frameHash;
        return;
    }

    fmt::memory_buffer output;
    std::string hideCursor = "\x1b[?25l";
    output.append(hideCursor.data(), hideCursor.data() + hideCursor.size());
    for (const auto& line : frameLines)
    {
        output.append(line.data(), line.data() + line.size());
    }

    fmt::print("{}", std::string_view(output.data(), output.size()));
    std::fflush(stdout);
}

void TuiRenderer::drawDiffToConsoleFmt(const std::vector<std::string>& lines, const std::string& statusLine)
{
    bool sizeChanged = lastFrameLines.size() != lines.size();
    if (sizeChanged)
    {
        lastFrameLines.assign(lines.size(), "");
    }

    fmt::memory_buffer diffOutput;
    std::string hideCursor = "\x1b[?25l";
    diffOutput.append(hideCursor.data(), hideCursor.data() + hideCursor.size());

    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (sizeChanged || lines[i] != lastFrameLines[i])
        {
            diffOutput.append(lines[i].data(), lines[i].data() + lines[i].size());
        }
    }

    fmt::print("{}", std::string_view(diffOutput.data(), diffOutput.size()));
    std::fflush(stdout);

    lastFrameLines = lines;
    lastStatusLine = statusLine;
}

} // namespace TilelandWorld
