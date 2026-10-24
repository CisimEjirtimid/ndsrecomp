#include "TextureUpscale.h"

// Builds without the compute renderer have no texture cache: every setting
// is accepted and nothing changes.
void nds_texture_upscale_set_factor(int) {}

void nds_texture_set_filter(NdsTextureFilter) {}

NdsTextureFilter nds_texture_filter() {
    return NdsTextureFilter::Original;
}

void nds_texture_pack_set_dir(const std::string&) {}

bool nds_texture_pack_available() {
    return false;
}

void nds_texture_pack_set_enabled(bool) {}

bool nds_texture_pack_enabled() {
    return false;
}

void nds_texture_dump_set_dir(const std::string&) {}

int nds_texture_upscale_apply() {
    return 1;
}

int nds_texture_upscale_factor() {
    return 1;
}

bool nds_texture_upscale_ready() {
    return false;
}

bool nds_texture_upscale_init() {
    return false;
}

void nds_texture_upscale_shutdown() {}

bool nds_texture_upscale_dispatch(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const void*) {
    return false;
}
