#include "core/sdcard_carry.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/sdcard_provisioner.h"
#include "peripheral/sd_write_journal.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace sdcard::carry {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = 64 * 1024;

std::string fr_text(FRESULT r) {
    switch (r) {
    case FR_OK: return "ok";
    case FR_DISK_ERR: return "disk error";
    case FR_NOT_READY: return "not ready";
    case FR_NO_FILE: return "no such file";
    case FR_NO_PATH: return "no such path";
    case FR_INVALID_NAME: return "invalid name";
    case FR_DENIED: return "denied (full, or a directory in the way)";
    case FR_EXIST: return "already exists";
    case FR_WRITE_PROTECTED: return "write protected";
    case FR_NO_FILESYSTEM: return "no FAT filesystem";
    default: return "FatFs error " + std::to_string(static_cast<int>(r));
    }
}

std::string parent_of(const std::string& p) {
    const size_t i = p.find_last_of('/');
    return i == std::string::npos || i == 0 ? std::string() : p.substr(0, i);
}

}  // namespace

std::string path_key(const std::string& path) {
    std::string k = path;
    for (char& c : k)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return k;
}

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

void Tree::sort_entries() {
    std::vector<std::pair<std::string, size_t>> order;
    order.reserve(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) order.emplace_back(path_key(entries_[i].path), i);
    std::sort(order.begin(), order.end());
    std::vector<Entry> sorted;
    sorted.reserve(entries_.size());
    keys_.clear();
    for (const auto& o : order) { sorted.push_back(std::move(entries_[o.second])); keys_.push_back(o.first); }
    entries_ = std::move(sorted);
}

