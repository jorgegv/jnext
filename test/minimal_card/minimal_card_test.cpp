// The minimal NextZXOS card builder (core/minimal_card.*, GH #458).
//
// The input is a SYNTHETIC distribution, made here from a hand-written byte layout that
// follows the Microsoft FAT32 spec (MBR, a type-0x0C partition at LBA 63, 4 KiB clusters,
// two FATs, VFAT long names, R/H/S/A attributes, write times). It uses neither FatFs nor
// core/fat32_image.cpp, so a fault shared by the builder and those cannot hide. No NextZXOS
// byte is involved. The zip around it is written by a test-local stored+deflate writer
// (zlib's deflate), image entry first with a 36-byte extra field, as the real zip is.
// The card the builder writes is read back with the vendored FatFs (f_mount/f_stat/f_read:
// third-party code, not our reader); the boot sectors are read raw.
// The expected values come from the fixture's declared contents.
// The row MCARD-17 (recipe 3 unchanged) lives in test/fat32_image/fat32_image_test.cpp.

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/minimal_card.h"
#include "core/sdcard_provisioner.h"
#include "core/sha256.h"
#include "core/sha256_stream.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; return; }
    ++g_fail;
    std::printf("  FAIL %s: %s", id, desc);
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

std::string tmp_path(const std::string& name) {
    const char* base = std::getenv("TMPDIR");
    std::string dir = base && *base ? base : "/tmp";
    return dir + "/jnext_mcard_test_" + std::to_string(::getpid()) + "_" + name;
}

bool exists(const std::string& p) { std::ifstream f(p, std::ios::binary); return static_cast<bool>(f); }

void w16(std::vector<uint8_t>& v, size_t off, uint32_t x) { v[off] = x; v[off + 1] = x >> 8; }
void w32(std::vector<uint8_t>& v, size_t off, uint32_t x) { w16(v, off, x); w16(v, off + 2, x >> 16); }
void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x); v.push_back(x >> 8); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x); put16(v, x >> 16); }
uint32_t r32(const std::vector<uint8_t>& v, size_t off) {
    return v[off] | (v[off + 1] << 8) | (v[off + 2] << 16) | (static_cast<uint32_t>(v[off + 3]) << 24);
}

std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

std::string sha_of(const std::vector<uint8_t>& v) { return sdcard::sha256_hex(v); }
std::string sha_file(const std::string& p) { return sdcard::sha256_file(p); }

// A reproducible byte pattern (an LCG), so that deflate has real work to do.
std::vector<uint8_t> pattern(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 12345u;
    for (auto& b : v) { x = x * 1664525u + 1013904223u; b = static_cast<uint8_t>(x >> 24); }
    return v;
}

// ---------------------------------------------------------------------------
// The synthetic distribution image (MS FAT32 spec layout, independent of FatFs).
// ---------------------------------------------------------------------------
struct FNode {
    std::string name;
    bool dir = false;
    std::vector<uint8_t> data;
    uint64_t declared = 0;          // a file whose size is declared but whose data is not stored
    uint8_t attr = 0x20;            // the R/H/S/A bits (the writer adds the directory bit)
    uint16_t wdate = 0, wtime = 0;
    std::vector<FNode> kids;
    uint32_t cluster = 0, nclusters = 0;   // laid out by build_image (cluster = chain[0])
    std::vector<uint32_t> chain;
    uint32_t entry_cluster = 0;            // when non-zero, the cluster the directory entry claims
    std::string sfn;
};

FNode file(const std::string& n, std::vector<uint8_t> d, uint8_t attr, uint16_t wd, uint16_t wt) {
    FNode f; f.name = n; f.data = std::move(d); f.attr = attr; f.wdate = wd; f.wtime = wt; return f;
}
FNode dirn(const std::string& n, uint8_t attr, uint16_t wd, uint16_t wt) {
    FNode f; f.name = n; f.dir = true; f.attr = attr; f.wdate = wd; f.wtime = wt; return f;
}

constexpr uint32_t kPartLba = 63, kSpc = 8, kBpc = kSpc * 512, kReserved = 32;

struct Image {
    std::vector<uint8_t> bytes;
    uint64_t data_off = 0;
    uint32_t last_boot_cluster = 0;
    std::map<std::string, std::vector<uint32_t>> chains;   // by node name
};

bool is_83(const std::string& n, bool& all_lower) {
    all_lower = false;
    const size_t dot = n.find('.');
    const std::string b = dot == std::string::npos ? n : n.substr(0, dot);
    const std::string e = dot == std::string::npos ? "" : n.substr(dot + 1);
    if (b.empty() || b.size() > 8 || e.size() > 3 || e.find('.') != std::string::npos) return false;
    bool lo = false, up = false;
    for (char c : n) {
        if (c == '.') continue;
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
        if (std::islower(static_cast<unsigned char>(c))) lo = true;
        if (std::isupper(static_cast<unsigned char>(c))) up = true;
    }
    if (lo && up) return false;
    all_lower = lo;
    return true;
}

std::string pad83(const std::string& n) {
    std::string up;
    for (char c : n) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    const size_t dot = up.find('.');
    std::string b = dot == std::string::npos ? up : up.substr(0, dot);
    std::string e = dot == std::string::npos ? "" : up.substr(dot + 1);
    b.resize(8, ' ');
    e.resize(3, ' ');
    return b + e;
}

uint8_t sfn_checksum(const std::string& s11) {
    uint8_t sum = 0;
    for (char c : s11) sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<uint8_t>(c));
    return sum;
}

void set_sfns(FNode& d) {
    int counter = 0;
    for (auto& k : d.kids) {
        bool lo;
        if (is_83(k.name, lo)) k.sfn = pad83(k.name);
        else {
            std::string up;
            for (char c : k.name) if (std::isalnum(static_cast<unsigned char>(c))) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            up.resize(std::min<size_t>(up.size(), 6));
            std::string ext;
            const size_t dot = k.name.rfind('.');
            if (dot != std::string::npos) for (char c : k.name.substr(dot + 1)) ext.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            ext.resize(std::min<size_t>(ext.size(), 3));
            std::string b = up + "~" + std::to_string(++counter);
            b.resize(8, ' ');
            ext.resize(3, ' ');
            k.sfn = b + ext;
        }
        if (k.dir) set_sfns(k);
    }
}

