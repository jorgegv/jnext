// Programs inside .zip archives (GH #392): the interchange ZIP reader
// (save/zip_interchange.h) and the program-archive layer (core/program_archive.h).
//
// No VHDL oracle: a .zip is a host-side container the FPGA core never sees. The
// oracle is PKWARE APPNOTE.TXT 6.3.x (doc/REFERENCES.md):
//   4.3.7 local file header, 4.3.9 data descriptor, 4.3.12 central header,
//   4.3.14-4.3.16 ZIP64 EOCD / locator / EOCD,
//   4.4.4 general purpose flag (bit 0 encrypted, 1-2 deflate options, 3 data
//         descriptor, 6 strong encryption, 11 UTF-8),
//   4.4.5 methods (0 stored, 8 deflate, 9 deflate64, 12 bzip2, 14 LZMA, 93 zstd, 99 AES),
//   4.4.8/4.4.9 sizes, 4.4.17 file names (no drive, no leading slash, forward slashes).
// Expected bytes come from the committed fixtures' sources (our own demo
// programs under test/00regression) and `unzip -v` (CRCs). The fixtures in
// test/fixtures/zip are written by FOREIGN writers (Info-ZIP 3.0, CPython
// zipfile; make-fixtures.sh); every refusal is built in code (zip_fixture.h).
//
// Run: ./build/test/program_archive_test

#include "core/host_path.h"
#include "core/program_archive.h"
#include "save/zip_archive.h"
#include "save/zip_interchange.h"

#include "../row_id.h"
#include "../test_portable.h"
#include "../zip_fixture.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef JNEXT_ZIP_FIXTURE_DIR
#error "JNEXT_ZIP_FIXTURE_DIR must be defined by test/CMakeLists.txt"
#endif

namespace fs = std::filesystem;
using jnext::zip::InterchangeEntry;
using jnext::zip::InterchangeReader;
using zipfx::Bytes;
using zipfx::Member;

namespace {

int passed = 0, failed = 0, total = 0;

void check(const char* id, const char* desc, bool condition, const std::string& detail = {}) {
    report_row_id(id);
    ++total;
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

const std::string kFix = JNEXT_ZIP_FIXTURE_DIR;
const std::string kTapDir = kFix + "/../../00regression/tap/";
const std::string kNexDir = kFix + "/../../00regression/nex/";

Bytes slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void spit(const std::string& path, const Bytes& b) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes pattern(size_t n) {
    Bytes b(n);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(i * 7 + 3);
    return b;
}

const InterchangeEntry* entry(const InterchangeReader& r, const std::string& name) {
    for (const auto& e : r.entries()) if (e.name == name) return &e;
    return nullptr;
}

// Open `buf` into `r`; true on success.
bool open_buf(InterchangeReader& r, const Bytes& buf, std::string& why) {
    return r.open(buf.data(), buf.size(), why);
}

// Open then read the first member; `why` is whichever step refused.
bool read_first(const Bytes& buf, Bytes& out, std::string& why, uint64_t cap = jnext::zip::kMaxInterchangeMemberBytes) {
    InterchangeReader r(cap);
    if (!open_buf(r, buf, why)) return false;
    if (r.entries().empty()) { why = "no entries"; return false; }
    return r.read(r.entries()[0], out, why);
}

// Open must be refused; returns the reason ("" when it was accepted).
std::string open_refusal(const Bytes& buf) {
    InterchangeReader r;
    std::string why;
    return open_buf(r, buf, why) ? std::string() : why;
}

Member mem(const std::string& name, const Bytes& data, uint16_t method = 8) {
    Member m;
    m.name = name;
    m.data = data;
    m.method = method;
    return m;
}

size_t count_named(const fs::path& root, const std::string& name) {
    size_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename() == name) ++n;
    return n;
}

}  // namespace

