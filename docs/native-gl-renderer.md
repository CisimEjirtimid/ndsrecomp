# Experimental native GL renderer

Branch `perf/mph-native-gl-20261018` is independent of the connected CPU
regions experiment. It starts from framework `041c4df`; the generated game
banks and the existing MPH math/submission selections are unchanged.

Build with `-DNDS_ACCELERATED_RENDERER=OPENGL` and the existing
`NDS_ENABLE_COMPUTE_RENDERER=ON` infrastructure switch. Use
`NDS_3D_RENDERER=native-gl` (strict) or `auto` (software fallback). The
default framework build selection remains `COMPUTE`. The Windows MPH preview
selects `OPENGL`. Existing launchers' `NDS_3D_RENDERER=compute` setting selects
the compiled accelerated backend; logs and `gpu3d_backend` report its actual
name. `NDS_3D_RENDERER=soft` and
`NDS_ENABLE_COMPUTE_RENDERER=OFF` retain the portable renderer. No renderer
capabilities, imports or GL dependencies are needed by the latter build.

This imports melonDS's native triangle/line rasterizer at the same pinned
commit as the maintained geometry/software engine:
[`e3fa6f4`](https://github.com/melonDS-emu/melonDS/tree/e3fa6f4224e0d706df3ee262ae41cfb0deadc593/src).
It replaces compute scan conversion with native draw calls, depth/stencil
and fragment shaders. It retains the guest FIFO, geometry arithmetic,
cycle accounting, IRQ/DMA boundaries and scheduler. The import is GPL-3.0-or-later;
see `THIRD_PARTY_ATTRIBUTION.md` and applied vendor patch `0022`.

The adapters supply the existing 256..448-wide integer presentation texture,
RGB6/alpha5 and six polygon-ID bits, normalized HD output, flat VRAM inputs,
PBO readback and context-bound resource destruction. The upstream native
clear-bitmap TODO falls back to the portable integer renderer for that job.

## Guest capture and determinism

Native rasterization can differ from the integer reference, particularly at
edges and depth ties. It is an experimental presentation path, not a claim
of cross-driver pixel identity. Native textures and host-selected HD scale
never supply the 3D input to guest VRAM display capture. At VCount215 an
independent job freezes render registers/list and 640 KiB of texture/palette
views. It runs the retained integer rasterizer once, lazily, if a scanline
needs capture. Both direct 3D capture and composited engine-A capture use
that reference line. Capture writes keep their existing scheduler order.

The job survives host renderer/HD reconstruction. It is discarded on guest
reset/load and replaced at the next guest render boundary. It borrows the
immutable geometry render bank until capture lines0..191 complete; the next
VBlank bank swap occurs at line192. It never follows live VRAM coherence
after latching. The fixed copy cost applies each rendered frame; a frame
using capture also pays a software raster, so such scenes may gain less.

This does not qualify netplay or Wiimmfi compatibility. No network protocol,
session negotiation, capability polling or simulation compatibility checks
are relaxed. Practical online and performance qualification remain open.

## Focused development evidence

`NDS_BUILD_NATIVE_GL_TESTS=ON` enables a hidden SDL3/GL context test. It checks
real native clear/triangle draws, widened and HD allocations, packed color
and polygon-ID output, frozen/deferred capture, host reconstruction, abort
and clear-bitmap fallback. It uses no ROM and opens no audio device. Hosts
without the required GL context report a CTest skip, not a pass. Existing
`gpu2d_window_test` additionally exercises both capture source modes with
reference pixels distinct from presentation; `video_savestate_test` covers
the portable reset/load path.

Private development logs, frozen binary, checkpoint and the single MPH
visual sample are under `F:/Projects/ndsrecomp/scratch/native-gl-20261018`.
The full MPH runner and a separate software-only/headless runner built on
Windows/GCC. All three focused tests passed on the available NVIDIA GL
context. The fresh-boot Gorea frame showed the arena, hunter, weapon, HUD
and touch screen without obvious corruption; this is a visual sanity check.
One matched 600-vblank uncapped Gorea pair on SERVER (i5-4670 / GTX760),
with equal warmup, measured 89.7 to 98.7 vblanks/sec (+10.0% throughput)
and 16.1% less process CPU time. Start/end gameplay state, guest event deltas
and HLE call deltas matched. This is one canary sample, not a whole-game
performance or Wiimmfi qualification. The connected-region experiment was
flat and is excluded from the preview. Friends' playtest feedback is pending.

Developer builds may reuse an existing bank archive with
`NDS_PREBUILT_BANK_ARCHIVE` and its required SHA256. The caller must establish
matching generated-bank inputs and runtime ABI; normal builds compile all
banks. The full package inventory and shard-identity checks are unchanged.
