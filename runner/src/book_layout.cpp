#include "book_layout.h"

namespace {

constexpr float kLines = 192.0f;

}  // namespace

NdsLogicalRect nds_book_screen_area(bool left_handed, int screen,
                                    int screen_width, int canvas_height) {
    // Right-handed: top screen left, touchscreen right; left-handed swaps.
    const bool left_slot = (screen == 0) != left_handed;
    return NdsLogicalRect{left_slot ? 0 : kBookAreaWidth,
                          (canvas_height - screen_width) / 2,
                          kBookAreaWidth, screen_width};
}

double nds_book_rotation_degrees(bool left_handed) {
    return left_handed ? 90.0 : -90.0;
}

// The transforms are exact on real coordinates, so flooring a mapped point
// lands in the pixel that was drawn under it. With (u, v) the offset inside
// the area and w the screen width (= area height):
//   counter-clockwise: u = y,       v = w - x
//   clockwise:         u = 192 - y, v = x
bool nds_book_logical_to_screen(bool left_handed, const NdsLogicalRect& area,
                                float logical_x, float logical_y,
                                float* screen_x, float* screen_y) {
    const float u = logical_x - static_cast<float>(area.x);
    const float v = logical_y - static_cast<float>(area.y);
    if (u < 0.0f || u >= static_cast<float>(area.width) || v < 0.0f ||
        v >= static_cast<float>(area.height))
        return false;
    if (left_handed) {
        *screen_x = v;
        *screen_y = kLines - u;
    } else {
        *screen_x = static_cast<float>(area.height) - v;
        *screen_y = u;
    }
    return true;
}

// Differentiating the transforms above: counter-clockwise, screen x runs
// along -v and screen y along u; clockwise, x along v and y along -u.
void nds_book_turn_delta(bool left_handed, float dx, float dy,
                         float* screen_dx, float* screen_dy) {
    if (left_handed) {
        *screen_dx = dy;
        *screen_dy = -dx;
    } else {
        *screen_dx = -dy;
        *screen_dy = dx;
    }
}

uint16_t nds_book_turn_dpad(bool left_handed, uint16_t pressed) {
    // Each window direction is a unit movement; turning it gives the DS
    // axis it runs along. Per bit, so opposite presses survive as pressed.
    static constexpr struct { uint16_t bit; float dx, dy; } kDirs[] = {
        {1u << 4, 1.0f, 0.0f},    // Right
        {1u << 5, -1.0f, 0.0f},   // Left
        {1u << 6, 0.0f, -1.0f},   // Up
        {1u << 7, 0.0f, 1.0f},    // Down
    };
    uint16_t out = static_cast<uint16_t>(pressed & ~0x00F0u);
    for (const auto& dir : kDirs) {
        if (!(pressed & dir.bit)) continue;
        float sx = 0.0f;
        float sy = 0.0f;
        nds_book_turn_delta(left_handed, dir.dx, dir.dy, &sx, &sy);
        if (sx > 0.0f) out |= 1u << 4;
        else if (sx < 0.0f) out |= 1u << 5;
        else if (sy < 0.0f) out |= 1u << 6;
        else out |= 1u << 7;
    }
    return out;
}

void nds_book_screen_to_logical(bool left_handed, const NdsLogicalRect& area,
                                float screen_x, float screen_y,
                                float* logical_x, float* logical_y) {
    float u = 0.0f;
    float v = 0.0f;
    if (left_handed) {
        u = kLines - screen_y;
        v = screen_x;
    } else {
        u = screen_y;
        v = static_cast<float>(area.height) - screen_x;
    }
    *logical_x = static_cast<float>(area.x) + u;
    *logical_y = static_cast<float>(area.y) + v;
}
