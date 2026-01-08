#include "AboutScreen.h"
#include "TuiUtils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    const BoxStyle kModernFrame{"╭", "╮", "╰", "╯", "─", "│"};
    const std::vector<std::string> kBannerLines = {
        "████████╗██╗██╗     ███████╗██╗      █████╗ ███╗   ██╗██████╗     ██╗    ██╗ ██████╗ ██████╗ ██╗     ██████╗ ",
        "╚══██╔══╝██║██║     ██╔════╝██║     ██╔══██╗████╗  ██║██╔══██╗    ██║    ██║██╔═══██╗██╔══██╗██║     ██╔══██╗",
        "   ██║   ██║██║     █████╗  ██║     ███████║██╔██╗ ██║██║  ██║    ██║ █╗ ██║██║   ██║██████╔╝██║     ██║  ██║",
        "   ██║   ██║██║     ██╔══╝  ██║     ██╔══██║██║╚██╗██║██║  ██║    ██║███╗██║██║   ██║██╔══██╗██║     ██║  ██║",
        "   ██║   ██║███████╗███████╗███████╗██║  ██║██║ ╚████║██████╔╝    ╚███╔███╔╝╚██████╔╝██║  ██║███████╗██████╔╝",
        "   ╚═╝   ╚═╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝      ╚══╝╚══╝  ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═════╝ "
    };
}

AboutScreen::AboutScreen()
    : surface(110, 40),
      input(std::make_unique<InputController>()) {}

std::vector<AboutScreen::Entry> AboutScreen::buildEntries() {
    auto& env = EnvConfig::getInstance();
    env.refresh();
    const auto& st = env.getStaticInfo();
    const auto& rt = env.getRuntimeInfo();

    auto rectToString = [](const RECT& r) {
        std::ostringstream ss; ss << "L:" << r.left << " T:" << r.top
                                   << " W:" << (r.right - r.left)
                                   << " H:" << (r.bottom - r.top); return ss.str(); };
    auto pointToString = [](const POINT& p) {
        std::ostringstream ss; ss << p.x << ", " << p.y; return ss.str(); };
    auto dbl2 = [](double a, double b, int prec = 2) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(prec) << a << " x " << b; return ss.str(); };
    auto dblPair = [](double a, double b, int prec = 2) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(prec) << a << ", " << b; return ss.str(); };

    std::vector<Entry> out = {
        {"Env name", st.envName},
        {"VT enabled", st.vtEnabled ? "Yes" : "No"},
        {"Running in WT", st.isRunningInWT ? "Yes" : "No"},
        {"Scaling", dblPair(st.scaling, st.scaling, 2)},
        {"Font (WinAPI)", dbl2(st.fontWidthWin, st.fontHeightWin, 0)},
        {"VT cells", dbl2(st.vtCols, st.vtRows, 0)},
        {"VT pixels", dbl2(st.vtPixW, st.vtPixH, 0)},

        {"Console cols", std::to_string(rt.consoleCols)},
        {"Console rows", std::to_string(rt.consoleRows)},
        {"Client rect", rectToString(rt.clientRect)},
        {"Client abs LT", pointToString(rt.clientAbsLT)},
        {"Window rect", rectToString(rt.windowRect)},
        {"WT client", "L:" + std::to_string(rt.wtClientL) + " T:" + std::to_string(rt.wtClientT) +
                       " W:" + std::to_string(rt.wtClientW) + " H:" + std::to_string(rt.wtClientH)},
        {"WT client abs", pointToString(rt.wtClientAbs)},
        {"Font (calc)", dbl2(rt.calcFontW, rt.calcFontH)},
        {"Font (WT)", dbl2(rt.wtFontW, rt.wtFontH)},
        {"Mouse screen", pointToString(rt.mouseScreen)},
        {"Mouse scaled", dblPair(rt.mouseScreenScaled.x, rt.mouseScreenScaled.y, 0)},
        {"Mouse cell (VT)", dblPair(rt.mouseCellVt.x, rt.mouseCellVt.y)},
        {"Mouse cell (Win)", dblPair(rt.mouseCellWin.x, rt.mouseCellWin.y)}
    };

    return out;
}

int AboutScreen::computeMaxLabel(const std::vector<Entry>& entries) const {
    int maxLen = 0;
    for (const auto& e : entries) {
        maxLen = std::max<int>(maxLen, static_cast<int>(e.label.size()));
    }
    return maxLen;
}