const Entry* Tree::find(const std::string& key) const {
    const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
    if (it == keys_.end() || *it != key) return nullptr;
    return &entries_[static_cast<size_t>(it - keys_.begin())];
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

struct Volume::Impl {
    FATFS fs{};
    FIL   fil{};
    bool  attached = false, mounted = false, file_open = false;
    bool  read_only = true;
};

Volume::~Volume() {
    if (!impl_) return;
    if (impl_->file_open) f_close(&impl_->fil);
    if (impl_->mounted) {
        const std::string root = std::string(1, static_cast<char>('0' + drive_)) + ":";
        if (dirty_ && !impl_->read_only) f_syncvol(root.c_str());
        f_mount(nullptr, root.c_str(), 0);
    }
    if (impl_->attached) fatfs_glue::detach(static_cast<uint8_t>(drive_));
}

std::string Volume::fat(const std::string& path) const {
    return std::string(1, static_cast<char>('0' + drive_)) + ":" + (path.empty() ? "/" : path);
}

std::unique_ptr<Volume> Volume::open(int drive, const std::string& image,
                                     std::shared_ptr<sd_write_journal::Journal> journal,
                                     bool read_only, std::string& err) {
    if (drive < 0 || drive >= FF_VOLUMES) { err = "no such FatFs drive"; return nullptr; }
    uint32_t lba = 0, total = 0;
    if (!read_partition_geometry(image, lba, total, err)) return nullptr;
    if (journal) {
        // A journal that rewrote the partition table or the boot sector holds another card
        // than the geometry above describes: refuse rather than read at the wrong offsets.
        uint8_t sec[512];
        if (journal->lookup(0, sec) || journal->lookup(lba, sec)) {
            err = "the writes file '" + journal->file() +
                  "' holds a rewritten partition table or boot sector for '" + image + "'";
            return nullptr;
        }
    }
    std::unique_ptr<Volume> v(new Volume());
    v->impl_.reset(new Impl());
    v->drive_ = drive;
    v->impl_->read_only = read_only;
    std::string glue_err;
    if (!fatfs_glue::attach(static_cast<uint8_t>(drive), image, lba, total, glue_err, journal, read_only)) {
        err = "cannot open SD image '" + image + "' (" + glue_err + ")";
        return nullptr;
    }
    v->impl_->attached = true;
    const std::string root = std::string(1, static_cast<char>('0' + drive)) + ":";
    const FRESULT fr = f_mount(&v->impl_->fs, root.c_str(), 1);
    if (fr != FR_OK) {
        // FatFs keeps the pointer of the FATFS it was given even when the mount fails: unregister it
        // before the object goes, or the next mount on this drive writes into freed memory.
        f_mount(nullptr, root.c_str(), 0);
        err = "'" + image + "' does not hold a FAT32 filesystem this build can mount (" +
              fr_text(fr) + ")";
        return nullptr;
    }
    v->impl_->mounted = true;
    if (!v->walk(err)) return nullptr;
    return v;
}

bool Volume::walk(std::string& err) {
    entries_.clear();
    std::vector<std::string> stack{std::string()};
    while (!stack.empty()) {
        const std::string dirpath = stack.back();
        stack.pop_back();
        DIR dj{};
        FRESULT fr = f_opendir(&dj, fat(dirpath).c_str());
        if (fr != FR_OK) {
            err = "cannot read directory '" + escape_name(dirpath.empty() ? std::string("/") : dirpath, false) +
                  "' (" + fr_text(fr) + ")";
            return false;
        }
        for (;;) {
            FILINFO fno{};
            fr = f_readdir(&dj, &fno);
            if (fr != FR_OK) {
                f_closedir(&dj);
                err = "cannot read directory '" + escape_name(dirpath.empty() ? std::string("/") : dirpath, false) +
                      "' (" + fr_text(fr) + ")";
                return false;
            }
            if (fno.fname[0] == 0) break;
            Entry e;
            e.path    = dirpath + "/" + fno.fname;
            e.dir     = (fno.fattrib & AM_DIR) != 0;
            e.size    = e.dir ? 0 : static_cast<uint32_t>(fno.fsize);
            e.attr    = fno.fattrib;
            e.fdate   = fno.fdate;
            e.ftime   = fno.ftime;
            e.altname = fno.altname;
            const size_t nl = std::strlen(fno.fname);
            e.unaddressable = nl > 0 && (fno.fname[nl - 1] == ' ' || fno.fname[nl - 1] == '.');
            if (e.dir && !e.unaddressable) stack.push_back(e.path);
            entries_.push_back(std::move(e));
        }
        f_closedir(&dj);
    }
    sort_entries();
    return true;
}

bool Volume::open(const Entry& e, std::string& err) {
    close();
    const FRESULT fr = f_open(&impl_->fil, fat(e.path).c_str(), FA_READ);
    if (fr != FR_OK) { err = "cannot read '" + escape_name(e.path, false) + "' on the card (" + fr_text(fr) + ")"; return false; }
    impl_->file_open = true;
    return true;
}

bool Volume::read(uint8_t* buf, size_t n, size_t& got, std::string& err) {
    UINT br = 0;
    const FRESULT fr = f_read(&impl_->fil, buf, static_cast<UINT>(n), &br);
    got = br;
    if (fr != FR_OK) { err = std::string("a read of the card failed (") + fr_text(fr) + ")"; return false; }
    return true;
}

void Volume::close() {
    if (impl_ && impl_->file_open) { f_close(&impl_->fil); impl_->file_open = false; }
}

bool Volume::free_space(uint64_t& free_clusters, uint64_t& cluster_bytes, std::string& err) {
    DWORD nfree = 0;
    FATFS* fsp = nullptr;
    const std::string root = std::string(1, static_cast<char>('0' + drive_)) + ":";
    const FRESULT fr = f_getfree(root.c_str(), &nfree, &fsp);
    if (fr != FR_OK || !fsp) { err = std::string("cannot read the free-space map (") + fr_text(fr) + ")"; return false; }
    free_clusters = nfree;
    cluster_bytes = static_cast<uint64_t>(fsp->csize) * 512;
    return true;
}

bool Volume::sync(std::string& err) {
    const std::string root = std::string(1, static_cast<char>('0' + drive_)) + ":";
    const FRESULT fr = f_syncvol(root.c_str());
    if (fr != FR_OK) { err = std::string("cannot flush the card (") + fr_text(fr) + ")"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// The lenient tree (an under-clustered image FatFs refuses)
// ---------------------------------------------------------------------------

namespace {

class MemTree : public Tree {
public:
    explicit MemTree(Fat32Tree&& t) : tree_(std::move(t)) {
        std::vector<std::pair<Entry, const Fat32Node*>> all;
        flatten(tree_.root, std::string(), all);
        std::vector<std::pair<std::string, size_t>> order;
        for (size_t i = 0; i < all.size(); ++i) order.emplace_back(path_key(all[i].first.path), i);
        std::sort(order.begin(), order.end());
        for (const auto& o : order) {
            entries_.push_back(all[o.second].first);
            keys_.push_back(o.first);
            nodes_.push_back(all[o.second].second);
        }
    }
    bool open(const Entry& e, std::string& err) override {
        const Entry* f = find(path_key(e.path));
        if (!f) { err = "no such file '" + e.path + "'"; return false; }
        cur_ = nodes_[static_cast<size_t>(f - entries_.data())];
        pos_ = 0;
        return true;
    }
    bool read(uint8_t* buf, size_t n, size_t& got, std::string&) override {
        const size_t left = cur_->data.size() - pos_;
        got = n < left ? n : left;
        if (got) std::memcpy(buf, cur_->data.data() + pos_, got);
        pos_ += got;
        return true;
    }
    void close() override { cur_ = nullptr; }
private:
    static void flatten(const std::vector<Fat32Node>& level, const std::string& dir,
                        std::vector<std::pair<Entry, const Fat32Node*>>& out) {
        for (const Fat32Node& n : level) {
            Entry e;
            e.path  = dir + "/" + n.name;
            e.dir   = n.is_dir;
            e.size  = n.is_dir ? 0 : static_cast<uint32_t>(n.data.size());
            e.attr  = n.attr;
            e.fdate = n.wdate;
            e.ftime = n.wtime;
            out.emplace_back(e, &n);
            if (n.is_dir) flatten(n.children, e.path, out);
        }
    }
    Fat32Tree tree_;
    std::vector<const Fat32Node*> nodes_;
    const Fat32Node* cur_ = nullptr;
    size_t pos_ = 0;
};

}  // namespace

std::unique_ptr<Tree> open_tree_lenient(const std::string& image, std::string& err) {
    std::string fatfs_err;
    if (auto v = Volume::open(1, image, nullptr, true, fatfs_err)) return v;
    uint32_t lba = 0;
    if (!fat32_find_partition(image, lba)) { err = fatfs_err; return nullptr; }
    Fat32Tree t;
    if (!fat32_read_tree(image, lba, t)) {
        err = "cannot read the FAT32 tree of '" + image + "' (" + fatfs_err + ")";
        return nullptr;
    }
    return std::make_unique<MemTree>(std::move(t));
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool same_entry(Tree& x, const Entry& ex, Tree& y, const Entry& ey, bool& same, std::string& err) {
    same = false;
    if (ex.unaddressable || ey.unaddressable) { same = true; return true; }   // cannot be read: not compared
    if (ex.dir != ey.dir) return true;
    if ((ex.attr ^ ey.attr) & kUserAttrMask) return true;
    if (ex.dir) { same = true; return true; }
    if (ex.size != ey.size) return true;
    if (ex.size == 0) { same = true; return true; }
    if (!x.open(ex, err)) return false;
    if (!y.open(ey, err)) { x.close(); return false; }
    std::vector<uint8_t> a(kChunk), b(kChunk);
    bool equal = true, ok = true;
    for (;;) {
        size_t ga = 0, gb = 0;
        if (!x.read(a.data(), a.size(), ga, err) || !y.read(b.data(), b.size(), gb, err)) { ok = false; break; }
        // A short read from one side only is a difference, and the files have equal sizes, so this is a fault.
        if (ga != gb || (ga && std::memcmp(a.data(), b.data(), ga) != 0)) { equal = false; break; }
        if (ga == 0) break;
    }
    x.close();
    y.close();
    if (!ok) return false;
    same = equal;
    return true;
}

bool compute_changes(Tree& x, Tree& y, std::vector<Change>& out, std::string& err) {
    out.clear();
    const auto& kx = x.keys();
    const auto& ky = y.keys();
    size_t i = 0, j = 0;
    while (i < kx.size() || j < ky.size()) {
        if (j >= ky.size() || (i < kx.size() && kx[i] < ky[j])) {
            out.push_back({Kind::Added, x.entries()[i]});
            ++i;
        } else if (i >= kx.size() || ky[j] < kx[i]) {
            out.push_back({Kind::Deleted, y.entries()[j]});
            ++j;
        } else {
            bool same = false;
            if (!same_entry(x, x.entries()[i], y, y.entries()[j], same, err)) return false;
            if (!same) out.push_back({Kind::Changed, x.entries()[i]});
            ++i; ++j;
        }
    }
    return true;
}

bool changes_at(Tree& x, Tree& y, const std::vector<std::string>& keys,
                std::vector<Change>& out, std::string& err) {
    out.clear();
    for (const std::string& k : keys) {
        const Entry* ex = x.find(k);
        const Entry* ey = y.find(k);
        if (!ex && !ey) continue;
        if (ex && !ey) { out.push_back({Kind::Added, *ex}); continue; }
        if (!ex && ey) { out.push_back({Kind::Deleted, *ey}); continue; }
        bool same = false;
        if (!same_entry(x, *ex, y, *ey, same, err)) return false;
        if (!same) out.push_back({Kind::Changed, *ex});
    }
    return true;
}

// ---------------------------------------------------------------------------
// Carry-over
// ---------------------------------------------------------------------------

std::string Report::text() const {
    std::string out;
    auto list = [&](const char* heading, std::vector<std::string> v) {
        if (v.empty()) return;
        std::sort(v.begin(), v.end());
        out += std::string(heading) + " (" + std::to_string(v.size()) + "):\n";
        for (const auto& s : v) out += "  " + s + "\n";
    };
    list("Kept from your card", carried);
    list("Replaced by the new NextZXOS (your version was not kept)", overridden);
    list("You deleted these, they are back on the new card", deleted_back);
    list("You changed these, the new NextZXOS removed them (not carried)", removed);
    list("Not carried: jnext cannot write these names (a trailing space or dot; what is inside is not carried either)", unwritable);
    if (out.empty()) out = "Nothing of yours needed carrying over.\n";
    return out;
}

void decide(const std::vector<Change>& user, const std::vector<Change>& dist, const Tree& n,
            std::vector<Entry>& carry, Report& report) {
    std::map<std::string, Kind> dist_by_key;
    for (const Change& d : dist) dist_by_key[path_key(d.entry.path)] = d.kind;
    for (const Change& u : user) {
        const std::string key = path_key(u.entry.path);
        const char* what = u.entry.dir ? "directory " : "";
        if (u.kind == Kind::Deleted) {
            if (n.find(key)) report.deleted_back.push_back(what + u.entry.path);
            continue;   // a deletion is never carried
        }
        if (u.entry.unaddressable) { report.unwritable.push_back(what + u.entry.path); continue; }
        const auto d = dist_by_key.find(key);
        if (d != dist_by_key.end()) {
            (d->second == Kind::Deleted ? report.removed : report.overridden)
                .push_back(what + u.entry.path);
            continue;
        }
        // The new card holds this very path with the other type (a file where the user made a
        // directory, a directory where the user made a file) and the distribution left it alone: the
        // distribution wins, the entry is listed, and so are its children (their parent is then a file
        // on the new card, or the entry's own type cannot replace a directory).
        if (const Entry* self = n.find(key); self && self->dir != u.entry.dir) {
            report.overridden.push_back(what + u.entry.path);
            continue;
        }
        // A path through something the new card made a FILE cannot hold this entry.
        bool blocked = false;
        for (std::string p = parent_of(u.entry.path); !p.empty(); p = parent_of(p)) {
            const Entry* a = n.find(path_key(p));
            if (a && !a->dir) { blocked = true; break; }
        }
        if (blocked) { report.overridden.push_back(what + u.entry.path); continue; }
        carry.push_back(u.entry);
        report.carried.push_back(std::string(u.kind == Kind::Added ? "added " : "changed ") + what + u.entry.path);
    }
}

namespace {

struct Created {
    std::vector<std::string> paths;   // FatFs paths, creation order
};

// Make `dir` (a card path) and every missing ancestor.
bool make_dirs(Volume& dst, const std::string& dir, Created& created, std::string& err) {
    if (dir.empty()) return true;
    if (!parent_of(dir).empty() && !make_dirs(dst, parent_of(dir), created, err)) return false;
    FILINFO fno{};
    const FRESULT st = f_stat(dst.fat(dir).c_str(), &fno);
    if (st == FR_OK) {
        if (!(fno.fattrib & AM_DIR)) { err = "'" + dir + "' is a file on the new card"; return false; }
        return true;
    }
    dst.mark_dirty();
    const FRESULT mk = f_mkdir(dst.fat(dir).c_str());
    if (mk != FR_OK) { err = "cannot make directory '" + dir + "' (" + fr_text(mk) + ")"; return false; }
    created.paths.push_back(dst.fat(dir));
    return true;
}

}  // namespace

namespace {

// A name that fits 8.3 in one case needs no long-name entry: FatFs stores it as its own short name.
bool plain_short_name(const std::string& n) {
    const size_t dot = n.rfind('.');
    const std::string base = dot == std::string::npos ? n : n.substr(0, dot);
    const std::string ext  = dot == std::string::npos ? std::string() : n.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3 || base.find('.') != std::string::npos) return false;
    bool lower = false, upper = false;
    for (const std::string* part : {&base, &ext})
        for (const char c : *part) {
            if (std::strchr(" +,;=[]", c) || static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7E) return false;
            if (c >= 'a' && c <= 'z') lower = true;
            if (c >= 'A' && c <= 'Z') upper = true;
        }
    return !(lower && upper);
}

// The order a carry writes in: in each directory the names that are their own short names first. A name
// that needs a long-name entry gets a generated short name (LONGNA~1), and one written BEFORE a file whose
// real name is LONGNA~1.TXT makes FatFs open that file instead of making it (one file silently replaces the
// other). A parent still comes before its children (it is a prefix of their component lists).
std::vector<Entry> write_order(const std::vector<Entry>& items) {
    auto comps = [](const std::string& path) {
        std::vector<std::pair<int, std::string>> v;
        size_t s = 1;
        while (s <= path.size()) {
            const size_t e = path.find('/', s);
            const std::string c = path.substr(s, e == std::string::npos ? e : e - s);
            std::string up = c;
            for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            v.push_back({plain_short_name(c) ? 0 : 1, up});
            if (e == std::string::npos) break;
            s = e + 1;
        }
        return v;
    };
    std::vector<Entry> out = items;
    std::stable_sort(out.begin(), out.end(), [&](const Entry& a, const Entry& b) { return comps(a.path) < comps(b.path); });
    return out;
}

}  // namespace

FileAddStatus apply_carry(Tree& src, const std::vector<Entry>& items, Volume& dst,
                          std::string& err, CarryStats* stats) {
    err.clear();
    // ---- free space, before anything is written ------------------------------
    uint64_t free_clusters = 0, cluster_bytes = 0;
    if (!dst.free_space(free_clusters, cluster_bytes, err)) return FileAddStatus::ImageUnusable;
    std::set<std::string> new_dirs;
    uint64_t need = 0;
    for (const Entry& e : items) {
        for (std::string p = e.dir ? e.path : parent_of(e.path); !p.empty(); p = parent_of(p)) {
            FILINFO fno{};
            if (f_stat(dst.fat(p).c_str(), &fno) != FR_OK) new_dirs.insert(p);
        }
        if (!e.dir) need += (static_cast<uint64_t>(e.size) + cluster_bytes - 1) / cluster_bytes;
    }
    need += new_dirs.size();
    if (need > free_clusters) {
        err = "the new card has " + std::to_string(free_clusters * cluster_bytes / 1024) + " KB free (" +
              std::to_string(free_clusters) + " clusters of " + std::to_string(cluster_bytes) +
              " bytes); your files need at least " + std::to_string(need) + " clusters";
        return FileAddStatus::ImageFull;
    }

    // ---- write ----------------------------------------------------------------
    Created created;
    CarryStats st;
    auto rollback = [&](FileAddStatus s) {
        size_t left = 0;
        for (auto r = created.paths.rbegin(); r != created.paths.rend(); ++r)
            if (f_unlink(r->c_str()) != FR_OK) ++left;
        err += "; the carry-over was rolled back, removing the " + std::to_string(created.paths.size()) +
               " entries it had created";
        if (left) err += " (" + std::to_string(left) + " could not be removed)";
        return s;
    };
    std::vector<uint8_t> buf(kChunk);
    const std::vector<Entry> ordered = write_order(items);
    for (const Entry& e : ordered) {
        if (e.dir) {
            if (!make_dirs(dst, e.path, created, err)) return rollback(FileAddStatus::DestInvalid);
            dst.mark_dirty();
            f_chmod(dst.fat(e.path).c_str(), e.attr & kUserAttrMask, kUserAttrMask);
            ++st.dirs;
            continue;
        }
        if (!make_dirs(dst, parent_of(e.path), created, err)) return rollback(FileAddStatus::DestInvalid);
        const std::string fp = dst.fat(e.path);
        FILINFO fno{};
        bool existed = false;
        if (f_stat(fp.c_str(), &fno) == FR_OK) {
            if (fno.fattrib & AM_DIR) { err = "'" + e.path + "' is a directory on the new card"; return rollback(FileAddStatus::DestInvalid); }
            existed = true;
            dst.mark_dirty();
            if (fno.fattrib & AM_RDO) f_chmod(fp.c_str(), 0, AM_RDO);
        }
        if (!src.open(e, err)) return rollback(FileAddStatus::SourceUnreadable);
        dst.mark_dirty();
        FIL fil{};
        FRESULT fr = f_open(&fil, fp.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            src.close();
            err = "cannot create '" + e.path + "' on the new card (" + fr_text(fr) + ")";
            return rollback(fr == FR_DENIED ? FileAddStatus::ImageFull : FileAddStatus::DestInvalid);
        }
        uint64_t left = e.size;
        FileAddStatus bad = FileAddStatus::Ok;
        while (left > 0 && bad == FileAddStatus::Ok) {
            size_t got = 0;
            if (!src.read(buf.data(), left < buf.size() ? static_cast<size_t>(left) : buf.size(), got, err) || got == 0) {
                if (err.empty()) err = "'" + e.path + "' became shorter while it was being copied";
                bad = FileAddStatus::SourceUnreadable;
                break;
            }
            UINT bw = 0;
            fr = f_write(&fil, buf.data(), static_cast<UINT>(got), &bw);
            if (fr != FR_OK || bw != got) {
                err = "writing '" + e.path + "' to the new card failed (" + fr_text(fr == FR_OK ? FR_DENIED : fr) + ")";
                bad = (fr == FR_OK || fr == FR_DENIED) ? FileAddStatus::ImageFull : FileAddStatus::ImageUnusable;
                break;
            }
            left -= got;
        }
        src.close();
        fr = f_close(&fil);
        if (bad == FileAddStatus::Ok && fr != FR_OK) { err = "cannot flush '" + e.path + "' (" + fr_text(fr) + ")"; bad = FileAddStatus::ImageUnusable; }
        if (bad != FileAddStatus::Ok) { f_unlink(fp.c_str()); return rollback(bad); }
        if (!existed) created.paths.push_back(fp);
        f_chmod(fp.c_str(), e.attr & kUserAttrMask, kUserAttrMask);
        FILINFO tm{};
        tm.fdate = e.fdate;
        tm.ftime = e.ftime;
        f_utime(fp.c_str(), &tm);
        ++st.files;
        st.bytes += e.size;
    }
    std::string serr;
    if (!dst.sync(serr)) { err = serr; return rollback(FileAddStatus::ImageUnusable); }
    if (stats) *stats = st;
    return FileAddStatus::Ok;
}

bool verify_carried_equal(Tree& result, Tree& src, const std::vector<Entry>& carried, std::string& err) {
    for (const Entry& e : carried) {
        const Entry* r = result.find(path_key(e.path));
        const Entry* s = src.find(path_key(e.path));
        if (!r || !s) { err = "carried path '" + e.path + "' is missing after the carry-over"; return false; }
        bool same = false;
        if (!same_entry(result, *r, src, *s, same, err)) return false;
        if (!same) {
            std::string why = r->dir != s->dir ? "type" : (r->attr ^ s->attr) & kUserAttrMask ? "attributes " + std::to_string(r->attr & kUserAttrMask) + " vs " + std::to_string(s->attr & kUserAttrMask)
                              : r->size != s->size ? "size " + std::to_string(r->size) + " vs " + std::to_string(s->size) : "content";
            err = "carried path '" + e.path + "' differs from its source after the carry-over (" + why + ")";
            return false;
        }
    }
    return true;
}

bool verify_nothing_else_moved(Tree& result, Tree& n_as_made, const std::vector<Entry>& carried, std::string& err) {
    std::set<std::string> carried_keys;
    for (const Entry& e : carried) {
        carried_keys.insert(path_key(e.path));
        // the parent directories apply_carry made for it are part of the carry
        for (std::string p = parent_of(e.path); !p.empty(); p = parent_of(p)) carried_keys.insert(path_key(p));
    }
    std::vector<Change> ch;
    if (!compute_changes(result, n_as_made, ch, err)) return false;
    for (const Change& c : ch) {
        if (c.kind == Kind::Deleted || !carried_keys.count(path_key(c.entry.path))) {
            err = "'" + c.entry.path + "' " +
                  (c.kind == Kind::Deleted ? "is missing from" : c.kind == Kind::Added ? "was added to" : "differs from") +
                  " the new card as made, and was not carried";
            return false;
        }
    }
    return true;
}

bool verify_carry(Tree& result, Tree& n_as_made, Tree& src, const std::vector<Entry>& carried,
                  std::string& err) {
    return verify_carried_equal(result, src, carried, err) &&
           verify_nothing_else_moved(result, n_as_made, carried, err);
}

bool copy_kept_file(const sd_write_journal::Journal& from, sd_write_journal::Journal& to) {
    const auto k = from.kept_file();
    if (!k) return true;
    return to.commit_file(k->name, k->data);
}

// ---------------------------------------------------------------------------
// The card as made
// ---------------------------------------------------------------------------

namespace {

std::string first_line(const std::string& path) {
    std::ifstream f(path);
    std::string l;
    if (f) std::getline(f, l);
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.erase(l.begin());
    return l;
}

int card_recipe(const std::string& card) {
    const std::string r = first_line(card + kFixedImageRecipeSuffix);
    if (r.empty()) return 1;   // a card made before recipes existed IS recipe 1
    for (char c : r) if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    return std::atoi(r.c_str());
}

std::string raw_sha_of(const std::string& raw) {
    const std::string side = first_line(raw + ".sha256");
    const size_t sp = side.find_first_of(" \t");
    return sp == std::string::npos ? side : side.substr(0, sp);
}

std::atomic<uint64_t> g_pristine_hashes{0};

// The last verification of the original: <raw>.verified, one line "<digest> <size> <mtime>\n"
// (digest 64 lowercase hex, size decimal uint64, mtime last_write_time ticks, decimal int64).
struct Stamp { uint64_t size = 0; int64_t mtime = 0; };

bool stamp_of(const std::string& raw, Stamp& st) {
    std::error_code ec;
    const uint64_t sz = fs::file_size(raw, ec);
    if (ec) return false;
    const auto t = fs::last_write_time(raw, ec);
    if (ec) return false;
    st.size = sz;
    st.mtime = static_cast<int64_t>(t.time_since_epoch().count());
    return true;
}

bool parse_u64(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 20) return false;
    v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    return true;
}

bool parse_i64(const std::string& s, int64_t& v) {
    const bool neg = !s.empty() && s[0] == '-';
    uint64_t m = 0;
    if (!parse_u64(neg ? s.substr(1) : s, m)) return false;
    if (m > (neg ? static_cast<uint64_t>(INT64_MAX) + 1 : static_cast<uint64_t>(INT64_MAX))) return false;
    v = neg ? static_cast<int64_t>(0 - m) : static_cast<int64_t>(m);
    return true;
}

// True when the record exists, is exactly in the grammar, and names this digest, size and mtime.
bool verified_record_valid(const std::string& raw, const std::string& want, const Stamp& now) {
    const std::string rec = raw + ".verified";
    std::error_code ec;
    if (!fs::is_regular_file(rec, ec)) return false;   // not a FIFO or other special file: opening one could block
    std::ifstream f(rec, std::ios::binary);
    if (!f) return false;
    char buf[257];
    f.read(buf, sizeof buf);   // a record is under 120 bytes: a longer file is not one
    std::string body(buf, static_cast<size_t>(f.gcount()));
    if (body.size() > 256 || body.empty() || body.back() != '\n') return false;
    body.pop_back();
    if (body.find('\n') != std::string::npos) return false;
    const size_t a = body.find(' ');
    const size_t b = a == std::string::npos ? a : body.find(' ', a + 1);
    if (b == std::string::npos) return false;
    const std::string digest = body.substr(0, a);
    if (digest.size() != 64) return false;
    for (char c : digest) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    uint64_t sz = 0;
    int64_t mt = 0;
    if (!parse_u64(body.substr(a + 1, b - a - 1), sz) || !parse_i64(body.substr(b + 1), mt)) return false;
    return digest == want && sz == now.size && mt == now.mtime;
}

void write_verified_record(const std::string& raw, const std::string& digest, const Stamp& st) {
    std::ofstream f(raw + ".verified", std::ios::binary | std::ios::trunc);   // binary: a text-mode "\n" is CRLF on Windows
    if (f) f << digest << ' ' << st.size << ' ' << st.mtime << '\n';
}

}  // namespace

uint64_t pristine_hash_count() { return g_pristine_hashes.load(); }

bool pristine_base_ok(const std::string& card_image, std::string& why) {
    const std::string dir = fs::path(card_image).parent_path().string();
    const std::string raw = (fs::path(dir) / kImageFileName).string();
    std::error_code ec;
    if (!fs::is_regular_file(raw, ec)) { why = "the original image " + raw + " is missing"; return false; }
    const std::string want = raw_sha_of(raw);
    if (want.empty()) { why = "the original image has no .sha256 sidecar"; return false; }
    // The hash takes ~0.5 s: skipped while <raw>.verified says this digest was matched at this size and mtime.
    Stamp st;
    const bool have_stamp = stamp_of(raw, st);
    if (!(have_stamp && verified_record_valid(raw, want, st))) {
        ++g_pristine_hashes;   // the stat above was taken before the hash: a change during it voids the record
        if (sha256_file(raw) != want) { why = "the original image does not match its .sha256 sidecar"; return false; }
        if (have_stamp) write_verified_record(raw, want, st);
    }
    auto dist = [](const std::string& img) {
        const std::string d = first_line(img + kDistroSuffix);
        return d.empty() ? std::string(kLegacyDistribution) : d;
    };
    if (dist(card_image) != dist(raw)) {
        why = "the card (" + dist(card_image) + ") was not made from the original image on disk (" + dist(raw) + ")";
        return false;
    }
    const int recipe = card_recipe(card_image);
    if (recipe < 1 || recipe > kFixedImageRecipe) {
        why = "the card's recipe number is not one this jnext knows";
        return false;
    }
    if (recipe >= 3) {
        uint32_t lba = 0;
        if (!fat32_find_partition(card_image, lba)) { why = "the card has no FAT32 partition"; return false; }
        uint8_t id[4] = {};
        std::ifstream f(card_image, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(lba) * 512 + 0x43));
        f.read(reinterpret_cast<char*>(id), 4);
        const uint32_t have = static_cast<uint32_t>(id[0]) | (static_cast<uint32_t>(id[1]) << 8) |
                              (static_cast<uint32_t>(id[2]) << 16) | (static_cast<uint32_t>(id[3]) << 24);
        if (!f || have != volume_id_from_sha256(want)) {
            why = "the card's volume serial is not the one made from the original image (the card was re-formatted?)";
            return false;
        }
    }
    return true;
}

bool rebuild_as_made(const std::string& raw, const std::string& card_image,
                     const std::string& out_image, std::string& err) {
    if (!copy_sparse_file(raw, out_image, err)) return false;
    if (!patch_image_for_recipe(out_image, card_recipe(card_image), raw_sha_of(raw), err)) {
        std::error_code ec;
        fs::remove(out_image, ec);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Rescue verbs
// ---------------------------------------------------------------------------

bool copy_sparse_file(const std::string& src, const std::string& dst, std::string& err) {
    std::error_code ec;
    if (fs::exists(dst, ec)) { err = "'" + dst + "' already exists"; return false; }
    std::ifstream in(src, std::ios::binary);
    if (!in) { err = "cannot open '" + src + "'"; return false; }
    const uint64_t size = fs::file_size(src, ec);
    if (ec) { err = "cannot stat '" + src + "'"; return false; }
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) { err = "cannot create '" + dst + "'"; return false; }
    std::vector<char> buf(1 << 20);
    uint64_t pos = 0;
    while (pos < size) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), size - pos));
        in.read(buf.data(), static_cast<std::streamsize>(n));
        if (static_cast<size_t>(in.gcount()) != n) { err = "cannot read '" + src + "'"; out.close(); fs::remove(dst, ec); return false; }
        bool zero = true;
        for (size_t i = 0; i < n; ++i) if (buf[i]) { zero = false; break; }
        if (!zero) {
            out.seekp(static_cast<std::streamoff>(pos));
            out.write(buf.data(), static_cast<std::streamsize>(n));
        }
        pos += n;
    }
    if (size > 0) {   // the file's length, even when the tail was all zeros
        out.seekp(static_cast<std::streamoff>(size - 1));
        char last = 0;
        in.clear();
        in.seekg(static_cast<std::streamoff>(size - 1));
        in.read(&last, 1);
        out.write(&last, 1);
    }
    out.flush();
    if (!out) { err = "cannot write '" + dst + "'"; out.close(); fs::remove(dst, ec); return false; }
    return true;
}

