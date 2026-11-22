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

#include "core/card_extract.h"
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
#include <filesystem>
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

constexpr uint32_t kSpc = 8, kBpc = kSpc * 512, kReserved = 32;

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
FNode fixture(bool with_sys = true, bool big = false, bool dup_root = false, bool with_dot = true) {
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
    if (with_dot) {
        FNode dot = dirn("dot", 0x00, 0x5A32, 0x8E00);           // NextZXOS runs its dot commands from here
        dot.kids.push_back(file("MORE", pattern(1500, 10), 0x20, 0x5A33, 0x9000));
        root.kids.push_back(dot);
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
                     k.name == "dot" || k.name == "LICENSE.md" || k.name == "README.md")) continue;
        const std::string p = prefix + "/" + lower(k.name);
        out[p] = &k;
        if (k.dir) expected_paths(k, p, false, out);
    }
}


// GH #509: fixture() with a GAMES tree on the root (not part of fixture() itself: MCARD-02/13 rest on
// its layout). Mixed case, distinct attributes and times. The nodes are named so that none equals
// "a.sys" or "next" (LayoutOpts::reverse is by exact name).
FNode fixture_games() {
    FNode root = fixture();
    FNode games = dirn("GAMES", 0x00, 0x5A40, 0x9000);
    FNode nx = dirn("NEXT", 0x01, 0x5A41, 0x9200);
    FNode g1 = dirn("G1", 0x00, 0x5A42, 0x9400);
    g1.kids.push_back(file("a.nex", pattern(9000, 40), 0x20, 0x5A43, 0x9600));
    FNode sub = dirn("sub", 0x02, 0x5A44, 0x9800);
    sub.kids.push_back(file("b.bin", pattern(5000, 41), 0x21, 0x5A45, 0x9A00));
    g1.kids.push_back(sub);
    nx.kids.push_back(g1);
    FNode g2 = dirn("g2", 0x00, 0x5A46, 0x9C00);
    g2.kids.push_back(file("c.nex", pattern(4500, 42), 0x20, 0x5A47, 0x9E00));
    nx.kids.push_back(g2);
    games.kids.push_back(nx);
    FNode classic = dirn("CLASSIC", 0x00, 0x5A48, 0xA000);
    classic.kids.push_back(file("d.tap", pattern(3000, 43), 0x20, 0x5A49, 0xA200));
    games.kids.push_back(classic);
    root.kids.push_back(games);
    return root;
}

bool is_boot_name(const std::string& n) {
    const std::string l = lower(n);
    return l == "tbblue.fw" || l == "machines" || l == "nextzxos" || l == "sys" || l == "dot" || l == "license.md" || l == "readme.md";
}

// What a card with the canonical requested `dirs` holds: the boot set, and each requested path with
// its subtree and its ancestors (keys "0:/lower/case/path").
void want_list(const FNode& n, const std::string& prefix, const std::vector<std::string>& dirs, bool top, bool whole,
               std::map<std::string, const FNode*>& out) {
    for (const auto& k : n.kids) {
        const std::string p = prefix + "/" + lower(k.name);
        bool keep = whole || (top && is_boot_name(k.name));
        bool partial = false, all = keep;
        for (const std::string& d : dirs) {
            const std::string rel = p.substr(2);   // drop "0:"
            if (d == rel) { keep = true; all = true; }
            else if (k.dir && d.size() > rel.size() && d.compare(0, rel.size(), rel) == 0 && d[rel.size()] == '/') { keep = true; partial = true; }
        }
        if (!keep) continue;
        out[p] = &k;
        if (k.dir) want_list(k, p, dirs, false, all && !partial, out);
    }
}

// The card at `path` against the fixture `fx` for the (canonical) `dirs`: "" when the file set, every
// file's bytes, and every file's and directory's date, time and R/H/S/A bits equal the fixture's.
std::string verify_list(const std::string& path, const FNode& fx, const std::vector<std::string>& dirs,
                        const std::vector<uint8_t>& rom16) {
    std::map<std::string, const FNode*> want;
    want_list(fx, "0:", dirs, true, false, want);
    Card c(path);
    if (!c.ok) return " card does not mount";
    std::map<std::string, FILINFO> got;
    c.walk("0:", got);
    std::set<std::string> have, expect;
    for (auto& kv : got) have.insert(kv.first);
    for (auto& kv : want) expect.insert(kv.first);
    expect.insert("0:/machines/next/config.ini");
    std::string d;
    for (const auto& h : have) if (!expect.count(h)) d += " extra:" + h;
    for (const auto& e : expect) if (!have.count(e)) d += " missing:" + e;
    for (auto& kv : want) {
        FILINFO fi{};
        if (f_stat(kv.first.c_str(), &fi) != FR_OK) { d += " nostat:" + kv.first; continue; }
        const uint8_t mask = AM_RDO | AM_HID | AM_SYS | AM_ARC;
        if (fi.fdate != kv.second->wdate || fi.ftime != kv.second->wtime || (fi.fattrib & mask) != (kv.second->attr & mask))
            d += " meta:" + kv.first;
        if (kv.second->dir) continue;
        std::vector<uint8_t> got_data, want_data = kv.second->data;
        if (kv.first == "0:/machines/next/zx81.rom") want_data.insert(want_data.end(), rom16.begin(), rom16.end());
        if (!c.read(kv.first, got_data) || got_data != want_data) d += " differs:" + kv.first;
    }
    return d;
}

