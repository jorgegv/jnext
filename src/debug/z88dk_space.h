#pragma once

// ---------------------------------------------------------------------------
// z88dk map rules shared by every consumer of a `.map` (GH #344, design 9.4):
// which 8K page a section's address lives in, and the one tokenizer for a map
// line. std headers and debug/mem_space.h only.
// ---------------------------------------------------------------------------

#include "debug/mem_space.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// Where a z88dk section's address lives (design 9.4's rules, in order of the
/// section name).
struct Z88dkSpace {
    enum class Kind {
        Paged,       // the section names its page: `space` and `addr16` are the answer
        BindAtLoad,  // rule 4: the RAM page the MMU maps at `addr16` with the program in memory
        Unpaged,     // a DivMMC section, a PAGE section with no usable head, a number out of range
        Refused,     // a 24-bit address in a section that names no bank or page
    } kind = Kind::Unpaged;
    jnext::dbg::FetchSource space;   // Paged only
    uint16_t addr16 = 0;             // the address inside the page window, every kind but Refused
    std::string why;                 // Unpaged and Refused: the reason, for a message
};

/// Rules 1 (`PAGE_<n>`, `<KIND>_PAGE_<n>`: page n plus the 8K pages the section
/// runs on into, from the map's `__PAGE_<n>_head`, `page_head(n)`), 2 (`BANK_<n>`
/// and `<KIND>_<n>` with `_L` / `_H`, and the aligned `RODATA_<n>_align_<m>` /
/// `DATA_<n>_align_<m>`: page 2n + bit 13 of the low 16 bits), 3 (a value above
/// 0xFFFF in an unnumbered section is refused), 4 (any other section: bind at
/// load) and 5 (`DIV_<n>`, `<KIND>_DIV_<n>`: not supported yet). `<KIND>` is
/// CODE, RODATA, DATA or BSS. Page numbers are 0-223, banks 0-111, DIV 0-15; a
/// number out of range is Unpaged when the value fits 16 bits, else Refused. In a
/// numbered section the bits of `value` above 15 are not read.
Z88dkSpace z88dk_section_space(
    std::string_view section, uint32_t value,
    const std::function<std::optional<uint16_t>(int)>& page_head);

/// One map line: `NAME = $HEX ; type, scope, def, module, section, location`.
struct Z88dkMapLine {
    std::string name;
    uint64_t value = 0;       // up to 64 bits: `__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF`
    bool is_addr = false;     // type "addr"
    bool is_const = false;    // type "const"
    std::string module;
    std::string section;      // empty for a symbol in no section
    std::string location;     // the 6th comma field, whole (it can hold commas or a TAB)
};

/// nullopt for a line that is not a symbol record (no `=`, no `$HEX` value, no
/// `;`). The caller decides whether that is fatal.
std::optional<Z88dkMapLine> parse_z88dk_map_line(std::string_view line);
