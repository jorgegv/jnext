#pragma once

#include "memory/mmu.h"

#include <cstddef>
#include <cstdint>

class Emulator;

/// Fast-load ROM trap shared by the TAP and TZX loaders.
///
/// The trap sits at LD-BREAK (0x056B, the `RET NZ` of the 48K BASIC ROM's
/// LD-BYTES routine), not at the routine's entry (0x0556): a loader may do
/// the prologue itself and enter at 0x0562, and every entry converges on
/// 0x056B. FUSE traps the same spot. At that point the entry's EX AF,AF' has
/// run, so the expected flag is in A' and LOAD/VERIFY is the carry of F'.
namespace tape_trap {

constexpr uint16_t LD_BREAK_ADDR   = 0x056B;
constexpr uint16_t LD_BYTES_RET_ADDR = 0x05E2;   ///< the routine's common exit RET

/// ROM identity gate: the bytes about to execute are the 48K LD-BYTES
/// (0x056B `RET NZ; CALL 05E7`, 0x05DF `LD A,H; CP 1; RET`). Same bytes in
/// the 48K ROM wherever it appears (48K, 128K ROM 1, +3 ROM 3, SD 48.rom).
/// Reads through the CPU's view of memory, like TapSaver::sa_bytes_rom_present.
inline bool ld_bytes_rom_present(Mmu& mmu) {
    static constexpr uint8_t sig_a[4] = { 0xC0, 0xCD, 0xE7, 0x05 };
    static constexpr uint8_t sig_b[4] = { 0x7C, 0xFE, 0x01, 0xC9 };
    for (uint16_t i = 0; i < 4; ++i) {
        if (mmu.read(static_cast<uint16_t>(LD_BREAK_ADDR + i)) != sig_a[i]) return false;
        if (mmu.read(static_cast<uint16_t>(0x05DF + i)) != sig_b[i]) return false;
    }
    return true;
}

/// Do what the ROM does from LD-START (0x056C) to its return, for the tape
/// block `blk` (flag + payload + checksum, `len` bytes; len 0 = no block, the
/// ROM's timeout). The CPU stands at LD-BREAK; on return PC is the routine's
/// RET at 0x05E2, so the ROM's own RET pops whatever the caller left on the
/// stack (SA/LD-RET 0x053F for a 0x0556 entry, the caller for a 0x0562 one).
/// Checksum, VERIFY, flag-mismatch and length outcomes follow the ROM; the
/// registers it leaves are those of the ROM run against jnext's real-time
/// player (C = 0x21 at the first byte).
void rom_ld_bytes(Emulator& emu, const uint8_t* blk, size_t len);

}  // namespace tape_trap
