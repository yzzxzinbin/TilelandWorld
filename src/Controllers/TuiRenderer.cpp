#include "TuiRenderer.h"
#include "../TerrainTypes.h"
#include "../Utils/Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <array>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "winmm.lib") // 链接多媒体库以使用 timeBeginPeriod
#endif

namespace TilelandWorld
{

    namespace {
        // 预计算 0-255 的字符串，避免循环内重复 std::to_string
        const std::string& numStr(uint8_t n)
        {
            static std::array<std::string, 256> table{};
            static bool initialized = false;
            if (!initialized)
            {
                for (int i = 0; i < 256; ++i)
                {
                    table[static_cast<size_t>(i)] = std::to_string(i);
                }
                initialized = true;
            }
            return table[n];
        }

        inline bool isNonBlack(const RGBColor& c)
        {
            return (static_cast<int>(c.r) | static_cast<int>(c.g) | static_cast<int>(c.b)) != 0;
        }

        // 使用整数混合，避免 std::round 与浮点开销；alpha 取值 0-255
        inline uint8_t blendComp(uint8_t top, uint8_t bottom, uint8_t alpha)
        {
            int a = static_cast<int>(alpha);
            return static_cast<uint8_t>((static_cast<int>(top) * a + static_cast<int>(bottom) * (255 - a) + 127) / 255);
        }

        inline std::uint64_t fnv1aHash(const std::vector<std::string>& lines, const std::string& status)
        {
            std::uint64_t h = 1469598103934665603ull;
            auto mix = [&h](const std::string& s)
            {
                for (unsigned char c : s)
                {
                    h ^= static_cast<std::uint64_t>(c);
                    h *= 1099511628211ull;
                }
                h ^= 0xFFu; // separator to reduce accidental concatenation collisions
                h *= 1099511628211ull;
            };

            for (const auto& line : lines)
            {
                mix(line);
            }
            mix(status);
            return h;
        }
    } // namespace

