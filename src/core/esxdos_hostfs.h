#pragma once

// GH #31 — the host directory served to the guest through --esxdos-stub's
// RST $08 interception.
//
// SCOPE, AND WHY IT IS PERMANENT. This serves programs that reach the
// filesystem through the esxDOS `RST $08 : DEFB hook` API: a directly loaded
// NEX, and dot commands. It does NOT serve NextZXOS's own Browser, loader or
// BASIC. That is not an omission to be fixed by widening this class — it is
// structural, and it was MEASURED, not assumed (2026-09-23, this branch):
//
//   Browser open at C:/ , descend into C:/DEMOS/ , scroll        0 calls in
//     16 entries listed on screen from the real FAT32 directory    $85..$B1
//   `.ls` from the NextZXOS command line, same card              76 calls in
//     F_OPENDIR x2, F_READDIR x35, F_GETCWD x2, F_READ x6, ...      $85..$B1
//
// Same emulator, same image, same trace channel (`--log-level esxdos=trace`).
// The `.ls` run is the positive control: the instrument plainly CAN see
// directory traffic — F_OPENDIR/F_READDIR is exactly what `.ls` does — and the
// Browser, doing the same job, emits none of it. NextZXOS carries its own
// SD/SPI block driver and FAT code in enNextZX.rom and never asks $0008.
// See doc/design/TASK89-ESXDOS-HOST-FILESYSTEM.md §0.2.
//
// Reaching the Browser would mean synthesising a FAT32 volume at the block
// layer, which the project declined (that doc's §0.1 decision 5).

#ifndef JNEXT_CORE_ESXDOS_HOSTFS_H
#define JNEXT_CORE_ESXDOS_HOSTFS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

/// A rooted, sandboxed view of one host directory, exposed through the esxDOS
/// file and directory calls.
///
/// Every register convention below is transcribed from an oracle, never from
/// memory: the hook codes and mode/attribute/error constants from
/// tbblue `src/asm/dot_commands/esxapi.def`, the per-call entry/exit registers
/// from z88dk's `libsrc/_DEVELOPMENT/arch/zxn/esxdos/z80/asm_esx_f_*.asm`
/// header blocks (verbatim excerpts of the NextZXOS/esxDOS API document), and
/// the F_READDIR entry layout additionally cross-checked against the reference
/// dot command `tbblue src/asm/readdir/readdir.asm`, which parses one.
class EsxdosHostFs {
public:
    // esxDOS error codes (esxapi.def:160-201). Only the ones this class can
    // actually return are listed; adding one means it has a caller.
    enum Error : uint8_t {
        kOk           = 0,
        kEnonsense    = 2,   // esx_enonsense
        kEnoent       = 5,   // esx_enoent
        kEio          = 6,   // esx_eio
        kEinval       = 7,   // esx_einval
        kEacces       = 8,   // esx_eacces
        kEnfile       = 12,  // esx_enfile   — out of handles
        kEbadf        = 13,  // esx_ebadf
        kEisdir       = 16,  // esx_eisdir
        kEnotdir      = 17,  // esx_enotdir
        kEexist       = 18,  // esx_eexist
        kEpath        = 19,  // esx_epath
        kEnosys       = 20,  // esx_enosys   — mode this class does not serve
        kErdonly      = 24,  // esx_erdonly  — read-only mount
    };

    // F_OPEN access modes (esxapi.def:78-84).
    static constexpr uint8_t kModeRead        = 0x01;
    static constexpr uint8_t kModeWrite       = 0x02;
    static constexpr uint8_t kModeCreatNoExist= 0x04;
    static constexpr uint8_t kModeOpenCreat   = 0x08;
    static constexpr uint8_t kModeCreatTrunc  = 0x0C;
    static constexpr uint8_t kModeCreatMask   = 0x0C;
    static constexpr uint8_t kModeUseHeader   = 0x40;  // +3DOS header: refused

    // F_OPENDIR modes (esxapi.def:87-96).
    static constexpr uint8_t kDirShortOnly    = 0x00;
    static constexpr uint8_t kDirLfnOnly      = 0x10;
    static constexpr uint8_t kDirLfnAndShort  = 0x18;
    static constexpr uint8_t kDirNameMask     = 0x18;
    static constexpr uint8_t kDirUseWildcards = 0x20;  // refused
    static constexpr uint8_t kDirSfEnable     = 0x80;  // sort/filter: refused

