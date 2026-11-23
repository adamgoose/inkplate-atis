#include "render.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>

namespace
{

const int HEADER_HEIGHT = 40;
const struct
{
    int x, y, width, height;
} DIAGRAM = {0, HEADER_HEIGHT, 300, 304};
const struct
{
    int x, y, width;
} PANEL = {306, HEADER_HEIGHT + 8, 288};
const int NOTES_TOP = 350;

bool contains(const std::vector<std::string> &list, const char *name)
{
    return std::find(list.begin(), list.end(), name) != list.end();
}

std::string join(const std::vector<std::string> &items, const char *separator)
{
    std::string out;
    for (size_t i = 0; i < items.size(); i++)
        out += (i == 0 ? "" : separator) + items[i];
    return out;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void drawHeader(Canvas &canvas, const Airport &airport, const AtisState *atis, bool lowBattery)
{
    canvas.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, INK_BLACK);
    const int afterIdent = canvas.text(FONT_LARGE, 10, 4, airport.ident, INK_WHITE);

    const std::string right = atis != nullptr && !atis->broadcasts.empty() ? atis->broadcasts[0].time + "Z" : "";
    const int rightX = SCREEN_WIDTH - 10 - textWidth(FONT_LARGE, right);
    canvas.text(FONT_LARGE, rightX, 4, right, INK_WHITE);

    if (lowBattery)
    {
        const std::string label = "LOW BATT";
        const int x = rightX - 16 - textWidth(FONT_SMALL, label) - 8;
        canvas.fillRect(x, 10, textWidth(FONT_SMALL, label) + 8, 20, INK_RED);
        canvas.text(FONT_SMALL, x + 4, 12, label, INK_WHITE);
    }

    const int nameRoom = (rightX - afterIdent - 28 - (lowBattery ? 90 : 0)) / FONT_SMALL.cellWidth;
    canvas.text(FONT_SMALL, afterIdent + 12, 13, std::string(airport.name).substr(0, std::max(0, nameRoom)), INK_WHITE);
}

// ---------------------------------------------------------------------------
// Runway diagram
// ---------------------------------------------------------------------------

const int DIAGRAM_MARGIN = 52;
const int ARROW_GAP = 5;
const int ARROW_LENGTH = 24;

struct Box
{
    double x, y, width, height;
};

enum LabelStyle
{
    LABEL_ACTIVE = 0,
    LABEL_CLOSED = 1,
    LABEL_IDLE = 2
};

struct Label
{
    const char *name;
    Point anchor;
    /** Unit vectors away from and across the runway, used to step out of collisions. */
    Point out;
    Point side;
    int width;
    int height;
    LabelStyle style;
};

double clamp(double value, double low, double high) { return std::max(low, std::min(high, value)); }

/** True when the boxes come within `gap` pixels of each other. */
bool overlaps(const Box &a, const Box &b, double gap = 3)
{
    return a.x < b.x + b.width + gap && b.x < a.x + a.width + gap && a.y < b.y + b.height + gap &&
           b.y < a.y + a.height + gap;
}

/** Moves `from` a few pixels towards `to`. */
Point shift(Point from, Point to, double distance)
{
    const double length = hypot(to.x - from.x, to.y - from.y);
    return {from.x + ((to.x - from.x) / length) * distance, from.y + ((to.y - from.y) / length) * distance};
}

void drawClosedMarks(Canvas &canvas, Point a, Point b, double width)
{
    const double size = std::max(5.0, width / 2 + 3);
    for (const double t : {0.2, 0.5, 0.8})
    {
        const double x = a.x + (b.x - a.x) * t;
        const double y = a.y + (b.y - a.y) * t;
        canvas.line({x - size, y - size}, {x + size, y + size}, INK_RED, 3);
        canvas.line({x - size, y + size}, {x + size, y - size}, INK_RED, 3);
    }
}

void drawArrow(Canvas &canvas, Point tail, Point tip, Ink ink, double shaft = 5, double head = 15,
               double headLength = 12)
{
    const double length = hypot(tip.x - tail.x, tip.y - tail.y);
    const double ux = (tip.x - tail.x) / length;
    const double uy = (tip.y - tail.y) / length;
    const auto at = [&](double distance, double side) {
        return Point{tail.x + ux * distance - uy * side, tail.y + uy * distance + ux * side};
    };
    const double neck = length - headLength;
    const Point outline[] = {at(0, shaft / 2),    at(neck, shaft / 2),   at(neck, head / 2), at(length, 0),
                             at(neck, -head / 2), at(neck, -shaft / 2), at(0, -shaft / 2)};
    canvas.fillPolygon(outline, 7, ink);
}

void drawNorthMark(Canvas &canvas, double x, double y)
{
    drawArrow(canvas, {x, y + 12}, {x, y - 12}, INK_BLACK, 3, 11, 10);
    canvas.text(FONT_SMALL, x - 4, y + 14, "N", INK_BLACK);
}

/** Label offsets (sideways, outwards) in pixels, nearest first. */
std::vector<Point> labelNudges()
{
    std::vector<Point> nudges;
    for (const double outwards : {0, 12, 24, 36})
        for (const double sideways : {0, 14, -14, 28, -28, 42, -42})
            nudges.push_back({sideways, outwards});
    std::stable_sort(nudges.begin(), nudges.end(),
                     [](const Point &a, const Point &b) { return hypot(a.x, a.y) < hypot(b.x, b.y); });
    return nudges;
}

void drawDiagram(Canvas &canvas, const Airport &airport, const AtisState *atis)
{
    if (airport.pavementCount == 0)
        return;
    static const std::vector<std::string> NO_RUNWAYS;
    const std::vector<std::string> &landing = atis != nullptr ? atis->runways.landing : NO_RUNWAYS;
    const std::vector<std::string> &departing = atis != nullptr ? atis->runways.departing : NO_RUNWAYS;
    const std::vector<std::string> &closed = atis != nullptr ? atis->runways.closed : NO_RUNWAYS;

    double minX = airport.pavements[0].ends[0].x, maxX = minX;
    double minY = airport.pavements[0].ends[0].y, maxY = minY;
    for (int i = 0; i < airport.pavementCount; i++)
    {
        for (const PavementEnd &end : airport.pavements[i].ends)
        {
            minX = std::min(minX, end.x);
            maxX = std::max(maxX, end.x);
            minY = std::min(minY, end.y);
            maxY = std::max(maxY, end.y);
        }
    }
    const double scale = std::min((DIAGRAM.width - 2 * DIAGRAM_MARGIN) / std::max(1.0, maxX - minX),
                                  (DIAGRAM.height - 2 * DIAGRAM_MARGIN) / std::max(1.0, maxY - minY));
    const double centreX = DIAGRAM.x + DIAGRAM.width / 2.0;
    const double centreY = DIAGRAM.y + DIAGRAM.height / 2.0;
    // North up: screen y grows downwards.
    const auto project = [&](const PavementEnd &end) {
        return Point{centreX + (end.x - (minX + maxX) / 2) * scale, centreY - (end.y - (minY + maxY) / 2) * scale};
    };

    const auto isClosed = [&](const Pavement &strip) {
        return contains(closed, strip.ends[0].name) || contains(closed, strip.ends[1].name);
    };
    const auto isActive = [&](const Pavement &strip) {
        for (const PavementEnd &end : strip.ends)
            if (contains(landing, end.name) || contains(departing, end.name))
                return true;
        return false;
    };

    // Idle strips first so the runways in use are drawn over their intersections.
    const auto rank = [&](const Pavement *strip) { return isClosed(*strip) ? 1 : isActive(*strip) ? 2 : 0; };
    std::vector<const Pavement *> ordered;
    for (int i = 0; i < airport.pavementCount; i++)
        ordered.push_back(&airport.pavements[i]);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const Pavement *a, const Pavement *b) { return rank(a) < rank(b); });
    for (const Pavement *strip : ordered)
    {
        const Point a = project(strip->ends[0]);
        const Point b = project(strip->ends[1]);
        const double width = std::max(7.0, strip->widthFt * scale);
        if (isClosed(*strip))
        {
            canvas.line(a, b, INK_RED, width);
            canvas.line(a, b, INK_WHITE, width - 4);
            drawClosedMarks(canvas, a, b, width);
        }
        else if (isActive(*strip))
        {
            canvas.line(a, b, INK_BLACK, width);
        }
        else
        {
            canvas.line(a, b, INK_BLACK, width);
            canvas.line(shift(a, b, 1), shift(b, a, 1), INK_WHITE, width - 2);
        }
    }

    // The north mark and AIRAC caption are fixed; labels must stay clear of them.
    std::vector<Box> occupied = {{DIAGRAM.x + DIAGRAM.width - 30.0, DIAGRAM.y + 8.0, 24, 50},
                                 {DIAGRAM.x + 4.0, DIAGRAM.y + DIAGRAM.height - 18.0, 70, 16}};
    std::vector<Label> labels;
    for (int i = 0; i < airport.pavementCount; i++)
    {
        const Pavement &strip = airport.pavements[i];
        for (int e = 0; e < 2; e++)
        {
            const PavementEnd &end = strip.ends[e];
            const PavementEnd &other = strip.ends[1 - e];
            const Point here = project(end);
            const Point there = project(other);
            const double length = hypot(there.x - here.x, there.y - here.y);
            // Unit vector pointing away from the pavement at this end.
            const Point out = {(here.x - there.x) / length, (here.y - there.y) / length};
            const auto along = [&](double distance) {
                return Point{here.x + out.x * distance, here.y + out.y * distance};
            };

            const bool lands = contains(landing, end.name) && !contains(closed, end.name);
            const bool departsOver = contains(departing, other.name) && !contains(closed, other.name);
            if (lands)
                drawArrow(canvas, along(ARROW_GAP + ARROW_LENGTH), along(ARROW_GAP), INK_GREEN);
            else if (departsOver)
                drawArrow(canvas, along(ARROW_GAP), along(ARROW_GAP + ARROW_LENGTH), INK_BLUE);
            if (lands || departsOver)
            {
                const Point from = along(ARROW_GAP);
                const Point to = along(ARROW_GAP + ARROW_LENGTH);
                occupied.push_back({std::min(from.x, to.x) - 6, std::min(from.y, to.y) - 6, fabs(from.x - to.x) + 12,
                                    fabs(from.y - to.y) + 12});
            }

            const int boxWidth = textWidth(FONT_SMALL, end.name) + 6;
            const int boxHeight = FONT_SMALL.cellHeight + 2;
            const double clearance = (lands || departsOver ? ARROW_GAP + ARROW_LENGTH : 0) + 5 +
                                     (fabs(out.x) * boxWidth + fabs(out.y) * boxHeight) / 2;
            const bool inUse = contains(landing, end.name) || contains(departing, end.name);
            labels.push_back({end.name, along(clearance), out, {-out.y, out.x}, boxWidth, boxHeight,
                              contains(closed, end.name) ? LABEL_CLOSED : inUse ? LABEL_ACTIVE : LABEL_IDLE});
        }
    }

    // Labels of runways in use keep their natural spot; the rest step sideways out of the way.
    std::stable_sort(labels.begin(), labels.end(), [](const Label &a, const Label &b) { return a.style < b.style; });
    const std::vector<Point> nudges = labelNudges();
    for (const Label &label : labels)
    {
        Box chosen = {0, 0, 0, 0};
        bool found = false;
        for (size_t n = 0; n < nudges.size() && !found; n++)
        {
            const double sideways = nudges[n].x, outwards = nudges[n].y;
            const Box candidate = {
                clamp(roundHalfUp(label.anchor.x + label.side.x * sideways + label.out.x * outwards - label.width / 2.0),
                      DIAGRAM.x + 2, DIAGRAM.x + DIAGRAM.width - label.width - 3),
                clamp(roundHalfUp(label.anchor.y + label.side.y * sideways + label.out.y * outwards - label.height / 2.0),
                      DIAGRAM.y + 2, DIAGRAM.y + DIAGRAM.height - label.height - 2),
                (double)label.width, (double)label.height};
            if (n == 0)
                chosen = candidate;
            found = true;
            for (const Box &other : occupied)
                found = found && !overlaps(candidate, other);
            if (found)
                chosen = candidate;
        }
        occupied.push_back(chosen);
        if (label.style == LABEL_ACTIVE)
            canvas.fillRect(chosen.x, chosen.y, chosen.width, chosen.height, INK_BLACK);
        const Ink ink = label.style == LABEL_ACTIVE ? INK_WHITE : label.style == LABEL_CLOSED ? INK_RED : INK_BLACK;
        canvas.text(FONT_SMALL, chosen.x + 3, chosen.y + 1, label.name, ink);
    }

    canvas.text(FONT_TINY, DIAGRAM.x + 6, DIAGRAM.y + DIAGRAM.height - 16, std::string("AIRAC ") + airport.airac,
                INK_BLACK);
    drawNorthMark(canvas, DIAGRAM.x + DIAGRAM.width - 18, DIAGRAM.y + 24);
}

