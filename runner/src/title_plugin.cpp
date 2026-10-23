#include "title_plugin.h"

#include <cstdio>

#include "state.h"
#include "gpu2d.h"
#include "gpu3d.h"
#include "io.h"
#include "runtime_arm.h"
#include "nds_title_plugin.h"

#ifndef NDS_HAVE_TITLE_PLUGIN
extern "C" const NdsTitlePlugin* nds_title_plugin(void) { return nullptr; }
#endif

namespace {

const NdsTitlePlugin* g_plugin = nullptr;
uint64_t g_frame = 0;
uint64_t g_writes = 0;
// Host stylus seen by the latch: presses, releases, last pressed point.
uint64_t g_touch_downs = 0, g_touch_ups = 0;
uint16_t g_touch_last_x = 0, g_touch_last_y = 0;
bool g_presented = false;  // the plugin drew the last presented frame
NdsTitleInput g_host{};

uint32_t host_read(uint32_t addr, void* dst, uint32_t len) {
    auto* out = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) out[i] = bus_debug_read8(9, addr + i);
    return len;
}

uint32_t host_write(uint32_t addr, const void* src, uint32_t len) {
    if (addr < 0x02000000u || addr >= 0x02400000u || len > 0x02400000u - addr)
        return 0;
    const auto* in = static_cast<const uint8_t*>(src);
    const NdsCpu old = g_nds_active;
    g_nds_active = NDS_ARM9;
    for (uint32_t i = 0; i < len; ++i) bus_write_u8_slow(addr + i, in[i]);
    g_nds_active = old;
    ++g_writes;
    return len;
}

uint64_t host_frame() { return g_frame; }

void host_capture_layers(int engines) {
    nds_gpu2d_set_layer_capture(static_cast<uint8_t>(engines & 3));
}

void host_set_view_roll(int quarters) { nds_gpu3d_set_view_roll(quarters); }

int host_layers(NdsTitleLayers* out) {
    static_assert(NDS_LAYER_COUNT == NDS_GPU2D_LAYER_COUNT, "layer enums");
    NdsGpu2dLayers l{};
    if (!out || !nds_gpu2d_layers(&l)) return 0;
    out->frame = l.frame;
    for (int e = 0; e < 2; ++e) {
        for (int i = 0; i < NDS_LAYER_COUNT; ++i) out->pixels[e][i] = l.pixels[e][i];
        out->window[e] = l.window[e];
        out->backdrop[e] = l.backdrop[e];
        out->dispcnt[e] = l.dispcnt[e];
        for (int bg = 0; bg < 4; ++bg) out->bgcnt[e][bg] = l.bgcnt[e][bg];
    }
    out->top_engine = l.top_engine;
    return 1;
}

void host_log(const char* message) {
    std::fprintf(stderr, "[%s] %s\n", g_plugin ? g_plugin->name : "plugin",
                 message);
}

const NdsTitleHost kHost = {NDS_TITLE_PLUGIN_ABI, host_read, host_frame,
                            host_log, host_write, host_capture_layers,
                            host_layers, host_set_view_roll};

}  // namespace

bool nds_title_plugin_init(const std::string& rom_sha1, bool enabled) {
    g_plugin = nullptr;
    const NdsTitlePlugin* plugin = nds_title_plugin();
    if (!plugin) return false;
    if (!enabled) {
        std::fprintf(stderr, "[plugin] %s built in, disabled\n", plugin->name);
        return false;
    }
    if (plugin->abi != NDS_TITLE_PLUGIN_ABI) {
        std::fprintf(stderr, "[plugin] %s: ABI %u, runner has %u; not started\n",
                     plugin->name, plugin->abi, NDS_TITLE_PLUGIN_ABI);
        return false;
    }
    if (!plugin->rom_sha1 || rom_sha1 != plugin->rom_sha1) {
        std::fprintf(stderr, "[plugin] %s: written for another ROM; not started\n",
                     plugin->name);
        return false;
    }
    g_plugin = plugin;
    if (plugin->start && !plugin->start(&kHost)) {
        std::fprintf(stderr, "[plugin] %s declined to start\n", plugin->name);
        g_plugin = nullptr;
        return false;
    }
    g_host = NdsTitleInput{};
    std::fprintf(stderr, "[plugin] %s active\n", plugin->name);
    return true;
}

bool nds_title_plugin_active() { return g_plugin != nullptr; }

void nds_title_plugin_latch_keys(uint16_t pressed) {
    g_host.keys = pressed;
    g_host.screen_dpad = pressed & NDS_KEY_DPAD;
}

void nds_set_screen_dpad(uint16_t pressed) {
    g_host.screen_dpad = pressed & NDS_KEY_DPAD;
}

void nds_title_plugin_latch_touch(uint16_t x, uint16_t y, bool down) {
    if (down && !g_host.touch_down) ++g_touch_downs;
    if (!down && g_host.touch_down) ++g_touch_ups;
    if (down) {
        g_touch_last_x = x;
        g_touch_last_y = y;
    }
    g_host.touch_down = down ? 1u : 0u;
    g_host.touch_x = down ? x : 0u;
    g_host.touch_y = down ? y : 0u;
}

void nds_title_plugin_vblank() {
    if (!g_plugin) return;
    ++g_frame;
    NdsTitleInput io = g_host;
    if (g_plugin->frame) g_plugin->frame(&kHost, &io);
    nds_apply_guest_keys(static_cast<uint16_t>(0x0FFFu & ~io.keys));
    nds_apply_guest_touch(io.touch_x, io.touch_y, io.touch_down != 0);
}

bool nds_title_plugin_present(const NdsTitleDraw* draw) {
    g_presented = g_plugin && g_plugin->present && draw &&
                  g_plugin->present(&kHost, draw) != 0;
    return g_presented;
}

uint16_t nds_title_plugin_wide_3d_width() {
    return g_plugin ? g_plugin->wide_3d_width : 0;
}

bool nds_title_plugin_presented() { return g_plugin && g_presented; }

int nds_title_plugin_pointer(float canvas_x, float canvas_y, uint16_t* x,
                             uint16_t* y) {
    if (!g_plugin || !g_presented) return -1;
    if (!g_plugin->pointer) return 0;
    return g_plugin->pointer(canvas_x, canvas_y, x, y) ? 1 : 0;
}

std::string nds_title_plugin_status() {
    if (!g_plugin) return {};
    std::string out = g_plugin->name;
    if (g_plugin->status) {
        char buf[512] = {};
        g_plugin->status(buf, sizeof(buf));
        out += ": ";
        out += buf;
    }
    if (g_writes) out += " [" + std::to_string(g_writes) + " guest writes]";
    if (g_touch_downs)
        out += " [stylus " + std::to_string(g_touch_downs) + " down/" +
               std::to_string(g_touch_ups) + " up, last (" +
               std::to_string(g_touch_last_x) + ", " +
               std::to_string(g_touch_last_y) + ")" +
               (g_host.touch_down ? " held" : "") + "]";
    return out;
}
