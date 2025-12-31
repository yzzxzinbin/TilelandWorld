#pragma once
#ifndef TILELANDWORLD_TUIRENDERER_H
#define TILELANDWORLD_TUIRENDERER_H

#include "../Map.h"
#include "../Coordinates.h"
#include "../UI/AnsiTui.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace TilelandWorld {

    enum class RendererBackend { Std, Fmt };

    // 渲染用的视图状态快照
    struct ViewState {
        int viewX;
        int viewY;
        int currentZ;
        int width;
        int height;
        size_t modifiedChunkCount; // 用于UI显示
        double tps; // 新增：实时 TPS 值
    };

    class TuiRenderer {
    public:
        // 修改构造函数接受 const Map&，强制只读访问
        TuiRenderer(const Map& map, std::mutex& mapMutex, double statsAlpha = 0.10, bool enableStats = true, bool enableDiff = false, double fpsLimit = 360.0);
        ~TuiRenderer();

        // 启动渲染线程
        void start();
        // 停止渲染线程
        void stop();

        // 切换渲染输出 API（std / fmt）
        void setBackend(RendererBackend backend);

        // 更新视图参数 (由逻辑线程调用)
        void updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount, double tps);

        // 运行时更新渲染配置（无须重启线程）
        void applyRuntimeSettings(double statsAlpha, bool enableStats, bool enableDiff, double fpsCap);

        // 设置可选的 UI 覆盖层（与地图同尺寸网格），alpha 用于背景预混合 [0,1]
        void setUiLayer(std::shared_ptr<const UI::TuiSurface> layer, double alphaBg = 0.0);

        // 清除 UI 覆盖层
        void clearUiLayer();

    private:
        const Map& map; // 修改为 const 引用，确保调用 const 版本的 getTile
        std::mutex& mapMutex; // 引用控制器的互斥锁，用于保护 Map 读取

        std::thread renderThread;
        std::atomic<bool> running;
        
        // 视图状态 (受内部互斥锁保护)
        ViewState currentViewState;
        std::mutex viewStateMutex;

        // UI 覆盖层 (可选)。使用 shared_ptr 便于外部复用/更新。
        std::shared_ptr<const UI::TuiSurface> uiLayer;
        double uiLayerAlphaBg = 0.0; // 0 表示不混合
        std::mutex uiMutex;

        std::atomic<double> baseStatsAlpha{0.10};
        std::atomic<bool> enableStatsOverlay{true};
        std::atomic<bool> enableDiffOutput{false};
        std::atomic<double> targetFpsCap{360.0};
        std::atomic<bool> useFmtBackend{false};

        // 渲染缓冲区 (本地副本)
        std::vector<Tile> tileBuffer;
        
        // --- 优化：渲染缓存 ---
        // 使用 vector<vector<string>> 作为查找表
        // 第一维是 (int)TerrainType，第二维是 LightLevel (0-255)
        std::vector<std::vector<std::string>> renderCache;

        // FPS 计算相关
        double currentFps = 0.0;
        int frameCount = 0;
        long long lastFpsTime = 0;
        long long frequency = 0;

        // Diff rendering cache
        std::vector<std::string> lastFrameLines;
        std::string lastStatusLine;
        std::uint64_t lastFrameHash = 0;

        // 渲染循环
        void renderLoop();

        // 内部辅助
        void copyMapData(const ViewState& state);
        void drawToConsole(const ViewState& state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha);
        void drawToConsoleStd(const ViewState& state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha);
        void drawToConsoleFmt(const ViewState& state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha);
        void drawDiffToConsoleStd(const std::vector<std::string>& lines, const std::string& statusLine);
        void drawDiffToConsoleFmt(const std::vector<std::string>& lines, const std::string& statusLine);
        std::shared_ptr<UI::TuiSurface> buildStatsOverlay(const ViewState& state) const;
        static RGBColor blendColor(const RGBColor& top, const RGBColor& bottom, double alpha);
        
        // 优化：返回引用，避免拷贝
        const std::string& getCachedTileString(const Tile& tile);
        
        // 原始的格式化逻辑，用于生成缓存
        std::string generateTileString(const Tile& tile);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TUIRENDERER_H
