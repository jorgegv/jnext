#include "core/sdcard_file_extract.h"

#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "peripheral/sd_write_journal.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace sdcard {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = 64 * 1024;

std::string fr_text(FRESULT r) {
    switch (r) {
    case FR_OK: return "ok";
    case FR_DISK_ERR: return "disk error";
    case FR_INT_ERR: return "damaged FAT structure";
    case FR_NOT_READY: return "not ready";
    case FR_NO_FILE: return "no such file";
    case FR_NO_PATH: return "no such path";
    case FR_INVALID_NAME: return "invalid name";
    case FR_NO_FILESYSTEM: return "no FAT filesystem";
    default: return "FatFs error " + std::to_string(static_cast<int>(r));
    }
}

// A name from the card, as it may be shown: bytes outside printable ASCII (an ESC in a long name) and '%' as %XX.
std::string shown_name(const std::string& s) { return carry::escape_name(s, false); }

bool printable_ascii(unsigned char c) { return c >= 0x20 && c <= 0x7E; }

// The card path, split on '/' and '\' (a FAT name holds neither), empty components dropped, so
// "/a/b", "a/b", "\a\b" and "//a//b" agree. Refuses what FatFs would read as something other than
// the name typed: ".", "..", a trailing dot or space (it strips them, so "/FILE." would open
// "/FILE"), and bytes outside printable ASCII (the API strings are CP437 bytes, which a UTF-8
// argument cannot match). Everything else is FatFs's to judge.
bool parse_card_path(const std::string& path, std::vector<std::string>& comps, std::string& err, bool bytes) {
    comps.clear();
    std::string cur;
    auto flush = [&] { if (!cur.empty()) { comps.push_back(cur); cur.clear(); } };
    for (char c : path) {
        if (c == '/' || c == '\\') flush(); else cur.push_back(c);
    }
    flush();
    if (comps.empty()) {
        err = "'" + path + "' names no file or directory on the card; give the path of one, e.g. /DIR/NAME";
        return false;
    }
    for (const std::string& c : comps) {
        if (c == "." || c == "..") {
            err = "card path '" + path + "' contains '" + c + "'; give the full path from the card root";
            return false;
        }
        for (char ch : c) {
            const unsigned char u = static_cast<unsigned char>(ch);
            // `bytes`: the caller holds the path as CP437 bytes it took from a listing (the SD card file manager), which
            // FatFs matches natively; only control bytes are refused. The CLI's strings are UTF-8, so it refuses all.
            if (bytes ? (u < 0x20 || u == 0x7F) : !printable_ascii(u)) {
                err = "card path '" + path + "' has a character outside printable ASCII in '" + c +
                      "'; FatFs matches CP437 bytes, so use the 8.3 name (ls shows it) or extract the "
                      "parent directory";
                return false;
            }
        }
        if (c.back() == '.' || c.back() == ' ') {
            err = "card path component '" + c + "' ends in a '.' or a space, which FAT strips; the "
                  "entry asked for could not be told from another";
            return false;
        }
    }
    return true;
}

// Can FatFs open an entry by this name? It cannot when the name ends in a dot or a space (it drops
// those, so the path would name another entry), holds a control byte (it ends the name there) or
// one of "*:<>?|, '/', '\' or DEL (invalid), or is longer than a long name can be.
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

// The image on FatFs drive 0, mounted read-only, for as long as the object lives. Unmount, then
// detach, on every return path. Nothing here modifies the volume, so nothing is flushed.
struct MountedCard {
    FATFS fs{};
    bool  attached = false;
    bool  mounted  = false;
    ~MountedCard() {
        if (mounted) f_mount(nullptr, "0:", 0);
        if (attached) fatfs_glue::detach(0);
    }
};

struct PlanItem {
    std::string              fat;      // "0:/..." as FatFs is to be asked
    std::vector<std::string> rel;      // host path components below the top entry (escaped)
    bool                     dir = false;
};

std::string join_slash(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) { if (!out.empty()) out += '/'; out += s; }
    return out;
}

}  // namespace

