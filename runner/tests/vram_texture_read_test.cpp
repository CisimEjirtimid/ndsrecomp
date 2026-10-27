// Ranged reads of the 3D engine's texture and texture-palette spaces
// (nds_vram_read_texture / nds_vram_read_texpal, behind the title plugin's
// read_texture and read_texture_palette).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gpu2d.h"
#include "io.h"
#include "vram.h"

// vram.cpp fences the 2D renderer's workers; none run here.
std::atomic<uint32_t> nds_gpu2d_jobs_outstanding{0};
std::atomic<uint32_t> nds_gpu2d_staged_captures{0};
void nds_gpu2d_drain(uint32_t) {}
// POWCNT1 (engine B's VRAM access): both engines on.
uint16_t nds_powercontrol9() { return 0x820F; }

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    nds_vram_reset();
    // Bank B as texture slot 1 (MST 3, OFS 1), bank E as palette slots 0-3.
    nds_vram_map(1, 0x8B);
    nds_vram_map(4, 0x83);
    uint8_t* b = nds_vram_bank_data(1);
    for (uint32_t i = 0; i < 0x20000; ++i) b[i] = static_cast<uint8_t>(i * 7 + 3);
    uint8_t* e = nds_vram_bank_data(4);
    for (uint32_t i = 0; i < 0x10000; ++i) e[i] = static_cast<uint8_t>(255 - i);

    uint8_t got[16];
    nds_vram_read_texture(0x20000 + 0x100, got, sizeof got);
    expect(std::memcmp(got, b + 0x100, sizeof got) == 0, "a mapped texture slot");
    std::memset(got, 0xEE, sizeof got);
    nds_vram_read_texture(0x0, got, 4);
    expect(got[0] == 0 && got[3] == 0, "an unmapped slot reads zero");
    // Across a slot boundary: slot 1's last bytes, then slot 2 (unmapped).
    nds_vram_read_texture(0x3FFFE, got, 4);
    expect(got[0] == b[0x1FFFE] && got[1] == b[0x1FFFF] && got[2] == 0 && got[3] == 0,
           "across slots");
    nds_vram_read_texture(0x7FFFE, got, 4);
    expect(got[2] == 0 && got[3] == 0, "past the space reads zero");
    // The copy the renderer uses agrees.
    static uint8_t flat[512 * 1024];
    nds_vram_copy_texture(flat);
    nds_vram_read_texture(0x2ABCD, got, 8);
    expect(std::memcmp(got, flat + 0x2ABCD, 8) == 0, "same as the renderer's copy");

    nds_vram_read_texpal(0x1E0, got, 16);
    expect(std::memcmp(got, e + 0x1E0, 16) == 0, "texture palettes");
    nds_vram_read_texpal(0x10000, got, 4);
    expect(got[0] == 0, "an unmapped palette slot reads zero");

    if (failures) return 1;
    std::printf("vram_texture_read_test: ok\n");
    return 0;
}
