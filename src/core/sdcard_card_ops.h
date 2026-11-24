#pragma once

#include "core/sdcard_file_add.h"

#include <cstdint>
#include <string>
#include <vector>

/// Operations on the FILES of an SD-card image that the SD card file manager needs (GH #558):
/// probe, list one directory, stat, rename, make a directory, remove (recursively). Copying a host
/// file onto the card is `add_to_image` and copying off it is `extract_from_image`; this module is
/// the rest.
///
/// EVERY CALL MOUNTS AND UNMOUNTS. Like the two engines above, each call attaches the image to FatFs
/// drive 0, works, flushes when it modified something, then unmounts and detaches. FatFs keeps global
/// state per drive and is not reentrant, so no call may hold the drive while another engine function
/// runs. A listing is therefore one mount per directory, which is cheap; `carry::Volume` (it walks
/// the whole tree on every open) is the wrong tool here.
///
/// NO ImageLock is taken here, deliberately. The manager works on images a slot of THIS process has
/// mounted, so the process already holds the card's lock; flock locks belong to the open file
/// description, so a second acquire in the same process would be refused.
///
/// READS see the card as a run with the same options would: the SD-writes journal bound to the image
/// (`--sdcard-writes`) is laid over it, and the image is opened read-only. WRITES need a card jnext
/// can write (`probe().writable`): the first FAT32-LBA partition, which is the volume the
/// firmware-faithful writers (`add_to_image`) use. With a journal bound, a write goes into the
/// journal and the image stays byte-identical, as for `--sdcard-file-add`.
///
/// Statuses are FileAddStatus's, as for every SD card verb: SourceUnreadable for a path that is not on
/// the card, DestInvalid for a name or a request that is refused (including every write on a card that
/// is not writable), DestExists for a name already taken, ImageUnusable for a card that cannot be
/// mounted or read, ImageFull for no free space or directory slots. Every refusal that is decided
/// before a write leaves the card byte-identical. A subdirectory that is not owned by its parent (its ".."
/// entry names another directory: a cross-link) or that is reached twice makes remove() refuse with
/// ImageUnusable before it removes anything.
///
/// PATHS are '/'-separated names from the root ("/" or "" is the root). Each component is the
/// `open_name` of a CardEntry, i.e. the spelling FatFs can open the entry by, matched
/// case-insensitively. It is not necessarily the displayed name: see CardEntry.
///
/// CONTRACT FOR EVERY CALLER (the manager's logic and its window): build a path from the `open_name` of
/// the entries listed, NEVER from the typed or displayed long name. A long name can address ANOTHER entry
/// on a card a guest wrote (trailing dot or space, backslash, a name an earlier entry matches first), and
/// rename and remove are permanent. An entry with an empty `open_name` is shown but is not acted on.
namespace sdcard::cardops {

/// One directory entry as stored on the card.
struct CardEntry {
    std::string name;       ///< the stored long name, CP437 bytes
    std::string altname;    ///< the 8.3 alias ("" when the name is its own alias)
    /// The spelling FatFs opens THIS entry by: `name` or else `altname`, whichever FatFs RESOLVES TO THIS ENTRY,
    /// decided by the entry's directory slot (f_stat of the candidate must return the slot the listing gave).
    /// A name does not identify an entry: FatFs takes the FIRST entry in directory order whose long name or
    /// 8.3 name matches, comparing in Unicode, and it returns the TYPED spelling for a match by long name. A
    /// long name with a character outside the code page lists under its alias yet still matches as the long
    /// name; a trailing dot or space is stripped (so the name opens a sibling) and a backslash is a path
    /// separator. FatFs never writes a directory where this matters; a guest or a damaged card can. EMPTY
    /// when neither spelling resolves here (rename and remove then refuse).
    std::string open_name;
    bool        dir   = false;
    uint32_t    size  = 0;
    uint8_t     attr  = 0;      ///< FAT attribute bits: 1 read-only, 2 hidden, 4 system
    uint16_t    fdate = 0, ftime = 0;
};

/// What a card allows.
struct CardInfo {
    bool        writable = false;
    /// Why not, when `writable` is false: one sentence for the manager to show.
    std::string read_only_reason;
    /// An SD-writes file is bound to the image and exists: reads and writes go through it.
    bool        through_writes = false;
};

/// Can the card be read, and written? Reads the volume the way the firmware finds it (FAT12/16/32,
/// partitioned or not) and mounts it read-only (an under-clustered FAT32 is ImageUnusable, with the
/// re-cluster hint). `writable` is true exactly when the first FAT32-LBA partition is that same
/// volume. Writes nothing.
FileAddStatus probe(const std::string& image, CardInfo& out, std::string& err);

/// The entries of directory `dir`, directories first, then by name (ASCII case folded). Writes nothing.
FileAddStatus list_dir(const std::string& image, const std::string& dir,
                       std::vector<CardEntry>& out, std::string& err);

/// Does `path` exist, and is it a directory? A missing path is Ok with exists=false, not an error.
/// The root exists. Writes nothing.
FileAddStatus stat(const std::string& image, const std::string& path, bool& exists, bool& is_dir,
                   std::string& err);

/// Rename the entry at `path` to `new_name`, in the same directory. `new_name` is ONE component under
/// the destination-name rules of normalize_dest_path (1..255 printable ASCII characters, none of
/// `"*:<>?\|/`, not "." or "..", no trailing dot or space); anything else is DestInvalid. A name that is
/// taken is DestExists and nothing changes: a rename never overwrites. A case-only change of the same
/// entry is allowed. The root cannot be renamed.
FileAddStatus rename(const std::string& image, const std::string& path, const std::string& new_name,
                     std::string& err);

/// Create the directory `name` inside directory `dir`. `name` follows the rename rules. An existing
/// name (a file or a directory, any case) is DestExists and nothing changes; `dir` must exist and be
/// a directory (SourceUnreadable when not on the card, DestInvalid when it is a file).
FileAddStatus make_dir(const std::string& image, const std::string& dir, const std::string& name,
                       std::string& err);

/// What remove() removed.
struct RemoveSummary {
    unsigned files = 0, dirs = 0;
};

/// Remove the file or directory at `path`, a directory with everything below it, PERMANENTLY. The tree
/// is walked first and the call refuses before removing anything (DestInvalid, naming the entry) when
/// the root is asked for, or the tree holds an entry with the read-only attribute (it is never cleared)
/// or one FatFs cannot address (empty open_name). Removal then runs depth-first; an I/O failure in the
/// middle says how many entries went.
FileAddStatus remove(const std::string& image, const std::string& path, std::string& err,
                     RemoveSummary* summary = nullptr);

/// The name to SHOW for an entry: printable ASCII as it is, every other byte and '%' as %XX.
std::string display_name(const CardEntry& e);

/// The name `extract_from_image` creates on the host for this entry.
std::string host_name_for(const CardEntry& e);

}  // namespace sdcard::cardops
