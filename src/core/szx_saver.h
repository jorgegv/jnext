#pragma once

#include "cpu/z80_cpu.h"      // Z80Registers (header-only POD, no jnext_cpu link needed)
#include "memory/mmu.h"       // Mmu (full definition needed for the inline build() body)

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Emulator;

/// Inverse of SzxLoader — serialises emulator state as a .szx (zx-state)
/// snapshot. Mirrors src/core/szx_loader.h/.cpp.
///
/// Format reference: "The zx-state File Format" 1.5, spectaculator.com/docs
/// /zx-state/{header,block,z80regs,specregs,rampage}.shtml. Only the three
/// block types SzxLoader::load() understands are emitted:
///   ZXSTZ80REGS ('Z80R'), ZXSTSPECREGS ('SPCR'), ZXSTRAMPAGE ('RAMP')
///
/// SCOPE — .szx is a CLASSIC-SPECTRUM interchange format, not a Next
/// snapshot format (Task 13b, reopened 2026-07-13 after a first cut wrongly
/// tried to widen the format to carry Next RAM by raising the RAM ceiling —
/// rejected by the user; see doc-history in git log for the full story).
/// libspectrum's `szx.c` (both the reader's `read_ramp_chunk()` page>63
/// rejection AND the writer's `write_ram_pages()` machine-specific page
/// sets) treats the RAM model as fixed per `chMachineId`: a classic
/// Spectrum's 8-bank-max memory map, not an arbitrary-sized blob. jnext
/// therefore saves `.szx` ONLY for the three machines that map cleanly onto
/// that model — 48K, 128K, +2A/+3 — and REFUSES for anything else
/// (Next/Pentagon/unknown), rather than truncating or lying in the header.
/// The Next-specific full-state format is Task 13a (PARKED); `.nex` already
/// exists as the Next-native program/snapshot container.
///
/// PAGE NUMBERING — chMachineId + chPageNo, verified against libspectrum
/// 1.5.0 source (szx.c `write_ram_pages()`/`write_ramp_chunk()`, the same
/// convention `read_ramp_chunk()` enforces on load):
///
///  * chMachineId: 1 = 48K, 2 = 128K, 4 = +2A, 5 = +3 (SZX_MACHINE_* enum,
///    szx.c:42-48). jnext has no distinct +2A `MachineType` — `ZX_PLUS3`
///    covers both +2A and +3 (identical memory model: 4 ROMs, port 0x1FFD
///    paging) — so jnext always emits 5 for that machine, never 4.
///  * chPageNo is NOT jnext's currently-mapped MMU *slot* number, and
///    critically is NOT simply "the first N banks". It is the classic
///    Spectrum 128 **physical RAM chip** index (0-7), the same convention
///    jnext's own `Mmu::compose_bank_()` / `rebuild_ram_slots_()` already
///    use (bank N -> physical 8K pages 2N, 2N+1) — confirmed against
///    `src/memory/mmu.cpp`: at reset, slots 2/3 (0x4000-0x7FFF) hold bank 5,
///    slots 4/5 (0x8000-0xBFFF) hold bank 2, slots 6/7 (0xC000-0xFFFF) hold
///    bank 0 (port_7ffd_=0) — i.e. jnext's own bank numbering already IS
///    the standard convention, so "bank N" == "chPageNo N" for banks that
///    are actually part of the machine's RAM.
///  * The set of banks that ARE part of the machine's RAM is what differs
///    by machine, and is NOT "banks 0..N-1":
///      - 48K: banks {5, 2, 0} ONLY (szx.c:3330-3334 — unconditionally
///        writes page 5, then, for any machine except the 16K Spectrum,
///        pages 2 and 0). These are exactly the three banks a real 48K
///        machine has wired to 0x4000/0x8000/0xC000 — there is no
///        paging, so no other bank number is meaningful. Banks 1, 3, 4,
///        6, 7 must NOT be written for a 48K save.
///      - 128K / +2A / +3: all banks {0..7} (szx.c:3337-3342, gated on
///        `LIBSPECTRUM_MACHINE_CAPABILITY_128_MEMORY`, true for all three).
///
/// `build()` reads RAM in ascending bank order for readability; order
/// within the file has no semantic meaning (each ZXSTRAMPAGE chunk is
/// self-describing via chPageNo).
///
/// REFUSAL — `build()`/`save()` return an EMPTY buffer (no partial/lying
/// file) plus a human-readable `error` string when `machine_id` doesn't map
/// to one of {1, 2, 4, 5}. Callers (GUI, headless CLI) must surface this as
/// a real failure, not silently write nothing.
class SzxSaver {
public:
    /// Classic port-driven ULA/paging state — the ZXSTSPECREGS payload.
    /// `port_fe` MUST already have the border colour folded into bits
    /// 2:0 (SzxLoader::apply() restores border by writing this byte
    /// straight to port 0xFE — it does not additionally consult
    /// `border`, which per spec is documentation-only / for readers
    /// that don't trust `chFe`'s border bits).
    struct SpecRegs {
        uint8_t border    = 0;  // ZXSTSPECREGS.chBorder (0-7)
        uint8_t port_7ffd = 0;  // ZXSTSPECREGS.ch7ffd
        uint8_t port_1ffd = 0;  // ZXSTSPECREGS.ch1ffd
        uint8_t port_fe   = 0;  // ZXSTSPECREGS.chFe (bits 2:0 = border, 3 = MIC, 4 = EAR)
    };

