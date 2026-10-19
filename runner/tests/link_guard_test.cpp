#include "runtime_arm.h"
#include "state.h"
#include "dispatch_stats.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t pc = 0x02010000u;
constexpr uint32_t inner_pc = pc + 0x1000u;
constexpr uint32_t nop = 0xE1A00000u;
const uint32_t expected[2] = {nop, nop};
NdsStaticValidation proof{pc, sizeof expected,
    reinterpret_cast<const uint8_t*>(expected), nullptr, 0};
NdsStaticValidation inner_proof{inner_pc, sizeof expected,
    reinterpret_cast<const uint8_t*>(expected), nullptr, 0};
const uint32_t changed = nop ^ 1u;
NdsStaticValidation changed_proof{pc, sizeof changed,
    reinterpret_cast<const uint8_t*>(&changed), nullptr, 0};
unsigned entered[2]{}, alternate = 0, rejected = 0;
bool nested_unwound = false;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void body() { ++entered[g_nds_active]; }
void other_body() { ++alternate; }
void fallback() { ++rejected; }
void writer() { bus_write_u32(pc, nop ^ 1u); }
NdsLinkSlot inner_slot{nullptr, 0, inner_pc, nullptr};
void outer() {
    runtime_link_call(&inner_slot);
    nested_unwound = runtime_should_yield();
}
void call(NdsLinkSlot& slot, int cpu = 0) {
    g_nds_active = cpu == 0 ? NDS_ARM9 : NDS_ARM7;
    g_cpu.cpsr &= ~CPSR_T_BIT;
    runtime_link_call(&slot);
}
}

int main() {
    runtime_init(nullptr);
    bus_init();
    cp15_reset();
    runtime_set_deep_trace(0);
    bus_write_u32(pc, nop);
    bus_write_u32(pc + 4u, nop);
    bus_write_u32(inner_pc, nop);
    bus_write_u32(inner_pc + 4u, nop);
    const DispatchEntry rows[] = {
        {pc, 0, fallback, &changed_proof}, {pc, 0, body, &proof},
        {pc + 4u, 0, other_body, &proof},
    };
    nds_register_dispatch(0, rows, 3u, 0u);
    nds_register_dispatch(1, rows, 3u, 0u);
    NdsLinkSlot first{nullptr, 0, pc, nullptr};
    NdsLinkSlot second{nullptr, 0, pc, nullptr};
    call(first); call(second);
    require(entered[0] == 2u && first.guard && second.guard, "both validated callsites execute");
#if defined(NDS_LINK_GUARD_REUSE)
    require(first.guard == second.guard, "same target shares its proof record");
#else
    require(first.guard != second.guard, "portable original allocator remains selectable");
#endif
    NdsLinkSlot cpu7{nullptr, 0, pc, nullptr};
    call(cpu7, 1);
    require(entered[1] == 1u && cpu7.guard != first.guard, "CPU proof identity stays separate");
    NdsLinkSlot different_fn{nullptr, 0, pc + 4u, nullptr};
    call(different_fn);
    require(alternate == 1u && different_fn.guard != first.guard, "distinct bodies do not share guards");
    g_nds_active = NDS_ARM9;
    bus_write_u32(pc, nop ^ 1u);
    call(first); call(second);
    require(rejected == 2u && entered[0] == 2u, "every alias rejects modified code");
    bus_write_u32(pc, nop);
    call(first); call(second);
    require(entered[0] == 4u, "restoration revalidates every alias");
    for (unsigned i = 0; i < 256u; ++i) {
        const void* prior = first.guard;
        bus_write_u32(pc, nop); // exact same bytes, new page generation
        call(first); call(second);
#if defined(NDS_LINK_GUARD_REUSE)
        require(first.guard == prior && second.guard == prior, "refresh reuses a stable proved record");
#else
        (void)prior;
#endif
    }
    const DispatchEntry nested[] = {
        {pc, 0, outer, &proof}, {inner_pc, 0, writer, &inner_proof},
    };
    nds_unregister_dispatch(0, rows, 3u);
    nds_register_dispatch(0, nested, 2u, 0u);
    call(first); // stale slot must resolve under the new registration epoch
    require(nested_unwound, "nested write invalidates the caller's private active snapshot");
    nds_unregister_dispatch(0, nested, 2u);
    nds_unregister_dispatch(1, rows, 3u);
    runtime_init(nullptr); // stale slot keys must not dereference the freed pool
    bus_init(); cp15_reset(); runtime_set_deep_trace(0);
    bus_write_u32(pc, nop); bus_write_u32(pc + 4u, nop);
    nds_register_dispatch(0, rows, 3u, 0u);
    const unsigned before = entered[0];
    call(first);
    require(entered[0] == before + 1u, "reset invalidates surviving callsite slots");
    std::puts("link guard sharing, code mutation, CPU/body identity, nested writes and reset pass");
}
