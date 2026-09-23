// GH #31 — rows for --esxdos-stub-root: the host directory served through the
// esxDOS RST $08 calls.
//
// Two tiers, deliberately:
//   * the SANDBOX and the 8.3/timestamp synthesis are asserted against
//     EsxdosHostFs directly, because those are the security-relevant decisions
//     and a class-level row says exactly which input was refused and why;
//   * every REGISTER CONVENTION is asserted through the real
//     Emulator::on_esxdos_call dispatcher, because a row that calls the class
//     cannot catch a wrong register, a wrong byte order in the marshalled
//     entry, or a call claimed for a handle that belongs to another back end.
//
// Conventions come from the oracles, never from memory: hook codes and
// mode/attribute/error constants from tbblue
// src/asm/dot_commands/esxapi.def; per-call entry/exit registers from z88dk
// libsrc/_DEVELOPMENT/arch/zxn/esxdos/z80/asm_esx_*.asm (verbatim excerpts of
// the NextZXOS/esxDOS API document); the F_READDIR entry layout additionally
// from tbblue src/asm/readdir/readdir.asm, which parses one.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_hostfs.h"
#include "core/saveable.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid() — per-process fixture paths

namespace fs = std::filesystem;

namespace {

int* g_passed = nullptr;
int* g_failed = nullptr;

void hcheck(const char* id, const char* desc, bool condition,
            const std::string& detail = "") {
    if (condition) {
        ++*g_passed;
    } else {
        ++*g_failed;
        std::printf("FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

std::string hex2(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02X", v & 0xFF);
    return b;
}

void put_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// ── Emulator-level helpers ───────────────────────────────────────────────

bool esx(Emulator& emu, uint8_t code, Z80Registers& r) {
    return emu.cpu().on_esxdos_call && emu.cpu().on_esxdos_call(code, r);
}
bool cy(const Z80Registers& r) { return (r.AF & 1) != 0; }
uint8_t rega(const Z80Registers& r) { return static_cast<uint8_t>(r.AF >> 8); }

void poke_str(Emulator& emu, uint16_t at, const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(at + i),
                        static_cast<uint8_t>(s[i]));
    emu.mmu().write(static_cast<uint16_t>(at + s.size()), 0);
}

std::string peek_str(Emulator& emu, uint16_t at, uint16_t& end) {
    std::string s;
    uint16_t p = at;
    for (int i = 0; i < 300; ++i) {
        const uint8_t c = emu.mmu().read(p++);
        if (c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    end = p;
    return s;
}

constexpr uint16_t kName = 0x8000;
constexpr uint16_t kBuf  = 0x8200;
constexpr uint16_t kStat = 0x8400;

/// F_OPEN through the dispatcher. Returns the handle, or 0 and sets `err`.
uint8_t do_open(Emulator& emu, const std::string& name, uint8_t mode,
                uint8_t& err) {
    poke_str(emu, kName, name);
    Z80Registers r{};
    r.IX = kName;
    r.BC = static_cast<uint16_t>(mode << 8);
    const bool handled = esx(emu, 0x9A, r);
    if (!handled) { err = 0xFF; return 0; }
    if (cy(r)) { err = rega(r); return 0; }
    err = 0;
    return rega(r);
}

}  // namespace

void run_esxdos_hostfs_rows(int& passed, int& failed);

void run_esxdos_hostfs_rows(int& passed, int& failed)
{
    g_passed = &passed;
    g_failed = &failed;

    // ── Fixture tree ──────────────────────────────────────────────────────
    //
    //   <root>/hello.txt            "HELLO-HOSTFS"
    //   <root>/Mixed.DAT            "mixed"
    //   <root>/a long name.txt      "long"          -> 8.3 synthesis
    //   <root>/another long.txt     "long2"         -> ~2 disambiguation
    //   <root>/sub/inner.bin        "INNER"         -> subdirectory access
    //   <root>/link-out             symlink -> <outside>/secret.txt
    //   <root>/link-dir             symlink -> <outside>
    //   <root>/link-in              symlink -> <root>/hello.txt
    //   <outside>/secret.txt        "SECRET"        -> must never be reachable
    const std::string tag = std::to_string(static_cast<long>(getpid()));
    const fs::path base   = fs::temp_directory_path() / ("jnext-hostfs-" + tag);
    const fs::path root   = base / "root";
    const fs::path outside = base / "outside";
    std::error_code rm_ec;
    fs::remove_all(base, rm_ec);

    put_file(root / "hello.txt", "HELLO-HOSTFS");
    put_file(root / "Mixed.DAT", "mixed");
    put_file(root / "a long name.txt", "long");
    put_file(root / "another long.txt", "long2");
    put_file(root / "sub" / "inner.bin", "INNER");
    put_file(outside / "secret.txt", "SECRET");
    std::error_code ln_ec;
    fs::create_symlink(outside / "secret.txt", root / "link-out", ln_ec);
    const bool have_symlinks = !ln_ec;
    fs::create_directory_symlink(outside, root / "link-dir", ln_ec);
    fs::create_symlink(root / "hello.txt", root / "link-in", ln_ec);
    // A link whose target is INSIDE the root. Containment cannot object to it,
    // so it is the only input that isolates the symlink POLICY from the
    // canonical back-stop. One per call surface, below.
    fs::create_directory_symlink(root / "sub", root / "link-sub", ln_ec);

    // A fixed mtime so the packed DOS date/time is the same on every run.
    // 2026-07-10 08:55:00 local — the same instant the boot rows pin.
    std::tm fixed{};
    fixed.tm_year = 2026 - 1900;
    fixed.tm_mon  = 7 - 1;
    fixed.tm_mday = 10;
    fixed.tm_hour = 8;
    fixed.tm_min  = 55;
    fixed.tm_sec  = 4;
    fixed.tm_isdst = -1;
    const std::time_t fixed_epoch = std::mktime(&fixed);
    {
        const auto now_ft = fs::file_time_type::clock::now();
        const auto now_sys = std::chrono::system_clock::now();
        const auto target = std::chrono::system_clock::from_time_t(fixed_epoch);
        const auto ft = std::chrono::time_point_cast<fs::file_time_type::duration>(
            target - now_sys + now_ft);
        std::error_code t_ec;
        fs::last_write_time(root / "hello.txt", ft, t_ec);
    }
    // Expected packing, computed here from the SAME instant but with the bit
    // layout spelled out independently of the implementation.
    std::tm exp_tm{};
#ifdef _WIN32
    localtime_s(&exp_tm, &fixed_epoch);
#else
    localtime_r(&fixed_epoch, &exp_tm);
#endif
    const uint16_t exp_date = static_cast<uint16_t>(
        ((exp_tm.tm_year + 1900 - 1980) << 9) | ((exp_tm.tm_mon + 1) << 5) |
        exp_tm.tm_mday);
    const uint16_t exp_time = static_cast<uint16_t>(
        (exp_tm.tm_hour << 11) | (exp_tm.tm_min << 5) | (exp_tm.tm_sec / 2));

    // ══ Tier 1 — sandbox, asserted against the class ═════════════════════
    {
        EsxdosHostFs hfs;
        std::string why;
        const bool configured = hfs.configure(root.string(), false, why);
        hcheck("HFS-01", "configure() accepts an existing directory as root",
               configured, why);

        EsxdosHostFs::StatInfo st;
        hcheck("HFS-02",
               "a '..' segment that would leave the root is refused with "
               "esx_epath (19), so the escaping path is never even formed",
               hfs.stat("../outside/secret.txt", st) == EsxdosHostFs::kEpath,
               "got " + hex2(hfs.stat("../outside/secret.txt", st)));
        hcheck("HFS-03",
               "a '..' buried mid-path escapes no better than a leading one",
               hfs.stat("sub/../../outside/secret.txt", st) ==
                   EsxdosHostFs::kEpath);
        hcheck("HFS-04", "a bare '..' is refused",
               hfs.stat("..", st) == EsxdosHostFs::kEpath);
        hcheck("HFS-05",
               "a guest ABSOLUTE path is absolute inside the root, so "
               "'/etc/passwd' is <root>/etc/passwd and simply does not exist",
               hfs.stat("/etc/passwd", st) == EsxdosHostFs::kEnoent);
        hcheck("HFS-06",
               "esx_drive_current '*' (esxapi.def:129) maps inside the root",
               hfs.stat("*:/hello.txt", st) == EsxdosHostFs::kOk);
        hcheck("HFS-07",
               "esx_drive_system '$' (esxapi.def:130) maps inside the root",
               hfs.stat("$:/hello.txt", st) == EsxdosHostFs::kOk);
        hcheck("HFS-08",
               "the 'c:/...' form NextZXOS itself writes maps inside the root",
               hfs.stat("c:/hello.txt", st) == EsxdosHostFs::kOk);
        hcheck("HFS-09",
               "a drive qualifier does not smuggle a '..' past the check",
               hfs.stat("c:/../outside/secret.txt", st) == EsxdosHostFs::kEpath);

        if (have_symlinks) {
            hcheck("HFS-10",
                   "a symlink pointing OUT of the root is refused (esx_eacces), "
                   "not followed",
                   hfs.stat("link-out", st) == EsxdosHostFs::kEacces,
                   "got " + hex2(hfs.stat("link-out", st)));
            hcheck("HFS-11",
                   "a symlinked DIRECTORY component is refused, so a link "
                   "cannot be used as a bridge to a path outside",
                   hfs.stat("link-dir/secret.txt", st) == EsxdosHostFs::kEacces);
            hcheck("HFS-12",
                   "a symlink pointing INSIDE the root is refused too: the "
                   "policy is no traversal at all, not 'only escaping links'",
                   hfs.stat("link-in", st) == EsxdosHostFs::kEacces);

            // HFS-90..92 — the same policy input, once per REMAINING call
            // surface. An inside-pointing link is the only case containment
            // cannot catch, so these are what go red if open(), opendir() or
            // chdir() stops refusing links at all. See the layering note in
            // esxdos_hostfs.cpp for why no row can isolate a SINGLE check.
            // Each pairs the refusal with a control proving the TARGET is
            // reachable by its real name, so the row cannot pass merely
            // because something is broken.
            uint8_t link_h = 0;
            const uint8_t open_link = hfs.open("link-in",
                                               EsxdosHostFs::kModeRead, link_h);
            uint8_t real_h = 0;
            const uint8_t open_real = hfs.open("hello.txt",
                                               EsxdosHostFs::kModeRead, real_h);
            hfs.close(real_h);
            hcheck("HFS-90",
                   "F_OPEN refuses an inside-pointing symlink (esx_eacces) "
                   "while opening its target by the real name succeeds",
                   open_link == EsxdosHostFs::kEacces &&
                       open_real == EsxdosHostFs::kOk,
                   "link=" + hex2(open_link) + " real=" + hex2(open_real));

            uint8_t dir_link_h = 0;
            const uint8_t od_link = hfs.opendir("link-sub",
                                                EsxdosHostFs::kDirLfnOnly,
                                                dir_link_h);
            uint8_t dir_real_h = 0;
            const uint8_t od_real = hfs.opendir("sub", EsxdosHostFs::kDirLfnOnly,
                                                dir_real_h);
            hfs.close(dir_real_h);
            hcheck("HFS-91",
                   "F_OPENDIR refuses a symlinked directory inside the root "
                   "while the real directory opens",
                   od_link == EsxdosHostFs::kEacces && od_real == EsxdosHostFs::kOk,
                   "link=" + hex2(od_link) + " real=" + hex2(od_real));

            const uint8_t cd_link = hfs.chdir("link-sub");
            const uint8_t cd_real = hfs.chdir("sub");
            hfs.chdir("/");
            hcheck("HFS-92",
                   "F_CHDIR refuses it too, and the real directory still works",
                   cd_link == EsxdosHostFs::kEacces && cd_real == EsxdosHostFs::kOk,
                   "link=" + hex2(cd_link) + " real=" + hex2(cd_real));
        } else {
            hcheck("HFS-10", "symlink fixtures unavailable on this host", false,
                   "create_symlink failed");
            hcheck("HFS-11", "symlink fixtures unavailable on this host", false, "");
            hcheck("HFS-12", "symlink fixtures unavailable on this host", false, "");
        }

        hcheck("HFS-13", "an embedded NUL is refused",
               hfs.stat(std::string("hello.txt\0/../../etc", 19), st) ==
                   EsxdosHostFs::kEinval);
        hcheck("HFS-14", "an over-long guest path is refused",
               hfs.stat(std::string(400, 'a'), st) == EsxdosHostFs::kEinval);
        hcheck("HFS-15",
               "a Windows reserved device name is refused on EVERY host, so a "
               "directory that works here cannot become a device write there",
               hfs.stat("CON", st) == EsxdosHostFs::kEacces);
        hcheck("HFS-16", "'COM1.TXT' is reserved too — the stem is what counts",
               hfs.stat("COM1.TXT", st) == EsxdosHostFs::kEacces);

        hcheck("HFS-93",
               "'\\\\' separates components as well as '/', because the guest "
               "writes FAT paths where a backslash cannot be part of a name",
               hfs.stat("sub\\\\inner.bin", st) == EsxdosHostFs::kOk &&
                   st.size == 5,
               "err=" + hex2(hfs.stat("sub\\\\inner.bin", st)));
        hcheck("HFS-94",
               "so a backslash cannot smuggle a '..' past the LEXICAL walk on "
               "any host — this refusal must not depend on whether "
               "std::filesystem::path re-splits it, which differs by platform",
               hfs.stat("a\\\\..\\\\..\\\\outside\\\\secret.txt", st) ==
                   EsxdosHostFs::kEpath,
               "err=" + hex2(hfs.stat("a\\\\..\\\\..\\\\outside\\\\secret.txt", st)));
        hcheck("HFS-95",
               "and a mixed-separator escape is refused as well",
               hfs.stat("sub/..\\\\../outside/secret.txt", st) ==
                   EsxdosHostFs::kEpath);

        hcheck("HFS-17",
               "a SUBDIRECTORY is reachable — the precedent resolve_sibling() "
               "forbade any parent path at all",
               hfs.stat("sub/inner.bin", st) == EsxdosHostFs::kOk &&
                   st.size == 5,
               "size=" + std::to_string(st.size));
        hcheck("HFS-18",
               "FAT is case-insensitive and Linux is not: 'HELLO.TXT' finds "
               "hello.txt",
               hfs.stat("HELLO.TXT", st) == EsxdosHostFs::kOk);
        hcheck("HFS-19", "and 'mIxEd.dat' finds Mixed.DAT",
               hfs.stat("mIxEd.dat", st) == EsxdosHostFs::kOk && st.size == 5);
        hcheck("HFS-20", "'.' resolves to the root itself, which is a directory",
               hfs.stat(".", st) == EsxdosHostFs::kOk &&
                   (st.attr & EsxdosHostFs::kAttrDirectory) != 0,
               "attr=" + hex2(st.attr));
        hcheck("HFS-21",
               "a directory reports esx_attr_directory ($10, esxapi.def:121) "
               "and a file esx_attr_archive ($20)",
               hfs.stat("sub", st) == EsxdosHostFs::kOk &&
                   (st.attr & EsxdosHostFs::kAttrDirectory) != 0 &&
                   hfs.stat("hello.txt", st) == EsxdosHostFs::kOk &&
                   (st.attr & EsxdosHostFs::kAttrArchive) != 0);
        hcheck("HFS-22",
               "a read-only mount marks every entry esx_attr_read_only ($01)",
               hfs.stat("hello.txt", st) == EsxdosHostFs::kOk &&
                   (st.attr & EsxdosHostFs::kAttrReadOnly) != 0);
        hcheck("HFS-23",
               "F_STAT carries the host mtime packed as MS-DOS date and time",
               hfs.stat("hello.txt", st) == EsxdosHostFs::kOk &&
                   st.date == exp_date && st.time == exp_time,
               "date=" + std::to_string(st.date) + " want " +
                   std::to_string(exp_date) + " time=" +
                   std::to_string(st.time) + " want " + std::to_string(exp_time));

        // 8.3 synthesis
        hcheck("HFS-24",
               "a name that already IS 8.3 keeps itself, uppercased",
               EsxdosHostFs::short_name("hello.txt", 0) == "HELLO.TXT",
               EsxdosHostFs::short_name("hello.txt", 0));
        hcheck("HFS-25",
               "a long name is truncated to 6 chars plus the FAT '~1' tail, "
               "spaces dropped and the extension kept",
               EsxdosHostFs::short_name("a long name.txt", 1) == "ALONGN~1.TXT",
               EsxdosHostFs::short_name("a long name.txt", 1));
        hcheck("HFS-26",
               "characters outside the 8.3 set become '_' rather than being "
               "passed through to the host name",
               EsxdosHostFs::short_name("we+ird,na;me.dat", 0) == "WE_IRD_N.DAT",
               EsxdosHostFs::short_name("we+ird,na;me.dat", 0));
        hcheck("HFS-27", "'.' and '..' are never rewritten",
               EsxdosHostFs::short_name(".", 0) == "." &&
                   EsxdosHostFs::short_name("..", 0) == "..");

        // Handle exhaustion and the private handle range.
        uint8_t h[EsxdosHostFs::kFileHandles + 1] = {0};
        bool all_distinct = true;
        bool all_in_range = true;
        uint8_t last_err = 0;
        for (int i = 0; i < EsxdosHostFs::kFileHandles; ++i) {
            last_err = hfs.open("hello.txt", EsxdosHostFs::kModeRead, h[i]);
            if (last_err) all_in_range = false;
            if (h[i] < EsxdosHostFs::kFirstFileHandle ||
                h[i] >= EsxdosHostFs::kFirstFileHandle +
                            EsxdosHostFs::kFileHandles)
                all_in_range = false;
            for (int j = 0; j < i; ++j)
                if (h[j] == h[i]) all_distinct = false;
        }
        hcheck("HFS-28",
               "every file handle is distinct and inside the private range "
               "$04..$0B, disjoint from the in-memory stub's handle 1 and "
               "ExtendedNexHost's 2 and 3",
               all_distinct && all_in_range,
               "last_err=" + hex2(last_err));
        uint8_t overflow_h = 0;
        hcheck("HFS-29", "the next open past the table returns esx_enfile (12)",
               hfs.open("hello.txt", EsxdosHostFs::kModeRead, overflow_h) ==
                   EsxdosHostFs::kEnfile);
        hcheck("HFS-30", "closing one frees a slot again",
               hfs.close(h[0]) == EsxdosHostFs::kOk &&
                   hfs.open("hello.txt", EsxdosHostFs::kModeRead, overflow_h) ==
                       EsxdosHostFs::kOk);
        hcheck("HFS-31", "closing a handle twice is esx_ebadf (13)",
               hfs.close(overflow_h) == EsxdosHostFs::kOk &&
                   hfs.close(overflow_h) == EsxdosHostFs::kEbadf);

        // Release every handle the exhaustion rows above are still holding.
        // Without this the read-only rows below run against a nearly full
        // table and would refuse with esx_enfile whatever the mount policy
        // says — which made HFS-34 pass for the wrong reason until the
        // "writable by default" mutation showed it could not go red.
        for (int i = 0; i < EsxdosHostFs::kFileHandles; ++i)
            hfs.close(static_cast<uint8_t>(EsxdosHostFs::kFirstFileHandle + i));

        // Read-only mount refusals.
        uint8_t wh = 0;
        hcheck("HFS-32",
               "without --esxdos-stub-writable, esx_mode_write is refused with "
               "esx_erdonly (24) — read-only is the DEFAULT, not a mode",
               hfs.open("hello.txt", EsxdosHostFs::kModeRead |
                                         EsxdosHostFs::kModeWrite, wh) ==
                   EsxdosHostFs::kErdonly);
        hcheck("HFS-33",
               "and so is a create mode, even asking only for read access",
               hfs.open("brandnew.txt", EsxdosHostFs::kModeRead |
                                            EsxdosHostFs::kModeOpenCreat, wh) ==
                   EsxdosHostFs::kErdonly);
        hcheck("HFS-34", "nothing was created on the host by that refusal",
               !fs::exists(root / "brandnew.txt"));
        hcheck("HFS-35",
               "esx_mode_use_header ($40) is refused with esx_enosys (20) "
               "rather than fabricating a +3DOS header a host file has not got",
               hfs.open("hello.txt", EsxdosHostFs::kModeRead |
                                         EsxdosHostFs::kModeUseHeader, wh) ==
                   EsxdosHostFs::kEnosys);
        hcheck("HFS-36", "opening a directory as a file is esx_eisdir (16)",
               hfs.open("sub", EsxdosHostFs::kModeRead, wh) ==
                   EsxdosHostFs::kEisdir);
        hcheck("HFS-37", "opening a missing file is esx_enoent (5)",
               hfs.open("nope.txt", EsxdosHostFs::kModeRead, wh) ==
                   EsxdosHostFs::kEnoent);
    }

    // ── Writable mount ────────────────────────────────────────────────────
    {
        EsxdosHostFs hfs;
        std::string why;
        hfs.configure(root.string(), true, why);
        uint8_t wh = 0;
        std::size_t written = 0;
        const std::string body = "WRITTEN";
        const uint8_t err = hfs.open("made.bin", EsxdosHostFs::kModeWrite |
                                                     EsxdosHostFs::kModeCreatTrunc,
                                     wh);
        const uint8_t werr = err ? err : hfs.write(
            wh, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
            written);
        hfs.close(wh);
        hcheck("HFS-38",
               "with --esxdos-stub-writable the guest's F_WRITE reaches the "
               "real host file",
               err == 0 && werr == 0 && written == body.size() &&
                   slurp(root / "made.bin") == body,
               "err=" + hex2(err) + " on-disk='" + slurp(root / "made.bin") + "'");
        hcheck("HFS-39",
               "esx_mode_creat_noexist ($04) on an existing file is "
               "esx_eexist (18)",
               hfs.open("made.bin", EsxdosHostFs::kModeWrite |
                                        EsxdosHostFs::kModeCreatNoExist, wh) ==
                   EsxdosHostFs::kEexist);
        uint8_t th = 0;
        hcheck("HFS-40",
               "esx_mode_creat_trunc ($0c) empties an existing file",
               hfs.open("made.bin", EsxdosHostFs::kModeWrite |
                                        EsxdosHostFs::kModeCreatTrunc, th) ==
                       EsxdosHostFs::kOk &&
                   slurp(root / "made.bin").empty());
        hfs.close(th);
        hcheck("HFS-41",
               "a write is still confined: a create outside the root is "
               "refused before anything is made",
               hfs.open("../escaped.bin", EsxdosHostFs::kModeWrite |
                                              EsxdosHostFs::kModeCreatTrunc,
                        th) == EsxdosHostFs::kEpath &&
                   !fs::exists(base / "escaped.bin"));
        std::error_code e;
        fs::remove(root / "made.bin", e);
    }

    // ══ Tier 2 — register conventions, through the real dispatcher ═══════
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.esxdos_stub = true;
    cfg.esxdos_stub_root = root.string();
    cfg.rewind_buffer_frames = 0;
    {
        std::tm pin{};
        pin.tm_year = 2026 - 1900;
        pin.tm_mon  = 7 - 1;
        pin.tm_mday = 10;
        pin.tm_hour = 8;
        pin.tm_min  = 55;
        pin.tm_sec  = 0;
        cfg.rtc_fixed_tm = pin;
        cfg.rtc_fixed = true;
    }
    Emulator emu;
    emu.init(cfg);

    hcheck("HFS-42",
           "--esxdos-stub-root attaches the RST $08 hook by itself",
           static_cast<bool>(emu.cpu().on_esxdos_call));

    uint8_t err = 0;
    const uint8_t fh = do_open(emu, "hello.txt", EsxdosHostFs::kModeRead, err);
    hcheck("HFS-43",
           "F_OPEN ($9a): A=drive, IX=filespec, B=mode -> Fc=0, A=handle",
           err == 0 && fh >= EsxdosHostFs::kFirstFileHandle,
           "err=" + hex2(err) + " h=" + hex2(fh));

    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.IX = kBuf;
        r.BC = 5;
        const bool ok = esx(emu, 0x9D, r) && !cy(r);
        std::string got;
        for (int i = 0; i < 5; ++i)
            got.push_back(static_cast<char>(emu.mmu().read(kBuf + i)));
        hcheck("HFS-44",
               "F_READ ($9d): A=handle, IX=dest, BC=count -> the host file's "
               "bytes land at IX, with BC=DE=actual and HL=IX+actual",
               ok && got == "HELLO" && r.BC == 5 && r.DE == 5 &&
                   r.HL == static_cast<uint16_t>(kBuf + 5),
               "got='" + got + "' BC=" + std::to_string(r.BC) +
                   " HL=" + std::to_string(r.HL));
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.BC = 0;
        r.DE = 2;
        r.IX = EsxdosHostFs::kSeekSet;
        const bool ok = esx(emu, 0x9F, r) && !cy(r);
        const uint32_t pos = (static_cast<uint32_t>(r.BC) << 16) | r.DE;
        hcheck("HFS-45",
               "F_SEEK ($9f) WORKS: A=handle, BCDE=distance, IXL=esx_seek_set "
               "-> Fc=0 and BCDE=the new position. It was hardcoded to fail "
               "with A=5 before this change",
               ok && pos == 2, "pos=" + std::to_string(pos));
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.IX = kBuf;
        r.BC = 4;
        const bool ok = esx(emu, 0x9D, r) && !cy(r);
        std::string got;
        for (int i = 0; i < 4; ++i)
            got.push_back(static_cast<char>(emu.mmu().read(kBuf + i)));
        hcheck("HFS-46", "and the next F_READ continues from the sought offset",
               ok && got == "LLO-", "got='" + got + "'");
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.BC = 0;
        r.DE = 3;
        r.IX = EsxdosHostFs::kSeekBwd;
        const bool ok = esx(emu, 0x9F, r) && !cy(r);
        const uint32_t pos = (static_cast<uint32_t>(r.BC) << 16) | r.DE;
        hcheck("HFS-47", "esx_seek_bwd (2) subtracts from the position",
               ok && pos == 3, "pos=" + std::to_string(pos));
        Z80Registers f{};
        f.AF = static_cast<uint16_t>(fh << 8);
        f.BC = 0;
        f.DE = 1;
        f.IX = EsxdosHostFs::kSeekFwd;
        const bool ok2 = esx(emu, 0x9F, f) && !cy(f);
        const uint32_t pos2 = (static_cast<uint32_t>(f.BC) << 16) | f.DE;
        hcheck("HFS-48", "esx_seek_fwd (1) adds to it",
               ok2 && pos2 == 4, "pos=" + std::to_string(pos2));
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.BC = 0;
        r.DE = 9999;
        r.IX = EsxdosHostFs::kSeekSet;
        const bool handled = esx(emu, 0x9F, r);
        hcheck("HFS-49", "seeking past the end is esx_einval (7), not silence",
               handled && cy(r) && rega(r) == EsxdosHostFs::kEinval,
               "A=" + hex2(rega(r)));
        Z80Registers b{};
        b.AF = static_cast<uint16_t>(fh << 8);
        b.BC = 0;
        b.DE = 9999;
        b.IX = EsxdosHostFs::kSeekBwd;
        hcheck("HFS-50", "and so is seeking backwards past zero",
               esx(emu, 0x9F, b) && cy(b) &&
                   rega(b) == EsxdosHostFs::kEinval);
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        const bool ok = esx(emu, 0xA0, r) && !cy(r);
        const uint32_t pos = (static_cast<uint32_t>(r.BC) << 16) | r.DE;
        hcheck("HFS-51",
               "F_FGETPOS ($a0): A=handle -> BCDE=position, unchanged by the "
               "two refused seeks above",
               ok && pos == 4, "pos=" + std::to_string(pos));
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        r.IX = kStat;
        const bool ok = esx(emu, 0xA1, r) && !cy(r);
        const uint32_t size =
            emu.mmu().read(kStat + 7) |
            (emu.mmu().read(kStat + 8) << 8) |
            (emu.mmu().read(kStat + 9) << 16) |
            (static_cast<uint32_t>(emu.mmu().read(kStat + 10)) << 24);
        const uint16_t t = static_cast<uint16_t>(
            emu.mmu().read(kStat + 3) | (emu.mmu().read(kStat + 4) << 8));
        const uint16_t d = static_cast<uint16_t>(
            emu.mmu().read(kStat + 5) | (emu.mmu().read(kStat + 6) << 8));
        hcheck("HFS-52",
               "F_FSTAT ($a1) fills the 11-byte esx_stat exactly as "
               "asm_esx_f_fstat.asm documents it: '*' / $81 / attr / time(2) / "
               "date(2) / size(4), all little-endian",
               ok && emu.mmu().read(kStat + 0) == '*' &&
                   emu.mmu().read(kStat + 1) == 0x81 && size == 12 &&
                   t == exp_time && d == exp_date,
               "b0=" + hex2(emu.mmu().read(kStat + 0)) +
                   " b1=" + hex2(emu.mmu().read(kStat + 1)) +
                   " size=" + std::to_string(size));
    }
    {
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(fh << 8);
        hcheck("HFS-53", "F_CLOSE ($9b): A=handle -> Fc=0, A=0",
               esx(emu, 0x9B, r) && !cy(r) && rega(r) == 0);
        Z80Registers r2{};
        r2.AF = static_cast<uint16_t>(fh << 8);
        r2.IX = kBuf;
        r2.BC = 1;
        hcheck("HFS-54", "a read on the closed handle is esx_ebadf (13)",
               esx(emu, 0x9D, r2) && cy(r2) &&
                   rega(r2) == EsxdosHostFs::kEbadf,
               "A=" + hex2(rega(r2)));
    }
    {
        // A short read at EOF is explicitly NOT an error (F_READ notes in
        // asm_esx_f_read.asm): the caller checks BC.
        uint8_t e2 = 0;
        const uint8_t h2 = do_open(emu, "sub/inner.bin",
                                   EsxdosHostFs::kModeRead, e2);
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(h2 << 8);
        r.IX = kBuf;
        r.BC = 64;
        const bool ok = esx(emu, 0x9D, r) && !cy(r);
        hcheck("HFS-55",
               "a short read at EOF returns Fc=0 with BC=bytes actually read, "
               "not an error — and a subdirectory path opens at all",
               e2 == 0 && ok && r.BC == 5, "e=" + hex2(e2) +
                   " BC=" + std::to_string(r.BC));
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(h2 << 8);
        esx(emu, 0x9B, c);
    }
    {
        uint8_t e3 = 0;
        do_open(emu, "../outside/secret.txt", EsxdosHostFs::kModeRead, e3);
        hcheck("HFS-56",
               "the escape refusal is visible to the GUEST as Fc=1 with "
               "esx_epath in A, through the real dispatcher",
               e3 == EsxdosHostFs::kEpath, "A=" + hex2(e3));
    }
    {
        // The legacy in-memory handle (1) and the extended-NEX handles (2, 3)
        // must NOT be answered by the host FS, or two back ends would fight
        // over the same call.
        Z80Registers r{};
        r.AF = 0x0100;   // handle 1
        r.IX = kBuf;
        r.BC = 1;
        const bool handled = esx(emu, 0x9D, r);
        hcheck("HFS-57",
               "F_READ on handle 1 is NOT claimed by the host FS — it reaches "
               "the in-memory stub below, which refuses it with A=5",
               handled && cy(r) && rega(r) == 0x05, "A=" + hex2(rega(r)));
    }
    {
        Z80Registers r{};
        r.IX = kName;
        poke_str(emu, kName, "hello.txt");
        r.DE = kStat;
        const bool ok = esx(emu, 0xAC, r) && !cy(r);
        hcheck("HFS-58",
               "F_STAT ($ac): A=drive, IX=filespec, DE=11-byte buffer — note "
               "the buffer is DE here and IX for F_FSTAT",
               ok && emu.mmu().read(kStat + 0) == '*' &&
                   emu.mmu().read(kStat + 7) == 12);
    }
    {
        Z80Registers r{};
        r.BC = 0;
        r.DE = 0;
        const bool ok = esx(emu, 0x8E, r) && !cy(r);
        const uint16_t want_date = static_cast<uint16_t>(
            ((2026 - 1980) << 9) | (7 << 5) | 10);
        const uint16_t want_time = static_cast<uint16_t>((8 << 11) | (55 << 5));
        hcheck("HFS-59",
               "M_GETDATE ($8e) answers from the EMULATED RTC, so it honours "
               "--rtc: BC=date, DE=time in MS-DOS format",
               ok && r.BC == want_date && r.DE == want_time,
               "BC=" + std::to_string(r.BC) + " want " +
                   std::to_string(want_date) + " DE=" + std::to_string(r.DE) +
                   " want " + std::to_string(want_time));
    }
    {
        Z80Registers r{};
        const bool ok = esx(emu, 0xB1, r) && !cy(r);
        const uint32_t blocks = (static_cast<uint32_t>(r.BC) << 16) | r.DE;
        hcheck("HFS-60",
               "F_GETFREE ($b1) reports 512-byte blocks in BCDE, clamped to "
               "4 GiB worth so a terabyte host does not overflow a guest that "
               "scales it back into bytes",
               ok && blocks > 0 && blocks <= 8388608u,
               "blocks=" + std::to_string(blocks));
    }

    // ── Directory calls ───────────────────────────────────────────────────
    {
        poke_str(emu, kName, "/");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(EsxdosHostFs::kDirLfnOnly << 8);
        const bool ok = esx(emu, 0xA3, r) && !cy(r);
        const uint8_t dh = rega(r);
        hcheck("HFS-61",
               "F_OPENDIR ($a3): A=drive, IX=path, B=mode -> A=dirhandle, in "
               "the $84.. range so it cannot collide with a file handle",
               ok && EsxdosHostFs::is_dir_handle(dh), "A=" + hex2(dh));

        std::vector<std::string> names;
        uint8_t last_a = 0xFF;
        for (int i = 0; i < 40; ++i) {
            Z80Registers e{};
            e.AF = static_cast<uint16_t>(dh << 8);
            e.IX = kBuf;
            if (!esx(emu, 0xA4, e) || cy(e)) { last_a = 0xFE; break; }
            last_a = rega(e);
            if (last_a == 0) break;
            uint16_t end = 0;
            names.push_back(peek_str(emu, kBuf + 1, end));
        }
        const bool has_hello =
            std::find(names.begin(), names.end(), "hello.txt") != names.end();
        const bool has_long =
            std::find(names.begin(), names.end(), "a long name.txt") != names.end();
        hcheck("HFS-62",
               "F_READDIR ($a4) with esx_mode_lfn_only walks the whole "
               "directory and returns the LONG names",
               has_hello && has_long,
               "n=" + std::to_string(names.size()));
        hcheck("HFS-63",
               "the walk ends with A=0 and Fc=0 — 'no more entries' is not an "
               "error (asm_esx_f_readdir.asm)",
               last_a == 0, "last A=" + hex2(last_a));
        const bool lists_symlink =
            std::find(names.begin(), names.end(), "link-out") != names.end() ||
            std::find(names.begin(), names.end(), "link-in") != names.end();
        hcheck("HFS-64",
               "a symlink is not LISTED either: advertising a name every other "
               "call refuses would be worse than omitting it",
               !have_symlinks || !lists_symlink);
        hcheck("HFS-65",
               "the FAT32 ROOT carries no '.' or '..' entries, matching real "
               "FAT (which is why esx_sf_exclude_dots exists)",
               std::find(names.begin(), names.end(), ".") == names.end() &&
                   std::find(names.begin(), names.end(), "..") == names.end());
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(dh << 8);
        hcheck("HFS-66", "F_CLOSE closes a DIRECTORY handle too",
               esx(emu, 0x9B, c) && !cy(c));
    }
    {
        // Short-name mode: the entry carries the synthesised 8.3 name, and the
        // full entry layout is checked byte by byte.
        poke_str(emu, kName, "/");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(EsxdosHostFs::kDirShortOnly << 8);
        esx(emu, 0xA3, r);
        const uint8_t dh = rega(r);
        std::vector<std::string> shorts;
        uint8_t hello_attr = 0;
        uint16_t hello_time = 0, hello_date = 0;
        uint32_t hello_size = 0;
        for (int i = 0; i < 40; ++i) {
            Z80Registers e{};
            e.AF = static_cast<uint16_t>(dh << 8);
            e.IX = kBuf;
            if (!esx(emu, 0xA4, e) || cy(e) || rega(e) == 0) break;
            uint16_t end = 0;
            const std::string n = peek_str(emu, kBuf + 1, end);
            shorts.push_back(n);
            if (n == "HELLO.TXT") {
                hello_attr = emu.mmu().read(kBuf);
                hello_time = static_cast<uint16_t>(
                    emu.mmu().read(end) | (emu.mmu().read(end + 1) << 8));
                hello_date = static_cast<uint16_t>(
                    emu.mmu().read(end + 2) | (emu.mmu().read(end + 3) << 8));
                hello_size = emu.mmu().read(end + 4) |
                             (emu.mmu().read(end + 5) << 8) |
                             (emu.mmu().read(end + 6) << 16) |
                             (static_cast<uint32_t>(emu.mmu().read(end + 7)) << 24);
            }
        }
        hcheck("HFS-67",
               "esx_mode_short_only returns the synthesised 8.3 name",
               std::find(shorts.begin(), shorts.end(), "HELLO.TXT") !=
                   shorts.end());
        hcheck("HFS-68",
               "the F_READDIR entry is attr, asciiz name, time word, date "
               "word, size dword — the layout readdir.asm's showanentry parses",
               hello_size == 12 && hello_time == exp_time &&
                   hello_date == exp_date &&
                   (hello_attr & EsxdosHostFs::kAttrArchive) != 0,
               "size=" + std::to_string(hello_size) + " time=" +
                   std::to_string(hello_time) + " date=" +
                   std::to_string(hello_date) + " attr=" + hex2(hello_attr));
        int tilde = 0;
        for (const std::string& n : shorts)
            if (n.find('~') != std::string::npos) ++tilde;
        std::vector<std::string> sorted = shorts;
        std::sort(sorted.begin(), sorted.end());
        const bool unique =
            std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
        hcheck("HFS-69",
               "the two long names each get a distinct '~N' short name, and no "
               "two entries in the listing share one",
               tilde >= 2 && unique, "tilde=" + std::to_string(tilde));
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(dh << 8);
        esx(emu, 0x9B, c);
    }
    {
        // lfn_and_short: two asciiz names, LFN first.
        poke_str(emu, kName, "/");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(EsxdosHostFs::kDirLfnAndShort << 8);
        esx(emu, 0xA3, r);
        const uint8_t dh = rega(r);
        bool found_pair = false;
        for (int i = 0; i < 40; ++i) {
            Z80Registers e{};
            e.AF = static_cast<uint16_t>(dh << 8);
            e.IX = kBuf;
            if (!esx(emu, 0xA4, e) || cy(e) || rega(e) == 0) break;
            uint16_t end = 0;
            const std::string lfn = peek_str(emu, kBuf + 1, end);
            uint16_t end2 = 0;
            const std::string sfn = peek_str(emu, end, end2);
            if (lfn == "a long name.txt" && sfn.find('~') != std::string::npos)
                found_pair = true;
        }
        hcheck("HFS-70",
               "esx_mode_lfn_and_short emits BOTH names, long first then "
               "short, which is the order readdir.asm reads them in",
               found_pair);
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(dh << 8);
        esx(emu, 0x9B, c);
    }
    {
        poke_str(emu, kName, "/sub");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(EsxdosHostFs::kDirLfnOnly << 8);
        const bool ok = esx(emu, 0xA3, r) && !cy(r);
        const uint8_t dh = rega(r);
        std::vector<std::string> names;
        for (int i = 0; i < 40; ++i) {
            Z80Registers e{};
            e.AF = static_cast<uint16_t>(dh << 8);
            e.IX = kBuf;
            if (!esx(emu, 0xA4, e) || cy(e) || rega(e) == 0) break;
            uint16_t end = 0;
            names.push_back(peek_str(emu, kBuf + 1, end));
        }
        hcheck("HFS-71",
               "a SUBDIRECTORY listing does carry '.' and '..', again matching "
               "FAT, and they come first",
               ok && names.size() >= 3 && names[0] == "." && names[1] == ".." &&
                   std::find(names.begin(), names.end(), "inner.bin") !=
                       names.end(),
               "n=" + std::to_string(names.size()));

        Z80Registers t{};
        t.AF = static_cast<uint16_t>(dh << 8);
        const bool tok = esx(emu, 0xA5, t) && !cy(t);
        const uint32_t at_end = (static_cast<uint32_t>(t.BC) << 16) | t.DE;
        Z80Registers w{};
        w.AF = static_cast<uint16_t>(dh << 8);
        const bool wok = esx(emu, 0xA7, w) && !cy(w);
        Z80Registers e0{};
        e0.AF = static_cast<uint16_t>(dh << 8);
        e0.IX = kBuf;
        const bool eok = esx(emu, 0xA4, e0) && !cy(e0) && rega(e0) == 1;
        uint16_t end = 0;
        const std::string first_again = peek_str(emu, kBuf + 1, end);
        hcheck("HFS-72",
               "F_TELLDIR ($a5) reports the walk position and F_REWINDDIR "
               "($a7) puts it back at the first entry",
               tok && at_end == names.size() && wok && eok &&
                   first_again == ".",
               "tell=" + std::to_string(at_end) + " first='" + first_again + "'");
        Z80Registers s{};
        s.AF = static_cast<uint16_t>(dh << 8);
        s.BC = 0;
        s.DE = 2;
        const bool sok = esx(emu, 0xA6, s) && !cy(s);
        Z80Registers e1{};
        e1.AF = static_cast<uint16_t>(dh << 8);
        e1.IX = kBuf;
        esx(emu, 0xA4, e1);
        uint16_t end1 = 0;
        const std::string third = peek_str(emu, kBuf + 1, end1);
        hcheck("HFS-73",
               "F_SEEKDIR ($a6): BCDE=pointer moves the walk to that entry",
               sok && third == names[2],
               "third='" + third + "' want '" + (names.size() > 2 ? names[2] : "?") + "'");
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(dh << 8);
        esx(emu, 0x9B, c);
    }
    {
        poke_str(emu, kName, "/");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(
            (EsxdosHostFs::kDirLfnOnly | EsxdosHostFs::kDirUseWildcards) << 8);
        const bool handled = esx(emu, 0xA3, r);
        hcheck("HFS-74",
               "esx_mode_use_wildcards ($20) is REFUSED with esx_enosys (20) "
               "rather than silently returning an unfiltered listing the "
               "caller would take for a filtered one",
               handled && cy(r) && rega(r) == EsxdosHostFs::kEnosys,
               "A=" + hex2(rega(r)));
        Z80Registers s{};
        s.IX = kName;
        s.BC = static_cast<uint16_t>(
            (EsxdosHostFs::kDirLfnOnly | EsxdosHostFs::kDirSfEnable) << 8);
        hcheck("HFS-75",
               "and so is esx_mode_sf_enable ($80) — an unsorted listing "
               "returned to a caller that asked for a sorted one is a lie",
               esx(emu, 0xA3, s) && cy(s) &&
                   rega(s) == EsxdosHostFs::kEnosys);
    }
    {
        poke_str(emu, kName, "/nope");
        Z80Registers r{};
        r.IX = kName;
        r.BC = 0;
        hcheck("HFS-76", "F_OPENDIR of a missing directory is esx_enoent (5)",
               esx(emu, 0xA3, r) && cy(r) && rega(r) == EsxdosHostFs::kEnoent);
        poke_str(emu, kName, "/hello.txt");
        Z80Registers f{};
        f.IX = kName;
        f.BC = 0;
        hcheck("HFS-77", "F_OPENDIR of a FILE is esx_enotdir (17)",
               esx(emu, 0xA3, f) && cy(f) && rega(f) == EsxdosHostFs::kEnotdir,
               "A=" + hex2(rega(f)));
    }

    // ── CWD ───────────────────────────────────────────────────────────────
    {
        Z80Registers g{};
        g.IX = kBuf;
        const bool ok = esx(emu, 0xA8, g) && !cy(g);
        uint16_t end = 0;
        const std::string cwd = peek_str(emu, kBuf, end);
        hcheck("HFS-78",
               "F_GETCWD ($a8): A=drive, IX=buffer -> the guest CWD, which "
               "starts at the root",
               ok && cwd == "/", "cwd='" + cwd + "'");

        poke_str(emu, kName, "sub");
        Z80Registers c{};
        c.IX = kName;
        const bool cok = esx(emu, 0xA9, c) && !cy(c);
        Z80Registers g2{};
        g2.IX = kBuf;
        esx(emu, 0xA8, g2);
        uint16_t end2 = 0;
        const std::string cwd2 = peek_str(emu, kBuf, end2);
        hcheck("HFS-79",
               "F_CHDIR ($a9) moves it, and F_GETCWD reports where it moved to",
               cok && cwd2 == "/sub", "cwd='" + cwd2 + "'");

        uint8_t e4 = 0;
        const uint8_t rh = do_open(emu, "inner.bin", EsxdosHostFs::kModeRead, e4);
        Z80Registers rd{};
        rd.AF = static_cast<uint16_t>(rh << 8);
        rd.IX = kBuf;
        rd.BC = 5;
        esx(emu, 0x9D, rd);
        std::string body;
        for (int i = 0; i < 5; ++i)
            body.push_back(static_cast<char>(emu.mmu().read(kBuf + i)));
        hcheck("HFS-80",
               "a RELATIVE F_OPEN now resolves against that CWD, not the root",
               e4 == 0 && body == "INNER", "e=" + hex2(e4) + " '" + body + "'");
        Z80Registers cl{};
        cl.AF = static_cast<uint16_t>(rh << 8);
        esx(emu, 0x9B, cl);

        poke_str(emu, kName, "../..");
        Z80Registers up{};
        up.IX = kName;
        const bool handled = esx(emu, 0xA9, up);
        Z80Registers g3{};
        g3.IX = kBuf;
        esx(emu, 0xA8, g3);
        uint16_t end3 = 0;
        const std::string cwd3 = peek_str(emu, kBuf, end3);
        hcheck("HFS-81",
               "a chdir that would climb out of the root is refused with "
               "esx_epath, and the CWD is left where it was",
               handled && cy(up) && rega(up) == EsxdosHostFs::kEpath &&
                   cwd3 == "/sub",
               "A=" + hex2(rega(up)) + " cwd='" + cwd3 + "'");

        poke_str(emu, kName, "/");
        Z80Registers home{};
        home.IX = kName;
        esx(emu, 0xA9, home);
    }

    // ── The drive-qualified filespec, through the MARSHALLING layer ──────
    //
    // WHY THESE ROWS EXIST, AND WHY THE SUITE NEEDED THEM.
    //
    // HFS-06..09 already assert that resolve() maps '*:', '$:' and 'c:' inside
    // the root — but they call the CLASS, so they never touch the code that
    // reads the filespec out of guest memory. Every Tier-2 row went through the
    // real dispatcher, which is the point of Tier 2, but every one of them used
    // a filename with no colon in it. So the one input class that proves the
    // two tiers agree was the one class neither tested, and a marshalling
    // helper that truncated the filespec at ':' (it terminated on NUL, CR and
    // ':', the dot-command COMMAND-TAIL convention, wrongly applied to a
    // filespec) passed 147 green rows while handing resolve() the single byte
    // "c". Found in review, GH #31. These rows close that seam for all four
    // path-keyed calls, which share the one reader.
    {
        uint8_t e = 0;
        const uint8_t h = do_open(emu, "c:/hello.txt", EsxdosHostFs::kModeRead, e);
        Z80Registers r{};
        r.AF = static_cast<uint16_t>(h << 8);
        r.IX = kBuf;
        r.BC = 5;
        const bool read_ok = e == 0 && esx(emu, 0x9D, r) && !cy(r);
        std::string got;
        for (int i = 0; i < 5; ++i)
            got.push_back(static_cast<char>(emu.mmu().read(kBuf + i)));
        hcheck("HFS-84",
               "F_OPEN of a DRIVE-QUALIFIED filespec 'c:/hello.txt' reaches the "
               "file: the marshalling layer must not stop the filespec at ':', "
               "which is the form asm_esx_f_open.asm documents ('overridden if "
               "filespec includes a drive') and NextZXOS's own ROM literals use",
               e == 0 && read_ok && got == "HELLO",
               "err=" + hex2(e) + " got='" + got + "'");
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(h << 8);
        esx(emu, 0x9B, c);

        uint8_t e2 = 0, e3 = 0;
        const uint8_t h2 = do_open(emu, "*:/hello.txt", EsxdosHostFs::kModeRead, e2);
        Z80Registers c2{};
        c2.AF = static_cast<uint16_t>(h2 << 8);
        esx(emu, 0x9B, c2);
        const uint8_t h3 = do_open(emu, "$:/hello.txt", EsxdosHostFs::kModeRead, e3);
        Z80Registers c3{};
        c3.AF = static_cast<uint16_t>(h3 << 8);
        esx(emu, 0x9B, c3);
        hcheck("HFS-85",
               "the esxDOS drive letters esx_drive_current '*' and "
               "esx_drive_system '$' (esxapi.def:129-130) survive marshalling "
               "too, not just the 'c:' form",
               e2 == 0 && e3 == 0,
               "star=" + hex2(e2) + " dollar=" + hex2(e3));
    }
    {
        poke_str(emu, kName, "c:/hello.txt");
        Z80Registers r{};
        r.IX = kName;
        r.DE = kStat;
        const bool ok = esx(emu, 0xAC, r) && !cy(r);
        hcheck("HFS-86",
               "F_STAT takes a drive-qualified filespec through the dispatcher",
               ok && emu.mmu().read(kStat + 7) == 12,
               "A=" + hex2(rega(r)));
    }
    {
        poke_str(emu, kName, "c:/sub");
        Z80Registers r{};
        r.IX = kName;
        r.BC = static_cast<uint16_t>(EsxdosHostFs::kDirLfnOnly << 8);
        const bool ok = esx(emu, 0xA3, r) && !cy(r);
        const uint8_t dh = rega(r);
        hcheck("HFS-87", "F_OPENDIR does as well",
               ok && EsxdosHostFs::is_dir_handle(dh), "A=" + hex2(dh));
        Z80Registers c{};
        c.AF = static_cast<uint16_t>(dh << 8);
        esx(emu, 0x9B, c);
    }
    {
        poke_str(emu, kName, "c:/sub");
        Z80Registers r{};
        r.IX = kName;
        const bool ok = esx(emu, 0xA9, r) && !cy(r);
        Z80Registers g{};
        g.IX = kBuf;
        esx(emu, 0xA8, g);
        uint16_t end = 0;
        const std::string cwd = peek_str(emu, kBuf, end);
        hcheck("HFS-88",
               "and so does F_CHDIR — all four path-keyed calls share the one "
               "filespec reader, so all four are pinned",
               ok && cwd == "/sub", "A=" + hex2(rega(r)) + " cwd='" + cwd + "'");
        poke_str(emu, kName, "/");
        Z80Registers home{};
        home.IX = kName;
        esx(emu, 0xA9, home);
    }
    {
        // The deliberately DIFFERENT caller. M_EXECCMD is handed a dot-command
        // COMMAND TAIL, which readdir.asm:78-79 documents as terminated by
        // $00, $0d or ':' — so it keeps the colon-splitting a filespec must not
        // have. Pinned so the split above cannot be "tidied" into one reader.
        poke_str(emu, kName, "RUN next.nex:REM trailing basic");
        Z80Registers r{};
        r.IX = kName;
        const bool handled = esx(emu, 0x8F, r);
        hcheck("HFS-89",
               "M_EXECCMD still ends the COMMAND TAIL at ':' — that terminator "
               "set belongs to the command line, and is exactly what must not "
               "be applied to a filespec",
               handled && !cy(r),
               "handled=" + std::to_string(handled) + " A=" + hex2(rega(r)));
    }

    // ── Rewind: handles travel as (path, offset, mode) ────────────────────
    {
        uint8_t e5 = 0;
        const uint8_t h = do_open(emu, "hello.txt", EsxdosHostFs::kModeRead, e5);
        Z80Registers sk{};
        sk.AF = static_cast<uint16_t>(h << 8);
        sk.BC = 0;
        sk.DE = 6;
        sk.IX = EsxdosHostFs::kSeekSet;
        esx(emu, 0x9F, sk);

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buffer(measure.position());
        StateWriter w(buffer.data(), buffer.size());
        emu.save_state(w);

        // Move the handle on, then restore and prove the offset came back.
        Z80Registers adv{};
        adv.AF = static_cast<uint16_t>(h << 8);
        adv.IX = kBuf;
        adv.BC = 3;
        esx(emu, 0x9D, adv);

        StateReader rd(buffer.data(), buffer.size());
        const bool loaded = emu.load_state(rd);

        Z80Registers pos{};
        pos.AF = static_cast<uint16_t>(h << 8);
        const bool pok = esx(emu, 0xA0, pos) && !cy(pos);
        const uint32_t at = (static_cast<uint32_t>(pos.BC) << 16) | pos.DE;
        hcheck("HFS-82",
               "an open host handle survives a rewind: the snapshot carries "
               "(path, offset, mode) and load_state reopens at that offset, so "
               "the file position is 6 again and not 9",
               loaded && pok && at == 6,
               "loaded=" + std::to_string(loaded) + " pos=" + std::to_string(at));

        Z80Registers rr{};
        rr.AF = static_cast<uint16_t>(h << 8);
        rr.IX = kBuf;
        rr.BC = 6;
        const bool rok = esx(emu, 0x9D, rr) && !cy(rr);
        std::string tail;
        for (int i = 0; i < 6; ++i)
            tail.push_back(static_cast<char>(emu.mmu().read(kBuf + i)));
        hcheck("HFS-83",
               "and the reopened stream really reads from there",
               rok && tail == "HOSTFS", "'" + tail + "'");
    }

    std::error_code cleanup;
    fs::remove_all(base, cleanup);
}
