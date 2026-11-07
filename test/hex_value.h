#pragma once

// Hex digits of a map, listing or symbol line as a fixed 64-bit value. `unsigned long` is 32 bits on Windows, so
// std::stoul throws std::out_of_range on a map constant such as `__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF`
// (GH #477: it killed c_value_test under wine). Never throws.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>

/// The hex digits of `s` starting at `pos` (they end at the first non-hex character), into `out`. False: no digit there, or more than 64 bits.
inline bool parse_hex64(const std::string& s, size_t pos, uint64_t* out) {
    if (pos >= s.size()) return false;
    const char* first = s.data() + pos;
    const char* last = s.data() + s.size();
    uint64_t v = 0;
    const auto r = std::from_chars(first, last, v, 16);
    if (r.ec != std::errc()) return false;
    *out = v;
    return true;
}
