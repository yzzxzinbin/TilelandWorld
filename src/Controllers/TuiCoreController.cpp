#include "TuiCoreController.h"
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

    TuiCoreController::TuiCoreController(Map& mapRef, const Settings& cfg) : map(mapRef), settings(cfg) {
        // 确保地形生成器与存档元数据一致。
        map.setTerrainGenerator(createTerrainGeneratorFromMetadata(map.getWorldMetadata()));

        // 1. 初始化通用任务系统
        taskSystem = std::make_unique<TaskSystem>(); // 默认使用 (核心数-1) 个线程

        // 2. 初始化区块生成池，传入任务系统
        generatorPool = std::make_unique<ChunkGeneratorPool>(map, *taskSystem);

        // 3. 初始化渲染器
        renderer = std::make_unique<TuiRenderer>(map, mapMutex, settings.statsOverlayAlpha, settings.enableStatsOverlay);

        // 4. 初始化输入控制器
        inputController = std::make_unique<InputController>();

        // 视图尺寸与 TPS 从设置读取
        viewWidth = settings.viewWidth;
        viewHeight = settings.viewHeight;
        targetTps = settings.targetTps;
    }

    TuiCoreController::~TuiCoreController() {
        // 析构顺序很重要：
        // 1. 停止渲染器
        if (renderer) renderer->stop();

        // 1.5 停止输入控制器
        if (inputController) inputController->stop();
        
        // 2. 停止任务系统 (确保没有工作线程在访问 generatorPool 或 map)
        if (taskSystem) taskSystem->stop();
        
        // 3. 之后 generatorPool 和 map 可以安全析构
        showCursor();
    }

    void TuiCoreController::initialize() {
        setupConsole();
        std::cout << "\x1b[?25l" << std::flush;

        if (inputController) inputController->start();
        
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

    void TuiCoreController::markChunkModified(const ChunkCoord& coord) {
        modifiedChunks.insert(coord);
    }

    void TuiCoreController::markChunkModified(int cx, int cy, int cz) {
        modifiedChunks.insert({cx, cy, cz});
    }

    const std::unordered_set<ChunkCoord, ChunkCoordHash>& TuiCoreController::getModifiedChunks() const {
        return modifiedChunks;
    }

    void TuiCoreController::run() {
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

    void TuiCoreController::handleInput() {
        if (!inputController) return;

        // 高频按键（WASD/Q/Esc/左右层级切换）在 Windows 下用 GetAsyncKeyState 提升长按采样率
#ifdef _WIN32
        auto keyDown = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

        if (keyDown('W')) viewY--;
        if (keyDown('S')) viewY++;
        if (keyDown('A')) viewX--;
        if (keyDown('D')) viewX++;

        // 使用左右箭头快速切换楼层
        if (keyDown(VK_LEFT)) currentZ--;
        if (keyDown(VK_RIGHT)) currentZ++;

        if (keyDown('Q') || keyDown(VK_ESCAPE)) running = false;
#endif

        auto events = inputController->pollEvents();
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
#ifdef _WIN32
                // Windows 下这些字符已用异步采样处理，避免重复
                if (ev.key == InputKey::Character) {
                    char c = static_cast<char>(ev.ch);
                    if (c == 'w' || c == 'W' || c == 's' || c == 'S' || c == 'a' || c == 'A' || c == 'd' || c == 'D' || c == 'q' || c == 'Q') {
                        continue;
                    }
                }
#endif
                // 字符键
                if (ev.key == InputKey::Character) {
                    if (ev.ch == 'w' || ev.ch == 'W') viewY--;
                    else if (ev.ch == 's' || ev.ch == 'S') viewY++;
                    else if (ev.ch == 'a' || ev.ch == 'A') viewX--;
                    else if (ev.ch == 'd' || ev.ch == 'D') viewX++;
                    else if (ev.ch == 'q' || ev.ch == 'Q') running = false;
                }

                // 特殊键
                if (ev.key == InputKey::ArrowUp) viewY--;
                else if (ev.key == InputKey::ArrowDown) viewY++;
                else if (ev.key == InputKey::ArrowLeft) { currentZ--; }
                else if (ev.key == InputKey::ArrowRight) { currentZ++; }
                else if (ev.key == InputKey::Escape) { running = false; }
            }
            else if (ev.type == InputEvent::Type::Mouse) {
                if (!settings.enableMouseCross) continue;
                mouseScreenX = ev.x;
                mouseScreenY = ev.y;
                rebuildMouseOverlay();
            }
        }
    }

    void TuiCoreController::rebuildMouseOverlay() {
        if (!settings.enableMouseCross) {
            mouseOverlay.reset();
            if (renderer) renderer->clearUiLayer();
            return;
        }

        int overlayW = viewWidth * 2;
        int overlayH = viewHeight;

        // 如果鼠标不在当前视图范围内，恢复默认 UI（例如统计条）
        if (mouseScreenX < 0 || mouseScreenY < 0 || mouseScreenX >= overlayW || mouseScreenY >= overlayH) {
            mouseOverlay.reset();
            if (renderer) renderer->clearUiLayer();
            return;
        }

        auto surface = std::make_shared<UI::TuiSurface>(overlayW, overlayH);
        RGBColor white{255, 255, 255};

        int tileX = mouseScreenX / 2;
        int tileY = mouseScreenY;

        // 横线
        surface->fillRect(0, tileY, overlayW, 1, white, white, " ");
        // 竖线（对应 tile 的两个列槽位）
        surface->fillRect(tileX * 2, 0, 2, overlayH, white, white, " ");

        mouseOverlay = surface;
        if (renderer) renderer->setUiLayer(mouseOverlay, settings.mouseCrossAlpha);
    }

    void TuiCoreController::preloadChunks() {
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
    
    void TuiCoreController::setupConsole() {
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
    
    void TuiCoreController::clearScreen() { std::cout << "\x1b[2J\x1b[H" << std::flush; }
    void TuiCoreController::showCursor() { std::cout << "\x1b[?25h" << std::flush; }

}
