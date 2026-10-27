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
    /* Per line, the brightness each line was composed with (the layers
     * above are before it): MASTER_BRIGHT, and BLDCNT with its BLDY factor
     * 0..16 (BLDCNT bits 6-7 = 2 brighten / 3 darken the first targets in
     * bits 0-5). Games fade with either. */
    const uint16_t* master_bright[2];
    const uint16_t* bldcnt[2];
    const uint8_t* evy[2];
} NdsTitleLayers;
/* NdsTitleLayers has the per-line brightness. */
#define NDS_LAYERS_HAS_BRIGHTNESS 1

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
    /* Turn the 3D view by `quarters` quarter turns (0 = off) and spread the
     * guest viewport across the plugin's wide_3d_width with square pixels,
     * the turned view filling the raster's height (a turned W x H viewport
     * covers H * 192 / W columns in the middle, the rest is extra scenery):
     * a title drawn for a sideways DS renders upright and wide. Host
     * only; the 2D engine (and display capture) still consumes the centre of
     * the raster, so never roll a pass the game reads back (see
     * skip_view_roll). Applies to vertices submitted after the call. */
    void (*set_view_roll)(int quarters);
    /* For titles that share the 3D engine between passes (the view, a
     * picture for the other screen, an object-picking pass the game reads
     * back through display capture): while `skip` is nonzero, vertices are
     * submitted unrolled although the roll stays set, and NDS_DRAW_3D keeps
     * showing the last frame rendered with the roll. Set it at VBlank start
     * before each pass that is not the view. May be NULL (older runners). */
    void (*skip_view_roll)(int skip);
    /* Run the guest as fast as the host can (the runner's turbo, audio
     * paused) while `on` is nonzero, e.g. while the player skips text the
     * title has shown before. Turned off again when the plugin stops
     * asking. May be NULL (older runners). */
    void (*fast_forward)(int on);
    /* Leave out of the 3D frames rendered from now on the polygons whose
     * TEXIMAGE_PARAM matches one of `count` {mask, value} pairs ((param &
     * mask) == value; e.g. mask 0x1FF00000 picks a texture format and size
     * wherever it sits in VRAM). `flags`: NDS_HIDE_CLEAR_TRANSPARENT renders
     * the clear colour with alpha 0; NDS_HIDE_KEEP_DEPTH keeps the opaque ones
     * as depth alone, drawn before the frame's other opaque polygons, so
     * what they covered stays hidden (the 3D in front of a picture without
     * the picture). count 0 and flags 0 restore the game's frames. For a
     * title that draws a scene's parts itself, e.g. a character without the
     * backdrop the game renders behind it (see NdsTitleDraw.keep_3d). Host
     * only, but what the guest then displays and captures changes too: only
     * in scenes that never read it back. May be NULL (older runners; before
     * NDS_HIDE_KEEP_DEPTH any nonzero flags clear transparent). */
    void (*hide_3d_polygons)(const uint32_t* mask_value, int count, int flags);
    /* What the 3D frame flushed last (rendered and shown next) is made of:
     * up to `max` distinct {TEXIMAGE_PARAM, PLTT_BASE} pairs of its
     * polygons into `pairs` (2 * max words), in submission order, polygons
     * hide_3d_polygons leaves out included. Returns how many there are
     * (may exceed max). Call from `frame`. May be NULL (older runners). */
    uint32_t (*frame_textures)(uint32_t* pairs, uint32_t max);
    /* Read texture image / texture palette memory as the 3D engine
     * addresses it (TEXIMAGE_PARAM's offset * 8; PLTT_BASE * 16, or * 8
     * for 4-colour textures), from the banks mapped there now; unmapped
     * bytes read as zero. Returns `len`. Call from `frame`. May be NULL. */
    uint32_t (*read_texture)(uint32_t addr, void* dst, uint32_t len);
    uint32_t (*read_texture_palette)(uint32_t addr, void* dst, uint32_t len);
} NdsTitleHost;
/* NdsTitleHost has frame_textures, read_texture and read_texture_palette. */
#define NDS_HOST_HAS_TEXTURES 1
/* hide_3d_polygons flags. */
#define NDS_HIDE_CLEAR_TRANSPARENT 1
#define NDS_HIDE_KEEP_DEPTH 2

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
    /* Read-only extras (changing them has no effect on the guest). */
    /* The pad's sticks, [0] left and [1] right, each {x, y} in -1..1 (x
     * right, y down) relative to the player, before any layout turning; 0
     * inside the dead zone and beyond it rescaled to start from 0. The left
     * stick also reaches `keys` / `screen_dpad` as D-pad directions. */
    float stick[2][2];
    /* Mouse motion over the window since the last frame, in window pixels,
     * and the buttons held now (NDS_MOUSE_*). The stylus mapping (the left
     * button over the touch screen, see `pointer`) is unaffected. */
    float mouse_dx, mouse_dy;
    uint8_t mouse_buttons;
    /* Where the mouse is, in canvas pixels (what `pointer` is given), and
     * whether it is over the canvas at all (for hover effects). */
    float mouse_x, mouse_y;
    uint8_t mouse_over;
    /* Mouse wheel notches turned since the last frame, positive away from
     * the player ("up"). */
    float wheel;
    /* The host keyboard: NDS_TITLE_KEYBOARD_SIZE entries indexed by USB HID
     * usage id (= SDL scancode), 1 while held. Keys the runner maps to DS
     * buttons reach `keys` too, so a plugin's own features take others.
     * Empty while the runner's menu has the keyboard. */
    const uint8_t* keyboard;
} NdsTitleInput;
/* NdsTitleInput has mouse_x / mouse_y / mouse_over, wheel and keyboard. */
#define NDS_INPUT_HAS_POINTER 1
#define NDS_TITLE_KEYBOARD_SIZE 512

