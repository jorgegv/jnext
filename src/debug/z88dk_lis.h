#pragma once

// ---------------------------------------------------------------------------
// The z88dk `--list` listing adapter (GH #348, E23-17; design 2.3, 9.5):
// z80asm's `<file>.lis` listings, read beside a `-debug` or plain map, give
// source lines the map alone cannot: every assembly line, sdcc's C lines on
// z88dk 2.4 (the `;file:line:` comments of --c-code-in-asm) and the start-up
// code's text. std headers and the debug layer only; no Qt.
// ---------------------------------------------------------------------------

#include "debug/mem_space.h"
#include "debug/source_map.h"
#include "debug/z88dk_space.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

/// One logical row of a listing: a source line with its bytes (the rows that
/// continue past 8 bytes are folded in) and the file header in force.
struct Z88dkListingRow {
    int phys = 0;              // physical line of the first row, one-based
    int col = 0;               // z80asm's line-number column; 0 when blank
    int header = 0;            // index into Z88dkListing::headers
    bool has_addr = false;
    uint32_t addr = 0;         // the listed (section-relative or phased) address
    std::vector<uint8_t> bytes;
    std::string text;
};

struct Z88dkListing {
    std::vector<std::string> headers;   // the `<file>:` lines, without the final ':'
    std::vector<Z88dkListingRow> rows;
};

/// Tokenise a listing strictly. nullopt, with `why` naming the physical line,
/// for a file that is not a z80asm listing.
std::optional<Z88dkListing> parse_z88dk_listing(const std::string& path, std::string& why);

/// Where `place` puts a (section, value) pair: the memory space and the 16-bit
/// address in it, or the reason it cannot be put anywhere.
struct Z88dkPlacement {
    bool ok = false;
    jnext::dbg::FetchSource space;
    uint16_t lo = 0;
    std::string why;
};

/// What the map side hands the listing side, read only.
struct Z88dkMapView {
    const std::vector<Z88dkMapLine>* symbols = nullptr;       // `addr` symbols that are not line records
    const std::vector<Z88dkMapLine>* line_records = nullptr;  // `__C_LINE_` / `__ASM_LINE_` in a non-empty section
    const std::map<std::string, uint64_t>* bounds = nullptr;  // const `__<group>_head` / `_tail`
    std::set<std::string> modules;            // every module the map names
    std::function<Z88dkPlacement(const std::string& section, uint64_t value)> place;
    /// The byte a section binary holds for `value` in `space`; nullopt when none covers it.
    std::function<std::optional<uint8_t>(uint64_t value, jnext::dbg::FetchSource space)> byte_at;
};

/// A byte row, placed.
struct Z88dkListedRange {
    SourceRange range;
    uint64_t value = 0;       // the map value of its first byte
    std::string section;
};

struct Z88dkListingResult {
    bool used = false;                          // false: dropped or ignored, `notes` say why
    std::string module;
    std::vector<Z88dkListedRange> ranges;
    std::vector<SourceAlias> aliases;           // `range` indexes `ranges`
    std::map<std::string, std::vector<std::string>> texts;   // `.m4` files: lines[i] is line i + 1
    int checks = 0;                             // map line records compared
    int disagreements = 0;                      // ... that the listing places elsewhere
    std::vector<std::string> disagree_examples; // "<location> map $XXXX, listing $YYYY|none"
    std::vector<std::string> notes;
};

/// Interpret one parsed listing against the map. `name` is how notes call it
/// (and, minus ".lis", the module name when the listing names none).
/// `accepted`, when not empty, maps the module names the listing may declare to
/// the map module that named it (a source `game/util.c` is module `game_util_c`
/// in the map, but its listing says `MODULE util_c`); empty means a listing of
/// any module of the map, under its own name.
Z88dkListingResult place_z88dk_listing(const Z88dkListing& listing, const std::string& name,
                                       const std::map<std::string, std::string>& accepted,
                                       const Z88dkMapView& view);
