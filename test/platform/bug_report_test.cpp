// ===========================================================================
// The bug report writer (GH #492): ONE zip holding the diagnostic state of the machine
// and of jnext, and the rule that it never carries NextZXOS, ROM or other distribution
// content.
//
// NO VHDL ORACLE, deliberately. What is under test is a host-side file: its structure,
// the rule for when RAM may ship, privacy, and that every decoded field agrees with the
// facade (`jnext::dbg::Debugger`) and the VHDL-verified machine the facade reads. The
// oracles are therefore: the facade's own answers, files on disk, the .jns state walk
// (for the key table), and the issue's rules.
//
// THE HARD RULE is checked END TO END: a synthetic "card" with ROM files of a known
// non-fill byte pattern is mounted, and no 32-byte window (stride 16, fill windows
// excluded) of those files, or of the embedded boot ROM, occurs in any decompressed
// member of a report taken from a card session.
//
// The owner dropped the check of a program file against card images ("we are not a
// policing emulator"); BR-10 and BR-26 pin what remains: the file ships unless it changed
// on disk since it was loaded, cannot be read, or is too large for a zip member.
//
// Run: ./build/test/bug_report_test
// ===========================================================================
#include "core/emulator.h"
#include "core/embedded_nextboot_rom.h"
#include "core/fat32_image.h"
#include "core/log.h"
#include "core/screenshot.h"
#include "core/sha256.h"
#include "debug/debugger.h"
#include "debug/disasm_text.h"
#include "debug/nextreg_names.h"
#include "platform/bug_report.h"
#include "platform/bug_report_at_exit.h"
#include "platform/config_keys.h"
#include "save/zip_archive.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace dbg = jnext::dbg;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

std::string g_tmp;   // this process's scratch directory

std::string tmp_path(const std::string& name) { return (fs::path(g_tmp) / name).string(); }

void write_bytes(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// A deterministic non-periodic byte stream (xorshift32).
std::vector<uint8_t> noise(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed ? seed : 1;
    for (size_t i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        v[i] = static_cast<uint8_t>(x >> 11);
    }
    return v;
}

// ---- the zip, parsed ------------------------------------------------------

struct Report {
    bool ok = false;
    std::vector<uint8_t> bytes;
    jnext::zip::Reader zip;
    bug_report::Outcome out;

    bool has(const std::string& m) const { return zip.has(m); }
    std::vector<uint8_t> member(const std::string& m) const {
        std::vector<uint8_t> b; std::string why;
        zip.read(m, b, why);
        return b;
    }
    std::string text(const std::string& m) const {
        const auto b = member(m);
        return std::string(b.begin(), b.end());
    }
    json js(const std::string& m) const { return json::parse(text(m), nullptr, false); }
    std::vector<std::string> names() const {
        std::vector<std::string> n;
        for (const auto& e : zip.entries()) n.push_back(e.name);
        return n;
    }
};

// A session: a machine and its debugger (the debugger first to go).
struct Session {
    std::unique_ptr<Emulator> emu;
    std::unique_ptr<dbg::Debugger> d;
};

EmulatorConfig nocard_config(int rewind_frames = 0) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.no_sdcard = true;
    cfg.load_file = "program.nex";   // a --no-sdcard session always has a program: no boot ROM
    cfg.rewind_buffer_frames = rewind_frames;
    return cfg;
}

Session open_session(const EmulatorConfig& cfg) {
    Session s;
    s.emu = std::make_unique<Emulator>();
    s.emu->init(cfg);
    s.d = std::make_unique<dbg::Debugger>(*s.emu);
    return s;
}

void leave_config_mode(Emulator& emu) {
    emu.port().out(0x243B, 0x03);
    emu.port().out(0x253B, 0x01);
}

// A no-card session with known patterns in bank 5 ($4000), bank 7 (page 0x0E at $C000) and
// the program's own pattern at $8000 (a loaded file, so it is a program too).
std::vector<uint8_t> g_pat_bank5, g_pat_bank7, g_pat_prog;
std::string g_prog_path;

Session nocard_session(int rewind_frames = 0, bool with_program = true) {
    Session s = open_session(nocard_config(rewind_frames));
    Emulator& emu = *s.emu;
    leave_config_mode(emu);
    for (size_t i = 0; i < g_pat_bank5.size(); ++i) emu.mmu().write(static_cast<uint16_t>(0x4000 + i), g_pat_bank5[i]);
    emu.port().out(0x243B, 0x56);   // slot 6 = page 0x0E (bank 7, first half)
    emu.port().out(0x253B, 0x0E);
    for (size_t i = 0; i < g_pat_bank7.size(); ++i) emu.mmu().write(static_cast<uint16_t>(0xC000 + i), g_pat_bank7[i]);
    if (with_program) emu.inject_binary(g_prog_path, 0x8000, 0x8000);
    return s;
}

// ---- the synthetic card ----------------------------------------------------

struct FakeRom { const char* name; size_t size; uint32_t seed; };
const FakeRom kFakeRoms[] = {
    { "48.rom",       0x4000, 11 },
    { "128.rom",      0x8000, 12 },
    { "plus3.rom",    0x10000, 13 },
    { "enNxtmmc.rom", 0x2000, 14 },
    { "enNextMf.rom", 0x2000, 15 },
};

std::string g_card;   // the synthetic card image

Fat32Node file_node(const std::string& name, const std::vector<uint8_t>& data) {
    Fat32Node n; n.name = name; n.is_dir = false; n.data = data; return n;
}

