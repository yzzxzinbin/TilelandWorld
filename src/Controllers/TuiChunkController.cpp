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
        // 初始化渲染器，传入 map 和 互斥锁
        renderer = std::make_unique<TuiRenderer>(map, mapMutex);
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
        
        //初始预加载 (此时尚未启动渲染线程，无需加锁，可以多加载一些)
        {
            // 临时扩大预加载范围或循环多次以确保初始画面完整
            int minCx = floorDiv(viewX, CHUNK_WIDTH);
            int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
            int minCy = floorDiv(viewY, CHUNK_HEIGHT);
            int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
            int cz = floorDiv(currentZ, CHUNK_DEPTH);
            int preloadRadius = 1;
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

            // 将最新的视图状态同步给渲染器
            if (renderer) {
                renderer->updateViewState(viewX, viewY, currentZ, viewWidth, viewHeight, modifiedChunks.size());
            }

            // 检查并预加载新区域
            // 修改：不再在外部加锁，而是让 preloadChunks 内部进行细粒度锁管理
            preloadChunks();

            // 逻辑线程休眠，避免空转占用 CPU
            #ifdef _WIN32
            Sleep(5); 
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
        // 优化后的预加载逻辑：细粒度锁 + 限制每帧生成数量
        
        int minCx = floorDiv(viewX, CHUNK_WIDTH);
        int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
        int minCy = floorDiv(viewY, CHUNK_HEIGHT);
        int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
        int cz = floorDiv(currentZ, CHUNK_DEPTH);

        int preloadRadius = 1;
        int chunksGeneratedThisFrame = 0;
        const int MAX_CHUNKS_PER_FRAME = 1; // 限制每帧生成的区块数量

        for (int cx = minCx - preloadRadius; cx <= maxCx + preloadRadius; ++cx) {
            for (int cy = minCy - preloadRadius; cy <= maxCy + preloadRadius; ++cy) {
                for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                    
                    int targetCz = cz + zOffset;
                    bool exists = false;

                    // 1. 快速检查：持有锁检查区块是否存在
                    {
                        std::lock_guard<std::mutex> lock(mapMutex);
                        if (map.getChunk(cx, cy, targetCz) != nullptr) {
                            exists = true;
                        }
                    }

                    if (exists) continue;

                    // 2. 慢速生成：释放锁后生成区块 (耗时 ~6ms，此时渲染线程可以工作)
                    auto newChunk = map.createChunkIsolated(cx, cy, targetCz);

                    // 3. 快速插入：再次持有锁将区块加入地图
                    {
                        std::lock_guard<std::mutex> lock(mapMutex);
                        // 双重检查，防止在生成期间已被其他线程添加 (虽然当前逻辑是单生成线程)
                        if (map.getChunk(cx, cy, targetCz) == nullptr) {
                            map.addChunk(std::move(newChunk));
                        }
                    }

                    chunksGeneratedThisFrame++;
                    if (chunksGeneratedThisFrame >= MAX_CHUNKS_PER_FRAME) {
                        return; // 达到本帧限额，退出
                    }
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