void dir_entries(const FNode& d, const FNode* parent, std::vector<uint8_t>& out) {
    auto sfn_entry = [&](const std::string& s11, uint8_t attr, uint8_t nt, uint32_t cl, uint32_t size, uint16_t wd, uint16_t wt) {
        std::vector<uint8_t> e(32, 0);
        std::memcpy(e.data(), s11.data(), 11);
        e[11] = attr;
        e[12] = nt;
        // create time/date and access date are DISTINCT from the write stamp (offsets 22/24),
        // so a reader that takes the wrong pair is caught (MCARD-03)
        w16(e, 14, wt ^ 0x0505); w16(e, 16, wd ^ 0x0100); w16(e, 18, wd ^ 0x0300);
        w16(e, 20, cl >> 16);
        w16(e, 22, wt); w16(e, 24, wd);
        w16(e, 26, cl & 0xFFFF);
        w32(e, 28, size);
        out.insert(out.end(), e.begin(), e.end());
    };
    if (parent || d.name != "<root>") {   // "." and ".."
        sfn_entry(".          ", 0x10, 0, d.cluster, 0, d.wdate, d.wtime);
        sfn_entry("..         ", 0x10, 0, parent && parent->name != "<root>" ? parent->cluster : 0, 0, d.wdate, d.wtime);
    }
    for (const auto& k : d.kids) {
        bool lo;
        const bool plain = is_83(k.name, lo);
        const uint8_t attr = static_cast<uint8_t>(k.attr | (k.dir ? 0x10 : 0));
        if (!plain) {
            const size_t slots = (k.name.size() + 12) / 13;
            const uint8_t cs = sfn_checksum(k.sfn);
            for (size_t s = slots; s >= 1; --s) {
                std::vector<uint8_t> e(32, 0);
                e[0] = static_cast<uint8_t>(s | (s == slots ? 0x40 : 0));
                e[11] = 0x0F;
                e[13] = cs;
                static const int idx[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                for (int i = 0; i < 13; ++i) {
                    const size_t ci = (s - 1) * 13 + static_cast<size_t>(i);
                    uint16_t u = ci < k.name.size() ? static_cast<uint8_t>(k.name[ci]) : (ci == k.name.size() ? 0x0000 : 0xFFFF);
                    w16(e, static_cast<size_t>(idx[i]), u);
                }
                out.insert(out.end(), e.begin(), e.end());
            }
        }
        const uint8_t nt = plain && lo ? (k.name.find('.') == std::string::npos ? 0x08 : 0x18) : 0;
        sfn_entry(k.sfn, attr, nt, k.entry_cluster ? k.entry_cluster : k.cluster, k.dir ? 0 : static_cast<uint32_t>(k.declared ? k.declared : k.data.size()),
                  k.wdate, k.wtime);
    }
}

void collect(FNode& n, std::vector<FNode*>& order) {
    order.push_back(&n);
    for (auto& k : n.kids) collect(k, order);
}

// How the fixture is laid out in the image.
struct LayoutOpts {
    std::string early_name;       // allocate that node's clusters before the root's (MCARD-10)
    std::string trim_before;      // drop the image's bytes from the first cluster of that node on
    std::string boot_last = "APPS";
    uint32_t lba = 63;            // partition start
    uint8_t ptype = 0x0C;         // partition type
    // Nodes whose cluster chain runs DOWNWARD (second cluster below the first): the chain is
    // assembled by index, not by arrival order. a.sys is a file, next a directory.
    std::set<std::string> reverse = {"a.sys", "next"};
    std::string short_chain;      // that node's FAT chain ends after its first cluster
    std::map<std::string, std::string> alias;   // node X's directory entry claims node Y's cluster
};

Image build_image(FNode root, const LayoutOpts& o = LayoutOpts()) {
    root.name = "<root>";
    set_sfns(root);
    std::vector<FNode*> order;
    collect(root, order);
    if (!o.early_name.empty()) {
        auto it = std::find_if(order.begin(), order.end(), [&](FNode* p) { return p->name == o.early_name; });
        if (it != order.end()) { FNode* p = *it; order.erase(it); order.insert(order.begin(), p); }
    }
    // directory sizes need the entry count: lay out directories first (entry count is known)
    uint32_t next = 2;
    for (FNode* p : order) {
        std::vector<uint8_t> tmp;
        if (p->dir) { FNode copy = *p; dir_entries(copy, nullptr, tmp); }
        const uint64_t bytes = p->dir ? std::max<uint64_t>(tmp.size() + 32, 1) : (p->declared ? p->declared : p->data.size());
        p->nclusters = bytes ? static_cast<uint32_t>((bytes + kBpc - 1) / kBpc) : 0;
        p->chain.clear();
        for (uint32_t i = 0; i < p->nclusters; ++i) p->chain.push_back(next + i);
        if (o.reverse.count(p->name) && p->chain.size() > 1) std::reverse(p->chain.begin(), p->chain.end());
        p->cluster = p->chain.empty() ? 0 : p->chain[0];
        next += p->nclusters;
    }
    for (auto& al : o.alias) {
        FNode *x = nullptr, *y = nullptr;
        for (FNode* p : order) { if (p->name == al.first) x = p; if (p->name == al.second) y = p; }
        if (x && y) x->entry_cluster = y->cluster;
    }
    const uint32_t nclst = next - 2;
    const uint32_t fat_sectors = ((nclst + 2) * 4 + 511) / 512;
    const uint32_t data_sector = kReserved + 2 * fat_sectors;
    const uint32_t total = data_sector + nclst * kSpc;
    Image im;
    im.data_off = static_cast<uint64_t>(o.lba + data_sector) * 512;
    size_t size = static_cast<size_t>(o.lba + total) * 512;
    if (!o.trim_before.empty())
        for (FNode* p : order)
            if (p->name == o.trim_before) size = static_cast<size_t>(im.data_off) + static_cast<size_t>(p->cluster - 2) * kBpc;
    im.bytes.assign(size, 0);
    auto& b = im.bytes;
    // MBR
    uint8_t* pe = b.data() + 0x1BE;
    pe[0] = 0x80; pe[4] = o.ptype;
    w32(b, 0x1BE + 8, o.lba); w32(b, 0x1BE + 12, total);
    b[510] = 0x55; b[511] = 0xAA;
    // BPB
    const size_t v = static_cast<size_t>(o.lba) * 512;
    b[v] = 0xEB; b[v + 1] = 0x58; b[v + 2] = 0x90;
    std::memcpy(&b[v + 3], "MSWIN4.1", 8);
    w16(b, v + 11, 512); b[v + 13] = kSpc; w16(b, v + 14, kReserved); b[v + 16] = 2;
    b[v + 21] = 0xF8; w16(b, v + 24, 63); w16(b, v + 26, 255); w32(b, v + 28, o.lba);
    w32(b, v + 32, total); w32(b, v + 36, fat_sectors);
    b[v + 510] = 0x55; b[v + 511] = 0xAA;
    w32(b, v + 44, root.cluster);
    // FATs and data
    const size_t fat0 = v + static_cast<size_t>(kReserved) * 512;
    auto set_fat = [&](uint32_t c, uint32_t val) {
        for (int f = 0; f < 2; ++f) w32(b, fat0 + static_cast<size_t>(f) * fat_sectors * 512 + 4 * static_cast<size_t>(c), val);
    };
    set_fat(0, 0x0FFFFFF8); set_fat(1, 0x0FFFFFFF);
    for (FNode* p : order) {
        im.chains[p->name] = p->chain;
        for (uint32_t i = 0; i < p->nclusters; ++i) {
            const bool last = i + 1 == p->nclusters || (p->name == o.short_chain && i == 0);
            set_fat(p->chain[i], last ? 0x0FFFFFFF : p->chain[i + 1]);
        }
        const FNode* par = nullptr;
        for (FNode* q : order) for (auto& k : q->kids) if (&k == p) par = q;
        std::vector<uint8_t> content = p->data;
        if (p->dir) { content.clear(); dir_entries(*p, par, content); }
        if (p->declared) continue;
        for (uint32_t i = 0; i < p->nclusters; ++i) {
            const size_t from = static_cast<size_t>(i) * kBpc;
            if (from >= content.size()) break;
            const size_t n = std::min<size_t>(kBpc, content.size() - from);
            const size_t at = static_cast<size_t>(im.data_off) + static_cast<size_t>(p->chain[i] - 2) * kBpc;
            if (at + n <= b.size()) std::memcpy(&b[at], content.data() + from, n);
        }
    }
    // the last cluster of the boot set = the highest cluster of any node outside the last top-level dir
    for (FNode* p : order) {
        if (p == &root || p->nclusters == 0) continue;
        bool in_last = false;
        for (const FNode* q = p; q; ) {
            if (q->name == o.boot_last || q->name == "OTHER.TXT") { in_last = true; break; }
            const FNode* par = nullptr;
            for (FNode* r : order) for (auto& k : r->kids) if (&k == q) par = r;
            q = (par && par != &root) ? par : nullptr;
        }
        if (!in_last) im.last_boot_cluster = std::max(im.last_boot_cluster, *std::max_element(p->chain.begin(), p->chain.end()));
    }
    return im;
}

// The fixture tree. Boot set + APPS (not in it) + OTHER.TXT (not in it).
FNode fixture(bool with_sys = true, bool big = false, bool dup_root = false) {
    FNode root = dirn("<root>", 0, 0, 0);
    root.kids.push_back(file("TBBLUE.FW", pattern(3000, 1), 0x24, 0x5A21, 0x6C41));
    if (dup_root) root.kids.push_back(file("TbBlue.fw", pattern(3000, 99), 0x20, 0x5A21, 0x6C41));   // the same name: the first one wins
    FNode mach = dirn("machines", 0x00, 0x5A22, 0x7000);
    FNode next = dirn("next", 0x00, 0x5A23, 0x7200);
    next.kids.push_back(file("48.rom", pattern(16384, 2), 0x21, 0x5A24, 0x7400));
    next.kids.push_back(file("zx81.rom", pattern(16384, 3), 0x20, 0x5A25, 0x7600));
    // 2 (. and ..) + 2 + 122 entries fill the first 128-entry cluster up to the long name, whose
    // two LFN slots then end it and whose short entry starts the second one (it straddles).
    for (int i = 0; i < 122; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "F%03d.BIN", i);
        next.kids.push_back(file(nm, {}, 0x20, 0x5A26, static_cast<uint16_t>(0x1000 + i)));
    }
    next.kids.push_back(file("straddle-long-name.dat", pattern(100, 4), 0x20, 0x5A27, 0x7800));
    mach.kids.push_back(next);
    root.kids.push_back(mach);
    FNode nzx = dirn("nextzxos", 0x01, 0x5A28, 0x7A00);          // read-only directory
    nzx.kids.push_back(file("a.sys", pattern(5000, 5), 0x00, 0x5A29, 0x7C00));   // no archive bit
    root.kids.push_back(nzx);
    if (with_sys) {
        FNode sys = dirn("SYS", 0x02, 0x5A2A, 0x7E00);           // hidden directory
        sys.kids.push_back(file("b.bas", pattern(700, 6), 0x01, 0x5A2B, 0x8000));
        if (big) {
            FNode f = file("big.bin", {}, 0x20, 0x5A2C, 0x8200);
            f.declared = 33ull << 20;
            sys.kids.push_back(f);
        }
        root.kids.push_back(sys);
    }
    root.kids.push_back(file("LICENSE.md", pattern(900, 7), 0x22, 0x5A2D, 0x8400));
    root.kids.push_back(file("README.md", pattern(800, 8), 0x21, 0x5A2E, 0x8600));
    root.kids.push_back(file("OTHER.TXT", pattern(300, 9), 0x20, 0x5A2F, 0x8800));
    FNode apps = dirn("APPS", 0x00, 0x5A30, 0x8A00);
    apps.kids.push_back(file("x.bin", std::vector<uint8_t>(2 << 20, 0x5A), 0x20, 0x5A31, 0x8C00));
    root.kids.push_back(apps);
    return root;
}

