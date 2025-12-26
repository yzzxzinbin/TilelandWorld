#include "TuiUtils.h"
#include <cctype>
#include <algorithm>

namespace TilelandWorld {
namespace UI {
namespace TuiUtils {

Utf8CharInfo nextUtf8Char(const std::string& s, size_t pos) {
    Utf8CharInfo info{};
    if (pos >= s.size()) return info;

    unsigned char byte = static_cast<unsigned char>(s[pos]);
    size_t charLen = 1;
    uint32_t codepoint = byte;
    size_t visual = 1;

    if ((byte & 0xE0) == 0xC0) {
        if (pos + 1 < s.size() && (static_cast<unsigned char>(s[pos + 1]) & 0xC0) == 0x80) {
            charLen = 2;
            codepoint = ((byte & 0x1F) << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
            visual = 2;
        }
    } else if ((byte & 0xF0) == 0xE0) {
        if (pos + 2 < s.size() && (static_cast<unsigned char>(s[pos + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[pos + 2]) & 0xC0) == 0x80) {
            charLen = 3;
            codepoint = ((byte & 0x0F) << 12) | ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
            if ((codepoint >= 0x2500 && codepoint <= 0x257F) || (codepoint >= 0x2580 && codepoint <= 0x259F)) {
                visual = 1;
            } else {
                visual = 2;
            }
        }
    } else if ((byte & 0xF8) == 0xF0) {
        if (pos + 3 < s.size() && (static_cast<unsigned char>(s[pos + 1]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[pos + 2]) & 0xC0) == 0x80 && (static_cast<unsigned char>(s[pos + 3]) & 0xC0) == 0x80) {
            charLen = 4;
            codepoint = ((byte & 0x07) << 18) | ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
            visual = 2;
        }
    }

    // Adjust for ranges treated as width 1
    if ((codepoint >= 0xE000 && codepoint <= 0xF8FF) ||
        (codepoint >= 0xF0000 && codepoint <= 0xFFFFD) ||
        (codepoint >= 0x100000 && codepoint <= 0x10FFFD)) {
        visual = 1;
    }
    if ((codepoint >= 0x0391 && codepoint <= 0x03A1) || (codepoint >= 0x03B1 && codepoint <= 0x03C1)) {
        visual = 1;
    }
    if ((codepoint >= 0x0041 && codepoint <= 0x005A) ||
        (codepoint >= 0x0061 && codepoint <= 0x007A) ||
        (codepoint >= 0x0080 && codepoint <= 0x00FF) ||
        (codepoint >= 0x0100 && codepoint <= 0x02AF)) {
        visual = 1;
    }
    if ((codepoint >= 0x2190 && codepoint <= 0x21FF) ||
        (codepoint >= 0x27F0 && codepoint <= 0x27FF) ||
        (codepoint >= 0x2B00 && codepoint <= 0x2BFF)) {
        visual = 1;
    }

    info.length = charLen;
    info.visualWidth = visual;
    return info;
}

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
        auto info = nextUtf8Char(str, i);
        if (info.length == 0) break;
        visual_width += info.visualWidth;
        i += info.length;
    }
    return visual_width;
}

std::string trimToUtf8VisualWidth(const std::string& s, size_t targetVisualWidth) {
    if (targetVisualWidth == 0) return "";
    std::string result;
    result.reserve(s.length());
    size_t current_visual_width = 0;

    for (size_t i = 0; i < s.length();) {
        auto info = nextUtf8Char(s, i);
        if (info.length == 0) break;
        if (current_visual_width + info.visualWidth > targetVisualWidth) break;

        result.append(s, i, info.length);
        current_visual_width += info.visualWidth;
        i += info.length;
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