enum {
    NDS_MOUSE_LEFT = 1u << 0,
    NDS_MOUSE_RIGHT = 1u << 1,
    NDS_MOUSE_MIDDLE = 1u << 2
};

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
/* NDS_DRAW_3D averaged 50/50 with the frame rendered before it: what a game
 * that blends consecutive frames through display capture shows (DS temporal
 * anti-aliasing jitters the camera by a fraction of a pixel every other
 * frame and blends; drawn raw, the view shimmers). */
#define NDS_DRAW_3D_BLEND (NDS_DRAW_3D + 1)
/* NdsTitleDraw.draw flags. */
#define NDS_DRAW_SMOOTH 1
#define NDS_DRAW_KEEP 2
typedef struct NdsTitleDraw {
    int width, height; /* canvas size: the window's render output, pixels */
    /* How the frontend's layout turns the DS screens (the book layouts turn
     * them a quarter turn, -90 or 90); a plugin keeps the game's orientation
     * by turning them the same way. */
    float screen_degrees;
    /* Source size in pixels; 0 when the source is unavailable. */
    int (*source_size)(int source, int* w, int* h);
    /* Draw `src` of `source` into `dst`, rotated `degrees` clockwise about
     * the destination's centre, with `alpha` 0..1. `flags`: NDS_DRAW_SMOOTH
     * picks linear filtering (else nearest); NDS_DRAW_KEEP draws a layer or
     * NDS_DRAW_3D as last drawn, without refreshing it from this frame (for
     * games that alternate what an engine shows from frame to frame). */
    void (*draw)(int source, const float* src, const float* dst, float degrees,
                 float alpha, int flags);
    void (*fill)(const float* dst, uint32_t argb);
    /* Limit the draws and fills that follow to `rect` {x, y, w, h} in canvas
     * pixels (e.g. a rotated map inside its panel); NULL lifts the limit.
     * Lifted again when present returns. */
    void (*clip)(const float* rect);
    /* Draw the plugin's own pixels (its composites, e.g. a round minimap,
     * or text): `pixels` (w x h, 0xAARRGGBB with straight alpha, rows
     * packed) are uploaded to host image `slot` (0..NDS_DRAW_IMAGE_SLOTS-1)
     * and drawn like `draw`, `src` in image pixels (NULL = all). The pixels
     * are read during the call. NULL `pixels` draws the slot's last upload
     * again. */
    void (*image)(int slot, const uint32_t* pixels, int w, int h,
                  const float* src, const float* dst, float degrees,
                  float alpha, int flags);
    /* Copy this frame's wide 3D image (NDS_DRAW_3D's pixels, with the 3D
     * alpha: 0 where nothing was drawn under a transparent clear colour,
     * see NdsTitleHost.hide_3d_polygons) into image `slot`, to draw now or
     * later with image(slot, NULL, ...); *w x *h is its size in pixels.
     * Returns 0 when there is no image. For games that draw a different
     * picture with the 3D engine every other frame. May be NULL. */
    int (*keep_3d)(int slot, int* w, int* h);
    /* Multiply the colour of the draws and images that follow by `rgb`
     * (0xRRGGBB; 0xFFFFFF for none), e.g. to darken a character who is not
     * speaking. Fills are unaffected. Lifted again when present returns. */
    void (*tint)(uint32_t rgb);
} NdsTitleDraw;
/* NdsTitleDraw has `clip`. */
#define NDS_DRAW_HAS_CLIP 1
/* NdsTitleDraw has `image`. */
#define NDS_DRAW_HAS_IMAGE 1
/* NdsTitleDraw has `keep_3d` and `tint`; NdsTitleHost has
 * `hide_3d_polygons`. */
#define NDS_DRAW_HAS_KEEP_3D 1
#define NDS_DRAW_IMAGE_SLOTS 16

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