FileAddStatus flatten(const std::string& image, const sd_write_journal::Journal& journal,
                      const std::string& out, std::string& err) {
    std::error_code ec;
    if (fs::exists(out, ec)) { err = "'" + out + "' already exists; give a new file name"; return FileAddStatus::DestInvalid; }
    const uint64_t size = fs::file_size(image, ec);
    if (ec) { err = "cannot stat the SD image '" + image + "'"; return FileAddStatus::DestInvalid; }
    if (journal.binding().bytes != size) {
        err = "the writes file '" + journal.file() + "' belongs to another card image (" +
              std::to_string(journal.binding().bytes) + " bytes, this one is " + std::to_string(size) + ")";
        return FileAddStatus::DestInvalid;
    }
    if (!copy_sparse_file(image, out, err)) return FileAddStatus::DestInvalid;
    std::fstream f(out, std::ios::in | std::ios::out | std::ios::binary);
    bool ok = static_cast<bool>(f);
    journal.for_each_sorted([&](uint32_t sector, const uint8_t* data) {
        if (!ok) return;
        if ((static_cast<uint64_t>(sector) + 1) * 512 > size) { ok = false; return; }
        f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(sector) * 512));
        f.write(reinterpret_cast<const char*>(data), 512);
        if (!f) ok = false;
    });
    f.flush();
    if (!ok || !f) {
        f.close();
        fs::remove(out, ec);
        err = "cannot lay the writes file over the copy";
        return FileAddStatus::DestInvalid;
    }
    return FileAddStatus::Ok;
}

