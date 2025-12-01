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

namespace TilelandWorld
{

    TuiRenderer::TuiRenderer(const Map &mapRef, std::mutex &mutexRef)
        : map(mapRef), mapMutex(mutexRef), running(false)
    {
        // 初始化默认视图状态
        currentViewState = {0, 0, 0, 64, 48, 0};
        std::ios::sync_with_stdio(false); // 关闭同步以提高性能

        // 预留一定的缓存空间，避免频繁 resize
        // 假设 TerrainType 不会超过 20 种
        renderCache.resize(20);
    }

    TuiRenderer::~TuiRenderer()
    {
        stop();
    }

    void TuiRenderer::start()
    {
        if (running)
            return;
        running = true;
        renderThread = std::thread(&TuiRenderer::renderLoop, this);

// 尝试提高进程优先级
#ifdef _WIN32
        HANDLE hProcess = GetCurrentProcess();
        // 注意：REALTIME_PRIORITY_CLASS 极其危险，可能导致鼠标键盘无响应。
        // 建议使用 HIGH_PRIORITY_CLASS 用于测试。
        if (!SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
        {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED)
            {
                LOG_ERROR("需要管理员权限以设置高优先级");
            }
        }else
        {
            LOG_INFO("已将进程优先级设置为 HIGH_PRIORITY_CLASS");
        }
        
        // 移除此处的 SetThreadPriority，移至 renderLoop 内部
#endif
    }

    void TuiRenderer::stop()
    {
        if (!running)
            return;
        running = false;
        if (renderThread.joinable())
        {
            renderThread.join();
        }
    }

