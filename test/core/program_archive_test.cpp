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

#include "core/program_archive.h"
#include "save/zip_archive.h"
#include "save/zip_interchange.h"

#include "../row_id.h"
#include "../test_portable.h"
#include "../zip_fixture.h"

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
        std::vector<Member> ms;
        for (int i = 0; i < 65534; ++i) {
            char nm[16];
            std::snprintf(nm, sizeof nm, "N%05d.TAP", i);
            ms.push_back(mem(nm, Bytes{}, 0));
        }
        spit(files + "/many.zip", zipfx::build(ms));
        pa::ProgramArchive a;
        std::string why;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = a.open(files + "/many.zip", why);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check("ZIPL-28", "an archive of 65534 program members opens in linear time (under 4 s; a find() per member took 12 s)",
              ok && a.programs().size() == 65534 && secs < 4.0, why + " secs=" + std::to_string(secs));
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

    fs::remove_all(root, ec);
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
