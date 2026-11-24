#include "core/sdcard_card_ops.h"

#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "peripheral/sd_write_journal.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace sdcard::cardops {

namespace {

constexpr const char* kNotWritable =
    "jnext writes only the first FAT32 partition of a card; this one is shown read-only";

std::string fr_text(FRESULT r) {
    switch (r) {
    case FR_OK: return "ok";
    case FR_DISK_ERR: return "disk error";
    case FR_INT_ERR: return "damaged FAT structure";
    case FR_NOT_READY: return "not ready";
    case FR_NO_FILE: return "no such file";
    case FR_NO_PATH: return "no such path";
    case FR_INVALID_NAME: return "invalid name";
    case FR_DENIED: return "denied";
    case FR_EXIST: return "already exists";
    case FR_WRITE_PROTECTED: return "write protected";
    case FR_NO_FILESYSTEM: return "no FAT filesystem";
    default: return "FatFs error " + std::to_string(static_cast<int>(r));
    }
}

std::string shown(const std::string& s) { return carry::escape_name(s, false); }

std::string fold(const std::string& s) {
    std::string k = s;
    for (char& c : k)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return k;
}

// The image on FatFs drive 0, mounted, for as long as the object lives. Unmount, then detach, on every
// return path. A mounted volume is flushed (f_syncvol) BEFORE the unmount, because a failed FatFs call can
// leave a change in its memory that f_mount(nullptr) would discard: f_mkdir() that fails in dir_register()
// frees the cluster it took in the FAT window and the FSInfo count only, with no sync_fs() (ff.c), and
// dropping that leaves a lost cluster on the card. The flush writes nothing on a volume nobody changed:
// sync_fs() writes the FSInfo sector only when an allocation changed it, and a read mount is write
// protected (STA_PROTECT), where f_syncvol is refused.
struct Card {
    FATFS fs{};
    bool  attached = false;
    bool  mounted  = false;
    std::shared_ptr<sd_write_journal::Journal> journal;
    ~Card() {
        if (mounted) {
            f_syncvol("0:");
            f_mount(nullptr, "0:", 0);
        }
        if (attached) fatfs_glue::detach(0);
    }
};

// Is the volume the firmware finds also the first FAT32-LBA partition, the one the writers use?
bool writable_geometry(const std::string& image, uint32_t lba, uint32_t sectors) {
    uint32_t wl = 0, ws = 0;
    std::string why;
    return read_partition_geometry(image, wl, ws, why) && wl == lba && ws == sectors;
}

FileAddStatus open_card(const std::string& image, bool write, Card& c, std::string& err) {
    uint32_t lba = 0, sectors = 0;
    std::string why;
    if (!find_sd_fat_volume(image, lba, sectors, why)) {
        err = why;
        return FileAddStatus::ImageUnusable;
    }
    if (write && !writable_geometry(image, lba, sectors)) {
        err = kNotWritable;
        return FileAddStatus::DestInvalid;
    }
    c.journal = sd_write_journal::for_image(image);
    if (c.journal) {
        // The geometry above came from the raw image. A journal that rewrote the partition table or the
        // boot sector means the card the machine sees is not that one.
        uint8_t sec[512];
        if (c.journal->lookup(0, sec) || c.journal->lookup(lba, sec)) {
            err = "the writes file '" + c.journal->file() +
                  "' holds a rewritten partition table or boot sector for '" + image +
                  "'; the card cannot be browsed or changed (--sdcard-writes-reset starts again from the image)";
            return FileAddStatus::ImageUnusable;
        }
    }
    std::string glue_err;
    if (!fatfs_glue::attach(0, image, lba, sectors, glue_err, c.journal, /*read_only=*/!write)) {
        err = "cannot open SD image '" + image + "' for " + (write ? "writing" : "reading") + " (" +
              glue_err + ")";
        return FileAddStatus::ImageUnusable;
    }
    c.attached = true;
    const FRESULT fr = f_mount(&c.fs, "0:", 1);
    if (fr != FR_OK) {
        // FatFs keeps the pointer of the FATFS it was handed even when the mount fails: unregister it.
        f_mount(nullptr, "0:", 0);
        err = "'" + image + "' does not hold a FAT filesystem this build can mount (" + fr_text(fr) +
              "). An under-clustered FAT32 image (< 65525 clusters) is rejected by the Next's own "
              "firmware too; tools/fix-sdcard-image.sh re-clusters one";
        return FileAddStatus::ImageUnusable;
    }
    c.mounted = true;
    return FileAddStatus::Ok;
}

// '/'-separated components, empty ones dropped. False (with err) for "." and "..".
bool split_path(const std::string& path, std::vector<std::string>& comps, std::string& err) {
    comps.clear();
    std::string cur;
    auto flush = [&] { if (!cur.empty()) { comps.push_back(cur); cur.clear(); } };
    for (char c : path) {
        if (c == '/') flush(); else cur.push_back(c);
    }
    flush();
    for (const std::string& c : comps) {
        if (c == "." || c == "..") {
            err = "card path '" + shown(path) + "' contains '" + c + "'; give the full path from the card root";
            return false;
        }
    }
    return true;
}

// "0:/a/b" for components {a, b}; "0:/" for none.
std::string fat_path(const std::vector<std::string>& comps, size_t n) {
    std::string p = "0:";
    if (n == 0) return "0:/";
    for (size_t i = 0; i < n; ++i) p += "/" + comps[i];
    return p;
}

// One component for a new name (rename, make_dir): the destination-name rules, no separator.
bool check_new_name(const std::string& name, std::string& err) {
    if (name.find('/') != std::string::npos) {
        err = "name '" + shown(name) + "' contains '/'; a name is one component";
        return false;
    }
    std::string fatfs_path;
    return normalize_dest_path(name, fatfs_path, err);
}

FileAddStatus status_of_open(FRESULT fr, const std::string& what, std::string& err) {
    if (fr == FR_NO_FILE || fr == FR_NO_PATH || fr == FR_INVALID_NAME) {
        err = "'" + shown(what) + "' is not on the card";
        return FileAddStatus::SourceUnreadable;
    }
    err = "cannot read '" + shown(what) + "' on the card (" + fr_text(fr) + ")";
    return FileAddStatus::ImageUnusable;
}

CardEntry to_entry(const FILINFO& f) {
    CardEntry e;
    e.name = f.fname;
    e.altname = f.altname;
    e.dir = (f.fattrib & AM_DIR) != 0;
    e.size = static_cast<uint32_t>(f.fsize);
    e.attr = static_cast<uint8_t>(f.fattrib & (AM_RDO | AM_HID | AM_SYS));
    e.fdate = f.fdate;
    e.ftime = f.ftime;
    return e;
}

// Where an entry lives in its directory: the sector and the byte offset of its 8.3 slot, as FatFs reports them
// in FILINFO (a local addition to the vendored ff.c, set by f_readdir() and f_stat() alike).
struct Slot {
    LBA_t sect = 0;
    WORD  ofs  = 0;
};

// Give each entry its open_name: the first of its long name and its 8.3 alias that FatFs RESOLVES TO THIS
// ENTRY, decided by the slot. FatFs takes the first entry, in directory order, whose long name or 8.3 name
// matches the typed one, comparing in Unicode, and it hands back the TYPED spelling for a match by long name
// and the same alias for a duplicated 8.3 name, so no name or field of the answer identifies the entry (a
// long name with a character outside the code page even lists under its alias while still matching as the
// long name). The slot does: f_stat(candidate) must return the slot the listing gave this entry. A name
// FatFs refuses (control bytes, "*:<>?|, too long), strips a trailing dot or space from, or reads a backslash
// of as a separator returns no entry or another one, so the same test refuses it. FatFs never writes a
// directory where this matters; a guest or a damaged card can. No candidate resolves here: empty.
void resolve_open_names(const std::string& fat_dir, std::vector<CardEntry>& ents, const std::vector<Slot>& slots) {
    const std::string base = fat_dir.back() == '/' ? fat_dir : fat_dir + "/";
    for (size_t i = 0; i < ents.size(); ++i) {
        for (const std::string* cand : {&ents[i].name, &ents[i].altname}) {
            if (cand->empty()) continue;
            FILINFO f{};
            if (f_stat((base + *cand).c_str(), &f) != FR_OK) continue;
            if (f.dir_sect != slots[i].sect || f.dir_ofs != slots[i].ofs) continue;
            ents[i].open_name = *cand;
            break;
        }
    }
}

// Read directory `fat` into `out` (unsorted). `shown_dir` names it in messages.
FileAddStatus read_dir(const std::string& fat, const std::string& shown_dir, std::vector<CardEntry>& out,
                       std::set<DWORD>* visited, std::string& err, DWORD* sclust = nullptr) {
    DIR dj{};
    std::vector<Slot> slots;
    FRESULT fr = f_opendir(&dj, fat.c_str());
    if (fr != FR_OK) return status_of_open(fr, shown_dir, err);
    if (sclust) *sclust = dj.obj.sclust;
    if (visited && !visited->insert(dj.obj.sclust).second) {
        f_closedir(&dj);
        err = "directory '" + shown(shown_dir) + "' on the card is reached twice (a damaged directory structure)";
        return FileAddStatus::ImageUnusable;
    }
    for (;;) {
        FILINFO fno{};
        fr = f_readdir(&dj, &fno);
        if (fr != FR_OK) {
            f_closedir(&dj);
            err = "cannot read directory '" + shown(shown_dir) + "' on the card (" + fr_text(fr) + ")";
            return FileAddStatus::ImageUnusable;
        }
        if (fno.fname[0] == 0) break;
        out.push_back(to_entry(fno));
        slots.push_back({fno.dir_sect, fno.dir_ofs});
    }
    f_closedir(&dj);
    resolve_open_names(fat, out, slots);
    return FileAddStatus::Ok;
}

// Does the subdirectory starting at `clst` say, in its ".." entry, that `parent_clst` is its parent? A
// subdirectory that two directories point at (a cross-link) fails this for one of them, which is how
// remove() avoids unlinking the contents of a directory outside the tree it was asked to remove. The root
// is 0 in a ".." entry, and its cluster number is also accepted. Two FILES sharing clusters are not
// detected (out of scope: nothing here reads a file's chain).
bool dotdot_names_parent(const FATFS& fs, DWORD clst, DWORD parent_clst) {
    BYTE sec[512];
    if (disk_read(0, sec, fs.database + static_cast<LBA_t>(clst - 2) * fs.csize, 1) != RES_OK) return false;
    if (sec[32] != '.' || sec[33] != '.') return false;
    const DWORD up = static_cast<DWORD>(sec[32 + 26] | (sec[32 + 27] << 8)) |
                     (static_cast<DWORD>(sec[32 + 20] | (sec[32 + 21] << 8)) << 16);
    if (parent_clst == 0) return up == 0 || up == fs.dirbase;
    return up == parent_clst;
}

}  // namespace