    TuiRenderer::TuiRenderer(const Map &mapRef, std::mutex &mutexRef, double statsAlpha, bool enableStats, bool enableDiff, double fpsLimit)
        : map(mapRef), mapMutex(mutexRef), running(false), baseStatsAlpha(statsAlpha), enableStatsOverlay(enableStats), enableDiffOutput(enableDiff), targetFpsCap(fpsLimit)
    {
        // 初始化默认视图状态
        currentViewState = {0, 0, 0, 64, 48, 0, 0.0}; // 新增 tps 初始化为 0.0
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
        // if (!SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
        // {
        //     DWORD err = GetLastError();
        //     if (err == ERROR_ACCESS_DENIED)
        //     {
        //         LOG_ERROR("需要管理员权限以设置高优先级");
        //     }
        // }else
        // {
        //     LOG_INFO("已将进程优先级设置为 HIGH_PRIORITY_CLASS");
        // }
        
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

    void TuiRenderer::updateViewState(int x, int y, int z, int w, int h, size_t modifiedCount, double tps)
    {
        std::lock_guard<std::mutex> lock(viewStateMutex);
        currentViewState.viewX = x;
        currentViewState.viewY = y;
        currentViewState.currentZ = z;
        currentViewState.width = w;
        currentViewState.height = h;
        currentViewState.modifiedChunkCount = modifiedCount;
        currentViewState.tps = tps; // 新增：设置 TPS
    }

    void TuiRenderer::applyRuntimeSettings(double statsAlpha, bool enableStats, bool enableDiff, double fpsCap)
    {
        baseStatsAlpha.store(std::clamp(statsAlpha, 0.0, 1.0));
        enableStatsOverlay.store(enableStats);
        enableDiffOutput.store(enableDiff);
        targetFpsCap.store(std::max(1.0, fpsCap));
    }

    void TuiRenderer::setBackend(RendererBackend backend)
    {
        useFmtBackend.store(backend == RendererBackend::Fmt);
    }

    void TuiRenderer::setUiLayer(std::shared_ptr<const UI::TuiSurface> layer, double alphaBg)
    {
        double clamped = std::clamp(alphaBg, 0.0, 1.0);
        std::lock_guard<std::mutex> lock(uiMutex);
        uiLayer = std::move(layer);
        uiLayerAlphaBg = clamped;
    }

    void TuiRenderer::clearUiLayer()
    {
        std::lock_guard<std::mutex> lock(uiMutex);
        uiLayer.reset();
        uiLayerAlphaBg = 0.0;
    }

    void TuiRenderer::renderLoop()
    {
#ifdef _WIN32
        // --- 在线程内部设置自身优先级 ---
        // GetCurrentThread() 返回的是当前线程的伪句柄，始终有效且权限最高
        // if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
        // {
        //     DWORD err = GetLastError();
        //     LOG_ERROR("设置渲染线程优先级失败: " + std::to_string(err));
        // }
        // else
        // {
        //     LOG_INFO("已在线程内部将渲染线程优先级设置为 THREAD_PRIORITY_ABOVE_NORMAL");
        // }

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

#endif

        size_t frameNumber = 0;

        while (running)
        {
            frameNumber++;

#ifdef _WIN32
            LARGE_INTEGER startTick, endTick;
            QueryPerformanceCounter(&startTick);
            double targetFrameTime = 1000.0 / std::max(1.0, targetFpsCap.load());
#endif

            // 1. 获取当前视图状态
            ViewState state;
            {
                std::lock_guard<std::mutex> lock(viewStateMutex);
                state = currentViewState;
            }

            // 2. 复制地图数据
            copyMapData(state);

            // 2.1 构建叠加层（默认绘制 FPS/TPS 半透明条），允许外部覆盖
            std::shared_ptr<const UI::TuiSurface> overlay;
            double overlayAlpha = 0.0;
            std::shared_ptr<const UI::TuiSurface> externalOverlay;
            {
                std::lock_guard<std::mutex> lock(uiMutex);
                externalOverlay = uiLayer;
                overlayAlpha = uiLayerAlphaBg;
            }
            std::shared_ptr<UI::TuiSurface> stats;
            if (enableStatsOverlay.load())
            {
                stats = buildStatsOverlay(state);
            }

            // 基线：stats 或 external
            if (stats)
            {
                overlay = stats;
            }
            else if (externalOverlay)
            {
                overlay = externalOverlay;
                overlayAlpha = std::max(baseStatsAlpha.load(), overlayAlpha); // fallback to avoid zero alpha
            }

            if (stats && externalOverlay)
            {
                // 合并：外部 UI 在上，文字优先保留，背景叠加
                auto merged = std::make_shared<UI::TuiSurface>(*stats);
                int w = std::min(merged->getWidth(), externalOverlay->getWidth());
                int h = std::min(merged->getHeight(), externalOverlay->getHeight());
                for (int y = 0; y < h; ++y)
                {
                    for (int x = 0; x < w; ++x)
                    {
                        const UI::TuiCell& top = externalOverlay->data()[static_cast<size_t>(y) * w + x];
                        if (!top.hasBg && (top.glyph.empty() || top.glyph == " ")) continue;

                        if (UI::TuiCell* dst = merged->editCell(x, y))
                        {
                            if (!top.glyph.empty() && top.glyph != " ")
                            {
                                dst->glyph = top.glyph;
                                dst->fg = top.fg;
                            }

                            if (top.hasBg || (!top.glyph.empty() && top.glyph != " "))
                            {
                                dst->bg = top.bg;
                                dst->hasBg = true;
                            }
                        }
                    }
                }
                overlay = merged;
                overlayAlpha = std::max(baseStatsAlpha.load(), overlayAlpha);
            }
            else if (stats)
            {
                overlayAlpha = baseStatsAlpha.load();
            }

            // 3. 渲染输出
            drawToConsole(state, overlay, overlayAlpha);

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
                        Sleep(sleepTime);
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

    void TuiRenderer::drawToConsole(const ViewState &state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha)
    {
        if (useFmtBackend.load())
        {
            drawToConsoleFmt(state, std::move(overlay), overlayAlpha);
        }
        else
        {
            drawToConsoleStd(state, std::move(overlay), overlayAlpha);
        }
    }

    void TuiRenderer::drawToConsoleStd(const ViewState &state, std::shared_ptr<const UI::TuiSurface> overlay, double overlayAlpha)
    {
        bool useOverlay = overlay && overlayAlpha > 0.0001;
        int overlayWidth = 0;
        int overlayHeight = 0;
        if (useOverlay)
        {
            overlayWidth = overlay->getWidth();
            overlayHeight = overlay->getHeight();
        }

        // 预计算混合系数（0-255 定点）
        uint8_t overlayAlphaFixed = static_cast<uint8_t>(std::clamp(overlayAlpha, 0.0, 1.0) * 255.0 + 0.5);

        std::vector<std::string> frameLines(static_cast<size_t>(state.height));

        for (int y = 0; y < state.height; ++y)
        {
            std::string line;
            line.append("\x1b[");
            line.append(std::to_string(y + 1));
            line.append(";1H");

            // 颜色状态在行级别独立，便于差分输出
            RGBColor lastFg{0, 0, 0};
            RGBColor lastBg{0, 0, 0};
            bool colorSet = false;

            // 缓存行指针，避免重复索引
            const UI::TuiCell *overlayRow = nullptr;
            if (useOverlay && y < overlayHeight)
            {
                overlayRow = &overlay->data()[static_cast<size_t>(y) * overlayWidth];
            }

            for (int x = 0; x < state.width; ++x)
            {
                const Tile &tile = tileBuffer[y * state.width + x];
                const auto &props = getTerrainProperties(tile.terrain);

                RGBColor mapFg = tile.getForegroundColor();
                RGBColor mapBg = tile.getBackgroundColor();
                std::string mapGlyph = props.displayChar.empty() ? " " : props.displayChar;

                auto emitGlyph = [&](const RGBColor &fg, const RGBColor &bg, const std::string &glyph)
                {
                    if (!colorSet || fg.r != lastFg.r || fg.g != lastFg.g || fg.b != lastFg.b || bg.r != lastBg.r || bg.g != lastBg.g || bg.b != lastBg.b)
                    {
                        line.append("\x1b[48;2;");
                        line.append(numStr(bg.r));
                        line.push_back(';');
                        line.append(numStr(bg.g));
                        line.push_back(';');
                        line.append(numStr(bg.b));
                        line.append("m\x1b[38;2;");
                        line.append(numStr(fg.r));
                        line.push_back(';');
                        line.append(numStr(fg.g));
                        line.push_back(';');
                        line.append(numStr(fg.b));
                        line.append("m");
                        colorSet = true;
                        lastFg = fg;
                        lastBg = bg;
                    }
                    line.append(glyph);
                };

                if (!props.isVisible)
                {
                    emitGlyph(mapFg, mapBg, "  ");
                    continue;
                }

                if (!useOverlay)
                {
                    emitGlyph(mapFg, mapBg, mapGlyph);
                    emitGlyph(mapFg, mapBg, mapGlyph);
                    continue;
                }

                // 检测 UI 是否对该格子有实际影响（任一槽位存在字符或非黑背景）
                bool tileHasOverlay = false;
                if (overlayRow)
                {
                    const UI::TuiCell &c1 = overlayRow[x * 2];
                    const UI::TuiCell &c2 = overlayRow[x * 2 + 1];
                    if (c1.hasBg || c2.hasBg || isNonBlack(c1.bg) || isNonBlack(c2.bg) || (!c1.glyph.empty() && c1.glyph != " ") || (!c2.glyph.empty() && c2.glyph != " "))
                    {
                        tileHasOverlay = true;
                    }
                }

                if (!tileHasOverlay)
                {
                    emitGlyph(mapFg, mapBg, mapGlyph);
                    emitGlyph(mapFg, mapBg, mapGlyph);
                    continue;
                }

                for (int slot = 0; slot < 2; ++slot)
                {
                    int uiX = x * 2 + slot;
                    RGBColor finalFg = mapFg;
                    RGBColor finalBg = mapBg;
                    std::string finalGlyph = mapGlyph;

                    if (overlayRow && uiX < overlayWidth)
                    {
                        const UI::TuiCell &cell = overlayRow[uiX];

                        // 字符覆盖：
                        // - 非空格字符总是覆盖
                        // - 若 hasBg 为真，即便是空格也要遮盖地图字符（显示空白背景）
                        if (!cell.glyph.empty() && cell.glyph != " ")
                        {
                            finalGlyph = cell.glyph;
                            finalFg = cell.fg;
                        }
                        else if (cell.hasBg)
                        {
                            finalGlyph = " ";
                            finalFg = cell.fg;
                        }

                        // 背景混合（仅当 UI 背景非黑）
                        if ((cell.hasBg || isNonBlack(cell.bg)) && overlayAlphaFixed > 0)
                        {
                            finalBg = RGBColor{
                                blendComp(cell.bg.r, mapBg.r, overlayAlphaFixed),
                                blendComp(cell.bg.g, mapBg.g, overlayAlphaFixed),
                                blendComp(cell.bg.b, mapBg.b, overlayAlphaFixed)
                            };
                        }
                    }

                    emitGlyph(finalFg, finalBg, finalGlyph);
                }
            }

            // 行末统一重置，避免跨行颜色污染
            line.append("\x1b[0m");
            frameLines[static_cast<size_t>(y)] = std::move(line);
        }

        std::string statusLine; // now empty: renderer no longer emits bottom status text

        if (enableDiffOutput.load())
        {
            std::uint64_t frameHash = fnv1aHash(frameLines, statusLine);
            if (frameHash == lastFrameHash)
            {
                return; // 完全相同的帧，跳过输出
            }

            drawDiffToConsoleStd(frameLines, statusLine);
            lastFrameHash = frameHash;
            return;
        }

        static std::string outputBuffer;
        outputBuffer.clear();
        size_t estimatedSize = state.width * state.height * 70 + 256;
        if (outputBuffer.capacity() < estimatedSize)
        {
            outputBuffer.reserve(estimatedSize);
        }

        outputBuffer.append("\x1b[?25l");
        for (const auto &line : frameLines)
        {
            outputBuffer.append(line);
        }

        std::cout.write(outputBuffer.data(), outputBuffer.size());
        std::cout.flush();
    }

    void TuiRenderer::drawDiffToConsoleStd(const std::vector<std::string> &lines, const std::string &statusLine)
    {
        bool sizeChanged = lastFrameLines.size() != lines.size();
        if (sizeChanged)
        {
            lastFrameLines.assign(lines.size(), "");
        }

        std::string diffOutput;
        diffOutput.reserve(lines.size() * 32);
        diffOutput.append("\x1b[?25l");

        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (sizeChanged || lines[i] != lastFrameLines[i])
            {
                diffOutput.append(lines[i]);
            }
        }

        std::cout.write(diffOutput.data(), diffOutput.size());
        std::cout.flush();

        lastFrameLines = lines;
        lastStatusLine = statusLine;
    }

    RGBColor TuiRenderer::blendColor(const RGBColor &top, const RGBColor &bottom, double alpha)
    {
        double a = std::clamp(alpha, 0.0, 1.0);
        uint8_t af = static_cast<uint8_t>(a * 255.0 + 0.5);
        return RGBColor{
            blendComp(top.r, bottom.r, af),
            blendComp(top.g, bottom.g, af),
            blendComp(top.b, bottom.b, af)
        };
    }

    std::shared_ptr<UI::TuiSurface> TuiRenderer::buildStatsOverlay(const ViewState &state) const
    {
        // UI 层宽度设为地图宽度的两倍，以支持单字符精度的文本显示
        auto surface = std::make_shared<UI::TuiSurface>(state.width * 2, state.height);
        RGBColor bg{10, 60, 160};
        RGBColor fg{230, 240, 255};
        int barHeight = 1;

        std::string fpsStr = std::to_string(currentFps);
        fpsStr = fpsStr.substr(0, fpsStr.find('.') + 2);
        std::string tpsStr = std::to_string(state.tps);
        tpsStr = tpsStr.substr(0, tpsStr.find('.') + 2);

        std::string text = "Pos: (" + std::to_string(state.viewX) + ", " + std::to_string(state.viewY) + ", " + std::to_string(state.currentZ) + ") | "
            "FPS: " + fpsStr + " | TPS: " + tpsStr + " | Modified: " + std::to_string(state.modifiedChunkCount);
        // 仅填充与文本长度相匹配的区域，避免整行覆盖
        int barWidth = std::min(static_cast<int>(surface->getWidth()), static_cast<int>(text.size()) + 4);
        surface->fillRect(0, 0, barWidth, barHeight, fg, bg, " ");
        surface->drawText(1, 0, text, fg, bg);
        return surface;
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
            return "  ";

        RGBColor fg = tile.getForegroundColor();
        RGBColor bg = tile.getBackgroundColor();

        // 手动拼接，虽然这里慢一点，但只执行一次
        std::string res;
        res.reserve(40);
        res += "\x1b[48;2;";
        res += std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
        res += "\x1b[38;2;";
        res += std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
        res += props.displayChar + props.displayChar;

        return res;
    }

}