void AboutScreen::clampScroll(int totalRows) {
    int maxScroll = std::max(0, totalRows - listHeight);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
}

void AboutScreen::render(const std::vector<Entry>& entries, int maxLabelWidth) {
    surface.clear({220, 230, 240}, {12, 14, 18}, " ");

    // top accent bars
    surface.fillRect(0, 0, surface.getWidth(), 1, {96, 140, 255}, {96, 140, 255}, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, {96, 140, 255}, {96, 140, 255}, " ");

    // banner
    int bannerStartY = 2;
    for (size_t i = 0; i < kBannerLines.size(); ++i) {
        double fade = static_cast<double>(i) / std::max<size_t>(1, kBannerLines.size() - 1);
        RGBColor rowBg = TuiUtils::blendColor({96, 140, 255}, {18, 21, 28}, 0.35 + fade * 0.15);
        RGBColor rowFg = TuiUtils::blendColor({220, 230, 255}, {200, 230, 255}, 0.4 + fade * 0.1);
        surface.fillRect(0, bannerStartY + static_cast<int>(i), surface.getWidth(), 1, rowFg, rowBg, " ");
        std::string line = kBannerLines[i];
        size_t vis = TuiUtils::calculateUtf8VisualWidth(line);
        int maxVis = surface.getWidth();
        if (static_cast<int>(vis) > maxVis) {
            line = TuiUtils::trimToUtf8VisualWidth(line, static_cast<size_t>(maxVis));
        }
        surface.drawCenteredText(0, bannerStartY + static_cast<int>(i), surface.getWidth(), line, rowFg, rowBg);
    }

    listStartY = bannerStartY + static_cast<int>(kBannerLines.size()) + 2;
    int bottomPadding = 3;
    listHeight = std::max(1, surface.getHeight() - listStartY - bottomPadding);

    clampScroll(static_cast<int>(entries.size()));

    // panel background
    surface.fillRect(1, listStartY - 1, surface.getWidth() - 2, listHeight + 1, {210, 215, 224}, {18, 21, 28}, " ");

    int labelX = 4;
    int valueX = labelX + maxLabelWidth + 2;

    for (int row = 0; row < listHeight; ++row) {
        int idx = row + scrollOffset;
        if (idx >= static_cast<int>(entries.size())) break;
        bool alt = (row % 2) == 1;
        RGBColor fg = {210, 215, 224};
        RGBColor bg = alt ? RGBColor{20, 24, 32} : RGBColor{18, 21, 28};
        surface.fillRect(2, listStartY + row, surface.getWidth() - 4, 1, fg, bg, " ");

        std::string label = entries[static_cast<size_t>(idx)].label;
        if (static_cast<int>(label.size()) < maxLabelWidth) {
            label = std::string(static_cast<size_t>(maxLabelWidth - static_cast<int>(label.size())), ' ') + label;
        }
        surface.drawText(labelX, listStartY + row, label + "  ", {160, 170, 190}, bg);
        surface.drawText(valueX, listStartY + row, entries[static_cast<size_t>(idx)].value, fg, bg);
    }

    std::ostringstream footer;
    footer << "Up/Down scroll · Mouse wheel · Q to exit";
    surface.drawCenteredText(0, surface.getHeight() - 2, surface.getWidth(), footer.str(), {160, 170, 190}, {12, 14, 18});
}

void AboutScreen::show() {
    input->start();
    bool running = true;
    std::cout << "\x1b[?25l" << "\x1b[2J\x1b[H" << std::flush;

    while (running) {
        auto entries = buildEntries();
        int maxLabel = computeMaxLabel(entries);

        render(entries, maxLabel);
        painter.present(surface, true, 1, 1);

        auto events = input->pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::ArrowUp) { --scrollOffset; }
                else if (ev.key == InputKey::ArrowDown) { ++scrollOffset; }
                else if (ev.key == InputKey::Escape || ev.key == InputKey::Unknown) { running = false; break; }
                else if (ev.key == InputKey::Character) {
                    char c = static_cast<char>(ev.ch);
                    if (c == 'q' || c == 'Q') { running = false; break; }
                    if (c == 'w' || c == 'W') { --scrollOffset; }
                    if (c == 's' || c == 'S') { ++scrollOffset; }
                }
            } else if (ev.type == InputEvent::Type::Mouse) {
                if (ev.wheel != 0) {
                    scrollOffset -= ev.wheel; // wheel>0 up
                }
            }
        }
    }

    painter.reset();
    input->stop();
}

} // namespace UI
} // namespace TilelandWorld
