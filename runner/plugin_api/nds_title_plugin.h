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
} NdsTitlePlugin;

/* Exported by the plugin library. */
const NdsTitlePlugin* nds_title_plugin(void);

#ifdef __cplusplus
}
#endif

#endif
