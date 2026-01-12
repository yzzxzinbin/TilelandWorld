#include "InputController.h"
#include "../Utils/EnvConfig.h"
#include <algorithm>
#include <cstring>

namespace TilelandWorld {

    namespace {
        // helpers
        bool startsWith(const std::string& s, const std::string& prefix) {
            return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
        }

        // Parse integer from string segment, return false if fail
        bool parseInt(const std::string& s, size_t& pos, int& out) {
            if (pos >= s.size() || (s[pos] < '0' || s[pos] > '9')) return false;
            int val = 0;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                val = val * 10 + (s[pos] - '0');
                ++pos;
            }
            out = val;
            return true;
        }
    }

    InputController::InputController(bool restoreOnExit_)
        : restoreOnExit(restoreOnExit_)
    {
#ifdef _WIN32
        hIn = GetStdHandle(STD_INPUT_HANDLE);
        enableVTInput();
#endif
    }

    InputController::~InputController()
    {
        stop();
#ifdef _WIN32
        if (restoreOnExit) restoreConsole();
#endif
    }

    void InputController::start()
    {
        if (running) return;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            eventQueue.clear();
            buffer.clear();
        }

#ifdef _WIN32
        if (hIn != INVALID_HANDLE_VALUE) {
            enableVTInput();
            FlushConsoleInputBuffer(hIn);
        }
