#pragma once

#include "gfx/gfxFont.h"

#include <algorithm>
#include <string_view>

// Draws a menu field's label, left of its divider at `indent`. The pipe
// sprite's end cap covers the first 26px and the divider's cap starts at
// indent - 10, so the label starts past the end cap when it fits; a label too
// long for that slides left, at most back to the original 16px offset.
inline void drawFieldLabel(const gfxFont& font, int x, int y, int indent, std::string_view text)
{
    const int inset = std::clamp(indent - 10 - font.getWidth(text), 16, 26);
    font.drawChopRight(x + inset, y, indent + 8 - inset, text);
}
