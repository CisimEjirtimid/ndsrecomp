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
