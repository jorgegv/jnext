#include "core/minimal_card.h"

#include "core/fat32_image.h"
#include "core/sha256_stream.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace minimal_card {

namespace {

const char* const kBootSet[] = {"TBBLUE.FW", "MACHINES", "NEXTZXOS", "SYS", "LICENSE.md", "README.md"};
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
    std::vector<int> kids;
};

struct Chain {
    std::vector<uint32_t> clusters;
    size_t                got = 0;
    std::vector<uint8_t>  buf;
    int                   rec = 0;   // the owner (0 = the root directory)
    bool                  is_dir = false;
};

class ImageStage {
public:
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

    void register_chain(int rec, const std::vector<uint32_t>& cl, bool is_dir, uint32_t cur,
                        const std::string& path) {
        retained_ += static_cast<uint64_t>(cl.size()) * bpc_;
        if (retained_ > kRetainCap) { fail("the boot set is larger than the card (more than 32 MiB at " + path + ")"); return; }
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
        ch.buf.assign(cl.size() * bpc_, 0);
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
        register_chain(0, cl, true, 0, "");
    }

    size_t data(const uint8_t* p, size_t n) {
        const uint64_t rel = pos_ - data_off_;
        const uint32_t c = 2 + static_cast<uint32_t>(rel / bpc_);
        const size_t off = static_cast<size_t>(rel % bpc_);
        const size_t take = std::min<size_t>(n, bpc_ - off);
        auto it = owner_.find(c);
        if (it != owner_.end())
            std::memcpy(chains_[it->second.first].buf.data() + it->second.second * bpc_ + off, p, take);
        advance(take);
        if (off + take == bpc_) {
            if (it != owner_.end()) {
                const size_t ci = it->second.first;
                owner_.erase(it);
                --outstanding_;
                if (++chains_[ci].got == chains_[ci].clusters.size() && chains_[ci].is_dir) dir_complete(ci, c);
            }
            if (!failed() && outstanding_ == 0) done_ = true;
        }
        return take;
    }

    void dir_complete(size_t ci, uint32_t cur) {
        const int parent = chains_[ci].rec;
        std::vector<Fat32Entry> ents = fat32_parse_dir(chains_[ci].buf);
        std::vector<uint8_t>().swap(chains_[ci].buf);
        const std::string pdir = recs_[static_cast<size_t>(parent)].path;
        for (const Fat32Entry& e : ents) {
            if (failed()) return;
            if (parent == 0) {
                if (!in_boot_set(e.name)) continue;
                bool dup = false;
                for (int k : recs_[0].kids)
                    if (ieq(recs_[static_cast<size_t>(k)].name, e.name)) dup = true;
                if (dup) continue;   // the first one wins, as the old script's `head -n1`
            }
            Rec r;
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
            if (e.is_dir) {
                if (e.cluster < 2) continue;   // an empty directory
                if (!walk(e.cluster, 0, r.path, cl)) return;
            } else if (e.size > 0) {
                if (e.cluster < 2) { fail(r.path + " has data but no clusters"); return; }
                if (!walk(e.cluster, (e.size + bpc_ - 1) / bpc_, r.path, cl)) return;
            } else {
                continue;
            }
            register_chain(ri, cl, e.is_dir, cur, r.path);
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
        std::FILE* f = std::fopen(out.c_str(), "wbx");
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
}
Builder::~Builder() = default;
Status Builder::feed(const uint8_t* p, size_t n) { return p_->feed(p, n); }
Status Builder::finish() { return p_->finish(); }
const std::string& Builder::error() const { return p_->err; }
uint64_t Builder::bytes_in() const { return p_->bytes_in; }
uint64_t Builder::inflated() const { return p_->img.pos(); }
std::string Builder::card_sha256() const { return p_->card_sha; }

}  // namespace minimal_card