// ---------------------------------------------------------------------------
// Right-hand panel
// ---------------------------------------------------------------------------

const struct
{
    int width, height, gap;
} BOX = {92, 92, 6};

void drawWindRose(Canvas &canvas, double cx, double cy, const Wind &wind)
{
    const double radius = 44;
    canvas.fillCircle(cx, cy, radius, INK_BLACK);
    canvas.fillCircle(cx, cy, radius - 2, INK_WHITE);
    for (int degrees = 0; degrees < 360; degrees += 30)
    {
        const double radians = (degrees * M_PI) / 180;
        const double inner = degrees % 90 == 0 ? radius - 9 : radius - 6;
        canvas.line({cx + sin(radians) * inner, cy - cos(radians) * inner},
                    {cx + sin(radians) * (radius - 1), cy - cos(radians) * (radius - 1)}, INK_BLACK,
                    degrees % 90 == 0 ? 3 : 1);
    }
    if (!wind.known)
        return;
    if (wind.speed == 0)
    {
        canvas.text(FONT_SMALL, cx - 16, cy - 8, "CALM", INK_BLACK);
        return;
    }
    if (!wind.hasDirection)
    {
        canvas.text(FONT_SMALL, cx - 12, cy - 8, "VRB", INK_BLACK);
        return;
    }
    // The arrow flies with the wind: tail at the direction it blows from.
    const double radians = (wind.direction * M_PI) / 180;
    const double reach = radius - 12;
    const Point from = {cx + sin(radians) * reach, cy - cos(radians) * reach};
    const Point to = {cx - sin(radians) * reach, cy + cos(radians) * reach};
    drawArrow(canvas, from, to, INK_BLACK, 9, 23, 18);
    drawArrow(canvas, shift(from, to, 2), shift(to, from, 3), INK_ORANGE, 5, 17, 13);
}