FileAddStatus probe(const std::string& image, CardInfo& out, std::string& err) {
    err.clear();
    out = CardInfo{};
    Card c;
    const FileAddStatus st = open_card(image, false, c, err);
    if (st != FileAddStatus::Ok) return st;
    uint32_t lba = 0, sectors = 0;
    std::string why;
    find_sd_fat_volume(image, lba, sectors, why);   // succeeded in open_card
    out.writable = writable_geometry(image, lba, sectors);
    if (!out.writable) out.read_only_reason = kNotWritable;
    std::error_code ec;
    out.through_writes = c.journal && std::filesystem::exists(c.journal->file(), ec);
    return FileAddStatus::Ok;
}

FileAddStatus list_dir(const std::string& image, const std::string& dir, std::vector<CardEntry>& out,
                       std::string& err) {
    err.clear();
    out.clear();
    std::vector<std::string> comps;
    if (!split_path(dir, comps, err)) return FileAddStatus::DestInvalid;
    Card c;
    FileAddStatus st = open_card(image, false, c, err);
    if (st != FileAddStatus::Ok) return st;
    st = read_dir(fat_path(comps, comps.size()), dir, out, nullptr, err);
    if (st != FileAddStatus::Ok) { out.clear(); return st; }
    std::sort(out.begin(), out.end(), [](const CardEntry& a, const CardEntry& b) {
        if (a.dir != b.dir) return a.dir;
        const std::string fa = fold(a.name), fb = fold(b.name);
        return fa != fb ? fa < fb : a.name < b.name;
    });
    return FileAddStatus::Ok;
}

