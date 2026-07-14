#include "core/tap_saver.h"
#include "core/emulator.h"
#include "core/log.h"

bool TapSaver::handle_sa_bytes_trap(Emulator& emu) {
    // SA-BYTES ROM routine interface (48K ROM 0x04C2, "The Complete
    // Spectrum ROM Disassembly", Logan & O'Hara; entry bytes verified
    // against the extracted 48.rom — see SA_BYTES_ADDR comment):
    //   Entry: A  = flag byte (0x00 header / 0xFF data — the routine
    //               itself only tests bit 7 to pick the pilot length),
    //          IX = start address of the block data,
    //          DE = block length.
    //   Both ROM entries land here: the header save is CALL 0x04C2
    //   (ROM 0x098A: PUSH IX / LD DE,0x0011 / XOR A / CALL 04C2) and the
    //   data save is JP 0x04C2 (ROM 0x099E: LD E,(IX+11) / LD D,(IX+12) /
    //   LD A,0xFF / POP IX / JP 04C2). In BOTH cases the top of stack is
    //   the address SA-BYTES ultimately returns to: for the CALL it is
    //   the instruction after the CALL; for the JP it is SA-CONTRL's own
    //   caller — the exact slot the routine's final RET (via the pushed
    //   SA/LD-RET, 0x053F) consumes on real hardware. So the same
    //   pop-return-address exit the LD-BYTES trap uses is correct here.
    //   Exit: carry set (SA/LD-RET's no-BREAK path); no caller of
    //   SA-BYTES in the 48K ROM tests the other registers afterwards.

    auto regs = emu.cpu().get_registers();

    const uint8_t  flag   = static_cast<uint8_t>(regs.AF >> 8);
    const uint16_t start  = regs.IX;
    const uint16_t length = regs.DE;

    Log::emulator()->debug("TAP save trap: flag={:#04x} IX={:#06x} DE={:#06x}",
                           flag, start, length);

    // Read the block payload from CPU-visible memory (wraps at 64K like
    // the ROM's INC IX walk would).
    std::vector<uint8_t> payload(length);
    for (uint16_t i = 0; i < length; ++i)
        payload[i] = emu.mmu().read(static_cast<uint16_t>(start + i));

    const bool ok = append_block(flag, payload.data(), payload.size());
    if (ok) {
        Log::emulator()->info("TAP save: block {} appended to '{}' (flag={:#04x}, {} bytes)",
                              blocks_written_, path_, flag, length);
    } else {
        Log::emulator()->error("TAP save: failed to append block to '{}'", path_);
    }

    // Mirror the LD-BYTES trap return mechanics: advance IX past the
    // block, zero DE, set carry (success), pop the return address.
    regs.IX = static_cast<uint16_t>(start + length);
    regs.DE = 0;
    if (ok)
        regs.AF |= 0x0001;   // carry set — success
    else
        regs.AF &= ~0x0001;  // carry clear — error (host write failed)

    uint16_t ret_lo = emu.mmu().read(regs.SP);
    uint16_t ret_hi = emu.mmu().read(static_cast<uint16_t>(regs.SP + 1));
    regs.SP += 2;
    regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));

    emu.cpu().set_registers(regs);
    return ok;
}