// ---------------------------------------------------------------------------
// The test-local zip writer.
// ---------------------------------------------------------------------------
struct ZipOpts {
    uint16_t flags = 0;
    uint16_t method = 8;
    bool with_image = true;
    std::string image_name = "cspect-next-1gb.img";
};

std::vector<uint8_t> deflate_raw(const std::vector<uint8_t>& in) {
    z_stream z{};
    deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(deflateBound(&z, static_cast<uLong>(in.size())) + 16);
    z.next_in = const_cast<uint8_t*>(in.data()); z.avail_in = static_cast<uInt>(in.size());
    z.next_out = out.data(); z.avail_out = static_cast<uInt>(out.size());
    deflate(&z, Z_FINISH);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return out;
}

void zip_entry(std::vector<uint8_t>& z, const std::string& name, const std::vector<uint8_t>& raw, bool defl,
               uint16_t flags, uint16_t method, size_t extra_len) {
    const std::vector<uint8_t> comp = defl ? deflate_raw(raw) : raw;
    put32(z, 0x04034b50); put16(z, 20); put16(z, flags); put16(z, method); put16(z, 0); put16(z, 0x21);
    put32(z, static_cast<uint32_t>(crc32(0, raw.data(), static_cast<uInt>(raw.size()))));
    put32(z, static_cast<uint32_t>(comp.size())); put32(z, static_cast<uint32_t>(raw.size()));
    put16(z, static_cast<uint32_t>(name.size())); put16(z, static_cast<uint32_t>(extra_len));
    z.insert(z.end(), name.begin(), name.end());
    z.insert(z.end(), extra_len, 0);
    z.insert(z.end(), comp.begin(), comp.end());
}

