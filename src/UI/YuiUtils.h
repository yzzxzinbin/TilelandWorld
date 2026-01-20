#pragma once
#ifndef TILELANDWORLD_UI_YUIUTILS_H
#define TILELANDWORLD_UI_YUIUTILS_H

#include "AnsiTui.h"

namespace TilelandWorld {
namespace UI {

inline const BoxStyle kFrame{"╭","╮","╰","╯","─","│"};

namespace YuiUtils {
    RGBColor darken(const RGBColor& c, double factor);
}
}
}

#endif