FileAddStatus stat(const std::string& image, const std::string& path, bool& exists, bool& is_dir,
                   std::string& err) {
    err.clear();
    exists = false;
    is_dir = false;
    std::vector<std::string> comps;
    if (!split_path(path, comps, err)) return FileAddStatus::DestInvalid;
    Card c;
    const FileAddStatus st = open_card(image, false, c, err);
    if (st != FileAddStatus::Ok) return st;
    if (comps.empty()) { exists = true; is_dir = true; return FileAddStatus::Ok; }
    FILINFO fno{};
    const FRESULT fr = f_stat(fat_path(comps, comps.size()).c_str(), &fno);
    if (fr == FR_OK) {
        exists = true;
        is_dir = (fno.fattrib & AM_DIR) != 0;
        return FileAddStatus::Ok;
    }
    if (fr == FR_NO_FILE || fr == FR_NO_PATH || fr == FR_INVALID_NAME) return FileAddStatus::Ok;
    err = "cannot read '" + shown(path) + "' on the card (" + fr_text(fr) + ")";
    return FileAddStatus::ImageUnusable;
}

FileAddStatus rename(const std::string& image, const std::string& path, const std::string& new_name,
                     std::string& err) {
    err.clear();
    std::vector<std::string> comps;
    if (!split_path(path, comps, err)) return FileAddStatus::DestInvalid;
    if (comps.empty()) {
        err = "the card root cannot be renamed";
        return FileAddStatus::DestInvalid;
    }
    if (!check_new_name(new_name, err)) return FileAddStatus::DestInvalid;
    Card c;
    FileAddStatus st = open_card(image, true, c, err);
    if (st != FileAddStatus::Ok) return st;
    FILINFO fno{};
    const FRESULT sr = f_stat(fat_path(comps, comps.size()).c_str(), &fno);
    if (sr != FR_OK) return status_of_open(sr, path, err);
    std::vector<std::string> np(comps.begin(), comps.end() - 1);
    np.push_back(new_name);
    const FRESULT fr = f_rename(fat_path(comps, comps.size()).c_str(), fat_path(np, np.size()).c_str());
    switch (fr) {
    case FR_OK: return FileAddStatus::Ok;
    case FR_EXIST:
        err = "'" + shown(new_name) + "' already exists in that directory; nothing was renamed";
        return FileAddStatus::DestExists;
    case FR_DENIED:
        err = "cannot rename to '" + shown(new_name) + "': no free directory slots";
        return FileAddStatus::ImageFull;
    case FR_INVALID_NAME:
        err = "'" + shown(new_name) + "' is not a valid name on the card";
        return FileAddStatus::DestInvalid;
    default:
        err = "cannot rename '" + shown(path) + "' (" + fr_text(fr) + ")";
        return FileAddStatus::ImageUnusable;
    }
}

