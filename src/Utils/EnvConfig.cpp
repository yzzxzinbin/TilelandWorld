#include "EnvConfig.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <psapi.h>
#include <sstream>
#include <thread>
#include <iostream>

namespace TilelandWorld {
namespace {
    constexpr int kWtOffsetLeft = 15;
    constexpr int kWtOffsetTop = 48;
    constexpr int kLegacyOffsetLeft = 7;
    constexpr int kLegacyOffsetTop = 30;
    constexpr int kRightBottomPadding = 16; // keep consistent with console_query sample
}

EnvConfig::EnvConfig() {
    startTime = std::chrono::steady_clock::now();
}

EnvConfig& EnvConfig::getInstance() {
    static EnvConfig instance;
    return instance;
}

bool EnvConfig::initialize() {
    detectEnvironment();
    staticInfo.vtEnabled = enableVTMode();
    fetchStaticSystemInfo();
    updateStaticMetrics();
    initialized = true;
    return refresh();
}

bool EnvConfig::refresh() {
    if (!initialized) {
        initialize();
        return initialized;
    }
    updateRuntimeMetrics();
    return true;
}

std::string EnvConfig::getProcessNameById(DWORD pid) const {
    char buffer[MAX_PATH] = {0};
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        if (GetModuleFileNameExA(hProcess, NULL, buffer, MAX_PATH)) {
            std::string path = buffer;
            size_t pos = path.find_last_of("\\/");
            CloseHandle(hProcess);
            if (pos != std::string::npos) return path.substr(pos + 1);
            return path;
        }
        CloseHandle(hProcess);
    }
    return std::string();
}

bool EnvConfig::enableVTMode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD outMode = 0;
    if (!GetConsoleMode(hOut, &outMode)) return false;
    DWORD desiredOut = outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    bool outOk = SetConsoleMode(hOut, desiredOut) != 0;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return outOk;
    DWORD inMode = 0;
    if (!GetConsoleMode(hIn, &inMode)) return outOk;
    DWORD desiredIn = inMode | ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hIn, desiredIn);
    return outOk;
}

void EnvConfig::detectEnvironment() {
    consoleWindow = GetConsoleWindow();
    rootWindow = consoleWindow;
    staticInfo.envName = "Legacy Console";
    staticInfo.isRunningInWT = false;

    if (consoleWindow) {
        HWND owner = GetAncestor(consoleWindow, GA_ROOTOWNER);
        if (owner) {
            rootWindow = owner;
            DWORD ownerPid = 0;
            GetWindowThreadProcessId(owner, &ownerPid);
            std::string ownerName = getProcessNameById(ownerPid);
            if (!ownerName.empty()) {
                std::string lower = ownerName;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower == "windowsterminal.exe") {
                    staticInfo.isRunningInWT = true;
                    staticInfo.envName = "Windows Terminal (WT)";
                } else {
                    staticInfo.envName = "Hosted console (" + ownerName + ")";
                }
            }
        }
    }
}

bool EnvConfig::queryVTDimensions(int& rows, int& cols, int& pixW, int& pixH) {
    rows = cols = pixW = pixH = 0;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    DWORD oldInMode = 0;
    GetConsoleMode(hIn, &oldInMode);
    SetConsoleMode(hIn, oldInMode | ENABLE_VIRTUAL_TERMINAL_INPUT);

    std::string seq;
    std::cout << "\x1b[18t\x1b[14t" << std::flush;

    auto parseSeq = [&](const std::string& text) {
        size_t pos = 0;
        while ((pos = text.find("\x1b[", pos)) != std::string::npos) {
            if (text.compare(pos, 3, "\x1b[8") == 0) {
                int r = 0, c = 0;
                if (std::sscanf(text.c_str() + pos, "\x1b[8;%d;%dt", &r, &c) == 2) {
                    if (r > 0 && c > 0) { rows = r; cols = c; }
                }
            } else if (text.compare(pos, 3, "\x1b[4") == 0) {
                int h = 0, w = 0;
                if (std::sscanf(text.c_str() + pos, "\x1b[4;%d;%dt", &h, &w) == 2) {
                    if (h > 0 && w > 0) { pixH = h; pixW = w; }
                }
            }
            ++pos;
        }
    };

    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() - start < timeout) {
        DWORD available = 0;
        if (!GetNumberOfConsoleInputEvents(hIn, &available) || available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        while (available-- > 0) {
            INPUT_RECORD rec{};
            DWORD read = 0;
            if (!ReadConsoleInputA(hIn, &rec, 1, &read) || read == 0) break;
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                char ch = rec.Event.KeyEvent.uChar.AsciiChar;
                if (ch != 0) seq.push_back(ch);
            }
        }
        parseSeq(seq);
        if ((rows > 0 && cols > 0) && (pixW > 0 && pixH > 0)) break;
    }

    SetConsoleMode(hIn, oldInMode);
    return rows > 0 && cols > 0;
}

