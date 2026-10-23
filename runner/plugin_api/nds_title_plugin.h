/* Title plugin ABI: game-specific code built from a title repository into its
 * runner (CMake NDS_TITLE_PLUGIN_DIR), for opt-in presentation and input
 * layers ("remaster" features) on top of the exact machine.
 *
 * A plugin observes the guest and hands back the input for each frame, the
 * same kind of state a player produces (buttons and the stylus). Where input
 * cannot express a feature, it may write guest main RAM through
 * `host->write` (a "hook"); keep those few, documented, and limited to the
 * scenes that need them. With the plugin disabled (--title-plugin off) the
 * runner behaves as if none were built in, so oracle comparisons stay exact.
 *
 * Plain C so a title repository depends on this one header only.
 */
#ifndef NDS_TITLE_PLUGIN_H
#define NDS_TITLE_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_TITLE_PLUGIN_ABI 1u

/* DS key bits, active high (pressed = 1); KEYINPUT order plus X/Y at 10/11. */
enum {
    NDS_KEY_A = 1u << 0,
    NDS_KEY_B = 1u << 1,
    NDS_KEY_SELECT = 1u << 2,
    NDS_KEY_START = 1u << 3,
    NDS_KEY_RIGHT = 1u << 4,
    NDS_KEY_LEFT = 1u << 5,
    NDS_KEY_UP = 1u << 6,
    NDS_KEY_DOWN = 1u << 7,
    NDS_KEY_R = 1u << 8,
    NDS_KEY_L = 1u << 9,
    NDS_KEY_X = 1u << 10,
    NDS_KEY_Y = 1u << 11,
    NDS_KEY_DPAD = NDS_KEY_RIGHT | NDS_KEY_LEFT | NDS_KEY_UP | NDS_KEY_DOWN
};

/* One frame's 2D layers per engine (A = 0, B = 1), see host->layers. */
enum {
    NDS_LAYER_BG0 = 0,
    NDS_LAYER_BG1,
    NDS_LAYER_BG2,
    NDS_LAYER_BG3,
    NDS_LAYER_OBJ, /* alpha 0xFF opaque, 0x80 semi-transparent */
    NDS_LAYER_3D,  /* engine A only; alpha = the 3D pixel's alpha */
    NDS_LAYER_COUNT
};
typedef struct NdsTitleLayers {
    uint64_t frame;
    /* 256 x 192 pixels each, 0xAARRGGBB, alpha 0 where transparent. */
    const uint32_t* pixels[2][NDS_LAYER_COUNT];
    /* Per pixel: the DS window mask (bits 0-3 BG0-3, 4 OBJ, 5 effects) and,
     * in bits 6-7, the OBJ pixel's priority. */
    const uint8_t* window[2];
    const uint32_t* backdrop[2]; /* per line, 0xFFRRGGBB */
    uint32_t dispcnt[2];
    uint16_t bgcnt[2][4];        /* bits 0-1: the BG's priority */
    uint8_t top_engine;          /* engine shown on the top screen */
} NdsTitleLayers;

typedef struct NdsTitleHost {
    uint32_t abi; /* NDS_TITLE_PLUGIN_ABI */
    /* Side-effect-free read of the ARM9 view of the guest bus (main RAM,
     * shared WRAM, VRAM, palettes, OAM, I/O registers without read side
     * effects). Returns `len` on success. */
    uint32_t (*read)(uint32_t addr, void* dst, uint32_t len);
    /* VBlanks since the plugin started. */
    uint64_t (*frame)(void);
    /* One line to the runner's log, prefixed with the plugin name. */
    void (*log)(const char* message);
    /* Write guest main RAM (0x02000000-0x023FFFFF) as the ARM9 would, at the
     * point of the call (VBlank start). Returns `len`, or 0 when refused. */
    uint32_t (*write)(uint32_t addr, const void* src, uint32_t len);
    /* Per-layer capture of the engines in `engines` (bit 0 engine A, bit 1
     * engine B; 0 = off). While on (a cost: those engines take the full
     * compositing path on every line), `layers` returns the frame that has
     * just finished, valid until the next VBlank; the other engine's planes
     * are empty. Returns 0 when nothing is available. */
    void (*capture_layers)(int engines);
    int (*layers)(NdsTitleLayers* out);
    /* Turn the 3D view by `quarters` quarter turns (0 = off) and spread a
     * square guest viewport across the plugin's wide_3d_width with square
     * pixels: a title drawn for a sideways DS renders upright and wide. Host
     * only; the 2D engine (and display capture) still consumes the centre of
     * the raster. Applies to vertices submitted after the call. */
    void (*set_view_roll)(int quarters);
} NdsTitleHost;

