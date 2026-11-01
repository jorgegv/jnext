#pragma once

// ---------------------------------------------------------------------------
// z88dk map rules shared by every consumer of a `.map` (GH #344, design 9.4):
// which 8K page a section's address lives in, and the one tokenizer for a map
// line. std headers and debug/mem_space.h only.
// ---------------------------------------------------------------------------

#include "debug/mem_space.h"

#include <cstdint>
#include <functional>
#include <map>
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

/// A z88dk dot command (GH #351, design 9.4 rule 5): a map whose `startupoffset` is
/// $100 (dot), $200 (dotx) or $300 (dotn), with `CODE` at $2000. NextZXOS copies
/// the file into a DivMMC RAM bank and runs it from $2000 there; the map names
/// no bank.
struct Z88dkDot {
    enum class Variant { Dot, Dotx, Dotn } variant = Variant::Dot;
};

/// The DivMMC RAM bank NextZXOS loads a dot command into and runs it from, with
/// port 0xE3 = $82 (conmem + bank 2). Measured in jnext booting NextZXOS: the
/// file is copied into bank 2, `E3 <- $82` is written and the CPU executes $2000
/// (plain dot, dotx and dotn alike, GH #351 scratch run1/run4/run3). Consistent
/// with NextZXOS_and_esxDOS_APIs.pdf pp. 20, 31-33: the dot is loaded into DivMMC
/// RAM at $2000, and NextZXOS reserves the first 6 banks for itself.
inline constexpr int kNextZxosDotBank = 2;

/// Whether the map (`consts`: its const values by name, first wins) is a dot
/// command's. Not a dot (no `startupoffset`, or another value): nullopt and an
/// empty `why`. A dot whose `__CODE_head` is not $2000: nullopt and `why` says so.
std::optional<Z88dkDot> z88dk_dot_command(const std::map<std::string, uint64_t>& consts,
                                          std::string* why);

/// Rules 1 (`PAGE_<n>`, `<KIND>_PAGE_<n>`: page n plus the 8K pages the section
/// runs on into, from the map's `__PAGE_<n>_head`, `page_head(n)`), 2 (`BANK_<n>`
/// and `<KIND>_<n>` with `_L` / `_H`, and the aligned `RODATA_<n>_align_<m>` /
/// `DATA_<n>_align_<m>`: page 2n + bit 13 of the low 16 bits), 3 (a value above
/// 0xFFFF in an unnumbered section is refused), 4 (any other section: bind at
/// load) and `DIV_<n>`, `<KIND>_DIV_<n>` (unplaced: the bank is the program's or a
/// dotn's allocator's choice). `<KIND>` is CODE, RODATA, DATA or BSS. Page numbers are 0-223, banks 0-111, DIV 0-15; a
/// number out of range is Unpaged when the value fits 16 bits, else Refused. In a
/// numbered section the bits of `value` above 15 are not read.
///
/// `dot` (null = the map is not a dot command's), checked before rule 4: in a dot
/// or dotx an unnumbered section at $2000-$3FFF is `DivMmcRam{kNextZxosDotBank}`;
/// a dotx's other unnumbered sections are rule 4 (its main part is main RAM); in a
/// dotn those, and every PAGE / DIV section, are Refused (NextZXOS allocates those
/// pages when the command runs). `DIV_<n>` outside a dotn stays Unpaged.
Z88dkSpace z88dk_section_space(
    std::string_view section, uint32_t value,
    const std::function<std::optional<uint16_t>(int)>& page_head,
    const Z88dkDot* dot = nullptr);

/// Design 9.4 rule 1: page n's head, as `z88dk_section_space()`'s `page_head`
/// wants it. `consts` holds the map's const values by name (first wins). The
/// head is the low 16 bits of `__PAGE_<n>_head`; nullopt when it is absent or
/// does not fit 24 bits (newlib puts the page in bits 16-23 for __banked;
/// GH #345).
std::optional<uint16_t> z88dk_page_head(const std::map<std::string, uint64_t>& consts, int n);

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
