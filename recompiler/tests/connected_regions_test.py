"""Compile and execute a cross-page guest loop with reference/connected emission.

The small runtime models register/flag operations and records every tick's
architectural state. Both variants retain their instruction costs, resume
entries and scheduler exits; only native handoffs may disappear.
"""
from pathlib import Path
import argparse
import hashlib
import re
import struct
import subprocess

HEADER = r'''
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t R[16], cpsr; } Cpu;
typedef struct { void (*fn)(void); uint32_t epoch, target; void* guard; } NdsLinkSlot;
typedef struct { uint32_t addr,size; const uint8_t* expected; } NdsStaticValidationRange;
typedef struct { uint32_t addr,size; const uint8_t* expected; const NdsStaticValidationRange* ranges; uint32_t range_count; } NdsStaticValidation;
typedef struct { uint32_t addr,thumb; void (*fn)(void); const NdsStaticValidation* validation; } NdsDispatchEntry;
extern Cpu g_cpu;
extern uint64_t ticks, digest;
extern uint32_t limit, handoffs;
extern const NdsDispatchEntry* rows;
extern unsigned row_count;
extern uint64_t g_insn_count[2];
#define g_insn_hook_armed 0
#define CPSR_T_BIT 32u
#define NDS_ARM9 0
#define g_nds_active 0
#define NDS_ARM9_CODE_K(pc,t) 1u
#define RUNTIME_TRACE_BRANCH 1
#define RUNTIME_TRACE_CALL 2
#define NDS_LIVE_TRANSFER_B 0
#define NDS_LIVE_TRANSFER_BL 1
#define NDS_LIVE_TRANSFER_BX 2
static inline void runtime_insn_slow(void) {}
static inline uint32_t nds_code_numc(uint32_t pc,uint32_t k) { return 1; }
static inline uint32_t arm9_cycle_combine(uint32_t c,uint32_t d,uint32_t i,uint32_t h) { return c+d+i; }
static inline uint32_t arm7_cycle_combine(uint32_t c,uint32_t d,uint32_t i,uint32_t h) { return c+d+i; }
static inline uint32_t arm9_refill_cycles(uint32_t p) { return 2; }
static inline uint32_t arm7_refill_cycles(uint32_t p) { return 2; }
static inline void arm_set_nzcv_sub(uint32_t a,uint32_t b,uint32_t r) {
 g_cpu.cpsr=(r&0x80000000u)|(r==0?0x40000000u:0)|(a>=b?0x20000000u:0)|(((a^b)&(a^r))>>3&0x10000000u);
}
static inline bool arm_cond_passes_i(unsigned c) { return c==14 || (c==1 && !(g_cpu.cpsr&0x40000000u)); }
static inline bool runtime_unwinding(void) { return false; }
static inline bool runtime_should_yield(void) { return ticks>=limit; }
static inline bool runtime_slice_yield(void) { return ticks>=limit; }
static inline void runtime_tick(uint32_t n) {
 ticks+=n; digest=(digest^n)*1099511628211ull;
 for(unsigned j=0;j<16;++j) digest=(digest^g_cpu.R[j])*1099511628211ull;
 digest=(digest^g_cpu.cpsr)*1099511628211ull;
}
static inline void runtime_trace_event(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e) {}
static inline void runtime_live_transfer(uint32_t a,uint32_t b,uint32_t c) { if(c==NDS_LIVE_TRANSFER_B) ++handoffs; }
static inline int runtime_call_should_return(uint32_t p) { return p==0x02FF0000u; }
void dispatch(void);
static inline void runtime_link_branch(NdsLinkSlot* s) { ++handoffs; if(!runtime_should_yield()) dispatch(); }
static inline void runtime_link_fallthrough(NdsLinkSlot* s) { ++handoffs; if(!runtime_should_yield()) dispatch(); }
static inline void runtime_dispatch_with_exchange(uint32_t p) { if(p!=0x02FF0000u) dispatch(); }
static inline void runtime_dispatch_bad_entry(uint32_t p) { __builtin_trap(); }
'''

DRIVER = r'''
#include "runtime_arm.h"
#include <stdio.h>
#include <stdlib.h>
Cpu g_cpu;
uint64_t ticks,digest;
uint64_t g_insn_count[2];
uint32_t limit,handoffs;
static bool service;
static unsigned native_calls;
extern const NdsDispatchEntry g_dispatch_loop[];
extern const unsigned g_dispatch_loop_len;
const NdsDispatchEntry* rows;
unsigned row_count;
void dispatch(void) {
 if(service && g_cpu.R[15]==0x02000FFCu) {
  ++native_calls; ++g_cpu.R[1]; ++g_cpu.R[2]; g_cpu.R[15]=0x02001000u; runtime_tick(1); return;
 }
 for(unsigned j=0;j<row_count;++j) if(rows[j].addr==g_cpu.R[15]) { rows[j].fn(); return; }
 fprintf(stderr,"missing entry %08x\n",g_cpu.R[15]); exit(9);
}
int main(int argc,char**argv) {
 rows=g_dispatch_loop; row_count=g_dispatch_loop_len;
 g_cpu.R[15]=0x02000FF0u;g_cpu.R[14]=0x02FF0000u;
 unsigned budget=atoi(argv[1]),calls=0;digest=1469598103934665603ull;
 bool spin=argc>2 && argv[2][0]=='s'; service=argc>2 && argv[2][0]=='b';
 while(g_cpu.R[15]!=0x02FF0000u) {
  if(spin && ticks>=192) break;
  if(++calls>1000) return 8;
  limit=ticks+budget; if(spin && limit>192) limit=192; dispatch();
 }
 if(g_cpu.R[0]!=0 || g_cpu.R[1]!=7) return 7;
 if(spin && g_cpu.R[15]!=0x02001008u) return 6;
 if(service && (native_calls!=7 || g_cpu.R[2]!=7)) return 5;
 printf("%llu %llu %u\n",(unsigned long long)ticks,(unsigned long long)digest,handoffs);
 return 0;
}
'''


