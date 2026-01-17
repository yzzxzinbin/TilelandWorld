#include "TuiCoreController.h"
#include "../Constants.h"
#include "../Utils/Logger.h"
#include "../UI/TuiUtils.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib") // 确保链接 winmm
#endif

namespace TilelandWorld {

    namespace {
        double clampDouble(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
        int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

        std::string formatFixed(double v, int digits = 2) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(digits) << v;
            return oss.str();
        }
    }

    TuiCoreController::TuiCoreController(Map& mapRef, const Settings& cfg) : map(mapRef), settings(cfg) {
        // 确保地形生成器与存档元数据一致。
        map.setTerrainGenerator(createTerrainGeneratorFromMetadata(map.getWorldMetadata()));

        // 1. 初始化通用任务系统
        taskSystem = std::make_unique<TaskSystem>(); // 默认使用 (核心数-1) 个线程

        // 2. 初始化区块生成池，传入任务系统
        generatorPool = std::make_unique<ChunkGeneratorPool>(map, *taskSystem);

        // 3. 初始化渲染器
        renderer = std::make_unique<TuiRenderer>(map, mapMutex, settings.statsOverlayAlpha, settings.enableStatsOverlay, settings.enableDiffRendering, settings.targetFpsLimit);
        renderer->setBackend(settings.useFmtRenderer ? RendererBackend::Fmt : RendererBackend::Std);

        // 4. 初始化输入控制器
        inputController = std::make_unique<InputController>();

        // 视图尺寸与 TPS 从设置读取
        viewWidth = settings.viewWidth;
        viewHeight = settings.viewHeight;
        refreshAutoViewSize();
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

        refreshAutoViewSize();

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

        // TPS 计算初始化
        LARGE_INTEGER tpsFreqLI;
        QueryPerformanceFrequency(&tpsFreqLI);
        tpsFrequency = tpsFreqLI.QuadPart;
        LARGE_INTEGER nowLI;
        QueryPerformanceCounter(&nowLI);
        lastTpsTime = nowLI.QuadPart;
        #endif

        #ifdef _WIN32
        LARGE_INTEGER nextFrameTick;
        QueryPerformanceCounter(&nextFrameTick);
        #endif

        while (running) {
            #ifdef _WIN32
            long long ticksPerFrame = static_cast<long long>(tpsFrequency / std::max(1.0, targetTps));
            // 预测本帧理想的结束时间点
            long long deadlineTick = nextFrameTick.QuadPart + ticksPerFrame;
            #endif

            refreshAutoViewSize();

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
            LARGE_INTEGER currentTick;
            
            // 1. 粗放休眠阶段
            while (true) {
                QueryPerformanceCounter(&currentTick);
                double remainingMs = (double)(deadlineTick - currentTick.QuadPart) / pcFreq;
                
                if (remainingMs <= 1.5) break; 

                if (remainingMs > 2) {
                    Sleep(1); 
                } else {
                    std::this_thread::yield();
                }
            }

            // 2. 精确忙等阶段
            while (true) {
                QueryPerformanceCounter(&currentTick);
                if (currentTick.QuadPart >= deadlineTick) break;
                YieldProcessor(); 
            }

            // 下一帧的起点严格对齐本帧的终点
            nextFrameTick.QuadPart = deadlineTick;

            // 防加速保护
            if (currentTick.QuadPart > deadlineTick + ticksPerFrame) {
                nextFrameTick = currentTick;
            }

            // TPS 统计
            tickCount++;
            long long now = currentTick.QuadPart;
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
    if (!settingsOverlayActive) {
        auto keyDown = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

        if (keyDown('W')) viewY--;
        if (keyDown('S')) viewY++;
        if (keyDown('A')) viewX--;
        if (keyDown('D')) viewX++;

        // 使用左右箭头快速切换楼层
        if (keyDown(VK_LEFT)) currentZ--;
        if (keyDown(VK_RIGHT)) currentZ++;

        if (keyDown('Q') || keyDown(VK_ESCAPE)) running = false;
    }
#endif

        auto events = inputController->pollEvents();
        if (settingsOverlayActive) {
            for (const auto& ev : events) {
                if (ev.type == InputEvent::Type::Key) {
                    handleSettingsOverlayKey(ev);
                }
            }
            return;
        }

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
                    else if (ev.ch == 'i' || ev.ch == 'I') { toggleInGameSettings(); if (settingsOverlayActive) return; }
                }

                // 特殊键
                if (ev.key == InputKey::ArrowUp) viewY--;
                else if (ev.key == InputKey::ArrowDown) viewY++;
                else if (ev.key == InputKey::ArrowLeft) { currentZ--; }
                else if (ev.key == InputKey::ArrowRight) { currentZ++; }
                else if (ev.key == InputKey::Escape) { running = false; }
            }
            else if (ev.type == InputEvent::Type::Mouse) {
                if (!settings.enableMouseCross || settingsOverlayActive) continue;
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

        if (settingsOverlayActive) {
            return; // UI 叠加优先
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
        pushActiveOverlay();
    }

    void TuiCoreController::pushActiveOverlay() {
        if (!renderer) return;

        constexpr double kUiOverlayAlpha = 0.10; // 与状态栏一致的 10% 透明度
        if (settingsOverlayActive && settingsOverlaySurface) {
            renderer->setUiLayer(settingsOverlaySurface, kUiOverlayAlpha);
            return;
        }

        if (settings.enableMouseCross && mouseOverlay) {
            renderer->setUiLayer(mouseOverlay, settings.mouseCrossAlpha);
        } else {
            renderer->clearUiLayer();
        }
    }

    void TuiCoreController::toggleInGameSettings() {
        if (settingsOverlayActive) {
            closeInGameSettings();
        } else {
            openInGameSettings();
        }
    }

    void TuiCoreController::openInGameSettings() {
        settingsOverlayWorking = settings;
        settingsOverlaySelected = 0;
        buildSettingsOverlayItems();
        settingsOverlayActive = true;
        rebuildSettingsOverlay();
    }

    void TuiCoreController::closeInGameSettings() {
        settingsOverlayActive = false;
        settingsOverlaySurface.reset();
        pushActiveOverlay();
        rebuildMouseOverlay();
    }

    void TuiCoreController::buildSettingsOverlayItems() {
        settingsOverlayItems.clear();

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "FPS limit",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.targetFpsLimit = clampDouble(settingsOverlayWorking.targetFpsLimit + dir * 5.0, 30.0, 1440.0);
            },
            [this]() { return std::to_string(static_cast<int>(settingsOverlayWorking.targetFpsLimit)); }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Target TPS",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.targetTps = clampDouble(settingsOverlayWorking.targetTps + dir * 1.0, 10.0, 240.0);
            },
            [this]() { return std::to_string(static_cast<int>(settingsOverlayWorking.targetTps)); }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Stats overlay alpha",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.statsOverlayAlpha = clampDouble(settingsOverlayWorking.statsOverlayAlpha + dir * 0.02, 0.0, 1.0);
            },
            [this]() { return formatFixed(settingsOverlayWorking.statsOverlayAlpha, 2); }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Mouse cross alpha",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.mouseCrossAlpha = clampDouble(settingsOverlayWorking.mouseCrossAlpha + dir * 0.05, 0.0, 1.0);
            },
            [this]() { return formatFixed(settingsOverlayWorking.mouseCrossAlpha, 2); }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Show stats overlay",
            RuntimeSettingItem::Kind::Toggle,
            [this](int) { settingsOverlayWorking.enableStatsOverlay = !settingsOverlayWorking.enableStatsOverlay; },
            [this]() { return settingsOverlayWorking.enableStatsOverlay ? "On" : "Off"; }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Show mouse cross",
            RuntimeSettingItem::Kind::Toggle,
            [this](int) { settingsOverlayWorking.enableMouseCross = !settingsOverlayWorking.enableMouseCross; },
            [this]() { return settingsOverlayWorking.enableMouseCross ? "On" : "Off"; }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Diff-based rendering",
            RuntimeSettingItem::Kind::Toggle,
            [this](int) { settingsOverlayWorking.enableDiffRendering = !settingsOverlayWorking.enableDiffRendering; },
            [this]() { return settingsOverlayWorking.enableDiffRendering ? "On" : "Off"; }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Auto view size",
            RuntimeSettingItem::Kind::Toggle,
            [this](int) { settingsOverlayWorking.autoViewSize = !settingsOverlayWorking.autoViewSize; },
            [this]() { return settingsOverlayWorking.autoViewSize ? "On" : "Off"; }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "Renderer API (fmt)",
            RuntimeSettingItem::Kind::Toggle,
            [this](int) { settingsOverlayWorking.useFmtRenderer = !settingsOverlayWorking.useFmtRenderer; },
            [this]() { return settingsOverlayWorking.useFmtRenderer ? "fmt" : "std"; }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "View width",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.viewWidth = clampInt(settingsOverlayWorking.viewWidth + dir * 2, 16, 200);
            },
            [this]() { return std::to_string(settingsOverlayWorking.viewWidth); }
        });

        settingsOverlayItems.push_back(RuntimeSettingItem{
            "View height",
            RuntimeSettingItem::Kind::Number,
            [this](int dir) {
                settingsOverlayWorking.viewHeight = clampInt(settingsOverlayWorking.viewHeight + dir * 2, 16, 120);
            },
            [this]() { return std::to_string(settingsOverlayWorking.viewHeight); }
        });

        if (settingsOverlaySelected >= settingsOverlayItems.size()) {
            settingsOverlaySelected = settingsOverlayItems.empty() ? 0 : settingsOverlayItems.size() - 1;
        }
    }

    void TuiCoreController::rebuildSettingsOverlay() {
        if (!settingsOverlayActive) return;
        if (settingsOverlayItems.empty()) buildSettingsOverlayItems();
        if (settingsOverlayItems.empty()) return;
        if (settingsOverlaySelected >= settingsOverlayItems.size()) {
            settingsOverlaySelected = settingsOverlayItems.size() - 1;
        }

        int overlayW = std::max(32, viewWidth * 2);
        int overlayH = std::max(8, viewHeight);
        auto surface = std::make_shared<UI::TuiSurface>(overlayW, overlayH);

        int panelW = std::min(std::max(32, overlayW - 6), overlayW - 2);
        int panelX = std::max(1, (overlayW - panelW) / 2);
        int panelY = 2;
        int minPanelH = static_cast<int>(settingsOverlayItems.size()) + 6;
        int panelH = std::min(std::max(minPanelH, 8), overlayH - panelY - 1);
        if (panelH < 6) panelH = 6;

        surface->fillRect(panelX, panelY, panelW, panelH, settingsOverlayTheme.itemFg, settingsOverlayTheme.panel, " ");
        // Use modern rounded single-line box characters for a contemporary look
        UI::BoxStyle modernFrame{"╭", "╮", "╰", "╯", "─", "│"};
        surface->drawFrame(panelX, panelY, panelW, panelH, modernFrame, settingsOverlayTheme.itemFg, settingsOverlayTheme.panel);

        RGBColor titleBg = UI::TuiUtils::blendColor(settingsOverlayTheme.accent, settingsOverlayTheme.panel, 0.35);
        surface->fillRect(panelX + 1, panelY + 1, panelW - 2, 1, settingsOverlayTheme.title, titleBg, " ");
        surface->drawText(panelX + 2, panelY + 1, "In-game Settings", settingsOverlayTheme.title, titleBg);

        std::string subtitle = "W/S or Up/Down: select - A/D or Left/Right: adjust";
        surface->drawText(panelX + 2, panelY + 2, subtitle, settingsOverlayTheme.hintFg, settingsOverlayTheme.panel);

        int rowY = panelY + 4;
        int labelX = panelX + 3;
        int valueRight = panelX + panelW - 3;

        for (size_t i = 0; i < settingsOverlayItems.size() && rowY < panelY + panelH - 2; ++i) {
            const auto& item = settingsOverlayItems[i];
            bool focus = (i == settingsOverlaySelected);
            RGBColor rowBg = focus ? settingsOverlayTheme.focusBg : settingsOverlayTheme.panel;
            RGBColor rowFg = focus ? settingsOverlayTheme.focusFg : settingsOverlayTheme.itemFg;

            surface->fillRect(panelX + 1, rowY, panelW - 2, 1, rowFg, rowBg, " ");
            surface->drawText(labelX, rowY, item.label, rowFg, rowBg);

            std::string value = item.display ? item.display() : "";
            int valueWidth = static_cast<int>(UI::TuiUtils::calculateUtf8VisualWidth(value));
            int valueX = std::max(labelX + 12, valueRight - valueWidth);
            surface->drawText(valueX, rowY, value, rowFg, rowBg);

            rowY += 1;
        }

        int hintY = std::min(panelY + panelH - 2, overlayH - 2);
        surface->fillRect(panelX + 1, hintY, panelW - 2, 1, settingsOverlayTheme.hintFg, settingsOverlayTheme.panel, " ");
        surface->drawText(panelX + 2, hintY, "Enter/I/Q: close", settingsOverlayTheme.hintFg, settingsOverlayTheme.panel);

        settingsOverlaySurface = surface;
        pushActiveOverlay();
    }

    void TuiCoreController::applySettingsWorking() {
        settings = settingsOverlayWorking;
        viewWidth = settings.viewWidth;
        viewHeight = settings.viewHeight;
        refreshAutoViewSize();
        targetTps = settings.targetTps;

        if (renderer) {
            renderer->setBackend(settings.useFmtRenderer ? RendererBackend::Fmt : RendererBackend::Std);
            renderer->applyRuntimeSettings(settings.statsOverlayAlpha, settings.enableStatsOverlay, settings.enableDiffRendering, settings.targetFpsLimit);
        }

        if (!settings.enableMouseCross) {
            mouseOverlay.reset();
        }
    }

    void TuiCoreController::adjustSettingsSelection(int delta) {
        if (settingsOverlayItems.empty()) return;
        int count = static_cast<int>(settingsOverlayItems.size());
        int next = static_cast<int>(settingsOverlaySelected) + delta;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        settingsOverlaySelected = static_cast<size_t>(next);
        rebuildSettingsOverlay();
    }

    void TuiCoreController::adjustSettingsValue(int dir) {
        if (settingsOverlayItems.empty()) return;
        int step = dir >= 0 ? 1 : -1;
        auto& item = settingsOverlayItems[settingsOverlaySelected];
        if (item.adjust) item.adjust(step);
        applySettingsWorking();
        rebuildSettingsOverlay();
    }

    void TuiCoreController::handleSettingsOverlayKey(const InputEvent& ev) {
        if (ev.type != InputEvent::Type::Key) return;

        // NOTE: Do NOT treat Escape as a close trigger to avoid conflicts with terminal ESC sequences.
        if (ev.key == InputKey::Enter) {
            closeInGameSettings();
            return;
        }

        if (ev.key == InputKey::ArrowUp) { adjustSettingsSelection(-1); return; }
        if (ev.key == InputKey::ArrowDown) { adjustSettingsSelection(1); return; }
        if (ev.key == InputKey::ArrowLeft) { adjustSettingsValue(-1); return; }
        if (ev.key == InputKey::ArrowRight) { adjustSettingsValue(1); return; }

        if (ev.key == InputKey::Character) {
            char c = static_cast<char>(ev.ch);
            if (c == 'i' || c == 'I' || c == 'q' || c == 'Q') { closeInGameSettings(); return; }
            if (c == 'w' || c == 'W') { adjustSettingsSelection(-1); return; }
            if (c == 's' || c == 'S') { adjustSettingsSelection(1); return; }
            if (c == 'a' || c == 'A') { adjustSettingsValue(-1); return; }
            if (c == 'd' || c == 'D') { adjustSettingsValue(1); return; }
            if (c == '\r' || c == '\n') { closeInGameSettings(); return; }
        }
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

    void TuiCoreController::refreshAutoViewSize() {
        if (!settings.autoViewSize) return;
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
            int consoleWidth = std::max(2, info.srWindow.Right - info.srWindow.Left + 1);
            int consoleHeight = std::max(1, info.srWindow.Bottom - info.srWindow.Top + 1);
            int newViewWidth = std::max(8, consoleWidth / 2); // two columns per tile
            int newViewHeight = std::max(8, consoleHeight);
            if (newViewWidth != viewWidth || newViewHeight != viewHeight) {
                viewWidth = newViewWidth;
                viewHeight = newViewHeight;
                // Keep persisted settings in sync so turning off auto sizing doesn't jump back
                settings.viewWidth = viewWidth;
                settings.viewHeight = viewHeight;
                if (settingsOverlayActive) {
                    settingsOverlayWorking.viewWidth = viewWidth;
                    settingsOverlayWorking.viewHeight = viewHeight;
                }
                if (settingsOverlayActive) {
                    rebuildSettingsOverlay();
                } else {
                    rebuildMouseOverlay();
                }
            }
        }
#endif
    }

}
