#pragma once
#ifndef TILELANDWORLD_UI_TUIUTILS_H
#define TILELANDWORLD_UI_TUIUTILS_H

#include <string>
#include <vector>
#include <cstdint>
#include "../TerrainTypes.h"

namespace TilelandWorld {
namespace UI {
namespace TuiUtils {

// Strip ANSI escape sequences from text.
std::string stripAnsiEscape(const std::string& s);

// Calculate visual width of UTF-8 text (accounts for box drawing, PUA, arrows, etc.).
size_t calculateUtf8VisualWidth(const std::string& s);

// Trim a UTF-8 string to a target visual width (preserves escape sequences).
std::string trimToUtf8VisualWidth(const std::string& s, size_t targetVisualWidth);

// Word wrap using visual width (preserves escape sequences).
std::vector<std::string> wordWrap(const std::string& text, size_t maxWidth);

// Color helpers
RGBColor blendColor(const RGBColor& from, const RGBColor& to, double t);
RGBColor lightenColor(const RGBColor& c, double ratio);

} // namespace TuiUtils
} // namespace UI
} // namespace TilelandWorld

#endif // TILELANDWORLD_UI_TUIUTILS_H
