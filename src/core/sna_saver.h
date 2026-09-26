#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Save the current emulator state as an SNA snapshot byte vector.
/// This is the reverse of SnaLoader::apply() — reads registers and RAM
/// from the emulator and produces the SNA file format.
///
/// Only the 48K SNA form is written — a 27-byte header plus the 48 KB the CPU
/// can see (banks 5, 2, 0). The 128K SNA variant (a 7FFD byte plus the other
/// five banks) is NOT implemented here.
///
/// SCOPE — what that form can represent, and what save() therefore refuses
/// (GH #274). A 48K SNA describes a 48 KB Spectrum: no paging register, and no
/// hardware beyond the ULA. On a ZX Spectrum Next — jnext's DEFAULT
/// `--machine` — it can carry none of what makes the machine a Next: the other
/// 700+ KB of RAM, the NextREG file, Layer 2, the sprites, the tilemap, the
/// Copper, the DivMMC. It used to be written anyway, with a zero exit status
/// and a "saved 48K snapshot" log line, so a user who asked for a snapshot of
/// a Next got a file that quietly was not one. save() now REFUSES that
/// machine, in the same shape SzxSaver::save() already refuses what .szx
/// cannot represent, and points at `.jns` — the only format that can represent
/// a Next.
///
/// 48K, 128K and +3 are NOT refused. On a 48K the form is exact. On a 128K or
/// +3 it is the 64 KB the CPU can see at that instant: the other five banks
/// and the 7FFD/1FFD paging are not in the file. That is a KNOWN LIMITATION of
/// writing only the 48K variant, not a claim that it is lossless — it is why
/// Emulator::start_rzx_recording() embeds an `.szx` for those two machines.
/// Closing it means implementing the 128K SNA variant, which is a separate
/// piece of work; `.sna` remains what the GUI offers first there, because it is
/// the format other emulators read.
class SnaSaver {
public:
    /// Save the current emulator state as a 48K SNA byte vector — the
    /// user-facing route, used by `--delayed-snapshot` and File ▸ Save
    /// Snapshot. Returns an EMPTY vector when the current machine cannot be
    /// represented (see SCOPE) or on failure; the reason is logged, and copied
    /// to `*error` when one is given, so a GUI can show it. Callers must
    /// surface that as a real failure, never write a partial file.
    static std::vector<uint8_t> save(Emulator& emu, std::string* error = nullptr);

    /// The same 48K dump with NO machine check. For the callers whose contract
    /// is the CPU VIEW rather than the machine — today
    /// Emulator::start_rzx_recording(), which embeds this in an RZX whose
    /// creator block names the real machine separately, so playback rebuilds
    /// the Next and the snapshot only has to restore the 64 KB the CPU saw.
    /// NOT a user-facing route: nothing on the command line or in the GUI
    /// reaches it, so asking for a `.sna` FILE on a Next is still refused.
    static std::vector<uint8_t> save_cpu_view_unchecked(Emulator& emu);
};
