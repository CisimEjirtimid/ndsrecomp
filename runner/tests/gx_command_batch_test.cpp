// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Matthew Stanley. See ../../LICENSE-GPL.
// Differential checks against the actual maintained melonDS FIFO/geometry
// implementation, including source mutation between scheduler slices.
#include "GPU3D_CommandBatch.h"
#include "NDS.h"
#include "Platform.h"

#include <cstdio>
#include <stdexcept>
#include <memory>

namespace melonDS {
void NDS::SetIRQ(u32, u32) {}
void NDS::ClearIRQ(u32, u32) {}
void NDS::CheckDMAs(u32, u32) {}
void NDS::GXFIFOStall() {}
void NDS::GXFIFOUnstall() {}
bool GPU::MakeVRAMFlat_TextureCoherent(NonStupidBitField<1024>&) noexcept { return false; }
bool GPU::MakeVRAMFlat_TexPalCoherent(NonStupidBitField<256>&) noexcept { return false; }
namespace Platform {
void Log(LogLevel, const char*, ...) {}
struct Thread {};
struct Semaphore {};
Thread* Thread_Create(std::function<void()>) { throw std::runtime_error("unexpected renderer thread"); }
void Thread_Free(Thread*) {}
void Thread_Wait(Thread*) {}
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore*) {}
void Semaphore_Wait(Semaphore*) {}
void Semaphore_Post(Semaphore*, int) {}
}
}

namespace {
using namespace melonDS;
using namespace melonDS::CommandBatch;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
State state(const GPU3D& gpu) { return {gpu.CurCommand, gpu.NumCommands, gpu.ParamCount, gpu.TotalParams}; }
template<class Queue> void queue_equal(Queue& a, Queue& b) {
    require(a.Level() == b.Level(), "FIFO level differs");
    for (uint32_t i = 0; i < a.Level(); ++i)
        require(a.Peek(i).Command == b.Peek(i).Command && a.Peek(i).Param == b.Peek(i).Param,
                "FIFO command/parameter differs");
}
void equal(GPU3D& a, GPU3D& b) {
    require(state(a) == state(b), "partial packed-command continuation differs");
    require(a.GXStat == b.GXStat && a.NumPushPopCommands == b.NumPushPopCommands &&
            a.NumTestCommands == b.NumTestCommands, "busy/test/matrix flags differ");
    queue_equal(a.CmdPIPE, b.CmdPIPE); queue_equal(a.CmdFIFO, b.CmdFIFO);
    queue_equal(a.CmdStallQueue, b.CmdStallQueue);
}
void replay(GPU3D& gpu, uint32_t value, const Word& decoded) {
    gpu.WriteDecodedGXFIFO(value, decoded.commands.data(), decoded.count);
    gpu.CurCommand = decoded.after.command; gpu.NumCommands = decoded.after.count;
    gpu.ParamCount = decoded.after.param; gpu.TotalParams = decoded.after.total;
}
void write(std::vector<uint8_t>& data, uint32_t index, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[index * 4 + i] = static_cast<uint8_t>(value >> (8 * i));
}
void tests() {
    auto first = std::make_unique<NDS>(); auto second = std::make_unique<NDS>();
    auto& a = first->GPU.GPU3D; auto& b = second->GPU.GPU3D;
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    uint32_t seed = 0x95AB41u;
    // Includes unknown/zero commands, all parameter lengths and arbitrary
    // split points. The reference parser is never replaced or copied here.
    for (unsigned i = 0; i < 20000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const Word decoded = decode(state(b), seed, a.CommandParamCounts());
        a.WriteToGXFIFO(seed); replay(b, seed, decoded); equal(a, b);
        if (i % 100 == 99) {
            a.CmdPIPE.Clear(); a.CmdFIFO.Clear(); a.CmdStallQueue.Clear();
            b.CmdPIPE.Clear(); b.CmdFIFO.Clear(); b.CmdStallQueue.Clear();
        }
    }
    // Deliberately fill and overflow the hardware FIFO/stall queue.
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    for (unsigned i = 0; i < 400; ++i) {
        const uint32_t value = i % 5 == 0 ? 0x20202020u : i;
        const Word decoded = decode(state(b), value, a.CommandParamCounts());
        a.WriteToGXFIFO(value); replay(b, value, decoded); equal(a, b);
    }
    Cache cache;
    std::vector<uint8_t> data(16384);
    std::array<uint32_t, 4> gen{};
    for (unsigned i = 0; i < 4096; ++i) write(data, i, i % 5 ? i : 0x20202020u);
    for (unsigned run = 0; run < 3; ++run) {
        a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
        for (uint32_t offset = 0; offset < 4096; offset += 8) {
            if (run == 1 && offset == 64) { write(data, 65, 0x12345678u); ++gen[0]; }
            if (run == 1 && offset == 1024) { write(data, 1025, 0x10u); ++gen[1]; }
            if (run == 2 && offset == 2048) ++gen[2]; // unrelated page write, same bytes
            const Word* decoded = cache.get(0, 4096, offset, 8, data.data(), gen.data(),
                                             state(b), a.CommandParamCounts());
            require(decoded != nullptr, "cache unexpectedly rejected valid source");
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t value = load(data.data() + (offset + i) * 4);
                a.WriteToGXFIFO(value); replay(b, value, decoded[i]); equal(a, b);
            }
            a.CmdPIPE.Clear(); a.CmdFIFO.Clear(); a.CmdStallQueue.Clear();
            b.CmdPIPE.Clear(); b.CmdFIFO.Clear(); b.CmdStallQueue.Clear();
        }
    }
    require(cache.statistics.hits && cache.statistics.invalidations && cache.statistics.reused_words,
            "did not exercise cached hits and mutation invalidation");
    cache.clear(); gen = {}; write(data, 0, 0x10u);
    a.Reset(); b.Reset();
    require(cache.get(0, 4096, 0, 8, data.data(), gen.data(), state(b), a.CommandParamCounts()) != nullptr,
            "cache reset with reused generations");
    require(!cache.get(0, 16385, 0, 1, data.data(), gen.data(), {}, a.CommandParamCounts()),
            "unbounded cache source accepted");