// ---- names, CHANGES.txt ---------------------------------------------------------

namespace {

const char* kMagic = "jnext-changes 1";

bool printable(unsigned char c) { return c >= 0x20 && c <= 0x7E; }

bool reserved_device(const std::string& name) {
    std::string stem = name.substr(0, name.find('.'));
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* const fixed[] = {"CON", "PRN", "AUX", "NUL"};
    for (const char* f : fixed) if (stem == f) return true;
    if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') return true;
    return false;
}

std::string hex2(unsigned char c) {
    char b[4];
    std::snprintf(b, sizeof b, "%%%02X", c);
    return b;
}

}  // namespace

std::string escape_name(const std::string& s, bool host) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        bool esc = !printable(c) || c == '%';
        if (host && std::strchr("<>:\"/\\|?*", c)) esc = true;
        if (host && i == 0 && reserved_device(s)) esc = true;           // CON.TXT -> %43ON.TXT
        if (host && i + 1 == s.size() && (c == '.' || c == ' ')) esc = true;
        if (esc) out += hex2(c); else out += static_cast<char>(c);
    }
    return out;
}

bool unescape_name(const std::string& s, std::string& out) {
    out.clear();
    auto hv = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') { out += s[i]; continue; }
        if (i + 2 >= s.size() || hv(s[i + 1]) < 0 || hv(s[i + 2]) < 0) return false;
        out += static_cast<char>(hv(s[i + 1]) * 16 + hv(s[i + 2]));
        i += 2;
    }
    return true;
}

