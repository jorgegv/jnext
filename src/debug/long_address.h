#pragma once

// ---------------------------------------------------------------------------
// The long address (GH #360, E23-22): ONE formatter that says WHICH memory an
// address is in, over the fetch identity (`MemSpace`). Every debugger view, the
// trace export and the remote protocols spell a space through here and nowhere
// else.
//
// STABLE FORMAT. The tags below are keyed on by the project file (.jdp) and by
// the ghidra and skoolkit exports (design 4.6, 10.5, 11.2), and `parse_space_tag`
// is their inverse. Changing a spelling is a format break, not a tidy-up.
// Charset: [A-Z0-9] only (no whitespace, no ':').
//
// std headers and `mem_space.h` only, like `mem_space.h` itself.
// ---------------------------------------------------------------------------

#include "debug/mem_space.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace jnext {
namespace dbg {

/// Friendly: the tag (`P0E`, `ROM1`, `DIVROM`). Physical: where the byte sits in
/// the Next's memory layout (`$060000`, `BRAM7+$0010`); on a machine that does
/// not use that layout it falls back to Friendly.
enum class AddressStyle : uint8_t { Friendly, Physical };

/// The stable name of a space: `Page{n}` P%02X, `Rom{i}` ROM%u, BOOT, MFROM,
/// MFRAM, DIVROM, `DivMmcRam{b}` DIVRAM%u (decimal), `AltRom{i}` ALTROM%u,
/// `Layer2{p}` L2P%02X, `Config{b}` CFG%02X, FLOAT, `Cpu` CPU.
std::string space_tag(MemSpace s);

/// The inverse of `space_tag`: strict digits, no leading zeros on the decimal
/// indexes, ranges as the hardware has them (a page is 0..0xDF, ROM 0..3,
/// DivMMC bank 0..15, alt ROM 0..1); case-insensitive. nullopt when refused.
std::optional<MemSpace> parse_space_tag(std::string_view tag);

/// Where `addr` (a CPU address inside the space's window) sits in the Next's
/// memory: `$` + 6 hex digits of the SRAM address, or `BRAM5+$XXXX`,
/// `BRAM7+$XXXX`, `BOOT+$XXXX`, `FLOAT`. "" for `Cpu`.
std::string physical_text(MemSpace s, uint16_t addr);

/// What a view prints for the space: "" for `Cpu`; otherwise the tag, or in the
/// Physical style on a Next-layout machine (`next_layout`) the physical text.
std::string space_text(MemSpace s, uint16_t addr, AddressStyle st, bool next_layout);

/// `$C000 P0E`: the address and `space_text`; just `$C000` for `Cpu`.
std::string long_address(uint16_t addr, MemSpace s, AddressStyle st, bool next_layout);

}  // namespace dbg
}  // namespace jnext