// The bytes the builder must count for `dirs`: file sizes plus directory clusters (by the image's layout).
uint64_t counted(const FNode& fx, const Image& im, const std::vector<std::string>& dirs) {
    std::map<std::string, const FNode*> want;
    want_list(fx, "0:", dirs, true, false, want);
    uint64_t t = im.chains.at("<root>").size() * static_cast<uint64_t>(kBpc);
    for (auto& kv : want) {
        const FNode& n = *kv.second;
        t += n.dir ? im.chains.at(n.name).size() * static_cast<uint64_t>(kBpc) : (n.declared ? n.declared : n.data.size());
    }
    return t;
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
        std::string content;
        {   // closed before the remove below: an open file cannot be removed on Windows (wine included), and the card stays in /tmp
            std::ifstream f(c2, std::ios::binary);
            content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
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


    // ---- GH #509: extra distribution paths (Expect::dirs) ------------------------------------------------
    const FNode fxg = fixture_games();
    const Image img_g = build_image(fxg);
    const std::vector<uint8_t> zip_g = make_zip(img_g.bytes);
    auto with_dirs = [&](std::vector<std::string> d) { minimal_card::Expect e = ex0; e.dirs = std::move(d); return e; };
    auto canon = [](std::vector<std::string> in) {
        std::vector<std::string> out;
        std::string why;
        minimal_card::canonical_dirs(in, out, why);
        return out;
    };

    // MCARD-28: a whole requested directory.
    {
        const std::string c2 = tmp_path("card-d1.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/APPS"}));
        const std::string d = r.st == minimal_card::Status::Done ? verify_list(c2, fxg, canon({"/APPS"}), rom16) : " " + r.err;
        bool x_ok = false;
        {   // closed before the remove below (an open file cannot be removed on Windows)
            Card c(c2);
            std::vector<uint8_t> x;
            x_ok = c.read("0:/APPS/x.bin", x) && x == std::vector<uint8_t>(2 << 20, 0x5A);
        }
        check("MCARD-28", "dirs {/APPS}: the card holds the boot set, config.ini and APPS/x.bin, byte-identical, with the fixture's attributes and times",
              d.empty() && x_ok, d);
        std::remove(c2.c_str());
    }

    // MCARD-29: a path below the root: its parents are created, their other children are not.
    {
        const std::string c2 = tmp_path("card-d2.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/GAMES/NEXT/G1"}));
        std::string d = r.st == minimal_card::Status::Done ? verify_list(c2, fxg, canon({"/GAMES/NEXT/G1"}), rom16) : " " + r.err;
        bool shape = false;
        if (r.st == minimal_card::Status::Done) {
            Card c(c2);
            FILINFO fi{};
            shape = f_stat("0:/GAMES/NEXT/G1/sub/b.bin", &fi) == FR_OK && f_stat("0:/GAMES/NEXT/g2", &fi) != FR_OK &&
                    f_stat("0:/GAMES/CLASSIC", &fi) != FR_OK && f_stat("0:/APPS", &fi) != FR_OK;
        }
        check("MCARD-29", "dirs {/GAMES/NEXT/G1}: GAMES and GAMES/NEXT exist with their own attributes and times, G1's subtree is complete, G2, CLASSIC and APPS are absent",
              d.empty() && shape, d);
        std::remove(c2.c_str());
    }

    // MCARD-30: case, a trailing slash and the order of the list do not change the card.
    {
        const std::string c1 = tmp_path("card-d3a.img"), c2 = tmp_path("card-d3b.img");
        std::remove(c1.c_str());
        std::remove(c2.c_str());
        const Run a = build(minimal_card::Input::Zip, zip_g, c1, with_dirs({"/games/next/g1/", "/APPS"}));
        const Run b = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/APPS", "/GAMES/NEXT/G1"}));
        check("MCARD-30", "{/games/next/g1/, /APPS} and {/APPS, /GAMES/NEXT/G1} give the same card sha256",
              a.st == minimal_card::Status::Done && b.st == minimal_card::Status::Done && !a.sha.empty() && a.sha == b.sha,
              a.err + b.err);
        std::remove(c1.c_str());
        std::remove(c2.c_str());
    }

    // MCARD-31: a path not in the distribution is refused, naming every missing one.
    {
        const std::string c2 = tmp_path("card-d4.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/GAMES/NOPE", "/NOPE2", "/APPS"}));
        check("MCARD-31", "{/GAMES/NOPE, /NOPE2, /APPS}: refused naming /games/nope and /nope2 (not /apps), no output file",
              r.st == minimal_card::Status::Error && r.err.find("/games/nope") != std::string::npos &&
                  r.err.find("/nope2") != std::string::npos && r.err.find("/apps") == std::string::npos && !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-32: over the bound with a list: the refusal names the exact total and the limit.
    {
        const std::string c2 = tmp_path("card-d5.img");
        std::remove(c2.c_str());
        FNode fb = fixture();
        FNode games = dirn("GAMES", 0x00, 0x5A40, 0x9000);
        FNode big = dirn("big", 0x00, 0x5A41, 0x9200);
        FNode f = file("huge.bin", {}, 0x20, 0x5A42, 0x9400);
        f.declared = 33ull << 20;
        big.kids.push_back(f);
        games.kids.push_back(big);
        fb.kids.push_back(games);
        const Image bi = build_image(fb);
        const uint64_t total = counted(fb, bi, canon({"/GAMES/big"}));
        const Run r = build(minimal_card::Input::Image, bi.bytes, c2, with_dirs({"/GAMES/big"}));
        // The same image cut before the huge file's data: past the bound files are counted, not
        // waited for, so the exact total is still reported (not "at least").
        const Image bt = build_image(fb, [] { LayoutOpts o; o.trim_before = "huge.bin"; return o; }());
        const Run rt = build(minimal_card::Input::Image, bt.bytes, c2, with_dirs({"/GAMES/big"}));
        check("MCARD-32", "a requested directory holding a 33 MiB file: refused, the message names the exact counted total (file sizes plus directory clusters) and 33554432, no output file; the same with the data cut off still gives the exact total",
              r.st == minimal_card::Status::Error && r.err.find("requested paths") != std::string::npos &&
                  r.err.find(": " + std::to_string(total) + " bytes") != std::string::npos &&
                  r.err.find("33554432") != std::string::npos && !exists(c2) &&
                  rt.st == minimal_card::Status::Error && rt.err.find(": " + std::to_string(total) + " bytes") != std::string::npos &&
                  rt.err.find("at least") == std::string::npos,
              r.err + " | " + rt.err + " expect " + std::to_string(total));
        std::remove(c2.c_str());
    }

    // MCARD-33: the bound counts file sizes, not the source's clusters.
    {
        const std::string c2 = tmp_path("card-d6.img");
        std::remove(c2.c_str());
        FNode fm = fixture();
        FNode games = dirn("GAMES", 0x00, 0x5A40, 0x9000);
        FNode many = dirn("many", 0x00, 0x5A41, 0x9200);
        for (int i = 0; i < 8400; ++i) {
            char nm[16];
            std::snprintf(nm, sizeof nm, "f%04d.bin", i);
            many.kids.push_back(file(nm, std::vector<uint8_t>(1, static_cast<uint8_t>(i)), 0x20, 0x5A42, 0x9400));
        }
        games.kids.push_back(many);
        fm.kids.push_back(games);
        const Run r = build(minimal_card::Input::Image, build_image(fm).bytes, c2, with_dirs({"/GAMES/many"}));
        size_t n = 0;
        bool sample = false;
        if (r.st == minimal_card::Status::Done) {
            Card c(c2);
            std::map<std::string, FILINFO> got;
            c.walk("0:/GAMES/many", got);
            n = got.size();
            std::vector<uint8_t> x;
            sample = c.read("0:/GAMES/many/f8399.bin", x) && x.size() == 1 && x[0] == static_cast<uint8_t>(8399);
        }
        check("MCARD-33", "8400 one-byte files in a requested directory (34 MiB in 4 KiB clusters, 8 KiB of data): accepted, every file present",
              r.st == minimal_card::Status::Done && n == 8400 && sample, r.err + " n=" + std::to_string(n));
        std::remove(c2.c_str());
    }

    // MCARD-34: canonical_dirs refuses each bad entry with its own reason naming the entry.
    {
        struct C { std::vector<std::string> in; const char* token; };
        const C cases[] = {{{"GAMES"}, "'GAMES'"}, {{"/"}, "'/'"}, {{""}, "''"}, {{"/A//B"}, "'/A//B'"},
                           {{"/A/../B"}, "'/A/../B'"}, {{"/A/./B"}, "'/A/./B'"}, {{"/A\\B"}, "'/A\\B'"},
                           {{std::string("/A\x01")}, "control"}, {{"/SYS"}, "'/SYS'"}, {{"/machines/next"}, "'/machines/next'"},
                           {{"/TBBLUE.FW"}, "'/TBBLUE.FW'"}, {{"/APPS", "/apps"}, "'/apps'"},
                           {{"/GAMES", "/GAMES/NEXT"}, "'/games/next'"},
                           {{"/GAMES", "/GAMES-X", "/GAMES/NEXT"}, "'/games/next'"}};
        std::string bad;
        std::set<std::string> reasons;
        for (const C& c : cases) {
            std::vector<std::string> out;
            std::string why;
            if (minimal_card::canonical_dirs(c.in, out, why) || why.find(c.token) == std::string::npos) bad += " " + std::string(c.token) + ":" + why;
            reasons.insert(why.substr(why.find("' ") == std::string::npos ? 0 : why.find("' ")));
        }
        // the Builder refuses a bad list on its first feed, writing nothing
        const std::string c2 = tmp_path("card-d7.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/SYS"}));
        check("MCARD-34", "canonical_dirs refuses GAMES, /, '', /A//B, /A/../B, /A/./B, a backslash, a control character, /SYS, /machines/next, /TBBLUE.FW, a duplicate and a nested path (also with a sibling sorting between them), each naming the entry; the Builder refuses a bad list with no output file",
              bad.empty() && reasons.size() >= 8 && r.st == minimal_card::Status::Error && r.err.find("/SYS") != std::string::npos && !exists(c2),
              bad + " " + r.err);
    }

    // MCARD-40: a missing DOT is named (the twin of MCARD-09): NextZXOS runs its dot commands from it.
    {
        const std::string c2 = tmp_path("card-nodot.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, make_zip(build_image(fixture(true, false, false, false)).bytes), c2, ex0);
        check("MCARD-40", "a fixture without DOT: refused naming DOT, no output file",
              r.st == minimal_card::Status::Error && r.err.find("DOT is not in the distribution's SD image") != std::string::npos &&
                  !exists(c2),
              r.err);
        std::remove(c2.c_str());
    }

    // MCARD-41: /DOT is part of the boot set in a page's list, as /SYS is (MCARD-34).
    {
        std::string bad;
        for (const char* d : {"/DOT", "/dot/extra"}) {
            std::vector<std::string> out;
            std::string why;
            if (minimal_card::canonical_dirs({d}, out, why) || why.find(std::string("'") + d + "'") == std::string::npos ||
                why.find("is part of the boot set") == std::string::npos) bad += std::string(" ") + d + ":" + why;
        }
        const std::string c2 = tmp_path("card-d9.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Zip, zip_g, c2, with_dirs({"/DOT"}));
        check("MCARD-41", "canonical_dirs refuses /DOT and /dot/extra as part of the boot set, each quoted; the Builder given /DOT ends in Error naming /DOT with no output file",
              bad.empty() && r.st == minimal_card::Status::Error && r.err.find("/DOT") != std::string::npos && !exists(c2),
              bad + " " + r.err);
    }

    // MCARD-35: the canonical form, and an empty list is the plain card.
    {
        std::vector<std::string> out;
        std::string why;
        const bool ok = minimal_card::canonical_dirs({"/GAMES/", "/apps"}, out, why) &&
                        out == std::vector<std::string>{"/apps", "/games"};
        const std::string c1 = tmp_path("card-d8a.img"), c2 = tmp_path("card-d8b.img");
        std::remove(c1.c_str());
        std::remove(c2.c_str());
        minimal_card::Expect pinned_style = ex0;
        pinned_style.card_sha256 = main_run.sha;
        const Run a = build(minimal_card::Input::Zip, zip, c1, pinned_style);   // dirs empty: the pin still applies
        minimal_card::Expect wrong = pinned_style;
        wrong.card_sha256 = std::string(64, '1');
        const Run b = build(minimal_card::Input::Zip, zip, c2, wrong);
        check("MCARD-35", "canonical_dirs({/GAMES/, /apps}) = {/apps, /games}; with no dirs the card equals the plain one, its pinned sha256 is still checked and a wrong one refused",
              ok && a.st == minimal_card::Status::Done && a.sha == main_run.sha &&
                  b.st == minimal_card::Status::Error && b.err.find("not the expected one") != std::string::npos,
              why + a.err + b.err);
        std::remove(c1.c_str());
        std::remove(c2.c_str());
    }


    // MCARD-36: a directory chain far over the cap is refused from feed(), before any of it is read.
    {
        // An invented image: MBR, BPB, and a FAT whose root directory is a chain of 2000 clusters of 32 KiB
        // (65 MB, twice the cap). Nothing after the first data byte is ever fed.
        const uint32_t nclu = 2000, spc = 64, reserved = 32, fat_sectors = 16, lba = 63;
        const uint64_t part = static_cast<uint64_t>(lba) * 512;
        const uint64_t data_off = part + static_cast<uint64_t>(reserved + 2 * fat_sectors) * 512;
        std::vector<uint8_t> b(static_cast<size_t>(data_off) + 512, 0);
        const uint32_t total = reserved + 2 * fat_sectors + nclu * spc;
        b[0x1BE] = 0x80; b[0x1BE + 4] = 0x0C; w32(b, 0x1BE + 8, lba); w32(b, 0x1BE + 12, total);
        b[510] = 0x55; b[511] = 0xAA;
        const size_t v = static_cast<size_t>(part);
        w16(b, v + 11, 512); b[v + 13] = spc; w16(b, v + 14, reserved); b[v + 16] = 2;
        w32(b, v + 32, total); w32(b, v + 36, fat_sectors); w32(b, v + 44, 2);
        b[v + 510] = 0x55; b[v + 511] = 0xAA;
        const size_t fat0 = v + static_cast<size_t>(reserved) * 512;
        for (uint32_t c = 2; c < 2 + nclu; ++c) w32(b, fat0 + 4 * static_cast<size_t>(c), c + 1 < 2 + nclu ? c + 1 : 0x0FFFFFFF);
        const std::string c2 = tmp_path("card-d9.img");
        std::remove(c2.c_str());
        minimal_card::Builder bd(minimal_card::Input::Image, c2, ex0);
        const minimal_card::Status st = bd.feed(b.data(), b.size());
        check("MCARD-36", "a root directory chain of 65 MB (twice the cap): refused from feed() with the exact-size message, before its data is read, no output file",
              st == minimal_card::Status::Error && bd.error().find("larger than the card") != std::string::npos && !exists(c2),
              bd.error());
        std::remove(c2.c_str());
    }

    // MCARD-37: the cap is straddled: exactly 33554432 counted bytes is accepted, one more is refused naming it.
    {
        auto tree = [](uint64_t sz) {
            FNode fb = fixture();
            FNode games = dirn("GAMES", 0x00, 0x5A40, 0x9000);
            FNode edge = dirn("edge", 0x00, 0x5A41, 0x9200);
            FNode f = file("e.bin", {}, 0x20, 0x5A42, 0x9400);
            f.declared = sz;
            edge.kids.push_back(f);
            games.kids.push_back(edge);
            fb.kids.push_back(games);
            return fb;
        };
        const uint64_t cap = 32ull << 20, probe = 1ull << 20;
        const FNode f0 = tree(probe);
        const uint64_t fixed = counted(f0, build_image(f0), canon({"/GAMES/edge"})) - probe;   // everything but e.bin
        const std::string c1 = tmp_path("card-d10a.img"), c2 = tmp_path("card-d10b.img");
        std::remove(c1.c_str());
        std::remove(c2.c_str());
        const FNode fa = tree(cap - fixed), fo = tree(cap - fixed + 1);
        const Run ra = build(minimal_card::Input::Image, build_image(fa).bytes, c1, with_dirs({"/GAMES/edge"}));
        const Run ro = build(minimal_card::Input::Image, build_image(fo).bytes, c2, with_dirs({"/GAMES/edge"}));
        check("MCARD-37", "a list counting exactly 33554432 bytes is accepted; one byte more is refused naming 33554433, no output file",
              ra.st == minimal_card::Status::Done && ro.st == minimal_card::Status::Error &&
                  ro.err.find(": " + std::to_string(cap + 1) + " bytes") != std::string::npos && !exists(c2),
              ra.err + " | " + ro.err);
        std::remove(c1.c_str());
        std::remove(c2.c_str());
    }


    // MCARD-38: past the cap what is HELD counts, not the directories alone: a 31 MiB boot-set file registers
    // under the cap, then a 30 MiB directory chain takes the total over it and is refused from feed().
    {
        const uint32_t spc = 64, reserved = 32, fat_sectors = 16, lba = 63, nf = 992, nd = 960;
        const uint32_t nclu = 1 + nf + nd;
        const uint64_t part = static_cast<uint64_t>(lba) * 512;
        const uint64_t data_off = part + static_cast<uint64_t>(reserved + 2 * fat_sectors) * 512;
        std::vector<uint8_t> b(static_cast<size_t>(data_off) + 32768, 0);   // the root cluster is the only data
        const uint32_t total = reserved + 2 * fat_sectors + nclu * spc;
        b[0x1BE] = 0x80; b[0x1BE + 4] = 0x0C; w32(b, 0x1BE + 8, lba); w32(b, 0x1BE + 12, total);
        b[510] = 0x55; b[511] = 0xAA;
        const size_t v = static_cast<size_t>(part);
        w16(b, v + 11, 512); b[v + 13] = spc; w16(b, v + 14, reserved); b[v + 16] = 2;
        w32(b, v + 32, total); w32(b, v + 36, fat_sectors); w32(b, v + 44, 2);
        b[v + 510] = 0x55; b[v + 511] = 0xAA;
        const size_t fat0 = v + static_cast<size_t>(reserved) * 512;
        auto fat = [&](uint32_t c, uint32_t val) { w32(b, fat0 + 4 * static_cast<size_t>(c), val); };
        fat(2, 0x0FFFFFFF);
        for (uint32_t i = 0; i < nf; ++i) fat(3 + i, i + 1 < nf ? 4 + i : 0x0FFFFFFF);
        for (uint32_t i = 0; i < nd; ++i) fat(3 + nf + i, i + 1 < nd ? 4 + nf + i : 0x0FFFFFFF);
        auto entry = [&](size_t at, const char* name11, uint8_t attr, uint32_t cluster, uint32_t size) {
            uint8_t* e = &b[static_cast<size_t>(data_off) + at];
            std::memcpy(e, name11, 11);
            e[11] = attr;
            w16(b, static_cast<size_t>(data_off) + at + 26, cluster & 0xFFFF);
            w16(b, static_cast<size_t>(data_off) + at + 20, cluster >> 16);
            w32(b, static_cast<size_t>(data_off) + at + 28, size);
        };
        entry(0, "TBBLUE  FW ", 0x20, 3, nf * 32768u - 100);
        entry(32, "SYS        ", 0x10, 3 + nf, 0);
        const std::string c2 = tmp_path("card-d11.img");
        std::remove(c2.c_str());
        minimal_card::Builder bd(minimal_card::Input::Image, c2, ex0);
        const minimal_card::Status st = bd.feed(b.data(), b.size());
        check("MCARD-38", "a 31 MiB boot-set file registered under the cap, then a 30 MiB directory chain: refused from feed() (held bytes pass the cap together), no output file",
              st == minimal_card::Status::Error && bd.error().find("at least") != std::string::npos && !exists(c2), bd.error());
        std::remove(c2.c_str());
    }

    // MCARD-39: the directories released as they are parsed: past the cap, a chain of 130 nested directories of 66
    // clusters each (35 MB in all, only one live at a time) still gets the exact total, not "at least".
    {
        const int levels = 130, fillers = 400;
        FNode fb = fixture();
        FNode big = dirn("big", 0x00, 0x5A41, 0x9200);
        FNode f = file("huge.bin", {}, 0x20, 0x5A42, 0x9400);
        f.declared = 33ull << 20;
        big.kids.push_back(f);
        fb.kids.push_back(big);
        auto name = [](int i) { char n[8]; std::snprintf(n, sizeof n, "d%03d", i); return std::string(n); };
        // innermost first; each level holds `fillers` zero-size files with 250-character names (not wanted: skipped
        // at no cost) that make its directory 66 clusters
        FNode inner = dirn(name(levels), 0x00, 0x5A43, 0x9600);
        std::string path = "/" + name(levels);
        for (int l = levels - 1; l >= 1; --l) {
            FNode d = dirn(name(l), 0x00, 0x5A43, 0x9600);
            for (int k = 0; k < fillers; ++k) {
                char tail[8];
                std::snprintf(tail, sizeof tail, "%04d", k);
                d.kids.push_back(file(std::string(250, 'x') + tail, {}, 0x20, 0x5A44, 0x9800));
            }
            d.kids.push_back(std::move(inner));
            inner = std::move(d);
            path = "/" + name(l) + path;
        }
        fb.kids.push_back(std::move(inner));
        const Image im = build_image(fb);
        const std::vector<std::string> want = canon({"/big", path});
        const uint64_t total = counted(fb, im, want);
        const std::string c2 = tmp_path("card-d12.img");
        std::remove(c2.c_str());
        const Run r = build(minimal_card::Input::Image, im.bytes, c2, with_dirs({"/big", path}));
        check("MCARD-39", "past the cap, 130 nested directories (35 MB of directory clusters, one live at a time): refused with the exact counted total, not 'at least'",
              r.st == minimal_card::Status::Error && r.err.find(": " + std::to_string(total) + " bytes") != std::string::npos &&
                  r.err.find("at least") == std::string::npos && !exists(c2),
              r.err + " expect " + std::to_string(total));
        std::remove(c2.c_str());
    }

    // ---- GH #510: a program's directory copied out of the card (card_extract) -------------------------------------
    // The card is the Builder's from the synthetic distribution, with /GAMES on it. G1 gets, besides a.nex (9000 bytes,
    // over two 4 KiB clusters) and sub/b.bin, a long mixed-case name, an empty file and an empty directory named x.nex;
    // g2 and CLASSIC are the siblings that must not be copied. The oracle is the fixture's declared contents.
    {
        FNode fxc = fixture_games();
        auto kid = [](FNode& n, const char* name) -> FNode& {
            for (FNode& k : n.kids) if (k.name == name) return k;
            std::abort();
        };
        FNode& g1c = kid(kid(kid(fxc, "GAMES"), "NEXT"), "G1");
        const std::vector<uint8_t> longdata = pattern(70, 77);
        g1c.kids.push_back(file("Long Name Data File.txt", longdata, 0x20, 0x5A50, 0x9000));
        g1c.kids.push_back(file("empty.dat", {}, 0x20, 0x5A51, 0x9000));
        g1c.kids.push_back(dirn("dir.nex", 0x00, 0x5A52, 0x9000));
        const std::vector<uint8_t> zip_c = make_zip(build_image(fxc).bytes);
        const std::string cc = tmp_path("card-cp.img"), dest = tmp_path("cp-dest");
        std::remove(cc.c_str());
        const Run rc = build(minimal_card::Input::Zip, zip_c, cc, with_dirs({"/GAMES"}));
        const bool built = rc.st == minimal_card::Status::Done;
        auto slurp = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        };
        const std::string before = built ? sha_of(slurp(cc)) : "";
        std::filesystem::remove_all(dest);
        std::string out_nex, why;
        const bool ok = built && card_extract::copy_program_dir(cc, "/games/next/g1/A.NEX", dest, out_nex, why);
        std::string diff;
        if (ok) {
            std::map<std::string, std::vector<uint8_t>> want = {
                {"a.nex", pattern(9000, 40)}, {"Long Name Data File.txt", longdata}, {"empty.dat", {}},
                {"sub/b.bin", pattern(5000, 41)}, {"dir.nex/", {}}, {"sub/", {}}};
            std::map<std::string, std::vector<uint8_t>> got;
            for (const auto& e : std::filesystem::recursive_directory_iterator(dest)) {
                std::string rel = std::filesystem::relative(e.path(), dest).generic_string();
                if (e.is_directory()) got[rel + "/"] = {}; else got[rel] = slurp(e.path().string());
            }
            for (const auto& kv : want) { auto it = got.find(kv.first); if (it == got.end() || it->second != kv.second) diff += " bad:" + kv.first; }
            for (const auto& kv : got) if (!want.count(kv.first)) diff += " extra:" + kv.first;
        }
        check("MCARD-CP-01", "copy_program_dir for /games/next/g1/A.NEX (another case than stored): dest holds exactly G1's subtree byte for byte (a.nex, the long mixed-case name, the empty file, the empty directory, sub/b.bin), out_nex is dest/a.nex; g2 and CLASSIC are not copied",
              ok && out_nex == dest + "/a.nex" && diff.empty(), why + diff + (built ? "" : " card: " + rc.err));
        const std::string after = built ? sha_of(slurp(cc)) : "x";
        check("MCARD-CP-02", "the card's SHA-256 is the same after the copy: the card is only read",
              built && !before.empty() && before == after, before + " / " + after);
        std::filesystem::remove_all(dest);

        // CP-03: refusals, each naming its path, and no dest left behind (an existing one untouched).
        std::string bad;
        auto refused = [&](const char* tag, const std::string& card_path, const std::string& nex, const std::string& token) {
            std::filesystem::remove_all(dest);
            std::string o, w;
            const bool r = card_extract::copy_program_dir(card_path, nex, dest, o, w);
            if (r || w.find(token) == std::string::npos || std::filesystem::exists(dest)) bad += std::string(" ") + tag + ":" + w;
        };
        refused("absent", cc, "/GAMES/NEXT/G1/NOPE.nex", "/GAMES/NEXT/G1/NOPE.nex");
        refused("dir", cc, "/GAMES/NEXT/G1/dir.nex", "dir.nex");
        refused("ext", cc, "/GAMES/NEXT/G1/sub/b.bin", "b.bin");
        refused("rel", cc, "GAMES/NEXT/G1/a.nex", "GAMES/NEXT/G1/a.nex");
        // A NEX that really is in the card's root (the boot file TBBLUE.FW renamed in place, on a copy of the card):
        // without the refusal the whole root would be copied.
        auto patch = [&](const std::string& path, const char* pat11, size_t off, const std::vector<uint8_t>& repl) {
            std::vector<uint8_t> b = slurp(path);
            const std::string needle(pat11, 11);
            const auto it = std::search(b.begin(), b.end(), needle.begin(), needle.end());
            if (it == b.end()) return false;
            std::copy(repl.begin(), repl.end(), it + off);
            std::ofstream o(path, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
            return static_cast<bool>(o);
        };
        const std::string croot = tmp_path("card-cproot.img");
        std::filesystem::copy_file(cc, croot, std::filesystem::copy_options::overwrite_existing);
        if (!patch(croot, "TBBLUE  FW ", 8, {'N', 'E', 'X'})) bad += " root: TBBLUE.FW entry not found";
        refused("root", croot, "/TBBLUE.NEX", "is in the card's root");
        std::remove(croot.c_str());
        const std::string zeros = tmp_path("zeros.img");
        { std::ofstream z(zeros, std::ios::binary); std::vector<char> blk(1 << 20, 0); z.write(blk.data(), blk.size()); }
        refused("notfat", zeros, "/GAMES/NEXT/G1/a.nex", zeros);
        std::remove(zeros.c_str());
        std::filesystem::create_directories(dest);
        { std::ofstream m(dest + "/marker"); m << "x"; }
        {
            std::string o, w;
            const bool r = card_extract::copy_program_dir(cc, "/GAMES/NEXT/G1/a.nex", dest, o, w);
            if (r || w.find(dest) == std::string::npos || !exists(dest + "/marker")) bad += " exists:" + w;
        }
        // A failure in the middle of the copy: a chain of 34 nested directories, past the depth limit, found after
        // a.nex and the other files of G1 were already written. Nothing is left behind.
        {
            FNode fxd = fixture_games();
            FNode& g1d = kid(kid(kid(fxd, "GAMES"), "NEXT"), "G1");
            FNode chain = dirn("d34", 0x00, 0x5A60, 0x9000);
            chain.kids.push_back(file("z.txt", pattern(10, 78), 0x20, 0x5A61, 0x9000));
            for (int i = 33; i >= 1; --i) {
                FNode d = dirn("d" + std::to_string(i), 0x00, 0x5A60, 0x9000);
                d.kids.push_back(std::move(chain));
                chain = std::move(d);
            }
            g1d.kids.push_back(std::move(chain));
            const std::string cd = tmp_path("card-cpdeep.img");
            std::remove(cd.c_str());
            const Run rd = build(minimal_card::Input::Zip, make_zip(build_image(fxd).bytes), cd, with_dirs({"/GAMES"}));
            std::filesystem::remove_all(dest);
            std::string o, w;
            const bool r = rd.st == minimal_card::Status::Done && card_extract::copy_program_dir(cd, "/GAMES/NEXT/G1/a.nex", dest, o, w);
            if (rd.st != minimal_card::Status::Done || r || w.find("nested more than") == std::string::npos || std::filesystem::exists(dest))
                bad += " deep:" + w + rd.err;
            std::remove(cd.c_str());
        }
        // The two bounds on the amount copied, each found before the copy is done and so with no destination left:
        // a file whose directory entry declares more than 48 MiB (a short chain behind it: the entry is patched on a copy
        // of the card, no 48 MiB fixture), and a tree of 66,049 entries, past the 65,536 cap.
        {
            FNode fxb = fixture_games();
            FNode& g1b = kid(kid(kid(fxb, "GAMES"), "NEXT"), "G1");
            g1b.kids.push_back(file("big.bin", pattern(100, 79), 0x20, 0x5A70, 0x9000));
            const std::string cb = tmp_path("card-cpbig.img");
            std::remove(cb.c_str());
            const Run rb = build(minimal_card::Input::Zip, make_zip(build_image(fxb).bytes), cb, with_dirs({"/GAMES"}));
            std::filesystem::remove_all(dest);
            std::string o, w;
            bool r = rb.st == minimal_card::Status::Done && patch(cb, "BIG     BIN", 28, {0x00, 0x00, 0x10, 0x03});   // 49 MiB
            r = r && !card_extract::copy_program_dir(cb, "/GAMES/NEXT/G1/a.nex", dest, o, w);
            if (!r || w.find("over 48 MiB") == std::string::npos || std::filesystem::exists(dest)) bad += " size48:" + w + rb.err;
            std::remove(cb.c_str());

            // The cap (4096 entries, files and directories together) and its exact boundary. The program directory G1 is
            // made of p.nex, 16 parent directories and, below them, 4079 empty files (4096 entries in all) or 4080 (4097),
            // or 4096 empty directories (4113 entries, no file but p.nex). Spread over 16 parents so that the Builder's
            // FatFs writes stay quick.
            auto cap_case = [&](const char* tag, int leaves, bool dirs_only, bool want_copy) {
                FNode fx = fixture_games();
                FNode& g1 = kid(kid(kid(fx, "GAMES"), "NEXT"), "G1");
                g1.kids.clear();
                g1.kids.push_back(file("p.nex", pattern(40, 80), 0x20, 0x5A74, 0x9000));
                int left = leaves;
                for (int d = 0; d < 16; ++d) {
                    char dn[16];
                    std::snprintf(dn, sizeof dn, "w%02d", d);
                    FNode wd = dirn(dn, 0x00, 0x5A75, 0x9000);
                    for (int i = 0; i < 256 && left > 0; ++i, --left) {
                        char n[16];
                        std::snprintf(n, sizeof n, dirs_only ? "e%03d" : "f%03d.dat", i);
                        wd.kids.push_back(dirs_only ? dirn(n, 0x00, 0x5A76, 0x9000) : file(n, {}, 0x20, 0x5A77, 0x9000));
                    }
                    g1.kids.push_back(std::move(wd));
                }
                const std::string cn = tmp_path("card-cpcap.img");
                std::remove(cn.c_str());
                const Run rn = build(minimal_card::Input::Zip, make_zip(build_image(fx).bytes), cn, with_dirs({"/GAMES"}));
                std::filesystem::remove_all(dest);
                std::string w2, o2;
                const bool copied = rn.st == minimal_card::Status::Done && card_extract::copy_program_dir(cn, "/GAMES/NEXT/G1/p.nex", dest, o2, w2);
                const bool gone = !std::filesystem::exists(dest);
                if (rn.st != minimal_card::Status::Done) bad += std::string(" ") + tag + ":card:" + rn.err;
                else if (want_copy ? (!copied || gone) : (copied || w2.find("more than 4096 entries") == std::string::npos || !gone))
                    bad += std::string(" ") + tag + ":" + w2;
                std::remove(cn.c_str());
            };
            cap_case("boundary-ok", 4079, false, true);      // 1 + 16 + 4079 = 4096 entries: copied
            cap_case("boundary-over", 4080, false, false);   // 4097: refused
            cap_case("dirs-only", 4096, true, false);        // 1 + 16 + 4096 = 4113, directories only: refused
        }
        std::filesystem::remove_all(dest);
        check("MCARD-CP-03", "copy_program_dir refuses a NEX that is not on the card, a directory named x.nex, a path not ending .nex, a relative path, a NEX that is in the card's root, a zero-filled image, a directory nested past the depth limit (a failure in the middle of the copy), a file declaring over 48 MiB, a program directory of exactly 4096 entries (copied) and of 4097 and of 4113 directories-only entries (refused, over the 4096 cap), and an existing dest (left untouched), each naming its path, with no dest left behind",
              built && bad.empty(), bad);
        std::remove(cc.c_str());
    }

    std::remove(card.c_str());
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
