#pragma once

// ---------------------------------------------------------------------------
// The Z80 target description and the register packing that follows from it
// (GH #281, epic #276 package G, WP-2).
//
// Design: doc/design/debug-subsystem/gdb-rsp-frontend.md §3. The client,
// `z88dk-gdb`, derives its whole register model from this document (§1.2):
// it collects the `<reg>` names under a feature whose name contains `z80`,
// assumes EVERY register is 16 bits, and packs `g`/`G` in document order,
// each register two bytes LITTLE-ENDIAN. So the document and the packer below
// are one contract, and `gdb_rsp_test` checks the packer's order against the
// names parsed back out of the document.
//
// ── THE 1022-BYTE CEILING ───────────────────────────────────────────────────
//
// `z88dk-gdb` v2.4 copies every reply body into `char recv_data[1024]` with
// `strcpy` (debugger_gdb.c:853). The `qXfer` reply body is `l` + the
// document, so a document longer than 1022 bytes overflows that buffer — a
// 1094-byte one SEGFAULTS the client (design §7.1, reproduced). The
// `static_assert` below makes a larger document a build failure.
//
// ── WHAT IS DELIBERATELY ABSENT ─────────────────────────────────────────────
//
// `i`, `r`, `iff1`, `iff2`, `im`, `memptr`, `halted`: the client's `G` writes
// 0000 into every register it does not recognise (set_regs, :420, :483-487),
// so exposing them would make every `set hl …` at the z88dk prompt also clear
// I, zero R and disable interrupts. They are reachable through `monitor regs`
// / `monitor set` instead (§4.3).
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cpu/z80_cpu.h"
#include "debug/inspect.h"

namespace jnext {
namespace remote {
namespace gdb {

/// §3.1, verbatim: 600 bytes.
constexpr char TARGET_XML[] =
    "<?xml version=\"1.0\"?>\n"
    "<target version=\"1.0\">\n"
    "<architecture>z80</architecture>\n"
    "<feature name=\"org.gnu.gdb.z80.cpu\">\n"
    "<reg name=\"af\" bitsize=\"16\"/>\n"
    "<reg name=\"bc\" bitsize=\"16\"/>\n"
    "<reg name=\"de\" bitsize=\"16\"/>\n"
    "<reg name=\"hl\" bitsize=\"16\"/>\n"
    "<reg name=\"af'\" bitsize=\"16\"/>\n"
    "<reg name=\"bc'\" bitsize=\"16\"/>\n"
    "<reg name=\"de'\" bitsize=\"16\"/>\n"
    "<reg name=\"hl'\" bitsize=\"16\"/>\n"
    "<reg name=\"ix\" bitsize=\"16\"/>\n"
    "<reg name=\"iy\" bitsize=\"16\"/>\n"
    "<reg name=\"sp\" bitsize=\"16\" type=\"data_ptr\"/>\n"
    "<reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\"/>\n"
    "<reg name=\"clockl\" bitsize=\"16\"/>\n"
    "<reg name=\"clockh\" bitsize=\"16\"/>\n"
    "</feature>\n"
    "</target>\n";

/// The document's length in bytes (no terminator).
constexpr std::size_t TARGET_XML_BYTES = sizeof(TARGET_XML) - 1;

/// The most `z88dk-gdb` v2.4 survives: its 1024-byte buffer holds `l` + the
/// document + the NUL.
constexpr std::size_t TARGET_XML_MAX_BYTES = 1022;

static_assert(TARGET_XML_BYTES <= TARGET_XML_MAX_BYTES,
              "target.xml above 1022 bytes segfaults z88dk-gdb v2.4 (design §1.2, §7.1)");

/// Registers in the document, in `g` order: `p`/`P` number n is entry n.
constexpr int REG_COUNT = 14;

/// The two read-only registers: the low and high 16 bits of the monotonic
/// T-state count (CAP-INS-07 `Time::tstates_total`), which the client's
/// profiler reads as `(clockh << 16) | clockl`.
constexpr int REG_CLOCKL = 12;
constexpr int REG_CLOCKH = 13;

/// Registers 0..11, the ones `G` and `P` write.
constexpr int REG_WRITABLE = 12;

/// The names, in document order — what `gdb_rsp_test` compares against the
/// document's own `<reg name=…>` attributes.
constexpr std::array<const char*, REG_COUNT> REG_NAMES = {
    "af", "bc", "de", "hl", "af'", "bc'", "de'", "hl'",
    "ix", "iy", "sp", "pc", "clockl", "clockh",
};

/// The backend register behind each writable entry.
constexpr std::array<jnext::dbg::RegId, REG_WRITABLE> REG_IDS = {
    jnext::dbg::RegId::AF,  jnext::dbg::RegId::BC,  jnext::dbg::RegId::DE,
    jnext::dbg::RegId::HL,  jnext::dbg::RegId::AF2, jnext::dbg::RegId::BC2,
    jnext::dbg::RegId::DE2, jnext::dbg::RegId::HL2, jnext::dbg::RegId::IX,
    jnext::dbg::RegId::IY,  jnext::dbg::RegId::SP,  jnext::dbg::RegId::PC,
};

/// Register `n`'s value (0..13). `tstates` feeds the clock pair.
std::uint16_t reg_value(int n, const Z80Registers& r, std::uint64_t tstates);

/// The `g` reply: 14 × 2 bytes little-endian, lower-case hex — 56 characters.
std::string pack_registers(const Z80Registers& r, std::uint64_t tstates);

/// A `G` payload → the 14 values. False unless it is exactly 56 hex digits.
bool unpack_registers(const std::string& hex, std::array<std::uint16_t, REG_COUNT>& out);

/// One register as `p` answers it: 2 bytes little-endian, 4 hex digits.
std::string pack_register(std::uint16_t value);

/// A `P` value → the register value. False unless exactly 4 hex digits.
bool unpack_register(const std::string& hex, std::uint16_t& out);

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
