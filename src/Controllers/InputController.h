#pragma once
#ifndef TILELANDWORLD_INPUTCONTROLLER_H
#define TILELANDWORLD_INPUTCONTROLLER_H

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {

    enum class InputKey {
        Unknown,
        ArrowUp,
        ArrowDown,
        ArrowLeft,
        ArrowRight,
        Enter,
        Escape,
        Tab,
        Character
    };

    struct InputEvent {
        enum class Type { Key, Mouse } type{Type::Key};

        // Key data
        InputKey key{InputKey::Unknown};
        char32_t ch{0}; // when key == Character
        bool ctrl{false};
        bool alt{false};
        bool shift{false};

        // Mouse data
        int x{0}; // 0-based column
        int y{0}; // 0-based row
        int button{0}; // 0=left,1=middle,2=right
        int wheel{0}; // +1 up, -1 down
        bool pressed{false};
        bool move{false};
    };

    class InputController {
    public:
        explicit InputController(bool restoreOnExit = true);
        ~InputController();

        // 启动后台读取线程
        void start();
        void stop();

        void setRestoreOnExit(bool enabled) { restoreOnExit = enabled; }

        // 拉取当前已解析的输入事件队列（清空内部队列）
        std::vector<InputEvent> pollEvents();

    private:
#ifdef _WIN32
        HANDLE hIn{INVALID_HANDLE_VALUE};
        DWORD oldMode{0};
        bool modeSaved{false};
#endif
    bool restoreOnExit{true};
        std::thread readerThread;
        std::atomic<bool> running{false};
        std::mutex queueMutex;
        std::vector<InputEvent> eventQueue;
        std::string buffer;

        void readerLoop();
        void parseBuffer();

        bool tryParseMouseSGR();
        bool tryParseArrow();
        bool tryParseVtResponse();
        void emitKey(InputKey key, char32_t ch = 0, bool ctrl = false, bool alt = false, bool shift = false);
        void emitMouse(int x, int y, int button, bool pressed, bool move, int wheel = 0, bool ctrl = false, bool alt = false, bool shift = false);
        void emitChar(char32_t ch);

#ifdef _WIN32
        bool enableVTInput();
        void restoreConsole();
#endif
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_INPUTCONTROLLER_H
