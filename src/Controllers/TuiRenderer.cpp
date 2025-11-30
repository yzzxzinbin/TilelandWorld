#include "TuiRenderer.h"
#include "../TerrainTypes.h"
#include "../Utils/Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "winmm.lib") // 链接多媒体库以使用 timeBeginPeriod
#endif

namespace TilelandWorld {

    TuiRenderer::TuiRenderer(Map& mapRef, std::mutex& mutexRef) 
        : map(mapRef), mapMutex(mutexRef), running(false) {
        // 初始化默认视图状态
        currentViewState = {0, 0, 0, 64, 48, 0};
        std::ios::sync_with_stdio(false); // 关闭同步以提高性能

    }

    TuiRenderer::~TuiRenderer() {
        stop();
    }

    void TuiRenderer::start() {
        if (running) return;
        running = true;
        renderThread = std::thread(&TuiRenderer::renderLoop, this);
    }

    void TuiRenderer::stop() {
        if (!running) return;
        running = false;
        if (renderThread.joinable()) {
            renderThread.join();
        }
    }

    void TuiRenderer::updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount) {
        std::lock_guard<std::mutex> lock(viewStateMutex);
        currentViewState.viewX = x;
        currentViewState.viewY = y;
        currentViewState.currentZ = z;
        currentViewState.width = w;
        currentViewState.height = h;
        currentViewState.modifiedChunkCount = modifiedCount;
    }

    void TuiRenderer::renderLoop() {
        // --- Windows 高精度计时器初始化 ---
        #ifdef _WIN32
        TIMECAPS tc;
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
            timeBeginPeriod(tc.wPeriodMin);
        }

        LARGE_INTEGER freqLI;
        QueryPerformanceFrequency(&freqLI);
        frequency = freqLI.QuadPart;
        double pcFreq = double(frequency) / 1000.0; // 毫秒频率

        LARGE_INTEGER nowLI;
        QueryPerformanceCounter(&nowLI);
        lastFpsTime = nowLI.QuadPart;

        const int targetFPS = 480;
        const double targetFrameTime = 1000.0 / targetFPS; // 毫秒
        #endif

        while (running) {
            #ifdef _WIN32
            LARGE_INTEGER startTick, endTick;
            QueryPerformanceCounter(&startTick);
            #endif

            // 1. 获取当前视图状态
            ViewState state;
            {
                std::lock_guard<std::mutex> lock(viewStateMutex);
                state = currentViewState;
            }

            // 2. 复制地图数据 (持有 Map 锁的时间应尽可能短)
            copyMapData(state);

            // 3. 渲染输出 (耗时操作，但已不占用 Map 锁)
            drawToConsole(state);

            // --- FPS Calculation ---
            #ifdef _WIN32
            frameCount++;
            QueryPerformanceCounter(&nowLI);
            long long now = nowLI.QuadPart;
            double elapsedSeconds = (double)(now - lastFpsTime) / frequency;
            if (elapsedSeconds >= 1.0) {
                currentFps = frameCount / elapsedSeconds;
                frameCount = 0;
                lastFpsTime = now;
            }
            #endif

            // 4. 帧率控制
            #ifdef _WIN32
            QueryPerformanceCounter(&endTick);
            double elapsedMs = (endTick.QuadPart - startTick.QuadPart) / pcFreq;
            
            if (elapsedMs < targetFrameTime) {
                DWORD sleepTime = static_cast<DWORD>(targetFrameTime - elapsedMs);
                if (sleepTime > 0) {
                    Sleep(sleepTime);
                }
            }
            #else
            // 非 Windows 平台的简单回退
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            #endif
        }

        #ifdef _WIN32
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
            timeEndPeriod(tc.wPeriodMin);
        }
        #endif
    }

    void TuiRenderer::copyMapData(const ViewState& state) {
        size_t requiredSize = state.width * state.height;
        if (tileBuffer.size() != requiredSize) {
            tileBuffer.resize(requiredSize);
        }

        // *** 关键：锁定 Map，快速复制 ***
        std::lock_guard<std::mutex> lock(mapMutex);
        
        for (int y = 0; y < state.height; ++y) {
            for (int x = 0; x < state.width; ++x) {
                int wx = state.viewX + x;
                int wy = state.viewY + y;
                try {
                    // 这里我们复制 Tile 对象的值
                    tileBuffer[y * state.width + x] = map.getTile(wx, wy, state.currentZ);
                } catch (...) {
                    // 如果越界或未加载，填充一个默认的空 Tile
                    tileBuffer[y * state.width + x] = Tile(TerrainType::VOIDBLOCK);
                }
            }
        }
    }

    void TuiRenderer::drawToConsole(const ViewState& state) {
        std::stringstream frameBuffer;
        
        // 隐藏光标并重置位置
        frameBuffer << "\x1b[?25l\x1b[H"; 

        // 绘制地图区域
        for (int y = 0; y < state.height; ++y) {
            frameBuffer << "\x1b[" << (y + 1) << ";1H"; // 移动光标到行首
            for (int x = 0; x < state.width; ++x) {
                const Tile& tile = tileBuffer[y * state.width + x];
                frameBuffer << formatTileForTerminal(tile);
            }
        }

        // 绘制信息栏
        frameBuffer << "\x1b[" << (state.height + 2) << ";1H";
        frameBuffer << "\x1b[K"; // 清除行
        frameBuffer << "Pos: (" << state.viewX << ", " << state.viewY << ", " << state.currentZ << ")"
                    << " | Modified: " << state.modifiedChunkCount
                    << " | FPS: " << std::fixed << std::setprecision(1) << currentFps ;

        // 一次性输出到控制台
        std::cout << frameBuffer.str() << std::flush;
    }

    std::string TuiRenderer::formatTileForTerminal(const Tile& tile) {
        const auto& props = getTerrainProperties(tile.terrain);
        if (!props.isVisible) return "  \x1b[0m";
        
        RGBColor fg = tile.getForegroundColor();
        RGBColor bg = tile.getBackgroundColor();
        
        std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
        std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
        
        return bgCode + fgCode + props.displayChar + props.displayChar + "\x1b[0m";
    }

}
