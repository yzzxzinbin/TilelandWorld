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
    };

    class TuiRenderer {
    public:
        TuiRenderer(Map& map, std::mutex& mapMutex);
        ~TuiRenderer();

        // 启动渲染线程
        void start();
        // 停止渲染线程
        void stop();

        // 更新视图参数 (由逻辑线程调用)
        void updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount);

    private:
        Map& map;
        std::mutex& mapMutex; // 引用控制器的互斥锁，用于保护 Map 读取

        std::thread renderThread;
        std::atomic<bool> running;
        
        // 视图状态 (受内部互斥锁保护)
        ViewState currentViewState;
        std::mutex viewStateMutex;

        // 渲染缓冲区 (本地副本)
        std::vector<Tile> tileBuffer;
        std::vector<std::string> outputBuffer; // 字符缓冲区

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
        std::string formatTileForTerminal(const Tile& tile);
        
        // 控制台控制
        void moveCursor(int row, int col);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TUIRENDERER_H
