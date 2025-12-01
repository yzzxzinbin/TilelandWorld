#include "TuiChunkController.h"
#include "../Constants.h"
#include "../Utils/Logger.h"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace TilelandWorld {

    TuiChunkController::TuiChunkController(Map& mapRef) : map(mapRef) {
        // 初始化渲染器
        renderer = std::make_unique<TuiRenderer>(map, mapMutex);
        // 初始化生成器线程池
        generatorPool = std::make_unique<ChunkGeneratorPool>(map);
    }

    TuiChunkController::~TuiChunkController() {
        if (renderer) {
            renderer->stop();
        }
        showCursor();
    }

    void TuiChunkController::initialize() {
        setupConsole();
        // 隐藏光标
        std::cout << "\x1b[?25l" << std::flush;
        
        // 初始预加载 (同步加载一小部分以避免开局空白)
        {
            int minCx = floorDiv(viewX, CHUNK_WIDTH);
            int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
            int minCy = floorDiv(viewY, CHUNK_HEIGHT);
            int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
            int cz = floorDiv(currentZ, CHUNK_DEPTH);
            
            // 初始同步加载半径设小一点
            int preloadRadius = 0; 
            for (int cx = minCx - preloadRadius; cx <= maxCx + preloadRadius; ++cx) {
                for (int cy = minCy - preloadRadius; cy <= maxCy + preloadRadius; ++cy) {
                    for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                        map.getOrLoadChunk(cx, cy, cz + zOffset);
                    }
                }
            }
        }
    }

    void TuiChunkController::markChunkModified(const ChunkCoord& coord) {
        modifiedChunks.insert(coord);
    }

    void TuiChunkController::markChunkModified(int cx, int cy, int cz) {
        modifiedChunks.insert({cx, cy, cz});
    }

    const std::unordered_set<ChunkCoord, ChunkCoordHash>& TuiChunkController::getModifiedChunks() const {
        return modifiedChunks;
    }

    void TuiChunkController::run() {
        // 关闭IO同步
        std::ios::sync_with_stdio(false);
        std::cout.tie(nullptr);

        // 启动渲染线程
        if (renderer) {
            renderer->start();
        }

        while (running) {
            handleInput();

            // 1. 批量处理已生成的区块 (集中更新，减少锁竞争)
            auto newChunks = generatorPool->getFinishedChunks();
            if (!newChunks.empty()) {
                std::lock_guard<std::mutex> lock(mapMutex);
                for (auto& chunk : newChunks) {
                    ChunkCoord coord = {chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ()};
                    
                    // 从 pending 集合中移除
                    pendingChunks.erase(coord);

                    // 再次检查是否已存在 (防止极少数情况下的冲突)
                    if (map.getChunk(coord.cx, coord.cy, coord.cz) == nullptr) {
                        map.addChunk(std::move(chunk));
                    }
                }
            }

            // 2. 将最新的视图状态同步给渲染器
            if (renderer) {
                renderer->updateViewState(viewX, viewY, currentZ, viewWidth, viewHeight, modifiedChunks.size());
            }

            // 3. 检查并请求预加载新区域 (非阻塞)
            preloadChunks();

            // 逻辑线程休眠
            #ifdef _WIN32
            Sleep(2); 
            #endif
        }
        
        // 停止渲染
        if (renderer) {
            renderer->stop();
        }
        
        clearScreen();
        showCursor();
    }

    void TuiChunkController::handleInput() {
        #ifdef _WIN32
        // WASD 移动
        if (GetAsyncKeyState('W') & 0x8000) { viewY--; }
        if (GetAsyncKeyState('S') & 0x8000) { viewY++; }
        if (GetAsyncKeyState('A') & 0x8000) { viewX--; }
        if (GetAsyncKeyState('D') & 0x8000) { viewX++; }

        // 层级切换
        bool leftPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000);
        if (leftPressed && !leftArrowPressedLastFrame) {
            currentZ--; 
        }
        leftArrowPressedLastFrame = leftPressed;

        bool rightPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        if (rightPressed && !rightArrowPressedLastFrame) {
            currentZ++;
        }
        rightArrowPressedLastFrame = rightPressed;

        // 退出
        if (GetAsyncKeyState('Q') & 0x8000) {
            running = false;
        }
        #endif
    }

    void TuiChunkController::preloadChunks() {
        // 优化后的预加载逻辑：使用线程池异步请求
        
        int minCx = floorDiv(viewX, CHUNK_WIDTH);
        int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
        int minCy = floorDiv(viewY, CHUNK_HEIGHT);
        int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
        int cz = floorDiv(currentZ, CHUNK_DEPTH);

        int preloadRadius = 1; // 可以适当增加半径，因为现在是异步的

        for (int cx = minCx - preloadRadius; cx <= maxCx + preloadRadius; ++cx) {
            for (int cy = minCy - preloadRadius; cy <= maxCy + preloadRadius; ++cy) {
                for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                    
                    int targetCz = cz + zOffset;
                    ChunkCoord coord = {cx, cy, targetCz};

                    // 1. 检查是否已在 pending 列表中
                    if (pendingChunks.find(coord) != pendingChunks.end()) {
                        continue;
                    }

                    // 2. 检查是否已加载 (需要加锁读取，但很快)
                    bool loaded = false;
                    {
                        std::lock_guard<std::mutex> lock(mapMutex);
                        if (map.getChunk(cx, cy, targetCz) != nullptr) {
                            loaded = true;
                        }
                    }

                    if (loaded) continue;

                    // 3. 发送生成请求
                    pendingChunks.insert(coord);
                    generatorPool->requestChunk(cx, cy, targetCz);
                }
            }
        }
    }

    void TuiChunkController::setupConsole() {
        #ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
        #endif
    }
    
    void TuiChunkController::clearScreen() { std::cout << "\x1b[2J\x1b[H" << std::flush; }
    void TuiChunkController::showCursor() { std::cout << "\x1b[?25h" << std::flush; }
    
}