void drawBroadcasts(Canvas &canvas, const AtisState &atis)
{
    for (size_t i = 0; i < atis.broadcasts.size() && i < 2; i++)
    {
        const Broadcast &broadcast = atis.broadcasts[i];
        const int x = PANEL.x + (int)i * (BOX.width + BOX.gap);
        const bool arrival = broadcast.type == "arr", departure = broadcast.type == "dep";
        const std::string label = arrival ? "ARR" : departure ? "DEP" : "ATIS";
        const Ink ink = arrival ? INK_GREEN : departure ? INK_BLUE : INK_BLACK;
        canvas.strokeRect(x, PANEL.y, BOX.width, BOX.height, ink, 2);
        canvas.fillRect(x, PANEL.y, BOX.width, 20, ink);
        canvas.text(FONT_SMALL, x + (BOX.width - textWidth(FONT_SMALL, label)) / 2.0, PANEL.y + 2, label, INK_WHITE);
        canvas.text(FONT_HUGE, x + (BOX.width - FONT_HUGE.cellWidth) / 2.0, PANEL.y + 24, broadcast.code.substr(0, 1),
                    INK_BLACK);
    }
    drawWindRose(canvas, PANEL.x + 2 * (BOX.width + BOX.gap) + BOX.width / 2.0, PANEL.y + BOX.height / 2.0,
                 atis.weather.wind);
}

