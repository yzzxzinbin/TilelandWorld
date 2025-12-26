#include "SaveCreationScreen.h"
#include "TuiUtils.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <random>
#include <thread>
#include <chrono>
#include <limits>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    constexpr int kArrowUp = 0x100 | 72;
    constexpr int kArrowDown = 0x100 | 80;
    constexpr int kArrowLeft = 0x100 | 75;
    constexpr int kArrowRight = 0x100 | 77;

    double clampDouble(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
    long long clampLL(long long v, long long lo, long long hi) { return std::max(lo, std::min(hi, v)); }

    std::string toLower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string defaultName() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return "world-" + std::to_string(static_cast<long long>(ms & 0xFFFFFF));
    }
}

SaveCreationScreen::SaveCreationScreen(std::string defaultDirectory, WorldMetadata defaults)
    : surface(100, 40), name(defaultName()), directory(std::move(defaultDirectory)), meta(defaults) {
    noiseChoices = {"OpenSimplex2", "Perlin", "Value"};
    fractalChoices = {"FBm", "Ridged", "PingPong"};
    syncChoiceFromMetadata();
    buildFields();
}

void SaveCreationScreen::buildFields() {
    fields.clear();
    fields.push_back(Field{"Save name", FieldType::Text});
    fields.push_back(Field{"Directory", FieldType::Directory});
    fields.push_back(Field{"Seed", FieldType::Integer, 101.0,
        static_cast<double>(std::numeric_limits<int64_t>::min() / 2),
        static_cast<double>(std::numeric_limits<int64_t>::max() / 2)});
    fields.push_back(Field{"Frequency", FieldType::Float, 0.005, 0.001, 0.2});
    fields.push_back(Field{"Noise type", FieldType::Choice});
    fields.push_back(Field{"Fractal type", FieldType::Choice});
    fields.push_back(Field{"Octaves", FieldType::Integer, 1.0, 1.0, 12.0});
    fields.push_back(Field{"Lacunarity", FieldType::Float, 0.1, 1.0, 4.0});
    fields.push_back(Field{"Gain", FieldType::Float, 0.05, 0.1, 1.0});
    fields.push_back(Field{"Create", FieldType::Action});
}

void SaveCreationScreen::syncChoiceFromMetadata() {
    auto findIdx = [](const std::vector<std::string>& list, const std::string& value, size_t fallback) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (toLower(list[i]) == toLower(value)) return i;
        }
        return fallback;
    };
    noiseIndex = findIdx(noiseChoices, meta.noiseType, 0);
    fractalIndex = findIdx(fractalChoices, meta.fractalType, 0);
    meta.noiseType = noiseChoices[noiseIndex];
    meta.fractalType = fractalChoices[fractalIndex];
}

SaveCreationScreen::Result SaveCreationScreen::show() {
    Result result{};
    bool running = true;
    bool accepted = false;

    InputController input;
    activeInput = &input;
    input.start();

    while (running) {
        renderFrame();
        painter.present(surface, true, 1, 1);

        auto events = input.pollEvents();
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Mouse) {
                handleMouse(ev, running, accepted);
            } else if (ev.type == InputEvent::Type::Key) {
                if (ev.key == InputKey::Character) {
                    int ch = static_cast<int>(ev.ch);
                    if (ch == 13 || ch == '\n' || ch == '\r') ch = 13;
                    handleKey(ch, running, accepted);
                } else if (ev.key == InputKey::Enter) {
                    handleKey(13, running, accepted);
                } else if (ev.key == InputKey::ArrowUp) {
                    handleKey(kArrowUp, running, accepted);
                } else if (ev.key == InputKey::ArrowDown) {
                    handleKey(kArrowDown, running, accepted);
                } else if (ev.key == InputKey::ArrowLeft) {
                    handleKey(kArrowLeft, running, accepted);
                } else if (ev.key == InputKey::ArrowRight) {
                    handleKey(kArrowRight, running, accepted);
                }
            }
            if (!running) break;
        }
    }

    painter.reset();
    input.stop();
    activeInput = nullptr;

    if (accepted) {
        meta.noiseType = noiseChoices[noiseIndex];
        meta.fractalType = fractalChoices[fractalIndex];
        result.accepted = true;
        result.metadata = meta;
        result.saveDirectory = directory;
        result.saveName = sanitizedName(name);
    }

    return result;
}

void SaveCreationScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int consoleWidth = std::max(70, info.srWindow.Right - info.srWindow.Left + 1);
        int consoleHeight = std::max(24, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(consoleWidth, consoleHeight);
    }
#endif

    surface.clear(theme.itemFg, theme.background, " ");
    surface.fillRect(0, 0, surface.getWidth(), 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, surface.getHeight() - 1, surface.getWidth(), 1, theme.accent, theme.accent, " ");

    surface.drawText(2, 1, "Create New World", {0, 0, 0}, {96, 140, 255});
    surface.drawText(2, 3, "Enter: confirm | Q: cancel | R: random seed | Double-click text/number to edit | B: browse directory", theme.hintFg, theme.background);

    listStartY = 6;
    listLabelX = 4;
    listValueX = surface.getWidth() / 2 + 6;

    int usableWidth = surface.getWidth() - 4;

    for (size_t i = 0; i < fields.size(); ++i) {
        bool focus = i == selected;
        bool editing = (static_cast<int>(i) == editingIndex);
        RGBColor rowFg = focus ? theme.focusFg : theme.itemFg;
        RGBColor rowBg = focus ? theme.focusBg : theme.panel;
        if (editing) {
            rowBg = TuiUtils::blendColor(rowBg, theme.accent, 0.25);
        }

        int y = listStartY + static_cast<int>(i);
        surface.fillRect(1, y, usableWidth, 1, rowFg, rowBg, " ");
        surface.drawText(listLabelX, y, fields[i].label, rowFg, rowBg);

        std::string val = valueForField(i);
        int maxValWidth = usableWidth - listValueX - 2;
        if (maxValWidth < 0) maxValWidth = 0;
        size_t vis = TuiUtils::calculateUtf8VisualWidth(val);
        if (vis > static_cast<size_t>(maxValWidth)) {
            val = TuiUtils::trimToUtf8VisualWidth(val, static_cast<size_t>(maxValWidth));
        }
        surface.drawText(listValueX, y, val, rowFg, rowBg);
    }

    std::string preview = "Will save to: " + previewPath();
    surface.drawText(2, surface.getHeight() - 3, preview, theme.subtitle, theme.background);
}

std::string SaveCreationScreen::valueForField(size_t idx) const {
    if (idx >= fields.size()) return "";
    const auto& f = fields[idx];
    std::ostringstream oss;
    switch (f.type) {
        case FieldType::Text:
            return editingIndex == static_cast<int>(idx) ? ("[ " + editingBuffer + " ]") : name;
        case FieldType::Directory:
            return directory;
        case FieldType::Integer:
            if (editingIndex == static_cast<int>(idx)) return "[ " + editingBuffer + " ]";
            if (idx == 2) {
                oss << static_cast<long long>(meta.seed);
            } else if (idx == 6) {
                oss << meta.octaves;
            }
            return oss.str();
        case FieldType::Float:
            if (editingIndex == static_cast<int>(idx)) return "[ " + editingBuffer + " ]";
            oss << std::fixed << std::setprecision(3);
            if (idx == 3) oss << meta.frequency;
            else if (idx == 7) oss << meta.lacunarity;
            else if (idx == 8) oss << meta.gain;
            return oss.str();
        case FieldType::Choice:
            if (idx == 4) return noiseChoices[noiseIndex];
            if (idx == 5) return fractalChoices[fractalIndex];
            return "";
        case FieldType::Action:
            return "[ Create ]";
    }
    return "";
}

