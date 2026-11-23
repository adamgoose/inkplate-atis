// Drawing primitives over a bare pixel sink, so the same renderer feeds the
// Inkplate frame buffer on the device and a PNG in the simulator.
#pragma once
#include <stdint.h>
#include <string>

#include "fonts.h"

/** The seven inks of the Inkplate 6COLOR; values match the library's INKPLATE_* constants. */
enum Ink : uint8_t
{
    INK_BLACK = 0,
    INK_WHITE = 1,
    INK_GREEN = 2,
    INK_BLUE = 3,
    INK_RED = 4,
    INK_YELLOW = 5,
    INK_ORANGE = 6
};

/** Where pixels end up. Coordinates passed to set() are always inside the canvas. */
class Surface
{
  public:
    virtual ~Surface() {}
    virtual void set(int x, int y, Ink ink) = 0;
};

struct Point
{
    double x, y;
};

class Canvas
{
  public:
    Canvas(Surface &surface, int width, int height) : surface_(surface), width_(width), height_(height) {}

    void set(int x, int y, Ink ink);
    void fillRect(double x, double y, double w, double h, Ink ink);
    void strokeRect(double x, double y, double w, double h, Ink ink, double thickness = 1);
    /** Even-odd scanline fill, sampling at pixel centres. */
    void fillPolygon(const Point *points, int count, Ink ink);
    void line(Point a, Point b, Ink ink, double thickness = 1);
    void fillCircle(double cx, double cy, double radius, Ink ink);
    /** Draws `text` with its top-left corner at (x, y). Returns the x after the last glyph. */
    int text(const Font &font, double x, double y, const std::string &text, Ink ink);

  private:
    void span(int y, int from, int to, Ink ink);

    Surface &surface_;
    int width_;
    int height_;
};

inline int textWidth(const Font &font, const std::string &text) { return (int)text.size() * font.cellWidth; }

/** Rounds half up like JavaScript's Math.round, which the layout was tuned with. */
int roundHalfUp(double value);
