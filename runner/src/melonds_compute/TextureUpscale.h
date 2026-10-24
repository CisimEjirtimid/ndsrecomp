// GPU texture upscaling, texture replacement and texture dumping for the
// vendored melonDS texture cache.
//
// This file and TextureUpscale.cpp are project-owned. The GLSL rule set in
// the .cpp is adapted from Hyllian's xBR-lv2 shader, which is MIT licensed
// -- see THIRD_PARTY_ATTRIBUTION.md. It is deliberately NOT placed under
// runner/vendor/melonds/, so the MIT source stays outside the GPL-3.0
// vendored tree and its provenance stays legible.
//
// Why here rather than at sample time: the DS reuses each decoded texture
// across many polygons and many frames, so filtering once per cache miss is
// far cheaper than filtering per sampled pixel, and it leaves the renderer's
// hot texture-fetch path untouched.
//
// Texture packs: every texture the cache decodes is named by a hash of its
// decoded texels (palette applied) and size, `<w>x<h>_<hash16>.png`. A PNG
// of that name in the pack directory replaces it; it may be any whole
// multiple of the native size and is resampled to the storage factor.
// Dumping writes each decoded texture under that name, for pack authors.
#pragma once

#include <cstdint>
#include <string>

// How decoded textures are filtered into the cache. Original keeps the DS
// texels (replicated when the storage is larger, for a texture pack);
// Smooth runs xBR-lv2.
enum class NdsTextureFilter : uint8_t { Original = 0, Smooth = 1 };

// Valid factors are 1, 2, and 4: the storage factor for the Smooth filter
// and texture packs (1 there means 4). Settings take effect when the cache
// is rebuilt (nds_texture_upscale_apply, called as the compute renderer is
// created): the factor sizes every texture array allocation, and the
// cache's free-list is derived from it.
void nds_texture_upscale_set_factor(int factor);
void nds_texture_set_filter(NdsTextureFilter filter);
NdsTextureFilter nds_texture_filter();
// The pack directory ("" = none) and whether it is used.
void nds_texture_pack_set_dir(const std::string& dir);
bool nds_texture_pack_available();
void nds_texture_pack_set_enabled(bool enabled);
bool nds_texture_pack_enabled();
// Write every newly decoded texture here as a PNG ("" = off).
void nds_texture_dump_set_dir(const std::string& dir);

// Fixes the settings above for the next texture cache. Requires a current
// GL 4.3 context (compiles the filter on first use). Returns the storage
// factor now in effect.
int nds_texture_upscale_apply();
// The storage factor of the current texture cache.
int nds_texture_upscale_factor();

// True once the compute program exists. Failure leaves the factor at 1 so
// the texture cache silently keeps native uploads rather than breaking.
bool nds_texture_upscale_ready();

// Compiles the compute program when the filter needs it and applies the
// settings. Requires a current GL 4.3 context. Safe to call repeatedly.
bool nds_texture_upscale_init();
void nds_texture_upscale_shutdown();

// Uploads one decoded RGB6A5 texture (width x height, four bytes per texel:
// r, g, b at 0..63 and alpha at 0..31) into the given array layer, which the
// caller allocated at width*factor x height*factor: a pack replacement, the
// Smooth filter, or replicated texels. Also dumps it when dumping is on.
//
// Returns false when the plain native upload is right (factor 1 and no
// replacement), or a dispatch could not run at factor 1.
bool nds_texture_upscale_dispatch(uint32_t dst_array, uint32_t width,
                                  uint32_t height, uint32_t layer,
                                  const void* decoded);