bool make_card(const std::string& path) {
    const uint32_t part_lba = 63;
    const uint32_t total_sectors = 70000;   // 512-byte clusters: > 65525, a valid FAT32
    const uint64_t bytes = static_cast<uint64_t>(part_lba + total_sectors) * 512;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::vector<uint8_t> mbr(512, 0);
        uint8_t* pe = mbr.data() + 0x1BE;
        pe[4] = 0x0C;
        for (int i = 0; i < 4; ++i) pe[8 + i] = static_cast<uint8_t>(part_lba >> (8 * i));
        for (int i = 0; i < 4; ++i) pe[12 + i] = static_cast<uint8_t>(total_sectors >> (8 * i));
        mbr[510] = 0x55; mbr[511] = 0xAA;
        f.write(reinterpret_cast<const char*>(mbr.data()), 512);
        f.seekp(static_cast<std::streamoff>(bytes - 1));
        const char z = 0;
        f.write(&z, 1);
        if (!f.good()) return false;
    }
    Fat32Node next; next.name = "NEXT"; next.is_dir = true;
    for (const FakeRom& r : kFakeRoms) next.children.push_back(file_node(r.name, noise(r.size, r.seed)));
    Fat32Node machines; machines.name = "MACHINES"; machines.is_dir = true;
    machines.children.push_back(next);
    Fat32Tree tree;
    tree.root.push_back(machines);
    Fat32WriteOptions opts;
    opts.au_size = 512;
    std::string err;
    return fat32_format_and_populate(path, part_lba, total_sectors, tree, err, opts);
}

Session card_session() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = g_card;
    cfg.sd_card_readonly = true;
    cfg.load_file = "program.nex";
    Session s = open_session(cfg);
    leave_config_mode(*s.emu);
    s.emu->inject_binary(g_prog_path, 0x8000, 0x8000);
    return s;
}

// ---- reports -----------------------------------------------------------------

bug_report::Inputs inputs() {
    bug_report::Inputs in;
    in.argv = { "jnext", "--headless", "--no-sdcard" };
    in.frontend = "headless";
    in.toolkit = "test";
    return in;
}

Report make_report(Session& s, const bug_report::Inputs& in = inputs()) {
    Report r;
    r.bytes = bug_report::build(*s.emu, *s.d, in, r.out);
    std::string why;
    r.ok = r.out.ok && r.zip.open(r.bytes.data(), r.bytes.size(), why);
    if (!r.ok) std::printf("  (report failed: %s %s)\n", r.out.why.c_str(), why.c_str());
    return r;
}

// ---- the content scan --------------------------------------------------------

constexpr size_t kWindow = 32;

bool is_fill_window(const uint8_t* p) {
    for (size_t period = 1; period <= 4; ++period) {
        bool same = true;
        for (size_t i = 0; i + period < kWindow; ++i)
            if (p[i] != p[i + period]) { same = false; break; }
        if (same) return true;
    }
    return false;
}

void add_windows(std::unordered_set<std::string>& set, const uint8_t* d, size_t n) {
    for (size_t i = 0; i + kWindow <= n; i += 16)
        if (!is_fill_window(d + i)) set.insert(std::string(reinterpret_cast<const char*>(d + i), kWindow));
}

// Every offset of `hay`, so a copy at any alignment is found.
size_t count_windows_in(const std::unordered_set<std::string>& set, const std::vector<uint8_t>& hay) {
    size_t hits = 0;
    for (size_t i = 0; i + kWindow <= hay.size(); ++i)
        if (set.count(std::string(reinterpret_cast<const char*>(hay.data() + i), kWindow))) ++hits;
    return hits;
}

size_t count_windows_in_report(const std::unordered_set<std::string>& set, const Report& r) {
    size_t hits = 0;
    for (const std::string& n : r.names()) hits += count_windows_in(set, r.member(n));
    return hits;
}

bool contains(const std::vector<uint8_t>& hay, const std::string& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

bool report_contains(const Report& r, const std::string& needle) {
    for (const std::string& n : r.names())
        if (contains(r.member(n), needle)) return true;
    return false;
}

std::tm fixed_tm() {
    std::tm t{};
    t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 20; t.tm_hour = 15; t.tm_min = 30; t.tm_sec = 5;
    return t;
}

uint32_t be32(const std::vector<uint8_t>& v, size_t off) {
    return (uint32_t(v[off]) << 24) | (uint32_t(v[off + 1]) << 16) | (uint32_t(v[off + 2]) << 8) | v[off + 3];
}

bool is_png(const std::vector<uint8_t>& b, uint32_t& w, uint32_t& h) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 24 || std::memcmp(b.data(), sig, 8) != 0) return false;
    w = be32(b, 16); h = be32(b, 20);
    return true;
}

}  // namespace

