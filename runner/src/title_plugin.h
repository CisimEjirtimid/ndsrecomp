#pragma once

#include <cstdint>
#include <string>

// Runner side of the title plugin ABI (plugin_api/nds_title_plugin.h).
//
// While a plugin is active, player input is latched instead of reaching the
// guest at once: nds_set_key_mask / nds_set_touch record the host state, and
// at the start of every VBlank the plugin sees it and decides what the
// guest gets for the next frame. Without an active plugin nothing changes.

// Starts the built-in plugin when `enabled`, one is compiled in, and it was
// written for `rom_sha1`. Returns whether a plugin is now active.
bool nds_title_plugin_init(const std::string& rom_sha1, bool enabled);
bool nds_title_plugin_active();

// Called from nds_set_key_mask / nds_set_touch while a plugin is active.
void nds_title_plugin_latch_keys(uint16_t pressed);
void nds_title_plugin_latch_touch(uint16_t x, uint16_t y, bool down);
// The D-pad relative to the screen as shown (before book-layout turning).
// nds_set_key_mask resets it to the guest D-pad; the frontend overrides it.
void nds_set_screen_dpad(uint16_t pressed);
// Analog sticks (dead zone applied, -1..1) and mouse motion (accumulated
// until the next VBlank) / buttons (NDS_MOUSE_*), for NdsTitleInput.
void nds_title_plugin_latch_sticks(float lx, float ly, float rx, float ry);
void nds_title_plugin_latch_mouse(float dx, float dy, uint8_t buttons);

// VBlank start, before the VBlank IRQ: run the plugin and apply its input.
void nds_title_plugin_vblank();

struct NdsTitleDraw;
// Frontend presentation: true when the plugin drew the whole canvas.
bool nds_title_plugin_present(const NdsTitleDraw* draw);
// The host 3D raster width the plugin needs (0 = native).
uint16_t nds_title_plugin_wide_3d_width();
// Whether the plugin drew the last presented frame (it then owns the mouse).
bool nds_title_plugin_presented();
// After a plugin-presented frame: -1 = not handled (frontend mapping), 0 = no
// touch, 1 = touch at *x, *y.
int nds_title_plugin_pointer(float canvas_x, float canvas_y, uint16_t* x,
                             uint16_t* y);

// "<name>: <plugin status>", or empty when no plugin is active.
std::string nds_title_plugin_status();
