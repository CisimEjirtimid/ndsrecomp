#pragma once

#include <cstdint>
#include <string>

// Runner side of the title plugin ABI (plugin_api/nds_title_plugin.h).
//
// While a plugin is active, player input is latched instead of reaching the
// guest at once: nds_set_key_mask / nds_set_touch record the host state, and
// at physical line 0 of every frame the plugin sees it and decides what the
// guest gets for that frame. Without an active plugin nothing changes.

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

// Physical line 0: run the plugin and apply its input to the guest.
void nds_title_plugin_start_frame();

// "<name>: <plugin status>", or empty when no plugin is active.
std::string nds_title_plugin_status();