int main() {
    Log::init();
    Log::parse_levels("info");
    g_tmp = jtp::make_temp_dir("jnext-bugreport-test-");
    if (g_tmp.empty()) { std::printf("FATAL: no scratch directory\n"); return 1; }

    g_pat_bank5 = noise(0x400, 101);
    g_pat_bank7 = noise(0x400, 202);
    g_pat_prog  = noise(0x300, 303);
    g_prog_path = tmp_path("prog.bin");
    write_bytes(g_prog_path, g_pat_prog);
    g_card = tmp_path("fake-card.img");
    const bool card_ok = make_card(g_card);
    std::printf("synthetic card: %s\n", card_ok ? "built" : "FAILED");

    // ---- BR-01 manifest structure ---------------------------------------------
    {
        Session s = nocard_session();
        Report r = make_report(s);
        json m = r.js("manifest.json");
        bool names_ok = r.ok && m.is_object() && m.value("kind", "") == "jnext-bug-report" &&
                        m.value("format_version", 0) == 1;
        std::set<std::string> declared, actual;
        bool sizes_ok = true;
        if (names_ok) {
            for (const auto& e : m["members"]) {
                declared.insert(e["name"].get<std::string>());
                const auto* ent = r.zip.find(e["name"].get<std::string>());
                if (!ent || ent->uncomp_size != e["bytes"].get<uint64_t>()) sizes_ok = false;
            }
            for (const std::string& n : r.names()) if (n != "manifest.json") actual.insert(n);
        }
        check("BR-01", "manifest: kind and version, and its member list equals the zip's entries, with sizes",
              names_ok && declared == actual && sizes_ok && !actual.empty() && r.names().front() == "manifest.json",
              fmt("ok=%d declared=%zu actual=%zu sizes=%d", names_ok, declared.size(), actual.size(), sizes_ok));
    }

    // ---- BR-02 file name rule --------------------------------------------------
    {
        const std::tm t = fixed_tm();
        const std::string a = bug_report::file_name("game.nex", t);
        const std::string b = bug_report::file_name("my game!.tap", t);
        const std::string c = bug_report::file_name("", t);
        const std::string d = bug_report::file_name(std::string(100, 'x') + ".nex", t);
        const std::string e = bug_report::file_name(".hidden.nex", t);
        const std::string f = bug_report::file_name("game.nex", t, 2);
        // A taken name gets -2 (resolve_path), checked on a real directory.
        const std::string dir = tmp_path("namedir");
        fs::create_directories(dir);
        std::string p1, p2, why;
        bug_report::resolve_path(dir, "game.nex", t, p1, why);
        { std::ofstream(p1) << "x"; }
        bug_report::resolve_path(dir, "game.nex", t, p2, why);
        // Local time, not UTC: with a zone far from UTC the fields differ from gmtime's.
        const char* old_tz = std::getenv("TZ");
        const std::string saved_tz = old_tz ? old_tz : "";
        jtp::set_env("TZ", "IST-5:30");
#ifdef _WIN32
        _tzset();
#else
        tzset();
#endif
        const std::tm local = bug_report::local_now();
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        const bool local_not_utc = (local.tm_hour * 60 + local.tm_min) != (utc.tm_hour * 60 + utc.tm_min);
        if (old_tz) jtp::set_env("TZ", saved_tz.c_str()); else jtp::unset_env("TZ");
#ifdef _WIN32
        _tzset();
#else
        tzset();
#endif
        check("BR-02", "file name: <name>.YYYYMMDDhhmm.jnext-bug.zip, sanitised, cut at 64, -2 when taken, local time",
              a == "game.202611201530.jnext-bug.zip" && b == "my_game_.202611201530.jnext-bug.zip" &&
              c == "jnext.202611201530.jnext-bug.zip" &&
              d == std::string(64, 'x') + ".202611201530.jnext-bug.zip" &&
              e == "_hidden.202611201530.jnext-bug.zip" && f == "game.202611201530-2.jnext-bug.zip" &&
              fs::path(p2).filename().string() == "game.202611201530-2.jnext-bug.zip" && local_not_utc,
              fmt("a=%s b=%s c=%s e=%s f=%s p2=%s local_not_utc=%d", a.c_str(), b.c_str(), c.c_str(),
                  e.c_str(), f.c_str(), p2.c_str(), local_not_utc));
    }

    // ---- BR-03 --bug-report path resolution -----------------------------------
    {
        const std::tm t = fixed_tm();
        const std::string existing = tmp_path("pdir");
        fs::create_directories(existing);
        std::string p_dir, p_slash, p_file, p_bs, why;
        const bool r1 = bug_report::resolve_path(existing, "game.nex", t, p_dir, why);
        const bool r2 = bug_report::resolve_path(tmp_path("newdir") + "/", "game.nex", t, p_slash, why);
        const bool r3 = bug_report::resolve_path(tmp_path("plain.zip"), "game.nex", t, p_file, why);
        const std::string bs_arg = tmp_path("bsdir") + "\\";
        const bool r4 = bug_report::resolve_path(bs_arg, "game.nex", t, p_bs, why);
        const std::string leaf = "game.202611201530.jnext-bug.zip";
#ifdef _WIN32
        const bool bs_ok = r4 && fs::path(p_bs).filename().string() == leaf;   // a trailing backslash is a directory
#else
        const bool bs_ok = r4 && p_bs == bs_arg;                                // a plain file name on POSIX
#endif
        check("BR-03", "--bug-report PATH: existing dir or trailing separator = directory, else the file exactly",
              r1 && r2 && r3 && bs_ok &&
              fs::path(p_dir).filename().string() == leaf && fs::path(p_dir).parent_path() == fs::path(existing) &&
              fs::path(p_slash).filename().string() == leaf && fs::is_directory(tmp_path("newdir")) &&
              p_file == tmp_path("plain.zip"),
              fmt("dir=%s slash=%s file=%s bs=%s", p_dir.c_str(), p_slash.c_str(), p_file.c_str(), p_bs.c_str()));
    }

    // ---- BR-04 no-card session ships RAM --------------------------------------
    {
        Session s = nocard_session();
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const std::vector<uint8_t> ram = r.member("memory/ram.bin");
        const std::vector<uint8_t> cpu = r.member("memory/cpu.bin");
        const size_t pages = std::min<size_t>(0xE0, s.emu->ram().size() / 0x2000 - 0x20);
        bool bank5 = ram.size() >= 0x0A * 0x2000 + g_pat_bank5.size() &&
                     std::equal(g_pat_bank5.begin(), g_pat_bank5.end(), ram.begin() + 0x0A * 0x2000);
        bool bank7 = ram.size() >= 0x0E * 0x2000 + g_pat_bank7.size() &&
                     std::equal(g_pat_bank7.begin(), g_pat_bank7.end(), ram.begin() + 0x0E * 0x2000);
        bool prog_in_ram = ram.size() >= 4 * 0x2000 + g_pat_prog.size() &&
                           std::equal(g_pat_prog.begin(), g_pat_prog.end(), ram.begin() + 4 * 0x2000);
        bool prog_in_cpu = cpu.size() == 0x10000 &&
                           std::equal(g_pat_prog.begin(), g_pat_prog.end(), cpu.begin() + 0x8000);
        check("BR-04", "no-card session: memory included; ram.bin pages hold what the guest wrote; cpu.bin the program",
              r.ok && m["memory"]["included"].get<bool>() && ram.size() == pages * 0x2000 &&
              bank5 && bank7 && prog_in_ram && prog_in_cpu && r.has("memory/patterns.bin") &&
              r.has("memory/cpu-disasm.txt"),
              fmt("ok=%d size=%zu pages=%zu b5=%d b7=%d pr=%d pc=%d", r.ok, ram.size(), pages, bank5, bank7,
                  prog_in_ram, prog_in_cpu));
    }

    // ---- BR-05 card session withholds RAM; no ROM window in any member --------
    std::unordered_set<std::string> rom_windows;
    for (const FakeRom& fr : kFakeRoms) { const auto b = noise(fr.size, fr.seed); add_windows(rom_windows, b.data(), b.size()); }
    {
        Session s = card_session();
        // Precondition: the machine really holds the card's ROM bytes, so the scan below proves something.
        size_t in_sram = 0;
        for (uint16_t p = 0; p < 0x20; ++p) {
            const uint8_t* pg = s.emu->ram().page_ptr(p);
            if (pg) in_sram += count_windows_in(rom_windows, std::vector<uint8_t>(pg, pg + 0x2000));
        }
        Report r = make_report(s);
        json m = r.js("manifest.json");
        bool no_mem_member = true;
        for (const std::string& n : r.names()) if (n.rfind("memory/", 0) == 0) no_mem_member = false;
        const size_t hits = r.ok ? count_windows_in_report(rom_windows, r) : 1;
        check("BR-05", "card session: RAM withheld (card-session), no memory member, no window of the card's ROM files anywhere",
              card_ok && in_sram > 0 && r.ok && !m["memory"]["included"].get<bool>() &&
              m["memory"]["withheld"]["code"] == "card-session" && no_mem_member && hits == 0,
              fmt("card=%d precondition_windows=%zu included=%d hits=%zu", card_ok, in_sram,
                  r.ok ? int(m["memory"]["included"].get<bool>()) : -1, hits));
    }

    // ---- BR-06 boot ROM ran ---------------------------------------------------
    {
        EmulatorConfig cfg = nocard_config();
        cfg.load_file.clear();   // a program-less --no-sdcard start: the boot ROM is installed
        Session s = open_session(cfg);
        std::vector<uint8_t> first(kWindow);
        s.d->peek(dbg::MemSpace::cpu(), 0, kWindow, first.data());
        const uint8_t* rom = embedded_nextboot_rom_data();
        const bool visible = std::memcmp(first.data(), rom, kWindow) == 0;   // the ROM is what the CPU sees at 0
        for (int i = 0; i < 3; ++i) s.emu->run_frame();
        std::unordered_set<std::string> boot_windows;
        add_windows(boot_windows, rom, embedded_nextboot_rom_size());
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const size_t hits = r.ok ? count_windows_in_report(boot_windows, r) : 1;
        check("BR-06", "boot ROM ran: RAM withheld (boot-rom-ran), no window of nextboot.rom in any member",
              visible && r.ok && !m["memory"]["included"].get<bool>() &&
              m["memory"]["withheld"]["code"] == "boot-rom-ran" && hits == 0 && !boot_windows.empty(),
              fmt("visible=%d hits=%zu windows=%zu", visible, hits, boot_windows.size()));
    }

    // ---- BR-07 a ring restore keeps RAM ---------------------------------------
    {
        Session s = nocard_session(20);
        for (int i = 0; i < 10; ++i) s.emu->run_frame();
        const uint32_t before = s.emu->frame_num();
        const bool rewound = s.emu->rewind_to_frame(before - 5);
        Report r = make_report(s);
        check("BR-07", "a rewind restores the session's own history: RAM still ships",
              rewound && s.emu->frame_num() < before && r.ok && r.out.memory_included &&
              s.emu->ram_withheld() == Emulator::RamWithheld::None,
              fmt("rewound=%d frame %u -> %u withheld=%d", rewound, before, s.emu->frame_num(),
                  static_cast<int>(s.emu->ram_withheld())));
    }

    // ---- BR-08 a foreign restore withholds RAM --------------------------------
    {
        Session s = nocard_session(20);
        for (int i = 0; i < 3; ++i) s.emu->run_frame();
        StateWriter measure;
        s.emu->save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        s.emu->save_state(w);
        StateReader rd(buf.data(), buf.size());
        const bool loaded = s.emu->load_state(rd);
        Report r = make_report(s);
        json m = r.js("manifest.json");
        bool no_mem_member = true;
        for (const std::string& n : r.names()) if (n.rfind("memory/", 0) == 0) no_mem_member = false;
        check("BR-08", "a state loaded from outside the ring withholds RAM (state-restored)",
              loaded && r.ok && !r.out.memory_included && m["memory"]["withheld"]["code"] == "state-restored" &&
              no_mem_member,
              fmt("loaded=%d included=%d", loaded, r.out.memory_included));
    }

    // ---- BR-09 a soft reset keeps the verdict ---------------------------------
    {
        Session a = nocard_session();
        a.emu->soft_reset();
        const bool clean_after = a.emu->ram_withheld() == Emulator::RamWithheld::None;
        Session b = nocard_session(20);
        for (int i = 0; i < 3; ++i) b.emu->run_frame();
        StateWriter measure;
        b.emu->save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        b.emu->save_state(w);
        StateReader rd(buf.data(), buf.size());
        b.emu->load_state(rd);
        b.emu->soft_reset();
        check("BR-09", "a soft reset keeps the verdict: clean stays clean, a foreign restore stays withheld",
              clean_after && b.emu->ram_withheld() == Emulator::RamWithheld::StateRestored,
              fmt("a=%d b=%d", static_cast<int>(a.emu->ram_withheld()), static_cast<int>(b.emu->ram_withheld())));
    }

    // ---- BR-10 program included -----------------------------------------------
    {
        Session s = nocard_session();
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const auto prog = r.member("program/prog.bin");
        const json& p = m["programs"][0];
        check("BR-10", "program: its bytes ship, and the manifest's SHA-256 is the file's",
              r.ok && prog == g_pat_prog && p["included"].get<bool>() && p["member"] == "program/prog.bin" &&
              p["sha256"] == sdcard::sha256_file(g_prog_path) && p["bytes"].get<uint64_t>() == g_pat_prog.size(),
              fmt("ok=%d same=%d", r.ok, prog == g_pat_prog));
    }

    // ---- BR-11 program changed on disk ----------------------------------------
    {
        const std::string path = tmp_path("changing.bin");
        write_bytes(path, g_pat_prog);
        Session s = nocard_session(0, false);
        s.emu->inject_binary(path, 0x8000, 0x8000);
        std::vector<uint8_t> longer = g_pat_prog;
        longer.push_back(0x55);
        write_bytes(path, longer);
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const json& p = m["programs"][0];
        check("BR-11", "program rewritten after load: withheld (changed-since-load), SHA-256 only, RAM still ships",
              r.ok && !p["included"].get<bool>() && p["withheld"]["code"] == "changed-since-load" &&
              !r.has("program/changing.bin") && p["sha256"] == sdcard::sha256_file(path) && r.out.memory_included,
              fmt("ok=%d code=%s mem=%d", r.ok, r.ok ? p["withheld"]["code"].dump().c_str() : "-", r.out.memory_included));
    }

    // ---- BR-12 program no longer readable -------------------------------------
    {
        const std::string path = tmp_path("vanishing.bin");
        write_bytes(path, g_pat_prog);
        Session s = nocard_session(0, false);
        s.emu->inject_binary(path, 0x8000, 0x8000);
        fs::remove(path);
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const json& p = m["programs"][0];
        check("BR-12", "program deleted after load: withheld (unreadable), no program member",
              r.ok && !p["included"].get<bool>() && p["withheld"]["code"] == "unreadable" &&
              !r.has("program/vanishing.bin"),
              fmt("ok=%d", r.ok));
    }

    // ---- BR-13 redaction ------------------------------------------------------
    {
        bug_report::RedactContext ctx;
        ctx.home = "/home/zq7user";
        ctx.users = { "zq7user" };
        const std::string a = bug_report::redact("loading /home/zq7user/x/game.nex now", ctx);
        const std::string b = bug_report::redact("C:\\Users\\zq7user\\game.nex", ctx);
        const std::string c = bug_report::redact("\\\\srv\\share\\a.img", ctx);
        const std::string d = bug_report::redact("hello zq7user", ctx);
        const std::string e = bug_report::redact("--sdcard=/mnt/cards/big.img (ok)", ctx);
        const std::string f = bug_report::redact("D:/data/x/y.tap and 1/2 and http://host/x", ctx);
        // End to end: the process environment names the user; nothing in a report may.
        const char* old_home = std::getenv("HOME");
        const char* old_user = std::getenv("USER");
        const char* old_logname = std::getenv("LOGNAME");
        const std::string sh = old_home ? old_home : "", su = old_user ? old_user : "", sl = old_logname ? old_logname : "";
        jtp::set_env("HOME", "/home/zq7user");
        jtp::set_env("USER", "zq7user");
        jtp::set_env("LOGNAME", "zq7user");
        Log::platform()->info("bugreport loading /home/zq7user/x/game.nex for zq7user");
        bug_report::Inputs in = inputs();
        in.argv = { "/home/zq7user/bin/jnext", "--load=/home/zq7user/x/game.nex", "--headless" };
        in.config_file = { { "paths.sd_card_path", "/home/zq7user/cards/c.img" }, { "startup.machine_type", "next" } };
        in.config_file_read = true;
        Session s = nocard_session();
        Report r = make_report(s, in);
        const bool leaked = r.ok && (report_contains(r, "zq7user") || report_contains(r, "/home/"));
        const json st = r.ok ? r.js("settings.json") : json();
        const bool argv_ok = r.ok && st["command_line"][0] == "<path>/jnext" &&
                             st["command_line"][1] == "--load=<path>/game.nex" && st["command_line"][2] == "--headless";
        if (old_home) jtp::set_env("HOME", sh.c_str()); else jtp::unset_env("HOME");
        if (old_user) jtp::set_env("USER", su.c_str()); else jtp::unset_env("USER");
        if (old_logname) jtp::set_env("LOGNAME", sl.c_str()); else jtp::unset_env("LOGNAME");
        check("BR-13", "redaction: POSIX, Windows and UNC paths and the user name, in text and in a whole report",
              a == "loading <path>/game.nex now" && b == "<path>/game.nex" && c == "<path>/a.img" &&
              d == "hello <user>" && e == "--sdcard=<path>/big.img (ok)" &&
              f == "<path>/y.tap and 1/2 and <path>/x" && !leaked && argv_ok,
              fmt("a=%s b=%s c=%s d=%s e=%s f=%s leaked=%d argv=%d", a.c_str(), b.c_str(), c.c_str(), d.c_str(),
                  e.c_str(), f.c_str(), leaked, argv_ok));
    }

    // ---- BR-14 log tail -------------------------------------------------------
    {
        Log::parse_levels("info");
        for (int i = 0; i < 600; ++i) Log::platform()->info("bugreport-line-{}", i);
        Log::platform()->set_level(spdlog::level::debug);
        Log::platform()->debug("bugreport-debug-line");
        Log::platform()->set_level(spdlog::level::info);
        const std::vector<std::string> tail = Log::tail();
        Session s = nocard_session();
        Report r = make_report(s);
        std::vector<std::string> lines;
        {
            const std::string t = r.text("log.txt");
            size_t at = 0;
            while (at < t.size()) {
                size_t nl = t.find('\n', at);
                if (nl == std::string::npos) nl = t.size();
                lines.push_back(t.substr(at, nl - at));
                at = nl + 1;
            }
        }
        bool no_debug = true;
        for (const std::string& l : lines) if (l.find("bugreport-debug-line") != std::string::npos) no_debug = false;
        check("BR-14", "log tail: the last 500 info-and-above lines, newest last, no debug line",
              tail.size() == 500 && r.ok && lines.size() == 500 &&
              lines.back().find("bugreport-line-599") != std::string::npos && no_debug,
              fmt("tail=%zu lines=%zu last=%s", tail.size(), lines.size(), lines.empty() ? "" : lines.back().c_str()));
    }

    // ---- BR-15 the key table equals the walk ----------------------------------
    {
        std::set<std::string> walked;
        auto collect = [&](Session& s) {
            std::vector<Emulator::StateDocument> docs;
            std::string why;
            s.emu->state_documents(docs, why);
            for (const auto& d : docs)
                for (const std::string& k : d.content_keys) walked.insert(d.subsystem + "." + k);
        };
        { Session s = nocard_session(); collect(s); }
        { Session s = card_session(); collect(s); }
        std::set<std::string> table;
        for (const std::string& k : bug_report::classified_keys()) table.insert(k);
        std::string missing_in_table, missing_in_walk;
        for (const auto& k : walked) if (!table.count(k)) missing_in_table += k + " ";
        for (const auto& k : table) if (!walked.count(k)) missing_in_walk += k + " ";
        check("BR-15", "the key class table lists exactly the arrays the state walk emits",
              !walked.empty() && missing_in_table.empty() && missing_in_walk.empty(),
              "walk-not-in-table: " + missing_in_table + " | table-not-in-walk: " + missing_in_walk);
    }

    // ---- BR-16 the filter, by session -----------------------------------------
    {
        auto keys_of = [&](const Report& r, bug_report::KeyClass c, bool want_present) {
            std::string bad;
            for (const std::string& k : bug_report::classified_keys()) {
                if (bug_report::key_class(k.substr(0, k.find('.')), k.substr(k.find('.') + 1)) != c) continue;
                const std::string sub = k.substr(0, k.find('.')), key = k.substr(k.find('.') + 1);
                const json doc = r.js("state/" + sub + ".json");
                const bool present = doc.is_object() && doc.contains(key);
                if (present != want_present) bad += k + " ";
            }
            return bad;
        };
        Session sc = card_session();
        Report rc = make_report(sc);
        Session sn = nocard_session();
        Report rn = make_report(sn);
        const std::string card_mem = keys_of(rc, bug_report::KeyClass::Memory, false);
        const std::string card_never = keys_of(rc, bug_report::KeyClass::Never, false);
        const std::string card_state = keys_of(rc, bug_report::KeyClass::State, true);
        const std::string nc_mem = keys_of(rn, bug_report::KeyClass::Memory, true);
        const std::string nc_never = keys_of(rn, bug_report::KeyClass::Never, false);
        check("BR-16", "state documents: a card session drops Memory and Never keys, a no-card one keeps Memory, never Never",
              rc.ok && rn.ok && card_mem.empty() && card_never.empty() && card_state.empty() && nc_mem.empty() &&
              nc_never.empty(),
              "card mem: " + card_mem + " never: " + card_never + " state: " + card_state +
              " | nocard mem: " + nc_mem + " never: " + nc_never);
    }

    // ---- BR-17 decoded machine ------------------------------------------------
    {
        Session s = nocard_session();
        s.emu->port().out(0x243B, 0x15);   // NR 0x15 = a recognisable value
        s.emu->port().out(0x253B, 0x2A);
        Report r = make_report(s);
        const json mj = r.js("machine.json");
        const Z80Registers regs = s.d->registers();
        bool regs_ok = r.ok && mj["registers"]["PC"] == regs.PC && mj["registers"]["SP"] == regs.SP &&
                       mj["registers"]["AF"] == regs.AF && mj["registers"]["HL"] == regs.HL &&
                       mj["registers"]["IM"] == regs.IM;
        bool nr_ok = r.ok && mj["nextregs"].size() == 256;
        for (int i = 0; nr_ok && i < 256; ++i) {
            const auto& e = mj["nextregs"][i];
            if (e["reg"] != i || e["name"].get<std::string>() != dbg::nextreg_name(static_cast<uint8_t>(i)) ||
                e["value"] != s.d->nextreg_peek(static_cast<uint8_t>(i)))
                nr_ok = false;
        }
        bool slots_ok = r.ok && mj["paging"]["slots"].size() == 8;
        const auto sl = s.d->mmu_slots();
        for (size_t i = 0; slots_ok && i < 8; ++i) {
            const auto& e = mj["paging"]["slots"][i];
            if (e["nr_page"] != sl[i].nr_page || e["effective_page"] != sl[i].effective_page ||
                e["is_rom"] != sl[i].is_rom || e["space"]["index"] != sl[i].space.index)
                slots_ok = false;
        }
        check("BR-17", "machine.json: registers, 256 NextREGs by name and value, and the 8 paging slots equal the facade's",
              regs_ok && nr_ok && slots_ok && mj["nextregs"][0x15]["value"] == 0x2A,
              fmt("regs=%d nr=%d slots=%d", regs_ok, nr_ok, slots_ok));
    }

    // ---- BR-18 pictures -------------------------------------------------------
    {
        Session s = nocard_session();
        for (int i = 0; i < 2; ++i) s.emu->run_frame();
        Report r = make_report(s);
        const dbg::ConstU32s fb = s.d->framebuffer();
        const int rows = static_cast<int>(fb.size / dbg::RENDER_WIDTH);
        const std::string ref = tmp_path("ref.png");
        const bool wrote = save_screenshot_png(ref, fb.data, static_cast<int>(dbg::RENDER_WIDTH), rows);
        const bool same = wrote && r.member("screen/composite.png") == read_bytes(ref);
        bool layers = true;
        for (const char* v : { "ula-primary", "ula-shadow", "layer2-active", "layer2-shadow", "sprites", "tilemap", "background" }) {
            uint32_t w = 0, h = 0;
            if (!r.has(std::string("screen/") + v + ".png") ||
                !is_png(r.member(std::string("screen/") + v + ".png"), w, h) || w != 640 || h != uint32_t(2 * rows))
                layers = false;
        }
        std::vector<uint8_t> enc;
        const bool enc_same = encode_png(fb.data, 640, rows, enc) && enc == r.member("screen/composite.png");
        check("BR-18", "pictures: composite.png is what save_screenshot_png writes; seven layer views, each a 640x(2*rows) PNG",
              r.ok && rows > 0 && same && layers && enc_same,
              fmt("rows=%d same=%d layers=%d enc=%d", rows, same, layers, enc_same));
    }

    // ---- BR-19 the report does not move the machine ---------------------------
    {
        Session s = nocard_session();
        s.emu->run_frame();
        for (int i = 0; i < 7; ++i) s.emu->debugger_step();   // mid-frame
        const dbg::Time t0 = s.d->time();
        const Z80Registers r0 = s.d->registers();
        Report r = make_report(s);
        const dbg::Time t1 = s.d->time();
        const Z80Registers r1 = s.d->registers();
        check("BR-19", "writing a report mid-frame changes neither the time nor the registers",
              r.ok && t0.master_cycle == t1.master_cycle && t0.tstates_total == t1.tstates_total &&
              t0.cycle_in_frame == t1.cycle_in_frame && t0.frame == t1.frame && t0.cycle_in_frame != 0 &&
              r0.PC == r1.PC && r0.SP == r1.SP && r0.AF == r1.AF && r0.BC == r1.BC && r0.DE == r1.DE &&
              r0.HL == r1.HL && r0.IX == r1.IX && r0.R == r1.R,
              fmt("cycle %llu -> %llu pc %04X -> %04X", (unsigned long long)t0.master_cycle,
                  (unsigned long long)t1.master_cycle, r0.PC, r1.PC));
    }

    // A program that loops forever incrementing A, with 64 NOPs before it, for the debugger rows.
    std::vector<uint8_t> loop_prog(64, 0x00);
    loop_prog.push_back(0x3C);   // INC A
    loop_prog.push_back(0x18);   // JR -3
    loop_prog.push_back(0xFD);
    const std::string loop_path = tmp_path("loop.bin");
    write_bytes(loop_path, loop_prog);

    auto debug_session = [&](bool card) {
        Session s;
        if (card) {
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2;
            cfg.sd_card_image = g_card;
            cfg.sd_card_readonly = true;
            cfg.load_file = "program.nex";
            s = open_session(cfg);
        } else {
            s = open_session(nocard_config());
        }
        leave_config_mode(*s.emu);
        s.emu->inject_binary(loop_path, 0x8000, 0x8000);
        dbg::ClientInfo ci;
        ci.name = "bug-report-test";
        const auto cid = s.d->attach(ci);
        dbg::Subscription sub;
        sub.kind = dbg::EventKind::Execute;
        sub.filter.lo = 0x9000;
        sub.filter.hi = 0x9000;
        sub.action = dbg::Action::Stop;
        s.d->subscribe(cid.value, sub);
        s.d->set_trace_enabled(true);
        for (int i = 0; i < 2; ++i) s.emu->run_frame();
        return s;
    };

    // ---- BR-20 debugger section, memory-derived fields ------------------------
    {
        Session sn = debug_session(false);
        Report rn = make_report(sn);
        Session sc = debug_session(true);
        Report rc = make_report(sc);
        const json dn = rn.js("debugger.json");
        const json dc = rc.js("debugger.json");
        const bool bps = dn["breakpoints"].size() == 1 && dc["breakpoints"].size() == 1 &&
                         dn["breakpoints"][0]["lo"] == 0x9000 && dn["breakpoints"][0]["kind"] == "execute";
        const bool trace_n = dn["trace"]["enabled"] == true && dn["trace"]["entries"].size() > 0 &&
                             dn["trace"]["entries"][0].contains("opcode_bytes");
        const bool trace_c = dc["trace"]["enabled"] == true && dc["trace"]["entries"].size() > 0 &&
                             !dc["trace"]["entries"][0].contains("opcode_bytes") &&
                             !dc["trace"]["entries"][0].contains("disasm");
        check("BR-20", "debugger.json: breakpoints and trace always; opcode bytes, disassembly and stack only with RAM",
              rn.ok && rc.ok && bps && trace_n && trace_c && dn.contains("disasm_at_pc") && dn.contains("stack") &&
              !dc.contains("disasm_at_pc") && !dc.contains("stack") && dn["stack"]["bytes"].size() == 32,
              fmt("bps=%d tn=%d tc=%d", bps, trace_n, trace_c));
    }

    // ---- BR-21 disassembly at PC ----------------------------------------------
    {
        Session s = nocard_session(0, false);
        s.emu->inject_binary(loop_path, 0x8000, 0x8000);
        Report r = make_report(s);
        const json d = r.js("debugger.json");
        const uint16_t pc = s.d->registers().PC;
        const uint16_t end = static_cast<uint16_t>(std::min<uint32_t>(0xFFFFu, uint32_t(pc) + 0xFFu));
        auto lines = disasm_text::collect_range(pc, end, s.d->memory_reader(), disasm_text::SymbolNamer{});
        if (lines.size() > 32) lines.resize(32);
        const std::string want = disasm_text::format_lines(lines, disasm_text::CopyFormat::WithAddresses);
        check("BR-21", "disassembly at PC: the first 32 lines equal the panel's copy of the range",
              r.ok && d["disasm_at_pc"] == want && lines.size() == 32 && pc == 0x8000,
              fmt("lines=%zu pc=%04X", lines.size(), pc));
    }

    // ---- BR-22 card identity --------------------------------------------------
    {
        Session s = card_session();
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const json& c = m["cards"][0];
        check("BR-22", "card: base name, size and SHA-256 of the image, never its bytes or directory",
              card_ok && r.ok && m["cards"].size() == 1 && c["name"] == "fake-card.img" &&
              c["sha256"] == sdcard::sha256_file(g_card) &&
              c["bytes"].get<uint64_t>() == fs::file_size(g_card) && c.contains("identity") &&
              !report_contains(r, g_tmp) && c["readonly"] == true,
              fmt("ok=%d", r.ok));
    }

    // ---- BR-23 settings blanking ----------------------------------------------
    {
        bug_report::Inputs in = inputs();
        in.config_file = {
            { "paths.sd_card_path", "/mnt/secret/card.img" },
            { "startup.machine_type", "next" },
            { "recent.files", "/a/b/c.nex" },
            { "invented.key", "value" },
        };
        in.config_file_read = true;
        Session s = nocard_session();
        Report r = make_report(s, in);
        const json st = r.js("settings.json");
        const config_keys::Key* k = config_keys::find("paths", "sd_card_path");
        check("BR-23", "settings: a Path value is blanked, a machine type kept, the recent history and unknown keys blanked",
              r.ok && k && k->type == config_keys::Type::Path && st["config_file"]["paths.sd_card_path"] == "<blanked>" &&
              st["config_file"]["startup.machine_type"] == "next" && st["config_file"]["recent.files"] == "<blanked>" &&
              st["config_file"]["invented.key"] == "<blanked>" && st["config_file_read"] == true &&
              !report_contains(r, "/mnt/secret"),
              fmt("ok=%d", r.ok));
    }

    // ---- BR-24 a hard init clears the program list ----------------------------
    {
        Session s = nocard_session();
        const std::string before = bug_report::program_name(*s.emu);
        s.emu->init(nocard_config());   // a hard init clears RAM, so the program that was in it
        const std::string after = bug_report::program_name(*s.emu);
        check("BR-24", "a hard init forgets the program: the file name falls back to jnext",
              before == "prog.bin" && after.empty() && s.emu->program_files().empty() &&
              bug_report::file_name(after, fixed_tm()) == "jnext.202611201530.jnext-bug.zip",
              fmt("before=%s after=%s", before.c_str(), after.c_str()));
    }

    // ---- BR-25 NextREG names unchanged by the move ----------------------------
    {
        check("BR-25", "nextreg_name(): the panel's strings, and --- for an unnamed register",
              std::string(dbg::nextreg_name(0x00)) == "Machine ID" && std::string(dbg::nextreg_name(0x15)) == "Spr/Layer Prior" &&
              std::string(dbg::nextreg_name(0x50)) == "MMU Slot 0" && std::string(dbg::nextreg_name(0xC0)) == "IM2 Control" &&
              std::string(dbg::nextreg_name(0x04)) == "---",
              dbg::nextreg_name(0x15));
    }

    // ---- BR-26 a program too large for a member -------------------------------
    {
        // A valid PCM WAV (8-bit mono 8 kHz) with 33 MiB of samples: a file jnext accepts as a
        // program whose size a zip member cannot hold.
        const std::string path = tmp_path("huge.wav");
        {
            const uint32_t data_bytes = 33u * 1024 * 1024;
            std::vector<uint8_t> hdr(44, 0);
            auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[o + i] = uint8_t(v >> (8 * i)); };
            auto put16 = [&](size_t o, uint16_t v) { hdr[o] = uint8_t(v); hdr[o + 1] = uint8_t(v >> 8); };
            std::memcpy(&hdr[0], "RIFF", 4); put32(4, 36 + data_bytes); std::memcpy(&hdr[8], "WAVE", 4);
            std::memcpy(&hdr[12], "fmt ", 4); put32(16, 16); put16(20, 1); put16(22, 1); put32(24, 8000);
            put32(28, 8000); put16(32, 1); put16(34, 8); std::memcpy(&hdr[36], "data", 4); put32(40, data_bytes);
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(hdr.data()), 44);
            f.seekp(44 + data_bytes - 1);
            const char z = 0x80;
            f.write(&z, 1);
        }
        Session s = nocard_session(0, false);
        const bool loaded = s.emu->load_wav(path);
        Report r = make_report(s);
        json m = r.js("manifest.json");
        const json& p = m["programs"][0];
        check("BR-26", "a program larger than a report member may be is withheld (too-large), with its SHA-256",
              loaded && r.ok && !p["included"].get<bool>() && p["withheld"]["code"] == "too-large" &&
              p["sha256"] == sdcard::sha256_file(path) && !r.has("program/huge.wav"),
              fmt("loaded=%d ok=%d", loaded, r.ok));
        fs::remove(path);
    }

    // ---- BR-27 two programs of one name ---------------------------------------
    {
        const std::string d1 = tmp_path("d1"), d2 = tmp_path("d2");
        fs::create_directories(d1);
        fs::create_directories(d2);
        write_bytes(d1 + "/same.bin", noise(64, 5));
        write_bytes(d2 + "/same.bin", noise(64, 6));
        Session s = nocard_session(0, false);
        s.emu->inject_binary(d1 + "/same.bin", 0x8000, 0x8000);
        s.emu->inject_binary(d2 + "/same.bin", 0x9000, 0x9000);
        Report r = make_report(s);
        check("BR-27", "two program files of one name: both ship, the second as program/same.bin-2; the last names the report",
              r.ok && r.member("program/same.bin") == noise(64, 5) && r.member("program/same.bin-2") == noise(64, 6) &&
              bug_report::program_name(*s.emu) == "same.bin",
              fmt("ok=%d", r.ok));
    }

    // ---- BR-28 --bug-report at exit -------------------------------------------
    {
        Session s = nocard_session();
        EmulatorConfig cfg = s.emu->config();
        const std::string dir = tmp_path("exitdir");
        cfg.bug_report_path = dir + "/";
        cfg.command_line = { "jnext", "--headless" };
        const bool ok1 = write_bug_report_at_exit(*s.emu, *s.d, cfg, "headless", "test");
        std::string found;
        for (const auto& e : fs::directory_iterator(dir)) found = e.path().filename().string();
        EmulatorConfig bad = cfg;
        bad.bug_report_path = tmp_path("no-such-dir/sub/file.zip");   // the parent does not exist
        const bool ok2 = write_bug_report_at_exit(*s.emu, *s.d, bad, "headless", "test");
        EmulatorConfig none = cfg;
        none.bug_report_path.clear();
        const bool ok3 = write_bug_report_at_exit(*s.emu, *s.d, none, "headless", "test");
        check("BR-28", "at exit: a directory gets <program>.<time>.jnext-bug.zip; an unwritable path fails; no option is a no-op",
              ok1 && found.rfind("prog.", 0) == 0 && found.size() > 15 &&
              found.compare(found.size() - 14, 14, ".jnext-bug.zip") == 0 && !ok2 && ok3,
              fmt("ok1=%d found=%s ok2=%d ok3=%d", ok1, found.c_str(), ok2, ok3));
    }

    std::error_code ec;
    fs::remove_all(g_tmp, ec);
    std::printf("\nTotal: %d, Passed: %d, Failed: %d\n", g_total, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