FileAddStatus make_dir(const std::string& image, const std::string& dir, const std::string& name,
                       std::string& err) {
    err.clear();
    std::vector<std::string> comps;
    if (!split_path(dir, comps, err)) return FileAddStatus::DestInvalid;
    if (!check_new_name(name, err)) return FileAddStatus::DestInvalid;
    Card c;
    FileAddStatus st = open_card(image, true, c, err);
    if (st != FileAddStatus::Ok) return st;
    if (!comps.empty()) {
        FILINFO pf{};
        const FRESULT pr = f_stat(fat_path(comps, comps.size()).c_str(), &pf);
        if (pr != FR_OK) return status_of_open(pr, dir, err);
        if (!(pf.fattrib & AM_DIR)) {
            err = "'" + shown(dir) + "' is a file, not a directory";
            return FileAddStatus::DestInvalid;
        }
    }
    std::vector<std::string> np = comps;
    np.push_back(name);
    const std::string target = fat_path(np, np.size());
    const FRESULT fr = f_mkdir(target.c_str());
    switch (fr) {
    case FR_OK: return FileAddStatus::Ok;
    case FR_EXIST:
        err = "'" + shown(name) + "' already exists in that directory; nothing was created";
        return FileAddStatus::DestExists;
    case FR_DENIED:
        err = "cannot create directory '" + shown(name) + "': no free space or no free directory slots";
        return FileAddStatus::ImageFull;
    case FR_INVALID_NAME:
        err = "'" + shown(name) + "' is not a valid name on the card";
        return FileAddStatus::DestInvalid;
    default:
        err = "cannot create directory '" + shown(name) + "' (" + fr_text(fr) + ")";
        return FileAddStatus::ImageUnusable;
    }
}

