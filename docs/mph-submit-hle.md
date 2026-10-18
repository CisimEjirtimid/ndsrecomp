# MPH native submission service

Tracker: `beads-yjp.95`; branch `perf/mph-submit-hle-20261018`.

## Why this boundary

The existing MPH vector pilot covered about 1.6% of the Oct 3 self samples.
The same profile found 10.99% in cycle helpers, 8.62% in dispatch, 6.19% in
bus/memory and 5.77% in DMA/timer/IRQ helpers. These overlapping costs cannot
be summed into a promised speedup. The earlier shared geometry-poll service
(`beads-qpws`, draft PR29) produced no material whole-workload win.

The selected family combines `0x01FF9420` DMA setup and `0x01FF9470`
synchronous geometry submission, including its polling continuations. Native
code replaces nested IRQ-wrapper calls, stack traffic, instruction dispatch,
472-byte chunk construction and per-instruction polling. Actual DMA movement
and geometry consumption remain scheduled devices. This is deliberately a
larger boundary than a faster arithmetic leaf or an idle geometry poll.

## Donor implementation assessment

Inspected local xboxlle source and measured reports:

| Mechanism | Actual evidence | Application here |
|---|---|---|
| Whole native renderer | Common device commands/interrupts feed an independent GPU draw backend | Own a complete service behind the required observable interface |
| Original vertex streams and upload rings | 175,545,669 expanded vertices avoided; 123-VBlank menu interval 10.459 to 8.861 s (15.28% reduction), with foreign-load caveat | Avoid reconstructing intermediate representations and per-word wrappers where the consumer can accept a batch |
| Connected native CPU regions | 6,581,932 intermediate register transfers avoided; interval 11.499 to 9.472 s (17.63%), with foreign-load caveat | Remove repeated call/dispatch/state transitions across connected native work |
| Blinx compiler optimization | `/Od` to `/O2 /fp:precise`: process CPU/update 54.25 to 38.45 ms, roughly 29% lower | Check actual generated/runtime compiler settings; NDS title banks already use `-O2`, runner Release uses optimization |

Sources: xboxlle `docs/HLE-RENDERER.md`, `docs/RESULTS-SPIKE-RAW-STREAMS-2026-10-03.md`,
`docs/RESULTS-SPIKE-NATIVE-REGIONS-2026-10-03.md`, and Blinx's framework
`docs/NATIVE-HLE-OPTIMIZATION-2026-10-18.md`. Do not multiply their independent
screening gains or present them as an NDS prediction. The XDK adapter also
retains real allocation/completion while replacing command construction.

Reviewed emulator source, with no copied implementation in this candidate:

- [melonDS DMA](https://github.com/melonDS-emu/melonDS/blob/906e9ebb27da8c6a715cd7abab4abfe8a8d29427/src/DMA.cpp),
  GPL-3.0-or-later: per-unit timing/transfer loop, request-limited FIFO DMA and
  scheduled completion. Much of the corresponding geometry/renderer machinery
  already exists in ndsrecomp. Preserve its device synchronization while
  removing guest-side setup/polling. A newer source pin alone is not a speedup.
- [DeSmuME BIOS HLE](https://github.com/TASEmulators/desmume/blob/1275dc64f5d1f5ef18dc1bc1fa9e012024b6df1a/desmume/src/bios.cpp),
  GPL-2.0-or-later: native whole-buffer copy/decompression and arithmetic
  services replace instruction streams and use coarse service costs. Its
  [geometry implementation](https://github.com/TASEmulators/desmume/blob/1275dc64f5d1f5ef18dc1bc1fa9e012024b6df1a/desmume/src/gfx3d.cpp)
  still owns guest command parsing and CPU-side geometry; hardware rendering
  does not automatically remove submission work. GPLv3-compatible portions can
  be adapted inside the GPL runner with notices; no code was imported here.
- [NooDS ARM7 HLE](https://github.com/Hydr8gon/NooDS/blob/4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0/src/core/hle/hle_arm7.cpp),
  GPL-3.0-or-later: a strong many-to-one IPC service boundary, but this reviewed
  implementation only handles touch commands and echoes unknown commands.
  That incomplete behavior cannot supply MPH sound/Wiimmfi contracts. Its
  [DMA implementation](https://github.com/Hydr8gon/NooDS/blob/4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0/src/core/memory/dma.cpp)
  batches work at scheduled tasks and retains FIFO request limits. It motivates
  a later measured transfer-batch backend, not copying unsupported completion
  behavior into the online build.

## Caller contract and selection

`NDS_MPH_SUBMIT_IMPLEMENTATION=LLE|HLE` is a developer build choice, independent
of the existing vector selector. Generic default: LLE. HLE records
`mph-submit-hle-v1` in build/save identity and startup diagnostics. The
read-only `mph_submit` debug command reports scope and execution counters.
There is no runtime selector or promise of HLE/LLE cross-build state portability.

Supported scope: AMHE USA rev0 ROM SHA1 `90164d1ac127ee5f9815ea4ae7de798c7b5fc629`,
ARM9 ARM mode, fingerprinted code/literals `0x01FF9420..0x01FF951B` (SHA1
`37571edeb9fb5166d791281d814b662d138b6ef4`). The whole proof, including rejected
proofs, participates in existing generation guards. It also covers the replaced
IRQ helpers at `0x020882E0` (44 bytes) and argument checker at `0x02088728`
(112 bytes); changing those dependencies invalidates hits and declines.
No ROM bytes are committed.

Setup inputs: r0 channel 0..3, r1 source, r2 destination, r3 DMA control.
It arms the real device transaction atomically and returns. Submission inputs:
r0 channel 0..3, r1 word-aligned ordinary main-RAM command buffer, r2 byte size
(multiple of four; zero is a no-op). A nonzero buffer must remain within the
main-RAM address window. Unsupported inputs stop explicitly rather than return
success. Other ROMs or changed resident code execute their existing native path.

Submission retains the original callee-saved register/return frame in guest
memory and uses original dispatch continuation addresses. It waits for a prior
transfer, submits at most 472 bytes per transaction, and returns only after the
real DMA enable bit clears. Source bytes are read by DMA at consumption time;
they are not speculatively copied into a hidden immutable snapshot. Existing
DMA registers, FIFO command order, stalls, IRQs and transfer timing remain.
Dead instruction temporaries and per-instruction traces are not reproduced.

Native setup charges 80 ARM9 cycles; entry 48; return 16. Waiting advances
at most to the existing published CPU rendezvous (bounded by 128 cycles).
These are deterministic coarse CPU service costs, not exact guest instruction
costs. ARM7, timer, display, Wi-Fi deadlines and DMA transfer scheduling are
unchanged. IRQ delivery can move to service boundaries. Native operations
publish their continuation before timing/unwind, so resume does not duplicate
chunks. Private HLE continuation state does not exist outside guest snapshots.

## Validation record

Focused `hle_mph_submit_test` passes, including a local fingerprint-checked
private runtime image: all four channels, zero/word/chunk/remainder sizes,
prior active DMA, output order, delayed source mutation, callee-saved registers,
Thumb return, unwind, plain CPU/RAM/device snapshot continuation, CPU/mode/ROM
and whole-body proof rejection. Device behavior is mocked in this unit suite;
it is not Wiimmfi, cross-host determinism or full gameplay qualification.

Prospective runtime evidence: one diagnostic window plus one matched pair of
600 VBlanks, native compute rendering, existing vector HLE unchanged, governor
off, no instruction event hook during timing, output muted. `run_vblanks`
advances at scheduler round boundaries without the per-instruction hook of
`run_to_event`. Report actual progress and whole-process CPU as well as wall
throughput. Promotion, real-device evidence and owner/online qualification
remain pending until recorded below.

## Bounded Windows canary results (2026-10-18)

One diagnostic window and the original matched pair were followed by one
reversed pair because the host census proved asymmetric interference. Each
interval advances 600 actual VBlanks with the same fire input, active Gorea
phase 2, native-resolution compute rendering and no instruction event hook.
Vector HLE is unchanged. Background audio output is muted; this is headless
GPU-rendered execution, not desktop present FPS or an interactive pacing test.

| Order / implementation | Wall seconds | ms/VBlank | VBlanks/s | Whole-process CPU seconds |
|---|---:|---:|---:|---:|
| First / LLE | 3.591950 | 5.9866 | 167.04 | 4.781250 |
| Second / HLE | 3.549750 | 5.9163 | 169.03 | 4.062500 |
| Reverse first / HLE | 4.371910 | 7.2865 | 137.24 | 5.828125 |
| Reverse second / LLE | 5.151902 | 8.5865 | 116.46 | 5.656250 |

**Decision: performance benefit is unqualified; keep the candidate draft and
submission LLE as the default.** The initial pair suggests 15.0% less process
CPU but only 1.19% higher throughput. The reverse suggests 17.84% higher
throughput but 3.04% more process CPU. These inconsistent observations are not
a validated speedup. Do not select a preferred leg, average away interference,
or claim that lower-end slowdown has been fixed. No further gameplay timing
was collected after this bounded repeat.

The per-leg process censuses explain the qualification problem. Two unrelated
MMX4 processes consumed about 12.83 CPU seconds during the first HLE leg,
absent from its baseline. During the reverse HLE leg, a compiler consumed
7.61 CPU seconds, an Unreal job 4.83 and a Yoshi runner 3.97. Their presence
was discovered from actual before/after counters, not guessed from process
names. Other background desktop/VM work also continued. A quiet matched pair
is still required; do not run a repeat-until-pass matrix on this host.

Useful functional and traffic evidence survives that limitation:

- Both implementations built and started; the new service unit suite and the
  existing dispatch/dependency lookup suite passed. The private binding fixture
  includes helper modifications and malformed buffer rejection before effects.
- All four timing intervals completed their full 600 VBlanks, each with exactly
  672,228,000 ARM9 and 336,114,000 ARM7 cycles of progress, and zero interpreter
  fallback during the interval. These clocks describe equal simulated progress,
  not equality of internal execution or cross-build deterministic compatibility.
- Every interval starts with one active unpaused player in room 92, health 799,
  weapon 0 and position `c7edffff1f6a02004da5ffff`. Every interval ends in the
  same room/state, health 689 and position `d5edffff1f6a0200d5a3ffff`.
- The initial HLE interval executes 13,277 submissions, 100,621 chunks and
  44,006,400 command bytes. ARM9 retired instructions fall from 57,867,043 to
  53,320,197 (about 7.86%); instruction removal alone is not a speedup claim.
- One current HLE image was inspected: ordinary Gorea gameplay, geometry and
  HUD, without visible garbling. This is basic sanity, not campaign/network
  qualification. No additional campaign or multiplayer validation session ran.
- Submission preparation finishes ten global VBlanks earlier in the HLE arm.
  The separately prepared same-build scenes have equal recorded player state;
  their absolute boot histories are not claimed identical. Event counters are
  diagnostic and monotonic across loads; compare interval deltas.

The diagnostic has 842 emulation-thread samples: generated bodies 23.16%,
cycle/helpers 8.91%, dispatch 9.38%, bus/memory 6.41%, DMA/timer/IRQ helpers
4.99%, GPU3D 9.86%, GPU driver 5.58%, wait/idle 13.66%. Stack unwinding reports
read failures in 97.4% of samples, so self counts are the ranking evidence.
Its separate 600-frame interval took 3.367 s; it is not substituted for the
uninstrumented baseline. Sampled emulation bucket estimates are diagnostic,
not a normalized accounting or a promotion gate.

Raw evidence and frozen executables are kept only in the workspace:
`F:/Projects/ndsrecomp/scratch/mph-submit-hle-20261018/` (`lle`, `hle`,
`lle-reverse`, `binaries`). Frozen executable hashes are in each arm's
`process.json`. The identical native-bank archive used in both builds has
SHA256 `ee36ba31bda670612de6372324eea1f236450c6840acb69838fb723030fa9f89`.
The LLE build identity is `3a57236-dirty`; HLE is
`3a57236-dirty-mph-submit-hle-v1`. Binary SHA256, source changes and build
configuration identify these working-tree candidates; no cross-build state
identity or public release is asserted.

## SERVER low-end canary (2026-10-18)

The owner requested an additional bounded pair on NAS/SERVER. `F:/NAS/README.md`
documents the existing key-based SSH connection. The actual target is Windows
10 Pro x64 build 19044, an Intel Core i5-4670 (4 cores/4 threads), 16 GiB RAM,
and a GeForce GTX 760 with 2 GiB VRAM and NVIDIA driver 456.71. Its existing
Balanced power plan was retained. A hidden WGL probe over SSH reported hardware
OpenGL 4.6; both runners subsequently initialized their actual OpenGL 4.3 compute
renderer, persistent-map readback, two 2D workers and one compositor helper.

Used **one matched 600-VBlank pair**, LLE then HLE. Each arm received the same
120-VBlank untimed renderer warm-up and then reloaded its own original exact-build
checkpoint through the normal identity checks. The transferred frozen executable
SHA256 values match the desktop manifests. No recompilation, cross-build identity
override, fresh campaign traversal or multiplayer session was needed. One process
ran at a time, audio output was dummy, networking and save persistence were off,
the governor was off and timing was uncapped. As on the desktop, this is native
256x192 headless rendering, not the owner's 448-wide interactive presentation.

| Submission build | 600 VBlanks wall time | ms/VBlank | VBlanks/s | Process CPU time |
|---|---:|---:|---:|---:|
| LLE | 6.656460 s | 11.0941 | 90.1380 | 8.078125 s |
| HLE | 6.451372 s | 10.7523 | 93.0035 | 8.265625 s |

HLE shows **3.18% greater throughput / 3.08% lower wall time** in this pair,
while process CPU time **increases 2.32%**. This modest observation is not a
qualified material gain, and does not justify promoting the default. The server
is usable as a lower-end canary, but it is not idle: a five-second preflight used
5.25 CPU seconds across its four cores. Around the measured intervals, other
processes consumed 4.265625 CPU seconds over 6.903 s for LLE and 3.781250 over
6.697 s for HLE. Java and WMI account for most of that load; no unrelated process
started or exited between either pair of census snapshots. These snapshots bracket
the measurement and nearby debug/profile work rather than just the timed call.
The modest wall-time difference cannot be separated confidently from that load
and scheduling variation using this one pair. No services were stopped or tuned.

Both intervals finish all 600 VBlanks without a terminal CPU or interpreter
fallback. Guest cycle deltas, initial/final health, room and position exactly
match the desktop observations above. HLE again performs 13,277 submissions,
100,621 chunks and 44,006,400 bytes, removing 4,546,846 retired ARM9 instructions.
However, scheduler rounds rise from 3,058,258 to 3,119,390 and GX-stalled rounds
from 121,010 to 146,990; instruction removal has not established a CPU-time win.
The real per-word DMA, command decode and geometry work still run. One HLE
framebuffer from SERVER was inspected and shows normal Gorea gameplay/HUD.
Matching these selected observations does not prove complete state equivalence,
cross-host netplay determinism or Wiimmfi interoperability.

The already-enabled sampler was dumped after each measurement without another
simulation leg. In HLE's 1,669 emulation-thread self samples, generated code is
33.79%, dispatch 9.11%, runtime/cycle helpers 7.37%, GPU3D 9.71%, bus/memory
6.11%, DMA/timer/IRQ devices 5.75% and scheduler/sync 3.48%. This slightly wider
6.67-second window includes checkpoint-load/debug overhead (state I/O is 2.40%).
Unwinding fails in 98.1% of samples, so use self counts rather than inclusive
totals. Only the hash-verified runner is mapped to local symbols; different
SERVER OS/driver binaries are left unresolved instead of using this PC's DLLs.
Those buckets rank investigation targets; they are not exact recoverable time.

This strengthens the case for larger guest computation or command-buffer batches
that avoid repeated work across the DMA/geometry boundary, as xboxlle does.
Replacing the submission wrapper alone leaves most work intact. The native GL
renderer remains a licensed donor opportunity, but this CPU profile does not
establish GPU rasterization as the largest bottleneck on SERVER.

Private evidence: `scratch/mph-submit-hle-20261018/server/` contains hardware,
idle census, summary, per-arm measurement/census manifests, raw profiles,
symbolization and framebuffers. The remote private workspace is
`C:/Users/matts/ndsrecomp-canary/20261018`; `server_assess.py lle|hle` records the
bounded procedure and reuses `tools/measure_mph_submit.py`. Both owned runner
processes were stopped after collection. The server's Nextcloud, PostgreSQL and
SSH services remain running. LLE remains the supported default; Wiimmfi and
practical owner playtest qualification remain open.

## Larger follow-up opportunities from the donor comparison

1. **Native GPU rasterization.** melonDS's maintained GPL-3.0-or-later
   [GLRenderer at the already vendored base revision](https://github.com/melonDS-emu/melonDS/blob/e3fa6f4224e0d706df3ee262ae41cfb0deadc593/src/GPU3D_OpenGL.cpp)
   submits triangle/line batches with `glDrawElements`, using native raster,
   depth/stencil and fragment shaders. ndsrecomp currently vendors its compute
   rasterizer, not this alternative. This is the closest direct NDS donor for
   xboxlle's native graphics approach, with the same `Renderer3D` interface at
   that revision and OpenGL 3.1-era requirements. It could help older GPUs that
   cannot use the current GL4.3 compute path, as well as GPU-bound scenes.
   No performance win is asserted without a port and measurement. Reuse the
   renderer, shader and texture-cache code with its notices; adapt widened
   surfaces, RGB6/alpha5 and polygon attributes, capture/readback, texture
   handles and GL lifecycle. The current bridge assumes a ComputeRenderer and
   packed integer output, so changing the constructor alone would be incorrect.
   Keep guest-visible captures/results coherent and qualify any numerical
   effects before making online compatibility claims.
2. **Cache/batch repeated command buffers.** Read-only inspection of the
   existing post-run DMA ring found 7,962 GXFIFO transfers among the last 8,192
   completions, but only 336 distinct source/count pairs across 40 source
   pages. Some pairs repeat 24 times within 25,905,493 system cycles (about
   46.2 frames); 6,905 transfers are full 118-word chunks. This is observed
   address/range reuse, not proof of unchanged contents. An xboxlle-style
   resident/predecoded command service should use source generations/content,
   parser continuation state and resource invalidation, then preserve actual
   command effects and completion. Caching only by address or caching rendered
   results without matrix/texture/state inputs would violate the contract.
3. **Larger title computation families.** Generated arithmetic still occupies
   the largest single self category. Target an attributed transform/animation
   or collision batch with its callers rather than treating frequent tiny IRQ
   leaves as expensive computation. Earlier leaf-inlining evidence was neutral;
   names or dispatch counts alone do not establish recoverable time.

The current change supplies the broader service boundary and reproducible
canary tooling. It does not claim completion of those larger renderer/cache
ports. Wiimmfi, external-peer interoperability and cross-host deterministic
qualification remain open. Per AGENTS.md, owner playtest handoff and default
promotion follow focused checks plus a material measured gain; no unqualified
candidate was launched as a completed QA handoff.

## Reproduction

Configure the same source, bank archive and compiler settings twice, changing
only `NDS_MPH_SUBMIT_IMPLEMENTATION=LLE|HLE`. Keep math HLE in both arms and
freeze each executable before changing the build selection. In the game repo:

```powershell
.venv/Scripts/python.exe tools/measure_mph_submit.py prepare --exe <frozen-runner> --out <private-arm-directory> --port 19948
.venv/Scripts/python.exe tools/measure_mph_submit.py measure --out <private-arm-directory>
```

`prepare` uses a fresh same-build boot and records its data-only assists.
For an explicitly justified reverse pair, reuse only that exact binary's
checkpoint with `--checkpoint <same-build-active.state>`; normal core identity
checks remain enabled. `--label reverse` preserves the initial timing JSON.
Stop only the recorded owned PID before starting the next arm. Do not reuse
another build's state or disable build identity checks to make a sample work.