void EnvConfig::updateStaticMetrics() {
    // DPI scaling
    staticInfo.scaling = 1.0;
    HWND dpiWindow = rootWindow ? rootWindow : consoleWindow;
    if (dpiWindow) {
        UINT dpi = GetDpiForWindow(dpiWindow);
        if (dpi > 0) staticInfo.scaling = static_cast<double>(dpi) / 96.0;
    }

    // WinAPI reported font size
    CONSOLE_FONT_INFOEX cfi{};
    cfi.cbSize = sizeof(cfi);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetCurrentConsoleFontEx(hOut, FALSE, &cfi)) {
        staticInfo.fontWidthWin = static_cast<int>(cfi.dwFontSize.X);
        staticInfo.fontHeightWin = static_cast<int>(cfi.dwFontSize.Y);
    }

    // VT query for dimensions
    int vtRows = 0, vtCols = 0, vtPixW = 0, vtPixH = 0;
    if (queryVTDimensions(vtRows, vtCols, vtPixW, vtPixH)) {
        staticInfo.vtRows = vtRows;
        staticInfo.vtCols = vtCols;
        staticInfo.vtPixW = vtPixW;
        staticInfo.vtPixH = vtPixH;
        staticInfo.vtFontW = (vtCols > 0) ? static_cast<double>(vtPixW) / vtCols : 0.0;
        staticInfo.vtFontH = (vtRows > 0) ? static_cast<double>(vtPixH) / vtRows : 0.0;
    }

    updateRuntimeMetrics();
}

void EnvConfig::fetchStaticSystemInfo() {
    // Windows Version
    staticInfo.windowsVersion = "Windows 10+";
    OSVERSIONINFOEXA osvi = { sizeof(osvi) };
#pragma warning(suppress : 4996)
    if (GetVersionExA((OSVERSIONINFOA*)&osvi)) {
        std::ostringstream oss;
        oss << (int)osvi.dwMajorVersion << "." << (int)osvi.dwMinorVersion << " (Build " << osvi.dwBuildNumber << ")";
        staticInfo.windowsVersion = oss.str();
    }

    // System DPI
    HDC hdc = GetDC(NULL);
    if (hdc) {
        staticInfo.systemDpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }

    // Language
    char lang[80] = {0}; // LOCALE_NAME_MAX_LENGTH
    if (GetUserDefaultLocaleName(reinterpret_cast<LPWSTR>(lang), 80)) {
        // GetUserDefaultLocaleName returns wide string, but we want UTF-8. 
        // For simplicity, let's use GetLocaleInfoEx if we need string.
        wchar_t wlang[80] = {0};
        if (GetUserDefaultLocaleName(wlang, 80)) {
            char clang[160] = {0};
            WideCharToMultiByte(CP_UTF8, 0, wlang, -1, clang, 160, NULL, NULL);
            staticInfo.language = clang;
        }
    }

    // UserInfo
    char user[256] = {0};
    DWORD userLen = 256;
    GetUserNameA(user, &userLen);

    char comp[256] = {0};
    DWORD compLen = 256;
    GetComputerNameA(comp, &compLen);

    staticInfo.userInfo = std::string(user) + "@" + std::string(comp);
}

