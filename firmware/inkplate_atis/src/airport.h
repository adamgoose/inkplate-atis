// Runway geometry baked in by tools/src/extract.ts (see airport_data.cpp).
#pragma once

struct PavementEnd
{
    /** Ident as spoken on the ATIS: "4", "30L". */
    const char *name;
    /** Physical end of the pavement behind this threshold, in feet east and north of the airport reference point. */
    double x, y;
};

struct Pavement
{
    PavementEnd ends[2];
    double widthFt;
};

struct Airport
{
    const char *ident;
    const char *name;
    const char *airac;
    const Pavement *pavements;
    int pavementCount;
};

extern const Airport AIRPORT;
