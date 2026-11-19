#pragma once
// GH #370 - the Qt-free half of the Memory panel's Find / Save / Load / Fill tools: the pattern
// grammar and matcher, the fill value, the hex length, and how a block splits into the pokes the
// backend's verbs can take. Std headers plus debug/mem_space.h only; no file I/O (the panel does
// that), so any frontend can use it and its suite runs in every build tree. A sibling of
// disasm_hint.h, not part of the four frozen facade headers.

#include "debug/mem_space.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jnext {
namespace dbg {
namespace memtools {

/// A byte pattern: a byte matches when (m & mask) == (bytes & mask). Same size.
struct Pattern {
    std::vector<uint8_t> bytes, mask;
};

/// Hex bytes: whitespace-separated tokens, each an even run of [0-9A-Fa-f?] read in pairs
/// ("CD3412" == "CD 34 12"); '?' is a nibble wildcard (mask nibble 0). `mask_text`, when not
/// blank, is hex bytes WITHOUT '?' and must have the pattern's byte count; the effective mask is
/// its AND with the wildcard mask. nullopt + *why on refusal.
std::optional<Pattern> parse_hex_pattern(std::string_view text, std::string_view mask_text,
                                         std::string* why);

/// Printable ASCII (0x20-0x7E). match_case=false: each letter is stored & 0xDF with mask 0xDF,
/// every other byte mask 0xFF.
std::optional<Pattern> text_pattern(std::string_view text, bool match_case, std::string* why);

/// Fill value: hex bytes, no '?', 1 or more.
std::optional<std::vector<uint8_t>> parse_fill(std::string_view text, std::string* why);

/// `value` repeated cyclically to exactly `len` bytes (the last repetition truncated).
std::vector<uint8_t> fill_buffer(const std::vector<uint8_t>& value, size_t len);

/// Hex length: optional "$" or "0x", 1-8 hex digits. nullopt + *why otherwise. 0 is parsed
/// (split_block refuses it).
std::optional<uint32_t> parse_length(std::string_view text, std::string* why);

struct Hit {
    size_t offset;
    bool   wrapped;
};

/// First full match at or after `from`; when there is none, the first before `from`
/// (wrapped = offset < from). A match never runs past `size`; `from` beyond the last start wraps.
std::optional<Hit> find(const uint8_t* data, size_t size, const Pattern& p, size_t from);

struct Chunk {
    MemSpace space;
    uint32_t addr;
    uint32_t len;
};

/// A block as the backend's verbs reach it:
///   Cpu      one chunk, never wraps (end <= 0x10000);
///   Page{p}  one chunk per 8K page from p, running on through p+1 ... up to page 0xDF;
///   Rom{i}   one chunk within the 16K image.
/// nullopt + *why when it does not fit.
std::optional<std::vector<Chunk>> split_block(MemSpace s, uint32_t addr, uint32_t len,
                                              std::string* why);

/// The most bytes a block from `addr` can hold (0 when the start itself is out):
/// 0x10000-addr, (0xE0-p)*0x2000-addr, 0x4000-addr.
uint32_t block_room(MemSpace s, uint32_t addr);

}  // namespace memtools
}  // namespace dbg
}  // namespace jnext