namespace {
std::string attr_letters(uint8_t a) {
    std::string s;
    if (a & kAttrRdo) s += 'R';
    if (a & kAttrHid) s += 'H';
    if (a & kAttrSys) s += 'S';
    return s.empty() ? "-" : s;
}
}  // namespace

std::string format_changes(const std::vector<ChangeLine>& lines_in, const std::vector<std::string>& comments) {
    std::vector<ChangeLine> lines = lines_in;
    std::stable_sort(lines.begin(), lines.end(),
                     [](const ChangeLine& a, const ChangeLine& b) { return a.card_path < b.card_path; });
    std::string out = std::string(kMagic) + "\n";
    for (const std::string& c : comments) out += "# " + c + "\n";
    for (const ChangeLine& l : lines) {
        out += l.kind;
        out += '\t';
        out += attr_letters(l.attrs);
        out += '\t';
        out += std::to_string(l.size);
        out += '\t';
        out += escape_name(l.card_path, false);
        out += '\t';
        out += l.host_path.empty() ? std::string("-") : l.host_path;
        out += '\n';
    }
    return out;
}

bool parse_changes(const std::string& text, std::vector<ChangeLine>& lines, std::string& err) {
    lines.clear();
    std::vector<std::string> raw;
    {
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t nl = text.find('\n', pos);
            std::string l = text.substr(pos, nl == std::string::npos ? nl : nl - pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            raw.push_back(std::move(l));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    if (raw.empty() || raw[0] != kMagic) {
        err = "not a jnext changes list (expected '" + std::string(kMagic) + "')";
        return false;
    }
    for (size_t n = 1; n < raw.size(); ++n) {
        const std::string& l = raw[n];
        if (l.empty() || l[0] == '#') continue;
        auto bad = [&](const std::string& why) {
            err = "line " + std::to_string(n + 1) + ": " + why;
            return false;
        };
        std::vector<std::string> f;
        size_t p = 0;
        for (;;) {
            const size_t t = l.find('\t', p);
            f.push_back(l.substr(p, t == std::string::npos ? t : t - p));
            if (t == std::string::npos) break;
            p = t + 1;
        }
        if (f.size() != 5) return bad("expected 5 TAB-separated fields, found " + std::to_string(f.size()));
        ChangeLine c;
        if (f[0].size() != 1 || !std::strchr("ACDR", f[0][0])) return bad("unknown kind '" + f[0] + "'");
        c.kind = f[0][0];
        if (f[1] != "-") {
            uint8_t a = 0;
            for (char ch : f[1]) {
                if (ch == 'R') a |= kAttrRdo; else if (ch == 'H') a |= kAttrHid; else if (ch == 'S') a |= kAttrSys;
                else return bad("bad attribute letters '" + f[1] + "'");
            }
            if (f[1] != attr_letters(a)) return bad("attribute letters '" + f[1] + "' are not in the order R, H, S");
            c.attrs = a;
        }
        if (f[2].empty() || f[2].find_first_not_of("0123456789") != std::string::npos) return bad("size '" + f[2] + "' is not a number");
        c.size = std::strtoull(f[2].c_str(), nullptr, 10);
        if (!unescape_name(f[3], c.card_path)) return bad("bad %XX escape in the card path");
        if (c.card_path.empty() || c.card_path[0] != '/') return bad("the card path must be absolute");
        if (c.kind == 'A' || c.kind == 'C') {
            if (f[4] == "-" || f[4].empty()) return bad("a file line needs a host path");
            if (f[4][0] == '/') return bad("the host path must be relative to the directory");
            size_t s = 0;
            while (s <= f[4].size()) {
                const size_t e = f[4].find('/', s);
                const std::string comp = f[4].substr(s, e == std::string::npos ? e : e - s);
                if (comp == ".." || comp.empty()) return bad("the host path has an empty or '..' component");
                if (e == std::string::npos) break;
                s = e + 1;
            }
            c.host_path = f[4];
        } else if (f[4] != "-") {
            return bad("a directory or removed line has '-' as its host path");
        }
        lines.push_back(std::move(c));
    }
    return true;
}

// ---- export -------------------------------------------------------------------------

FileAddStatus export_changes(const ExportRequest& req, ExportReport& rep, std::string& err) {
    err.clear();
    rep = ExportReport{};
    std::error_code ec;
    // The directory is decided before anything is opened or made.
    if (fs::exists(req.dir, ec)) {
        if (!fs::is_directory(req.dir, ec)) { err = "'" + req.dir + "' is not a directory"; return FileAddStatus::DestInvalid; }
        if (fs::directory_iterator(req.dir, ec) != fs::directory_iterator()) {
            err = "'" + req.dir + "' is not empty; export into a new or empty directory";
            return FileAddStatus::DestInvalid;
        }
    }
    std::string why;
    std::unique_ptr<Volume> view;   // opened AFTER the base: rebuilding the as-made card formats on drive 0

    // ---- the base ----------------------------------------------------------------------
    std::unique_ptr<Tree> base;
    std::string tmp_base;
    const std::string upgrade_dir = (fs::path(req.image).parent_path() / "upgrade-work").string();
    bool made_upgrade_dir = false;
    auto cleanup = [&]() {
        base.reset();
        if (!tmp_base.empty()) { fs::remove(tmp_base, ec); }
        if (made_upgrade_dir) fs::remove(upgrade_dir, ec);   // only an empty directory goes
    };
    if (req.is_default_card && pristine_base_ok(req.image, why)) {
        made_upgrade_dir = !fs::exists(upgrade_dir, ec);
        fs::create_directories(upgrade_dir, ec);
        tmp_base = (fs::path(upgrade_dir) / "export-base.img").string();
        fs::remove(tmp_base, ec);
        const std::string raw = (fs::path(req.image).parent_path() / kImageFileName).string();
        if (!rebuild_as_made(raw, req.image, tmp_base, err)) { tmp_base.clear(); cleanup(); return FileAddStatus::DestInvalid; }
        std::string verr;
        base = Volume::open(1, tmp_base, nullptr, true, verr);
        if (!base) { err = verr; cleanup(); return FileAddStatus::DestInvalid; }
        rep.base = "as-made";
    } else if (!req.base_image.empty()) {
        base = open_tree_lenient(req.base_image, err);
        if (!base) return FileAddStatus::DestInvalid;
        rep.base = "--sdcard-export-base " + req.base_image;
    } else if (req.journal) {
        base = Volume::open(1, req.image, nullptr, true, err);
        if (!base) return FileAddStatus::ImageUnusable;
        rep.base = "card";
    } else {
        err = "nothing to compare against: this card was not made by jnext" +
              (req.is_default_card && !why.empty() ? " in a way that can be rebuilt (" + why + ")" : std::string()) +
              "; give the image it was made from with --sdcard-export-base FILE, or the writes file with --sdcard-writes FILE";
        return FileAddStatus::DestInvalid;
    }

    view = Volume::open(0, req.image, req.journal, true, err);
    if (!view) { cleanup(); return FileAddStatus::ImageUnusable; }
    std::vector<Change> changes;
    if (!compute_changes(*view, *base, changes, err)) { cleanup(); return FileAddStatus::DestInvalid; }

    // ---- write DIR ----------------------------------------------------------------------
    const bool made_dir = !fs::exists(req.dir, ec);
    fs::create_directories(req.dir, ec);
    if (ec) { err = "cannot create '" + req.dir + "'"; cleanup(); return FileAddStatus::DestInvalid; }
    auto fail = [&](const std::string& why2) {
        err = why2;
        if (made_dir) fs::remove_all(req.dir, ec);
        cleanup();
        return FileAddStatus::DestInvalid;
    };
    std::vector<ChangeLine> lines;
    std::vector<uint8_t> buf(kChunk);
    for (const Change& c : changes) {
        ChangeLine l;
        l.card_path = c.entry.path;
        l.attrs = c.entry.attr & kUserAttrMask;
        if (c.kind == Kind::Deleted) {
            l.kind = 'R';
            ++rep.removed;
            lines.push_back(l);
            continue;
        }
        if (c.entry.unaddressable) { rep.skipped.push_back(escape_name(c.entry.path, false)); continue; }
        if (c.entry.dir) {
            l.kind = 'D';
            ++rep.dirs;
            lines.push_back(l);
            continue;
        }
        l.kind = c.kind == Kind::Added ? 'A' : 'C';
        l.size = c.entry.size;
        std::string host;
        size_t s = 1;
        bool escaped = false;
        while (s <= c.entry.path.size()) {
            const size_t e = c.entry.path.find('/', s);
            const std::string comp = c.entry.path.substr(s, e == std::string::npos ? e : e - s);
            const std::string h = escape_name(comp, true);
            if (h != comp) escaped = true;
            host += (host.empty() ? "" : "/") + h;
            if (e == std::string::npos) break;
            s = e + 1;
        }
        // The list itself is DIR/CHANGES.txt (on a host that folds case, any case of the name): a card
        // file of that name at the root is written under an escaped name, never over the list.
        if (host.find('/') == std::string::npos && host.size() == 11) {
            std::string up = host;
            for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (up == "CHANGES.TXT") { host = "%43" + host.substr(1); escaped = true; }
        }
        if (escaped) rep.escaped.push_back(host);
        l.host_path = host;
        const fs::path dest = fs::path(req.dir) / fs::path(host);
        fs::create_directories(dest.parent_path(), ec);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot create '" + dest.string() + "'");
        if (!view->open(c.entry, err)) return fail(err);
        for (;;) {
            size_t got = 0;
            if (!view->read(buf.data(), buf.size(), got, err)) { view->close(); return fail(err); }
            if (!got) break;
            out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(got));
        }
        view->close();
        out.flush();
        if (!out) return fail("cannot write '" + dest.string() + "'");
        (l.kind == 'A' ? rep.added : rep.changed)++;
        lines.push_back(l);
    }
    std::vector<std::string> comments;
    comments.push_back("card: " + req.image);
    if (req.journal) comments.push_back("writes: " + req.journal->file());
    comments.push_back("base: " + rep.base);
    {
        char when[32];
        const std::time_t now = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
        comments.push_back(std::string("made: ") + when);
    }
    {
        std::ofstream ct(fs::path(req.dir) / "CHANGES.txt", std::ios::binary | std::ios::trunc);
        ct << format_changes(lines, comments);
        ct.flush();
        if (!ct) return fail("cannot write CHANGES.txt");
    }
    cleanup();
    return FileAddStatus::Ok;
}

// ---- import -----------------------------------------------------------------------------

FileAddStatus import_changes(const std::string& image, const std::string& dir,
                             ImportReport& rep, std::string& err) {
    err.clear();
    rep = ImportReport{};
    const fs::path ct = fs::path(dir) / "CHANGES.txt";
    std::error_code ec;
    if (!fs::is_regular_file(ct, ec)) {
        err = dir + " is not a directory made by --sdcard-export-changes (no CHANGES.txt)";
        return FileAddStatus::SourceUnreadable;
    }
    std::ifstream in(ct, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<ChangeLine> lines;
    std::string perr;
    if (!parse_changes(text, lines, perr)) {
        err = ct.string() + (perr.compare(0, 4, "not ") == 0
                  ? " is " + perr
                  : " is not a valid changes list: " + perr);
        return FileAddStatus::SourceUnreadable;
    }
    std::vector<ImportItem> items;
    for (const ChangeLine& l : lines) {
        if (l.kind == 'R') continue;   // import never deletes
        std::string fatp, nerr;
        if (!normalize_dest_path(l.card_path, fatp, nerr)) {
            err = nerr + "; edit or remove that line of CHANGES.txt";
            return FileAddStatus::DestInvalid;
        }
        ImportItem it;
        it.card_path = l.card_path;
        it.attrs = l.attrs;
        it.is_dir = l.kind == 'D';
        if (!it.is_dir) {
            it.host_file = (fs::path(dir) / fs::path(l.host_path)).string();
            it.size = l.size;
        }
        items.push_back(std::move(it));
    }
    ImportSummary sum;
    const FileAddStatus st = import_items_to_image(image, items, err, &sum);
    if (st != FileAddStatus::Ok) return st;
    rep.written = std::move(sum.written);
    rep.replaced = std::move(sum.replaced);
    rep.dirs_created = sum.dirs_created;
    rep.bytes = sum.bytes;
    return FileAddStatus::Ok;
}

}  // namespace sdcard::carry
