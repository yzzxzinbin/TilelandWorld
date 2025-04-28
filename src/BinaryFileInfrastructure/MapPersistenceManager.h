#pragma once
#ifndef TILELANDWORLD_MAP_PERSISTENCE_MANAGER_H
#define TILELANDWORLD_MAP_PERSISTENCE_MANAGER_H

#include "../Map.h" // Need Map definition
#include <string>
#include <memory> // For std::unique_ptr

namespace TilelandWorld {

    class MapPersistenceManager {
    public:
        /**
         * @brief 保存当前地图状态到压缩存档文件 (.tlwz)。
         *
         * 此过程通常涉及：
         * 1. 使用 MapSerializer 将地图保存到临时的 .tlwf 文件。
         * 2. 读取 .tlwf 文件内容。
         * 3. 压缩内容。
         * 4. 将压缩内容和元数据写入 .tlwz 文件。
         * 5. (可选) 删除临时的 .tlwf 文件。
         *
         * @param map 要保存的地图对象。
         * @param saveName 存档名称 (不含扩展名，例如 "my_save")。
         * @param savesDirectory 存档文件存放的目录。
         * @param deleteTlwfAfterwards 是否在成功创建 .tlwz 后删除 .tlwf 文件。
         * @return true 如果保存成功，否则 false。
         */
        static bool saveMap(const Map& map, const std::string& saveName, const std::string& savesDirectory = ".", bool deleteTlwfAfterwards = true);

        /**
         * @brief 从存档文件加载地图。
         *
         * 此过程尝试按以下顺序加载：
         * 1. 直接加载 .tlwf 文件 (如果存在且有效，用于快速恢复或运行时)。
         * 2. 如果 .tlwf 加载失败或不存在，则尝试加载 .tlwz 文件：
         *    a. 读取并验证 .tlwz 文件头和数据。
         *    b. 解压缩数据。
         *    c. 将解压后的数据写入 .tlwf 文件。
         *    d. 从新生成的 .tlwf 文件加载地图。
         *
         * @param saveName 存档名称 (不含扩展名)。
         * @param savesDirectory 存档文件存放的目录。
         * @return 如果加载成功，返回包含地图数据的 std::unique_ptr<Map>；否则返回 nullptr。
         */
        static std::unique_ptr<Map> loadMapFromSave(const std::string& saveName, const std::string& savesDirectory = ".");

        // 获取 .tlwf 和 .tlwz 的完整路径
        static std::string getTlwfPath(const std::string& saveName, const std::string& directory);
        static std::string getTlwzPath(const std::string& saveName, const std::string& directory);

    private:
        // 内部辅助函数，用于实际的 .tlwz 加载和解压逻辑
        static std::unique_ptr<Map> loadFromCompressedFile(const std::string& tlwzPath, const std::string& tlwfPath);
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_MAP_PERSISTENCE_MANAGER_H
