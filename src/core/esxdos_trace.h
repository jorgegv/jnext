#pragma once
#include <cstdint>

/// esxdos / NextZXOS syscall names for RST $08 tracing (Task 85).
///
/// The Next exposes its filesystem and OS services through `RST $08` followed
/// by a one-byte function code (the "DEFB" immediately after the RST). jnext
/// intercepts that at `Z80Cpu::on_esxdos_call` and services a small subset in
/// the `--esxdos-stub` handler; everything else falls through to whatever is
/// really at $0008.
///
/// When a program written for NextZXOS misbehaves under `--load`, the decisive
/// question is which of these it called and what it got back — especially the
/// ones jnext does not implement, which are invisible by construction. This
/// table exists so the trace prints `F_READ` rather than `$9D`.
///
/// Codes per the esxdos 0.8.7 API and the NextZXOS additions. Anything not
/// listed is reported by number.
inline const char* esxdos_call_name(uint8_t code) {
    switch (code) {
    // ── Disk / drive ────────────────────────────────────────────────────
    case 0x85: return "M_GETSETDRV";
    case 0x89: return "M_DRVAPI";
    case 0x8A: return "M_GETERR";
    case 0x8B: return "M_TAPEIN";
    case 0x8C: return "M_TAPEOUT";
    case 0x8D: return "M_GETPOS";      // NextZXOS
    case 0x8E: return "M_GETHANDLE";
    case 0x8F: return "M_EXECCMD";
    // ── System ──────────────────────────────────────────────────────────
    case 0x88: return "M_DOSVERSION";
    case 0x90: return "M_SETCAPS";
    case 0x91: return "M_GETDATE";
    case 0x92: return "M_ADDBASICEXT";
    case 0x94: return "M_P3DOS";       // gateway to the +3DOS / NextZXOS API
    case 0x95: return "M_ERRH";
    // ── File ────────────────────────────────────────────────────────────
    case 0x9A: return "F_OPEN";
    case 0x9B: return "F_CLOSE";
    case 0x9C: return "F_SYNC";
    case 0x9D: return "F_READ";
    case 0x9E: return "F_WRITE";
    case 0x9F: return "F_SEEK";
    case 0xA0: return "F_FGETPOS";
    case 0xA1: return "F_FSTAT";
    case 0xA2: return "F_FTRUNCATE";
    case 0xA3: return "F_OPENDIR";
    case 0xA4: return "F_READDIR";
    case 0xA5: return "F_TELLDIR";
    case 0xA6: return "F_SEEKDIR";
    case 0xA7: return "F_REWINDDIR";
    case 0xA8: return "F_GETCWD";
    case 0xA9: return "F_CHDIR";
    case 0xAA: return "F_MKDIR";
    case 0xAB: return "F_RMDIR";
    case 0xAC: return "F_STAT";
    case 0xAD: return "F_UNLINK";
    case 0xAE: return "F_TRUNCATE";
    case 0xAF: return "F_CHMOD";
    case 0xB0: return "F_RENAME";
    case 0xB1: return "F_GETFREE";
    default:   return nullptr;
    }
}
