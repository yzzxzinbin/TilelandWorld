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
        
        // 初始预加载 (此时尚未启动渲染线程，无需加锁，可以多加载一些)
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
            // 注意：preloadChunks 内部会修改 map，所以需要加锁
            // 但为了避免长时间阻塞渲染线程，我们只在确实需要加载时才持有锁
            // 或者在 preloadChunks 内部精细化锁的粒度。
            // 这里为了简单安全，我们在调用 preloadChunks 时加锁。
            {
                // 使用 try_lock 或者直接 lock。
                // 渲染线程只在 copyMapData 时持有锁，时间很短。
                // 预加载可能会触发地形生成，时间较长。
                // 如果生成时间过长，渲染线程会卡顿。
                // 理想情况下，地形生成也应该在后台线程池中进行，这里只负责调度。
                // 鉴于目前架构，我们直接加锁。
                std::lock_guard<std::mutex> lock(mapMutex);
                preloadChunks();
            }

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
        // 注意：调用此函数前必须锁定 mapMutex
        
        int minCx = floorDiv(viewX, CHUNK_WIDTH);
        int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
        int minCy = floorDiv(viewY, CHUNK_HEIGHT);
        int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
        int cz = floorDiv(currentZ, CHUNK_DEPTH);

        int preloadRadius = 1;
        int chunksLoadedThisFrame = 0;
        const int MAX_CHUNKS_PER_FRAME = 1; // 限制每帧生成的区块数量，防止卡顿

        for (int cx = minCx - preloadRadius; cx <= maxCx + preloadRadius; ++cx) {
            for (int cy = minCy - preloadRadius; cy <= maxCy + preloadRadius; ++cy) {
                for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                     // 检查区块是否已加载，如果未加载则加载
                     // 使用 getChunk (const) 检查是否存在，避免 getOrLoadChunk 直接触发生成
                     if (map.getChunk(cx, cy, cz + zOffset) == nullptr) {
                         map.getOrLoadChunk(cx, cy, cz + zOffset);
                         chunksLoadedThisFrame++;
                         if (chunksLoadedThisFrame >= MAX_CHUNKS_PER_FRAME) {
                             return; // 达到本帧限额，释放锁，让渲染线程有机会运行
                         }
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
