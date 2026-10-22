#include "title_plugin.h"

#include <cstdio>

#include "state.h"
#include "io.h"
#include "nds_title_plugin.h"

#ifndef NDS_HAVE_TITLE_PLUGIN
extern "C" const NdsTitlePlugin* nds_title_plugin(void) { return nullptr; }
#endif

namespace {

const NdsTitlePlugin* g_plugin = nullptr;
uint64_t g_frame = 0;
NdsTitleInput g_host{};

uint32_t host_read(uint32_t addr, void* dst, uint32_t len) {
    auto* out = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) out[i] = bus_debug_read8(9, addr + i);
    return len;
}

uint64_t host_frame() { return g_frame; }

void host_log(const char* message) {
    std::fprintf(stderr, "[%s] %s\n", g_plugin ? g_plugin->name : "plugin",
                 message);
}

const NdsTitleHost kHost = {NDS_TITLE_PLUGIN_ABI, host_read, host_frame,
                            host_log};

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
    g_host.touch_down = down ? 1u : 0u;
    g_host.touch_x = down ? x : 0u;
    g_host.touch_y = down ? y : 0u;
}

void nds_title_plugin_start_frame() {
    if (!g_plugin) return;
    ++g_frame;
    NdsTitleInput io = g_host;
    if (g_plugin->frame) g_plugin->frame(&kHost, &io);
    nds_apply_guest_keys(static_cast<uint16_t>(0x0FFFu & ~io.keys));
    nds_apply_guest_touch(io.touch_x, io.touch_y, io.touch_down != 0);
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
    return out;
}
