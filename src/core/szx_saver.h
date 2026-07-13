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
/// so every byte this saver writes is structurally round-trippable through
/// our own loader. There is no standard zx-state block for Next-only
/// state (NextREG, video layers, sprites, copper, audio, peripheral
/// devices, individual MMU-slot page assignments beyond what ports
/// 0x7FFD/0x1FFD imply) — a real SZX reader loading a file this saver
/// produced will see a classic-Spectrum-equivalent snapshot only: CPU
/// registers (both sets), IM/IFF1/IFF2, tstates-in-frame, border,
/// classic 128K/+3 paging state, and — this is the part worth reading
/// carefully — only the FIRST 1024 KB of RAM (see RAM CEILING below). A
/// Next's full 1792/2048 KB RAM never round-trips through .szx: the
/// wire format itself cannot carry it.
///
/// RAM CEILING — two separate limits; the tighter one wins:
///
///  1. FORMAT CEILING (this is the one that binds). ZXSTRAMPAGE's
///     `chPageNo` is checked by real-world readers, not just accepted as
///     an opaque index. Confirmed directly against libspectrum 1.5.0
///     source (the library FUSE / ZEsarUX's importer / most other tools
///     use to read .szx): `szx.c` `read_ramp_chunk()` hard-rejects
///     `page > 63` for *every* machine ID —
///     `LIBSPECTRUM_ERROR_CORRUPT: "unknown page number %lu"`. That is a
///     64-page (1024 KB) ceiling, independent of jnext's own hardware
///     model or RAM size. A file with pages 0-111 (jnext's *own* MMU-
///     addressability figure — see point 2) loads fine through jnext's
///     own `SzxLoader` (which has no such check) but is REJECTED outright
///     by real FUSE: `fuse: error: libspectrum: szx.c:read_ramp_chunk:
///     unknown page number 64`.
///  2. jnext's own MMU-addressability ceiling (112 banks / 1792 KB — see
///     `NexSaver`'s doc-comment for the VHDL citation) is LOOSER than
///     the format ceiling above, so it never actually binds here.
///
/// `build()` therefore clamps `ram_bank_count` to 64 (1024 KB), not 112:
/// the format's real-world ceiling, not jnext's hardware-addressability
/// ceiling — they are different numbers, and only the smaller one is
/// safe to ship. This was found the hard way: the first cut of this
/// saver clamped to 112 and passed every jnext-internal test, because
/// `SzxLoader::parse_ramp()` has no upper-bound check either — saver and
/// loader shared the same blind spot, so the (self-consistent, wrong)
/// test suite was green while every file this saver produced was
/// silently rejected by real FUSE. Fixed after an independent review
/// caught it; re-verified against the actual libspectrum source, not
/// just the review's word for it.
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

    /// Build a complete .szx byte buffer. Emulator-free — RAM is read
    /// directly from `mmu` via the same temp-slot-7 pattern
    /// SzxLoader::apply() uses in reverse — so this is directly
    /// unit-testable against a bare Mmu fixture (no Emulator needed).
    ///
    /// `ram_bank_count` 16K banks (bank N = physical 8K pages N*2, N*2+1)
    /// are each written as one uncompressed ZXSTRAMPAGE chunk, chPageNo
    /// = the bank index — clamped to 64 (1024 KB; see class doc-comment
    /// RAM CEILING paragraph — this is libspectrum's real-world
    /// `page > 63` rejection, NOT jnext's own 112-bank MMU-addressability
    /// figure, which is looser and irrelevant here). Uncompressed by
    /// choice (zlib compression is optional per spec;
    /// SzxLoader::parse_ramp() already supports both paths, so leaving
    /// compression out keeps this function dependency-free — no ZLIB
    /// link requirement at the Mmu test tier).
    static std::vector<uint8_t> build(const Z80Registers& regs, uint32_t tstates,
                                       uint8_t machine_id, const SpecRegs& spec,
                                       Mmu& mmu, unsigned ram_bank_count)
    {
        // See class doc-comment RAM CEILING point 1: real-world zx-state
        // readers (libspectrum szx.c:read_ramp_chunk) reject page > 63.
        // This is NOT the same number as jnext's own 112-bank MMU-
        // addressability ceiling (point 2) — that one is looser and
        // never binds here.
        constexpr unsigned SZX_MAX_RAM_BANKS = 64;  // libspectrum: page > 63 rejected
        if (ram_bank_count > SZX_MAX_RAM_BANKS) ram_bank_count = SZX_MAX_RAM_BANKS;

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

        // ---- ZXSTRAMPAGE ('RAMP') × ram_bank_count, uncompressed ----
        constexpr int      TEMP_SLOT = 7;
        constexpr uint16_t SLOT_BASE = 0xE000;
        constexpr size_t   PAGE_SIZE = 16384;
        uint8_t saved_slot7 = mmu.get_page(TEMP_SLOT);

        for (unsigned bank = 0; bank < ram_bank_count; ++bank) {
            size_t hdr = out.size();
            out.insert(out.end(), {'R', 'A', 'M', 'P'});
            out.resize(out.size() + 4);
            size_t p = out.size();
            out.resize(p + 3 + PAGE_SIZE, 0);
            put_u16(p + 0, 0);                            // wFlags: uncompressed
            out[p + 2] = static_cast<uint8_t>(bank);       // chPageNo

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

    /// Result of save(): the bytes, plus whether installed RAM exceeded
    /// the 64-bank (1024 KB) format ceiling (see class doc-comment RAM
    /// CEILING) — callers (GUI, headless CLI) surface this to the user
    /// rather than silently dropping RAM.
    struct SaveResult {
        std::vector<uint8_t> data;
        bool     truncated       = false;  // installed RAM > 64 banks
        unsigned banks_written   = 0;
        unsigned banks_installed = 0;
    };

    /// Convenience wrapper: extracts state from a running Emulator and
    /// calls build(). Implemented in szx_saver.cpp (needs core/emulator.h).
    static SaveResult save(Emulator& emu);
};
