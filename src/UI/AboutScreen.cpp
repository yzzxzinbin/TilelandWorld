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
      input(std::make_unique<InputController>()) {
    auto& env = EnvConfig::getInstance();
    env.refresh();
    const auto& rt = env.getRuntimeInfo();
    surface.resize(rt.consoleCols, rt.consoleRows);
}

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

    auto formatBytes = [](size_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        if (bytes < 1024ULL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
        return std::to_string(bytes / (1024ULL * 1024 * 1024)) + " GB";
    };

    auto formatTime = [](double seconds) {
        int s = static_cast<int>(seconds) % 60;
        int m = (static_cast<int>(seconds) / 60) % 60;
        int h = static_cast<int>(seconds) / 3600;
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(2) << h << ":"
           << std::setfill('0') << std::setw(2) << m << ":"
           << std::setfill('0') << std::setw(2) << s;
        return ss.str();
    };

    std::vector<Entry> out = {
        {"Env name", st.envName},
        {"UserInfo", st.userInfo},
        {"Windows version", st.windowsVersion},
        {"Language", st.language},
        {"System DPI", std::to_string(st.systemDpi) + " (" + std::to_string(static_cast<int>(st.scaling * 100)) + "%)"},
        {"Memory usage", formatBytes(rt.memoryUsage)},
        {"Uptime", formatTime(rt.uptimeSeconds)},
        {"VT enabled", st.vtEnabled ? "Yes" : "No"},
        {"Running in WT", st.isRunningInWT ? "Yes" : "No"},
        {"Font (VT)", dbl2(st.vtFontW, st.vtFontH, 2)},
        {"Font (Win)", dbl2(st.fontWidthWin, st.fontHeightWin, 0)},
        {"Font (calc)", dbl2(rt.calcFontW, rt.calcFontH)},
        {"Font (WT-calc)", dbl2(rt.wtFontW, rt.wtFontH)},

        {"VT cells", dbl2(static_cast<double>(st.vtCols), static_cast<double>(st.vtRows), 0)},
        {"VT pixels", dbl2(static_cast<double>(st.vtPixW), static_cast<double>(st.vtPixH), 0)},

        {"Console size", std::to_string(rt.consoleCols) + " x " + std::to_string(rt.consoleRows)},
        {"Client rect", rectToString(rt.clientRect)},
        {"Client abs LT", pointToString(rt.clientAbsLT)},
        {"Window rect", rectToString(rt.windowRect)},
        {"WT client", "AbsL:" + std::to_string(rt.wtClientAbs.x) + " AbsT:" + std::to_string(rt.wtClientAbs.y) +
                       " W:" + std::to_string(rt.wtClientW) + " H:" + std::to_string(rt.wtClientH)},
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
        maxLen = std::max<int>(maxLen, static_cast<int>(TuiUtils::calculateUtf8VisualWidth(e.label)));
    }
    return maxLen;
}

void AboutScreen::clampScroll(int totalRows) {
    int maxScroll = std::max(0, totalRows - listHeight);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
}