// GH #541 data-archive rows, apart from main() so that function stays inside the WebAssembly
// function-size bound (developer guide, section 4.7).
[[gnu::noinline]] static void run_data_archive_rows(const std::string& root, const Bytes& lores) {
    namespace pa = program_archive;
    std::error_code ec;
    // --------------------------------------------- data archives (GH #541)
    // Oracle: the issue's rules (a NEX archive serves its directory) and APPNOTE 4.4.17
    // for names; bytes are the fixtures' own. Every refusal happens before a write.
    {
        const std::string dd = root + "/zdir";
        fs::create_directories(dd);
        int zn = 0;
        // Write a zip of `ms` and open it as a ProgramArchive; `why` empty on success.
        auto open_zip = [&](pa::ProgramArchive& a, const std::vector<Member>& ms, std::string& why) {
            const std::string f = dd + "/z" + std::to_string(++zn) + ".zip";
            spit(f, zipfx::build(ms));
            return a.open(f, why);
        };
        auto tree_files = [](const fs::path& r) {
            size_t n = 0;
            std::error_code e;
            for (fs::recursive_directory_iterator it(r, e), end; !e && it != end; it.increment(e)) ++n;
            return n;
        };
        const Bytes nexb = pattern(40), l1 = pattern(33), sfx = pattern(21);
        auto clean = [](const std::string& m) {
            for (unsigned char c : m) if (c < 0x20 || c >= 0x7F) return false;
            return true;
        };

        {
            pa::ProgramArchive a; std::string why, mem_;
            const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1)}, why);
            const bool ch = ok && a.choose(mem_, why);
            check("ZDIR-01", "game/G.NEX with game/data/L1.BIN: choose() is the NEX and it has data files",
                  ch && mem_ == "game/G.NEX" && a.has_data_files("game/G.NEX"), why);
        }
        {
            pa::ProgramArchive a; std::string why, mem_;
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("data/SFX.WAV", sfx), mem("lv/X.TAP", sfx)}, why);
            check("ZDIR-02", "a NEX with other programs only in subdirectories below it: choose() is the NEX",
                  ok && a.choose(mem_, why) && mem_ == "G.NEX", why);
        }
        {
            pa::ProgramArchive a; std::string why, mem_;
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("G.TAP", sfx)}, why);
            const bool ch = ok && a.choose(mem_, why);
            check("ZDIR-03", "a NEX and a TAP in the same directory are refused with the several-programs text",
                  ok && !ch && mem_.empty() && why == a.several_why() && has(why, "G.NEX") && has(why, "G.TAP"), why);
        }
        {
            pa::ProgramArchive a, b; std::string w1, w2, m1, m2;
            const bool o1 = open_zip(a, {mem("A.NEX", nexb), mem("B.NEX", nexb)}, w1);
            const bool o2 = open_zip(b, {mem("game/A.NEX", nexb), mem("B.TAP", sfx)}, w2);
            check("ZDIR-04", "two NEX in one directory, and a TAP outside the NEX's directory, are refused",
                  o1 && o2 && !a.choose(m1, w1) && !b.choose(m2, w2) && has(w1, "holds 2 programs") &&
                      has(w2, "holds 2 programs"),
                  w1 + " | " + w2);
        }
        {
            pa::ProgramArchive a1, a2, a3, a4; std::string w;
            const bool o1 = open_zip(a1, {mem("G.NEX", nexb)}, w);
            const bool o2 = open_zip(a2, {mem("game/G.NEX", nexb), mem("README.TXT", sfx)}, w);
            const bool o3 = open_zip(a3, {mem("G.NEX", nexb), mem("__MACOSX/x", sfx), mem("._G.NEX", sfx)}, w);
            const bool o4 = open_zip(a4, {mem("G.TAP", nexb), mem("data.bin", sfx)}, w);
            check("ZDIR-05", "has_data_files() is false for a NEX alone, a NEX with a file above it, a NEX with only junk beside it, and a TAP",
                  o1 && o2 && o3 && o4 && !a1.has_data_files("G.NEX") && !a2.has_data_files("game/G.NEX") &&
                      !a3.has_data_files("G.NEX") && !a4.has_data_files("G.TAP"));
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr6");
            const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1), mem("README.TXT", sfx)}, why);
            const bool ex = ok && a.extract_program("game/G.NEX", sc, out, why);
            const fs::path slot = ex ? fs::path(out).parent_path() : fs::path();
            check("ZDIR-06", "extract_program() writes the NEX's directory as a tree in one slot, the NEX directly in it, and nothing from above",
                  ex && fs::path(out).filename() == "G.NEX" && slurp(out) == nexb &&
                      slurp((slot / "data" / "L1.BIN").string()) == l1 &&
                      !fs::exists(slot / "README.TXT") && !fs::exists(slot / "game") && tree_files(slot) == 3,
                  why);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr7");
            Member d; d.name = "game/saves/"; d.method = 0;
            const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1), d}, why);
            const bool ex = ok && a.extract_program("game/G.NEX", sc, out, why);
            const fs::path sv = ex ? fs::path(out).parent_path() / "saves" : fs::path();
            check("ZDIR-07", "a directory member becomes an empty directory in the slot",
                  ex && fs::is_directory(sv) && fs::is_empty(sv), why);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            const std::string sdir = dd + "/scr8";
            pa::ScratchDir sc(sdir);
            const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1), mem("../evil.bin", sfx)}, why);
            const bool ex = ok && a.extract_program("game/G.NEX", sc, out, why);
            check("ZDIR-08", "an unsafe member OUTSIDE the NEX's directory refuses the load, naming it; nothing is written anywhere",
                  ok && !ex && has(why, "../evil.bin") && has(why, "unsafe") && out.empty() &&
                      !fs::exists(root + "/evil.bin") && !fs::exists(dd + "/evil.bin") &&
                      count_named(root, "evil.bin") == 0 && !fs::exists(sdir),
                  why);
        }
        {
            const char* bad[] = {"/abs.bin", "C:x.bin", "game\\..\\..\\x.bin"};
            bool all = true;
            std::string seen;
            for (const char* b : bad) {
                pa::ProgramArchive a; std::string why, out;
                pa::ScratchDir sc(dd + "/scr9");
                const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1), mem(b, sfx)}, why);
                const bool ex = ok && a.extract_program("game/G.NEX", sc, out, why);
                all = all && ok && !ex && has(why, "unsafe") && has(why, b == bad[2] ? "game" : b);
                seen += why + " | ";
            }
            check("ZDIR-09", "an absolute path, a drive-letter path and a backslash ..-path anywhere in the archive are each refused as unsafe",
                  all, seen);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr10");
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("data/Level1.bin", l1), mem("DATA/LEVEL1.BIN", sfx)}, why);
            const bool ex = ok && a.extract_program("G.NEX", sc, out, why);
            check("ZDIR-10", "two members whose paths differ only in case are refused naming both, before anything is written",
                  ok && !ex && has(why, "data/Level1.bin") && has(why, "DATA/LEVEL1.BIN") && sc.process_dir().empty(), why);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr11");
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("data", l1), mem("data/x", sfx)}, why);
            const bool ex = ok && a.extract_program("G.NEX", sc, out, why);
            check("ZDIR-11", "a file whose path is a directory of another member is refused naming both",
                  ok && !ex && has(why, "data") && has(why, "data/x") && sc.process_dir().empty(), why);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr12");
            Member big1 = mem("G.NEX", nexb, 0), big2 = mem("data/big.bin", l1, 0);
            big1.usize_override = big2.usize_override = 128ll * 1024 * 1024 + 1;
            const bool ok = open_zip(a, {big1, big2}, why);
            const bool ex = ok && a.extract_program("G.NEX", sc, out, why);
            check("ZDIR-12", "declared sizes summing over 256 MiB are refused by the size check before any member is read or any slot made",
                  ok && !ex && has(why, "268435458") && has(why, "268435456") && !has(why, "CRC") &&
                      sc.process_dir().empty(),
                  why);
        }
        {
            std::string w_ok, w_bad, out1, out2;
            pa::ProgramArchive a, b;
            pa::ScratchDir sc(dd + "/scr13");
            const bool o1 = open_zip(a, {mem("G.NEX", Bytes(4, 1), 0), mem("d/x", Bytes(6, 2), 0)}, w_ok);
            const bool o2 = open_zip(b, {mem("G.NEX", Bytes(4, 1), 0), mem("d/x", Bytes(7, 2), 0)}, w_bad);
            const bool e1 = o1 && a.extract_program("G.NEX", sc, out1, w_ok, 10);
            const bool e2 = o2 && b.extract_program("G.NEX", sc, out2, w_bad, 10);
            check("ZDIR-13", "a declared total of exactly the limit extracts and one byte over is refused",
                  e1 && !e2 && has(w_bad, "11 bytes") && has(w_bad, "10-byte limit"), w_ok + " | " + w_bad);
        }
        {
            const std::string c100(100, 'a'), c100b(100, 'b');
            const std::string ok255 = c100 + "/" + c100b + "/" + std::string(53, 'c');
            const std::string bad256 = c100 + "/" + c100b + "/" + std::string(54, 'c');
            pa::ProgramArchive a, b;
            std::string w1, w2, o1, o2;
            pa::ScratchDir sc(dd + "/scr14");
            const bool p1 = open_zip(a, {mem("G.NEX", nexb), mem(ok255, l1)}, w1);
            const bool p2 = open_zip(b, {mem("G.NEX", nexb), mem(bad256, l1)}, w2);
            const bool e1 = p1 && a.extract_program("G.NEX", sc, o1, w1);
            const bool e2 = p2 && b.extract_program("G.NEX", sc, o2, w2);
            check("ZDIR-14", "a relative path of exactly 255 bytes extracts; 256 is refused naming the member",
                  e1 && !e2 && slurp((fs::path(o1).parent_path() / ok255).string()) == l1 && has(w2, "256 bytes") &&
                      has(w2, std::string(54, 'c').c_str()),
                  w1 + " | " + w2);
        }
        {
            std::string w1, w2, out;
            pa::ProgramArchive a, b;
            pa::ScratchDir sc(dd + "/scr15");
            const bool o1 = open_zip(a, {mem("G.NEX", nexb), mem("save.", l1)}, w1);
            const bool o2 = open_zip(b, {mem("G.NEX", nexb), mem("dir /x", l1)}, w2);
            const bool e1 = o1 && a.extract_program("G.NEX", sc, out, w1);
            const bool e2 = o2 && b.extract_program("G.NEX", sc, out, w2);
            check("ZDIR-15", "a file or directory name ending in a dot or a space is refused naming the member",
                  !e1 && !e2 && has(w1, "save.") && has(w2, "dir /x") && sc.process_dir().empty(), w1 + " | " + w2);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr16");
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("data/a<b>c?.bin", l1), mem("data/CON.BIN", sfx),
                                          mem(std::string("data/e\x1b" "x.bin"), nexb)}, why);
            const bool ex = ok && a.extract_program("G.NEX", sc, out, why);
            const fs::path d = ex ? fs::path(out).parent_path() / "data" : fs::path();
            check("ZDIR-16", "path components are sanitised as file names are: <>?: and control bytes to _, a device name prefixed _",
                  ex && slurp((d / "a_b_c_.bin").string()) == l1 && slurp((d / "_CON.BIN").string()) == sfx &&
                      slurp((d / "e_x.bin").string()) == nexb,
                  why);
        }
        {
            pa::ProgramArchive a; std::string why, out;
            const std::string sdir = dd + "/scr17";
            pa::ScratchDir sc(sdir);
            Member bad = mem("data/L1.BIN", l1);
            bad.crc_override = static_cast<int64_t>(crc32(0, l1.data(), static_cast<uInt>(l1.size()))) + 1;
            const bool ok = open_zip(a, {mem("G.NEX", nexb), bad}, why);
            const bool ex = ok && a.extract_program("G.NEX", sc, out, why);
            check("ZDIR-17", "a data member with a bad CRC refuses the whole load naming it, and its slot is removed",
                  ok && !ex && has(why, "data/L1.BIN") && has(why, "CRC") && out.empty() &&
                      !sc.process_dir().empty() && tree_files(sc.process_dir()) == 0,
                  why);
        }
        {
            pa::ProgramArchive a, b; std::string why, out, arch, arch2;
            pa::ScratchDir sc(dd + "/scr18");
            const bool o1 = open_zip(a, {mem("G.NEX", nexb), mem("sub/X.NEX", nexb), mem("data/L1.BIN", l1)}, why);
            const bool e1 = o1 && a.extract_program("G.NEX", sc, out, why);
            const std::string slot = e1 ? fs::path(out).parent_path().string() : std::string();
            const bool in_a = e1 && pa::in_archive_directory(out, arch);
            const bool in_sub = e1 && pa::in_archive_directory(slot + "/sub/X.NEX", arch2);
            std::string flat, w2;
            const bool o2 = open_zip(b, {mem("F.NEX", nexb)}, w2);
            const bool e2 = o2 && b.extract_program("F.NEX", sc, flat, w2);
            std::string unused;
            check("ZDIR-18", "in_archive_directory() is true inside a data slot (component-wise), false for a flat extraction, an unrelated path and a sibling-named directory",
                  e1 && e2 && in_a && in_sub && arch == jnext::absolute_host_path(dd + "/z" + std::to_string(zn - 1) + ".zip") &&
                      arch2 == arch && !pa::in_archive_directory(flat, unused) &&
                      !pa::in_archive_directory(root + "/elsewhere/G.NEX", unused) &&
                      !pa::in_archive_directory(slot + "X/G.NEX", unused),
                  why + " " + w2);
        }
        {
            // The whole-archive route: a data zip gives the tree, the NEX-only fixture a flat file.
            std::string out, why, member, unused;
            const std::string dz = dd + "/data.zip";
            spit(dz, zipfx::build({mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1)}));
            const bool ok = pa::extract_single_program(dz, out, why, &member);
            std::string flat, w2;
            const bool ok2 = pa::extract_single_program(kFix + "/lores.nex.zip", flat, w2);
            check("ZDIR-19", "extract_single_program() extracts a data zip as a tree and a NEX-only zip as one flat file served no directory",
                  ok && ok2 && member == "game/G.NEX" && pa::in_archive_directory(out, unused) &&
                      slurp((fs::path(out).parent_path() / "data" / "L1.BIN").string()) == l1 &&
                      slurp(flat) == lores && !pa::in_archive_directory(flat, unused),
                  why + " " + w2);
        }
        {
            const std::string e = "\x1b[2J";
            std::vector<std::string> msgs;
            auto refused = [&](const std::vector<Member>& ms, uint64_t cap = pa::kMaxDirectoryBytes) {
                pa::ProgramArchive a; std::string why, out;
                pa::ScratchDir sc(dd + "/scr20");
                if (!open_zip(a, ms, why)) return std::string("OPEN-FAILED " + why);
                if (a.extract_program(ms[0].name, sc, out, why, cap)) return std::string();
                return why;
            };
            msgs.push_back(refused({mem("G" + e + ".NEX", nexb), mem("../" + e, l1)}));
            msgs.push_back(refused({mem("G.NEX", nexb), mem("a" + e + "/x", l1), mem("A" + e + "/X", l1)}));
            msgs.push_back(refused({mem("G.NEX", nexb), mem("a" + e, l1), mem("a" + e + "/x", l1)}));
            msgs.push_back(refused({mem(e + "/G.NEX", nexb), mem(e + "/x", Bytes(8, 1), 0)}, 5));
            msgs.push_back(refused({mem("G.NEX", nexb), mem(std::string(130, 'a') + "/" + e + std::string(130, 'b'), l1)}));
            msgs.push_back(refused({mem("G.NEX", nexb), mem("t" + e + ".", l1)}));
            bool all = true;
            std::string joined;
            for (const auto& m : msgs) { all = all && !m.empty() && clean(m); joined += m + " | "; }
            check("ZDIR-20", "no control byte survives into the unsafe, case-collision, file/directory, size, length and trailing-dot messages",
                  all && has(msgs[0], "\\x1B") && has(msgs[3], "\\x1B"), joined);
        }
        {
            std::string sdir = dd + "/scr21";
            std::string slot;
            {
                pa::ProgramArchive a; std::string why, out;
                pa::ScratchDir sc(sdir);
                if (open_zip(a, {mem("G.NEX", nexb), mem("data/deep/L1.BIN", l1)}, why) &&
                    a.extract_program("G.NEX", sc, out, why))
                    slot = fs::path(out).parent_path().string();
                const bool there = !slot.empty() && fs::exists(slot + "/data/deep/L1.BIN");
                if (!there) slot.clear();
            }
            check("ZDIR-21", "destroying the ScratchDir removes the nested tree",
                  !slot.empty() && !fs::exists(slot + "/data") && !fs::exists(slot) && !fs::exists(fs::path(slot).parent_path()));
        }
        {
            pa::ProgramArchive a; std::string why, out;
            pa::ScratchDir sc(dd + "/scr22");
            const bool ok = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/L1.BIN", l1),
                                          mem("__MACOSX/game/x", sfx), mem("game/._L1.BIN", sfx)}, why);
            const bool ex = ok && a.extract_program("game/G.NEX", sc, out, why);
            const fs::path slot = ex ? fs::path(out).parent_path() : fs::path();
            check("ZDIR-22", "the junk members __MACOSX/game/x and game/._L1.BIN are not written",
                  ex && tree_files(slot) == 3 && !fs::exists(slot / "._L1.BIN") && !fs::exists(slot / "__MACOSX"),
                  why);
        }
        {
            // A directory spelled in two cases is one directory, under the spelling seen first: the tree
            // is the same on a case-sensitive host (Linux, MEMFS) and a case-insensitive one.
            pa::ProgramArchive a, b, c; std::string w1, w2, w3, o1, o2, o3;
            pa::ScratchDir sc(dd + "/scr23");
            const bool p1 = open_zip(a, {mem("game/G.NEX", nexb), mem("game/data/a.bin", l1), mem("game/DATA/b.bin", sfx)}, w1);
            Member d; d.name = "game/Data/"; d.method = 0;
            const bool p2 = open_zip(b, {mem("game/G.NEX", nexb), d, mem("game/data/b.bin", sfx), mem("game/DATA/sub/c.bin", l1)}, w2);
            const bool p3 = open_zip(c, {mem("game/G.NEX", nexb), mem("game/data/a.bin", l1), mem("game/Data/a2.bin", sfx)}, w3);
            const bool e1 = p1 && a.extract_program("game/G.NEX", sc, o1, w1);
            const bool e2 = p2 && b.extract_program("game/G.NEX", sc, o2, w2);
            const bool e3 = p3 && c.extract_program("game/G.NEX", sc, o3, w3);
            const fs::path s1 = e1 ? fs::path(o1).parent_path() : fs::path();
            const fs::path s2 = e2 ? fs::path(o2).parent_path() : fs::path();
            const fs::path s3 = e3 ? fs::path(o3).parent_path() : fs::path();
            auto names = [](const fs::path& r) {
                std::vector<std::string> v; std::error_code e;
                for (fs::recursive_directory_iterator it(r, e), end; !e && it != end; it.increment(e))
                    v.push_back(fs::relative(it->path(), r).generic_string());
                std::sort(v.begin(), v.end());
                return v;
            };
            const std::vector<std::string> want1 = {"G.NEX", "data", "data/a.bin", "data/b.bin"};
            const std::vector<std::string> want2 = {"Data", "Data/b.bin", "Data/sub", "Data/sub/c.bin", "G.NEX"};
            const std::vector<std::string> want3 = {"G.NEX", "data", "data/a.bin", "data/a2.bin"};
            check("ZDIR-23", "a directory spelled in two cases (implicit, explicit entry first, nested) extracts under its first-seen spelling, every file in it",
                  e1 && e2 && e3 && names(s1) == want1 && names(s2) == want2 && names(s3) == want3 &&
                      slurp((s1 / "data" / "b.bin").string()) == sfx && slurp((s2 / "Data" / "sub" / "c.bin").string()) == l1,
                  w1 + " | " + w2 + " | " + w3);
        }
        {
            // choose(): a second NEX anywhere (here below the first) makes the archive ambiguous.
            pa::ProgramArchive a; std::string why, mem_;
            const bool ok = open_zip(a, {mem("G.NEX", nexb), mem("sub/X.NEX", nexb), mem("data/a.bin", l1)}, why);
            const bool ch = ok && a.choose(mem_, why);
            check("ZDIR-24", "a NEX with a second NEX in a subdirectory below it is refused by choose() with the several-programs text",
                  ok && !ch && mem_.empty() && why == a.several_why() && has(why, "G.NEX") && has(why, "sub/X.NEX"), why);
        }
        {
            // The NEX's own folder spelled in another case is the same folder: its data is served,
            // and a game-data program below it does not make the archive ambiguous.
            pa::ProgramArchive a, b; std::string w1, w2, o1, m2;
            pa::ScratchDir sc(dd + "/scr25");
            const bool p1 = open_zip(a, {mem("Game/G.NEX", nexb), mem("GAME/data.txt", l1)}, w1);
            const bool p2 = open_zip(b, {mem("Game/G.NEX", nexb), mem("GAME/data/x.tap", sfx)}, w2);
            const bool h1 = p1 && a.has_data_files("Game/G.NEX");
            const bool e1 = h1 && a.extract_program("Game/G.NEX", sc, o1, w1);
            const fs::path s1 = e1 ? fs::path(o1).parent_path() : fs::path();
            const bool c2 = p2 && b.choose(m2, w2);
            check("ZDIR-25", "a data file in GAME/ beside Game/G.NEX is extracted into the slot, and choose() takes Game/G.NEX over GAME/data/x.tap",
                  p1 && p2 && h1 && e1 && slurp((s1 / "data.txt").string()) == l1 && c2 && m2 == "Game/G.NEX",
                  w1 + " | " + w2);
        }
    }
}