def run(*args):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--recompiler", type=Path, required=True)
    p.add_argument("--compiler", type=Path, required=True)
    p.add_argument("--work", type=Path, required=True)
    args = p.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    # Two adjacent functions straddle 4KB; the taken loop edge crosses back.
    program = struct.pack("<8I", 0xE3A00007, 0xE3A01000, 0xEAFFFFFF,
                          0xE2811001, 0xE2500001, 0x1AFFFFFC, 0xE12FFF1E, 0xE1A07007)
    image = args.work / "loop.bin"
    image.write_bytes(program)
    config = args.work / "loop.toml"
    config.write_text(f'''[program]
name="connected loop"
id="loop"
cpu="arm9"
isa="armv5te"
load_address=0x02000FF0
size=32
entry_pc=0x02000FF0
authoritative_entry_points=true
[identity]
sha1="{hashlib.sha1(program).hexdigest()}"
[[entry_point]]
addr=0x02000FF0
size=16
mode="arm"
[[entry_point]]
addr=0x02000FFC
size=4
mode="arm"
[[entry_point]]
addr=0x02001000
size=16
mode="arm"
''')
    commands = [args.recompiler, "--config", config, "--bin", image,
                "--bank", "loop", "--validate-live-bytes", "--coalesce-fallthroughs",
                "--validated-live-direct-calls", "--inline-leaf-max-insns", "0"]
    original_config = config.read_text()
    results = {}
    for mode, extra in (("reference", []), ("connected", ["--connected-region-bytes", "16384"])):
        out = args.work / mode
        out.mkdir(exist_ok=True)
        run(*commands, "--out", out, *extra)
        body = (out / "loop.c").read_text()
        table = (out / "loop_dispatch.c").read_text()
        if mode == "connected":
            assert "goto L_02000FFC;" in body
            assert re.search(r"static const NdsStaticValidation \w+ = \{0x02000FF0u, 28u,", table), "incomplete cross-page code proof"
            assert "case 0x02001004u: goto L_02001004" in body, "interior resume lost"
        (out / "runtime_arm.h").write_text(HEADER)
        (out / "driver.c").write_text(DRIVER)
        exe = out / "run.exe"
        run(args.compiler, "-x", "c", "-O1", "-I", out, out / "loop.c",
            out / "loop_dispatch.c", out / "driver.c", "-o", exe)
        results[mode] = [run(exe, str(budget)).split() for budget in (1, 7, 64)]
    for a, b in zip(results["reference"], results["connected"]):
        assert a[:2] == b[:2], ("CPU/tick trace changed", a, b)
    assert int(results["connected"][-1][2]) < int(results["reference"][-1][2]), results
    # A self-spin which is also a named member must yield through dispatch,
    # rather than recursively invoking its enclosing connected leader.
    spin = bytearray(program)
    struct.pack_into("<I", spin, 24, 0xEAFFFFFE)
    image.write_bytes(spin)
    config.write_text(config.read_text().replace(hashlib.sha1(program).hexdigest(),
        hashlib.sha1(spin).hexdigest()) + '\n[[entry_point]]\naddr=0x02001008\nsize=4\nmode="arm"\n')
    spin_results = []
    for mode, extra in (("reference", []), ("connected", ["--connected-region-bytes", "16384"])):
        out = args.work / mode
        run(*commands, "--out", out, *extra)
        run(args.compiler, "-x", "c", "-O1", "-I", out, out / "loop.c",
            out / "loop_dispatch.c", out / "driver.c", "-o", out / "run.exe")
        spin_results.append(run(out / "run.exe", "64", "spin").split())
    assert spin_results[0][:2] == spin_results[1][:2], spin_results
    assert int(spin_results[1][2]), "self-spin did not use runtime dispatch"
    # A native service inside a finder span remains a runtime boundary,
    # including under validated direct-call emission.
    image.write_bytes(program)
    config.write_text(original_config)
    service_results = []
    for mode, extra in (("reference", []), ("connected", ["--connected-region-bytes", "16384"])):
        out = args.work / mode
        run(*commands, "--out", out, *extra, "--region-barrier", "0x02000FFC")
        run(args.compiler, "-x", "c", "-O1", "-I", out, out / "loop.c",
            out / "loop_dispatch.c", out / "driver.c", "-o", out / "run.exe")
        service_results.append(run(out / "run.exe", "64", "barrier").split())
    assert service_results[0][:2] == service_results[1][:2], service_results
    # An aligned 16KB region cannot grow into a fifth code page.
    config.write_text(original_config.replace("02000FF", "02003FF").replace("02001000", "02004000"))
    boundary = args.work / "boundary"
    boundary.mkdir(exist_ok=True)
    run(*commands, "--out", boundary, "--connected-region-bytes", "16384")
    table = (boundary / "loop_dispatch.c").read_text()
    assert not re.search(r"static const NdsStaticValidation \w+ = \{0x02003FF0u, 28u,", table), "region crossed its four-page window"
    for invalid in ("0", "8193", "32768", "4294983680", "16384junk"):
        bad = subprocess.run([str(x) for x in commands + ["--out", args.work / "reject",
                              "--connected-region-bytes", invalid]], capture_output=True)
        assert bad.returncode == 2, (invalid, bad.returncode)
    print("PASS: compiled cross-page loop, tick/register traces, fewer handoffs, self-spin, native service barriers, four-page cap, rejected sizes")


if __name__ == "__main__":
    main()
