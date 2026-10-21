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
