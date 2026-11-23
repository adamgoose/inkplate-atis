// Lays out the whole 600x448 screen: header, runway diagram, ATIS panel, notes.
#pragma once
#include "airport.h"
#include "atis.h"
#include "canvas.h"

static const int SCREEN_WIDTH = 600;
static const int SCREEN_HEIGHT = 448;

/**
 * Draws the screen. `atis` is null when there is no broadcast to show; `problem`
 * then says why in a few words.
 */
void render(Canvas &canvas, const Airport &airport, const AtisState *atis, bool lowBattery,
            const char *problem = "Waiting for atis.info");
