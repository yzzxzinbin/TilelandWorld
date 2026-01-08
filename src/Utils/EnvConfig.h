#pragma once
#ifndef TILELANDWORLD_ENVCONFIG_H
#define TILELANDWORLD_ENVCONFIG_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <chrono>
#include <mutex>
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

    // New static fields
    std::string windowsVersion;
    int systemDpi{96};
    std::string language;
    std::string userInfo; // user@computer
};

struct EnvRuntimeInfo {
    RECT windowRect{0, 0, 0, 0};
    RECT clientRect{0, 0, 0, 0};
    POINT clientAbsLT{0, 0};

    int consoleCols{0};
    int consoleRows{0};

    int vtRows{0};
    int vtCols{0};
    int vtPixW{0};
    int vtPixH{0};
    double vtFontW{0.0};
    double vtFontH{0.0};

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

    // New runtime fields
    size_t memoryUsage{0};
    double uptimeSeconds{0.0};
};

class EnvConfig {
public:
    static EnvConfig& getInstance();

    // 初始化环境配置并进行一次刷新。
    bool initialize();

    // 按需刷新运行时数据（窗口/客户区尺寸、鼠标位置等）。
    bool refresh();

    // 更新 VT 报告的鼠标位置（由输入控制器解析序列后调用）
    void setMouseCellVt(double x, double y);

    // 更新 VT 像素和单元格尺寸（由输入控制器解析序列后调用）
    void setVtDimensions(int rows, int cols, int pixW, int pixH);

    const EnvStaticInfo& getStaticInfo() const { return staticInfo; }
    EnvRuntimeInfo getRuntimeInfo() const;

private:
    EnvConfig();

    bool initialized{false};
    EnvStaticInfo staticInfo{};
    EnvRuntimeInfo runtimeInfo{};
    mutable std::mutex dataMutex;
    HWND consoleWindow{nullptr};
    HWND rootWindow{nullptr};
    std::chrono::steady_clock::time_point startTime{};

    bool enableVTMode();
    std::string getProcessNameById(DWORD pid) const;
    void detectEnvironment();
    void updateStaticMetrics();
    void updateRuntimeMetrics();
    bool queryVTDimensions(int& rows, int& cols, int& pixW, int& pixH);

    // Helpers for new fields
    void fetchStaticSystemInfo();
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_ENVCONFIG_H
