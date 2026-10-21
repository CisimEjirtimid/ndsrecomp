#include "book_layout.h"

#include <cmath>
#include <initializer_list>

namespace {

bool require(bool value) {
    return value;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

}  // namespace

int main() {
    // Right-handed: top screen left, touchscreen right; a 256-wide screen
    // fills a 256-tall canvas.
    const NdsLogicalRect top = nds_book_screen_area(false, 0, 256, 256);
    const NdsLogicalRect bottom = nds_book_screen_area(false, 1, 256, 256);
    if (!require(top.x == 0) || !require(top.y == 0) ||
        !require(top.width == 192) || !require(top.height == 256) ||
        !require(bottom.x == 192) || !require(bottom.y == 0)) return 1;

    // Left-handed swaps the areas; a narrower screen is centred vertically.
    const NdsLogicalRect left_bottom = nds_book_screen_area(true, 1, 256, 320);
    const NdsLogicalRect left_top = nds_book_screen_area(true, 0, 320, 320);
    if (!require(left_bottom.x == 0) || !require(left_bottom.y == 32) ||
        !require(left_top.x == 192) || !require(left_top.y == 0)) return 2;

    if (!require(nds_book_rotation_degrees(false) == -90.0) ||
        !require(nds_book_rotation_degrees(true) == 90.0)) return 3;

    // Counter-clockwise: the screen's right edge is the area's top, its top
    // edge the area's left. Pixel (255, 0) is the area's top-left pixel.
    float sx = 0.0f;
    float sy = 0.0f;
    if (!require(nds_book_logical_to_screen(false, bottom, 192.5f, 0.5f,
                                            &sx, &sy)) ||
        !require(static_cast<int>(sx) == 255) ||
        !require(static_cast<int>(sy) == 0)) return 4;
    // Pixel (0, 191) is the area's bottom-right pixel.
    if (!require(nds_book_logical_to_screen(false, bottom, 383.5f, 255.5f,
                                            &sx, &sy)) ||
        !require(static_cast<int>(sx) == 0) ||
        !require(static_cast<int>(sy) == 191)) return 5;

    // Clockwise: pixel (0, 191) is the area's top-left pixel.
    if (!require(nds_book_logical_to_screen(true, left_bottom, 0.5f, 32.5f,
                                            &sx, &sy)) ||
        !require(static_cast<int>(sx) == 0) ||
        !require(static_cast<int>(sy) == 191)) return 6;

    // Outside the area (including the vertical margin) is rejected.
    if (!require(!nds_book_logical_to_screen(false, bottom, 191.9f, 10.0f,
                                             &sx, &sy)) ||
        !require(!nds_book_logical_to_screen(true, left_bottom, 10.0f, 31.0f,
                                             &sx, &sy)) ||
        !require(!nds_book_logical_to_screen(true, left_bottom, 10.0f, 288.0f,
                                             &sx, &sy))) return 7;

    // A turned delta moves the stylus the way the cursor moved on screen:
    // the mapped point of (start + delta) equals start + turned delta.
    for (const bool left_handed : {false, true}) {
        const NdsLogicalRect area =
            nds_book_screen_area(left_handed, 1, 256, 256);
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        const float lx = static_cast<float>(area.x) + 60.0f;
        const float ly = static_cast<float>(area.y) + 90.0f;
        if (!require(nds_book_logical_to_screen(left_handed, area, lx, ly,
                                                &x0, &y0)) ||
            !require(nds_book_logical_to_screen(left_handed, area, lx + 7.0f,
                                                ly - 3.0f, &x1, &y1)))
            return 9;
        nds_book_turn_delta(left_handed, 7.0f, -3.0f, &dx, &dy);
        if (!require(near(x1 - x0, dx)) || !require(near(y1 - y0, dy)))
            return 10;
    }

    // D-pad: right-handed (console turned counter-clockwise), on-screen Up
    // is the DS Right button, Right is Down, Down is Left, Left is Up.
    constexpr uint16_t kR = 1u << 4, kL = 1u << 5, kU = 1u << 6, kD = 1u << 7;
    if (!require(nds_book_turn_dpad(false, kU) == kR) ||
        !require(nds_book_turn_dpad(false, kR) == kD) ||
        !require(nds_book_turn_dpad(false, kD) == kL) ||
        !require(nds_book_turn_dpad(false, kL) == kU) ||
        // Left-handed is the mirror.
        !require(nds_book_turn_dpad(true, kU) == kL) ||
        !require(nds_book_turn_dpad(true, kR) == kU) ||
        !require(nds_book_turn_dpad(true, kD) == kR) ||
        !require(nds_book_turn_dpad(true, kL) == kD) ||
        // Diagonals turn together; A (bit 0) and Start (bit 3) pass through.
        !require(nds_book_turn_dpad(false, kU | kR | 0x0009u) ==
                 (kR | kD | 0x0009u)))
        return 11;

    // The two transforms are inverses for both hands.
    for (const bool left_handed : {false, true}) {
        const NdsLogicalRect area =
            nds_book_screen_area(left_handed, 1, 256, 256);
        for (const float x : {0.25f, 17.0f, 128.5f, 255.75f}) {
            for (const float y : {0.25f, 96.0f, 191.5f}) {
                float lx = 0.0f;
                float ly = 0.0f;
                nds_book_screen_to_logical(left_handed, area, x, y, &lx, &ly);
                if (!require(nds_book_logical_to_screen(left_handed, area, lx,
                                                        ly, &sx, &sy)) ||
                    !require(near(sx, x)) || !require(near(sy, y)))
                    return 8;
            }
        }
    }
    return 0;
}
