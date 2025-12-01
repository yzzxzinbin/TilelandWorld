#pragma once
#ifndef TILELANDWORLD_TUIRENDERER_H
#define TILELANDWORLD_TUIRENDERER_H

#include "../Map.h"
#include "../Coordinates.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

namespace TilelandWorld {

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
        TuiRenderer(const Map& map, std::mutex& mapMutex);
        ~TuiRenderer();

        // 启动渲染线程
        void start();
        // 停止渲染线程
        void stop();

        // 更新视图参数 (由逻辑线程调用)
        void updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount, double tps);

    private:
        const Map& map; // 修改为 const 引用，确保调用 const 版本的 getTile
        std::mutex& mapMutex; // 引用控制器的互斥锁，用于保护 Map 读取

        std::thread renderThread;
        std::atomic<bool> running;
        
        // 视图状态 (受内部互斥锁保护)
        ViewState currentViewState;
        std::mutex viewStateMutex;

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

        // 渲染循环
        void renderLoop();

        // 内部辅助
        void copyMapData(const ViewState& state);
        void drawToConsole(const ViewState& state);
        
        // 优化：返回引用，避免拷贝
        const std::string& getCachedTileString(const Tile& tile);
        
        // 原始的格式化逻辑，用于生成缓存
        std::string generateTileString(const Tile& tile);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TUIRENDERER_H
