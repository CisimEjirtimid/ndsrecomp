/* SPDX-License-Identifier: GPL-3.0-or-later
 * Packed-command decoding adapted from GPU3D::WriteToGXFIFO.
 * Copyright 2016-2024 melonDS team; batch/cache adaptation 2026 ndsrecomp.
 * The unchanged WriteToGXFIFO implementation remains the reference.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace melonDS::CommandBatch {
struct State {
    uint32_t command = 0, count = 0, param = 0, total = 0;
    bool operator==(const State& rhs) const {
        return command == rhs.command && count == rhs.count &&
               param == rhs.param && total == rhs.total;
    }
};
struct Word {
    State after;
    std::array<uint8_t, 4> commands{};
    uint8_t count = 0;
};

inline Word decode(State state, uint32_t value, const uint8_t* parameters) {
    Word out;
    if (!state.count) {
        state.count = 4;
        state.command = value;
        state.param = 0;
        state.total = parameters[state.command & 255u];
        if (state.total) { out.after = state; return out; }
    } else {
        ++state.param;
    }
    for (;;) {
        if ((state.command & 255u) || (state.count == 4 && !state.command))
            out.commands[out.count++] = static_cast<uint8_t>(state.command);
        if (state.param >= state.total) {
            state.command >>= 8;
            if (!--state.count) break;
            state.param = 0;
            state.total = parameters[state.command & 255u];
        }
        if (state.param < state.total) break;
    }
    out.after = state;
    return out;
}

inline uint32_t load(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
           uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Purely derived cache: no matrix, texture, geometry result, guest pointer or
// completion is cached. Source generations and live decoder state are checked
// at each scheduler slice, so ARM7 writes and another DMA's commands stay live.
class Cache {
    struct Entry {
        uint32_t base = 0, size = 0, first = 0;
        State initial;
        std::vector<uint32_t> raw, generations;
        std::vector<Word> words;
    };
    std::array<Entry, 256> entries{};
    size_t resident = 0;
public:
    struct Statistics {
        uint64_t hits = 0, builds = 0, invalidations = 0;
        // reused_words is the cached word budget offered to a slice. The FIFO
        // may stall before consuming all of it; it is not a transfer-byte count.
        uint64_t decoded_words = 0, reused_words = 0;
    } statistics;

    void clear() { for (auto& entry : entries) entry = {}; resident = 0; statistics = {}; }

    // data starts at physical Main RAM, gen has one item per 4 KiB page.
    // The caller bounds the current slice to its source page and proves the
    // complete source range is contiguous. nullptr means uncached decoding.
    const Word* get(uint32_t base, uint32_t size, uint32_t offset,
                    uint32_t count, const uint8_t* data, const uint32_t* gen,
                    State live, const uint8_t* parameters) {
        if (!size || size > 16384 || offset >= size || count > size - offset ||
            live.count > 4 || live.param > 32 || live.total > 32)
            return nullptr;
        auto& e = entries[((base >> 2) ^ (base >> 11) ^ size) & 255u];
        bool valid = e.base == base && e.size == size && offset >= e.first && !e.words.empty();
        const uint32_t index = offset - (valid ? e.first : offset);
        if (valid) {
            valid = (index ? e.words[index - 1].after : e.initial) == live;
            if (valid) {
                const uint32_t page = (base + offset * 4u) >> 12;
                const uint32_t first_page = (base + e.first * 4u) >> 12;
                if (e.generations[page - first_page] != gen[page]) {
                    const uint32_t begin = std::max(e.first, (page * 4096u > base)
                        ? (page * 4096u - base) / 4u : 0u);
                    const uint32_t end = std::min(size, ((page + 1u) * 4096u - base) / 4u);
                    for (uint32_t i = begin; i < end; ++i)
                        if (e.raw[i - e.first] != load(data + base + i * 4u)) { valid = false; break; }
                    if (valid) e.generations[page - first_page] = gen[page];
                }
            }
        }
        if (valid) {
            ++statistics.hits;
            statistics.reused_words += count;
            return e.words.data() + index;
        }
        ++statistics.builds;
        if (!e.words.empty()) ++statistics.invalidations;
        resident -= e.words.size();
        e = {};
        // Bounded ~7 MiB payload. Evict derived entries rather than growing
        // with a game's transient allocation history.
        const size_t wanted = size - offset;
        for (auto& old : entries) {
            if (resident + wanted <= 262144) break;
            resident -= old.words.size();
            old = {};
        }
        e.base = base; e.size = size; e.first = offset; e.initial = live;
        e.words.reserve(wanted); e.raw.reserve(wanted);
        for (uint32_t i = offset; i < size; ++i) {
            const uint32_t value = load(data + base + i * 4u);
            const Word decoded = decode(live, value, parameters);
            e.raw.push_back(value); e.words.push_back(decoded); live = decoded.after;
        }
        const uint32_t first_page = (base + offset * 4u) >> 12;
        const uint32_t last_page = (base + size * 4u - 1u) >> 12;
        for (uint32_t page = first_page; page <= last_page; ++page)
            e.generations.push_back(gen[page]);
        resident += wanted;
        statistics.decoded_words += wanted;
        return e.words.data();
    }
};
} // namespace melonDS::CommandBatch
