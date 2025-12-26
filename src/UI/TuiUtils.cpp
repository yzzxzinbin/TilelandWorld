#include "TuiUtils.h"
#include <cctype>
#include <algorithm>

namespace TilelandWorld {
namespace UI {
namespace TuiUtils {

std::string stripAnsiEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1B') {
            if (i + 1 >= s.size()) break;
            if (s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == ';' || s[i] == '?')) ++i;
                if (i < s.size() && s[i] >= '@' && s[i] <= '~') ++i;
                continue;
            } else if (s[i + 1] == ']') {
                i += 2;
                while (i < s.size()) {
                    if (s[i] == '\x07') { ++i; break; }
                    if (s[i] == '\x1B' && i + 1 < s.size() && s[i + 1] == '\\') { i += 2; break; }
                    ++i;
                }
                continue;
            } else if (std::isalpha(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '7' || s[i + 1] == '8' || s[i + 1] == '=' || s[i + 1] == '>') {
                i += 2;
                continue;
            } else {
                ++i;
                continue;
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

size_t calculateUtf8VisualWidth(const std::string& s) {
    std::string str = stripAnsiEscape(s);
    size_t visual_width = 0;
    for (size_t i = 0; i < str.length();) {
        unsigned char byte = static_cast<unsigned char>(str[i]);
        size_t char_len = 1;
        size_t char_visual_width = 1;
        uint32_t codepoint = 0;

        if (byte < 0x80) {
            codepoint = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            char_len = 2;
            if (i + 1 < str.length() && (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x1F) << 6) | (static_cast<unsigned char>(str[i + 1]) & 0x3F);
                char_visual_width = 2;
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else if ((byte & 0xF0) == 0xE0) {
            char_len = 3;
            if (i + 2 < str.length() && (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x0F) << 12) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(str[i + 2]) & 0x3F);
                if (codepoint >= 0x2500 && codepoint <= 0x257F) {
                    char_visual_width = 1;
                } else {
                    char_visual_width = 2;
                }
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else if ((byte & 0xF8) == 0xF0) {
            char_len = 4;
            if (i + 3 < str.length() && (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80 && (static_cast<unsigned char>(str[i + 3]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x07) << 18) | ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(str[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(str[i + 3]) & 0x3F);
                char_visual_width = 2;
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else {
            char_len = 1; char_visual_width = 1; codepoint = byte;
        }

        if ((codepoint >= 0xE000 && codepoint <= 0xF8FF) ||
            (codepoint >= 0xF0000 && codepoint <= 0xFFFFD) ||
            (codepoint >= 0x100000 && codepoint <= 0x10FFFD)) {
            char_visual_width = 1;
        }
        if ((codepoint >= 0x0391 && codepoint <= 0x03A1) || (codepoint >= 0x03B1 && codepoint <= 0x03C1)) {
            char_visual_width = 1;
        }
        if ((codepoint >= 0x0041 && codepoint <= 0x005A) ||
            (codepoint >= 0x0061 && codepoint <= 0x007A) ||
            (codepoint >= 0x0080 && codepoint <= 0x00FF) ||
            (codepoint >= 0x0100 && codepoint <= 0x02AF)) {
            char_visual_width = 1;
        }
        if ((codepoint >= 0x2190 && codepoint <= 0x21FF) ||
            (codepoint >= 0x27F0 && codepoint <= 0x27FF) ||
            (codepoint >= 0x2B00 && codepoint <= 0x2BFF)) {
            char_visual_width = 1;
        }

        if (i + char_len > str.length()) {
            visual_width += (str.length() - i);
            break;
        }
        visual_width += char_visual_width;
        i += char_len;
    }
    return visual_width;
}

std::string trimToUtf8VisualWidth(const std::string& s, size_t targetVisualWidth) {
    if (targetVisualWidth == 0) return "";
    std::string result;
    result.reserve(s.length());
    size_t current_visual_width = 0;

    for (size_t i = 0; i < s.length();) {
        unsigned char byte = static_cast<unsigned char>(s[i]);
        size_t char_len = 1;
        size_t char_visual_width = 1;
        uint32_t codepoint = 0;

        if (byte < 0x80) {
            codepoint = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            char_len = 2;
            if (i + 1 < s.length() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
                char_visual_width = 2;
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else if ((byte & 0xF0) == 0xE0) {
            char_len = 3;
            if (i + 2 < s.length() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
                if (codepoint >= 0x2500 && codepoint <= 0x257F) {
                    char_visual_width = 1;
                } else {
                    char_visual_width = 2;
                }
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else if ((byte & 0xF8) == 0xF0) {
            char_len = 4;
            if (i + 3 < s.length() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[i + 3]) & 0xC0) == 0x80) {
                codepoint = ((byte & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
                char_visual_width = 2;
            } else {
                char_len = 1; char_visual_width = 1; codepoint = byte;
            }
        } else {
            codepoint = byte;
        }

        if (i + char_len > s.length()) break;
        if (current_visual_width + char_visual_width > targetVisualWidth) break;

        result.append(s, i, char_len);
        current_visual_width += char_visual_width;
        i += char_len;
    }
    return result;
}

std::vector<std::string> wordWrap(const std::string& text, size_t maxWidth) {
    std::vector<std::string> lines;
    if (maxWidth == 0) return lines;
    if (maxWidth > 1) maxWidth += 2;
    size_t pos = 0;
    size_t len = text.length();
    while (pos < len) {
        if (text[pos] == '\n') {
            lines.push_back("");
            ++pos;
            continue;
        }
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = len;
        size_t segStart = pos;
        while (segStart < lineEnd) {
            size_t i = segStart;
            size_t visualWidth = 0;
            std::string line;
            while (i < lineEnd && visualWidth < maxWidth) {
                if (text[i] == '\x1B' && i + 1 < lineEnd && text[i + 1] == '[') {
                    size_t escEnd = i + 2;
                    while (escEnd < lineEnd && (std::isdigit(static_cast<unsigned char>(text[escEnd])) || text[escEnd] == ';' || text[escEnd] == '?')) ++escEnd;
                    if (escEnd < lineEnd && text[escEnd] >= '@' && text[escEnd] <= '~') ++escEnd;
                    line.append(text, i, escEnd - i);
                    i = escEnd;
                    continue;
                }
                unsigned char c = text[i];
                size_t charLen = 1;
                if (c < 0x80) {
                } else if ((c & 0xE0) == 0xC0) {
                    charLen = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    charLen = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    charLen = 4;
                }
                if (i + charLen > lineEnd) break;
                std::string ch = text.substr(i, charLen);
                size_t chWidth = calculateUtf8VisualWidth(ch);
                if (visualWidth + chWidth > maxWidth) break;
                line += ch;
                visualWidth += chWidth;
                i += charLen;
            }
            lines.push_back(line);
            segStart = i;
        }
        pos = lineEnd + 1;
    }
    return lines;
}

RGBColor blendColor(const RGBColor& from, const RGBColor& to, double t) {
    double clamped = std::clamp(t, 0.0, 1.0);
    auto blendChannel = [clamped](uint8_t a, uint8_t b) {
        int val = static_cast<int>(a + (b - a) * clamped + 0.5);
        return static_cast<uint8_t>(std::clamp(val, 0, 255));
    };
    return { blendChannel(from.r, to.r), blendChannel(from.g, to.g), blendChannel(from.b, to.b) };
}

RGBColor lightenColor(const RGBColor& c, double ratio) {
    double t = std::clamp(ratio, 0.0, 1.0);
    auto lift = [t](uint8_t ch) {
        int val = static_cast<int>(ch + (255 - ch) * t + 0.5);
        return static_cast<uint8_t>(std::clamp(val, 0, 255));
    };
    return { lift(c.r), lift(c.g), lift(c.b) };
}

} // namespace TuiUtils
} // namespace UI
} // namespace TilelandWorld
