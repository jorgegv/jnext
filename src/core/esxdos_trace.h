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
/// ORACLES — this table is transcribed from these two files and from nothing
/// else. It was once written from memory and two of the names in it did not
/// exist; do not "remember" an entry, open the file.
///
///   [1] tbblue firmware, esxDOS API definitions:
///       src/asm/dot_commands/esxapi.def  — hook codes $85..$95, $9A..$B1
///   [2] z88dk (2.3) ZX target:
///       lib/target/zx/def/esxdos.def     — TWO separate constant blocks:
///         `__ESXDOS_SYS_*` (lines 7..44)   $80..$8E, $98..$B1
///         `__ESX_*`        (lines 104..182) $85..$8F, $91..$95, $9A..$B1
///
/// Where they overlap ($85..$8F, $91..$95, $9A..$B1) all three blocks agree
/// exactly — the per-row citations below name the `__ESXDOS_SYS_*` decimals
/// because that block came first, but most of those rows are corroborated a
/// third time by `__ESX_*`.
///
/// Neither file is a superset. [1] alone carries $90 M_AUTOLOAD. [2] alone
/// carries the low-level disk calls $80..$84, $8A M_DRIVEINFO, and the mount
/// calls $98/$99. This table is their union: 48 entries. $96 and $97 are
/// assigned by none of the blocks and are deliberately absent.
///
/// Anything not listed is reported by number.
inline const char* esxdos_call_name(uint8_t code) {
    switch (code) {
    // ── Low-level disk ($80..$87) ───────────────────────────────────────
    case 0x80: return "DISK_STATUS";     // [2] __ESXDOS_SYS_DISK_STATUS = 128
    case 0x81: return "DISK_READ";       // [2] = 129
    case 0x82: return "DISK_WRITE";      // [2] = 130
    case 0x83: return "DISK_IOCTL";      // [2] = 131
    case 0x84: return "DISK_INFO";       // [2] = 132
    case 0x85: return "DISK_FILEMAP";    // [1] disk_filemap
    case 0x86: return "DISK_STRMSTART";  // [1] disk_strmstart
    case 0x87: return "DISK_STRMEND";    // [1] disk_strmend
    // ── Miscellaneous ($88..$95) ────────────────────────────────────────
    case 0x88: return "M_DOSVERSION";    // [1] m_dosversion  [2] = 136
    case 0x89: return "M_GETSETDRV";     // [1] m_getsetdrv   [2] = 137
    case 0x8A: return "M_DRIVEINFO";     // [2] = 138
    case 0x8B: return "M_TAPEIN";        // [1] m_tapein      [2] = 139
    case 0x8C: return "M_TAPEOUT";       // [1] m_tapeout     [2] = 140
    case 0x8D: return "M_GETHANDLE";     // [1] m_gethandle   [2] = 141
    case 0x8E: return "M_GETDATE";       // [1] m_getdate     [2] = 142
    case 0x8F: return "M_EXECCMD";       // [1] m_execcmd
    case 0x90: return "M_AUTOLOAD";      // [1] m_autoload
    case 0x91: return "M_SETCAPS";       // [1] m_setcaps
    case 0x92: return "M_DRVAPI";        // [1] m_drvapi
    case 0x93: return "M_GETERR";        // [1] m_geterr
    case 0x94: return "M_P3DOS";         // [1] m_p3dos — gateway to the +3DOS API
    case 0x95: return "M_ERRH";          // [1] m_errh
    // ── File / directory ($98..$B1) ─────────────────────────────────────
    case 0x98: return "F_MOUNT";         // [2] = 152
    case 0x99: return "F_UMOUNT";        // [2] = 153
    case 0x9A: return "F_OPEN";          // [1] f_open        [2] = 154
    case 0x9B: return "F_CLOSE";         // [1] f_close       [2] = 155
    case 0x9C: return "F_SYNC";          // [1] f_sync        [2] = 156
    case 0x9D: return "F_READ";          // [1] f_read        [2] = 157
    case 0x9E: return "F_WRITE";         // [1] f_write       [2] = 158
    case 0x9F: return "F_SEEK";          // [1] f_seek        [2] = 159
    case 0xA0: return "F_FGETPOS";       // [1] f_fgetpos     [2] = 160
    case 0xA1: return "F_FSTAT";         // [1] f_fstat       [2] = 161
    case 0xA2: return "F_FTRUNCATE";     // [1] f_ftruncate   [2] = 162
    case 0xA3: return "F_OPENDIR";       // [1] f_opendir     [2] = 163
    case 0xA4: return "F_READDIR";       // [1] f_readdir     [2] = 164
    case 0xA5: return "F_TELLDIR";       // [1] f_telldir     [2] = 165
    case 0xA6: return "F_SEEKDIR";       // [1] f_seekdir     [2] = 166
    case 0xA7: return "F_REWINDDIR";     // [1] f_rewinddir   [2] = 167
    case 0xA8: return "F_GETCWD";        // [1] f_getcwd      [2] = 168
    case 0xA9: return "F_CHDIR";         // [1] f_chdir       [2] = 169
    case 0xAA: return "F_MKDIR";         // [1] f_mkdir       [2] = 170
    case 0xAB: return "F_RMDIR";         // [1] f_rmdir       [2] = 171
    case 0xAC: return "F_STAT";          // [1] f_stat        [2] = 172
    case 0xAD: return "F_UNLINK";        // [1] f_unlink      [2] = 173
    case 0xAE: return "F_TRUNCATE";      // [1] f_truncate    [2] = 174
    case 0xAF: return "F_CHMOD";         // [1] f_chmod       [2] = 175
    case 0xB0: return "F_RENAME";        // [1] f_rename      [2] = 176
    case 0xB1: return "F_GETFREE";       // [1] f_getfree     [2] = 177
    default:   return nullptr;
    }
}
