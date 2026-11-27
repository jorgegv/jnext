#pragma once
// The comparison and carry-over engine of the SD card upgrade, and the rescue verbs built on it
// (GH #555): walk a card's tree, say what the user changed against the card as it was made, carry
// those changes onto a new card, flatten a writes file into a plain image, export the changes to
// a folder and import such a folder.
//
// Everything reads and writes through ChaN FatFs on two volumes at once (FF_VOLUMES 2): drive 0
// and drive 1. A tree can also be read by the lenient hand-rolled FAT32 reader (an under-clustered
// distribution image, which FatFs refuses). The developer guide's "SD card set" page is the
// reference; the design is in the issue.
#include "core/sdcard_file_add.h"   // FileAddStatus: the rescue verbs share its exit statuses

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sd_write_journal { class Journal; }

namespace sdcard::carry {

/// FAT attribute bits a user can see and set. The archive bit is FatFs's own bookkeeping.
constexpr uint8_t kAttrRdo = 0x01, kAttrHid = 0x02, kAttrSys = 0x04;
constexpr uint8_t kUserAttrMask = kAttrRdo | kAttrHid | kAttrSys;

/// One file or directory of a card.
struct Entry {
    std::string path;      ///< absolute, '/'-separated, in the card's own (CP437) bytes
    bool        dir   = false;
    uint32_t    size  = 0;
    uint8_t     attr  = 0;
    uint16_t    fdate = 0, ftime = 0;
    std::string altname;   ///< the 8.3 name (empty for the lenient reader)
    /// A name FatFs cannot address (it ends in a space or a dot: FatFs drops those from a path), such as
    /// one NextZXOS can make. The entry is listed but never compared, carried or exported, and a
    /// directory of that name is not entered.
    bool        unaddressable = false;
};

/// A path with ASCII letters folded: FAT names are case-insensitive.
std::string path_key(const std::string& path);

/// A card's tree, as a list of entries (a directory before its contents, in key order) plus a
/// sequential reader of one file at a time.
class Tree {
public:
    virtual ~Tree() = default;
    const std::vector<Entry>& entries() const { return entries_; }
    /// The entry whose path_key is `key`, or null.
    const Entry* find(const std::string& key) const;
    /// path_key of each entry, in step with entries().
    const std::vector<std::string>& keys() const { return keys_; }
    virtual bool open(const Entry& e, std::string& err) = 0;
    /// Read up to `n` bytes of the open file; `got` is how many came (0 = end).
    virtual bool read(uint8_t* buf, size_t n, size_t& got, std::string& err) = 0;
    virtual void close() = 0;
protected:
    std::vector<Entry>       entries_;
    std::vector<std::string> keys_;
    void sort_entries();
};

/// An image mounted on a FatFs drive for as long as the object lives. Never write to one opened
/// `read_only`. A writable one is flushed, then unmounted, on destruction.
class Volume : public Tree {
public:
    ~Volume() override;
    /// Mount `image` on drive `drive` (0 or 1). With a `journal` the card is seen through it, and
    /// written through it unless `read_only` (then nothing is written anywhere). Walks the tree.
    static std::unique_ptr<Volume> open(int drive, const std::string& image,
                                        std::shared_ptr<sd_write_journal::Journal> journal,
                                        bool read_only, std::string& err);
    bool open(const Entry& e, std::string& err) override;
    bool read(uint8_t* buf, size_t n, size_t& got, std::string& err) override;
    void close() override;
    /// "<drive>:" + path, the FatFs spelling.
    std::string fat(const std::string& path) const;
    int drive() const { return drive_; }
    void mark_dirty() { dirty_ = true; }
    /// Free clusters and the cluster size.
    bool free_space(uint64_t& free_clusters, uint64_t& cluster_bytes, std::string& err);
    /// Flush the volume now (a writable one).
    bool sync(std::string& err);
private:
    Volume() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int  drive_ = 0;
    bool dirty_ = false;
    bool walk(std::string& err);
};

/// `image` as a Tree: through FatFs, and when FatFs refuses it (a raw distribution image is
/// under-clustered) through the lenient hand-rolled reader, which holds the file contents in memory.
/// NOTE: it always mounts on drive 1: a Volume still open on drive 1 is silently re-attached and
/// loses its mount, so open the others on drive 0 or close the drive-1 one first.
std::unique_ptr<Tree> open_tree_lenient(const std::string& image, std::string& err);

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

enum class Kind : char { Added = 'A', Changed = 'C', Deleted = 'R' };
struct Change {
    Kind  kind;
    Entry entry;   ///< X's entry (Y's for a deletion)
};

/// Is the entry `ex` of `x` the same as `ey` of `y`? The definition of "changed" (D4 of the plan):
/// a type difference, a size or content difference, or an R/H/S attribute difference. Not a
/// change: the archive bit, a date or time alone, the 8.3 name, a case-only rename, the label.
bool same_entry(Tree& x, const Entry& ex, Tree& y, const Entry& ey, bool& same, std::string& err);

/// What `x` has that `y` has not (Added), has differently (Changed), and `y` has that `x` has not (Deleted),
/// sorted by path key.
bool compute_changes(Tree& x, Tree& y, std::vector<Change>& out, std::string& err);

// ---------------------------------------------------------------------------
// Carry-over
// ---------------------------------------------------------------------------

/// Why a user change was not carried, for the report.
struct Report {
    std::vector<std::string> carried;        ///< "added /X", "changed /Y"
    std::vector<std::string> overridden;     ///< the new NextZXOS also changed it: its version won
    std::vector<std::string> deleted_back;   ///< you deleted it, the new card has it
    std::vector<std::string> removed;        ///< you changed it, the new NextZXOS removed it
    std::vector<std::string> unwritable;     ///< its name cannot be written by jnext (a trailing space or dot)
    /// The whole report as text, sorted, one entry per line, a heading per list.
    std::string text() const;
};

/// Decide, per user change, whether to carry it (section 3 step 7). `user` is the user layer's
/// changes (C against A, or C+J against C), `dist` the distribution's (N against A: only its
/// path set is read), `n` the new card as made. The entries to write into `carry` (pre-order),
/// the lists into `report`. Never carries a deletion.
void decide(const std::vector<Change>& user, const std::vector<Change>& dist, const Tree& n,
            std::vector<Entry>& carry, Report& report);

/// Changes of `x` against `y` restricted to the keys in `keys` (a sorted list of path keys): the
/// distribution side of decide(), without comparing the whole of both trees.
bool changes_at(Tree& x, Tree& y, const std::vector<std::string>& keys,
                std::vector<Change>& out, std::string& err);

struct CarryStats { unsigned files = 0, dirs = 0; uint64_t bytes = 0; };

/// Write `items` (entries of `src`) into `dst`: parent directories made as needed, file bytes,
/// the R/H/S bits and the date and time of the source. The free space is checked first, and a copy
/// that does not fit by the data clusters it needs fails with `dst` untouched (byte-identical, an
/// image or a journal); the precheck does not count directory-entry growth, so ImageFull can still
/// come mid-write, and then what this call created is removed but the image is not byte-identical.
/// `dst` is a writable Volume, on an image or through a journal. It unlinks only what this call
/// created, and a file it was replacing whose write failed (its old bytes were already truncated).
FileAddStatus apply_carry(Tree& src, const std::vector<Entry>& items, Volume& dst,
                          std::string& err, CarryStats* stats = nullptr);

/// After a carry: `result` (the new card, plus the carried files) equals `n_as_made` except at
/// exactly the carried paths, and every carried path equals its source in `src` (bytes, R/H/S).
bool verify_carry(Tree& result, Tree& n_as_made, Tree& src, const std::vector<Entry>& carried,
                  std::string& err);

/// The two halves of verify_carry, for a caller that has only two drives to give: every carried
/// path of `result` equals its source in `src`; and `result` differs from `n_as_made` at nothing
/// but the carried paths. Two distinct trees each time (one object read twice shares a cursor).
bool verify_carried_equal(Tree& result, Tree& src, const std::vector<Entry>& carried, std::string& err);
bool verify_nothing_else_moved(Tree& result, Tree& n_as_made, const std::vector<Entry>& carried, std::string& err);
/// Copy the esxDOS stand-in's kept-file record from one journal to another, verbatim.
bool copy_kept_file(const sd_write_journal::Journal& from, sd_write_journal::Journal& to);

// ---------------------------------------------------------------------------
// The card as made
// ---------------------------------------------------------------------------

/// Whether the card at the default location can be compared with the card as it was made, and if
/// not why not (the first reason). It holds when: the raw exists, its .sha256 sidecar matches it,
/// the card's .distro is the raw's, the card's .recipe is known (1..kFixedImageRecipe, none = 1),
/// and for recipe 3 the card's volume serial is the one the raw's hash gives.
/// The sha256 of the raw is skipped when <raw>.verified (digest, size, mtime of its last match) is valid.
bool pristine_base_ok(const std::string& card_image, std::string& why);
/// How many times pristine_base_ok has hashed the original (test instrument).
uint64_t pristine_hash_count();

/// Rebuild the card as made into `out_image`: a sparse copy of the card's raw, then the card's
/// recipe. `raw` and `card_image` are the default location's pair.
/// NOTE: the rebuild formats through FatFs drive 0 (patch_image_fat32), so no Volume may be open
/// on drive 0 while it runs, and any open one loses its mount.
bool rebuild_as_made(const std::string& raw, const std::string& card_image,
                     const std::string& out_image, std::string& err);

// ---------------------------------------------------------------------------
// The rescue verbs
// ---------------------------------------------------------------------------

/// A sparse copy of `src` (all-zero blocks are not written), created new.
bool copy_sparse_file(const std::string& src, const std::string& dst, std::string& err);

/// OUT = `image` with `journal` laid over it: a plain image any FAT tool opens. Refuses an
/// existing OUT, and a journal bound to another image. Statuses: 0, 3 (nothing written).
FileAddStatus flatten(const std::string& image, const sd_write_journal::Journal& journal,
                      const std::string& out, std::string& err);

/// One line of CHANGES.txt.
struct ChangeLine {
    char        kind = 'A';       ///< A added file, C changed file, D added directory, R removed
    uint8_t     attrs = 0;        ///< R/H/S bits
    uint64_t    size = 0;
    std::string card_path;        ///< decoded: the card's own bytes
    std::string host_path;        ///< relative to the export directory, as the host spells it (names
                                  ///< already %XX-escaped where needed, so not decoded); empty for D and R
};

/// The one reader and writer of CHANGES.txt (the format is in the developer guide).
std::string format_changes(const std::vector<ChangeLine>& lines, const std::vector<std::string>& comments);
bool parse_changes(const std::string& text, std::vector<ChangeLine>& lines, std::string& err);

/// %XX-escape `s`: bytes outside printable ASCII, '%', and with `host` also the characters a host
/// file system refuses, a trailing dot or space, and a reserved device name.
std::string escape_name(const std::string& s, bool host);
bool unescape_name(const std::string& s, std::string& out);

struct ExportRequest {
    std::string image;
    std::shared_ptr<sd_write_journal::Journal> journal;   ///< the card's writes file, or null
    std::string base_image;                               ///< --sdcard-export-base, or empty
    std::string dir;                                      ///< empty or absent
    bool        is_default_card = false;                  ///< `image` is the default card
};
struct ExportReport {
    unsigned added = 0, changed = 0, dirs = 0, removed = 0;
    std::string base;                                     ///< what it was compared against
    std::vector<std::string> escaped;                     ///< host names written escaped
    std::vector<std::string> skipped;                     ///< card paths not exported: a name FatFs cannot address
};
/// Copy every added or changed file of the card, as the machine sees it (card + journal), to
/// `req.dir`, plus CHANGES.txt. The base it compares against: the card as made (the default card
/// with a pristine base), else req.base_image, else the card itself when there is a journal, else
/// a refusal. Statuses: 0, 3 (failed, DIR left as it was or removed), 5 image unusable.
FileAddStatus export_changes(const ExportRequest& req, ExportReport& rep, std::string& err);

struct ImportReport {
    std::vector<std::string> written, replaced;
    unsigned dirs_created = 0;
    uint64_t bytes = 0;
};
/// Put the files of an exported `dir` onto `image` (or into its bound journal): every A and C
/// line's file, every D line's directory; R lines are never applied and CHANGES.txt is never
/// copied. Statuses are file-add's: 2 for a missing or malformed CHANGES.txt or a bad host file,
/// 3 a card path that cannot be written, 5 image unusable, 6 full.
FileAddStatus import_changes(const std::string& image, const std::string& dir,
                             ImportReport& rep, std::string& err);

}  // namespace sdcard::carry
