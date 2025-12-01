#pragma once
#ifndef TILELANDWORLD_TUICHUNKCONTROLLER_H
#define TILELANDWORLD_TUICHUNKCONTROLLER_H

#include "../Map.h"
#include "../Coordinates.h"
#include "TuiRenderer.h" 
#include "../MapGenInfrastructure/ChunkGeneratorPool.h" // 引入线程池
#include <unordered_set>
#include <string>
#include <mutex> 
#include <memory>

namespace TilelandWorld {

    class TuiChunkController {
    public:
        explicit TuiChunkController(Map& map);
        ~TuiChunkController();

        // 1. 初始化 TUI 环境
        void initialize();

        // 运行主循环 (处理输入、预加载)
        void run();

        // 3. 标记区块为已修改
        void markChunkModified(const ChunkCoord& coord);
        void markChunkModified(int cx, int cy, int cz);

        const std::unordered_set<ChunkCoord, ChunkCoordHash>& getModifiedChunks() const;

    private:
        Map& map;
        // 保护 Map 的互斥锁，用于协调预加载(写)和渲染复制(读)
        std::mutex mapMutex; 

        std::unordered_set<ChunkCoord, ChunkCoordHash> modifiedChunks;
        
        // 追踪正在生成中的区块，避免重复请求
        std::unordered_set<ChunkCoord, ChunkCoordHash> pendingChunks;

        // 渲染器实例
        std::unique_ptr<TuiRenderer> renderer;
        
        // 区块生成线程池
        std::unique_ptr<ChunkGeneratorPool> generatorPool;

        // 视图状态
        int viewX = 0;
        int viewY = 0;
        int currentZ = 0;
        int viewWidth = 64;
        int viewHeight = 48;
        bool running = true;
        
        // 输入状态追踪
        bool leftArrowPressedLastFrame = false;
        bool rightArrowPressedLastFrame = false;

        // 内部逻辑
        void handleInput();
        
        // 2. 预加载逻辑
        void preloadChunks();
        
        // 控制台辅助方法
        void setupConsole();
        void clearScreen();
        void showCursor();
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TUICHUNKCONTROLLER_H
