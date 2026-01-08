#pragma once
#ifndef TILELANDWORLD_ENVCONFIG_H
#define TILELANDWORLD_ENVCONFIG_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <windows.h>

namespace TilelandWorld {

struct DoublePoint {
    double x{0.0};
    double y{0.0};
};

struct EnvStaticInfo {
    bool vtEnabled{false};
    bool isRunningInWT{false};
    std::string envName{"Legacy Console"};
    double scaling{1.0};
    int fontWidthWin{0};
    int fontHeightWin{0};
    int vtRows{0};
    int vtCols{0};
    int vtPixW{0};
    int vtPixH{0};
};

struct EnvRuntimeInfo {
    RECT windowRect{0, 0, 0, 0};
    RECT clientRect{0, 0, 0, 0};
    POINT clientAbsLT{0, 0};

    int consoleCols{0};
    int consoleRows{0};

    int wtClientW{0};
    int wtClientH{0};
    int wtClientL{0};
    int wtClientT{0};
    POINT wtClientAbs{0, 0};

    double calcFontW{0.0};
    double calcFontH{0.0};
    double wtFontW{0.0};
    double wtFontH{0.0};

    POINT mouseScreen{0, 0};
    DoublePoint mouseScreenScaled{};
    DoublePoint mouseCellVt{};
    DoublePoint mouseCellWin{};
};

class EnvConfig {
public:
    static EnvConfig& getInstance();

    // 初始化环境配置并进行一次刷新。
    bool initialize();

    // 按需刷新运行时数据（窗口/客户区尺寸、鼠标位置等）。
    bool refresh();

    const EnvStaticInfo& getStaticInfo() const { return staticInfo; }
    const EnvRuntimeInfo& getRuntimeInfo() const { return runtimeInfo; }

private:
    EnvConfig() = default;

    bool initialized{false};
    EnvStaticInfo staticInfo{};
    EnvRuntimeInfo runtimeInfo{};
    HWND consoleWindow{nullptr};
    HWND rootWindow{nullptr};

    bool enableVTMode();
    std::string getProcessNameById(DWORD pid) const;
    void detectEnvironment();
    void updateStaticMetrics();
    void updateRuntimeMetrics();
    bool queryVTDimensions(int& rows, int& cols, int& pixW, int& pixH);
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_ENVCONFIG_H