    void TuiRenderer::updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount)
    {
        std::lock_guard<std::mutex> lock(viewStateMutex);
        currentViewState.viewX = x;
        currentViewState.viewY = y;
        currentViewState.currentZ = z;
        currentViewState.width = w;
        currentViewState.height = h;
        currentViewState.modifiedChunkCount = modifiedCount;
    }

    void TuiRenderer::renderLoop()
    {
#ifdef _WIN32
        // --- 在线程内部设置自身优先级 ---
        // GetCurrentThread() 返回的是当前线程的伪句柄，始终有效且权限最高
        if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
        {
            DWORD err = GetLastError();
            LOG_ERROR("设置渲染线程优先级失败: " + std::to_string(err));
        }
        else
        {
            LOG_INFO("已在线程内部将渲染线程优先级设置为 THREAD_PRIORITY_HIGHEST");
        }

        TIMECAPS tc;
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
        {
            timeBeginPeriod(tc.wPeriodMin);
        }

        LARGE_INTEGER freqLI;
        QueryPerformanceFrequency(&freqLI);
        frequency = freqLI.QuadPart;
        double pcFreq = double(frequency) / 1000.0; // 毫秒频率

        LARGE_INTEGER nowLI;
        QueryPerformanceCounter(&nowLI);
        lastFpsTime = nowLI.QuadPart;

        const int targetFPS = 360;
        const double targetFrameTime = 1000.0 / targetFPS; // 毫秒
#endif

        size_t frameNumber = 0;

        while (running)
        {
            frameNumber++;

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

            // 2. 复制地图数据
            copyMapData(state);

            // 3. 渲染输出
            drawToConsole(state);

// --- FPS Calculation ---
#ifdef _WIN32
            frameCount++;
            QueryPerformanceCounter(&nowLI);
            long long now = nowLI.QuadPart;
            double elapsedSeconds = (double)(now - lastFpsTime) / frequency;
            if (elapsedSeconds >= 1.0)
            {
                currentFps = frameCount / elapsedSeconds;
                frameCount = 0;
                lastFpsTime = now;
            }
#endif

// 4. 帧率控制
#ifdef _WIN32
            double elapsedMs = 0.0;
            while (elapsedMs < targetFrameTime)
            {
                QueryPerformanceCounter(&endTick);
                double elapsedTick = (endTick.QuadPart - startTick.QuadPart);
                elapsedMs = (elapsedTick) / pcFreq;

                if (elapsedMs < targetFrameTime)
                {
                    DWORD sleepTime = static_cast<DWORD>(targetFrameTime - elapsedMs);
                    if (sleepTime > 0)
                    {
                        Sleep(0);
                    }
                }
                else
                {
                    break;
                }
            }
            // 仅在超时严重时记录日志，避免日志刷屏影响性能
            if (elapsedMs > targetFrameTime + 1.0)
            {
                LOG_WARNING("Frame " + std::to_string(frameNumber) + " lag: " + std::to_string(elapsedMs) + " ms");
            }
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
#endif
        }

#ifdef _WIN32
        if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
        {
            timeEndPeriod(tc.wPeriodMin);
        }
#endif
    }

    void TuiRenderer::copyMapData(const ViewState &state)
    {
        size_t requiredSize = state.width * state.height;
        if (tileBuffer.size() != requiredSize)
        {
            tileBuffer.resize(requiredSize);
        }

        // *** 关键：锁定 Map，快速复制 ***
        std::lock_guard<std::mutex> lock(mapMutex);

        for (int y = 0; y < state.height; ++y)
        {
            for (int x = 0; x < state.width; ++x)
            {
                int wx = state.viewX + x;
                int wy = state.viewY + y;
                try
                {
                    tileBuffer[y * state.width + x] = map.getTile(wx, wy, state.currentZ);
                }
                catch (...)
                {
                    tileBuffer[y * state.width + x] = Tile(TerrainType::VOIDBLOCK);
                }
            }
        }
    }

    void TuiRenderer::drawToConsole(const ViewState &state)
    {
        // 优化：使用预分配的 std::string 而不是 stringstream
        // 估算大小：每个 Tile 约 40 字节 ANSI 码 + 屏幕控制码
        static std::string outputBuffer;
        outputBuffer.clear();
        size_t estimatedSize = state.width * state.height * 45 + 100;
        if (outputBuffer.capacity() < estimatedSize)
        {
            outputBuffer.reserve(estimatedSize);
        }

        // 隐藏光标并重置位置
        outputBuffer.append("\x1b[?25l\x1b[H");

        // 绘制地图区域
        for (int y = 0; y < state.height; ++y)
        {
            // 移动光标到行首: CSI <n> ; 1 H
            outputBuffer.append("\x1b[");
            outputBuffer.append(std::to_string(y + 1));
            outputBuffer.append(";1H");

            for (int x = 0; x < state.width; ++x)
            {
                const Tile &tile = tileBuffer[y * state.width + x];
                // 使用缓存获取字符串，避免重复计算和分配
                outputBuffer.append(getCachedTileString(tile));
            }
        }

        // 绘制信息栏
        outputBuffer.append("\x1b[");
        outputBuffer.append(std::to_string(state.height + 2));
        outputBuffer.append(";1H\x1b[K"); // 移动并清除行

        outputBuffer.append("Pos: (");
        outputBuffer.append(std::to_string(state.viewX));
        outputBuffer.append(", ");
        outputBuffer.append(std::to_string(state.viewY));
        outputBuffer.append(", ");
        outputBuffer.append(std::to_string(state.currentZ));
        outputBuffer.append(") | Modified: ");
        outputBuffer.append(std::to_string(state.modifiedChunkCount));
        outputBuffer.append(" | FPS: ");

        // 简单的 float 转 string，避免 stringstream
        std::string fpsStr = std::to_string(currentFps);
        outputBuffer.append(fpsStr.substr(0, fpsStr.find('.') + 2)); // 保留一位小数

        // 一次性输出到控制台，使用 write 避免格式化开销
        std::cout.write(outputBuffer.data(), outputBuffer.size());
        std::cout.flush();
    }

    // 核心优化：带缓存的字符串获取
    const std::string &TuiRenderer::getCachedTileString(const Tile &tile)
    {
        size_t typeIndex = static_cast<size_t>(tile.terrain);

        // 确保第一维足够大
        if (typeIndex >= renderCache.size())
        {
            renderCache.resize(typeIndex + 1);
        }

        std::vector<std::string> &lightLevels = renderCache[typeIndex];

        // 如果该地形类型的缓存未初始化 (大小不为 256)，则进行初始化
        if (lightLevels.size() != 256)
        {
            lightLevels.resize(256);
            // 预计算该地形在所有光照等级下的字符串
            for (int i = 0; i < 256; ++i)
            {
                Tile tempTile = tile;
                tempTile.lightLevel = static_cast<uint8_t>(i);
                lightLevels[i] = generateTileString(tempTile);
            }
        }

        return lightLevels[tile.lightLevel];
    }

    // 原始的生成逻辑，仅在缓存未命中时调用
    std::string TuiRenderer::generateTileString(const Tile &tile)
    {
        const auto &props = getTerrainProperties(tile.terrain);
        if (!props.isVisible)
            return "  \x1b[0m";

        RGBColor fg = tile.getForegroundColor();
        RGBColor bg = tile.getBackgroundColor();

        // 手动拼接，虽然这里慢一点，但只执行一次
        std::string res;
        res.reserve(40);
        res += "\x1b[48;2;";
        res += std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
        res += "\x1b[38;2;";
        res += std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
        res += props.displayChar + props.displayChar + "\x1b[0m";

        return res;
    }

}
