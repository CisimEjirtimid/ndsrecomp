#include "hle_mph_math.h"
#include "hle_vector_math.h"

#include <bit>
#include <array>
#include <cstdint>
#include <cstring>

#include "sha1.h"
#include "state.h"

namespace mph_hle {
namespace {
bool supported_rom = false;
Statistics counters{};

struct Binding {
    uint32_t pc;
    uint32_t size;
    const char* sha1;
    Function function;
};
// Fingerprints, not ROM payloads. Only full routine entries are replaced.
constexpr Binding bindings[] = {
    {0x02080BD8u, 96u, "a62a720989cd408241a1acc35520b93d866bf7df", scale_add},
    {0x02080DE0u, 156u, "8a97f6bdacd40175ea4f577788a82635a8057984", cross_product},
    {0x0207FD38u, 180u, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
    {0x0204E7E0u, 180u, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
};
std::array<std::array<uint8_t, 544>, 2> matrix_proofs{};
NdsStaticValidation matrix_validations[] = {
    {0x01FF8C2Cu, 544u, matrix_proofs[0].data(), nullptr, 0},
    {0x0207FDECu, 544u, matrix_proofs[1].data(), nullptr, 0},
};

} // namespace

bool initialize(const char* rom_sha1) {
    supported_rom = rom_sha1 &&
        std::strcmp(rom_sha1, "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") == 0;
    counters = {supported_rom, 0, 0, 0, 0};
    return supported_rom;
}

Statistics statistics() { return counters; }

MatrixReplacement resolve_matrix(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!supported_rom || cpu != NDS_ARM9 || thumb) return {};
    for (auto& validation : matrix_validations) {
        if (pc != validation.addr) continue;
        std::array<uint8_t, 544> live{};
        if (!bus_range_has_write_provenance(pc, live.size()) ||
            !bus_debug_copy(0, pc, live.data(), live.size()) ||
            gba::sha1(live.data(), live.size()).hex() !=
                "33407f601fee3cc8d211ff511f571e141ebec689")
            return {nullptr, &validation};
        matrix_proofs[&validation - matrix_validations] = live;
        return {matrix_concat, &validation};
    }
    return {};
}

Function resolve(NdsCpu cpu, uint32_t pc, bool thumb,
                 const NdsStaticValidation* validation) {
    if (!supported_rom || cpu != NDS_ARM9 || thumb || !validation ||
        !validation->expected || pc < validation->addr) return nullptr;
    for (const auto& binding : bindings) {
        if (binding.pc != pc) continue;
        const uint32_t offset = pc - validation->addr;
        if (offset > validation->size ||
            binding.size > validation->size - offset) return nullptr;
        if (gba::sha1(validation->expected + offset, binding.size).hex() !=
            binding.sha1) return nullptr;
        return binding.function;
    }
    return nullptr;
}

void scale_add() { ++counters.scale_add_calls; nds_hle::scale_add(); }
void cross_product() { ++counters.cross_product_calls; nds_hle::cross_product(); }
void transform_translate() { ++counters.transform_translate_calls; nds_hle::transform_translate(); }
void matrix_concat() {
    ++counters.matrix_concat_calls;
    const uint32_t a = g_cpu.R[0], b = g_cpu.R[1], out = g_cpu.R[2];
    // One complete affine job. Snapshotting the two inputs supports the SDK's
    // exact in-place aliases without guest stack traffic or intermediate ticks.
    std::array<uint32_t, 12> left{}, right{}, result{};
    for (unsigned i = 0; i < 12; ++i) {
        left[i] = nds_hle::load(a + i * 4u);
        right[i] = nds_hle::load(b + i * 4u);
    }
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 3; ++col) {
            const auto sum = nds_hle::product(left[row * 3], right[col]) +
                nds_hle::product(left[row * 3 + 1], right[col + 3]) +
                nds_hle::product(left[row * 3 + 2], right[col + 6]);
            result[row * 3 + col] = nds_hle::q12(sum) + (row == 3 ? right[col + 9] : 0u);
        }
    for (unsigned i = 0; i < 12; ++i) nds_hle::store(out + i * 4u, result[i]);
    nds_hle::finish(384u);
}
} // namespace mph_hle
