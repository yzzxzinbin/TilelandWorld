#define STB_TRUETYPE_IMPLEMENTATION
#include "YuiExporter.h"
#include "../third_party/stb_truetype.h"
#include "../Utils/EnvConfig.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <functional>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <cwchar>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    struct Mask8x8 {
        uint8_t data[64];
        bool active = false;
    };

    std::unordered_map<std::string, Mask8x8> createBlockMasks() {
        std::unordered_map<std::string, Mask8x8> masks;
        auto add = [&](const std::string& ch, std::function<bool(int, int)> f) {
            Mask8x8 m;
            m.active = true;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    m.data[y * 8 + x] = f(x, y) ? 255 : 0;
                }
            }
            masks[ch] = m;
        };

        add("█", [](int, int) { return true; });
        add(" ", [](int, int) { return false; });
        add("▀", [](int, int y) { return y < 4; });
        add("▄", [](int, int y) { return y >= 4; });
        add("▌", [](int x, int) { return x < 4; });
        add("▐", [](int x, int) { return x >= 4; });
        add("▘", [](int x, int y) { return x < 4 && y < 4; });
        add("▝", [](int x, int y) { return x >= 4 && y < 4; });
        add("▖", [](int x, int y) { return x < 4 && y >= 4; });
        add("▗", [](int x, int y) { return x >= 4 && y >= 4; });
        add("▚", [](int x, int y) { return (x < 4 && y < 4) || (x >= 4 && y >= 4); });
        add("▞", [](int x, int y) { return (x >= 4 && y < 4) || (x < 4 && y >= 4); });
        add("▙", [](int x, int y) { return !(x >= 4 && y < 4); });
        add("▛", [](int x, int y) { return !(x >= 4 && y >= 4); });
        add("▜", [](int x, int y) { return !(x < 4 && y >= 4); });
        add("▟", [](int x, int y) { return !(x < 4 && y < 4); });
        
        // 1/8 blocks
        add("▏", [](int x, int) { return x < 1; });
        add("▎", [](int x, int) { return x < 2; });
        add("▍", [](int x, int) { return x < 3; });
        add("▌", [](int x, int) { return x < 4; });
        add("▋", [](int x, int) { return x < 5; });
        add("▊", [](int x, int) { return x < 6; });
        add("▉", [](int x, int) { return x < 7; });
        add(" ", [](int, int) { return false; });
        
        add(" ", [](int, int y) { return y >= 8; });
        add("▁", [](int, int y) { return y >= 7; });
        add("▂", [](int, int y) { return y >= 6; });
        add("▃", [](int, int y) { return y >= 5; });
        add("▄", [](int, int y) { return y >= 4; });
        add("▅", [](int, int y) { return y >= 3; });
        add("▆", [](int, int y) { return y >= 2; });
        add("▇", [](int, int y) { return y >= 1; });

        return masks;
    }

    const std::unordered_map<std::string, Mask8x8>& getBlockMasks() {
        static auto masks = createBlockMasks();
        return masks;
    }

    uint32_t blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
        if (alpha == 0) return bg;
        if (alpha == 255) return fg;
        uint32_t rb = (bg & 0xFF00FF) + ((((fg & 0xFF00FF) - (bg & 0xFF00FF)) * alpha) >> 8);
        uint32_t g = (bg & 0x00FF00) + ((((fg & 0x00FF00) - (bg & 0x00FF00)) * alpha) >> 8);
        return (rb & 0xFF00FF) | (g & 0x00FF00);
    }
    
    uint32_t toARGB(RGBColor c, uint8_t a = 255) {
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
    }

#ifdef _WIN32
    class GdiPlusGuard {
    public:
        static bool ensure() {
            static GdiPlusGuard guard;
            return guard.started;
        }

    private:
        GdiPlusGuard() {
            Gdiplus::GdiplusStartupInput input;
            started = (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);
        }
        ~GdiPlusGuard() {
            if (started) {
                Gdiplus::GdiplusShutdown(token);
            }
        }
        ULONG_PTR token{0};
        bool started{false};
    };

    std::wstring utf8ToWide(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        if (len <= 0) return {};
        std::wstring ws(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), ws.data(), len);
        return ws;
    }

    bool getEncoderClsid(const WCHAR* format, CLSID& clsid) {
        UINT num = 0;
        UINT size = 0;
        if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
        std::vector<BYTE> buffer(size);
        auto info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
        if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(info[i].MimeType, format) == 0) {
                clsid = info[i].Clsid;
                return true;
            }
        }
        return false;
    }