FileAddStatus remove(const std::string& image, const std::string& path, std::string& err,
                     RemoveSummary* summary) {
    err.clear();
    std::vector<std::string> comps;
    if (!split_path(path, comps, err)) return FileAddStatus::DestInvalid;
    if (comps.empty()) {
        err = "the card root cannot be removed";
        return FileAddStatus::DestInvalid;
    }
    Card c;
    FileAddStatus st = open_card(image, true, c, err);
    if (st != FileAddStatus::Ok) return st;

    FILINFO top{};
    const std::string top_fat = fat_path(comps, comps.size());
    const FRESULT sr = f_stat(top_fat.c_str(), &top);
    if (sr != FR_OK) return status_of_open(sr, path, err);
    const bool top_dir = (top.fattrib & AM_DIR) != 0;
    if (top.fattrib & AM_RDO) {
        err = "'" + shown(path) + "' is read-only on the card; it is not removed";
        return FileAddStatus::DestInvalid;
    }
    std::set<DWORD> visited;
    // Depth-first with an explicit stack so a deep tree cannot overflow the call stack.
    struct Frame { std::string fat, shown; DWORD parent; };
    struct Item { std::string fat, shown; bool dir; };
    std::vector<Frame> stack;
    std::vector<Item> pre;      // pre-order; reversed gives children before their directory
    pre.push_back({top_fat, path, top_dir});
    if (top_dir) {
        DIR pd{};
        const FRESULT por = f_opendir(&pd, fat_path(comps, comps.size() - 1).c_str());
        if (por != FR_OK) return status_of_open(por, path, err);
        const DWORD pc = pd.obj.sclust;
        f_closedir(&pd);
        stack.push_back({top_fat, path, pc});
    }
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        std::vector<CardEntry> ents;
        DWORD mine = 0;
        st = read_dir(f.fat, f.shown, ents, &visited, err, &mine);
        if (st != FileAddStatus::Ok) return st;
        // Start cluster 0 is the root (FAT32): there is no ".." to read, and the visited set refuses it as
        // soon as the walk meets a directory it has already read.
        if (mine != 0 && !dotdot_names_parent(c.fs, mine, f.parent)) {
            err = "directory '" + shown(f.shown) + "' on the card is not owned by its parent (a cross-linked or "
                  "damaged directory structure); nothing was removed";
            return FileAddStatus::ImageUnusable;
        }
        for (const CardEntry& e : ents) {
            const std::string sh = f.shown + "/" + shown(e.name);
            if (e.open_name.empty()) {
                err = "'" + sh + "' on the card has a name FatFs cannot open by its long or its 8.3 name; "
                      "nothing was removed";
                return FileAddStatus::DestInvalid;
            }
            if (e.attr & AM_RDO) {
                err = "'" + sh + "' is read-only on the card; nothing was removed";
                return FileAddStatus::DestInvalid;
            }
            const std::string fp = f.fat + "/" + e.open_name;
            pre.push_back({fp, sh, e.dir});
            if (e.dir) stack.push_back({fp, sh, mine});
        }
    }
    // A directory's children were all pushed after it in `pre`, so the reverse order removes them first.
    RemoveSummary sum;
    for (auto it = pre.rbegin(); it != pre.rend(); ++it) {
        const FRESULT fr = f_unlink(it->fat.c_str());
        if (fr != FR_OK) {
            err = "cannot remove '" + shown(it->shown) + "' (" + fr_text(fr) + "); " +
                  std::to_string(sum.files + sum.dirs) + " entries were removed before it";
            if (summary) *summary = sum;
            return fr == FR_DENIED ? FileAddStatus::DestInvalid : FileAddStatus::ImageUnusable;
        }
        if (it->dir) ++sum.dirs; else ++sum.files;
    }
    if (summary) *summary = sum;
    return FileAddStatus::Ok;
}

std::string display_name(const CardEntry& e) { return carry::escape_name(e.name, false); }
std::string host_name_for(const CardEntry& e) { return carry::escape_name(e.name, true); }

}  // namespace sdcard::cardops