    // Exercise the real geometry executor after batched ingress: matrix
    // mode/identity, color, begin triangle, three vertices and end.
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    const uint32_t stream[] = {0x10u, 1u, 0x15u, 0x20u, 0x7FFFu,
        0x29u, 0x001F00C0u, 0x60u, 0xBFFF0000u, 0x40u, 0u,
        0x23u, 0u, 0u, 0x23u, 0x1000u, 0u, 0x23u, 0x10000000u, 0u, 0x41u};
    for (uint32_t value : stream) {
        const Word decoded = decode(state(b), value, a.CommandParamCounts());
        a.WriteToGXFIFO(value); replay(b, value, decoded);
        first->ARM9Timestamp += 100; second->ARM9Timestamp += 100;
        a.Run(); b.Run(); equal(a, b);
        require(a.CycleCount == b.CycleCount && a.NumVertices == b.NumVertices &&
                a.NumPolygons == b.NumPolygons &&
                !std::memcmp(a.PosMatrix, b.PosMatrix, sizeof(a.PosMatrix)) &&
                !std::memcmp(a.VertexRAM, b.VertexRAM, sizeof(a.VertexRAM)),
                "geometry/matrix execution differs");
    }
    require(a.NumVertices >= 3 && a.NumPolygons > 0, "geometry fixture did not emit a triangle");

    // Exact integer projection against the cache-disabled retained body.
    // Include repeated shared vertices, collisions, viewport/width changes,
    // the 16-bit W divider boundary, zero W and guest widescreen projection.
    b.NativeProjectionCache.enabled = true;
    for (unsigned i = 0; i < 4096; ++i) {
        const unsigned sample = i / 2;
        Vertex left{}, right{};
        seed = sample * 1664525u + 1013904223u;
        left.Position[0] = static_cast<s32>(seed & 0xFFFF) - 32768;
        left.Position[1] = static_cast<s32>((seed >> 16) & 0xFFFF) - 32768;
        left.Position[2] = seed & 0xFFFFF;
        const u32 ws[] = {0, 1, 0xFFFF, 0x10000, 0xFFFFFF, 0x1001000};
        left.Position[3] = ws[sample % 6];
        left.HiresPosition[0] = 123; left.HiresPosition[1] = 456;
        right = left;
        const u32 widths[] = {256, 448, 384};
        a.SetRenderWidth(widths[(sample / 32) % 3]); b.SetRenderWidth(a.GetRenderWidth());
        a.SetGuestWideProjection((sample / 96) % 2); b.SetGuestWideProjection(a.GetGuestWideProjection());
        const u32 viewport[] = {sample % 2 ? 8u : 0u, 0, 255, 3, sample % 2 ? 128u : 256u, 189};
        std::memcpy(a.Viewport, viewport, sizeof(viewport));
        std::memcpy(b.Viewport, viewport, sizeof(viewport));
        a.ProjectVertex(&left); b.ProjectVertex(&right);
        require(!std::memcmp(&left, &right, sizeof(Vertex)), "cached integer projection differs");
    }
    require(b.NativeProjectionCache.statistics.hits >= 2048 && b.NativeProjectionCache.statistics.misses,
            "projection cache did not exercise hits and misses");
    b.Reset();
    require(!b.NativeProjectionCache.statistics.hits, "projection cache survived reset");
}
}
int main() {
    try { tests(); std::puts("[gx_command_batch_test] PASS: real FIFO/parser/geometry, cache, mutation, reset and stalls"); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