    // FAT attribute bits (esxapi.def:117-123).
    static constexpr uint8_t kAttrReadOnly  = 0x01;
    static constexpr uint8_t kAttrHidden    = 0x02;
    static constexpr uint8_t kAttrDirectory = 0x10;
    static constexpr uint8_t kAttrArchive   = 0x20;

    // F_SEEK whence (esxapi.def:140-147).
    static constexpr uint8_t kSeekSet = 0;
    static constexpr uint8_t kSeekFwd = 1;
    static constexpr uint8_t kSeekBwd = 2;

    // Handle allocation. DISJOINT by construction from the two handle users
    // that already exist: the legacy in-memory stub file (1, emulator.h) and
    // ExtendedNexHost (2 and 3, extended_nex_host.h). Directory handles carry
    // bit 7, mirroring what real NextZXOS was measured to hand out in the
    // `.ls` trace above ($81, $82) — programs must not assume it, but matching
    // it costs nothing and keeps the two spaces obviously distinct.
    static constexpr uint8_t kFirstFileHandle = 0x04;
    static constexpr uint8_t kFileHandles     = 8;     // $04..$0B
    static constexpr uint8_t kFirstDirHandle  = 0x84;
    static constexpr uint8_t kDirHandles      = 4;     // $84..$87

    /// Longest guest path or filename accepted, in bytes. The guest buffer for
    /// F_GETCWD is not sized in the API, so a long answer is a buffer overrun
    /// in the GUEST; cap what we can ever produce.
    static constexpr std::size_t kMaxPath = 255;

    /// One 11-byte esx_stat, as F_STAT/F_FSTAT return it. Layout from
    /// asm_esx_f_fstat.asm: '*' / $81 / attr / time(2) / date(2) / size(4).
    struct StatInfo {
        uint8_t  attr = 0;
        uint16_t time = 0;   // MS-DOS packed time
        uint16_t date = 0;   // MS-DOS packed date
        uint32_t size = 0;
    };

    /// One F_READDIR entry, before it is marshalled into guest memory.
    struct DirEntry {
        uint8_t     attr = 0;
        std::string lfn;     // long name
        std::string sfn;     // synthesised 8.3 name
        uint16_t    time = 0;
        uint16_t    date = 0;
        uint32_t    size = 0;
    };

    /// Per-open-handle state as it travels in a rewind snapshot. An open
    /// `std::ifstream` is not snapshottable, so only the reopenable triple
    /// (path, offset, mode) is stored and the stream is re-established on
    /// restore. See restore().
    struct HandleSnapshot {
        uint8_t     handle = 0;
        bool        is_dir = false;
        std::string path;        // host path, absolute
        uint64_t    position = 0;  // file offset, or dir entry index
        uint8_t     mode = 0;
    };

    /// Point the sandbox at `root`. Returns false and fills `error` when the
    /// directory does not exist or cannot be canonicalised. Calling it again
    /// closes every handle.
    bool configure(const std::string& root, bool writable, std::string& error);

    bool active() const { return active_; }
    bool writable() const { return writable_; }
    const std::filesystem::path& root() const { return root_; }

    /// True when `handle` is inside either handle range — open or not. Used by
    /// the dispatcher to decide whether a call is ours at all.
    static bool owns_handle(uint8_t handle) {
        return (handle >= kFirstFileHandle &&
                handle < kFirstFileHandle + kFileHandles) ||
               (handle >= kFirstDirHandle &&
                handle < kFirstDirHandle + kDirHandles);
    }
    static bool is_dir_handle(uint8_t handle) {
        return handle >= kFirstDirHandle &&
               handle < kFirstDirHandle + kDirHandles;
    }