int main() {
    std::error_code ec;
    const std::string root = jtp::make_temp_dir("jnext-zip-");
    const std::string cfg = root + "/cfg";
    fs::create_directories(cfg);
    jtp::set_env("JNEXT_CONFIG_DIR", cfg.c_str());

    const Bytes beeper = slurp(kTapDir + "beeper_demo.tap");
    const Bytes charset = slurp(kTapDir + "rom_charset_test.tap");
    const Bytes lores = slurp(kNexDir + "lores_demo.nex");
    if (root.empty() || beeper.size() != 1400 || lores.size() != 16896) {
        std::printf("harness fault: fixtures sources missing\n");
        return 2;
    }

    // ------------------------------------------------------------ interchange
    {
        Bytes z = slurp(kFix + "/beeper.tap.zip");
        InterchangeReader r;
        std::string why;
        const bool ok = open_buf(r, z, why);
        std::vector<std::string> names;
        for (const auto& e : r.entries()) names.push_back(e.name);
        const std::vector<std::string> want = {"BEEPER.TAP", "SCRSHOT/", "SCRSHOT/BEEPER.SCR",
                                               "POKES/", "POKES/BEEPER.POK"};
        check("ZIPI-01", "Info-ZIP beeper.tap.zip opens: members in central order, directories flagged",
              ok && names == want && r.entries()[1].is_directory() && r.entries()[3].is_directory() &&
                  !r.entries()[0].is_directory(),
              why);
    }
    {
        Bytes z = slurp(kFix + "/beeper.tap.zip");
        InterchangeReader r;
        std::string why;
        Bytes out;
        const bool ok = open_buf(r, z, why) && entry(r, "BEEPER.TAP") &&
                        r.read(*entry(r, "BEEPER.TAP"), out, why);
        const InterchangeEntry* e = entry(r, "BEEPER.TAP");
        check("ZIPI-02", "BEEPER.TAP (flag 0x0002, central extra field) reads back == beeper_demo.tap",
              ok && out == beeper && e && (e->flags & 0x0002) && e->central_extra_len > 0, why);
    }
    {
        Bytes z = slurp(kFix + "/lores.nex.zip");
        InterchangeReader r;
        std::string why;
        Bytes out;
        const bool ok = open_buf(r, z, why) && entry(r, "LORES.NEX") &&
                        r.read(*entry(r, "LORES.NEX"), out, why);
        check("ZIPI-03", "CPython data-descriptor member (flag 0x0008, local sizes 0) reads == lores_demo.nex",
              ok && out == lores && (entry(r, "LORES.NEX")->flags & 0x0008) && lores.size() == 16896, why);
    }
    {
        const Bytes d = pattern(300);
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({mem("S.BIN", d, 0)}), out, why);
        check("ZIPI-04", "a STORED member reads back exactly", ok && out == d, why);
    }
    {
        Member m = mem("E.BIN", pattern(500));
        m.local_extra = 28;
        m.central_extra = 24;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-05", "local extra 28 bytes vs central 24: data offset comes from the LOCAL header",
              ok && out == m.data, why);
    }
    {
        zipfx::Eocd e;
        e.comment = "seventeen chars!!!";
        e.comment.resize(17);
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({mem("C.BIN", pattern(40))}, e), out, why);
        check("ZIPI-06", "an EOCD with a 17-byte archive comment opens", ok && out == pattern(40), why);
    }
    {
        Bytes t(100, 'x');
        const std::string why = open_refusal(t);
        check("ZIPI-07", "100 bytes of text are refused as not a ZIP archive",
              !why.empty() && has(why, "not a ZIP archive"), why);
    }
    {
        zipfx::Eocd e;
        e.total_override = 0xFFFF;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-08", "EOCD entry count 0xFFFF (ZIP64 sentinel) is refused naming ZIP64",
              !why.empty() && has(why, "ZIP64"), why);
    }
    {
        zipfx::Eocd e;
        e.zip64_locator = true;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-09", "a ZIP64 EOCD locator before the EOCD (otherwise valid) is refused naming ZIP64",
              !why.empty() && has(why, "ZIP64"), why);
    }
    {
        Member m = mem("A.BIN", pattern(10));
        m.usize_override = 0xFFFFFFFFll;
        const std::string why = open_refusal(zipfx::build({m}));
        check("ZIPI-10", "a central entry with size 0xFFFFFFFF is refused naming ZIP64",
              !why.empty() && has(why, "ZIP64"), why);
    }
    {
        zipfx::Eocd e;
        e.disk = 1;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-11", "EOCD disk number 1 is refused as multi-disk",
              !why.empty() && has(why, "multi-disk"), why);
    }
    {
        zipfx::Eocd e;
        e.cd_size_override = 100000;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-12", "a central directory running past the EOCD is refused as damaged",
              !why.empty() && has(why, "damaged"), why);
    }
    {
        Bytes z = slurp(kFix + "/encrypted.zip");
        InterchangeReader r;
        std::string why, rwhy;
        Bytes out;
        const bool opened = open_buf(r, z, why);
        const bool read_ok = opened && r.read(r.entries()[0], out, rwhy);
        check("ZIPI-13", "ZipCrypto member (flag 0x000b) is refused as encrypted, not as an inflate error",
              opened && !read_ok && has(rwhy, "encrypted") && !has(rwhy, "deflate"), why + "/" + rwhy);
    }
    {
        Member m = mem("S.BIN", pattern(20), 0);
        m.flags = 0x0040;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-14", "flag bit 6 only (strong encryption) is refused as encrypted",
              !ok && has(why, "encrypted"), why);
    }
    {
        Member m = mem("B.BIN", pattern(20), 12);
        m.use_raw = true;
        m.raw = pattern(10);
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-15", "method 12 (bzip2) is refused naming the method",
              !ok && has(why, "compression method 12"), why);
    }
    {
        Member m = mem("D.BIN", pattern(20), 9);
        m.use_raw = true;
        m.raw = pattern(10);
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-16", "method 9 (deflate64, the near miss of 8) is refused naming the method",
              !ok && has(why, "compression method 9"), why);
    }
    {
        Member m = mem("L.BIN", pattern(20), 0);
        m.use_raw = true;
        m.raw = pattern(20);
        m.csize_override = 65;
        m.usize_override = 65;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why, 64);
        check("ZIPI-17", "a member declaring 65 bytes over a 64-byte cap is refused naming the limit",
              !ok && has(why, "limit"), why);
    }
    {
        const Bytes d = pattern(64);
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({mem("L.BIN", d, 0)}), out, why, 64);
        check("ZIPI-18", "a real 64-byte member reads under a 64-byte cap (the bound is >)",
              ok && out == d, why);
    }
    {
        Member m = mem("H.BIN", pattern(20));
        m.usize_override = 256ll * 1024 * 1024 + 1;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);   // default cap
        check("ZIPI-19", "the default cap is 256 MiB: a 256 MiB + 1 declaration is refused before allocation",
              jnext::zip::kMaxInterchangeMemberBytes == 256ull * 1024 * 1024 && !ok && has(why, "limit"), why);
    }
    {
        Member m = mem("C.BIN", pattern(50));
        m.crc_override = static_cast<int64_t>(
            crc32(0, pattern(50).data(), 50)) + 1;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-20", "a CRC off by one is refused naming the CRC", !ok && has(why, "CRC"), why);
    }
    {
        Member m = mem("S.BIN", pattern(10), 0);
        m.csize_override = 10;
        m.usize_override = 11;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-21", "STORED with compressed size 10 and size 11 is refused", !ok && !why.empty(), why);
    }
    {
        Member m = mem("F.BIN", pattern(5));
        m.usize_override = 6;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-22", "DEFLATE that inflates to fewer bytes than declared is refused", !ok && !why.empty(), why);
    }
    {
        Member m = mem("G.BIN", pattern(30));
        m.corrupt_local_sig = true;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-23", "a corrupted local header signature is refused naming the local header",
              !ok && has(why, "local header"), why);
    }
    {
        Member m = mem("P.BIN", pattern(30));
        m.csize_override = 100000;
        Bytes out;
        std::string why;
        const bool ok = read_first(zipfx::build({m}), out, why);
        check("ZIPI-24", "a compressed size running past the end of the buffer is refused", !ok && !why.empty(), why);
    }
    {
        // Each relaxation the interchange reader needs must still be refused by the
        // STRICT reader, one at a time: the flag (Info-ZIP's 0x0002) and the extra field.
        // (The Info-ZIP fixture is refused first on its upper-case name, so the
        // flag and the extra field are built alone, in lower-case names the grammar allows.)
        Member f = mem("a.bin", pattern(40), 0);
        f.flags = 0x0002;
        const Bytes z = zipfx::build({f});
        jnext::zip::Reader strict;
        std::string why;
        const bool ok = strict.open(z.data(), z.size(), why);
        Member m = mem("a.bin", pattern(40), 0);
        m.central_extra = 24;
        const Bytes z2 = zipfx::build({m});
        jnext::zip::Reader strict2;
        std::string why2;
        const bool ok2 = strict2.open(z2.data(), z2.size(), why2);
        check("ZIPI-25", "the STRICT .jns reader still refuses an Info-ZIP flag (0x0002) and, alone, a central extra field",
              !ok && has(why, "general-purpose flags") && !ok2 && has(why2, "extra field"), why + " / " + why2);
    }


    // ---- GH #392 round 2: the bounds and refusals a hostile archive reaches ----
    {
        Member m = mem("A.BIN", pattern(10));
        m.central_name_len_override = 60000;
        const std::string why = open_refusal(zipfx::build({m}));
        check("ZIPI-26", "a central entry whose name length runs past the directory is refused as overrunning it",
              has(why, "overruns"), why);
    }
    {
        zipfx::Eocd e;
        e.central_tail = Bytes{'P', 'K', 1, 2};
        e.central_tail.resize(20, 0);   // a valid central signature with only 20 bytes left
        e.central_extra_entries = 1;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-27", "a central entry cut short by the end of the directory (valid signature, 20 of 46 bytes) is refused as truncated",
              has(why, "truncated"), why);
    }
    {
        zipfx::Eocd e;
        e.total_override = 1;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10)), mem("B.BIN", pattern(10))}, e));
        check("ZIPI-28", "a directory holding more entries than the EOCD counts is refused as disagreeing with its entry count",
              has(why, "entry count"), why);
    }
    {
        Member m = mem("A.BIN", pattern(10));
        zipfx::Eocd e;
        e.comment = std::string("PK\x03\x04") + std::string(6, '\0');
        Bytes b = zipfx::build({m}, e);
        m.local_offset_override = static_cast<int64_t>(b.size()) - 10;   // a local signature, but under 30 bytes left
        b = zipfx::build({m}, e);
        InterchangeReader r;
        std::string why, rwhy;
        Bytes out;
        const bool opened = open_buf(r, b, why);
        const bool ok = opened && r.read(r.entries()[0], out, rwhy);
        check("ZIPI-29", "a local header offset with fewer than 30 bytes before the end of the file is refused as a damaged local header",
              opened && !ok && has(rwhy, "local header"), why + "/" + rwhy);
    }
    {
        Bytes b = zipfx::build({mem("A.BIN", pattern(10))});
        b.insert(b.end(), {'x', 'y', 'z'});
        const std::string why = open_refusal(b);
        check("ZIPI-30", "bytes after the EOCD: the archive is refused (the EOCD must end exactly at the end of the file)",
              has(why, "not a ZIP archive"), why);
    }
    {
        zipfx::Eocd e;
        e.cd_disk = 1;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-31", "EOCD central-directory disk number 1 is refused as multi-disk", has(why, "multi-disk"), why);
    }
    {
        zipfx::Eocd e;
        e.here_override = 0;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-32", "an EOCD whose this-disk entry count differs from its total is refused as multi-disk",
              has(why, "multi-disk"), why);
    }
    {
        Member m = mem("A.BIN", pattern(10));
        m.disk_start = 1;
        const std::string why = open_refusal(zipfx::build({m}));
        check("ZIPI-33", "a central entry starting on disk 1 is refused as multi-disk", has(why, "multi-disk"), why);
    }
    {
        zipfx::Eocd e;
        e.cd_size_override = 0xFFFFFFFFll;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-34", "an EOCD central-directory size of 0xFFFFFFFF is refused naming ZIP64", has(why, "ZIP64"), why);
    }
    {
        zipfx::Eocd e;
        e.cd_off_override = 0xFFFFFFFFll;
        const std::string why = open_refusal(zipfx::build({mem("A.BIN", pattern(10))}, e));
        check("ZIPI-35", "an EOCD central-directory offset of 0xFFFFFFFF is refused naming ZIP64", has(why, "ZIP64"), why);
    }
    {
        Member m = mem("A.BIN", pattern(10));
        m.csize_override = 0xFFFFFFFFll;
        const std::string why = open_refusal(zipfx::build({m}));
        check("ZIPI-36", "a central entry with compressed size 0xFFFFFFFF is refused naming ZIP64", has(why, "ZIP64"), why);
    }
    {
        Member m = mem("A.BIN", pattern(10));
        m.local_offset_override = 0xFFFFFFFFll;
        const std::string why = open_refusal(zipfx::build({m}));
        check("ZIPI-37", "a central entry with local offset 0xFFFFFFFF is refused naming ZIP64", has(why, "ZIP64"), why);
    }

    // ----------------------------------------------------- program-archive layer
    namespace pa = program_archive;
    const std::string files = root + "/files";
    {
        const std::string p = files + "/x.zip";
        spit(p, slurp(kFix + "/beeper.tap.zip"));
        check("ZIPL-01", "a real archive named x.zip is an archive path", pa::is_archive_path(p));
    }
    {
        const Bytes z = slurp(kFix + "/beeper.tap.zip");
        spit(files + "/game", z);
        spit(files + "/game.bin", z);
        check("ZIPL-02", "an archive saved as `game` and as `game.bin` is detected by its PK magic",
              pa::is_archive_path(files + "/game") && pa::is_archive_path(files + "/game.bin"));
    }
    {
        const Bytes z = slurp(kFix + "/beeper.tap.zip");
        bool any = false;
        std::string which;
        for (const char* ext : {".jns", ".nex", ".sna", ".szx", ".z80", ".tap", ".tzx", ".wav", ".rzx", ".pok", ".TAP"}) {
            const std::string p = files + "/prog" + ext;
            spit(p, z);
            if (pa::is_archive_path(p)) { any = true; which += ext; }
        }
        check("ZIPL-03", "zip content named with a program extension or .pok keeps its own route (never an archive)",
              !any, which);
    }
    {
        spit(files + "/t.zip", text("this is not a zip file"));
        spit(files + "/t2.ZIP", text("this is not a zip file"));
        spit(files + "/t.bin", text("this is not a zip file"));
        check("ZIPL-04", "text named .zip / .ZIP is an archive path (refused later); text named .bin is not",
              pa::is_archive_path(files + "/t.zip") && pa::is_archive_path(files + "/t2.ZIP") &&
                  !pa::is_archive_path(files + "/t.bin"));
    }
    {
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(kFix + "/beeper.tap.zip", why);
        check("ZIPL-05", "programs() of beeper.tap.zip is exactly BEEPER.TAP (no .SCR, .POK, directories)",
              ok && a.programs() == std::vector<std::string>{"BEEPER.TAP"}, why);
    }
    {
        spit(files + "/mix.zip", zipfx::build({
            mem("A.TAP", pattern(5)), mem("b.tzx", pattern(5)), mem("C.NEX", pattern(5)),
            mem("d.Sna", pattern(5)), mem("E.SZX", pattern(5)), mem("f.z80", pattern(5)),
            mem("G.RZX", pattern(5)), mem("h.wav", pattern(5)), mem("I.JNS", pattern(5)),
            mem("J.SCR", pattern(5)), mem("K.POK", pattern(5)), mem("L.TXT", pattern(5))}));
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(files + "/mix.zip", why);
        check("ZIPL-06", "programs() are the nine program extensions, in order; .SCR .POK .TXT are not",
              ok && a.programs() == std::vector<std::string>{"A.TAP", "b.tzx", "C.NEX", "d.Sna", "E.SZX",
                                                              "f.z80", "G.RZX", "h.wav", "I.JNS"}, why);
    }
    {
        spit(files + "/mac.zip", zipfx::build({mem("__MACOSX/._BEEPER.TAP", pattern(5)),
                                               mem("._BEEPER.TAP", pattern(5)),
                                               mem("BEEPER.TAP", pattern(5))}));
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(files + "/mac.zip", why);
        check("ZIPL-07", "macOS __MACOSX/ and ._ AppleDouble members are not programs",
              ok && a.programs() == std::vector<std::string>{"BEEPER.TAP"}, why);
    }
    {
        std::string path, why;
        const bool ok = pa::extract_single_program(kFix + "/beeper.tap.zip", path, why);
        const std::string under = cfg + "/archive-load/";
        check("ZIPL-08", "extract_single_program(beeper.tap.zip): under <config>/archive-load, BEEPER.TAP, same bytes",
              ok && path.rfind(under, 0) == 0 && fs::path(path).filename() == "BEEPER.TAP" &&
                  slurp(path) == beeper, path + " " + why);
    }
    {
        std::string path, why;
        const bool ok = pa::extract_single_program(kFix + "/twotapes.zip", path, why);
        check("ZIPL-09", "two programs: refused naming both, nothing extracted",
              !ok && has(why, "2 programs") && has(why, "SIDEA.TAP") && has(why, "SIDEB.TAP") &&
                  count_named(cfg + "/archive-load", "SIDEA.TAP") == 0 &&
                  count_named(cfg + "/archive-load", "SIDEB.TAP") == 0, why);
    }
    {
        std::string path, why;
        const bool ok = pa::extract_single_program(kFix + "/noprog.zip", path, why);
        check("ZIPL-10", "no program: refused as `no program`, listing what the archive holds",
              !ok && has(why, "no program") && has(why, "README.TXT"), why);
    }
    {
        spit(files + "/evil.zip", zipfx::build({mem("../../evil.tap", pattern(8))}));
        std::string path, why;
        const bool ok = pa::extract_single_program(files + "/evil.zip", path, why);
        check("ZIPL-11", "member ../../evil.tap is refused as unsafe; no evil.tap appears anywhere under the temp root",
              !ok && has(why, "unsafe") && count_named(root, "evil.tap") == 0, why);
    }
    {
        bool all = true;
        std::string det;
        int i = 0;
        for (const char* n : {"/abs.tap", "C:/x.tap", "..\\evil.tap"}) {
            const std::string p = files + "/bad" + std::to_string(i++) + ".zip";
            spit(p, zipfx::build({mem(n, pattern(8))}));
            std::string path, why;
            if (pa::extract_single_program(p, path, why) || !has(why, "unsafe")) { all = false; det += n; det += ":" + why + " "; }
        }
        check("ZIPL-12", "absolute, drive-lettered and backslash-traversal member names are each refused as unsafe",
              all && count_named(root, "evil.tap") == 0, det);
    }
    pa::ScratchDir scratch(root + "/scratch");
    {
        spit(files + "/deep.zip", zipfx::build({mem("GAMES/SUB/GAME.TAP", pattern(9))}));
        pa::ProgramArchive a;
        std::string why, path;
        const bool ok = a.open(files + "/deep.zip", why) &&
                        a.extract("GAMES/SUB/GAME.TAP", scratch, path, why);
        const std::string rel = ok ? path.substr(std::min(path.size(), scratch.process_dir().size() + 1)) : "";
        const auto slash = rel.find('/');
        check("ZIPL-13", "GAMES/SUB/GAME.TAP extracts as GAME.TAP directly inside its numbered slot",
              ok && slash != std::string::npos && rel.substr(slash + 1) == "GAME.TAP" &&
                  slurp(path) == pattern(9), path + " " + why);
    }
    {
        spit(files + "/odd.zip", zipfx::build({mem(std::string("GA:ME?\xE9.TAP"), pattern(9))}));
        pa::ProgramArchive a;
        std::string why, path;
        const bool ok = a.open(files + "/odd.zip", why) &&
                        a.extract(a.programs()[0], scratch, path, why);
        check("ZIPL-14", "unsafe bytes in a member basename are replaced by _ (GA_ME__.TAP)",
              ok && fs::path(path).filename() == "GA_ME__.TAP", path + " " + why);
    }
    {
        spit(files + "/g1.zip", zipfx::build({mem("GAME.TAP", pattern(11))}));
        spit(files + "/g2.zip", zipfx::build({mem("GAME.TAP", pattern(22))}));
        pa::ProgramArchive a1, a2;
        std::string why, p1, p2;
        const bool ok = a1.open(files + "/g1.zip", why) && a2.open(files + "/g2.zip", why) &&
                        a1.extract("GAME.TAP", scratch, p1, why) && a2.extract("GAME.TAP", scratch, p2, why);
        check("ZIPL-15", "two archives both holding GAME.TAP extract to two paths, each file keeping its own bytes",
              ok && p1 != p2 && slurp(p1) == pattern(11) && slurp(p2) == pattern(22), p1 + " " + p2 + " " + why);
    }
    {
        std::string proc;
        bool existed = false;
        {
            pa::ScratchDir s(root + "/scratch16");
            pa::ProgramArchive a;
            std::string why, path;
            if (a.open(kFix + "/beeper.tap.zip", why) && a.extract("BEEPER.TAP", s, path, why)) {
                proc = s.process_dir();
                existed = fs::exists(path);
            }
        }
        check("ZIPL-16", "a ScratchDir removes its process directory when destroyed",
              existed && !proc.empty() && !fs::exists(proc), proc);
    }
    {
        // A directory's mtime cannot be set portably (std::filesystem on Windows
        // cannot open one for writing), so the sweep age is injected instead: a
        // negative age puts every existing directory behind the cutoff, the
        // default age (7 days) puts none.
        const std::string r = root + "/sweep";
        fs::create_directories(r + "/fresh/1");
        std::string why;
        std::string slot_keep, slot_sweep;
        {
            pa::ScratchDir keep(r);   // default age
            slot_keep = keep.new_slot(why);
        }
        const bool kept = fs::exists(r + "/fresh");
        {
            pa::ScratchDir sweep(r, std::chrono::hours(-1));
            slot_sweep = sweep.new_slot(why);
        }
        const bool swept = !fs::exists(r + "/fresh");
        check("ZIPL-17", "the first slot sweeps directories older than the sweep age (7 days by default: a fresh one is kept)",
              pa::ScratchDir::kSweepAge == std::chrono::hours(7 * 24) && !slot_keep.empty() &&
                  !slot_sweep.empty() && kept && swept, why);
    }
    {
        spit(files + "/pok1.zip", zipfx::build({mem("BEEPER.TAP", pattern(5)), mem("POKES/OTHER.POK", pattern(5)),
                                                mem("POKES/beeper.pok", pattern(5))}));
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(files + "/pok1.zip", why);
        check("ZIPL-18", "matching_pok(BEEPER.TAP) picks POKES/beeper.pok by stem, not the first .pok",
              ok && a.matching_pok("BEEPER.TAP") == "POKES/beeper.pok", why);
    }
    {
        spit(files + "/pok2.zip", zipfx::build({mem("GAME.TAP", pattern(5)), mem("CHEATS.POK", pattern(5))}));
        spit(files + "/pok3.zip", zipfx::build({mem("GAME.TAP", pattern(5)), mem("A.POK", pattern(5)), mem("B.POK", pattern(5))}));
        spit(files + "/pok4.zip", zipfx::build({mem("GAME.TAP", pattern(5))}));
        pa::ProgramArchive a2, a3, a4;
        std::string why;
        const bool ok = a2.open(files + "/pok2.zip", why) && a3.open(files + "/pok3.zip", why) &&
                        a4.open(files + "/pok4.zip", why);
        check("ZIPL-19", "an unrelated lone .pok is offered; two unrelated, or none, offer nothing",
              ok && a2.matching_pok("GAME.TAP") == "CHEATS.POK" && a3.matching_pok("GAME.TAP").empty() &&
                  a4.matching_pok("GAME.TAP").empty(), why);
    }
    {
        pa::ProgramArchive a;
        std::string why, path;
        const bool opened = a.open(kFix + "/encrypted.zip", why);
        const bool ok = opened && a.extract("BEEPER.TAP", scratch, path, why);
        check("ZIPL-20", "extracting the encrypted member is refused with the member name first, naming encryption",
              opened && !ok && why.rfind("BEEPER.TAP", 0) == 0 && has(why, "encrypted"), why);
    }
    {
        spit(files + "/big.zip", zipfx::build({mem("BIG.BIN", pattern(900), 0)}));
        pa::ProgramArchive a;
        std::string why;
        const bool size_ok = fs::file_size(files + "/big.zip") > 1000;
        const bool ok = a.open(files + "/big.zip", why, 1000);
        check("ZIPL-21", "an archive file over the archive cap is refused naming the limit before it is read",
              size_ok && !ok && has(why, "limit"), why);
    }
    {
        spit(files + "/dup.zip", zipfx::build({mem("GAME.TAP", pattern(5)), mem("GAME.TAP", pattern(6))}));
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(files + "/dup.zip", why);
        check("ZIPL-22", "two members both named GAME.TAP: the archive is refused (readers disagree which wins)",
              !ok && has(why, "two members named"), why);
    }

    // ---- GH #392 round 2 ----
    {
        spit(files + "/bs.zip", zipfx::build({mem("\\abs.tap", pattern(8))}));
        std::string path, why;
        const bool ok = pa::extract_single_program(files + "/bs.zip", path, why);
        check("ZIPL-23", "a member named \\abs.tap (leading backslash) is refused as unsafe", !ok && has(why, "unsafe"), why);
    }
    {
        spit(files + "/ctl.zip", zipfx::build({mem(std::string("A\x01\x1b" "B\x7f" "C.TAP"), pattern(8))}));
        pa::ProgramArchive a;
        std::string why, path;
        const bool ok = a.open(files + "/ctl.zip", why) && a.extract(a.programs()[0], scratch, path, why);
        check("ZIPL-24", "control bytes (0x01, ESC, DEL) in a member basename become _ in the extracted file name",
              ok && fs::path(path).filename() == "A__B_C.TAP", path + " " + why);
    }
    {
        spit(files + "/empty", zipfx::build({}));
        pa::ProgramArchive a;
        std::string why;
        const bool opened = a.open(files + "/empty", why);
        check("ZIPL-25", "an empty archive (only an EOCD, PK\\5\\6) saved without an extension is an archive, with no program",
              pa::is_archive_path(files + "/empty") && opened && a.programs().empty() &&
                  has(a.no_program_why(), "nothing"), why);
    }
    {
        spit(files + "/mac2.zip", zipfx::build({mem("__MACOSX/BEEPER.TAP", pattern(5)), mem("BEEPER.TAP", pattern(5))}));
        pa::ProgramArchive a;
        std::string why;
        const bool ok = a.open(files + "/mac2.zip", why);
        check("ZIPL-26", "a member under __MACOSX/ is not a program even without a ._ name",
              ok && a.programs() == std::vector<std::string>{"BEEPER.TAP"}, why);
    }
    {
        const std::string p = files + "/cap.zip";
        spit(p, zipfx::build({mem("CAP.TAP", pattern(300), 0)}));
        const uint64_t sz = fs::file_size(p);
        pa::ProgramArchive a1, a2;
        std::string w1, w2;
        const bool at = a1.open(p, w1, sz);
        const bool over = a2.open(p, w2, sz - 1);
        check("ZIPL-27", "the archive cap straddles: a file of exactly the cap opens, one byte over is refused",
              at && !over && has(w2, "limit"), w1 + "/" + w2);
    }
    {
        // A long shared prefix makes every name comparison cost, so a per-member
        // find() over the earlier members cannot hide behind short early-out compares.
        std::vector<Member> ms;
        const std::string prefix(200, 'A');
        for (int i = 0; i < 65534; ++i) {
            char nm[16];
            std::snprintf(nm, sizeof nm, "N%05d.TAP", i);
            ms.push_back(mem(prefix + nm, Bytes{}, 0));
        }
        spit(files + "/many.zip", zipfx::build(ms));
        pa::ProgramArchive a;
        std::string why;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = a.open(files + "/many.zip", why);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check("ZIPL-28", "an archive of 65534 program members opens in linear time (under 8 s; a find() per member took 12 s)",
              ok && a.programs().size() == 65534 && secs < 8.0, why + " secs=" + std::to_string(secs));
    }
    {
        const std::string esc1 = "\x1b]0;PWNED\x07\x1b[31mRED.TAP", esc2 = "\x1b[2JCLEAR.TAP";
        spit(files + "/two_esc.zip", zipfx::build({mem(esc1, pattern(5)), mem(esc2, pattern(5))}));
        spit(files + "/none_esc.zip", zipfx::build({mem("\x1b[2JREADME.TXT", pattern(5))}));
        spit(files + "/dup_esc.zip", zipfx::build({mem("\x1b[2J.TAP", pattern(5)), mem("\x1b[2J.TAP", pattern(6))}));
        Member enc = mem("\x1b[2JENC.TAP", pattern(5), 0);
        enc.flags = 1;
        spit(files + "/enc_esc.zip", zipfx::build({enc}));
        std::string p, w_several, w_none, w_dup, w_enc, w_unsafe;
        pa::extract_single_program(files + "/two_esc.zip", p, w_several);
        pa::extract_single_program(files + "/none_esc.zip", p, w_none);
        pa::extract_single_program(files + "/dup_esc.zip", p, w_dup);
        pa::extract_single_program(files + "/enc_esc.zip", p, w_enc);
        spit(files + "/unsafe_esc.zip", zipfx::build({mem("\x1b[2J/../x.tap", pattern(5))}));
        pa::extract_single_program(files + "/unsafe_esc.zip", p, w_unsafe);
        auto clean = [](const std::string& s) {
            for (unsigned char c : s) if (c < 0x20 || c >= 0x7F) return false;
            return true;
        };
        check("ZIPL-29", "no control byte survives into the several-programs, no-program, duplicate, encrypted or unsafe-path messages",
              !w_several.empty() && !w_none.empty() && !w_dup.empty() && !w_enc.empty() && !w_unsafe.empty() &&
                  clean(w_several) && clean(w_none) && clean(w_dup) && clean(w_enc) && clean(w_unsafe) &&
                  has(w_several, "\\x1B") && has(w_enc, "encrypted"),
              w_several + " | " + w_none + " | " + w_dup + " | " + w_enc + " | " + w_unsafe);
    }
    {
        using pa::display_name;
        check("ZIPL-30", "display_name escapes C0, DEL, C1 and bytes outside well-formed UTF-8, keeps valid UTF-8, and stays injective",
              display_name(std::string("A\x82" "B")) == "A\\x82B" &&
                  display_name("caf\xC3\xA9") == "caf\xC3\xA9" &&
                  display_name("\xC2\x9B") == "\\xC2\\x9B" &&
                  display_name("\x7f") == "\\x7F" &&
                  display_name(std::string("A\0B", 3)) == "A\\x00B" &&
                  display_name("\xE0\x80\x80") == "\\xE0\\x80\\x80" &&
                  display_name("F\x82") != display_name("F\x83"));
    }
    {
        using pa::display_name;
        check("ZIPL-31", "display_name escapes a lead byte followed by a control byte, surrogates, overlong F0 and above U+10FFFF",
              display_name("\xC3\x1b[2J") == "\\xC3\\x1B[2J" &&
                  display_name("\xED\xA0\x80") == "\\xED\\xA0\\x80" &&
                  display_name("\xF0\x80\x80\x80") == "\\xF0\\x80\\x80\\x80" &&
                  display_name("\xF4\x90\x80\x80") == "\\xF4\\x90\\x80\\x80" &&
                  display_name("\xF0\x9F\x98\x80") == "\xF0\x9F\x98\x80");
    }
    {
        // Oracle: Microsoft "Naming Files, Paths, and Namespaces": CON PRN AUX NUL
        // COM1-9 LPT1-9 are reserved with or without an extension, in any case.
        using pa::is_windows_reserved_name;
        using pa::extraction_name;
        bool reserved = true;
        for (const char* n : {"CON.TAP", "con.tap", "PRN.Z80", "Aux.SNA", "NUL.tar.TAP", "COM1.NEX", "com9.tzx",
                              "LPT1.TAP", "LPT9.WAV", "CON .TAP", "NUL"})
            reserved = reserved && is_windows_reserved_name(n);
        bool ordinary = true;
        for (const char* n : {"GAME.TAP", "CONSOLE.TAP", "COM0.TAP", "COM10.TAP", "LPT.TAP", "XCON.TAP", "COM.TAP", "AUXX.TAP"})
            ordinary = ordinary && !is_windows_reserved_name(n);
        check("ZIPL-32", "the Windows reserved device names are recognised (any case, any extension) and ordinary names are not",
              reserved && ordinary);
        check("ZIPL-33", "an extraction name prefixes `_` to a reserved name and leaves others alone",
              extraction_name("GAMES/CON.TAP") == "_CON.TAP" && extraction_name("nul.z80") == "_nul.z80" &&
                  extraction_name("GAME.TAP") == "GAME.TAP" && extraction_name("a\\COM1.NEX") == "_COM1.NEX");
    }
    {
        spit(files + "/devname.zip", zipfx::build({mem("CON.TAP", pattern(9))}));
        pa::ProgramArchive a;
        std::string why, path;
        const bool ok = a.open(files + "/devname.zip", why) && a.extract("CON.TAP", scratch, path, why);
        check("ZIPL-34", "a member named CON.TAP extracts (as _CON.TAP) with its bytes on every OS",
              ok && fs::path(path).filename() == "_CON.TAP" && slurp(path) == pattern(9), path + " " + why);
    }

    // SD card images (jnext-internal): a zipped card is unpacked by exactly one
    // `.img` member. Oracle: the rule in the issue plan (one non-directory
    // member whose basename ends .img, case-insensitive, ignoring __MACOSX/
    // and ._*); bytes and CRC checks are PKWARE APPNOTE 4.3.7 / 4.4.7.
    {
        const Bytes card = pattern(1500);
        for (int method : {0, 8}) {
            spit(files + "/sd" + std::to_string(method) + ".zip",
                 zipfx::build({mem("README.txt", pattern(7)), mem("Card.IMG", card, static_cast<uint16_t>(method))}));
            std::string path, why, member;
            const bool ok = pa::extract_single_sd_image(files + "/sd" + std::to_string(method) + ".zip",
                                                         path, why, &member);
            const bool bytes = ok && slurp(path) == card;
            const bool name = ok && fs::path(path).filename() == pa::extraction_name("Card.IMG") &&
                              member == "Card.IMG";
            if (method == 0)
                check("ZIPL-35", "a stored-method zip with one .img member extracts byte-identically under its extraction name",
                      bytes && name, path + " " + why);
            else
                check("ZIPL-36", "a deflated .img member extracts byte-identically under its extraction name",
                      bytes && name, path + " " + why);
        }
    }
    {
        spit(files + "/sdnone.zip", zipfx::build({mem("a.txt", pattern(3)), mem("b.nex", pattern(4))}));
        spit(files + "/sdtwo.zip", zipfx::build({mem("one.img", pattern(3)), mem("two.IMG", pattern(4))}));
        std::string p1, p2, w1, w2;
        const bool n1 = pa::extract_single_sd_image(files + "/sdnone.zip", p1, w1);
        const bool n2 = pa::extract_single_sd_image(files + "/sdtwo.zip", p2, w2);
        check("ZIPL-37", "no .img member is refused naming the members it holds; two are refused naming both",
              !n1 && !n2 && has(w1, "holds no SD card image") && has(w1, "a.txt") && has(w1, "b.nex") &&
                  p1.empty() && p2.empty() && has(w2, "holds 2 SD card images") && has(w2, "one.img") &&
                  has(w2, "two.IMG"),
              w1 + " | " + w2);
    }
    {
        spit(files + "/sdmac.zip", zipfx::build({mem("__MACOSX/._card.img", pattern(5)), mem("card.img", pattern(60)),
                                                  mem("._other.img", pattern(6))}));
        std::string path, why, member;
        const bool ok = pa::extract_single_sd_image(files + "/sdmac.zip", path, why, &member);
        check("ZIPL-38", "__MACOSX/ entries and ._ resource forks are ignored: the real card.img extracts",
              ok && member == "card.img" && slurp(path) == pattern(60), path + " " + why + " " + member);
    }
    {
        Member m = mem("bad.img", pattern(80));
        m.crc_override = static_cast<int64_t>(crc32(0, pattern(80).data(), 80)) + 1;
        spit(files + "/sdbad.zip", zipfx::build({m}));
        auto count_files = [] {
            const std::string d = pa::process_scratch().process_dir();
            size_t n = 0;
            std::error_code e;
            if (!d.empty() && fs::exists(d, e))
                for (auto it = fs::recursive_directory_iterator(d, e); it != fs::recursive_directory_iterator(); it.increment(e))
                    if (it->is_regular_file(e)) ++n;
            return n;
        };
        const size_t before = count_files();
        std::string path, why;
        const bool ok = pa::extract_single_sd_image(files + "/sdbad.zip", path, why);
        check("ZIPL-39", "a member with a corrupt CRC is refused and no output file is left in the scratch directory",
              !ok && has(why, "CRC") && path.empty() && count_files() == before, why);
    }

    run_data_archive_rows(root, lores);

    fs::remove_all(root, ec);
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