const struct
{
    int top, pitch, valueX;
} ROW = {PANEL.y + BOX.height + 8, 25, PANEL.x + 48};

std::string formatWind(const Wind &wind)
{
    if (!wind.known)
        return "--";
    if (wind.speed == 0)
        return "CALM";
    char text[24];
    if (wind.hasDirection)
        snprintf(text, sizeof(text), "%03d/%d", wind.direction, wind.speed);
    else
        snprintf(text, sizeof(text), "VRB/%d", wind.speed);
    std::string out = text;
    if (wind.hasGust)
        out += "G" + std::to_string(wind.gust);
    return out + "KT";
}

struct Fitted
{
    const Font *font;
    std::string text;
};

/** Picks the largest font in which `text` fits `width`, truncating as a last resort. */
Fitted fit(const std::string &text, int width, std::initializer_list<const Font *> candidates)
{
    const Font *last = nullptr;
    for (const Font *font : candidates)
    {
        if (textWidth(*font, text) <= width)
            return {font, text};
        last = font;
    }
    return {last, text.substr(0, width / last->cellWidth)};
}

void drawWeather(Canvas &canvas, const Weather &weather)
{
    std::string visibility = weather.visibility.empty() ? "--" : weather.visibility + "SM";
    for (const std::string &phenomenon : weather.phenomena)
        visibility += " " + phenomenon;
    const std::string temperature =
        weather.hasTemperature ? std::to_string(weather.temperature) + "/" + std::to_string(weather.dewpoint) + " C"
                               : "--/-- C";
    const std::pair<const char *, std::string> rows[] = {
        {"WIND", formatWind(weather.wind)},
        {"VIS", visibility},
        {"SKY", weather.clouds.empty() ? "--" : join(weather.clouds, " ")},
        {"TEMP", temperature},
        {"ALTM", weather.altimeter.empty() ? "--" : weather.altimeter}};
    int i = 0;
    for (const auto &row : rows)
    {
        const int y = ROW.top + i++ * ROW.pitch;
        canvas.text(FONT_SMALL, PANEL.x, y + 6, row.first, INK_BLACK);
        const Fitted fitted =
            fit(row.second, PANEL.x + PANEL.width - ROW.valueX, {&FONT_MEDIUM, &FONT_SMALL, &FONT_TINY});
        canvas.text(*fitted.font, ROW.valueX, y + (FONT_MEDIUM.cellHeight - fitted.font->cellHeight) / 2.0, fitted.text,
                    INK_BLACK);
    }
}