#endif

        running = true;
        readerThread = std::thread(&InputController::readerLoop, this);
    }

    void InputController::stop()
    {
        if (!running) return;
        running = false;
    #ifdef _WIN32
        // Cancel the blocking ReadFile on the reader thread so join() won't wait for another keypress
        if (readerThread.joinable()) {
            HANDLE th = reinterpret_cast<HANDLE>(readerThread.native_handle());
            if (th) {
                CancelSynchronousIo(th);
            }
        }
        if (hIn != INVALID_HANDLE_VALUE) {
            CancelIoEx(hIn, nullptr);
        }
    #endif
        if (readerThread.joinable()) readerThread.join();
    }

    std::vector<InputEvent> InputController::pollEvents()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::vector<InputEvent> out;
        out.swap(eventQueue);
        return out;
    }

    void InputController::readerLoop()
    {
#ifdef _WIN32
        if (hIn == INVALID_HANDLE_VALUE) return;
        DWORD read = 0;
        char buf[256];
        while (running)
        {
            DWORD waitRes = WaitForSingleObject(hIn, 50); // small timeout to allow clean shutdown
            if (!running) break;
            if (waitRes == WAIT_TIMEOUT) {
                continue; // no input yet
            }
            if (waitRes == WAIT_FAILED) {
                break;
            }

            if (!ReadFile(hIn, buf, sizeof(buf), &read, nullptr)) {
                break;
            }
            if (read == 0) {
                continue;
            }
            buffer.append(buf, buf + read);
            parseBuffer();
        }
#endif
    }

    void InputController::parseBuffer()
    {
        // Simple state-less parsing loop
        while (!buffer.empty())
        {
            // ESC sequences
            if (buffer[0] == '\x1b')
            {
                if (buffer.size() == 1) return; // wait for possible sequence
                if (buffer[1] != '[') {
                    emitKey(InputKey::Escape);
                    buffer.erase(0, 1);
                    continue;
                }

                // Try mouse first (CSI < ...)
                if (buffer.size() >= 3 && buffer[2] == '<') {
                    if (!tryParseMouseSGR()) return; // incomplete
                    continue;
                }

                // Try arrows or simple CSI sequences
                if (!tryParseArrow()) return; // incomplete
                continue;
            }

            // Printable / control chars
            unsigned char c = static_cast<unsigned char>(buffer[0]);
            if (c == '\r') {
                emitKey(InputKey::Enter, 13);
                buffer.erase(0, 1);
                continue;
            }
            if (c == '\n') {
                // Often CTRL+Enter sends \n (LF) while Enter sends \r (CR)
                emitKey(InputKey::Enter, 13, true); 
                buffer.erase(0, 1);
                continue;
            }
            if (c == '\t') {
                emitKey(InputKey::Tab);
                buffer.erase(0, 1);
                continue;
            }
            if (c == 0x7F || c == 0x08) {
                emitKey(InputKey::Character, '\b');
                buffer.erase(0, 1);
                continue;
            }

            // Control characters (CTRL+A to CTRL+Z)
            if (c >= 0x01 && c <= 0x1A) {
                // 0x01 is CTRL+A, 0x02 is CTRL+B, etc.
                // Exclude Tab (0x09), Enter (0x0D), and others already handled
                if (c != 0x09 && c != 0x0D && c != 0x0A && c != 0x08) {
                    emitKey(InputKey::Character, static_cast<char32_t>(c + 'A' - 1), true);
                    buffer.erase(0, 1);
                    continue;
                }
            }

            if (c >= 0x20 && c <= 0x7E) {
                emitChar(static_cast<char32_t>(c));
                buffer.erase(0, 1);
                continue;
            }

            // Unknown control, drop
            buffer.erase(0, 1);
        }
    }

    bool InputController::tryParseArrow()
    {
        // Expecting ESC [ X
        if (buffer.size() < 3) return false; // incomplete
        if (!(buffer[0] == '\x1b' && buffer[1] == '[')) return false;

        char code = buffer[2];
        InputKey key = InputKey::Unknown;
        switch (code) {
            case 'A': key = InputKey::ArrowUp; break;
            case 'B': key = InputKey::ArrowDown; break;
            case 'C': key = InputKey::ArrowRight; break;
            case 'D': key = InputKey::ArrowLeft; break;
            default: break;
        }

        if (key != InputKey::Unknown) {
            emitKey(key);
            buffer.erase(0, 3);
            return true;
        }

        // Try VT response (e.g., cell/pixel query)
        if (tryParseVtResponse()) return true;

        // Not an arrow or known CSI; drop ESC and continue
        buffer.erase(0, 1);
        return true;
    }

    bool InputController::tryParseVtResponse()
    {
        // Expecting ESC [ 8 ; r ; c t  OR  ESC [ 4 ; h ; w t
        // Also possibly ESC [ < ... (handled by tryParseMouseSGR)
        if (buffer.size() < 4) return false;

        size_t tPos = buffer.find('t', 2);
        if (tPos == std::string::npos) {
            // If it's too long and no 't' is found, it's probably not a VT response
            return false;
        }

        std::string seq = buffer.substr(2, tPos - 2);
        size_t pos = 0;
        int type = 0;
        if (!parseInt(seq, pos, type)) return false;

        if (type == 8) { // \x1b[8;r;ct
            if (pos >= seq.size() || seq[pos] != ';') return false;
            ++pos;
            int r = 0, c = 0;
            if (!parseInt(seq, pos, r)) return false;
            if (pos >= seq.size() || seq[pos] != ';') return false;
            ++pos;
            if (!parseInt(seq, pos, c)) return false;

            EnvConfig::getInstance().setVtDimensions(r, c, -1, -1);
            buffer.erase(0, tPos + 1);
            return true;
        } else if (type == 4) { // \x1b[4;h;wt
            if (pos >= seq.size() || seq[pos] != ';') return false;
            ++pos;
            int h = 0, w = 0;
            if (!parseInt(seq, pos, h)) return false;
            if (pos >= seq.size() || seq[pos] != ';') return false;
            ++pos;
            if (!parseInt(seq, pos, w)) return false;

            EnvConfig::getInstance().setVtDimensions(-1, -1, w, h);
            buffer.erase(0, tPos + 1);
            return true;
        }

        return false;
    }

    bool InputController::tryParseMouseSGR()
    {
        // Format: ESC [ < b ; x ; y (M|m)
        // Need full sequence before consuming
        size_t mPos = buffer.find_first_of("Mm", 3);
        if (mPos == std::string::npos) return false; // incomplete

        std::string seq = buffer.substr(3, mPos - 3); // after ESC[
        size_t pos = 0; // start at b (we already skipped "<")
        int b = 0, x = 0, y = 0;
        if (!parseInt(seq, pos, b)) { buffer.erase(0, mPos + 1); return true; }
        if (pos >= seq.size() || seq[pos] != ';') { buffer.erase(0, mPos + 1); return true; }
        ++pos;
        if (!parseInt(seq, pos, x)) { buffer.erase(0, mPos + 1); return true; }
        if (pos >= seq.size() || seq[pos] != ';') { buffer.erase(0, mPos + 1); return true; }
        ++pos;
        if (!parseInt(seq, pos, y)) { buffer.erase(0, mPos + 1); return true; }

        bool press = (buffer[mPos] == 'M');
        bool release = (buffer[mPos] == 'm');
        bool isWheel = (b & 0x40) != 0;
        bool isMotion = (b & 0x20) != 0;
        
        // SGR Modifiers: Shift=4, Alt=8, Ctrl=16
        bool ctrl = (b & 0x10) != 0;
        bool alt = (b & 0x08) != 0;
        bool shift = (b & 0x04) != 0;

        int button = b & 0x03;
        int wheel = 0;
        if (isWheel) {
            wheel = (button == 0) ? 1 : (button == 1 ? -1 : 0); // 0: up,1:down
        }

        // Coordinates are 1-based per spec
        int col = std::max(0, x - 1);
        int row = std::max(0, y - 1);

        // Update EnvConfig with raw VT cell position (1-based)
        EnvConfig::getInstance().setMouseCellVt(static_cast<double>(x), static_cast<double>(y));

        emitMouse(col, row, button, press && !isWheel && !isMotion && !release, isMotion, wheel, ctrl, alt, shift);

        buffer.erase(0, mPos + 1);
        return true;
}

    void InputController::emitKey(InputKey key, char32_t ch, bool ctrl, bool alt, bool shift)
    {
        InputEvent ev;
        ev.type = InputEvent::Type::Key;
        ev.key = key;
        ev.ch = ch;
        ev.ctrl = ctrl;
        ev.alt = alt;
        ev.shift = shift;
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.push_back(ev);
    }

    void InputController::emitMouse(int x, int y, int button, bool pressed, bool move, int wheel, bool ctrl, bool alt, bool shift)
    {
        InputEvent ev;
        ev.type = InputEvent::Type::Mouse;
        ev.x = x;
        ev.y = y;
        ev.button = button;
        ev.pressed = pressed;
        ev.move = move;
        ev.wheel = wheel;
        ev.ctrl = ctrl;
        ev.alt = alt;
        ev.shift = shift;
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.push_back(ev);
    }

    void InputController::emitChar(char32_t ch)
    {
        emitKey(InputKey::Character, ch);
    }

