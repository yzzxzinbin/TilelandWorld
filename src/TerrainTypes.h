#pragma once
#ifndef TILELANDWORLD_TERRAINTYPES_H
#define TILELANDWORLD_TERRAINTYPES_H

#include <string>
#include <unordered_map> // 包含 unordered_map
#include <cstdint> // For uint8_t

namespace TilelandWorld {

    // 地形类型枚举
    enum class TerrainType {
        UNKNOWN, // 未知或默认
        VOIDBLOCK,    // 空，虚空 (用于多层地图的空区域)
        GRASS,   // 草地
        WATER,   // 水
        WALL,    // 墙壁
        FLOOR,   // 地板 (例如室内)
        // 可以根据需要添加更多地形类型...
    };

    // RGB 颜色结构体 (24位色)
    struct RGBColor {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
    };

    // 地形属性结构体
    struct TerrainProperties {
        std::string displayChar; // 用于在终端显示的UTF-8字符(串)
        // --- 颜色属性 (24位色) ---
        RGBColor foregroundColor;
        RGBColor backgroundColor;
        // --- 通行性属性 ---
        bool allowEnterSameLevel;// allowEnterSameLevel: 实体是否能进入与其 *相同层级* 的此类型地形？
        bool allowStandOnTop; // allowStandOnTop: 实体是否能站在比其 *低一层* 的此类型地形 *上方*？
        // --- 可见性 ---
        bool isVisible;         // 该地形本身是否可见/应被直接渲染？
        // --- 其他属性 ---
        int defaultMovementCost; // 默认移动消耗
    };

    // 获取地形属性的函数 这里使用函数示例，更灵活，易于扩展
    inline const TerrainProperties& getTerrainProperties(TerrainType type) {
        // 使用静态局部变量确保只初始化一次
        // TerrainProperties: {displayChar, fgColor, bgColor, allowEnterSameLevel, allowStandOnTop, isVisible, defaultMovementCost}
        static const std::unordered_map<TerrainType, TerrainProperties> terrainData = {
            {TerrainType::UNKNOWN, {"?",   {255, 0, 255}, {0, 0, 0},       false, false, true,  99}}, // 品红前景，黑背景, 可见
            {TerrainType::VOIDBLOCK,    {" ",   {0, 0, 0},     {0, 0, 0},       true,  false, false, 99}}, // 黑前景，黑背景 (纯黑), 不可见
            {TerrainType::GRASS,   {"░",   {0, 180, 0},   {0, 100, 0},     true,  false, true,  1}},  // 亮绿前景，暗绿背景, 可见
            {TerrainType::WATER,   {"≈",   {0, 100, 255}, {0, 50, 150},    false, false, true,  5}},  // 亮蓝前景，暗蓝背景, 可见
            {TerrainType::WALL,    {"█",   {150, 150, 150},{100, 100, 100}, false, true,  true,  99}}, // 灰色前景，深灰背景, 可见
            {TerrainType::FLOOR,   {"·",   {200, 200, 200},{50, 50, 50},    true,  false, true,  1}}   // 浅灰前景，非常暗的灰背景, 可见
            // 添加其他地形的属性...
        };

        auto it = terrainData.find(type);
        if (it != terrainData.end()) {
            return it->second;
        } else {
            // 如果找不到，返回 UNKNOWN 的属性
            return terrainData.find(TerrainType::UNKNOWN)->second; // 假设 UNKNOWN 总存在
        }
    }

} // namespace TilelandWorld

#endif // TILELANDWORLD_TERRAINTYPES_H
