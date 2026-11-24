#include "core/sdcard_card_ops.h"

#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "peripheral/sd_write_journal.h"

extern "C" {
#include "third_party/fatfs/ff.h"
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

// Can FatFs open an entry by this name? Not when it ends in a dot or a space (FatFs drops those, so the
// path would name another entry), holds a control byte or one of "*:<>?|, '/', '\' or DEL, or is longer
// than a long name can be.
bool fatfs_can_open_by(const std::string& n) {
    if (n.empty() || n.size() > 255) return false;
    if (n.back() == '.' || n.back() == ' ') return false;
    for (char ch : n) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) return false;
        if (std::strchr("\"*:<>?|/\\", c)) return false;
    }
    return true;
}

std::string fold(const std::string& s) {
    std::string k = s;
    for (char& c : k)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return k;
}

// The image on FatFs drive 0, mounted, for as long as the object lives. Unmount, then detach, on every
// return path. `dirty` is set before the first call that may MODIFY the volume; only then is it
// flushed at unmount (a flush is not a no-op: f_getfree() alone can leave FSInfo pending, GH #292), and
// the flush comes BEFORE the unmount, because a failed FatFs call can leave a change in its memory that
// f_mount(nullptr) would discard, leaving a lost cluster on the card.
struct Card {
    FATFS fs{};
    bool  attached = false;
    bool  mounted  = false;
    bool  dirty    = false;
    std::shared_ptr<sd_write_journal::Journal> journal;
    ~Card() {
        if (mounted) {
            if (dirty) f_syncvol("0:");
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
    if (fatfs_can_open_by(e.name)) e.open_name = e.name;
    else if (fatfs_can_open_by(e.altname)) e.open_name = e.altname;
    return e;
}

// Read directory `fat` into `out` (unsorted). `shown_dir` names it in messages.
FileAddStatus read_dir(const std::string& fat, const std::string& shown_dir, std::vector<CardEntry>& out,
                       std::set<DWORD>* visited, std::string& err) {
    DIR dj{};
    FRESULT fr = f_opendir(&dj, fat.c_str());
    if (fr != FR_OK) return status_of_open(fr, shown_dir, err);
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
    }
    f_closedir(&dj);
    return FileAddStatus::Ok;
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
    c.dirty = true;
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
    // An existing name is refused before anything is written (FatFs's own FR_EXIST arm below stays as
    // the backstop), so a refusal leaves the card byte-identical.
    FILINFO ef{};
    if (f_stat(target.c_str(), &ef) == FR_OK) {
        err = "'" + shown(name) + "' already exists in that directory; nothing was created";
        return FileAddStatus::DestExists;
    }
    c.dirty = true;
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
    struct Frame { std::string fat, shown; };
    struct Item { std::string fat, shown; bool dir; };
    std::vector<Frame> stack;
    std::vector<Item> pre;      // pre-order; reversed gives children before their directory
    pre.push_back({top_fat, path, top_dir});
    if (top_dir) stack.push_back({top_fat, path});
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        std::vector<CardEntry> ents;
        st = read_dir(f.fat, f.shown, ents, &visited, err);
        if (st != FileAddStatus::Ok) return st;
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
            if (e.dir) stack.push_back({fp, sh});
        }
    }
    // A directory's children were all pushed after it in `pre`, so the reverse order removes them first.
    RemoveSummary sum;
    c.dirty = true;
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
