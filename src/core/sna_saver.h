#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Save the current emulator state as an SNA snapshot byte vector — the
/// inverse of SnaLoader, and written to BE its exact inverse: SnaLoader is the
/// oracle for both forms, and a save its own loader cannot read back
/// identically is not a save (GH #274).
///
/// THE TWO FORMS, and which machine gets which:
///
///   * 48K (49179 bytes) — 27-byte header + the 48 KB the CPU can see
///     (banks 5, 2, 0). PC is PUSHED on the guest stack, which is why the
///     header's SP is SP-2 and why saving CLOBBERS two bytes of guest RAM:
///     that is the format, not a choice. Written for `--machine 48k`, where
///     the CPU view IS the whole machine and the form is exact.
///
///   * 128K (131103 bytes, or 147487 — see SIZE below) — the same 27-byte
///     header, then bank 5, bank 2 and the bank currently paged at 0xC000,
///     then a 4-byte extended header (PC, port 0x7FFD, TR-DOS flag), then the
///     remaining banks in ascending order. PC lives in the extended header, so
///     this form does NOT push and does NOT modify the machine. Written for
///     `--machine 128k`, and for a `--machine plus3` whose state a 128K
///     machine can describe (see PLUS3).
///
/// SIZE — 131103 is the usual 128K size: five remaining banks, because
/// {5, 2, paged} are three distinct banks. When the bank paged at 0xC000 IS
/// bank 2 or bank 5, that set has only two members and SIX banks remain, so
/// the file is 147487 bytes. SnaLoader computes the same skip set from the
/// port 0x7FFD byte it just read, so both sizes round-trip; this is not a
/// special case in either direction, it is the same rule.
///
/// PLUS3 — a +3 is saved as a 128K SNA when, and only when, a 128K machine can
/// describe it. The format has no port 0x1FFD byte, so:
///   * `0x1FFD` bit 0 (SPECIAL PAGING) replaces the whole 0x0000-0xFFFF map
///     with four RAM banks and no ROM. The file's three blocks are DEFINED as
///     banks 5, 2 and the paged bank, which is not that layout at all, so such
///     a machine is REFUSED — it cannot be written, not merely written badly.
///   * `0x1FFD` bit 2 (ROM HIGH) is the top bit of the +3's 4-ROM selection
///     (`rebuild_rom_slots()`). The format carries only 0x7FFD bit 4, so a +3
///     paging ROM 2 or ROM 3 — the DOS and 48K-BASIC ROMs, the common case for
///     a program that calls into them — would come back on ROM 0 or 1. That is
///     a wrong machine rather than a smaller one, so it is REFUSED too.
///   * bit 3 is the +3 DISK MOTOR (`port_1ffd_mtr_n <= not cpu_do(3)`,
///     zxnext.vhd:3757 — and there is no printer-strobe signal in the core at
///     all), and bit 1 is one of the two special-paging configuration bits
///     (`port_1ffd_reg(2 downto 1)`, zxnext.vhd:4623-4625, jnext
///     `Mmu::map_plus3_bank()`), which only selects anything while bit 0 is
///     set — and bit 0 already refuses. Neither is part of the refusal:
///     neither changes what the form's three fixed blocks mean, and NO SNA of
///     ANY machine carries peripheral state such as a motor line. Losing that
///     is the format's scope, not a misrepresentation of memory.
/// `.szx` carries ch1ffd and can hold what is refused here, so the refusal
/// message says so.
///
/// THE WINDOW — REFUSED for any machine whose 0x4000-0xFFFF mapping is not the
/// one the form describes. Both forms describe that window by POSITION, and
/// SnaLoader::apply() puts the three blocks back at banks 5, 2 and (48K) bank 0
/// / (128K) the bank `port_7ffd` names — the only paging state in the file.
/// EXTENDED PAGING breaks that: `port_7ffd_bank` composes bits 6:3 of the bank
/// at 0xC000 from `port_dffd_reg` on every non-Pentagon machine
/// (zxnext.vhd:3763-3766, `Mmu::compose_bank_()`), and port 0xDFFD is writable
/// whenever paging is unlocked — on `--machine 48k` as much as on a 128K, since
/// neither the composition nor the port decode is gated on machine type. The
/// file then cannot describe the machine even with every byte in it correct:
/// the MAPPING comes back wrong, and a bank above 7 has no block to live in at
/// all. The Next MMU registers (NR 0x50-0x57, ungated per zxnext.vhd:4686) can
/// move 0x4000/0x8000 the same way. Both are refused, pointing at `.jns` — and
/// NOT at `.szx`, whose ZXSTSPECREGS has ch7ffd and ch1ffd and no field for
/// 0xDFFD either. See classic_window_intact() in the .cpp for the derivation.
///
/// NEXT — REFUSED outright. Neither form can carry what makes the machine a
/// Next: the other 700+ KB of RAM, the NextREG file, Layer 2, the sprites, the
/// tilemap, the Copper, the DivMMC. A 48K SNA of a Next used to be written
/// anyway, with exit status 0 and a "saved 48K snapshot" log line, so a user
/// who asked for a snapshot of a Next got a file that quietly was not one
/// (GH #274). `.jns` is the only format that can represent a Next, and the
/// refusal says that too.
class SnaSaver {
public:
    /// Save the current emulator state as an SNA byte vector — the user-facing
    /// route, used by `--delayed-snapshot` and File ▸ Save Snapshot. The form
    /// follows the machine (see the class doc-comment). Returns an EMPTY vector
    /// when the machine cannot be represented, or on failure; the reason is
    /// logged, and copied to `*error` when one is given, so a GUI can show it.
    /// Callers must surface that as a real failure, never write a partial file.
    static std::vector<uint8_t> save(Emulator& emu, std::string* error = nullptr);

    /// The 48K form with NO machine check — the CPU view, whatever machine is
    /// running. It exists for ONE caller, Emulator::start_rzx_recording(), and
    /// that embed is legitimate where a `.sna` FILE would not be: an RZX
    /// records its machine type separately in its own creator block, so
    /// playback rebuilds the Next and this snapshot only has to restore the
    /// 64 KB the CPU could see. NOT a user-facing route — nothing on the
    /// command line or in the GUI reaches it, so asking for a `.sna` of a Next
    /// is still refused.
    static std::vector<uint8_t> save_cpu_view_unchecked(Emulator& emu);

private:
    /// The 128K form. Reached through save() only, for the machines whose
    /// state it can describe.
    static std::vector<uint8_t> save_128k(Emulator& emu);
};