void EnvConfig::updateRuntimeMetrics() {
    // Window and client rectangles
    RECT client{};
    RECT window{};
    if (consoleWindow) {
        GetClientRect(consoleWindow, &client);
        POINT lt{client.left, client.top};
        ClientToScreen(consoleWindow, &lt);
        runtimeInfo.clientRect = client;
        runtimeInfo.clientAbsLT = lt;
    } else {
        runtimeInfo.clientRect = {0, 0, 0, 0};
        runtimeInfo.clientAbsLT = {0, 0};
    }

    if (rootWindow) {
        GetWindowRect(rootWindow, &window);
        runtimeInfo.windowRect = window;
    } else if (consoleWindow) {
        GetWindowRect(consoleWindow, &window);
        runtimeInfo.windowRect = window;
    } else {
        runtimeInfo.windowRect = {0, 0, 0, 0};
    }

    // Console rows/cols
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    int cols = 0, rows = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    runtimeInfo.consoleCols = cols;
    runtimeInfo.consoleRows = rows;

    // WT-style offsets and derived client metrics
    int offsetL = staticInfo.isRunningInWT ? kWtOffsetLeft : kLegacyOffsetLeft;
    int offsetT = staticInfo.isRunningInWT ? kWtOffsetTop : kLegacyOffsetTop;
    int winW = runtimeInfo.windowRect.right - runtimeInfo.windowRect.left;
    int winH = runtimeInfo.windowRect.bottom - runtimeInfo.windowRect.top;
    runtimeInfo.wtClientL = offsetL;
    runtimeInfo.wtClientT = offsetT;
    runtimeInfo.wtClientW = std::max(0, winW - offsetL - kRightBottomPadding);
    runtimeInfo.wtClientH = std::max(0, winH - offsetT - kRightBottomPadding);
    runtimeInfo.wtClientAbs = {
        runtimeInfo.windowRect.left + runtimeInfo.wtClientL,
        runtimeInfo.windowRect.top + runtimeInfo.wtClientT
    };

    // Calculated font sizes
    runtimeInfo.calcFontW = (cols > 0) ? static_cast<double>(runtimeInfo.clientRect.right - runtimeInfo.clientRect.left) / cols : 0.0;
    runtimeInfo.calcFontH = (rows > 0) ? static_cast<double>(runtimeInfo.clientRect.bottom - runtimeInfo.clientRect.top) / rows : 0.0;
    runtimeInfo.wtFontW = (cols > 0) ? static_cast<double>(runtimeInfo.wtClientW) / cols : 0.0;
    runtimeInfo.wtFontH = (rows > 0) ? static_cast<double>(runtimeInfo.wtClientH) / rows : 0.0;

    double vtFontW = (staticInfo.vtCols > 0) ? static_cast<double>(staticInfo.vtPixW) / static_cast<double>(staticInfo.vtCols) : runtimeInfo.calcFontW;
    double vtFontH = (staticInfo.vtRows > 0) ? static_cast<double>(staticInfo.vtPixH) / static_cast<double>(staticInfo.vtRows) : runtimeInfo.calcFontH;

    // Mouse positions
    POINT screenP{0, 0};
    GetCursorPos(&screenP);
    runtimeInfo.mouseScreen = screenP;
    runtimeInfo.mouseScreenScaled = { screenP.x * staticInfo.scaling, screenP.y * staticInfo.scaling };

    // VT cell estimate (1-based to match VT reporting style)
    runtimeInfo.mouseCellVt.x = (vtFontW > 0.0) ? ((static_cast<double>(screenP.x) - runtimeInfo.wtClientAbs.x) / vtFontW) + 1.0 : 0.0;
    runtimeInfo.mouseCellVt.y = (vtFontH > 0.0) ? ((static_cast<double>(screenP.y) - runtimeInfo.wtClientAbs.y) / vtFontH) + 1.0 : 0.0;

    // WinAPI cell estimate using WT-corrected client size
    runtimeInfo.mouseCellWin.x = (runtimeInfo.wtFontW > 0.0) ? ((static_cast<double>(screenP.x) - runtimeInfo.wtClientAbs.x) / runtimeInfo.wtFontW) + 1.0 : 0.0;
    runtimeInfo.mouseCellWin.y = (runtimeInfo.wtFontH > 0.0) ? ((static_cast<double>(screenP.y) - runtimeInfo.wtClientAbs.y) / runtimeInfo.wtFontH) + 1.0 : 0.0;

    // Memory usage
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        runtimeInfo.memoryUsage = pmc.WorkingSetSize;
    }

    // Uptime
    auto now = std::chrono::steady_clock::now();
    runtimeInfo.uptimeSeconds = std::chrono::duration<double>(now - startTime).count();
}

} // namespace TilelandWorld