void AboutScreen::render(const std::vector<Entry>& entries, int maxLabelWidth) {
    surface.clear({220, 230, 240}, {12, 14, 18}, " ");

    const int sw = surface.getWidth();
    const int sh = surface.getHeight();

    // top accent bars
    surface.fillRect(0, 0, sw, 1, {96, 140, 255}, {96, 140, 255}, " ");
    surface.fillRect(0, sh - 1, sw, 1, {96, 140, 255}, {96, 140, 255}, " ");

    // banner (centered)
    int bannerStartY = 2;
    for (size_t i = 0; i < kBannerLines.size(); ++i) {
        double fade = static_cast<double>(i) / std::max<size_t>(1, kBannerLines.size() - 1);
        RGBColor rowBg = TuiUtils::blendColor({96, 140, 255}, {18, 21, 28}, 0.35 + fade * 0.15);
        RGBColor rowFg = TuiUtils::blendColor({220, 230, 255}, {200, 230, 255}, 0.4 + fade * 0.1);
        surface.fillRect(0, bannerStartY + static_cast<int>(i), sw, 1, rowFg, rowBg, " ");
        std::string line = kBannerLines[i];
        surface.drawCenteredText(0, bannerStartY + static_cast<int>(i), sw, line, rowFg, rowBg);
    }

    listStartY = bannerStartY + static_cast<int>(kBannerLines.size()) + 2;
    int bottomPadding = 3;
    listHeight = std::max(1, sh - listStartY - bottomPadding);

    clampScroll(static_cast<int>(entries.size()));

    // Center panel
    int panelWidth = std::min(sw - 4, std::max(80, maxLabelWidth + 50));
    int panelX = (sw - panelWidth) / 2;

    // panel background
    surface.fillRect(panelX, listStartY - 1, panelWidth, listHeight + 1, {210, 215, 224}, {18, 21, 28}, " ");

    int labelX = panelX + 3;
    int valueX = labelX + maxLabelWidth + 3;

    for (int row = 0; row < listHeight; ++row) {
        int idx = row + scrollOffset;
        if (idx >= static_cast<int>(entries.size())) break;
        bool alt = (row % 2) == 1;
        RGBColor fg = {210, 215, 224};
        RGBColor bg = alt ? RGBColor{20, 24, 32} : RGBColor{18, 21, 28};
        surface.fillRect(panelX + 1, listStartY + row, panelWidth - 2, 1, fg, bg, " ");

        std::string label = entries[static_cast<size_t>(idx)].label;
        int pad = maxLabelWidth - static_cast<int>(TuiUtils::calculateUtf8VisualWidth(label));
        if (pad > 0) label = std::string(static_cast<size_t>(pad), ' ') + label;

        surface.drawText(labelX, listStartY + row, label + ":", {160, 170, 190}, bg);
        surface.drawText(valueX, listStartY + row, entries[static_cast<size_t>(idx)].value, fg, bg);
    }

    // Scroll indicator if needed
    if (static_cast<int>(entries.size()) > listHeight) {
        int scrollX = panelX + panelWidth - 2;
        // Background track
        surface.fillRect(scrollX, listStartY, 1, listHeight, {60, 70, 80}, {12, 14, 18}, " ");
        
        int totalRows = static_cast<int>(entries.size());
        int thumbH = std::max(1, (listHeight * listHeight) / totalRows);
        int maxScroll = totalRows - listHeight;
        // Correctly map [0, maxScroll] -> [0, listHeight - thumbH] to ensure it reaches the bottom
        int thumbY = (maxScroll > 0) ? (scrollOffset * (listHeight - thumbH) / maxScroll) : 0;
        
        surface.fillRect(scrollX, listStartY + thumbY, 1, thumbH, {96, 140, 255}, {96, 140, 255}, " ");
    }

    std::ostringstream footer;
    footer << "Up/Down · Mouse wheel · Q to exit";
    surface.drawCenteredText(0, sh - 2, sw, footer.str(), {160, 170, 190}, {12, 14, 18});
}

void AboutScreen::show() {
    input->start();
    bool running = true;
    std::cout << "\x1b[?25l" << "\x1b[2J\x1b[H" << std::flush;

    while (running) {
        auto& env = EnvConfig::getInstance();
        env.refresh();
        const auto& rt = env.getRuntimeInfo();
        if (rt.consoleCols != surface.getWidth() || rt.consoleRows != surface.getHeight()) {
            surface.resize(rt.consoleCols, rt.consoleRows);
            std::cout << "\x1b[2J\x1b[H" << std::flush;
        }

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
                    scrollOffset -= ev.wheel;
                }
            }
        }
    }

    painter.reset();
    input->stop();
}

} // namespace UI
} // namespace TilelandWorld
