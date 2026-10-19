# Guarded connected CPU regions (experimental branch)

`--connected-region-bytes 4096|8192|16384`, together with
`--validate-live-bytes --coalesce-fallthroughs`, connects adjacent same-ISA,
identity-mapped generated members within an aligned region. The default
without this option retains the existing 4KB fall-through-only pass.

Branches between connected members use labels in one native body instead
of another host function or dispatch. Instruction bodies, per-instruction
tick charges, code-write exits, scheduler rendezvous and every interior
resume entry are retained. This removes native handoffs; it does not add
register-local execution or combine instruction timing.

The limit is four pages because the maintained dispatch/link guards have
four page-generation slots. The complete connected span is content-validated
and generation-guarded. The pass does not join different ISAs, code-copy
mappings, gaps, duplicate entry keys, an already-owned preceding member,
or separate aligned windows. It does not enlarge the runtime guard arrays.
Self-spins retain their dispatch/yield path.

Declare every native service boundary with repeatable `--region-barrier
<address>` arguments (Thumb entries use bit 0). A boundary can occur inside
a finder span: the emitter splits it before coalescing. A protected member
stays separate, and direct-call/leaf maps exclude it, so branches, calls and
fall-through still reach the runtime service binding. Barriers must name
real instruction entry points, as with the existing entry-point config.

The matching MPH branch adds `MPH_CONNECTED_REGION_BYTES` (default `0`,
reference) and protects the current math/submission service entries. It also
allows `MPH_RECOMP_DIR` to point at a separate experimental bank directory:

```powershell
cmake -S ../metroidprimehuntersrecomp -B <private-game-build> -DMPH_CONNECTED_REGION_BYTES=16384 -DMPH_RECOMP_DIR=<private-banks>
cmake --build <private-game-build> --target mph_generate_banks
```

Configure a runner against those banks to qualify this branch. Keep the
reference runner against its own original banks and exact-build checkpoint;
do not substitute code into another build's savestate. No cache identity
checks or bank inventory checks are weakened. Codegen identity advances to
`nds-codegen-v4`; old live-shard provider caches invalidate normally.

Focused checks compile and execute the same cross-page ARM loop in both
forms, comparing complete register/flag tick traces at scheduler budgets
1, 7 and 64. The connected form removes handoffs. Named self-spins remain
bounded, a simulated native service inside a finder span executes through
its protected runtime entry, and a region cannot cross its four-page
window. Malformed/oversized options are rejected. Golden emission,
interior-entry fail-closed and live-transfer validation checks also pass.

MPH-derived pilot source around `0x02047000..0x02049000` is generated and
compiled privately to check real instruction coverage. This is development
evidence, not a measured gameplay performance win, full-console equivalence,
or Wiimmfi/cross-host qualification. Default release settings remain intact.