/* Player input for one frame. The runner fills it from the host devices
 * (keyboard, pad, stick, mouse stylus, debug server) and applies whatever the
 * plugin leaves in it to the guest. */
typedef struct NdsTitleInput {
    /* Keys the guest will see, pressed = 1, after the runner's own mapping
     * (in book layouts the D-pad is already turned with the console). */
    uint16_t keys;
    /* The D-pad as the player pressed it, relative to the screen as shown
     * (up = toward the top of the window), before any layout turning. */
    uint16_t screen_dpad;
    uint8_t touch_down;
    uint16_t touch_x; /* raw bottom-screen pixels */
    uint16_t touch_y;
} NdsTitleInput;

/* Drawing for `present`: a canvas in the window's pixels (the coordinates
 * mouse positions are then reported in, see `pointer`). Sources are the two
 * physical screens as the frontend would show them (HD when available) and,
 * while layer capture is on, each captured layer. Rectangles are
 * {x, y, w, h}; a NULL source rectangle means the whole source. */
enum {
    NDS_DRAW_TOP_SCREEN = 0,
    NDS_DRAW_BOTTOM_SCREEN = 1,
    NDS_DRAW_LAYER_FIRST = 2 /* + engine * NDS_LAYER_COUNT + layer */
};
#define NDS_DRAW_LAYER(engine, layer) \
    (NDS_DRAW_LAYER_FIRST + (engine) * NDS_LAYER_COUNT + (layer))
/* The host-only wide 3D image (the plugin's wide_3d_width x 192 in DS
 * pixels), e.g. the view turned by host->set_view_roll. */
#define NDS_DRAW_3D (NDS_DRAW_LAYER_FIRST + 2 * NDS_LAYER_COUNT)
typedef struct NdsTitleDraw {
    int width, height; /* canvas size: the window's render output, pixels */
    /* How the frontend's layout turns the DS screens (the book layouts turn
     * them a quarter turn, -90 or 90); a plugin keeps the game's orientation
     * by turning them the same way. */
    float screen_degrees;
    /* Source size in pixels; 0 when the source is unavailable. */
    int (*source_size)(int source, int* w, int* h);
    /* Draw `src` of `source` into `dst`, rotated `degrees` clockwise about
     * the destination's centre, with `alpha` 0..1. `smooth` picks linear
     * filtering (1) or nearest (0). */
    void (*draw)(int source, const float* src, const float* dst, float degrees,
                 float alpha, int smooth);
    void (*fill)(const float* dst, uint32_t argb);
} NdsTitleDraw;

typedef struct NdsTitlePlugin {
    uint32_t abi; /* NDS_TITLE_PLUGIN_ABI */
    const char* name;
    /* Lowercase ROM SHA-1 the plugin is written for; the runner starts it only
     * for that cartridge. */
    const char* rom_sha1;
    /* Called once before the first frame; returning 0 disables the plugin. */
    int (*start)(const NdsTitleHost* host);
    /* At the start of every VBlank, before the VBlank IRQ (where games run
     * their per-frame logic): observe the guest, then rewrite `io` (may be
     * NULL) to translate the player's input. Both may be NULL. */
    void (*frame)(const NdsTitleHost* host, NdsTitleInput* io);
    /* Short human-readable state for the debug server and logs; writes at most
     * `cap` bytes including the terminator. May be NULL. */
    void (*status)(char* out, size_t cap);
    /* Each presented frame (single-window layouts): draw the whole canvas
     * and return 1, or return 0 to let the frontend draw its own layout.
     * The frontend's menu and notices are drawn on top either way. May be
     * NULL. */
    int (*present)(const NdsTitleHost* host, const NdsTitleDraw* draw);
    /* After a frame the plugin presented: map a canvas position (mouse) to
     * the stylus. Return 1 with raw bottom-screen pixels in *x / *y, or 0
     * for "not on the touch screen". May be NULL (the mouse then never
     * touches while the plugin presents). */
    int (*pointer)(float canvas_x, float canvas_y, uint16_t* x, uint16_t* y);
    /* Width of the host 3D raster the plugin needs (256..448, a multiple of
     * 64 for the accelerated renderer), fixed at startup; 0 = native. */
    uint16_t wide_3d_width;
} NdsTitlePlugin;

/* Exported by the plugin library. */
const NdsTitlePlugin* nds_title_plugin(void);

#ifdef __cplusplus
}
#endif

#endif
