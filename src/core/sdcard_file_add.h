#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Copy a host file INTO the FAT32 partition of an SD-card image (GH #269).
///
/// This is the engine behind `--sdcard-file-add FILE --sdcard-file-dest PATH`,
/// a copy-and-exit CLI mode: no emulation is started, the image is mutated and
/// jnext exits with a status that says exactly what happened. When an SD-writes
/// journal is bound to the image (`--sdcard-writes`), the writes go into that
/// file instead and the image is opened read-only (fatfs_glue journal mode).
///
/// WHY THERE IS NO FAT32 WRITER HERE.
///
/// The obvious reading of "put a file on the card" is "write a host-side FAT32
/// writer": cluster allocation, both FAT copies, directory-entry creation,
/// short-name generation with `~N` tails, LFN entries, free-space accounting,
/// FSInfo. jnext already HAS all of that, and has had since Task 27 — the
/// vendored ChaN FatFs at `third_party/fatfs` is configured for writing
/// (FF_FS_READONLY 0, FF_USE_LFN 2, FF_MAX_LFN 255, FF_USE_MKFS 1), and
/// `src/core/fatfs_diskio.cpp` already backs it with a host image file,
/// applying the MBR partition offset so FatFs sees the partition as a linear
/// volume. `fatfs_format.cpp` already CREATES files through it (f_mkdir /
/// f_open / f_write) when the SD-card provisioner rebuilds an image.
///
/// So this module mounts the EXISTING filesystem instead of re-implementing
/// one. That is not merely less code: FatFs is the same library family the
/// Next's own firmware (tbblue.fw) runs, so a file written here is written the
/// way the machine that reads it would have written it. A second, hand-rolled
/// writer would be the one place in jnext where a bug silently corrupts a 1 GB
/// image the user cares about.
///
/// Consequences of that choice, all deliberate:
///   * The image must be a filesystem FatFs will mount — i.e. a spec-valid
///     FAT32 (>= 65525 clusters). The canonical CSpect image is under-clustered
///     and jnext already re-clusters it while provisioning
///     (`cspect-next-1gb-fixed.img`); an under-clustered image supplied by hand
///     is refused with a pointer at `tools/fix-sdcard-image.sh`. The emulated
///     machine's own firmware would reject it too.
///   * Long file names come out exactly as FatFs writes them: an 8.3-clean
///     uppercase name (`DRV-A.DSK`) gets a plain short entry with no LFN, and
///     anything else gets LFN entries plus a generated `~N` short name.
///   * Directory-entry timestamps are FatFs's fixed no-RTC date (FF_FS_NORTC),
///     so the same copy into the same image is byte-reproducible.
namespace sdcard {

/// Outcome of add_file_to_image().
///
/// THE NUMERIC VALUES ARE THE PROCESS EXIT CODES jnext returns for
/// `--sdcard-file-add`. This is a scriptable non-emulation mode, so each
/// failure a script might want to branch on gets its own code, and they are
/// documented in jnext(1) under EXIT STATUS. 1 is deliberately NOT used: it
/// stays jnext's generic command-line-usage error, as everywhere else.
enum class FileAddStatus {
    Ok               = 0,  ///< the file is on the card
    SourceUnreadable = 2,  ///< host file missing, unreadable, or too big
    DestInvalid      = 3,  ///< destination path malformed or unusable
    DestExists       = 4,  ///< destination exists and overwrite was not asked
    ImageUnusable    = 5,  ///< image missing, not FAT32, or not writable
    ImageFull        = 6,  ///< not enough free space or directory slots
};

/// Human-readable name of a status, for messages and tests.
const char* file_add_status_name(FileAddStatus s);

/// Validate a user-supplied destination path and turn it into the FatFs path
/// that names it (`"0:/NEXTZXOS/DRV-A.DSK"`).
///
/// Accepted: forward-slash separated, leading '/' optional, empty components
/// collapsed. Every component must be 1..255 printable-ASCII characters, must
/// not be "." or "..", must not contain any of `" * : < > ? \ |`, and must not
/// end in a '.' or a space (FAT strips those, so the name written would not be
/// the name asked for).
///
/// '\\' is rejected rather than treated as a separator: FatFs WOULD take it as
/// one, so a Windows-style path would silently create directories instead of
/// the single file the user named.
///
/// Returns true and fills `fatfs_path_out`; on failure returns false and sets
/// `err` to a sentence naming the offending component.
bool normalize_dest_path(const std::string& dest_path,
                         std::string& fatfs_path_out,
                         std::string& err);
/// The same, with `card_bytes`: the components are CP437 BYTES taken from a listing of the card (the SD card file
/// manager), so a byte above 0x7E is accepted (control bytes, the FAT-forbidden characters and the rest are still refused).
bool normalize_dest_path(const std::string& dest_path,
                         std::string& fatfs_path_out,
                         std::string& err, bool card_bytes);

/// Do `a` and `b` name the same image file?
///
/// A real question rather than a string comparison: `--sdcard ./card.img`, a
/// symlink to it, `--sdcard a/../card.img`, or a case-different spelling on a
/// case-insensitive volume all name the same image while comparing unequal as
/// strings. Used where two SD slots must hold two different images (startup and
/// a live card change).
///
/// Answered by filesystem identity (device + inode) when both paths exist,
/// which settles symlinks, hard links, relative spellings and case-folding at
/// once. When one of them does not exist — the usual case for the default
/// image on a machine that has never provisioned one — it falls back to
/// comparing normalised paths, and then to comparing the strings, so the
/// answer degrades rather than throwing. An empty path never matches anything.
bool same_image_file(const std::string& a, const std::string& b);

/// Copy `host_file` into `image_path` at `dest_path`, creating any missing
/// intermediate directories.
///
/// `overwrite` false (the default `--sdcard-file-add` behaviour) refuses an
/// existing destination with FileAddStatus::DestExists and leaves it untouched.
/// With `overwrite` true the destination is replaced.
///
/// The free-space check counts the directories still to be made as well as
/// the data. On any failure after the destination file was created, the
/// partial file is removed — a truncated `DRV-A.DSK` is worse than no
/// `DRV-A.DSK` — and so are the directories this call made for it.
///
/// `err` always receives a one-sentence explanation on failure.///
/// `dest_is_bytes` (default false, the command line's behaviour): the destination path is CP437 BYTES the caller took
/// from a listing of this card (the SD card file manager). PRECONDITION: every existing component of it is the
/// `open_name` its listing gives for that entry (the spelling FatFs resolves to THAT entry); a component that
/// FatFs resolves to a different entry than the one carrying that name is refused (DestInvalid), never written through.
/// Control bytes and DEL are refused; bytes above 0x7E are accepted. The command line leaves it false and refuses them.
FileAddStatus add_file_to_image(const std::string& image_path,
                                const std::string& host_file,
                                const std::string& dest_path,
                                bool overwrite,
                                std::string& err, bool dest_is_bytes = false);

/// The `--sdcard-file-add` / `--sdcard-file-dest` pairing rule (GH #292).
///
/// One run copies ONE source, so there is never a question of which dest
/// belongs to which add, and the two may come in either order:
///   * `--sdcard-file-dest` is optional; without it the source lands in the
///     card root (default_dest_path()).
///   * `--sdcard-file-dest` or `--sdcard-file-force` without
///     `--sdcard-file-add` copies nothing, and is an error rather than a flag
///     accepted and ignored.
///   * Either of the first two given twice is an error. A repeat used to
///     overwrite the earlier value, so `--sdcard-file-add a --sdcard-file-add
///     b` copied only `b` and reported success.
///   * An EMPTY `--sdcard-file-dest` is an error, not "not given": a script
///     whose variable came out empty would otherwise write to the root.
///
/// Counts are occurrences on the command line. Returns an empty string when
/// the combination is acceptable, otherwise the one-line usage error (exit 1).
std::string file_add_usage_error(int add_count, int dest_count,
                                 bool dest_empty, bool force);

/// The card path a source lands at when `--sdcard-file-dest` is not given
/// (GH #292): the ROOT of the card, under the source's own name. `game.nex`
/// -> `/game.nex`, `games/` -> `/games`, `.` -> the current directory's name.
///
/// The name is the one the user typed, not a symlink's target: `jnext
/// --sdcard-file-add latest.nex` with latest.nex -> v3.nex lands as
/// `/latest.nex`. It is validated exactly like a destination the user typed,
/// so a host name FAT cannot hold is refused rather than renamed.
///
/// Returns false and sets `err` when no usable name can be taken from
/// `host_path` (the host root `/`, a name FAT forbids).
bool default_dest_path(const std::string& host_path,
                       std::string& dest_out,
                       std::string& err);

/// What add_to_image() did, for the success message.
struct AddSummary {
    bool        is_dir       = false;  ///< the source was a directory
    std::string dest;                  ///< the card path it landed at
    unsigned    files        = 0;      ///< files written (new or replaced)
    unsigned    dirs_created = 0;      ///< directories that did not exist before
    uint64_t    bytes        = 0;      ///< file bytes written
};

/// Copy a host FILE or DIRECTORY onto the card — the whole of
/// `--sdcard-file-add` (GH #292).
///
/// `dest_path` empty means "not given": the source lands in the card root
/// under its own name (default_dest_path()). Otherwise it is the card path the
/// source BECOMES, exactly as for a single file: `--sdcard-file-add games
/// --sdcard-file-dest /GAMES` puts the contents of `games` in `/GAMES`. For a
/// directory, and only for a directory, `dest_path` may be `/`, which merges
/// its contents into the card root.
///
/// A regular file (or a symlink to one) goes through add_file_to_image()
/// unchanged. A directory is copied recursively, in name order, with these
/// policies:
///
///   * EXISTING DIRECTORIES ARE MERGED INTO, never replaced — the same rule
///     add_file_to_image() already applies to the directories along a path.
///   * EXISTING FILES follow the single-file rule: refused (DestExists) unless
///     `overwrite`, a read-only one refused even then (DestInvalid), and a
///     file where a directory is needed or vice versa refused (DestInvalid).
///   * SYMBOLIC LINKS ARE FOLLOWED — FAT has no links, so the guest gets what
///     the link points at, as `cp -rL` would copy it. A link back into its own
///     ancestry would never end and is refused; so is a dangling link.
///   * Anything that is neither a file nor a directory (a FIFO, a socket, a
///     device), an unreadable file or directory, a file of 4 GiB or more, a
///     name FAT cannot hold, and two names in one directory that differ only
///     in case (FAT names are case-insensitive) are refused.
///   * EMPTY DIRECTORIES are created.
///
/// ALL OR NOTHING. Every refusal above is decided before the image is opened
/// for writing or, for clashes on the card, before the first byte is written,
/// so a refused copy leaves the card byte-for-byte untouched. A failure in the
/// middle of the copy (out of space, an I/O error, a source that shrank) ROLLS
/// BACK: every file and directory this run created is removed again. Not
/// undone: a file `overwrite` already replaced (the message says how many),
/// and the cluster an existing directory grew by (FAT never shrinks one). A
/// partial copy is never reported as success. A single file has the same
/// contract (add_file_to_image()).
///
///
/// `dest_is_bytes` (default false, the command line's behaviour): the destination path is CP437 BYTES the caller took
/// from a listing of this card (the SD card file manager). PRECONDITION: every existing component of it is the
/// `open_name` its listing gives for that entry (the spelling FatFs resolves to THAT entry); a component that
/// FatFs resolves to a different entry than the one carrying that name is refused (DestInvalid), never written through.
/// Control bytes and DEL are refused; bytes above 0x7E are accepted. The command line leaves it false and refuses them.
///
/// `summary` (optional) is filled on success.
FileAddStatus add_to_image(const std::string& image_path,
                           const std::string& host_path,
                           const std::string& dest_path,
                           bool overwrite,
                           std::string& err,
                           AddSummary* summary = nullptr, bool dest_is_bytes = false);

/// The sentence a successful add prints, without its newline: "Copied 'HOST' to 'DEST' in IMAGE" (a
/// directory: "Copied directory 'HOST' to 'DEST' in IMAGE (N file(s), M new director[y|ies])"), plus
/// ", kept in WRITES (the image is not written)" when `writes_file` is not empty. ONE source for the
/// command line and for the copy a running jnext does on its behalf (GH #568).
std::string file_add_success_text(const std::string& host, const std::string& image,
                                  const std::string& writes_file, const AddSummary& summary);

/// The partition geometry FatFs needs to present `image_path`'s volume: the first FAT32-LBA
/// partition's start sector and its sector count. False, with a one-sentence `err`, for a
/// missing or truncated image, no partition, or sectors that are not 512 bytes.
bool read_partition_geometry(const std::string& image_path, uint32_t& lba,
                             uint32_t& total_sectors, std::string& err);

/// One thing `--sdcard-import-changes` puts on the card (GH #555): a host file or an (empty or not)
/// directory, at a card path, with the R/H/S attribute bits to give it.
struct ImportItem {
    std::string card_path;       ///< absolute, '/'-separated, printable ASCII
    std::string host_file;       ///< the host file (files only)
    uint64_t    size   = 0;      ///< its size, as checked by the caller
    bool        is_dir = false;
    uint8_t     attrs  = 0;      ///< FAT attribute bits: 1 read-only, 2 hidden, 4 system
};

/// What import_items_to_image() did.
struct ImportSummary {
    std::vector<std::string> written;   ///< card paths of files that did not exist
    std::vector<std::string> replaced;  ///< card paths of files that were replaced
    unsigned    dirs_created = 0;       ///< directories that did not exist before
    uint64_t    bytes        = 0;
};

/// Put `items` on the card: the write path of `--sdcard-import-changes`. It is the file-add path
/// (the same mount, journal mode, free-space check and rollback), with the two things only an
/// import does: an EXISTING file is replaced, a read-only one too (the user asked for these
/// files, so they win, the opposite of the upgrade), and every item's R/H/S bits are restored.
///
/// Every item is checked before the image is opened (the card path passes normalize_dest_path;
/// a host file is a readable regular file of the size given); a clash on the card (a file where a
/// directory is needed, or the reverse) is found before the first byte is written. A failure
/// during the write rolls back what this call created; files it already replaced stay replaced,
/// and `err` says how many (as add_to_image does). Statuses are file-add's: SourceUnreadable 2
/// for a bad source, DestInvalid 3 for a card path that cannot be written, ImageUnusable 5,
/// ImageFull 6. DestExists is never returned.
FileAddStatus import_items_to_image(const std::string& image_path,
                                    const std::vector<ImportItem>& items,
                                    std::string& err,
                                    ImportSummary* summary = nullptr);

}  // namespace sdcard
