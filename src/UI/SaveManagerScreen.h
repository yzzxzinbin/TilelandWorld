#pragma once
#ifndef TILELANDWORLD_UI_SAVEMANAGERSCREEN_H
#define TILELANDWORLD_UI_SAVEMANAGERSCREEN_H

#include "AnsiTui.h"
#include "../Controllers/InputController.h"
#include "../SaveMetadata.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include <string>
#include <vector>

namespace TilelandWorld {
namespace UI {

class SaveManagerScreen {
public:
    struct Result {
        enum class Action { Load, CreateNew, Back };
        Action action{Action::Back};
        std::string saveName;
        std::string saveDirectory;
        WorldMetadata metadata{};
    };

    explicit SaveManagerScreen(std::string saveDirectory);

    // 显示存档管理器，返回用户动作与所选存档信息
    Result show();

private:
    std::string directory;
    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;
    MenuView menu;
    size_t selectedIndex{0};
    std::vector<std::string> saves;
    struct SaveInfo { bool loaded{false}; bool ok{false}; MapSerializer::SaveSummary summary{}; };
    std::vector<SaveInfo> infoCache;

    // 布局缓存供鼠标命中
    int lastPanelX{0};
    int lastPanelY{0};
    int lastPanelWidth{0};
    int lastListStart{0};
    int lastListCount{0};

    void refreshList();
    void renderFrame();
    void handleKey(int key, bool& running, Result& result, InputController& input);
    void handleMouse(const InputEvent& ev, bool& running, Result& result, InputController& input);
    void ensureAnsiEnabled();
    void ensureInfo(size_t idx);
    void renderInfoBar();
    std::string formatBytes(size_t bytes) const;
    bool editSave(size_t idx, InputController& input);

    Result handleCreateNew(InputController& input);
    bool deleteSelected();
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_SAVEMANAGERSCREEN_H