std::vector<uint8_t> make_zip(const std::vector<uint8_t>& image, const ZipOpts& o = {}) {
    std::vector<uint8_t> z;
    if (o.with_image) zip_entry(z, o.image_name, image, o.method == 8, o.flags, o.method, 36);
    zip_entry(z, "enNextZX.rom", pattern(64, 20), false, 0, 0, 0);
    zip_entry(z, "enNxtmmc.rom", pattern(64, 21), false, 0, 0, 0);
    put32(z, 0x02014b50);                 // central directory: not read by the builder
    z.insert(z.end(), 42, 0);
    put32(z, 0x06054b50);
    z.insert(z.end(), 18, 0);
    return z;
}

// ---------------------------------------------------------------------------
// Running the builder
// ---------------------------------------------------------------------------
struct Run {
    minimal_card::Status st = minimal_card::Status::NeedMore;
    std::string err, sha;
    uint64_t inflated_at_stop = 0;
    bool stopped = false;
    uint64_t inflated_end = 0;
};

Run build(minimal_card::Input in, const std::vector<uint8_t>& bytes, const std::string& out,
          const minimal_card::Expect& ex, size_t chunk = 1 << 20) {
    Run r;
    minimal_card::Builder b(in, out, ex);
    for (size_t off = 0; off < bytes.size() && b.error().empty(); off += chunk) {
        const size_t n = std::min(chunk, bytes.size() - off);
        const auto s = b.feed(bytes.data() + off, n);
        if (s == minimal_card::Status::InflateDone && !r.stopped) { r.stopped = true; r.inflated_at_stop = b.inflated(); }
    }
    r.inflated_end = b.inflated();
    r.st = b.error().empty() ? b.finish() : minimal_card::Status::Error;
    r.err = b.error();
    r.sha = b.card_sha256();
    return r;
}

minimal_card::Expect synth_expect(const std::vector<uint8_t>& rom16) {
    minimal_card::Expect e;
    e.zx81_known_sha256 = sha_of(rom16);
    return e;
}

// FatFs view of a card.
struct Card {
    FATFS fs{};
    bool ok = false;
    std::string err;
    explicit Card(const std::string& path, uint32_t sectors = 40 * 2048) {
        if (!fatfs_glue::attach(0, path, 63, sectors - 63, err)) return;
        ok = f_mount(&fs, "0:", 1) == FR_OK;
    }
    ~Card() { if (ok) f_mount(nullptr, "0:", 0); fatfs_glue::detach(0); }
    bool read(const std::string& p, std::vector<uint8_t>& out) {
        FIL f;
        if (f_open(&f, p.c_str(), FA_READ) != FR_OK) return false;
        out.assign(f_size(&f), 0);
        UINT n = 0;
        const bool good = out.empty() || (f_read(&f, out.data(), static_cast<UINT>(out.size()), &n) == FR_OK && n == out.size());
        f_close(&f);
        return good;
    }
    void walk(const std::string& dir, std::map<std::string, FILINFO>& out) {
        DIR d;
        if (f_opendir(&d, dir.c_str()) != FR_OK) return;
        FILINFO fi{};
        // The vendored get_fileinfo leaves fname alone at the end of a directory and for an
        // entry with no long name, so the caller clears it before every call.
        for (fi.fname[0] = 0; f_readdir(&d, &fi) == FR_OK && fi.fname[0]; fi.fname[0] = 0) {
            if (!std::strcmp(fi.fname, ".") || !std::strcmp(fi.fname, "..")) continue;
            const std::string p = dir + "/" + fi.fname;
            out[lower(p)] = fi;
            if (fi.fattrib & AM_DIR) walk(p, out);
        }
        f_closedir(&d);
    }
};

