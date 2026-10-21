#pragma once

#include <cstdint>

#include "relative_mouse_touch.h"

// Book presentation: the console held sideways like a book (Hotel Dusk,
// Brain Age, ...). Each screen is turned a quarter turn and the two sit side
// by side in one window; guest framebuffers are untouched. Right-handed play
// turns the console counter-clockwise, so each screen is shown rotated 90
// degrees counter-clockwise with the top screen on the left and the
// touchscreen on the right. Left-handed play is the mirror: clockwise, the
// touchscreen on the left.
//
// Logical canvas: two areas of kBookAreaWidth (a turned screen's 192 lines)
// side by side, as tall as the widest screen (256, or wider with adaptive
// widescreen). A screen narrower than the canvas is centred vertically.

constexpr int kBookAreaWidth = 192;

// Where `screen` (0 = top, 1 = bottom) is drawn on the logical canvas.
NdsLogicalRect nds_book_screen_area(bool left_handed, int screen,
                                    int screen_width, int canvas_height);

// Clockwise rotation, in degrees, applied when drawing each screen texture.
double nds_book_rotation_degrees(bool left_handed);

// Logical point -> that screen's texture coordinates (x in [0, screen_width),
// y in [0, 192)). Returns false when the point lies outside `area`.
bool nds_book_logical_to_screen(bool left_handed, const NdsLogicalRect& area,
                                float logical_x, float logical_y,
                                float* screen_x, float* screen_y);

// A movement along the window's axes -> the same movement along the turned
// screen's own axes, for relative pointer and stick input (exact: a swap and
// a sign, so integer deltas stay integers).
void nds_book_turn_delta(bool left_handed, float dx, float dy,
                         float* screen_dx, float* screen_dy);

// The D-pad turns with the console: maps the directions pressed relative to
// the window (KEYINPUT bit layout, active-high: Right 4, Left 5, Up 6,
// Down 7) to the DS buttons that point that way when the console is held
// like the book. Other bits pass through unchanged.
uint16_t nds_book_turn_dpad(bool left_handed, uint16_t pressed);

// Texture coordinates -> logical point (inverse of the above).
void nds_book_screen_to_logical(bool left_handed, const NdsLogicalRect& area,
                                float screen_x, float screen_y,
                                float* logical_x, float* logical_y);
