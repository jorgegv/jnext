#include "core/minimal_card.h"

#include "core/fat32_image.h"
#include "core/sha256_stream.h"

#include <zlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace minimal_card {

namespace {

const char* const kBootSet[] = {"TBBLUE.FW", "MACHINES", "NEXTZXOS", "SYS", "DOT", "LICENSE.md", "README.md"};
constexpr uint64_t kFatCap    = 16ull << 20;   // the FAT copy
constexpr uint64_t kRetainCap = 32ull << 20;   // the boot set's data and directories
constexpr size_t   kOutStep   = 64 * 1024;     // inflate output step
constexpr uint32_t kEocMin    = 0x0FFFFFF8u;
constexpr uint32_t kSerial    = 0x4A4E5854u;   // "JNXT", the volume serial of the card
constexpr uint32_t kCardPartLba = 63;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void wr32(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
                                    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24); }

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool in_boot_set(const std::string& name) {
    for (const char* b : kBootSet)
        if (ieq(name, b)) return true;
    return false;
}

std::string lower_of(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string basename_of(const std::string& n) {
    const size_t s = n.find_last_of("/\\");
    return s == std::string::npos ? n : n.substr(s + 1);
}

// The CHS bytes of an LBA with 255 heads and 63 sectors per track (what sfdisk writes);
// 0xFE/0xFF/0xFF above cylinder 1023.
void chs(uint32_t lba, uint8_t out[3]) {
    const uint32_t cyl = lba / (255u * 63u);
    if (cyl > 1023) { out[0] = 0xFE; out[1] = 0xFF; out[2] = 0xFF; return; }
    out[0] = static_cast<uint8_t>((lba / 63u) % 255u);
    out[1] = static_cast<uint8_t>(((lba % 63u) + 1u) | ((cyl >> 2) & 0xC0u));
    out[2] = static_cast<uint8_t>(cyl & 0xFFu);
}

// Create `path` for writing, EXCLUSIVELY: any existing entry (a file, a dangling symlink) is
// refused with EEXIST, with no race between a check and the create. C11 "x" does it where the C
// library has it; MinGW's msvcrt ignores the "x" and would truncate the file (GH #458), so
// Windows asks _open for O_EXCL.
std::FILE* create_exclusive(const std::string& path) {
#ifdef _WIN32
    const int fd = _open(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return nullptr;
    std::FILE* f = _fdopen(fd, "wb");
    if (!f) _close(fd);
    return f;
#else
    return std::fopen(path.c_str(), "wbx");
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// The image stage: the SD image as a stream, by absolute offset.
// ---------------------------------------------------------------------------
namespace {

struct Rec {
    std::string name, path;
    bool        is_dir = false;
    uint8_t     attr = 0;
    uint16_t    wdate = 0, wtime = 0;
    uint32_t    size = 0;
    int         chain = -1;
    bool        partial = false;   // GH #509: the root or an ancestor of a requested path: only some kids are kept
    std::vector<int> kids;
};

struct Chain {
    std::vector<uint32_t> clusters;
    size_t                got = 0;
    std::vector<uint8_t>  buf;
    int                   rec = 0;   // the owner (0 = the root directory)
    bool                  is_dir = false;
    size_t                bytes = 0; // what is kept: a file's size, a directory's clusters (GH #509)
};

class ImageStage {
public:
    // The canonical (lower case, sorted) requested paths, before the first byte.
    void set_dirs(const std::vector<std::string>& d) { dirs_ = d; }
    // The retained bytes passed the cap: the total counted so far (a lower bound until done).
    bool over() const { return over_; }
    std::string over_message() const {
        return std::string(dirs_.empty() ? "the boot set" : "the boot set and the requested paths") +
               " is larger than the card: " + (done_over_ ? "" : "at least ") + std::to_string(retained_) +
               " bytes, the limit is " + std::to_string(kRetainCap) + " (32 MiB)";
    }
    bool failed() const { return !err_.empty(); }
    const std::string& error() const { return err_; }
    bool done() const { return done_; }
    uint64_t pos() const { return pos_; }

    // Feed inflated (or raw) image bytes, in order. Ignored once done or failed.
    void consume(const uint8_t* p, size_t n) {
        while (n && !failed() && !done_) {
            size_t take;
            if (pos_ < 512) {
                take = std::min<size_t>(n, 512 - pos_);
                std::memcpy(hdr_ + pos_, p, take);
                advance(take);
                if (pos_ == 512) parse_mbr();
            } else if (pos_ < part_off_) {
                take = static_cast<size_t>(std::min<uint64_t>(n, part_off_ - pos_));
                advance(take);
            } else if (pos_ < part_off_ + 512) {
                take = static_cast<size_t>(std::min<uint64_t>(n, part_off_ + 512 - pos_));
                std::memcpy(hdr_ + (pos_ - part_off_), p, take);
                advance(take);
                if (pos_ == part_off_ + 512) parse_bpb();
            } else if (pos_ < fat_off_) {
                take = static_cast<size_t>(std::min<uint64_t>(n, fat_off_ - pos_));
                advance(take);
            } else if (pos_ < fat_end_) {
                take = static_cast<size_t>(std::min<uint64_t>(n, fat_end_ - pos_));
                std::memcpy(fat_.data() + (pos_ - fat_off_), p, take);
                advance(take);
            } else if (pos_ < data_off_) {
                take = static_cast<size_t>(std::min<uint64_t>(n, data_off_ - pos_));
                advance(take);
            } else {
                if (!data_started_) { data_started_ = true; start_data(); if (failed() || done_) return; }
                take = data(p, n);
            }
            p += take;
            n -= take;
        }
    }

    // The boot set as a tree, after done(). Moves the data out.
    bool build_tree(Fat32Tree& out, std::string& why) {
        for (const char* b : kBootSet) {
            bool found = false;
            for (int k : recs_[0].kids)
                if (ieq(recs_[static_cast<size_t>(k)].name, b)) { found = true; break; }
            if (!found) { why = std::string(b) + " is not in the distribution's SD image"; return false; }
        }
        std::string missing;
        for (const std::string& d : dirs_)
            if (!found_.count(d)) missing += (missing.empty() ? "" : ", ") + d;
        if (!missing.empty()) {
            why = std::string(std::count(missing.begin(), missing.end(), ',') ? "the requested paths " : "the requested path ") +
                  missing + (std::count(missing.begin(), missing.end(), ',') ? " are" : " is") + " not in the distribution";
            return false;
        }
        out.root.clear();
        for (int k : recs_[0].kids) out.root.push_back(node_of(k));
        return true;
    }

private:
    void advance(size_t n) { pos_ += n; }
    void fail(const std::string& m) { if (err_.empty()) err_ = m; }

    void parse_mbr() {
        if (hdr_[510] != 0x55 || hdr_[511] != 0xAA) { fail("the SD image has no partition table (no 0x55AA at byte 510)"); return; }
        for (int i = 0; i < 4; ++i) {
            const uint8_t* pe = hdr_ + 0x1BE + i * 16;
            const uint32_t lba = rd32(pe + 8);
            if ((pe[4] == 0x0B || pe[4] == 0x0C) && lba > 0) { part_lba_ = lba; break; }
        }
        if (!part_lba_) { fail("the SD image has no FAT32 partition"); return; }
        part_off_ = static_cast<uint64_t>(part_lba_) * 512;
    }

    void parse_bpb() {
        const uint16_t bps = rd16(hdr_ + 11);
        spc_ = hdr_[13];
        const uint16_t reserved = rd16(hdr_ + 14);
        const uint8_t  nfats = hdr_[16];
        fat_sectors_ = rd32(hdr_ + 36);
        root_cluster_ = rd32(hdr_ + 44);
        total_sectors_ = rd32(hdr_ + 32);
        if (!total_sectors_) total_sectors_ = rd16(hdr_ + 19);
        if (bps != 512 || spc_ == 0 || nfats == 0 || fat_sectors_ == 0 || root_cluster_ < 2 || reserved == 0) {
            fail("the SD image's FAT32 geometry is not supported (512-byte sectors and a FAT32 BPB are required)");
            return;
        }
        if (static_cast<uint64_t>(fat_sectors_) * 512 > kFatCap) { fail("the SD image's FAT is larger than 16 MiB"); return; }
        bpc_ = static_cast<uint32_t>(spc_) * 512;
        fat_off_ = part_off_ + static_cast<uint64_t>(reserved) * 512;
        fat_end_ = fat_off_ + static_cast<uint64_t>(fat_sectors_) * 512;
        data_off_ = fat_end_ + static_cast<uint64_t>(nfats - 1) * fat_sectors_ * 512;
        fat_.assign(static_cast<size_t>(fat_sectors_) * 512, 0);
        const uint64_t data_sectors = total_sectors_ > (data_off_ - part_off_) / 512
                                          ? total_sectors_ - (data_off_ - part_off_) / 512 : 0;
        n_clusters_ = static_cast<uint32_t>(data_sectors / spc_);
    }

    // The cluster chain of `first` from FAT #1. `limit` clusters at most (0 = to its end).
    bool walk(uint32_t first, size_t limit, const std::string& path, std::vector<uint32_t>& out) {
        uint32_t c = first;
        const size_t fat_entries = fat_.size() / 4;
        while (true) {
            if (c < 2 || c >= fat_entries || (n_clusters_ && c > n_clusters_ + 1)) {
                fail("a cluster chain of " + path + " leaves the volume (cluster " + std::to_string(c) + ")");
                return false;
            }
            out.push_back(c);
            if (out.size() > fat_entries) { fail("a cluster chain of " + path + " loops"); return false; }
            if (limit && out.size() == limit) return true;
            const uint32_t nx = rd32(fat_.data() + 4 * static_cast<size_t>(c)) & 0x0FFFFFFFu;
            if (nx >= kEocMin) {
                if (limit) { fail(path + " is shorter on disk than its size says"); return false; }
                return true;
            }
            c = nx;
        }
    }

    // `bytes` is what the card keeps of the chain: a file's size, a directory's clusters. The
    // source's clusters (32 KiB in the distribution) are not what is held, so they are not
    // what is counted (GH #509). Past the cap the total is still counted, but files are no
    // longer registered (no buffer), so the refusal can name the exact size.
    void register_chain(int rec, const std::vector<uint32_t>& cl, bool is_dir, uint32_t cur,
                        const std::string& path, uint64_t bytes) {
        retained_ += bytes;
        if (retained_ > kRetainCap) over_ = true;
        if (over_ && !is_dir) return;
        // Past the cap the directories are still read (to count the rest), but what is HELD at
        // once, the files buffered before the crossing plus the directories not yet parsed, may
        // not pass the cap: refused before the buffer exists (the distribution's directories are
        // about 3 MB in all).
        if (over_ && held_ + bytes > kRetainCap) { fail(over_message()); return; }
        held_ += bytes;
        for (uint32_t c : cl) {
            if (c <= cur) {
                fail("the distribution's layout needs a second pass over the zip, which this builder does not do (cluster " +
                     std::to_string(c) + " of " + (path.empty() ? "/" : path) + " comes before its directory)");
                return;
            }
            if (owner_.count(c)) { fail("cluster " + std::to_string(c) + " of " + path + " is shared with another file"); return; }
        }
        Chain ch;
        ch.clusters = cl;
        ch.buf.assign(static_cast<size_t>(bytes), 0);
        ch.bytes = static_cast<size_t>(bytes);
        ch.rec = rec;
        ch.is_dir = is_dir;
        chains_.push_back(std::move(ch));
        const size_t ci = chains_.size() - 1;
        for (size_t i = 0; i < cl.size(); ++i) owner_[cl[i]] = {ci, i};
        outstanding_ += cl.size();
        if (rec >= 0) recs_[static_cast<size_t>(rec)].chain = static_cast<int>(ci);
    }

    void start_data() {
        recs_.push_back(Rec{});   // 0 = the root directory
        recs_[0].is_dir = true;
        std::vector<uint32_t> cl;
        if (!walk(root_cluster_, 0, "the root directory", cl)) return;
        recs_[0].partial = true;
        register_chain(0, cl, true, 0, "", static_cast<uint64_t>(cl.size()) * bpc_);
    }

    size_t data(const uint8_t* p, size_t n) {
        const uint64_t rel = pos_ - data_off_;
        const uint32_t c = 2 + static_cast<uint32_t>(rel / bpc_);
        const size_t off = static_cast<size_t>(rel % bpc_);
        const size_t take = std::min<size_t>(n, bpc_ - off);
        auto it = owner_.find(c);
        if (it != owner_.end()) {
            Chain& ch = chains_[it->second.first];
            const size_t dest = it->second.second * bpc_ + off;
            if (dest < ch.bytes) std::memcpy(ch.buf.data() + dest, p, std::min(take, ch.bytes - dest));   // the last cluster is clamped
        }
        advance(take);
        if (off + take == bpc_) {
            if (it != owner_.end()) {
                const size_t ci = it->second.first;
                owner_.erase(it);
                --outstanding_;
                if (++chains_[ci].got == chains_[ci].clusters.size() && chains_[ci].is_dir) dir_complete(ci, c);
            }
            if (!failed() && outstanding_ == 0) {
                if (over_) { done_over_ = true; fail(over_message()); }
                else done_ = true;
            }
        }
        return take;
    }

    void dir_complete(size_t ci, uint32_t cur) {
        const int parent = chains_[ci].rec;
        held_ -= chains_[ci].bytes;
        std::vector<Fat32Entry> ents = fat32_parse_dir(chains_[ci].buf);
        std::vector<uint8_t>().swap(chains_[ci].buf);
        const std::string pdir = recs_[static_cast<size_t>(parent)].path;
        for (const Fat32Entry& e : ents) {
            if (failed()) return;
            bool partial = false;
            if (recs_[static_cast<size_t>(parent)].partial) {
                // The root keeps the boot set; every partial directory keeps the requested
                // paths and the directories on the way to one (GH #509).
                const std::string lp = lower_of(pdir) + "/" + lower_of(e.name);
                bool keep = parent == 0 && in_boot_set(e.name);
                if (!keep) {
                    for (const std::string& d : dirs_) {
                        if (d == lp) { keep = true; found_.insert(d); break; }
                        if (e.is_dir && d.size() > lp.size() && d.compare(0, lp.size(), lp) == 0 && d[lp.size()] == '/') {
                            keep = true;
                            partial = true;
                            break;
                        }
                    }
                }
                if (!keep) continue;
                bool dup = false;
                for (int k : recs_[static_cast<size_t>(parent)].kids)
                    if (ieq(recs_[static_cast<size_t>(k)].name, e.name)) dup = true;
                if (dup) continue;   // the first one wins, as the old script's `head -n1`
            }
            Rec r;
            r.partial = partial;
            r.name = e.name;
            r.path = pdir + "/" + e.name;
            r.is_dir = e.is_dir;
            r.attr = e.attr;
            r.wdate = e.wdate;
            r.wtime = e.wtime;
            r.size = e.size;
            recs_.push_back(r);
            const int ri = static_cast<int>(recs_.size() - 1);
            recs_[static_cast<size_t>(parent)].kids.push_back(ri);
            std::vector<uint32_t> cl;
            uint64_t kept = 0;
            if (e.is_dir) {
                if (e.cluster < 2) continue;   // an empty directory
                if (!walk(e.cluster, 0, r.path, cl)) return;
                kept = static_cast<uint64_t>(cl.size()) * bpc_;
            } else if (e.size > 0) {
                if (e.cluster < 2) { fail(r.path + " has data but no clusters"); return; }
                if (!walk(e.cluster, (e.size + bpc_ - 1) / bpc_, r.path, cl)) return;
                kept = e.size;
            } else {
                continue;
            }
            register_chain(ri, cl, e.is_dir, cur, r.path, kept);
        }
    }

    Fat32Node node_of(int ri) {
        Rec& r = recs_[static_cast<size_t>(ri)];
        Fat32Node n;
        n.name = r.name;
        n.is_dir = r.is_dir;
        n.attr = r.attr;
        n.wdate = r.wdate;
        n.wtime = r.wtime;
        if (r.is_dir) {
            for (int k : r.kids) n.children.push_back(node_of(k));
        } else if (r.chain >= 0) {
            n.data = std::move(chains_[static_cast<size_t>(r.chain)].buf);
            n.data.resize(r.size);
        }
        return n;
    }

    std::string err_;
    bool done_ = false, data_started_ = false;
    uint64_t pos_ = 0;
    uint8_t hdr_[512];
    uint32_t part_lba_ = 0;
    uint64_t part_off_ = 1ull << 60, fat_off_ = 1ull << 60, fat_end_ = 1ull << 60, data_off_ = 1ull << 60;
    uint8_t spc_ = 0;
    uint32_t bpc_ = 0, fat_sectors_ = 0, root_cluster_ = 0, total_sectors_ = 0, n_clusters_ = 0;
    std::vector<uint8_t> fat_;
    std::vector<Rec> recs_;
    std::vector<Chain> chains_;
    std::map<uint32_t, std::pair<size_t, size_t>> owner_;   // cluster -> (chain, index in chain)
    size_t outstanding_ = 0;
    uint64_t retained_ = 0;
    bool over_ = false, done_over_ = false;
    uint64_t held_ = 0;   // bytes buffered now: kept files, and directories not yet parsed
    std::vector<std::string> dirs_;
    std::set<std::string> found_;
};

}  // namespace

// ---------------------------------------------------------------------------
// The builder
// ---------------------------------------------------------------------------
struct Builder::Impl {
    Input in;
    std::string out;
    Expect ex;
    sdcard::Sha256Stream sha;
    ImageStage img;
    std::string err, card_sha;
    uint64_t bytes_in = 0;
    bool finished = false;

    // zip layer
    enum class Z { Header, Name, Image, Skip, Tail } z = Z::Header;
    uint8_t hdr[30];
    size_t hdr_len = 0;
    std::vector<uint8_t> name_extra;
    size_t need_name = 0, nlen = 0;
    uint64_t remaining = 0;
    uint32_t cur_method = 0;
    bool first_entry = true, inflating = false;
    z_stream zs{};
    std::vector<uint8_t> outbuf = std::vector<uint8_t>(kOutStep);

    ~Impl() { if (inflating) inflateEnd(&zs); }

    void fail(const std::string& m) { if (err.empty()) err = m; }

    Status status() const {
        if (!err.empty()) return Status::Error;
        return img.done() ? Status::InflateDone : Status::NeedMore;
    }

    void image_data(const uint8_t* p, size_t n) {
        if (cur_method == 0) { img.consume(p, n); return; }
        zs.next_in = const_cast<uint8_t*>(p);
        zs.avail_in = static_cast<uInt>(n);
        for (;;) {
            zs.next_out = outbuf.data();
            zs.avail_out = static_cast<uInt>(outbuf.size());
            const int r = inflate(&zs, Z_NO_FLUSH);
            const size_t produced = outbuf.size() - zs.avail_out;
            if (produced) img.consume(outbuf.data(), produced);
            if (img.failed()) { fail(img.error()); return; }
            if (r == Z_STREAM_END) { inflate_ended(); return; }
            if (r != Z_OK && r != Z_BUF_ERROR) { fail("the zip's SD image entry is corrupt (inflate failed)"); return; }
            if (img.done()) return;
            if (zs.avail_out != 0 && zs.avail_in == 0) return;
        }
    }

    void inflate_ended() {
        if (!img.done() && err.empty()) fail("the zip's SD image ends before the boot set is complete (truncated)");
    }

    void entry_header() {
        const uint32_t sig = rd32(hdr);
        if (sig != 0x04034b50u) {
            if (first_entry) fail("not a zip file");
            else if (sig == 0x02014b50u || sig == 0x06054b50u) fail("the zip holds no cspect-next-1gb.img");
            else fail("the zip is corrupt (bad entry header)");
            return;
        }
        const uint16_t flags = rd16(hdr + 6);
        const uint16_t method = rd16(hdr + 8);
        const uint32_t csize = rd32(hdr + 18);
        nlen = rd16(hdr + 26);
        const size_t elen = rd16(hdr + 28);
        if (flags & 1) { fail("the zip's entry is encrypted"); return; }
        if (flags & 8) { fail("the zip's entry has a data descriptor (its sizes are not in the header): not supported"); return; }
        if (csize == 0xFFFFFFFFu || rd32(hdr + 22) == 0xFFFFFFFFu) { fail("a ZIP64 zip is not supported"); return; }
        cur_method = method;
        remaining = csize;
        need_name = nlen + elen;
        name_extra.clear();
        first_entry = false;
        z = Z::Name;
        hdr_len = 0;
    }

    void entry_named() {
        const std::string name(reinterpret_cast<const char*>(name_extra.data()), nlen);
        if (ieq(basename_of(name), sdcard::kImageFileName)) {
            if (cur_method != 0 && cur_method != 8) {
                fail("the zip's SD image uses compression method " + std::to_string(cur_method) +
                     " (only stored and deflate are supported)");
                return;
            }
            if (cur_method == 8) {
                if (inflateInit2(&zs, -15) != Z_OK) { fail("inflateInit2 failed"); return; }
                inflating = true;
            }
            z = Z::Image;
            if (remaining == 0) { inflate_ended(); z = Z::Tail; }
        } else {
            z = remaining ? Z::Skip : Z::Header;
        }
    }

    Status feed(const uint8_t* p, size_t n) {
        sha.update(p, n);
        bytes_in += n;
        if (!err.empty()) return Status::Error;
        if (in == Input::Image) {
            img.consume(p, n);
            if (img.failed()) fail(img.error());
            return status();
        }
        while (n && err.empty()) {
            switch (z) {
            case Z::Tail:
                return status();
            case Z::Header: {
                const size_t take = std::min(n, sizeof hdr - hdr_len);
                std::memcpy(hdr + hdr_len, p, take);
                hdr_len += take; p += take; n -= take;
                if (hdr_len >= 4 && rd32(hdr) != 0x04034b50u) { entry_header(); break; }
                if (hdr_len == sizeof hdr) entry_header();
                break;
            }
            case Z::Name: {
                const size_t take = std::min(n, need_name - name_extra.size());
                name_extra.insert(name_extra.end(), p, p + take);
                p += take; n -= take;
                if (name_extra.size() == need_name) entry_named();
                break;
            }
            case Z::Skip: {
                const size_t take = static_cast<size_t>(std::min<uint64_t>(n, remaining));
                p += take; n -= take; remaining -= take;
                if (remaining == 0) z = Z::Header;
                break;
            }
            case Z::Image: {
                const size_t take = static_cast<size_t>(std::min<uint64_t>(n, remaining));
                image_data(p, take);
                p += take; n -= take; remaining -= take;
                if (!err.empty()) break;
                if (img.done()) { z = Z::Tail; break; }
                if (remaining == 0) { if (cur_method == 0) inflate_ended(); z = Z::Tail; }
                break;
            }
            }
        }
        return status();
    }

    bool write_card(const Fat32Tree& tree) {
        const uint64_t bytes = static_cast<uint64_t>(ex.card_sectors) * 512;
        std::FILE* f = create_exclusive(out);
        if (!f) {
            if (errno == EEXIST) fail("destination exists, not overwritten: " + out);
            else fail("cannot create " + out + ": " + std::strerror(errno));
            return false;
        }
        uint8_t mbr[512] = {};
        uint8_t* pe = mbr + 0x1BE;
        const uint32_t part_sectors = ex.card_sectors - kCardPartLba;
        pe[0] = 0x80;
        chs(kCardPartLba, pe + 1);
        pe[4] = 0x0C;
        chs(ex.card_sectors - 1, pe + 5);
        wr32(pe + 8, kCardPartLba);
        wr32(pe + 12, part_sectors);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        bool ok = std::fwrite(mbr, 1, 512, f) == 512 &&
                  std::fseek(f, static_cast<long>(bytes - 1), SEEK_SET) == 0 &&
                  std::fputc(0, f) == 0;
        ok = (std::fclose(f) == 0) && ok;
        if (!ok) { fail("cannot write " + out); std::remove(out.c_str()); return false; }

        std::string e;
        Fat32WriteOptions wo;
        wo.au_size = 512;
        wo.apply_meta = true;
        if (!fat32_format_and_populate(out, kCardPartLba, part_sectors, tree, e, wo)) {
            fail(e);
            std::remove(out.c_str());
            return false;
        }
        // The volume serial and the hidden sectors (the FAT spec's value, and the
        // distribution's own), in the boot sector and in its backup six sectors on.
        {
            std::fstream g(out, std::ios::in | std::ios::out | std::ios::binary);
            uint8_t ser[4], hid[4];
            wr32(ser, kSerial);
            wr32(hid, kCardPartLba);
            for (uint64_t sec : {uint64_t{0}, uint64_t{6}}) {
                const uint64_t base = (kCardPartLba + sec) * 512;
                g.seekp(static_cast<std::streamoff>(base + 0x43));
                g.write(reinterpret_cast<const char*>(ser), 4);
                g.seekp(static_cast<std::streamoff>(base + 0x1C));
                g.write(reinterpret_cast<const char*>(hid), 4);
            }
            g.flush();
            if (!g) { fail("cannot write the boot sectors of " + out); std::remove(out.c_str()); return false; }
        }
        // Hash the card.
        {
            std::ifstream g(out, std::ios::binary);
            sdcard::Sha256Stream h;
            std::vector<uint8_t> buf(1 << 20);
            while (g) {
                g.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
                const std::streamsize k = g.gcount();
                if (k > 0) h.update(buf.data(), static_cast<size_t>(k));
            }
            card_sha = h.final_hex();
        }
        if (!ex.card_sha256.empty() && card_sha != ex.card_sha256) {
            fail("the card built from the distribution is not the expected one: got " + card_sha +
                 ", expected " + ex.card_sha256 + " (a builder or toolchain fault)");
            std::remove(out.c_str());
            return false;
        }
        return true;
    }

    Status finish() {
        if (finished) return err.empty() ? Status::Done : Status::Error;
        finished = true;
        if (!err.empty()) return Status::Error;
        const std::string name = ex.file.empty() ? std::string("the distribution") : ex.file;
        const std::string digest = sha.final_hex();
        if (in == Input::Zip) {
            const bool bad_size = ex.zip_size && bytes_in != ex.zip_size;
            const bool bad_sha = !ex.zip_sha256.empty() && digest != ex.zip_sha256;
            if (bad_size || bad_sha) {
                fail("this is not " + name + ": it is " + std::to_string(bytes_in) + " bytes with SHA-256 " + digest +
                     "; the official file is " + (ex.zip_size ? std::to_string(ex.zip_size) + " bytes" : std::string("?")) +
                     (ex.zip_sha256.empty() ? std::string() : " with SHA-256 " + ex.zip_sha256));
                return Status::Error;
            }
        } else if (!ex.image_sha256.empty() && digest != ex.image_sha256) {
            fail("this is not the image of " + name + ": its SHA-256 is " + digest + ", expected " + ex.image_sha256);
            return Status::Error;
        }
        if (img.over()) { fail(img.over_message()); return Status::Error; }   // before the truncation: the size is the news
        if (!img.done()) {
            fail(in == Input::Zip && z != Z::Tail && z != Z::Image
                     ? "the zip holds no cspect-next-1gb.img"
                     : "the SD image ends before the boot set is complete (truncated)");
            return Status::Error;
        }
        Fat32Tree tree;
        std::string why;
        if (!img.build_tree(tree, why)) { fail(why); return Status::Error; }
        // jnext's two patches: the same functions recipe 3 uses (one source).
        fat32_tree_upsert(tree, "MACHINES/NEXT/config.ini", sdcard::default_config_ini());
        sdcard::double_known_zx81_rom(tree, ex.zx81_known_sha256);
        if (!write_card(tree)) return Status::Error;
        return Status::Done;
    }
};

bool canonical_dirs(const std::vector<std::string>& in, std::vector<std::string>& out, std::string& why) {
    out.clear();
    for (const std::string& raw : in) {
        auto bad = [&](const char* reason) { why = "'" + raw + "' " + reason; return false; };
        std::string s = raw;
        if (s.empty()) return bad("is empty");
        if (s[0] != '/') return bad("does not start with '/'");
        for (char c : s)
            if (c == '\\') return bad("has a backslash");
            else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return bad("has a control character");
        if (s == "/") return bad("is the whole distribution");
        if (s.back() == '/') s.pop_back();
        std::string first;
        size_t at = 1;
        while (true) {
            const size_t e = s.find('/', at);
            const std::string el = s.substr(at, e == std::string::npos ? std::string::npos : e - at);
            if (el.empty()) return bad("has an empty element");
            if (el == "." || el == "..") return bad("has a '.' or '..' element");
            if (at == 1) first = el;
            if (e == std::string::npos) break;
            at = e + 1;
        }
        if (in_boot_set(first)) return bad("is part of the boot set, which is always on the card");
        out.push_back(lower_of(s));
    }
    std::sort(out.begin(), out.end());
    // Every pair, not only neighbours: a sibling whose name has a character below '/' sorts between a
    // path and its child (/games, /games-x, /games/next).
    for (size_t j = 1; j < out.size(); ++j)
        for (size_t i = 0; i < j; ++i) {
            const std::string& a = out[i];
            const std::string& b = out[j];
            if (a == b) { why = "'" + b + "' is given twice"; return false; }
            if (b.size() > a.size() && b.compare(0, a.size(), a) == 0 && b[a.size()] == '/') {
                why = "'" + b + "' is inside '" + a + "', which is also given";
                return false;
            }
        }
    return true;
}

Expect pinned(Input in) {
    Expect e;
    e.file = kDistribution.file;
    if (in == Input::Zip) {
        e.zip_sha256 = kDistribution.zip_sha256;
        e.zip_size = kDistribution.zip_size;
    } else {
        e.image_sha256 = kDistribution.image_sha256;
    }
    e.card_sha256 = kDistribution.card_sha256;
    return e;
}

Builder::Builder(Input in, std::string out_path, Expect expect) : p_(new Impl) {
    p_->in = in;
    p_->out = std::move(out_path);
    p_->ex = std::move(expect);
    std::vector<std::string> canon;
    std::string why;
    if (!canonical_dirs(p_->ex.dirs, canon, why)) { p_->fail("option dirs: " + why); return; }
    p_->img.set_dirs(canon);
}
Builder::~Builder() = default;
Status Builder::feed(const uint8_t* p, size_t n) { return p_->feed(p, n); }
Status Builder::finish() { return p_->finish(); }
const std::string& Builder::error() const { return p_->err; }
uint64_t Builder::bytes_in() const { return p_->bytes_in; }
uint64_t Builder::inflated() const { return p_->img.pos(); }
std::string Builder::card_sha256() const { return p_->card_sha; }

}  // namespace minimal_card