void expected_paths(const FNode& n, const std::string& prefix, bool top, std::map<std::string, const FNode*>& out) {
    for (const auto& k : n.kids) {
        if (top && !(k.name == "TBBLUE.FW" || k.name == "machines" || k.name == "nextzxos" || k.name == "SYS" ||
                     k.name == "LICENSE.md" || k.name == "README.md")) continue;
        const std::string p = prefix + "/" + lower(k.name);
        out[p] = &k;
        if (k.dir) expected_paths(k, p, false, out);
    }
}

}  // namespace

int main() {
    const std::string card = tmp_path("card.img");
    const std::vector<uint8_t> rom16 = pattern(16384, 3);   // the fixture's zx81.rom
    const FNode fx = fixture();
    const Image im = build_image(fx);
    const std::vector<uint8_t> zip = make_zip(im.bytes);
    const minimal_card::Expect ex0 = synth_expect(rom16);
    std::remove(card.c_str());
    const Run main_run = build(minimal_card::Input::Zip, zip, card, ex0);
    const bool built = main_run.st == minimal_card::Status::Done;
    if (!built) std::printf("  main build failed: %s\n", main_run.err.c_str());

    std::map<std::string, const FNode*> want;
    expected_paths(fx, "0:", true, want);

    // MCARD-01: the card mounts as FAT32, 512-byte clusters, more than 65525 of them.
    {
        bool fat32 = false, csize1 = false, many = false;
        std::string d;
        if (built) {
            Card c(card);
            if (c.ok) {
                fat32 = c.fs.fs_type == FS_FAT32;
                csize1 = c.fs.csize == 1;
                many = c.fs.n_fatent - 2 > 65525u;
                d = "clusters=" + std::to_string(c.fs.n_fatent - 2);
            } else d = "mount failed: " + c.err;
        }
        check("MCARD-01", "the zip fixture builds; FatFs mounts a FAT32 card with 512-byte clusters and > 65525 of them",
              built && fat32 && csize1 && many, d);
    }

    // MCARD-02: exactly the boot set plus config.ini, byte-identical; APPS absent; the long name survives.
    {
        std::set<std::string> have, expect;
        bool same = built, apps_absent = built, long_ok = false;
        std::string d;
        if (built) {
            Card c(card);
            std::map<std::string, FILINFO> got;
            c.walk("0:", got);
            for (auto& kv : got) have.insert(kv.first);
            for (auto& kv : want) expect.insert(kv.first);
            expect.insert("0:/machines/next/config.ini");
            apps_absent = !have.count("0:/apps") && !have.count("0:/other.txt");
            for (auto& kv : want) {
                if (kv.second->dir) continue;
                std::vector<uint8_t> got_data, want_data = kv.second->data;
                if (kv.first == "0:/machines/next/zx81.rom")    // the digest is the fixture's: doubled (MCARD-04)
                    want_data.insert(want_data.end(), kv.second->data.begin(), kv.second->data.end());
                if (!c.read(kv.first, got_data) || got_data != want_data) { same = false; d += " differs:" + kv.first; }
            }
            auto it = got.find("0:/machines/next/straddle-long-name.dat");
            long_ok = it != got.end() && std::string(it->second.fname) == "straddle-long-name.dat";
        }
        check("MCARD-02", "the card holds exactly the boot set plus config.ini, byte-identical, APPS absent, the straddling long name kept",
              built && have == expect && same && apps_absent && long_ok,
              d + " have=" + std::to_string(have.size()) + " expect=" + std::to_string(expect.size()));
    }

    // MCARD-03: write time and the R/H/S/A bits of every copied file and directory.
    {
        bool all = built;
        std::string d;
        if (built) {
            Card c(card);
            for (auto& kv : want) {
                FILINFO fi{};
                if (f_stat(kv.first.c_str(), &fi) != FR_OK) { all = false; d += " nostat:" + kv.first; continue; }
                const uint8_t mask = AM_RDO | AM_HID | AM_SYS | AM_ARC;
                if (fi.fdate != kv.second->wdate || fi.ftime != kv.second->wtime ||
                    (fi.fattrib & mask) != (kv.second->attr & mask)) {
                    all = false;
                    char b[96];
                    std::snprintf(b, sizeof b, " %s: date %04X/%04X time %04X/%04X attr %02X/%02X", kv.first.c_str(),
                                  fi.fdate, kv.second->wdate, fi.ftime, kv.second->wtime, fi.fattrib & mask, kv.second->attr & mask);
                    d += b;
                }
            }
        }
        check("MCARD-03", "write date/time and the R/H/S/A bits of every copied file and directory equal the fixture's",
              built && all, d);
    }

    // MCARD-04: the two patches (the provisioner's own functions).
    {
        bool cfg_ok = false, doubled = false, meta_ok = false, untouched = false;
        if (built) {
            Card c(card);
            std::vector<uint8_t> cfg, rom;
            cfg_ok = c.read("0:/machines/next/config.ini", cfg) && cfg == sdcard::default_config_ini();
            doubled = c.read("0:/machines/next/zx81.rom", rom) && rom.size() == 32768 &&
                      std::equal(rom.begin(), rom.begin() + 16384, rom16.begin()) &&
                      std::equal(rom.begin() + 16384, rom.end(), rom16.begin());
            FILINFO fi{};
            meta_ok = f_stat("0:/machines/next/config.ini", &fi) == FR_OK && fi.fdate == 0x0021 && fi.ftime == 0 &&
                      (fi.fattrib & AM_ARC);
        }
        // another known digest: zx81.rom stays 16 KiB
        const std::string c2 = tmp_path("card-other.img");
        std::remove(c2.c_str());
        minimal_card::Expect e2;
        e2.zx81_known_sha256 = std::string(64, '0');
        const Run r2 = build(minimal_card::Input::Zip, zip, c2, e2);
        if (r2.st == minimal_card::Status::Done) {
            Card c(c2);
            std::vector<uint8_t> rom;
            untouched = c.read("0:/machines/next/zx81.rom", rom) && rom == rom16;
        }
        std::remove(c2.c_str());
        check("MCARD-04", "config.ini is default_config_ini(); the known zx81.rom is doubled, another digest leaves it as it is",
              built && cfg_ok && doubled && meta_ok && untouched,
              std::string(cfg_ok ? "" : " cfg") + (doubled ? "" : " doubled") + (meta_ok ? "" : " meta") + (untouched ? "" : " untouched"));
    }

    // MCARD-05: MBR and boot sectors.
    {
        bool mbr_ok = false, vbr_ok = false;
        if (built) {
            std::ifstream f(card, std::ios::binary);
            std::vector<uint8_t> b(70 * 512);
            f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
            mbr_ok = b[0x1BE] == 0x80 && b[0x1BE + 4] == 0x0C && r32(b, 0x1BE + 8) == 63 && r32(b, 0x1BE + 12) == 81857 &&
                     r32(b, 0x1B8) == 0 && b[510] == 0x55 && b[511] == 0xAA;
            vbr_ok = true;
            for (size_t sec : {size_t{63}, size_t{69}}) {   // the boot sector and its backup, 6 sectors on
                vbr_ok = vbr_ok && r32(b, sec * 512 + 0x43) == 0x4A4E5854u && r32(b, sec * 512 + 0x1C) == 63;
            }
        }
        check("MCARD-05", "MBR entry 0 = 80 .. 0C, LBA 63, 81857 sectors, signature 0, 55AA; boot sector and its backup: serial 4A4E5854, hidden 63",
              built && mbr_ok && vbr_ok);
    }

    // MCARD-06: chunk-boundary independence.
    {
        bool same = built;
        std::string d;
        for (size_t chunk : {size_t{1}, size_t{7}, size_t{4096}}) {
            const std::string c2 = tmp_path("card-chunk.img");
            std::remove(c2.c_str());
            const Run r = build(minimal_card::Input::Zip, zip, c2, ex0, chunk);
            if (r.st != minimal_card::Status::Done || r.sha != main_run.sha) { same = false; d += " chunk" + std::to_string(chunk) + ":" + r.err; }
            std::remove(c2.c_str());
        }
        check("MCARD-06", "the same zip fed in chunks of 1, 7, 4096 and 1 MiB bytes gives four identical card digests",
              built && same, d);
    }

    // MCARD-07: a wrong zip digest is refused before anything is written.
    {
        const std::string c2 = tmp_path("card-wrongzip.img");
        std::remove(c2.c_str());
        minimal_card::Expect e = ex0;
        e.zip_sha256 = std::string(64, '1');
        e.file = "sn-emulator-test.zip";
        const Run r = build(minimal_card::Input::Zip, zip, c2, e);
        const std::string real = sha_of(zip);
        check("MCARD-07", "pinned mode with a wrong zip sha256: refused naming both digests and the file, no output file",
              r.st == minimal_card::Status::Error && r.err.find(real) != std::string::npos &&
                  r.err.find(e.zip_sha256) != std::string::npos && r.err.find("sn-emulator-test.zip") != std::string::npos &&
                  !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-08: a wrong card digest removes the card.
    {
        const std::string c2 = tmp_path("card-wrongcard.img");
        std::remove(c2.c_str());
        minimal_card::Expect e = ex0;
        e.card_sha256 = std::string(64, '2');
        const Run r = build(minimal_card::Input::Zip, zip, c2, e);
        check("MCARD-08", "pinned mode with a wrong card sha256: refused naming both digests, the output removed",
              built && r.st == minimal_card::Status::Error && r.err.find(main_run.sha) != std::string::npos &&
                  r.err.find(e.card_sha256) != std::string::npos && !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-09: a missing boot-set entry is named.
    {
        const std::string c2 = tmp_path("card-nosys.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fixture(false)).bytes), c2, ex0);
        check("MCARD-09", "a fixture without SYS: refused naming SYS, no output file",
              r.st == minimal_card::Status::Error && r.err.find("SYS") != std::string::npos && !exists(c2), r.err);
        std::remove(c2.c_str());
    }

    // MCARD-10: a layout that needs a second pass.
    {
        const std::string c2 = tmp_path("card-order.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fx, [] { LayoutOpts o; o.early_name = "a.sys"; return o; }()).bytes), c2, ex0);
        check("MCARD-10", "a file whose cluster comes before its directory: refused, naming 'second pass' and the path",
              r.st == minimal_card::Status::Error && r.err.find("second pass") != std::string::npos &&
                  r.err.find("/nextzxos/a.sys") != std::string::npos && !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-11: the zip refusals, each with its own message.
    {
        struct Case { const char* what; std::vector<uint8_t> bytes; const char* needle; };
        std::vector<Case> cases;
        cases.push_back({"not a zip", std::vector<uint8_t>(600, 'x'), "not a zip"});
        ZipOpts o;
        o.with_image = false;
        cases.push_back({"no image", make_zip(im.bytes, o), "holds no cspect-next-1gb.img"});
        o = ZipOpts(); o.flags = 8;
        cases.push_back({"bit 3", make_zip(im.bytes, o), "data descriptor"});
        o = ZipOpts(); o.flags = 1;
        cases.push_back({"bit 0", make_zip(im.bytes, o), "encrypted"});
        o = ZipOpts(); o.method = 12;
        cases.push_back({"method 12", make_zip(im.bytes, o), "method 12"});
        std::vector<uint8_t> cut(zip.begin(), zip.begin() + 400);   // inside the deflate stream, before the boot set is read
        cases.push_back({"truncated", cut, "truncated"});
        bool all = true;
        std::set<std::string> msgs;
        std::string d;
        for (auto& c : cases) {
            const std::string c2 = tmp_path("card-zip.img");
            std::remove(c2.c_str());
            const Run r = build(minimal_card::Input::Zip, c.bytes, c2, ex0);
            const bool ok = r.st == minimal_card::Status::Error && r.err.find(c.needle) != std::string::npos && !exists(c2);
            if (!ok) { all = false; d += std::string(" [") + c.what + ": '" + r.err + "']"; }
            msgs.insert(r.err);
            std::remove(c2.c_str());
        }
        check("MCARD-11", "zip refusals (not a zip, no image, bit 3, bit 0, method 12, truncated) each give their own message",
              all && msgs.size() == cases.size(), d);
    }

    // MCARD-12: Input::Image gives the same card as Input::Zip.
    {
        const std::string c2 = tmp_path("card-image.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Image, im.bytes, c2, ex0);
        check("MCARD-12", "the raw image gives the same card sha256 as its zip",
              built && r.st == minimal_card::Status::Done && r.sha == main_run.sha, r.err);
        std::remove(c2.c_str());
    }

    // MCARD-13: inflation stops early; the hash still covers the whole zip.
    {
        const uint64_t end_of_boot = im.data_off + static_cast<uint64_t>(im.last_boot_cluster - 1) * kBpc;
        const std::string c2 = tmp_path("card-early.img");
        std::remove(c2.c_str());
        minimal_card::Expect e = ex0;
        e.zip_sha256 = sha_of(zip);
        const Run full = build(minimal_card::Input::Zip, zip, c2, e, 4096);
        const bool early = full.stopped && full.inflated_at_stop >= end_of_boot &&
                           full.inflated_at_stop <= end_of_boot + 65536 && full.inflated_at_stop + (1 << 20) < im.bytes.size();
        std::remove(c2.c_str());
        // truncated AFTER the stop point: inflation is happy, the pinned zip digest is not
        std::vector<uint8_t> cut(zip.begin(), zip.end() - 1000);
        const Run t = build(minimal_card::Input::Zip, cut, c2, e, 4096);
        check("MCARD-13", "feed stops inflating at the last wanted cluster, still hashes the rest; a zip truncated after that point fails the pinned digest",
              full.st == minimal_card::Status::Done && early && t.st == minimal_card::Status::Error &&
                  t.err.find(e.zip_sha256) != std::string::npos && !exists(c2),
              "stopped=" + std::to_string(full.stopped) + " at=" + std::to_string(full.inflated_at_stop) +
                  " end=" + std::to_string(end_of_boot) + " " + full.err + " | " + t.err);
        std::remove(c2.c_str());
    }

    // MCARD-14: the retention cap.
    {
        const std::string c2 = tmp_path("card-big.img");
        std::remove(c2.c_str());
        const Image bigim = build_image(fixture(true, true), [] { LayoutOpts o; o.trim_before = "big.bin"; return o; }());
        const Run r = build(minimal_card::Input::Image, bigim.bytes, c2, ex0);
        check("MCARD-14", "a boot-set file beyond the 32 MiB retention cap: refused ('boot set is larger than the card')",
              r.st == minimal_card::Status::Error && r.err.find("larger than the card") != std::string::npos && !exists(c2), r.err);
        std::remove(c2.c_str());
    }

    // MCARD-15: an existing destination is never overwritten.
    {
        const std::string c2 = tmp_path("card-exists.img");
        { std::ofstream f(c2, std::ios::binary); f << "keep"; }
        const Run r = build(minimal_card::Input::Zip, zip, c2, ex0);
        std::ifstream f(c2, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check("MCARD-15", "an existing destination file: refused, its bytes unchanged",
              r.st == minimal_card::Status::Error && r.err.find("exists") != std::string::npos && content == "keep", r.err);
        std::remove(c2.c_str());
    }

    // MCARD-16: the pinned distribution constants.
    {
        const auto& p = minimal_card::kDistribution;
        auto hex64 = [](const char* s) {
            const std::string x = s;
            return x.size() == 64 && std::all_of(x.begin(), x.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        };
        const std::string url = p.url;
        check("MCARD-16", "kDistribution: url == kDistroUrl and ends with file; three 64-digit lower-case digests; size > 0; recipe >= 1",
              url == sdcard::kDistroUrl && url.substr(url.rfind('/') + 1) == p.file && hex64(p.zip_sha256) &&
                  hex64(p.image_sha256) && hex64(p.card_sha256) && p.zip_size > 0 && p.recipe >= 1);
    }

    // MCARD-18: a card too small for FAT32.
    {
        const std::string c2 = tmp_path("card-small.img");
        std::remove(c2.c_str());
        minimal_card::Expect e = ex0;
        e.card_sectors = 32 * 2048;
        const Run r = build(minimal_card::Input::Zip, zip, c2, e);
        check("MCARD-18", "a 32 MiB card: refused 'too few clusters for FAT32', no output file",
              r.st == minimal_card::Status::Error && r.err.find("too few clusters for FAT32") != std::string::npos && !exists(c2), r.err);
        std::remove(c2.c_str());
    }

    // MCARD-19: a wrong IMAGE digest is refused before anything is written (the twin of MCARD-07).
    {
        const std::string c2 = tmp_path("card-wrongimg.img");
        std::remove(c2.c_str());
        minimal_card::Expect e = ex0;
        e.image_sha256 = std::string(64, '3');
        e.file = "sn-emulator-test.zip";
        const Run r = build(minimal_card::Input::Image, im.bytes, c2, e);
        const std::string real = sha_of(im.bytes);
        check("MCARD-19", "Input::Image with a wrong image sha256: refused naming both digests and the file, no output file",
              r.st == minimal_card::Status::Error && r.err.find(real) != std::string::npos &&
                  r.err.find(e.image_sha256) != std::string::npos && r.err.find("sn-emulator-test.zip") != std::string::npos &&
                  !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-20: pinned() takes every field from kDistribution, for both kinds of input.
    {
        const auto& p = minimal_card::kDistribution;
        const minimal_card::Expect z = minimal_card::pinned(minimal_card::Input::Zip);
        const minimal_card::Expect i = minimal_card::pinned(minimal_card::Input::Image);
        check("MCARD-20", "pinned(Zip) carries file, zip sha256, zip size and card sha256 from kDistribution (no image digest); pinned(Image) file, image sha256 and card sha256 (no zip digest or size)",
              z.file == p.file && z.zip_sha256 == p.zip_sha256 && z.zip_size == p.zip_size && z.card_sha256 == p.card_sha256 &&
                  z.image_sha256.empty() && i.file == p.file && i.image_sha256 == p.image_sha256 &&
                  i.card_sha256 == p.card_sha256 && i.zip_sha256.empty() && i.zip_size == 0 &&
                  z.zx81_known_sha256 == sdcard::kZx81Rom16kSha256 && i.card_sectors == 40 * 2048);
    }

    // MCARD-21: chains assembled by index. The fixture's a.sys (a file) and next (a directory) run
    // DOWNWARD; the card equals the one made from the same tree laid out ascending.
    {
        const std::string c2 = tmp_path("card-asc.img");
        std::remove(c2.c_str());
        LayoutOpts o;
        o.reverse.clear();
        const Image asc = build_image(fx, o);
        const Run r = build(minimal_card::Input::Zip, make_zip(asc.bytes), c2, ex0);
        const auto& a = im.chains.at("a.sys");
        const auto& n = im.chains.at("next");
        const bool down = a.size() > 1 && a[1] < a[0] && n.size() > 1 && n[1] < n[0] &&
                          asc.chains.at("a.sys")[1] > asc.chains.at("a.sys")[0];
        check("MCARD-21", "a file and a directory whose chains run downward build the same card as the ascending layout",
              built && down && r.st == minimal_card::Status::Done && r.sha == main_run.sha,
              std::string(down ? "" : "fixture not downward ") + r.err);
        std::remove(c2.c_str());
    }

    // MCARD-22: the partition start is read from the MBR (LBA 2048, as sfdisk and the distribution
    // after 24.11 place it), and partition type 0x0B is taken as FAT32 too.
    {
        const std::string c2 = tmp_path("card-lba.img");
        std::remove(c2.c_str());
        LayoutOpts o;
        o.lba = 2048;
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fx, o).bytes), c2, ex0);
        std::remove(c2.c_str());
        LayoutOpts o2;
        o2.ptype = 0x0B;
        const Run r2 = build(minimal_card::Input::Zip, make_zip(build_image(fx, o2).bytes), c2, ex0);
        std::remove(c2.c_str());
        check("MCARD-22", "a partition at LBA 2048, and one of type 0x0B, build the same card as LBA 63 type 0x0C",
              built && r.st == minimal_card::Status::Done && r.sha == main_run.sha &&
                  r2.st == minimal_card::Status::Done && r2.sha == main_run.sha,
              r.err + "|" + r2.err);
    }

    // MCARD-23: a STORED (method 0) image entry gives the same card as the deflated one (twin of MCARD-12).
    {
        const std::string c2 = tmp_path("card-stored.img");
        std::remove(c2.c_str());
        ZipOpts o;
        o.method = 0;
        const Run r = build(minimal_card::Input::Zip, make_zip(im.bytes, o), c2, ex0);
        check("MCARD-23", "a stored zip of the fixture gives the same card sha256 as the deflated one",
              built && r.st == minimal_card::Status::Done && r.sha == main_run.sha, r.err);
        std::remove(c2.c_str());
    }

    // MCARD-24: two files of one directory claiming the same cluster.
    {
        const std::string c2 = tmp_path("card-shared.img");
        std::remove(c2.c_str());
        LayoutOpts o;
        o.alias["LICENSE.md"] = "README.md";
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fx, o).bytes), c2, ex0);
        check("MCARD-24", "two files sharing a cluster: refused ('shared with another file'), no output file",
              r.st == minimal_card::Status::Error && r.err.find("shared with another file") != std::string::npos && !exists(c2), r.err);
        std::remove(c2.c_str());
    }

    // MCARD-25: a file whose FAT chain ends before its size does.
    {
        const std::string c2 = tmp_path("card-short.img");
        std::remove(c2.c_str());
        LayoutOpts o;
        o.short_chain = "a.sys";
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fx, o).bytes), c2, ex0);
        check("MCARD-25", "a chain shorter than the file's size: refused ('shorter on disk'), no output file",
              r.st == minimal_card::Status::Error && r.err.find("shorter on disk") != std::string::npos && !exists(c2), r.err);
        std::remove(c2.c_str());
    }

    // MCARD-26: the ZIP64 marker in the entry's compressed size (bytes 18..21) and, separately, in its
    // uncompressed size (22..25): each alone is refused.
    {
        const std::string c2 = tmp_path("card-zip64.img");
        bool both = true;
        std::string d;
        for (size_t from : {size_t{18}, size_t{22}}) {
            std::remove(c2.c_str());
            std::vector<uint8_t> z64 = zip;
            for (size_t k = from; k < from + 4; ++k) z64[k] = 0xFF;
            const Run r = build(minimal_card::Input::Zip, z64, c2, ex0);
            if (!(r.st == minimal_card::Status::Error && r.err.find("ZIP64") != std::string::npos && !exists(c2))) {
                both = false;
                d += " field@" + std::to_string(from) + ":" + r.err;
            }
            std::remove(c2.c_str());
        }
        check("MCARD-26", "a ZIP64 size marker in the image entry's compressed or uncompressed size: refused naming ZIP64, no output file",
              both, d);
    }

    // MCARD-27: two root entries of one name (different case): the first wins, as the old script's head -n1.
    {
        const std::string c2 = tmp_path("card-dup.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fixture(true, false, true)).bytes), c2, ex0);
        bool first = false;
        if (r.st == minimal_card::Status::Done) {
            Card c(c2);
            std::vector<uint8_t> d;
            first = c.read("0:/TBBLUE.FW", d) && d == pattern(3000, 1);
        }
        check("MCARD-27", "a duplicate root name: the first entry is the one copied (the card equals the one without the duplicate)",
              r.st == minimal_card::Status::Done && first && r.sha == main_run.sha, r.err);
        std::remove(c2.c_str());
    }

    std::remove(card.c_str());
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
