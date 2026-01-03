#include "TuiUtils.h"
#include <cctype>
#include <algorithm>
#include <cmath>

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
    // Treat certain geometric/arrow symbols as width 1 (e.g., ●, •, ▶, ▲, ▼, ◀)
    if (codepoint == 0x25CF || codepoint == 0x2022 || codepoint == 0x25B6 || codepoint == 0x25B2 || codepoint == 0x25BC || codepoint == 0x25C0) {
        visual = 1;
    }

    info.length = charLen;
    info.visualWidth = visual;
    return info;
}

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
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

RGBColor hsvToRgb(double h, double s, double v) {
    double hh = std::fmod(h, 360.0);
    if (hh < 0) hh += 360.0;
    double ss = std::clamp(s, 0.0, 1.0);
    double vv = std::clamp(v, 0.0, 1.0);

    double c = vv * ss;
    double x = c * (1.0 - std::fabs(std::fmod(hh / 60.0, 2.0) - 1.0));
    double m = vv - c;

    double r = 0, g = 0, b = 0;
    if (hh < 60) { r = c; g = x; b = 0; }
    else if (hh < 120) { r = x; g = c; b = 0; }
    else if (hh < 180) { r = 0; g = c; b = x; }
    else if (hh < 240) { r = 0; g = x; b = c; }
    else if (hh < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    auto toByte = [&](double vch) {
        int val = static_cast<int>((vch + m) * 255.0 + 0.5);
        return static_cast<uint8_t>(std::clamp(val, 0, 255));
    };
    return { toByte(r), toByte(g), toByte(b) };
}

void rgbToHsv(const RGBColor& rgb, double& h, double& s, double& v) {
    double r = rgb.r / 255.0;
    double g = rgb.g / 255.0;
    double b = rgb.b / 255.0;

    double maxc = std::max({r, g, b});
    double minc = std::min({r, g, b});
    double delta = maxc - minc;

    v = maxc;
    s = (maxc <= 0.0) ? 0.0 : (delta / maxc);

    if (delta < 1e-6) {
        h = 0.0;
        return;
    }

    if (maxc == r) {
        h = 60.0 * std::fmod(((g - b) / delta), 6.0);
    } else if (maxc == g) {
        h = 60.0 * (((b - r) / delta) + 2.0);
    } else {
        h = 60.0 * (((r - g) / delta) + 4.0);
    }
    if (h < 0.0) h += 360.0;
}

std::string base64Encode(const std::string& in) {
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(lookup[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

} // namespace TuiUtils
} // namespace UI
} // namespace TilelandWorld
