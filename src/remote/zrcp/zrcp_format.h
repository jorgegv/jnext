#pragma once

// ---------------------------------------------------------------------------
// ZRCP formatters — the byte layouts of zrcp-frontend.md §1.4 / §1.7 / §2.3.
//
// PURE FUNCTIONS OF VALUES: no `Debugger`, no socket, no session. Each one
// takes what the backend already handed the adapter (a `Z80Registers`, the
// eight `SlotInfo`s, a byte range) and returns the exact text ZEsarUX 12.0
// prints for it, so a unit row can pin the bytes without a machine.
//
// THE ORACLE is the [T] transcripts of zrcp-frontend.md (ZEsarUX 12.0 driven
// by a socket client that recorded every byte) and, for what a transcript did
// not probe, ZEsarUX's own `remote.c` / `debug.c`. What the client parses —
// and therefore what must not move by a column — is DeZog 3.7.4's
// `decodezesaruxdata.ts` (fixed offsets after each label).
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "debug/inspect.h"

namespace jnext {
namespace remote {
namespace zrcp {

/// `F=` / `F'=`: `SZ5H3PNC`, a `-` for each clear bit (§1.4).
std::string flags_string(std::uint8_t f);

/// One slot's `MMU=` value (§2.3.1). A RAM slot is its 8 K page; a ROM slot is
/// `0x8000 | ((rom_bank & 1) << 1) | (slot & 1)` with `rom_bank =
/// effective_page >> 1`, so ROM 3 (48K BASIC) reads as DeZog's ROM1 halves
/// (`8002`/`8003`) and ROM 0 as ROM0 (`8000`/`8001`). Never the NR 0x50 `0xFF`
/// sentinel and never ZEsarUX 12.0's own `0000` for a ROM slot.
std::uint16_t mmu_value(const jnext::dbg::SlotInfo& slot, int index);

/// The `MMU=` field on a NON-Next machine (48K, 128K, +3): ZEsarUX's legacy
/// projection (`mem128.c`, `debug_paginas_memoria_mapeadas[0..3]`) — four 16 K
/// segments, a ROM segment `0x8000 + image` (`DEBUG_PAGINA_MAP_ES_ROM + rom`), a
/// RAM segment its 16 K bank — then four `0000`s for the slots ZEsarUX leaves
/// unset there. DeZog's 128K decoder reads exactly the first four, as 16 K slots
/// (`decodezesaruxdata.ts`: `>= 0x8000` -> ROM `8 + (v & 1)`, else the bank). A
/// segment's value is read from its first 8 K slot.
std::string mmu_field_legacy(const std::array<jnext::dbg::SlotInfo, 8>& slots);

/// The `i`-th of the eight `MMU=` values `register_line` prints — ZEsarUX's
/// `debug_paginas_memoria_mapeadas[i]` — on this machine: `mmu_value` on the
/// Next, the legacy segment for `i` < 4 elsewhere and 0 above. ONE source for
/// the register line and for the `SEGn` / `ROM` / `RAM` breakpoint variables
/// (`zrcp_condition.h`), which read the same array in ZEsarUX.
std::uint16_t mapped_page(const std::array<jnext::dbg::SlotInfo, 8>& slots, int i,
                          MachineType type);

/// `get-registers` / `set-register`: the register line, byte for byte as §1.4:
/// lower-case hex, `HL` before `DE`, two spaces before `F=`, `IM%d`, `IFF%c%c`,
/// `VPS: 0`, eight `MMU=` values with no separator — the Next's 8 x 8 K slots
/// (`mmu_value`) on the Next, `mmu_field_legacy` on every other machine. No
/// trailing newline.
std::string register_line(const Z80Registers& r,
                          const std::array<jnext::dbg::SlotInfo, 8>& slots, MachineType type);

/// `cpu-history get i` (ZEsarUX `cpu_history_legacy_regs_bin_to_string`): one
/// entry, byte for byte — lower-case hex, `HL` before `DE`, `IM%d IFF%c%c`,
/// `(PC)=` the four opcode bytes in fetch order, `(SP)=` the word at SP, the
/// eight `mmu` values, and the trailing space ZEsarUX leaves where its
/// memory-access list would be (jnext records none). No newline.
std::string history_line(const ::TraceEntry& e, const std::array<std::uint16_t, 8>& mmu);

/// The quoted argument split ZEsarUX's file commands use
/// (`util_parse_commands_argvc_comillas`): spaces separate arguments except
/// inside double quotes, and an argument that contained a quote loses its
/// first and last character (`"my file.nex"` -> `my file.nex`).
std::vector<std::string> split_quoted_args(const std::string& text);

/// A disassembly mnemonic in ZEsarUX's spelling: jnext's `$XXXX` operands lose
/// their `$` (`JR $0136` -> `JR 0136`), and nothing else changes.
std::string strip_dollar(const std::string& mnemonic);

/// One `disassemble` line: `  %04X %s` — the mnemonic starts at column 7,
/// which is where DeZog reads it (`disasm.substring(7, 7 + 4)`).
std::string disasm_line(std::uint16_t addr, const std::string& mnemonic);

/// `hexdump addr len`: 16 bytes per line, `  %04XH ` + `XX ` each + padding of
/// three spaces per missing byte + ` |ascii|` + `\n`. `bytes` holds `n` bytes
/// read from `addr` on (the caller wraps the address space).
std::string hexdump(std::uint16_t addr, const std::uint8_t* bytes, std::size_t n);

/// CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) — what ZEsarUX's
/// `util_crc32_calculation` computes for `get-crc32`.
std::uint32_t crc32_ieee(const std::uint8_t* bytes, std::size_t n);

/// `get-memory-pages`, as ZEsarUX's `debug_get_memory_pages_extended()` spells
/// each machine: on the Next `RO` for a ROM slot and `A<page>` for RAM, eight
/// 8 K segments ([T1]); on 128K / +3 four 16 K segments `RO<rom>` / `RA<bank>`
/// (`%X`); on 48K two, `ROM` and `RAM`. Each followed by one space. `verbose`:
/// one `Segment` block per segment ([T3]).
std::string memory_pages(const std::array<jnext::dbg::SlotInfo, 8>& slots, bool verbose,
                         MachineType type);

/// `get-current-machine`: the name DeZog keys its memory model on (§2.1).
std::string machine_name(MachineType type);

/// A ZEsarUX number: decimal, or hexadecimal with an `H`/`h` suffix (`38h`,
/// `0FFH`). Anything else — `0x38`, a sign, a trailing letter, an empty token,
/// a value past 32 bits — is refused, never read as 0 (§1.1: `0x38` is a
/// parse error in ZEsarUX's own grammar).
bool parse_number(const std::string& tok, std::uint32_t& out);

/// `text` split on runs of spaces/tabs.
std::vector<std::string> split_args(const std::string& text);

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