FileAddStatus extract_from_image(const std::string& image_path, const std::string& card_path,
                                 const std::string& dest_dir, std::string& err,
                                 ExtractSummary* summary, bool card_path_is_bytes) {
    using St = FileAddStatus;
    err.clear();

    // 1. the card path (refused before the card is opened)
    std::vector<std::string> comps;
    if (!parse_card_path(card_path, comps, err, card_path_is_bytes)) return St::SourceUnreadable;

    // 2. the volume, as the firmware finds it
    uint32_t lba = 0, sectors = 0;
    std::string why;
    if (!find_sd_fat_volume(image_path, lba, sectors, why)) {
        err = why;
        return St::ImageUnusable;
    }

    // 3. the card as a run with the same options would see it: the writes file laid over the image
    auto journal = sd_write_journal::for_image(image_path);
    if (journal) {
        // The geometry above came from the raw image. A journal that rewrote the partition table or
        // the boot sector means the card the machine sees is not that one.
        uint8_t sec[512];
        if (journal->lookup(0, sec) || journal->lookup(lba, sec)) {
            err = "the writes file '" + journal->file() +
                  "' holds a rewritten partition table or boot sector for '" + image_path +
                  "'; --sdcard-file-extract cannot read it (--sdcard-writes-reset starts again from the image)";
            return St::ImageUnusable;
        }
    }

    // 4. mount, read-only
    MountedCard card;
    {
        std::string glue_err;
        if (!fatfs_glue::attach(0, image_path, lba, sectors, glue_err, journal, /*read_only=*/true)) {
            err = "cannot open SD image '" + image_path + "' for reading (" + glue_err + ")";
            return St::ImageUnusable;
        }
        card.attached = true;
        const FRESULT fr = f_mount(&card.fs, "0:", 1);
        if (fr != FR_OK) {
            // FatFs keeps the pointer of the FATFS it was handed even when the mount fails: unregister it.
            f_mount(nullptr, "0:", 0);
            err = "'" + image_path + "' does not hold a FAT filesystem this build can mount (" +
                  fr_text(fr) + "). An under-clustered FAT32 image (< 65525 clusters) is rejected by "
                  "the Next's own firmware too; tools/fix-sdcard-image.sh re-clusters one";
            return St::ImageUnusable;
        }
        card.mounted = true;
    }

    // 5. the entry: each prefix is stat'ed, so the stored spelling of every component is known. f_stat
    // alone cannot give it: a name matched through its 8.3 form comes back without its long name
    // (ff.c dir_find resets the LFN when the short name matched), so the parent is listed for the
    // entry FatFs found, which is the first one whose long or 8.3 name equals the component.
    std::string typed = "0:";
    std::string stored;                       // '/'-separated stored names
    FILINFO top{};
    for (size_t i = 0; i < comps.size(); ++i) {
        const std::string parent = typed;
        typed += "/" + comps[i];
        FILINFO fno{};
        const FRESULT fr = f_stat(typed.c_str(), &fno);
        if (fr == FR_NO_FILE || fr == FR_NO_PATH || fr == FR_INVALID_NAME) {
            err = std::string("'") + card_path + "' is " +
                  (fr == FR_INVALID_NAME ? "not a valid name on the card" : "not on the card") +
                  " (no " + (i + 1 < comps.size() ? "directory '" : "entry '") + comps[i] + "')";
            return St::SourceUnreadable;
        }
        if (fr != FR_OK) {
            err = "cannot read '" + typed.substr(2) + "' on the card (" + fr_text(fr) + ")";
            return St::ImageUnusable;
        }
        {
            const auto stat_sect = fno.dir_sect;   // the directory slot f_stat resolved the typed name to
            const auto stat_ofs = fno.dir_ofs;
            DIR dj{};
            if (f_opendir(&dj, parent == "0:" ? "0:/" : parent.c_str()) == FR_OK) {
                const std::string want = fold(comps[i]);
                for (;;) {
                    FILINFO e{};
                    if (f_readdir(&dj, &e) != FR_OK || e.fname[0] == 0) break;
                    if (fold(e.fname) == want || fold(e.altname) == want) {
                        fno = e;
                        // Bytes mode: the entry the LISTING names must be the entry FatFs opens. A name whose 8.3
                        // conversion collides with an earlier entry's resolves to THAT entry (and its bytes would be
                        // written under this one's name): refuse it.
                        if (card_path_is_bytes && (e.dir_sect != stat_sect || e.dir_ofs != stat_ofs)) {
                            f_closedir(&dj);
                            err = "'" + shown_name(card_path) + "' names an entry FatFs resolves to a different one (a duplicate 8.3 "
                                  "name in the directory); it is not extracted";
                            return St::SourceUnreadable;
                        }
                        break;
                    }
                }
                f_closedir(&dj);
            }
        }
        stored += "/";
        stored += shown_name(fno.fname);
        if (i + 1 < comps.size() && !(fno.fattrib & AM_DIR)) {
            err = "'" + card_path + "' is not on the card ('" + comps[i] + "' is a file, not a directory)";
            return St::SourceUnreadable;
        }
        if (i + 1 == comps.size()) top = fno;
    }
    const bool top_is_dir = (top.fattrib & AM_DIR) != 0;
    const std::string top_name = top.fname;
    const std::string top_host = carry::escape_name(top_name, true);

    std::error_code ec;
    if (!fs::is_directory(fs::path(dest_dir), ec)) {
        err = "cannot extract into '" + dest_dir + "': not a directory";
        return St::DestInvalid;
    }
    const fs::path target = fs::path(dest_dir) / top_host;
    {
        std::error_code sec;
        const fs::file_status st = fs::symlink_status(target, sec);
        if (st.type() != fs::file_type::not_found) {
            if (sec) {
                err = "cannot examine '" + target.string() + "' (" + sec.message() + ")";
                return St::DestInvalid;
            }
            err = "'" + target.string() + "' already exists; it is left as it is (move it away first)";
            return St::DestExists;
        }
    }

    // 6. pass 1: the plan. Only directories are read here; nothing is created.
    std::vector<PlanItem> plan;
    PlanItem first;
    first.fat = typed;
    first.dir = top_is_dir;
    plan.push_back(first);
    std::vector<std::string> escaped;
    if (top_host != top_name) escaped.push_back(top_host);

    if (top_is_dir) {
        std::set<DWORD> visited;               // start clusters of the directories read (the root is 0)
        struct Pending { std::string fat; std::vector<std::string> rel; std::string shown; };
        std::vector<Pending> stack{{typed, {}, stored}};
        while (!stack.empty()) {
            const Pending cur = stack.back();
            stack.pop_back();
            DIR dj{};
            FRESULT fr = f_opendir(&dj, cur.fat.c_str());
            if (fr != FR_OK) {
                err = "cannot read directory '" + cur.shown + "' on the card (" + fr_text(fr) + ")";
                return St::ImageUnusable;
            }
            if (!visited.insert(dj.obj.sclust).second) {
                f_closedir(&dj);
                err = "directory '" + cur.shown + "' on the card is reached twice (a damaged "
                      "directory structure: a directory that points at itself, its parent or the root)";
                return St::ImageUnusable;
            }
            std::set<std::string> seen;
            for (;;) {
                FILINFO fno{};
                fr = f_readdir(&dj, &fno);
                if (fr != FR_OK) {
                    f_closedir(&dj);
                    err = "cannot read directory '" + cur.shown + "' on the card (" + fr_text(fr) + ")";
                    return St::ImageUnusable;
                }
                if (fno.fname[0] == 0) break;
                const std::string lname = fno.fname;
                const std::string aname = fno.altname;
                const std::string shown = cur.shown + "/" + shown_name(lname);
                // How FatFs is to be asked for it: by the long name, or, when that is a name it
                // cannot open (a trailing dot or space, a control byte, a '?'), by the 8.3 alias.
                std::string comp = lname;
                if (!fatfs_can_open_by(lname)) {
                    if (aname.empty() || !fatfs_can_open_by(aname)) {
                        f_closedir(&dj);
                        err = "'" + shown + "' on the card has a name FatFs cannot open by its long or "
                              "its 8.3 name; it cannot be copied";
                        return St::ImageUnusable;
                    }
                    comp = aname;
                }
                const std::string esc = carry::escape_name(lname, true);
                if (!seen.insert(fold(esc)).second) {
                    f_closedir(&dj);
                    err = "directory '" + cur.shown + "' on the card holds two entries the host cannot "
                          "tell apart ('" + shown_name(lname) + "'); it is a damaged directory";
                    return St::ImageUnusable;
                }
                PlanItem it;
                it.fat = cur.fat + "/" + comp;
                it.rel = cur.rel;
                it.rel.push_back(esc);
                it.dir = (fno.fattrib & AM_DIR) != 0;
                if (esc != lname) escaped.push_back(join_slash(it.rel));
                if (it.dir) stack.push_back({it.fat, it.rel, shown});
                plan.push_back(std::move(it));
            }
            f_closedir(&dj);
        }
    }

    // 7. pass 2: create and stream. Whatever fails removes what this run created.
    auto rollback = [&] { std::error_code rec; fs::remove_all(target, rec); };
    auto host_of = [&](const PlanItem& it) {
        fs::path p = target;
        for (const std::string& c : it.rel) p /= c;
        return p;
    };
    uint64_t bytes = 0;
    unsigned files = 0, dirs = 0;
    std::vector<uint8_t> buf(kChunk);
    for (const PlanItem& it : plan) {
        const fs::path hp = host_of(it);
        if (it.dir) {
            std::error_code cec;
            fs::create_directory(hp, cec);
            if (cec) {
                err = "cannot create directory '" + hp.string() + "' (" + cec.message() + ")";
                rollback();
                return St::DestInvalid;
            }
            ++dirs;
            continue;
        }
        FIL fp{};
        FRESULT fr = f_open(&fp, it.fat.c_str(), FA_READ);
        if (fr != FR_OK) {
            err = "cannot read '" + shown_name(it.fat.substr(2)) + "' on the card (" + fr_text(fr) + ")";
            rollback();
            return St::ImageUnusable;
        }
        std::ofstream out(hp, std::ios::binary | std::ios::trunc);
        if (!out) {
            f_close(&fp);
            err = "cannot create '" + hp.string() + "'";
            rollback();
            return St::DestInvalid;
        }
        uint64_t got_total = 0;
        for (;;) {
            UINT br = 0;
            fr = f_read(&fp, buf.data(), static_cast<UINT>(buf.size()), &br);
            if (fr != FR_OK) {
                f_close(&fp);
                out.close();
                err = "a read of '" + shown_name(it.fat.substr(2)) + "' on the card failed (" + fr_text(fr) + ")";
                rollback();
                return St::ImageUnusable;
            }
            if (br == 0) break;
            out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(br));
            if (!out) {
                f_close(&fp);
                out.close();
                err = "cannot write '" + hp.string() + "' (disk full?)";
                rollback();
                return St::DestInvalid;
            }
            got_total += br;
        }
        f_close(&fp);
        out.flush();
        const bool wrote = static_cast<bool>(out);
        out.close();
        if (!wrote) {
            err = "cannot write '" + hp.string() + "' (disk full?)";
            rollback();
            return St::DestInvalid;
        }
        ++files;
        bytes += got_total;
    }

    if (summary) {
        summary->is_dir = top_is_dir;
        summary->card_path = stored;
        summary->host_path = target.string();
        summary->files = files;
        summary->dirs = dirs;
        summary->bytes = bytes;
        summary->escaped = std::move(escaped);
        std::error_code jec;
        summary->through_writes = journal && fs::exists(journal->file(), jec);
    }
    return St::Ok;
}

}  // namespace sdcard