const struct
{
    int top, pitch, tagWidth;
} USE = {ROW.top + 5 * ROW.pitch + 4, 20, 42};

void drawRunwayUse(Canvas &canvas, const RunwayUse &runways)
{
    struct Row
    {
        std::string tag;
        Ink ink;
        std::string value;
    };
    const std::string approaches = runways.approaches.empty() ? "" : "  " + join(runways.approaches, "/");
    std::vector<Row> rows = {
        {"LDG", INK_GREEN, runways.landing.empty() ? "--" : join(runways.landing, " ") + approaches},
        {"DEP", INK_BLUE, runways.departing.empty() ? "--" : join(runways.departing, " ")}};
    if (!runways.closed.empty())
        rows.push_back({"CLSD", INK_RED, join(runways.closed, " ")});
    for (size_t i = 0; i < rows.size(); i++)
    {
        const Row &row = rows[i];
        const int y = USE.top + (int)i * USE.pitch;
        canvas.fillRect(PANEL.x, y, USE.tagWidth, 18, row.ink);
        canvas.text(FONT_SMALL, PANEL.x + (USE.tagWidth - textWidth(FONT_SMALL, row.tag)) / 2.0, y + 1, row.tag,
                    INK_WHITE);
        const Fitted fitted = fit(row.value, PANEL.width - USE.tagWidth - 8, {&FONT_SMALL, &FONT_TINY});
        canvas.text(*fitted.font, PANEL.x + USE.tagWidth + 8, y + 1 + (16 - fitted.font->cellHeight) / 2.0, fitted.text,
                    INK_BLACK);
    }
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

std::vector<std::string> wrap(const std::string &text, size_t columns)
{
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space == std::string::npos ? space : space - at);
        at = space == std::string::npos ? text.size() : space + 1;
        if (word.empty())
            continue;
        if (!line.empty() && line.size() + 1 + word.size() > columns)
        {
            lines.push_back(line);
            line = word;
        }
        else
            line = line.empty() ? word : line + " " + word;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

void drawNotes(Canvas &canvas, const AtisState &atis)
{
    const int margin = 8;
    canvas.fillRect(0, NOTES_TOP - 6, SCREEN_WIDTH, 2, INK_BLACK);
    if (atis.notes.empty())
        return;
    std::string text;
    for (const std::string &note : atis.notes)
        text += (text.empty() ? "" : " ") + note + ".";
    const int room = SCREEN_HEIGHT - NOTES_TOP - 2;

    // Prefer the larger font; drop to the small one before truncating.
    for (const Font *font : {&FONT_SMALL, &FONT_TINY})
    {
        const size_t columns = (SCREEN_WIDTH - 2 * margin) / font->cellWidth;
        const size_t maxLines = room / font->cellHeight;
        std::vector<std::string> lines = wrap(text, columns);
        const bool isLast = font == &FONT_TINY;
        if (lines.size() > maxLines && !isLast)
            continue;
        if (lines.size() > maxLines)
        {
            lines.resize(maxLines);
            lines.back() = lines.back().substr(0, columns - 4) + " ...";
        }
        for (size_t i = 0; i < lines.size(); i++)
            canvas.text(*font, margin, NOTES_TOP + (int)i * font->cellHeight, lines[i], INK_BLACK);
        return;
    }
}

} // namespace

void render(Canvas &canvas, const Airport &airport, const AtisState *atis, bool lowBattery, const char *problem)
{
    canvas.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, INK_WHITE);
    drawHeader(canvas, airport, atis, lowBattery);
    drawDiagram(canvas, airport, atis);
    canvas.fillRect(DIAGRAM.width, HEADER_HEIGHT, 1, NOTES_TOP - 6 - HEADER_HEIGHT, INK_BLACK);
    if (atis == nullptr)
    {
        canvas.text(FONT_MEDIUM, PANEL.x, PANEL.y + 40, "ATIS UNAVAILABLE", INK_RED);
        const std::vector<std::string> lines = wrap(problem, PANEL.width / FONT_SMALL.cellWidth);
        for (size_t i = 0; i < lines.size(); i++)
            canvas.text(FONT_SMALL, PANEL.x, PANEL.y + 72 + (int)i * FONT_SMALL.cellHeight, lines[i], INK_BLACK);
        return;
    }
    drawBroadcasts(canvas, *atis);
    drawWeather(canvas, atis->weather);
    drawRunwayUse(canvas, atis->runways);
    drawNotes(canvas, *atis);
}
