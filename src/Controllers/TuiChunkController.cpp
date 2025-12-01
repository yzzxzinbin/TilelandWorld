#include "TuiChunkController.h"
#include "../Constants.h"
#include "../Utils/Logger.h"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib") // 确保链接 winmm
#endif

namespace TilelandWorld {

    TuiChunkController::TuiChunkController(Map& mapRef) : map(mapRef) {
        // 1. 初始化通用任务系统
        taskSystem = std::make_unique<TaskSystem>(); // 默认使用 (核心数-1) 个线程

        // 2. 初始化区块生成池，传入任务系统
        generatorPool = std::make_unique<ChunkGeneratorPool>(map, *taskSystem);

        // 3. 初始化渲染器
        renderer = std::make_unique<TuiRenderer>(map, mapMutex);
    }

    TuiChunkController::~TuiChunkController() {
        // 析构顺序很重要：
        // 1. 停止渲染器
        if (renderer) renderer->stop();
        
        // 2. 停止任务系统 (确保没有工作线程在访问 generatorPool 或 map)
        if (taskSystem) taskSystem->stop();
        
        // 3. 之后 generatorPool 和 map 可以安全析构
        showCursor();
    }

    void TuiChunkController::initialize() {
        setupConsole();
        std::cout << "\x1b[?25l" << std::flush;
        
        // 初始同步预加载
        {
            int minCx = floorDiv(viewX, CHUNK_WIDTH);
            int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
            int minCy = floorDiv(viewY, CHUNK_HEIGHT);
            int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
            int cz = floorDiv(currentZ, CHUNK_DEPTH);
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
        std::ios::sync_with_stdio(false);
        std::cout.tie(nullptr);

        if (renderer) renderer->start();

        // --- TPS 控制初始化 ---
        #ifdef _WIN32
        TIMECAPS tc;
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
            timeBeginPeriod(tc.wPeriodMin);
        }
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        double pcFreq = double(frequency.QuadPart) / 1000.0; // ms
        double targetFrameTime = 1000.0 / targetTps;

        // 新增：TPS 计算初始化
        LARGE_INTEGER tpsFreqLI;
        QueryPerformanceFrequency(&tpsFreqLI);
        tpsFrequency = tpsFreqLI.QuadPart;
        LARGE_INTEGER nowLI;
        QueryPerformanceCounter(&nowLI);
        lastTpsTime = nowLI.QuadPart;
        #endif

        while (running) {
            #ifdef _WIN32
            LARGE_INTEGER startTick;
            QueryPerformanceCounter(&startTick);
            #endif

            // --- 1. 逻辑更新开始 ---
            handleInput();

            // 批量处理已生成的区块
            auto newChunks = generatorPool->getFinishedChunks();
            if (!newChunks.empty()) {
                std::lock_guard<std::mutex> lock(mapMutex);
                for (auto& chunk : newChunks) {
                    ChunkCoord coord = {chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ()};
                    pendingChunks.erase(coord);
                    // 再次检查，防止覆盖
                    if (map.getChunk(coord.cx, coord.cy, coord.cz) == nullptr) {
                        map.addChunk(std::move(chunk));
                    }
                }
            }

            // 同步渲染器
            if (renderer) {
                renderer->updateViewState(viewX, viewY, currentZ, viewWidth, viewHeight, modifiedChunks.size(), currentTps); // 传递 currentTps
            }

            // 请求预加载
            preloadChunks();
            // --- 逻辑更新结束 ---

            // --- TPS 休眠控制 ---
            #ifdef _WIN32
            LARGE_INTEGER endTick;
            QueryPerformanceCounter(&endTick);
            double elapsedMs = (endTick.QuadPart - startTick.QuadPart) / pcFreq;
            
            if (elapsedMs < targetFrameTime) {
                DWORD sleepTime = static_cast<DWORD>(targetFrameTime - elapsedMs);
                // 保持最小睡眠时间为 1ms 以让渡 CPU，除非时间非常紧迫
                if (sleepTime > 0) {
                    Sleep(sleepTime); 
                }
            }

            // 新增：TPS 计算逻辑
            tickCount++;
            QueryPerformanceCounter(&nowLI);
            long long now = nowLI.QuadPart;
            double elapsedSeconds = (double)(now - lastTpsTime) / tpsFrequency;
            if (elapsedSeconds >= 1.0) {
                currentTps = tickCount / elapsedSeconds;
                tickCount = 0;
                lastTpsTime = now;
            }
            #else
            // 非 Windows 简单回退
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            #endif
        }
        
        if (renderer) renderer->stop();
        if (taskSystem) taskSystem->stop(); 
        
        clearScreen();
        showCursor();

        #ifdef _WIN32
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
            timeEndPeriod(tc.wPeriodMin);
        }
        #endif
    }

    void TuiChunkController::handleInput() {
        #ifdef _WIN32
        if (GetAsyncKeyState('W') & 0x8000) { viewY--; }
        if (GetAsyncKeyState('S') & 0x8000) { viewY++; }
        if (GetAsyncKeyState('A') & 0x8000) { viewX--; }
        if (GetAsyncKeyState('D') & 0x8000) { viewX++; }

        bool leftPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000);
        if (leftPressed && !leftArrowPressedLastFrame) { currentZ--; }
        leftArrowPressedLastFrame = leftPressed;

        bool rightPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        if (rightPressed && !rightArrowPressedLastFrame) { currentZ++; }
        rightArrowPressedLastFrame = rightPressed;

        if (GetAsyncKeyState('Q') & 0x8000) { running = false; }
        #endif
    }

    void TuiChunkController::preloadChunks() {
        int minCx = floorDiv(viewX, CHUNK_WIDTH);
        int maxCx = floorDiv(viewX + viewWidth, CHUNK_WIDTH);
        int minCy = floorDiv(viewY, CHUNK_HEIGHT);
        int maxCy = floorDiv(viewY + viewHeight, CHUNK_HEIGHT);
        int cz = floorDiv(currentZ, CHUNK_DEPTH);

        int preloadRadius = 1;

        for (int cx = minCx - preloadRadius; cx <= maxCx + preloadRadius; ++cx) {
            for (int cy = minCy - preloadRadius; cy <= maxCy + preloadRadius; ++cy) {
                for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                    int targetCz = cz + zOffset;
                    ChunkCoord coord = {cx, cy, targetCz};

                    if (pendingChunks.find(coord) != pendingChunks.end()) continue;

                    bool loaded = false;
                    {
                        std::lock_guard<std::mutex> lock(mapMutex);
                        if (map.getChunk(cx, cy, targetCz) != nullptr) loaded = true;
                    }

                    if (loaded) continue;

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
