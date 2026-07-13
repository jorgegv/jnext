#pragma once

#include "memory/mmu.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Emulator;

/// Inverse of NexLoader — serialises a subset of emulator state as a .nex
/// (V1.2) "program" container. See https://wiki.specnext.dev/NEX_file_format
/// (the same oracle nex_loader.h cites).
///
/// HONEST FORMAT LIMITATIONS (read before trusting a round trip):
///
///  * The NEX header carries only PC and SP — there is no field for AF,
///    BC, DE, HL, IX, IY, the alternate register set, I, R, IFF1/2, IM,
///    or the CPU's tstate-in-frame counter. None of those survive a
///    save→reload cycle, by construction of the format.
///  * `Emulator::load_nex()` unconditionally calls reset() before
///    NexLoader::apply(), so even the header's `preserve_regs` flag
///    (which this saver sets to 1, "preserve") only preserves whatever
///    the *reset* register state happens to be — not the state that was
///    running when the snapshot was taken. PC and SP are the only two
///    registers `apply()` ever writes explicitly, in both branches of
///    `preserve_regs`, so they are the only ones that reliably survive.
///  * Only the 16K bank currently mapped at 0xC000-0xFFFF (MMU slots
///    6 and 7) is recorded, as NEX's single `entry_bank` byte. If slots
///    6/7 do not hold a contiguous (even, even+1) bank pair — i.e. some
///    non-standard split mapping — this cannot be represented exactly;
///    the saver falls back to slot 6's bank and flags the mismatch (see
///    `BuildResult::contiguous_entry_bank`).
///  * MMU slots 0-5 are not recorded at all. NEX's loader never touches
///    them — it relies on the standard nexload.asm post-reset mapping
///    (the same convention the *real* tbblue nexload.asm uses). If the
///    original machine had a non-standard mapping in slots 0-5, that is
///    lost.
///  * The NEX bank table is a fixed 112 bytes (banks 0-111 = up to
///    1792 KB). Installations with more RAM (jnext's 2048 KB / 128-bank
///    ceiling) can only have banks 0-111 saved; the extra 256 KB (banks
///    112-127) is not representable and is silently omitted (matches the
///    112-byte `NexHeader::banks` array / `kBankOrder` in nex_loader.h).
///  * NextREG state (video layers, sprites, copper, audio, palette,
///    peripherals) is not saved — `NexLoader::apply()` always re-applies
///    a fixed nexload.asm-equivalent NextREG initialisation sequence on
///    load, regardless of what this saver wrote.
///
/// What DOES survive a full save→reload cycle through jnext's own
/// pipeline: PC, SP, border colour, and the full byte content of every
/// saved 16K RAM bank (including the code/data the program needs to
/// resume execution at PC).
class NexSaver {
public:
    struct BuildResult {
        std::vector<uint8_t> data;
        bool contiguous_entry_bank = true;  // false: slots 6/7 weren't a clean bank pair
        unsigned banks_written = 0;
        bool truncated_by_112_bank_limit = false;
    };