#endif
}

bool YuiExporter::exportToImage(const YuiLayeredImage& image, const std::string& filename, Mode mode, Format format) {
    int imgW = image.getWidth();
    int imgH = image.getHeight();
    if (imgW <= 0 || imgH <= 0) return false;

    int cellW = 1, cellH = 1;
    if (mode == Mode::BlockToPixel) {
        cellW = 1;
        cellH = 2;
    } else {
        auto& env = EnvConfig::getInstance();
        auto runtime = env.getRuntimeInfo();
        auto stat = env.getStaticInfo();
        if (stat.isRunningInWT) {
            cellW = static_cast<int>(std::round(runtime.wtFontW));
            cellH = static_cast<int>(std::round(runtime.wtFontH));
        } else {
            cellW = stat.fontWidthWin;
            cellH = stat.fontHeightWin;
        }
    }
    
    if (cellW <= 0) cellW = 8;
    if (cellH <= 0) cellH = 16;

    int targetW = imgW * cellW;
    int targetH = imgH * cellH;
    std::vector<uint32_t> pixels(targetW * targetH, 0xFF000000);

    const auto& masks = getBlockMasks();
    
    // Font handling
    stbtt_fontinfo font;
    std::vector<uint8_t> fontData;
    bool fontLoaded = false;
    
    // Always try to load font for characters
    fontData = loadFontData("Consolas"); 
    if (!fontData.empty()) {
        if (stbtt_InitFont(&font, fontData.data(), 0)) {
            fontLoaded = true;
        }
    }

    float fontScale = fontLoaded ? stbtt_ScaleForPixelHeight(&font, (float)cellH) : 0;

    for (int y = 0; y < imgH; ++y) {
        for (int x = 0; x < imgW; ++x) {
            ImageCell cell = image.compositeCell(x, y);
            uint32_t fg = toARGB(cell.fg, cell.fgA);
            uint32_t bg = toARGB(cell.bg, cell.bgA);

            auto it = masks.find(cell.character);
            if (it != masks.end()) {
                const Mask8x8& m = it->second;
                for (int cy = 0; cy < cellH; ++cy) {
                    for (int cx = 0; cx < cellW; ++cx) {
                        int mx = (cx * 8) / cellW;
                        int my = (cy * 8) / cellH;
                        uint8_t alpha = m.data[std::clamp(my, 0, 7) * 8 + std::clamp(mx, 0, 7)];
                        pixels[(y * cellH + cy) * targetW + (x * cellW + cx)] = blend(bg, fg, alpha);
                    }
                }
            } else if (fontLoaded && cell.character != " " && !cell.character.empty()) {
                // Render as text
                for (int cy = 0; cy < cellH; ++cy) {
                    for (int cx = 0; cx < cellW; ++cx) {
                        pixels[(y * cellH + cy) * targetW + (x * cellW + cx)] = bg;
                    }
                }

                // Simple UTF-8 to Codepoint
                uint32_t codepoint = 0;
                size_t csize = cell.character.size();
                unsigned char c0 = (unsigned char)cell.character[0];
                if (c0 < 0x80) codepoint = c0;
                else if ((c0 & 0xE0) == 0xC0 && csize >= 2) codepoint = ((c0 & 0x1F) << 6) | (cell.character[1] & 0x3F);
                else if ((c0 & 0xF0) == 0xE0 && csize >= 3) codepoint = ((c0 & 0x0F) << 12) | ((cell.character[1] & 0x3F) << 6) | (cell.character[2] & 0x3F);
                else if ((c0 & 0xF8) == 0xF0 && csize >= 4) codepoint = ((c0 & 0x07) << 18) | ((cell.character[1] & 0x3F) << 12) | ((cell.character[2] & 0x3F) << 6) | (cell.character[3] & 0x3F);


                int w, h, xoff, yoff;
                unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0, fontScale, codepoint, &w, &h, &xoff, &yoff);
                if (bitmap) {
                    int ascent;
                    stbtt_GetFontVMetrics(&font, &ascent, 0, 0);
                    int baseline = (int)(ascent * fontScale);
                    int startX = x * cellW + xoff + (cellW - w) / 2; // center horizontal
                    int startY = y * cellH + baseline + yoff;
                    
                    for (int by = 0; by < h; ++by) {
                        for (int bx = 0; bx < w; ++bx) {
                            int px = startX + bx;
                            int py = startY + by;
                            if (px >= x * cellW && px < (x + 1) * cellW && py >= y * cellH && py < (y + 1) * cellH) {
                                uint8_t alpha = bitmap[by * w + bx];
                                uint32_t currentBg = pixels[py * targetW + px];
                                pixels[py * targetW + px] = blend(currentBg, fg, alpha);
                            }
                        }
                    }
                    stbtt_FreeBitmap(bitmap, 0);
                }
            } else {
                // fallback to bg
                for (int cy = 0; cy < cellH; ++cy) {
                    for (int cx = 0; cx < cellW; ++cx) {
                        pixels[(y * cellH + cy) * targetW + (x * cellW + cx)] = bg;
                    }
                }
            }
        }
    }

    if (format == Format::BMP) {
        return saveBmp(filename, targetW, targetH, pixels);
    }
    return saveWithEncoder(filename, targetW, targetH, pixels, format);
}