    /// The set of physical RAM bank numbers (== ZXSTRAMPAGE chPageNo, see
    /// class doc-comment PAGE NUMBERING) that make up a given chMachineId's
    /// RAM. Empty iff `machine_id` is not one of the four .szx supports
    /// (1=48K, 2=128K, 4=+2A, 5=+3) — jnext's `save()` only ever produces
    /// 1, 2 or 5 (no distinct +2A `MachineType`), but `build()`/this helper
    /// accept 4 too since it is a valid chMachineId per spec.
    static std::vector<uint8_t> ram_page_set(uint8_t machine_id)
    {
        switch (machine_id) {
            case 1: return {0, 2, 5};                          // 48K
            case 2: case 4: case 5:                             // 128K / +2A / +3
                return {0, 1, 2, 3, 4, 5, 6, 7};
            default: return {};                                 // unsupported
        }
    }

    /// Build a complete .szx byte buffer, or refuse (empty return, `*error`
    /// set) if `machine_id` is not one of the machines .szx can represent —
    /// see class doc-comment SCOPE. Emulator-free — RAM is read directly
    /// from `mmu` via the same temp-slot-7 pattern SzxLoader::apply() uses
    /// in reverse — so this is directly unit-testable against a bare Mmu
    /// fixture (no Emulator needed).
    static std::vector<uint8_t> build(const Z80Registers& regs, uint32_t tstates,
                                       uint8_t machine_id, const SpecRegs& spec,
                                       Mmu& mmu, std::string* error = nullptr)
    {
        std::vector<uint8_t> banks = ram_page_set(machine_id);
        if (banks.empty()) {
            if (error) {
                *error = "SzxSaver::build(): unsupported chMachineId " + std::to_string(machine_id)
                       + " — .szx can only represent 48K (1), 128K (2), +2A (4) or "
                         "+3 (5); refusing rather than truncating or lying in the header.";
            }
            return {};
        }

        std::vector<uint8_t> out;

        auto put_u16 = [&out](size_t pos, uint16_t v) {
            out[pos]     = static_cast<uint8_t>(v);
            out[pos + 1] = static_cast<uint8_t>(v >> 8);
        };
        auto put_u32 = [&out](size_t pos, uint32_t v) {
            out[pos]     = static_cast<uint8_t>(v);
            out[pos + 1] = static_cast<uint8_t>(v >> 8);
            out[pos + 2] = static_cast<uint8_t>(v >> 16);
            out[pos + 3] = static_cast<uint8_t>(v >> 24);
        };

        // ---- zx-state header (8 bytes) -----------------------------
        out.insert(out.end(), {'Z', 'X', 'S', 'T'});
        out.push_back(1);            // chMajorVersion
        out.push_back(4);            // chMinorVersion
        out.push_back(machine_id);   // chMachineId
        out.push_back(0);            // chFlags (no alternate timings)

        // ---- ZXSTZ80REGS ('Z80R'), 37-byte payload -----------------
        {
            size_t hdr = out.size();
            out.insert(out.end(), {'Z', '8', '0', 'R'});
            out.resize(out.size() + 4);           // dwSize placeholder
            size_t p = out.size();
            out.resize(p + 37, 0);

            put_u16(p + 0,  regs.AF);
            put_u16(p + 2,  regs.BC);
            put_u16(p + 4,  regs.DE);
            put_u16(p + 6,  regs.HL);
            put_u16(p + 8,  regs.AF2);
            put_u16(p + 10, regs.BC2);
            put_u16(p + 12, regs.DE2);
            put_u16(p + 14, regs.HL2);
            put_u16(p + 16, regs.IX);
            put_u16(p + 18, regs.IY);
            put_u16(p + 20, regs.SP);
            put_u16(p + 22, regs.PC);
            out[p + 24] = regs.I;
            out[p + 25] = regs.R;
            out[p + 26] = regs.IFF1 ? 1 : 0;
            out[p + 27] = regs.IFF2 ? 1 : 0;
            out[p + 28] = regs.IM;
            put_u32(p + 29, tstates);   // dwCyclesStart
            out[p + 33] = 0;            // chHoldIntReqCycles (not modelled)
            // chFlags: bit1 = ZXSTZF_HALTED per spec (offset 34, NOT 33 —
            // see the fix in szx_loader.cpp::parse_z80r() this branch
            // makes to match). Bits 0 (SUPPRESS_INTS) / 2 (FSET) unused.
            out[p + 34] = regs.halted ? 0x02 : 0x00;
            put_u16(p + 35, regs.MEMPTR);  // wMemPtr

            uint32_t sz = static_cast<uint32_t>(out.size() - p);
            put_u32(hdr + 4, sz);
        }

        // ---- ZXSTSPECREGS ('SPCR'), 8-byte payload -----------------
        {
            size_t hdr = out.size();
            out.insert(out.end(), {'S', 'P', 'C', 'R'});
            out.resize(out.size() + 4);
            size_t p = out.size();
            out.resize(p + 8, 0);
            out[p + 0] = spec.border & 0x07;
            out[p + 1] = spec.port_7ffd;
            out[p + 2] = spec.port_1ffd;
            out[p + 3] = spec.port_fe;
            // out[p+4..7] chReserved = 0 (already zero-filled)

            uint32_t sz = static_cast<uint32_t>(out.size() - p);
            put_u32(hdr + 4, sz);
        }

        // ---- ZXSTRAMPAGE ('RAMP') × banks.size(), uncompressed ------
        constexpr int      TEMP_SLOT = 7;
        constexpr uint16_t SLOT_BASE = 0xE000;
        constexpr size_t   PAGE_SIZE = 16384;
        uint8_t saved_slot7 = mmu.get_page(TEMP_SLOT);

        for (uint8_t bank : banks) {
            size_t hdr = out.size();
            out.insert(out.end(), {'R', 'A', 'M', 'P'});
            out.resize(out.size() + 4);
            size_t p = out.size();
            out.resize(p + 3 + PAGE_SIZE, 0);
            put_u16(p + 0, 0);                            // wFlags: uncompressed
            out[p + 2] = bank;                             // chPageNo

            uint16_t lo_page = static_cast<uint16_t>(bank * 2);
            uint16_t hi_page = static_cast<uint16_t>(bank * 2 + 1);
            mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(lo_page));
            for (size_t i = 0; i < 0x2000; ++i)
                out[p + 3 + i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + i));
            mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(hi_page));
            for (size_t i = 0; i < 0x2000; ++i)
                out[p + 3 + 0x2000 + i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + i));

            uint32_t sz = static_cast<uint32_t>(out.size() - p);
            put_u32(hdr + 4, sz);
        }
        mmu.set_page(TEMP_SLOT, saved_slot7);

        return out;
    }

    /// Result of save(): the bytes, or (ok=false, data empty, error set)
    /// when the emulator's current machine type cannot be represented as
    /// .szx — see class doc-comment SCOPE.
    struct SaveResult {
        std::vector<uint8_t> data;
        bool        ok = false;
        std::string error;
    };

    /// Convenience wrapper: extracts state from a running Emulator and
    /// calls build(). Implemented in szx_saver.cpp (needs core/emulator.h).
    static SaveResult save(Emulator& emu);
};
