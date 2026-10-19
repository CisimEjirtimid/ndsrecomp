// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Matthew Stanley. See ../../../LICENSE-GPL.
#pragma once

#include <array>
#include <cstdint>

namespace melonDS {
// Derived integer results only: no guest pointers, renderer resources, time,
// color or polygon state. Full keys qualify hits; collisions only cost work.
class ProjectionCache {
public:
    using Key = std::array<uint32_t, 12>; // clip position, viewport, width, wide flag
    using Result = std::array<int32_t, 4>; // native XY, hires XY
    struct Statistics { uint64_t hits{}, misses{}; } statistics;
    bool enabled = false;
    void clear() { entries = {}; statistics = {}; }
    const Result* find(const Key& key) {
        const auto& entry = entries[index(key)];
        if (entry.valid && entry.key == key) { ++statistics.hits; return &entry.result; }
        ++statistics.misses;
        return nullptr;
    }
    void insert(const Key& key, const Result& result) { entries[index(key)] = {key, result, true}; }
private:
    struct Entry { Key key{}; Result result{}; bool valid{}; };
    std::array<Entry, 512> entries{};
    static uint32_t index(const Key& key) {
        uint32_t h = key[0] * 0x9E3779B1u ^ key[1] * 0x85EBCA77u ^
                     key[2] * 0xC2B2AE3Du ^ key[3];
        return (h ^ (h >> 16) ^ (h >> 8)) & 511u;
    }
};
} // namespace melonDS