bool YuiExporter::saveBmp(const std::string& filename, int w, int h, const std::vector<uint32_t>& pixels) {
    std::ofstream f(filename, std::ios::binary);
    if (!f) return false;

    uint32_t fileSize = 54 + w * h * 4;
    uint32_t offset = 54;
    uint32_t headerSize = 40;
    uint16_t planes = 1;
    uint16_t bpp = 32;

    f.write("BM", 2);
    f.write((char*)&fileSize, 4);
    uint32_t res = 0;
    f.write((char*)&res, 4);
    f.write((char*)&offset, 4);

    f.write((char*)&headerSize, 4);
    f.write((char*)&w, 4);
    int32_t h_signed = -h; 
    f.write((char*)&h_signed, 4);
    f.write((char*)&planes, 2);
    f.write((char*)&bpp, 2);
    uint32_t comp = 0;
    f.write((char*)&comp, 4);
    uint32_t imgSize = w * h * 4;
    f.write((char*)&imgSize, 4);
    int32_t ppm = 2835; 
    f.write((char*)&ppm, 4);
    f.write((char*)&ppm, 4);
    uint32_t clr = 0;
    f.write((char*)&clr, 4);
    f.write((char*)&clr, 4);

    for (int i = 0; i < w * h; ++i) {
        uint32_t p = pixels[i];
        uint8_t a = (p >> 24) & 0xFF;
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = p & 0xFF;
        f.write((char*)&b, 1);
        f.write((char*)&g, 1);
        f.write((char*)&r, 1);
        f.write((char*)&a, 1);
    }
    return true;
}

bool YuiExporter::saveWithEncoder(const std::string& filename, int w, int h, const std::vector<uint32_t>& pixels, Format format) {
#ifdef _WIN32
    if (format == Format::BMP) {
        return saveBmp(filename, w, h, pixels);
    }

    const WCHAR* mime = (format == Format::PNG) ? L"image/png" : L"image/jpeg";
    if (!GdiPlusGuard::ensure()) return false;

    CLSID clsid;
    if (!getEncoderClsid(mime, clsid)) return false;

    std::wstring wpath = utf8ToWide(filename);
    if (wpath.empty()) return false;

    Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppARGB, reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels.data())));
    return bmp.Save(wpath.c_str(), &clsid, nullptr) == Gdiplus::Ok;
#else
    (void)filename; (void)w; (void)h; (void)pixels; (void)format;
    return false;
#endif
}

std::vector<uint8_t> YuiExporter::loadFontData(const std::string& fontName) {
    std::vector<std::string> paths = {
        "C:\\Windows\\Fonts\\SauceCodeProNerdFont-Regular.ttf",
        "C:\\Windows\\Fonts\\JetBrainsMonoNerdFont-Regular.ttf",
        "C:\\Windows\\Fonts\\consola.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\msgothic.ttc"
    };
    
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) {
            // Use ios::ate to seek to end for size query
            std::ifstream f(p, std::ios::binary | std::ios::ate);
            if (f) {
                size_t size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<uint8_t> data(size);
                f.read((char*)data.data(), size);
                return data;
            }
        }
    }
    return {};
}

} // namespace UI
} // namespace TilelandWorld