#ifdef _WIN32
    bool InputController::enableVTInput()
    {
        if (hIn == INVALID_HANDLE_VALUE) return false;
        
        DWORD currentMode = 0;
        if (!GetConsoleMode(hIn, &currentMode)) return false;

        if (!modeSaved) {
            oldMode = currentMode;
            modeSaved = true;
        }

        DWORD newMode = currentMode;
        newMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_MOUSE_INPUT | ENABLE_QUICK_EDIT_MODE);
        newMode |= ENABLE_WINDOW_INPUT; // receive resize
        newMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        newMode |= ENABLE_PROCESSED_INPUT;

        if (!SetConsoleMode(hIn, newMode)) return false;

        // 启用鼠标 SGR 报告
        DWORD written = 0;
        const char* enableSeq = "\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h";
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), enableSeq, static_cast<DWORD>(strlen(enableSeq)), &written, nullptr);
        return true;
    }

    void InputController::restoreConsole()
    {
        if (hIn != INVALID_HANDLE_VALUE && modeSaved) {
            SetConsoleMode(hIn, oldMode);
            modeSaved = false;
            DWORD written = 0;
            const char* disableSeq = "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l";
            WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), disableSeq, static_cast<DWORD>(strlen(disableSeq)), &written, nullptr);
        }
    }
#endif

} // namespace TilelandWorld