    // ── File calls ────────────────────────────────────────────────────────
    uint8_t open(const std::string& guest_path, uint8_t mode, uint8_t& handle);
    uint8_t close(uint8_t handle);
    uint8_t read(uint8_t handle, std::size_t want, std::vector<uint8_t>& out);
    uint8_t write(uint8_t handle, const uint8_t* data, std::size_t count,
                  std::size_t& written);
    uint8_t seek(uint8_t handle, uint8_t whence, uint32_t distance,
                 uint32_t& position);
    uint8_t fgetpos(uint8_t handle, uint32_t& position) const;
    uint8_t fstat(uint8_t handle, StatInfo& out) const;
    uint8_t stat(const std::string& guest_path, StatInfo& out);
    uint8_t sync(uint8_t handle);

    // ── Directory calls ───────────────────────────────────────────────────
    uint8_t opendir(const std::string& guest_path, uint8_t mode,
                    uint8_t& handle);
    /// `have` is false at end of directory (F_READDIR's A=0, Fc=0 answer).
    uint8_t readdir(uint8_t handle, DirEntry& out, bool& have);
    uint8_t rewinddir(uint8_t handle);
    uint8_t telldir(uint8_t handle, uint32_t& position) const;
    uint8_t seekdir(uint8_t handle, uint32_t position);
    /// The name mode F_OPENDIR was given, so the caller can marshal the right
    /// name(s). Returns kDirNameMask-masked bits.
    uint8_t dir_name_mode(uint8_t handle) const;
    uint8_t getcwd(std::string& out) const;
    uint8_t chdir(const std::string& guest_path);

    /// F_GETFREE returns BCDE = 512-byte blocks free (asm_esx_f_getfree.asm).
    /// A host filesystem answers in terabytes, which overflows a guest that
    /// scales it; clamp to what the largest FAT32 volume could hold.
    uint32_t free_blocks() const;

    // ── Rewind / save-state ───────────────────────────────────────────────
    std::vector<HandleSnapshot> snapshot() const;
    void restore(const std::vector<HandleSnapshot>& handles);
    /// The guest CWD, as the snapshot carries it (a '/'-joined relative path).
    std::string cwd_for_snapshot() const;
    void restore_cwd(const std::string& cwd);

    /// Build the 8.3 short name for `name`, uppercased and character-filtered.
    /// `ordinal` >= 1 forces the `~N` tail. Exposed for the unit suite.
    static std::string short_name(const std::string& name, unsigned ordinal);

    /// Pack a host modification time into MS-DOS date/time words.
    static void dos_timestamp(std::filesystem::file_time_type mtime,
                              uint16_t& date, uint16_t& time);

private:
    struct FileHandle {
        bool          open = false;
        bool          writable = false;
        std::string   path;
        std::fstream  stream;
        uint64_t      position = 0;
    };

    struct DirHandle {
        bool                  open = false;
        std::string           path;       // host path of the directory
        uint8_t               mode = 0;
        std::size_t           index = 0;
        std::vector<DirEntry> entries;
    };

    /// Resolve a guest path to a host path inside the root.
    ///
    /// Containment is decided LEXICALLY first — the component walk pops `..`
    /// and refuses to pop past the root, so a path that would escape is never
    /// even formed. Each existing component is then checked with
    /// symlink_status() and refused if it is a symlink, and the final result
    /// is canonicalised and re-checked against the canonical root. See the
    /// comment block in the .cpp for what that does and does not close.
    ///
    /// On success `out` is the host path (which need not exist — F_OPEN with a
    /// create mode needs a resolvable name for a file that is not there yet).
    uint8_t resolve(const std::string& guest_path, std::filesystem::path& out,
                    std::vector<std::string>* components = nullptr) const;

    /// Re-check containment of an already-resolved path. Cheap, and run again
    /// immediately after the host open so a symlink swapped in between the
    /// two checks has to win a race in both directions.
    bool contained(const std::filesystem::path& p) const;

    FileHandle* file_slot(uint8_t handle);
    const FileHandle* file_slot(uint8_t handle) const;
    DirHandle*  dir_slot(uint8_t handle);
    const DirHandle* dir_slot(uint8_t handle) const;

    bool active_ = false;
    bool writable_ = false;
    std::filesystem::path root_;              // canonical
    std::vector<std::string> cwd_;            // components below root_

    std::array<FileHandle, kFileHandles> files_{};
    std::array<DirHandle, kDirHandles>   dirs_{};
};

#endif  // JNEXT_CORE_ESXDOS_HOSTFS_H
