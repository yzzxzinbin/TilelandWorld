#pragma once
#ifndef TILELANDWORLD_TILE_H
#define TILELANDWORLD_TILE_H

#include "TerrainTypes.h" // 引入地形类型定义
#include <cstdint>       // For uint8_t
#include <string>        // For std::string

namespace TilelandWorld {

    // 定义基本的 Tile 结构
    struct Tile {
        // --- Tile 基本类型枚举标识 ---
        TerrainType terrain = TerrainType::UNKNOWN; // 地形类型

        // --- Tile 地形属性 ---
        // 当前 Tile 实例的通行性状态。这些值由地形的默认属性初始化，但可能被游戏逻辑修改。
        bool canEnterSameLevel; // 同层级可通行性
        bool canStandOnTop;     // Tile的上表面可站立
        int movementCost;       // 移动消耗 (可能需要根据移动方式调整，例如攀爬 vs. 平地移动)

        // --- 可见性/光照 ---
        uint8_t lightLevel = MAX_LIGHT_LEVEL; // 当前光照等级 (0-255), 0=全黑, 255=全亮
        bool isExplored = false; // 是否已被玩家探索过 (用于战争迷雾)

        // --- 其他层/元素/状态 ---
        // TODO: 添加指向或标识物体、角色、特效等的数据成员
        // 例如：
        // int objectId = -1;
        // int characterId = -1;
        // TODO: 添加特殊效果标志 (e.g., bitmask, std::set<EffectType>)
        // TODO: 添加事件触发器 ID 或指针

        // 构造函数 - 根据地形类型初始化
        explicit Tile(TerrainType type = TerrainType::UNKNOWN) :
            terrain(type),
            lightLevel(MAX_LIGHT_LEVEL), // 默认设置为最大光照
            isExplored(false)            // 默认未探索
        {
            const auto& props = getTerrainProperties(type);
            // 初始化 Tile 的通行状态和移动成本为地形的默认值
            canEnterSameLevel = props.allowEnterSameLevel;
            canStandOnTop = props.allowStandOnTop;
            movementCost = props.defaultMovementCost;
        }

        // 获取用于显示的字符
        const std::string& getDisplayChar() const; // 实现移至 cpp

        // 获取考虑光照影响后的前景色和背景色
        RGBColor getForegroundColor() const; // 返回计算后的颜色，实现移至 cpp
        RGBColor getBackgroundColor() const; // 返回计算后的颜色，实现移至 cpp
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TILE_H