void SaveCreationScreen::handleKey(int key, bool& running, bool& accepted) {
    if (!running) return;

    if (editingIndex >= 0) {
        if (key == 8 || key == 0x7F) {
            if (!editingBuffer.empty()) editingBuffer.pop_back();
            return;
        }
        if (key == 13) {
            commitEdit();
            return;
        }
        if (key == 'q' || key == 'Q') {
            cancelEdit();
            return;
        }
        if (editingType == FieldType::Text) {
            if (key >= 32 && key <= 126) {
                char ch = static_cast<char>(key);
                if (ch == '\t') return;
                editingBuffer.push_back(ch);
            }
        } else { // numeric edit
            if ((key >= '0' && key <= '9') || key == '-' || key == '.') {
                editingBuffer.push_back(static_cast<char>(key));
            }
        }
        return;
    }

    if (key == kArrowUp || key == 'w' || key == 'W') {
        if (selected == 0) selected = fields.size() - 1; else --selected;
    } else if (key == kArrowDown || key == 's' || key == 'S') {
        selected = (selected + 1) % fields.size();
    } else if (key == kArrowLeft || key == 'a' || key == 'A') {
        auto& f = fields[selected];
        if (f.type == FieldType::Integer) {
            if (selected == 2) {
                meta.seed = clampLL(meta.seed - static_cast<long long>(f.step), static_cast<long long>(f.minVal), static_cast<long long>(f.maxVal));
            } else if (selected == 6) {
                meta.octaves = static_cast<int>(clampDouble(meta.octaves - f.step, f.minVal, f.maxVal));
            }
        } else if (f.type == FieldType::Float) {
            if (selected == 3) meta.frequency = static_cast<float>(clampDouble(meta.frequency - f.step, f.minVal, f.maxVal));
            else if (selected == 7) meta.lacunarity = static_cast<float>(clampDouble(meta.lacunarity - f.step, f.minVal, f.maxVal));
            else if (selected == 8) meta.gain = static_cast<float>(clampDouble(meta.gain - f.step, f.minVal, f.maxVal));
        } else if (f.type == FieldType::Choice) {
            if (selected == 4 && noiseIndex > 0) --noiseIndex; else if (selected == 5 && fractalIndex > 0) --fractalIndex;
        }
    } else if (key == kArrowRight || key == 'd' || key == 'D' || key == ' ') {
        auto& f = fields[selected];
        if (f.type == FieldType::Integer) {
            if (selected == 2) {
                meta.seed = clampLL(meta.seed + static_cast<long long>(f.step), static_cast<long long>(f.minVal), static_cast<long long>(f.maxVal));
            } else if (selected == 6) {
                meta.octaves = static_cast<int>(clampDouble(meta.octaves + f.step, f.minVal, f.maxVal));
            }
        } else if (f.type == FieldType::Float) {
            if (selected == 3) meta.frequency = static_cast<float>(clampDouble(meta.frequency + f.step, f.minVal, f.maxVal));
            else if (selected == 7) meta.lacunarity = static_cast<float>(clampDouble(meta.lacunarity + f.step, f.minVal, f.maxVal));
            else if (selected == 8) meta.gain = static_cast<float>(clampDouble(meta.gain + f.step, f.minVal, f.maxVal));
        } else if (f.type == FieldType::Choice) {
            if (selected == 4 && noiseIndex + 1 < noiseChoices.size()) ++noiseIndex; else if (selected == 5 && fractalIndex + 1 < fractalChoices.size()) ++fractalIndex;
        } else if (f.type == FieldType::Action) {
            accepted = true; running = false; return;
        } else if (f.type == FieldType::Directory) {
            openDirectoryPicker();
        }
    } else if (key == 13) { // Enter
        const auto& f = fields[selected];
        if (f.type == FieldType::Action) {
            accepted = true; running = false;
        } else if (f.type == FieldType::Directory) {
            openDirectoryPicker();
        } else if (f.type == FieldType::Text || f.type == FieldType::Integer || f.type == FieldType::Float) {
            startEdit(selected);
        }
    } else if (key == 'q' || key == 'Q') {
        accepted = false; running = false;
    } else if (key == 'b' || key == 'B') {
        openDirectoryPicker();
    } else if (key == 'r' || key == 'R') {
        if (selected == 2 || selected == fields.size() - 1) { // seed row or create row
            randomizeSeed();
        }
    } else if (key == 'e' || key == 'E') {
        const auto& f = fields[selected];
        if (f.type == FieldType::Text || f.type == FieldType::Integer || f.type == FieldType::Float) {
            startEdit(selected);
        }
    }
}

