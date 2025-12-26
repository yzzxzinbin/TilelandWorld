#pragma once
#ifndef TILELANDWORLD_UI_SAVECREATIONSCREEN_H
#define TILELANDWORLD_UI_SAVECREATIONSCREEN_H

#include "AnsiTui.h"
#include "DirectoryBrowserScreen.h"
#include "../SaveMetadata.h"
#include "../Controllers/InputController.h"
#include <string>
#include <vector>
#include <chrono>

namespace TilelandWorld {
namespace UI {

class SaveCreationScreen {
public:
    struct Result {
        bool accepted{false};
        std::string saveName;
        std::string saveDirectory;
        WorldMetadata metadata{};
    };

    explicit SaveCreationScreen(std::string defaultDirectory, WorldMetadata defaults = {}, std::string defaultName = {}, bool lockName = false, bool lockDirectory = false);

    // 显示创建界面，返回结果；accepted=false 表示取消
    Result show();

private:
    enum class FieldType { Text, Directory, Integer, Float, Choice, Action };

    struct Field {
        std::string label;
        FieldType type{FieldType::Text};
        double step{1.0};
        double minVal{0.0};
        double maxVal{0.0};
        // Choice uses options
    };

    TuiSurface surface;
    TuiPainter painter;
    MenuTheme theme;

    std::string name;
    std::string directory;
    WorldMetadata meta;

    bool allowNameEdit{true};
    bool allowDirectoryEdit{true};

    std::vector<Field> fields;
    size_t selected{0};

    // editing state
    int editingIndex{-1};
    FieldType editingType{FieldType::Text};
    std::string editingBuffer;
    double editMin{0.0};
    double editMax{0.0};
    bool editIsInt{false};

    // choices
    std::vector<std::string> noiseChoices;
    std::vector<std::string> fractalChoices;
    size_t noiseIndex{0};
    size_t fractalIndex{0};

    // layout cache
    int listStartY{6};
    int listLabelX{4};
    int listValueX{0};

    // double-click detection
    size_t lastClickIndex{static_cast<size_t>(-1)};
    std::chrono::steady_clock::time_point lastClickTime{};

    // nested input coordination
    InputController* activeInput{nullptr};

    void buildFields();
    void syncChoiceFromMetadata();
    void renderFrame();
    void handleKey(int key, bool& running, bool& accepted);
    void handleMouse(const InputEvent& ev, bool& running, bool& accepted);
    void startEdit(size_t idx);
    void commitEdit();
    void cancelEdit();
    void openDirectoryPicker();
    void randomizeSeed();

    std::string valueForField(size_t idx) const;
    std::string sanitizedName(const std::string& raw) const;
    std::string previewPath() const;
};

} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_SAVECREATIONSCREEN_H
