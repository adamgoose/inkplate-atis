// Fixed-cell bitmap fonts covering printable ASCII (see fonts.cpp, generated).
#pragma once
#include <stdint.h>

struct Font
{
    uint8_t cellWidth;
    uint8_t cellHeight;
    uint8_t bytesPerRow;
    /** Glyphs 0x20..0x7e, each cellHeight rows of bytesPerRow bytes, MSB-first. */
    const uint8_t *bitmap;
};

extern const Font FONT_TINY;   // 6x12
extern const Font FONT_SMALL;  // 8x16
extern const Font FONT_MEDIUM; // 12x24
extern const Font FONT_LARGE;  // 16x32
extern const Font FONT_HUGE;   // 32x64
