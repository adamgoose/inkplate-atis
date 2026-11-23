#include "canvas.h"

#include <algorithm>
#include <math.h>

int roundHalfUp(double value) { return (int)floor(value + 0.5); }

void Canvas::set(int x, int y, Ink ink)
{
    if (x >= 0 && y >= 0 && x < width_ && y < height_)
        surface_.set(x, y, ink);
}

void Canvas::span(int y, int from, int to, Ink ink)
{
    for (int x = from; x < to; x++)
        surface_.set(x, y, ink);
}

void Canvas::fillRect(double x, double y, double w, double h, Ink ink)
{
    const int x0 = std::max(0, roundHalfUp(x));
    const int y0 = std::max(0, roundHalfUp(y));
    const int x1 = std::min(width_, roundHalfUp(x + w));
    const int y1 = std::min(height_, roundHalfUp(y + h));
    for (int py = y0; py < y1; py++)
        span(py, x0, x1, ink);
}

void Canvas::strokeRect(double x, double y, double w, double h, Ink ink, double thickness)
{
    fillRect(x, y, w, thickness, ink);
    fillRect(x, y + h - thickness, w, thickness, ink);
    fillRect(x, y, thickness, h, ink);
    fillRect(x + w - thickness, y, thickness, h, ink);
}

void Canvas::fillPolygon(const Point *points, int count, Ink ink)
{
    static const int MAX_POINTS = 8;
    if (count < 3 || count > MAX_POINTS)
        return;
    double minY = points[0].y, maxY = points[0].y;
    for (int i = 1; i < count; i++)
    {
        minY = std::min(minY, points[i].y);
        maxY = std::max(maxY, points[i].y);
    }
    const int yStart = std::max(0, (int)ceil(minY - 0.5));
    const int yEnd = std::min(height_ - 1, (int)ceil(maxY - 0.5) - 1);
    for (int py = yStart; py <= yEnd; py++)
    {
        const double sy = py + 0.5;
        double xs[MAX_POINTS];
        int crossings = 0;
        for (int i = 0; i < count; i++)
        {
            const Point &a = points[i];
            const Point &b = points[(i + 1) % count];
            if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy))
                xs[crossings++] = a.x + ((sy - a.y) / (b.y - a.y)) * (b.x - a.x);
        }
        for (int i = 1; i < crossings; i++)
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--)
                std::swap(xs[j - 1], xs[j]);
        for (int i = 0; i + 1 < crossings; i += 2)
        {
            const int from = std::max(0, (int)ceil(xs[i] - 0.5));
            const int to = std::min(width_, (int)ceil(xs[i + 1] - 0.5));
            span(py, from, to, ink);
        }
    }
}

void Canvas::line(Point a, Point b, Ink ink, double thickness)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = hypot(dx, dy);
    if (length == 0)
        return;
    const double nx = (-dy / length) * (thickness / 2);
    const double ny = (dx / length) * (thickness / 2);
    const Point corners[] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
    fillPolygon(corners, 4, ink);
}

void Canvas::fillCircle(double cx, double cy, double radius, Ink ink)
{
    for (int py = (int)floor(cy - radius); py <= (int)ceil(cy + radius); py++)
        for (int px = (int)floor(cx - radius); px <= (int)ceil(cx + radius); px++)
            if (hypot(px + 0.5 - cx, py + 0.5 - cy) <= radius)
                set(px, py, ink);
}

int Canvas::text(const Font &font, double x, double y, const std::string &text, Ink ink)
{
    int penX = roundHalfUp(x);
    const int top = roundHalfUp(y);
    for (const char raw : text)
    {
        const unsigned char c = (unsigned char)raw;
        const int index = (c >= 0x20 && c <= 0x7e ? c : '?') - 0x20;
        const uint8_t *glyph = font.bitmap + (size_t)index * font.cellHeight * font.bytesPerRow;
        for (int row = 0; row < font.cellHeight; row++)
            for (int col = 0; col < font.cellWidth; col++)
                if (glyph[row * font.bytesPerRow + (col >> 3)] & (0x80 >> (col & 7)))
                    set(penX + col, top + row, ink);
        penX += font.cellWidth;
    }
    return penX;
}