void SaveCreationScreen::handleMouse(const InputEvent& ev, bool& running, bool& accepted) {
    if (!running) return;

    if (ev.wheel != 0) {
        if (ev.wheel > 0) {
            if (selected == 0) selected = fields.size() - 1; else --selected;
        } else {
            selected = (selected + 1) % fields.size();
        }
        return;
    }

    if (!ev.pressed && !ev.move) return;

    int relY = ev.y - listStartY;
    if (relY < 0 || relY >= static_cast<int>(fields.size())) return;
    size_t idx = static_cast<size_t>(relY);
    selected = idx;

    if (ev.button == 0 && ev.pressed) {
        auto now = std::chrono::steady_clock::now();
        bool doubleClick = (lastClickIndex == idx) && (now - lastClickTime < std::chrono::milliseconds(400));
        lastClickIndex = idx;
        lastClickTime = now;

        const auto& f = fields[idx];
        if (doubleClick) {
            if (f.type == FieldType::Text || f.type == FieldType::Integer || f.type == FieldType::Float) {
                startEdit(idx);
            } else if (f.type == FieldType::Directory) {
                openDirectoryPicker();
            } else if (f.type == FieldType::Action) {
                accepted = true; running = false;
            }
            return;
        }

        if (f.type == FieldType::Directory) {
            openDirectoryPicker();
        } else if (f.type == FieldType::Choice) {
            if (idx == 4) {
                noiseIndex = (noiseIndex + 1) % noiseChoices.size();
            } else if (idx == 5) {
                fractalIndex = (fractalIndex + 1) % fractalChoices.size();
            }
        } else if (f.type == FieldType::Action) {
            accepted = true; running = false;
        }
    }
}

void SaveCreationScreen::startEdit(size_t idx) {
    if (idx >= fields.size()) return;
    editingIndex = static_cast<int>(idx);
    editingType = fields[idx].type;
    if (editingType == FieldType::Text) {
        editingBuffer = name;
    } else if (editingType == FieldType::Integer) {
        if (idx == 2) editingBuffer = std::to_string(static_cast<long long>(meta.seed));
        else if (idx == 6) editingBuffer = std::to_string(meta.octaves);
        editMin = fields[idx].minVal; editMax = fields[idx].maxVal; editIsInt = true;
    } else if (editingType == FieldType::Float) {
        if (idx == 3) editingBuffer = std::to_string(meta.frequency);
        else if (idx == 7) editingBuffer = std::to_string(meta.lacunarity);
        else if (idx == 8) editingBuffer = std::to_string(meta.gain);
        editMin = fields[idx].minVal; editMax = fields[idx].maxVal; editIsInt = false;
    } else {
        editingIndex = -1;
    }
}

void SaveCreationScreen::commitEdit() {
    if (editingIndex < 0 || static_cast<size_t>(editingIndex) >= fields.size()) { editingIndex = -1; return; }
    size_t idx = static_cast<size_t>(editingIndex);
    auto type = fields[idx].type;
    if (type == FieldType::Text) {
        name = sanitizedName(editingBuffer);
    } else if (type == FieldType::Integer || type == FieldType::Float) {
        double v = 0.0;
        try {
            if (editIsInt) v = static_cast<double>(std::stoll(editingBuffer));
            else v = std::stod(editingBuffer);
        } catch (...) {
            editingIndex = -1; editingBuffer.clear(); return; }
        v = clampDouble(v, editMin, editMax);
        if (idx == 2) meta.seed = static_cast<int64_t>(v);
        else if (idx == 3) meta.frequency = static_cast<float>(v);
        else if (idx == 6) meta.octaves = static_cast<int>(v);
        else if (idx == 7) meta.lacunarity = static_cast<float>(v);
        else if (idx == 8) meta.gain = static_cast<float>(v);
    }
    editingIndex = -1;
    editingBuffer.clear();
}

void SaveCreationScreen::cancelEdit() {
    editingIndex = -1;
    editingBuffer.clear();
}

void SaveCreationScreen::openDirectoryPicker() {
    painter.reset();
    if (activeInput) activeInput->stop();

    DirectoryBrowserScreen browser(directory);
    std::string chosen = browser.show();
    if (!chosen.empty()) {
        directory = chosen;
    }

    if (activeInput) activeInput->start();
}

void SaveCreationScreen::randomizeSeed() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<long long> dist(static_cast<long long>(fields[2].minVal), static_cast<long long>(fields[2].maxVal));
    meta.seed = dist(gen);
}

std::string SaveCreationScreen::sanitizedName(const std::string& raw) const {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.';
        if (ok) out.push_back(c); else out.push_back('_');
    }
    if (out.empty()) out = defaultName();
    return out;
}

std::string SaveCreationScreen::previewPath() const {
    std::string base = sanitizedName(name);
    std::string dir = directory.empty() ? "." : directory;
    if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        return dir + base + ".tlwz";
    }
    return dir + "/" + base + ".tlwz";
}

} // namespace UI
} // namespace TilelandWorld