    /// Build a complete .nex (V1.2) byte buffer. Emulator-free — RAM and
    /// the current slot-6/7 mapping are read directly from `mmu`, so this
    /// is directly unit-testable against a bare Mmu fixture.
    static BuildResult build(uint16_t pc, uint16_t sp, uint8_t border,
                              Mmu& mmu, size_t ram_bytes)
    {
        BuildResult result;

        // Same bank-write order NexLoader::apply() reads back in (its
        // private kBankOrder — duplicated here; see nex_loader.h. Banks
        // not present are simply skipped by the loader, so writing every
        // bank 0..count-1 in this order and marking them all present in
        // the header is loader-compatible regardless of order, but we
        // keep the identical order for clarity / future minimal-subset
        // saves).
        static constexpr int kBankOrder[] = {
            5, 2, 0, 1, 3, 4, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23,
            24, 25, 26, 27, 28, 29, 30, 31,
            32, 33, 34, 35, 36, 37, 38, 39,
            40, 41, 42, 43, 44, 45, 46, 47,
            48, 49, 50, 51, 52, 53, 54, 55,
            56, 57, 58, 59, 60, 61, 62, 63,
            64, 65, 66, 67, 68, 69, 70, 71,
            72, 73, 74, 75, 76, 77, 78, 79,
            80, 81, 82, 83, 84, 85, 86, 87,
            88, 89, 90, 91, 92, 93, 94, 95,
            96, 97, 98, 99, 100, 101, 102, 103,
            104, 105, 106, 107, 108, 109, 110, 111
        };

        unsigned total_banks = static_cast<unsigned>(ram_bytes / 16384);
        unsigned bank_count  = total_banks > 112 ? 112 : total_banks;
        result.truncated_by_112_bank_limit = total_banks > 112;
        result.banks_written = bank_count;

        // ram_required: 0 = 768 KB, 1 = 1792 KB (NexLoader::ram_required_kb).
        // Since bank_count is capped at 112 (1792 KB), the value we write
        // is always representable without needing the non-spec value 2.
        uint8_t ram_required = (bank_count * 16 <= 768) ? 0 : 1;

        uint8_t page6 = mmu.get_page(6);
        uint8_t page7 = mmu.get_page(7);
        result.contiguous_entry_bank =
            (page6 % 2 == 0) && (page7 == static_cast<uint8_t>(page6 + 1));
        uint8_t entry_bank = static_cast<uint8_t>(page6 / 2);

        std::vector<uint8_t> out(512, 0);
        out[0] = 'N'; out[1] = 'e'; out[2] = 'x'; out[3] = 't';
        out[4] = 'V'; out[5] = '1'; out[6] = '.'; out[7] = '2';
        out[8]  = ram_required;
        out[9]  = static_cast<uint8_t>(bank_count);
        out[10] = 0;                       // screen_flags: no optional screen chunk
        out[11] = border & 0x07;
        out[12] = static_cast<uint8_t>(sp);
        out[13] = static_cast<uint8_t>(sp >> 8);
        out[14] = static_cast<uint8_t>(pc);
        out[15] = static_cast<uint8_t>(pc >> 8);
        out[16] = 0; out[17] = 0;          // extra_files
        for (unsigned b = 0; b < bank_count && b < 112; ++b)
            out[18 + b] = 1;               // banks[b] present
        out[130] = 0;                      // loading_bar off
        out[131] = 0;
        out[132] = 0;
        out[133] = 0;
        out[134] = 1;                      // preserve_regs (best effort; see class comment)
        out[135] = 0; out[136] = 0; out[137] = 0;  // core_version (unused by loader)
        out[138] = 0;                      // hires_colour (no screen chunk)
        out[139] = entry_bank;
        out[140] = 0; out[141] = 0;        // file_handle

        constexpr int      TEMP_SLOT = 7;
        constexpr uint16_t SLOT_BASE = 0xE000;
        uint8_t saved_slot7 = mmu.get_page(TEMP_SLOT);

        for (int bank : kBankOrder) {
            if (bank < 0 || static_cast<unsigned>(bank) >= bank_count) continue;
            size_t p = out.size();
            out.resize(p + 16384, 0);
            uint16_t lo_page = static_cast<uint16_t>(bank * 2);
            uint16_t hi_page = static_cast<uint16_t>(bank * 2 + 1);
            mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(lo_page));
            for (size_t i = 0; i < 0x2000; ++i)
                out[p + i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + i));
            mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(hi_page));
            for (size_t i = 0; i < 0x2000; ++i)
                out[p + 0x2000 + i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + i));
        }
        mmu.set_page(TEMP_SLOT, saved_slot7);

        result.data = std::move(out);
        return result;
    }

    /// Convenience wrapper: extracts state from a running Emulator and
    /// calls build(). Implemented in nex_saver.cpp (needs core/emulator.h).
    static BuildResult save(Emulator& emu);
};
