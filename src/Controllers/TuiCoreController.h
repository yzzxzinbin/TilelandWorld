#pragma once
#ifndef TILELANDWORLD_TUICHUNKCONTROLLER_H
#define TILELANDWORLD_TUICHUNKCONTROLLER_H

#include "../Map.h"
#include "../Coordinates.h"
#include "TuiRenderer.h" 
#include "InputController.h"
#include "../Settings.h"
#include "../MapGenInfrastructure/ChunkGeneratorPool.h"
#include "../MapGenInfrastructure/TerrainGeneratorFactory.h"
#include "../Utils/TaskSystem.h" // 引入 TaskSystem
#include <unordered_set>
#include <string>
#include <mutex> 
#include <memory>
#include <vector>
#include <functional>
#include <windows.h> // 引入 QueryPerformanceCounter

namespace TilelandWorld {

    class TuiCoreController {
    public:
        explicit TuiCoreController(Map& map, const Settings& settings);
        ~TuiCoreController();

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

        // 核心组件
        std::unique_ptr<TaskSystem> taskSystem; // 通用任务系统
        std::unique_ptr<ChunkGeneratorPool> generatorPool; // 区块生成管理器
        std::unique_ptr<TuiRenderer> renderer; // 渲染器
        std::unique_ptr<InputController> inputController; // 输入控制器

        // 视图状态
        int viewX = 0;
        int viewY = 0;
        int currentZ = 0;
        int viewWidth = 64;
        int viewHeight = 48;
        bool running = true;

        Settings settings;

        struct RuntimeSettingItem {
            enum class Kind { Toggle, Number };
            std::string label;
            Kind kind{Kind::Number};
            std::function<void(int)> adjust; // dir: -1/1
            std::function<std::string()> display;
        };

        bool settingsOverlayActive{false};
        Settings settingsOverlayWorking{};
        size_t settingsOverlaySelected{0};
        std::shared_ptr<UI::TuiSurface> settingsOverlaySurface;
        UI::MenuTheme settingsOverlayTheme{};
        std::vector<RuntimeSettingItem> settingsOverlayItems;

        // 鼠标叠加层
        std::shared_ptr<UI::TuiSurface> mouseOverlay;
        int mouseScreenX = -1;
        int mouseScreenY = -1;
        
        // TPS 控制
        double targetTps = 60.9;

        // 新增：TPS 计算变量
        double currentTps = 0.0;
        int tickCount = 0;
        long long lastTpsTime = 0;
        long long tpsFrequency = 0;

        // 输入状态追踪
        bool leftArrowPressedLastFrame = false;
        bool rightArrowPressedLastFrame = false;

        // 内部逻辑
        void handleInput();
        void rebuildMouseOverlay();
        void pushActiveOverlay();
        void toggleInGameSettings();
        void openInGameSettings();
        void closeInGameSettings();
        void buildSettingsOverlayItems();
        void rebuildSettingsOverlay();
        void handleSettingsOverlayKey(const InputEvent& ev);
        void applySettingsWorking();
        void adjustSettingsSelection(int delta);
        void adjustSettingsValue(int dir);
        void refreshAutoViewSize();
        
        // 2. 预加载逻辑
        void preloadChunks();
        
        // 控制台辅助方法
        void setupConsole();
        void clearScreen();
        void showCursor();
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TUICHUNKCONTROLLER_H
