// GH #457 - rows for the esxDOS calls that change files on the host folder:
// F_UNLINK, F_RENAME, F_MKDIR, F_RMDIR, F_TRUNCATE, F_FTRUNCATE, F_CHMOD, M_GETERR.
//
// ORACLE. Real NextZXOS 2.09, MEASURED by the kept probe
// (test/00regression/esxdos-write-probe.py) and recorded in
// test/fixtures/esxdos-write-probe/nextzxos.txt. Every expected value below is a
// line of that recording or a declared divergence from it (the generator's
// DIVERGE table); the comment of each row names its recorded group. No VHDL: this
// is NextZXOS software. Two tiers, as esxdos_hostfs_rows.cpp: the class directly
// (what was answered and what the host looks like afterwards) and the real
// dispatcher (the registers, which a class-level row cannot see).

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_hostfs.h"
#include "core/saveable.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid() - per-process fixture paths
#ifdef _WIN32
#include <windows.h>
#endif

#include "../row_id.h"

namespace fs = std::filesystem;
using H = EsxdosHostFs;

namespace {

int* g_passed = nullptr;
int* g_failed = nullptr;

void wcheck(const char* id, const char* desc, bool condition,
            const std::string& detail = "") {
    report_row_id(id);
    if (condition) {
        ++*g_passed;
    } else {
        ++*g_failed;
        std::printf("FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " - ", detail.c_str());
    }
}

std::string hx(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02X", v & 0xFF);
    return b;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

void put(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

void make_read_only(const fs::path& p) {
    fs::permissions(p, fs::perms::owner_write | fs::perms::group_write |
                           fs::perms::others_write, fs::perm_options::remove);
}

bool host_writable(const fs::path& p) {
    return (fs::status(p).permissions() & fs::perms::owner_write) != fs::perms::none;
}

/// Every host path under `root`, with size, type and the write bit: what a
/// "nothing changed" row compares.
std::string fingerprint(const fs::path& root) {
    std::vector<std::string> rows;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::ostringstream o;
        o << fs::relative(e.path(), root).generic_string();
        if (e.is_directory()) {
            o << "/D";
        } else {
            o << ":" << slurp(e.path()) << ":" << host_writable(e.path());
        }
        rows.push_back(o.str());
    }
    std::sort(rows.begin(), rows.end());
    std::string s;
    for (const std::string& r : rows) s += r + "|";
    return s;
}

/// True when the directory `dir` holds an entry whose name is exactly `name`. Not
/// fs::exists(): a Windows host strips a trailing dot or space from the name it is
/// asked about, so exists("dot.") is true when only "dot" is there.
bool lists_exactly(const fs::path& dir, const std::string& name) {
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string() == name) return true;
    return false;
}

/// A fresh fixture root and a class instance served from it.
struct Fx {
    fs::path root;
    H hfs;
    Fx(const fs::path& base, const char* name, bool writable) {
        root = base / name;
        fs::create_directories(root);
        std::string why;
        hfs.configure(root.string(), writable, why);
    }
    void file(const std::string& rel, const std::string& body = "x") { put(root / rel, body); }
    void dir(const std::string& rel) { fs::create_directories(root / rel); }
    bool has(const std::string& rel) const {
        std::error_code ec;
        return fs::exists(root / rel, ec);
    }
    std::string read(const std::string& rel) const { return slurp(root / rel); }
    uint32_t size_of(const std::string& guest) {
        H::StatInfo st;
        return hfs.stat(guest, st) == H::kOk ? st.size : 0xFFFFFFFFu;
    }
    uint8_t attr_of(const std::string& guest) {
        H::StatInfo st;
        return hfs.stat(guest, st) == H::kOk ? st.attr : 0xFF;
    }
};

// ── Dispatcher-level helpers ─────────────────────────────────────────────

bool esx(Emulator& emu, uint8_t code, Z80Registers& r) {
    return emu.cpu().on_esxdos_call && emu.cpu().on_esxdos_call(code, r);
}
bool cy(const Z80Registers& r) { return (r.AF & 1) != 0; }
uint8_t rega(const Z80Registers& r) { return static_cast<uint8_t>(r.AF >> 8); }

void poke_str(Emulator& emu, uint16_t at, const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(at + i), static_cast<uint8_t>(s[i]));
    emu.mmu().write(static_cast<uint16_t>(at + s.size()), 0);
}

constexpr uint16_t kName = 0x8000;
constexpr uint16_t kName2 = 0x8100;
constexpr uint16_t kBuf = 0x8200;

EmulatorConfig make_cfg(const fs::path& root, bool writable) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.esxdos_stub = true;
    cfg.esxdos_stub_root = root.string();
    cfg.esxdos_stub_writable = writable;
    cfg.rewind_buffer_frames = 0;
    return cfg;
}

}  // namespace

void run_esxdos_write_rows(int& passed, int& failed);

void run_esxdos_write_rows(int& passed, int& failed)
{
    g_passed = &passed;
    g_failed = &failed;

    const std::string tag = std::to_string(static_cast<long>(getpid()));
    const fs::path base = fs::temp_directory_path() / ("jnext-hfw-" + tag);
    std::error_code rm_ec;
    fs::remove_all(base, rm_ec);

    // ══ A write-protected root (recording group [ro]) ═════════════════════
    {
        Fx f(base, "ro", false);
        f.file("a.txt", "AAAA");
        f.file("b.txt", "B");
        f.file("rofile.txt", "R"); make_read_only(f.root / "rofile.txt");
        f.dir("e"); f.dir("ne"); f.file("ne/in.txt", "n");
        const std::string before = fingerprint(f.root);

        wcheck("HFW-01",
               "read-only root: F_UNLINK of an existing file, and a wildcard that matches "
               "one, answers esx_edevicebusy (31) and the file stays ([ro] S0008, S000D)",
               f.hfs.unlink("a.txt") == H::kEdevicebusy && f.has("a.txt") &&
                   f.hfs.unlink("*.txt") == H::kEdevicebusy && f.has("a.txt") && f.has("b.txt"));
        wcheck("HFW-02",
               "read-only root: F_RENAME answers esx_edevicebusy, nothing moved ([ro] S0013)",
               f.hfs.rename("a.txt", "z.txt") == H::kEdevicebusy &&
                   f.has("a.txt") && !f.has("z.txt"));
        wcheck("HFW-03",
               "read-only root: F_MKDIR of a new name answers esx_edevicebusy, nothing "
               "created ([ro] S001A)",
               f.hfs.mkdir("newdir") == H::kEdevicebusy && !f.has("newdir"));
        wcheck("HFW-04",
               "read-only root: F_RMDIR of an empty directory answers esx_edevicebusy, "
               "the directory stays ([ro] S001F)",
               f.hfs.rmdir("e") == H::kEdevicebusy && f.has("e"));
        // Declared divergence (generator tag ro-truncate): NextZXOS answers Fc=0 and
        // loses the write; a write that did not happen is not an answer to copy.
        wcheck("HFW-05",
               "read-only root: F_TRUNCATE is refused with the code of every other write "
               "(declared divergence: NextZXOS answers Fc=0, [ro] S0025), size kept",
               f.hfs.truncate("a.txt", 1) == H::kEdevicebusy && f.size_of("a.txt") == 4);
        wcheck("HFW-06",
               "read-only root: F_CHMOD answers esx_edevicebusy after the lookup, even for "
               "a mask of 0, and for a wildcard ([ro] S002C, S002F, S0032); a missing name "
               "is still esx_enoent (S0030)",
               f.hfs.chmod("a.txt", 0, H::kChmodWrite) == H::kEdevicebusy &&
                   f.hfs.chmod("*.txt", 0, H::kChmodWrite) == H::kEdevicebusy &&
                   f.hfs.chmod("a.txt", 1, 0) == H::kEdevicebusy &&
                   f.hfs.chmod("none.txt", 0, H::kChmodWrite) == H::kEnoent &&
                   host_writable(f.root / "a.txt"));
        uint8_t h = 0;
        f.hfs.open("b.txt", H::kModeRead, h);
        wcheck("HFW-07",
               "read-only root: every check that does not write answers first: unlink "
               "missing esx_enoent, mkdir existing esx_eexist, rmdir non-empty esx_enospc, "
               "unlink read-only file esx_erdonly, unlink open file esx_eacces, rename "
               "onto an existing file esx_eexist ([ro] S0009, S001B, S0020, S000B, S0010, S0014)",
               f.hfs.unlink("none.txt") == H::kEnoent && f.hfs.mkdir("e") == H::kEexist &&
                   f.hfs.rmdir("ne") == H::kEnospc && f.hfs.unlink("rofile.txt") == H::kErdonly &&
                   f.hfs.unlink("b.txt") == H::kEacces &&
                   f.hfs.rename("a.txt", "b.txt") == H::kEexist);
        f.hfs.close(h);
        wcheck("HFW-08",
               "read-only root: the host tree (names, contents, write bits) is identical "
               "after all of the above",
               fingerprint(f.root) == before);
    }

    // ══ F_UNLINK ([unlink] group) ══════════════════════════════════════════
    {
        Fx f(base, "unl", true);
        f.file("f1.txt", "1"); f.file("f2.txt", "22"); f.file("keep.txt", "k");
        wcheck("HFW-10",
               "F_UNLINK deletes exactly the named file ([unlink] S0063)",
               f.hfs.unlink("f1.txt") == H::kOk && !f.has("f1.txt") && f.has("f2.txt") &&
                   f.has("keep.txt"));
        wcheck("HFW-11",
               "F_UNLINK of a missing file is esx_enoent, through a missing directory "
               "esx_enotdir ([unlink] S0066, S0067)",
               f.hfs.unlink("f1.txt") == H::kEnoent &&
                   f.hfs.unlink("nodir/x.txt") == H::kEnotdir);
        wcheck("HFW-12",
               "F_UNLINK of an empty name and of the root is esx_einval ([unlink] S0068, S0069)",
               f.hfs.unlink("") == H::kEinval && f.hfs.unlink("C:/") == H::kEinval);
        f.dir("d"); f.dir("nd"); f.file("nd/in.txt", "i");
        wcheck("HFW-13",
               "F_UNLINK of a directory is Fc=0 and deletes nothing, empty or not ([unlink] "
               "S0043, S0045)",
               f.hfs.unlink("d") == H::kOk && f.has("d") && f.hfs.unlink("nd") == H::kOk &&
                   f.has("nd/in.txt"));
        f.file("open.txt", "o");
        uint8_t h = 0;
        f.hfs.open("open.txt", H::kModeRead | H::kModeWrite, h);
        const uint8_t while_open = f.hfs.unlink("open.txt");
        const bool kept = f.has("open.txt");
        f.hfs.close(h);
        wcheck("HFW-14",
               "F_UNLINK of an open file is esx_eacces and it stays; after F_CLOSE it goes "
               "([unlink] S0072, S0075)",
               while_open == H::kEacces && kept && f.hfs.unlink("open.txt") == H::kOk &&
                   !f.has("open.txt"));
        f.file("ro.txt", "r"); make_read_only(f.root / "ro.txt");
        wcheck("HFW-15",
               "F_UNLINK of a read-only file is esx_erdonly and it stays ([unlink] S006F)",
               f.hfs.unlink("ro.txt") == H::kErdonly && f.has("ro.txt"));
        f.file("Long Name.txt", "LONG"); f.file("other.txt", "o");
        wcheck("HFW-16",
               "F_UNLINK by the 8.3 name deletes the file the listing shows under it, and "
               "only it ([unlink] S007B)",
               f.hfs.unlink("LONGNA~1.TXT") == H::kOk && !f.has("Long Name.txt") &&
                   f.has("other.txt"));
        f.file("Case.TXT", "c");
        wcheck("HFW-17",
               "F_UNLINK by a case variant deletes the host-spelled file ([unlink] S0080)",
               f.hfs.unlink("case.txt") == H::kOk && !f.has("Case.TXT"));
    }

    // ══ The name matcher ([wild] group; each pattern run on the recorded file set) ═
    {
        auto m = [](const char* p, const char* n) { return H::wildcard_match(p, n); };
        wcheck("HFW-20",
               "matcher: \"L*.txt\" matches \"Long Name.txt\" and \"longna.txt\", not "
               "\"ab.txt\" ([wild] L*.txt)",
               m("L*.txt", "Long Name.txt") && m("L*.txt", "longna.txt") && !m("L*.txt", "ab.txt"));
        wcheck("HFW-21",
               "matcher: \"*.TXT\" matches every .txt name, not \"x.tar.gz\" ([wild] *.TXT)",
               m("*.TXT", "Long Name.txt") && m("*.TXT", "abc.txt") && !m("*.TXT", "x.tar.gz"));
        wcheck("HFW-22",
               "matcher: \"*.t*\" matches the .txt names and \"a.tx\" ([wild] *.t*)",
               m("*.t*", "a.txt") && m("*.t*", "a.tx") && !m("*.t*", "x.tar.gz") && !m("*.t*", "b"));
        wcheck("HFW-23",
               "matcher: \"Long?Name.txt\" matches \"Long Name.txt\" only ([wild] Long?Name.txt)",
               m("Long?Name.txt", "Long Name.txt") && !m("Long?Name.txt", "longna.txt"));
        wcheck("HFW-24",
               "matcher: '*' is a wildcard only as the LAST character of a half: \"*NAME.TXT\", "
               "\"*ame.txt\", \"L*e.txt\" and \"*~1.txt\" match neither the long nor the 8.3 "
               "name ([wild] *NAME.TXT, *ame.txt, L*e.txt, *~1.txt)",
               !m("*NAME.TXT", "Long Name.txt") && !m("*NAME.TXT", "LONGNA~1.TXT") &&
                   !m("*ame.txt", "Long Name.txt") && !m("L*e.txt", "Long Name.txt") &&
                   !m("L*e.txt", "LONGNA~1.TXT") && !m("*~1.txt", "LONGNA~1.TXT"));
        wcheck("HFW-25",
               "matcher: the extension half must match too: \"Long*\" and \"Lo*\" do not "
               "match \"Long Name.txt\" ([wild] Long*, Lo*)",
               !m("Long*", "Long Name.txt") && !m("Lo*", "Long Name.txt") && m("Lo*", "Lo"));
        wcheck("HFW-26",
               "matcher: both split at the LAST '.': \"*.gz\" matches \"x.tar.gz\" and "
               "\"*.tx\" does not match \"a.txt\" ([wild] *.gz, *.tx)",
               m("*.gz", "x.tar.gz") && !m("*.tx", "a.txt") && m("*.tx", "a.tx"));
        wcheck("HFW-27",
               "matcher: '?' is one character or the end of the half: \"a?.txt\" matches "
               "\"a.txt\" and \"ab.txt\", not \"abc.txt\" ([wild] a?.txt)",
               m("a?.txt", "a.txt") && m("a?.txt", "ab.txt") && !m("a?.txt", "abc.txt"));
        wcheck("HFW-28",
               "matcher: \"b.*\" matches \"b\" (no extension) and \"*\" matches only names "
               "without one ([wild] b.*, *)",
               m("b.*", "b") && m("*", "b") && !m("*", "a.txt") && m("*.*", "b") &&
                   m("*.*", "a.txt"));
        wcheck("HFW-29",
               "matcher: case-insensitive in both halves: \"l*.TXT\" matches \"Long Name.txt\" "
               "([wild] l*.TXT)",
               m("l*.TXT", "Long Name.txt") && m("l*.TXT", "longna.txt"));
    }

    // ══ Wildcard F_UNLINK ([wild], [wildmix]) ═══════════════════════════════
    {
        Fx f(base, "wu", true);
        f.file("Long Name.txt", "L"); f.file("longna.txt", "l"); f.file("other.bin", "o");
        const bool first = f.hfs.unlink("LONGNA~1.*") == H::kOk && !f.has("Long Name.txt") &&
                           f.has("longna.txt");
        Fx g(base, "wu2", true);
        g.file("Long Name.txt", "L"); g.file("longna.txt", "l");
        wcheck("HFW-30",
               "wildcard F_UNLINK matches 8.3 names: \"LONGNA~1.*\" and \"??????~1.TXT\" "
               "delete \"Long Name.txt\" and not \"longna.txt\", whose 8.3 name is its own "
               "([wild] LONGNA~1.*, ??????~1.TXT)",
               first && g.hfs.unlink("??????~1.TXT") == H::kOk && !g.has("Long Name.txt") &&
                   g.has("longna.txt"));
    }
    {
        Fx f(base, "wm", true);
        f.dir("d.txt");
        f.file("a.txt", "a"); f.file("b.txt", "b"); f.file("e.txt", "e");
        wcheck("HFW-31",
               "wildcard F_UNLINK deletes every matching file and passes over a matching "
               "directory ([wildmix] S01FC before the read-only file)",
               f.hfs.unlink("*.txt") == H::kOk && !f.has("a.txt") && !f.has("b.txt") &&
                   !f.has("e.txt") && f.has("d.txt"));
        wcheck("HFW-32",
               "wildcard F_UNLINK with no match is esx_enoent and deletes nothing ([wildmix] "
               "S01E4)",
               f.hfs.unlink("*.zzz") == H::kEnoent && f.has("d.txt"));
        Fx g(base, "wm2", true);
        g.file("a.txt", "a"); g.file("b.txt", "b"); g.file("c.txt", "c"); g.file("e.txt", "e");
        make_read_only(g.root / "c.txt");
        wcheck("HFW-33",
               "wildcard F_UNLINK stops at the first refused match (esx_erdonly): the earlier "
               "ones are deleted, the later one kept ([wildmix] S01FC..S0202)",
               g.hfs.unlink("*.txt") == H::kErdonly && !g.has("a.txt") && !g.has("b.txt") &&
                   g.has("c.txt") && g.has("e.txt"));
        g.dir("sub");
        wcheck("HFW-34",
               "\"*\" in an empty subdirectory matches only \".\" and \"..\": Fc=0 and "
               "nothing is deleted ([wildmix] S0208)",
               g.hfs.unlink("sub/*") == H::kOk && g.has("sub") && g.has("c.txt"));
        wcheck("HFW-35",
               "a wildcard in a DIRECTORY component finds nothing: esx_enoent, nothing "
               "deleted ([wildmix] S020A)",
               g.hfs.unlink("s*/x.txt") == H::kEnoent && g.has("sub") && g.has("e.txt"));
    }

    // ══ F_RENAME ([rename] group) ═══════════════════════════════════════════
    {
        Fx f(base, "rn", true);
        f.file("a.txt", "AAA"); f.file("b.txt", "BB"); f.file("Case.txt", "C");
        f.dir("dir"); f.dir("dir2"); f.dir("dir2/dir");
        wcheck("HFW-40",
               "F_RENAME in place: the new name has the content, the old name is gone "
               "([rename] S0223)",
               f.hfs.rename("a.txt", "a2.txt") == H::kOk && f.read("a2.txt") == "AAA" &&
                   !f.has("a.txt"));
        f.dir("mv");
        wcheck("HFW-41",
               "F_RENAME moves a file into another directory ([rename] a path in another "
               "directory)",
               f.hfs.rename("b.txt", "mv/b9.txt") == H::kOk && f.read("mv/b9.txt") == "BB" &&
                   !f.has("b.txt"));
        f.file("x1.txt", "X1"); f.file("x2.txt", "X2");
        wcheck("HFW-42",
               "F_RENAME onto an existing file is esx_eexist and both files are intact "
               "(POSIX rename(2) and MinGW would replace it) ([rename] S0226)",
               f.hfs.rename("x1.txt", "x2.txt") == H::kEexist && f.read("x1.txt") == "X1" &&
                   f.read("x2.txt") == "X2");
        wcheck("HFW-43",
               "F_RENAME to a case variant of itself is esx_eexist ([rename] S0229)",
               f.hfs.rename("Case.txt", "case.txt") == H::kEexist && f.has("Case.txt"));
        wcheck("HFW-44",
               "F_RENAME into an existing directory puts the source inside it ([rename] S022D)",
               f.hfs.rename("a2.txt", "dir") == H::kOk && f.read("dir/a2.txt") == "AAA" &&
                   !f.has("a2.txt"));
        f.file("dir2/dir/a2.txt", "Z");
        wcheck("HFW-45",
               "F_RENAME into a directory already holding that name is esx_edevicebusy and "
               "nothing moved ([rename] S0233)",
               f.hfs.rename("dir/a2.txt", "dir2/dir") == H::kEdevicebusy &&
                   f.read("dir/a2.txt") == "AAA" && f.read("dir2/dir/a2.txt") == "Z");
        f.file("dir2/dir/in.txt", "I");
        wcheck("HFW-46",
               "F_RENAME of a directory works; into its own subtree is esx_eexist ([rename] "
               "S020F, S0212)",
               f.hfs.rename("dir2/dir", "dir3") == H::kOk && f.read("dir3/in.txt") == "I" &&
                   f.hfs.rename("dir2", "dir2/sub") == H::kEexist && f.has("dir2"));
        f.file("o.txt", "O");
        uint8_t oh = 0;
        f.hfs.open("o.txt", H::kModeRead | H::kModeWrite, oh);
        const uint8_t open_rn = f.hfs.rename("o.txt", "o2.txt");
        f.hfs.close(oh);
        wcheck("HFW-47",
               "F_RENAME of an open file is esx_eacces ([rename] S023C)",
               open_rn == H::kEacces && f.has("o.txt") && !f.has("o2.txt"));
        f.file("ro.txt", "R"); make_read_only(f.root / "ro.txt");
        wcheck("HFW-48",
               "F_RENAME of a read-only file succeeds and it stays read-only ([rename] S0240, "
               "S021A)",
               f.hfs.rename("ro.txt", "ro2.txt") == H::kOk && f.attr_of("ro2.txt") != 0xFF &&
                   (f.attr_of("ro2.txt") & H::kAttrReadOnly));
        wcheck("HFW-49",
               "F_RENAME with a wildcard in either name is esx_einval ([rename] S0242, S0243)",
               f.hfs.rename("*.txt", "zz.txt") == H::kEinval &&
                   f.hfs.rename("o.txt", "z*.txt") == H::kEinval && f.has("o.txt"));
        f.dir("cd"); f.file("cd/f.txt", "F");
        f.hfs.chdir("cd");
        f.file("x9.txt", "x9");
        wcheck("HFW-50",
               "F_RENAME resolves a relative destination from the CWD, not from the source's "
               "directory ([rename] S0258); an empty destination is the CWD itself ([rename] "
               "S0249)",
               f.hfs.rename("f.txt", "../moved.txt") == H::kOk && f.has("moved.txt") &&
                   !f.has("cd/f.txt") && f.hfs.rename("../x9.txt", "") == H::kOk &&
                   f.has("cd/x9.txt") && !f.has("x9.txt"));
        f.hfs.chdir("/");
        f.dir("hd"); f.file("hd/h.txt", "0123456789");
        uint8_t hh = 0, e = f.hfs.open("hd/h.txt", H::kModeRead, hh);
        uint32_t pos = 0;
        f.hfs.seek(hh, H::kSeekSet, 5, pos);
        const uint8_t rn = f.hfs.rename("hd", "hd2");
        std::vector<uint8_t> got;
        const uint8_t rd = f.hfs.read(hh, 4, got);
        uint32_t p2 = 0;
        f.hfs.fgetpos(hh, p2);
        f.hfs.close(hh);
        wcheck("HFW-51",
               "F_RENAME of a directory holding an open file: the handle keeps its position "
               "and F_READ reads the moved file ([rename] S0263..S0266)",
               e == H::kOk && rn == H::kOk && rd == H::kOk && p2 == 9 &&
                   std::string(got.begin(), got.end()) == "5678");
        f.dir("cwd");
        f.hfs.chdir("cwd");
        const uint8_t rc = f.hfs.rename("../cwd", "../cwd2");
        std::string cwd;
        f.hfs.getcwd(cwd);
        uint8_t mh = 0;
        f.hfs.open("made.txt", H::kModeWrite | H::kModeCreatTrunc, mh);
        f.hfs.close(mh);
        wcheck("HFW-52",
               "F_RENAME of the CWD: F_GETCWD shows the new name (declared divergence: "
               "NextZXOS shows the old one, [rename] S026C) and a create lands in the "
               "renamed directory (S0244)",
               rc == H::kOk && cwd == "C:/CWD2/" && f.has("cwd2/made.txt") && !f.has("cwd"));
        f.hfs.chdir("/");
        f.file("n.txt", "n");
        wcheck("HFW-53",
               "F_RENAME of \".\" and \"..\" is esx_eacces, of the root esx_einval, into a "
               "missing directory esx_enotdir ([rename] S0246, S0247, S0248, S0245)",
               f.hfs.rename("dir/.", "dir/dx") == H::kEacces &&
                   f.hfs.rename("dir/..", "dx") == H::kEacces &&
                   f.hfs.rename("C:/", "rootx") == H::kEinval &&
                   f.hfs.rename("n.txt", "nodir/n.txt") == H::kEnotdir && f.has("n.txt"));
    }

    // ══ F_MKDIR and the names it, F_RENAME and F_OPEN refuse ([mkdir] group) ═══
    {
        Fx f(base, "mk", true);
        f.dir("seed");
        wcheck("HFW-60",
               "F_MKDIR creates a directory, and F_OPENDIR of it lists \".\" and \"..\" only "
               "([mkdir] S0274)",
               f.hfs.mkdir("new") == H::kOk && f.has("new") && [&] {
                   uint8_t dh = 0;
                   if (f.hfs.opendir("new", H::kDirLfnOnly, dh) != H::kOk) return false;
                   H::DirEntry de; bool have = false;
                   int n = 0;
                   std::string names;
                   while (f.hfs.readdir(dh, de, have) == H::kOk && have) { ++n; names += de.lfn + ","; }
                   f.hfs.close(dh);
                   return n == 2 && names == ".,..,";
               }());
        f.file("file.txt", "x");
        wcheck("HFW-61",
               "F_MKDIR of an existing directory, an existing file and a case variant is "
               "esx_eexist; of the root esx_einval, not esx_eexist ([mkdir] S0276, S027A, "
               "S0254, S024F)",
               f.hfs.mkdir("new") == H::kEexist && f.hfs.mkdir("file.txt") == H::kEexist &&
                   f.hfs.mkdir("NEW") == H::kEexist && f.hfs.mkdir("C:/") == H::kEinval);
        wcheck("HFW-62",
               "F_MKDIR with a missing parent is esx_enotdir and creates nothing ([mkdir] "
               "S0250)",
               f.hfs.mkdir("no/parent") == H::kEnotdir && !f.has("no"));
        wcheck("HFW-63",
               "F_MKDIR with a wildcard is esx_einval ([mkdir] S027E, S027F)",
               f.hfs.mkdir("w*") == H::kEinval && f.hfs.mkdir("w?") == H::kEinval &&
                   !f.has("w*") && !f.has("w?"));
        const char* bad[] = {"x\"y", "x<y", "x>y", "x|y", "xx:y"};
        bool all_einval = true, none_made = true;
        for (const char* n : bad) {
            all_einval = all_einval && f.hfs.mkdir(n) == H::kEinval;
            none_made = none_made && !f.has(n);
        }
        wcheck("HFW-64",
               "F_MKDIR with \" < > | or : is esx_einval and creates nothing, on every host "
               "([mkdir] S0284..S029C)",
               all_einval && none_made);
        uint8_t dh = 0;
        const uint8_t dot_mk = f.hfs.mkdir("dot.");
        wcheck("HFW-65",
               "F_MKDIR \"dot.\" drops the trailing dot, so \"dot\" and \"dot.\" are the same "
               "directory ([mkdir] S02AA..S02AE); a trailing space is refused with esx_einval "
               "(declared divergence: NextZXOS keeps it)",
               dot_mk == H::kOk && lists_exactly(f.root, "dot") && !lists_exactly(f.root, "dot.") &&
                   f.hfs.opendir("dot.", H::kDirLfnOnly, dh) == H::kOk &&
                   f.hfs.close(dh) == H::kOk && f.hfs.opendir("dot", H::kDirLfnOnly, dh) == H::kOk &&
                   f.hfs.close(dh) == H::kOk && f.hfs.mkdir("sp ") == H::kEinval &&
                   !lists_exactly(f.root, "sp ") && !lists_exactly(f.root, "sp"));
        f.file("rn.txt", "r");
        wcheck("HFW-66",
               "F_RENAME to a name with \" < > | is esx_einval and nothing moves ([mkdir] "
               "S0277, S027A..S027C)",
               f.hfs.rename("rn.txt", "r\"n.txt") == H::kEinval &&
                   f.hfs.rename("rn.txt", "r<n.txt") == H::kEinval &&
                   f.hfs.rename("rn.txt", "r>n.txt") == H::kEinval &&
                   f.hfs.rename("rn.txt", "r|n.txt") == H::kEinval && f.has("rn.txt"));
        uint8_t ch = 0;
        const uint8_t c1 = f.hfs.open("f\"g.txt", H::kModeWrite | H::kModeCreatTrunc, ch);
        const uint8_t c2 = f.hfs.open("f*g.txt", H::kModeWrite | H::kModeCreatTrunc, ch);
        const uint8_t c3 = f.hfs.open("f?g.txt", H::kModeWrite | H::kModeCreatTrunc, ch);
        wcheck("HFW-67",
               "F_OPEN create with \" or a wildcard character is esx_einval and creates "
               "nothing ([mkdir] S0286, S0296, S029A)",
               c1 == H::kEinval && c2 == H::kEinval && c3 == H::kEinval && !f.has("f\"g.txt") &&
                   !f.has("f*g.txt") && !f.has("f?g.txt"));
    }

    // ══ F_RMDIR ([rmdir] group) ═════════════════════════════════════════════
    {
        Fx f(base, "rd", true);
        f.dir("e"); f.dir("e2"); f.dir("ne"); f.file("ne/f.txt", "f"); f.file("file.txt", "x");
        wcheck("HFW-70",
               "F_RMDIR removes an empty directory ([rmdir] S02C3)",
               f.hfs.rmdir("e") == H::kOk && !f.has("e"));
        wcheck("HFW-71",
               "F_RMDIR of a non-empty directory is esx_enospc and it stays ([rmdir] S02C5)",
               f.hfs.rmdir("ne") == H::kEnospc && f.has("ne/f.txt"));
        wcheck("HFW-72",
               "F_RMDIR of a file is esx_enotdir, of a missing name esx_enoent ([rmdir] "
               "S029B, S029D)",
               f.hfs.rmdir("file.txt") == H::kEnotdir && f.has("file.txt") &&
                   f.hfs.rmdir("none") == H::kEnoent);
        wcheck("HFW-73",
               "F_RMDIR of the root and with a wildcard is esx_einval ([rmdir] S02CA, S02CC)",
               f.hfs.rmdir("C:/") == H::kEinval && f.hfs.rmdir("e*") == H::kEinval && f.has("e2"));
        f.hfs.chdir("e2");
        wcheck("HFW-74",
               "F_RMDIR of the CWD is esx_eacces, also as \".\" ([rmdir] S02D0, S02D2)",
               f.hfs.rmdir("../e2") == H::kEacces && f.hfs.rmdir(".") == H::kEacces && f.has("e2"));
        f.hfs.chdir("/");
    }

    // ══ F_TRUNCATE / F_FTRUNCATE ([trunc] group) ════════════════════════════
    {
        Fx f(base, "tr", true);
        std::string body(120, 'a');
        for (int i = 0; i < 120; ++i) body[i] = static_cast<char>('a' + i % 26);
        f.file("s.txt", body);
        wcheck("HFW-80",
               "F_TRUNCATE shrinks to the size in BCDE, the prefix intact ([trunc] S02DF)",
               f.hfs.truncate("s.txt", 50) == H::kOk && f.read("s.txt") == body.substr(0, 50));
        const bool ext = f.hfs.truncate("s.txt", 200) == H::kOk;
        const std::string grown = f.read("s.txt");
        wcheck("HFW-81",
               "F_TRUNCATE extends, the added bytes read as zero ([trunc] S02E5, S02E9)",
               ext && grown.size() == 200 && grown.substr(0, 50) == body.substr(0, 50) &&
                   grown.substr(50) == std::string(150, '\0'));
        f.dir("dd");
        wcheck("HFW-82",
               "F_TRUNCATE of a directory is esx_einval, of a missing name esx_enoent ([trunc] "
               "S02C2, S02C4)",
               f.hfs.truncate("dd", 5) == H::kEinval && f.hfs.truncate("none", 5) == H::kEnoent);
        f.file("o.txt", std::string(100, 'o'));
        uint8_t oh = 0;
        f.hfs.open("o.txt", H::kModeWrite, oh);
        const uint8_t t_open = f.hfs.truncate("o.txt", 10);
        f.hfs.close(oh);
        f.file("r.txt", std::string(100, 'r')); make_read_only(f.root / "r.txt");
        wcheck("HFW-83",
               "F_TRUNCATE of an open file is esx_eacces, of a read-only file esx_erdonly, both "
               "unchanged ([trunc] S0306, S030D)",
               t_open == H::kEacces && f.size_of("o.txt") == 100 &&
                   f.hfs.truncate("r.txt", 10) == H::kErdonly && f.size_of("r.txt") == 100);
        f.dir("tw"); f.file("tw/t1.txt", std::string(10, '1')); f.file("tw/t2.txt", std::string(20, '2'));
        f.file("tw/t3.dat", std::string(30, '3'));
        wcheck("HFW-84",
               "F_TRUNCATE with a wildcard truncates the FIRST match only; no match is "
               "esx_enoent ([trunc] S0311..S0315)",
               f.hfs.truncate("tw/*.txt", 3) == H::kOk && f.size_of("tw/t1.txt") == 3 &&
                   f.size_of("tw/t2.txt") == 20 && f.size_of("tw/t3.dat") == 30 &&
                   f.hfs.truncate("tw/*.zzz", 3) == H::kEnoent);
        f.file("f.txt", std::string(100, 'f'));
        uint8_t fh = 0;
        f.hfs.open("f.txt", H::kModeRead | H::kModeWrite, fh);
        uint32_t pos = 0;
        f.hfs.seek(fh, H::kSeekSet, 60, pos);
        const uint8_t s1 = f.hfs.ftruncate(fh, 30);
        uint32_t p1 = 0;
        f.hfs.fgetpos(fh, p1);
        const uint32_t sz1 = f.size_of("f.txt");
        const uint8_t s2 = f.hfs.ftruncate(fh, 90);
        uint32_t p2 = 0;
        f.hfs.fgetpos(fh, p2);
        wcheck("HFW-85",
               "F_FTRUNCATE sets the size; F_FGETPOS is unchanged, also past the new end "
               "([trunc] S0314..S0319)",
               s1 == H::kOk && s2 == H::kOk && p1 == 60 && p2 == 60 && sz1 == 30 &&
                   f.size_of("f.txt") == 90);
        f.hfs.close(fh);
        uint8_t rh = 0;
        f.hfs.open("f.txt", H::kModeRead, rh);
        const uint8_t rdonly = f.hfs.ftruncate(rh, 10);
        f.hfs.close(rh);
        wcheck("HFW-86",
               "F_FTRUNCATE of a read handle is esx_erdonly, of a closed handle esx_ebadf "
               "([trunc] S031F, S0321)",
               rdonly == H::kErdonly && f.hfs.ftruncate(rh, 10) == H::kEbadf &&
                   f.size_of("f.txt") == 90);
    }

    // ══ F_CHMOD and the read-only attribute ([chmod] group) ═════════════════
    {
        Fx f(base, "ch", true);
        f.file("f.txt", "12345");
        const uint8_t c0 = f.hfs.chmod("f.txt", 0x00, H::kChmodWrite);
        uint8_t fh = 0;
        f.hfs.open("f.txt", H::kModeRead, fh);
        H::StatInfo fst;
        f.hfs.fstat(fh, fst);
        f.hfs.close(fh);
        uint8_t dh = 0;
        f.hfs.opendir("/", H::kDirLfnOnly, dh);
        H::DirEntry de; bool have = false;
        uint8_t rd_attr = 0xFF;
        while (f.hfs.readdir(dh, de, have) == H::kOk && have)
            if (de.lfn == "f.txt") rd_attr = de.attr;
        f.hfs.close(dh);
        wcheck("HFW-90",
               "F_CHMOD A_WRITE clear: read-only in F_STAT, F_FSTAT and F_READDIR, and the "
               "host write bits are gone ([chmod] S0328, S0329)",
               c0 == H::kOk && (f.attr_of("f.txt") & H::kAttrReadOnly) &&
                   (fst.attr & H::kAttrReadOnly) && (rd_attr & H::kAttrReadOnly) &&
                   !host_writable(f.root / "f.txt"));
        wcheck("HFW-91",
               "F_CHMOD A_WRITE set clears the read-only attribute again ([chmod] S032A, S032B)",
               f.hfs.chmod("f.txt", H::kChmodWrite, H::kChmodWrite) == H::kOk &&
                   !(f.attr_of("f.txt") & H::kAttrReadOnly) && host_writable(f.root / "f.txt"));
        f.hfs.chmod("f.txt", 0, H::kChmodWrite);
        uint8_t h1 = 0, h2 = 0, h3 = 0, h4 = 0;
        const uint8_t o2 = f.hfs.open("f.txt", H::kModeWrite, h1);
        const uint8_t o3 = f.hfs.open("f.txt", H::kModeRead | H::kModeWrite, h2);
        const uint8_t oe = f.hfs.open("f.txt", H::kModeWrite | H::kModeCreatTrunc, h3);
        const uint8_t on = f.hfs.open("f.txt", H::kModeWrite | H::kModeCreatNoExist, h4);
        const uint8_t orr = f.hfs.open("f.txt", H::kModeRead, h4);
        f.hfs.close(h4);
        wcheck("HFW-92",
               "a read-only file refuses F_OPEN for write and creat_trunc with esx_erdonly, "
               "esx_eexist for creat_noexist, and opens for read ([openro] S03A6..S03B4)",
               o2 == H::kErdonly && o3 == H::kErdonly && oe == H::kErdonly &&
                   on == H::kEexist && orr == H::kOk && f.read("f.txt") == "12345");
        f.hfs.chmod("f.txt", H::kChmodWrite, H::kChmodWrite);
        f.dir("dd");
        const std::string before = fingerprint(f.root);
        const uint8_t hid = f.hfs.chmod("f.txt", H::kChmodHidden, H::kChmodHidden);
        const uint8_t sys = f.hfs.chmod("f.txt", H::kChmodSystem, H::kChmodSystem);
        const uint8_t arc = f.hfs.chmod("f.txt", 0, H::kChmodArch);
        const uint8_t all = f.hfs.chmod("f.txt", 0, 0xFF);
        const uint8_t dirw = f.hfs.chmod("dd", 0, H::kChmodWrite);
        wcheck("HFW-93",
               "F_CHMOD changes the host cannot store (hidden, system, archive cleared, any "
               "mask outside A_WRITE/A_READ/A_ARCH, A_WRITE on a directory) are esx_enosys and "
               "change nothing (declared divergence: NextZXOS stores them, [chmod] S0330..S0344, "
               "S0370)",
               hid == H::kEnosys && sys == H::kEnosys && arc == H::kEnosys && all == H::kEnosys &&
                   dirw == H::kEnosys && fingerprint(f.root) == before);
        wcheck("HFW-94",
               "F_CHMOD that leaves the reported bits as they are is Fc=0: A_ARCH set, A_READ "
               "either way, a mask of 0 ([chmod] S032C..S032F, S033A, S033C)",
               f.hfs.chmod("f.txt", H::kChmodArch, H::kChmodArch) == H::kOk &&
                   f.hfs.chmod("f.txt", 0, H::kChmodRead) == H::kOk &&
                   f.hfs.chmod("f.txt", H::kChmodRead, H::kChmodRead) == H::kOk &&
                   f.hfs.chmod("f.txt", 0, 0) == H::kOk && f.hfs.chmod("dd", 0, H::kChmodRead) == H::kOk &&
                   fingerprint(f.root) == before);
        f.file("w1.txt", "1"); f.file("w2.txt", "2"); f.file("v.dat", "3");
        wcheck("HFW-95",
               "wildcard F_CHMOD applies to every matching file; a pattern with no match is "
               "esx_enoent ([chmod] S038D..S0390)",
               f.hfs.chmod("w*.txt", 0, H::kChmodWrite) == H::kOk &&
                   !host_writable(f.root / "w1.txt") && !host_writable(f.root / "w2.txt") &&
                   host_writable(f.root / "v.dat") && f.hfs.chmod("*.zzz", 0, H::kChmodWrite) == H::kEnoent);
        wcheck("HFW-96",
               "F_CHMOD of a missing name is esx_enoent, of the root esx_einval ([chmod] "
               "S0316, S032F)",
               f.hfs.chmod("none.txt", 0, H::kChmodWrite) == H::kEnoent &&
                   f.hfs.chmod("C:/", 0, H::kChmodWrite) == H::kEinval);
        f.file("host-ro.txt", "h"); make_read_only(f.root / "host-ro.txt");
        wcheck("HFW-97",
               "a host file with no write permission reports read-only on a WRITABLE root "
               "(F_STAT of a file made read-only on the host)",
               (f.attr_of("host-ro.txt") & H::kAttrReadOnly) != 0 &&
                   (f.attr_of("v.dat") & H::kAttrReadOnly) == 0);
    }

    // ══ 8.3 aliases stay put; host-only aliases are refused ([alias]) ═══════
    {
        Fx f(base, "al", true);
        f.file("Long Name a.txt", "a"); f.file("Long Name b.txt", "bb"); f.file("Long Name c.txt", "ccc");
        const uint32_t s1 = f.size_of("LONGNA~1.TXT"), s2 = f.size_of("LONGNA~2.TXT"),
                       s3 = f.size_of("LONGNA~3.TXT");
        f.hfs.unlink("LONGNA~1.TXT");
        const uint32_t t2 = f.size_of("LONGNA~2.TXT"), t3 = f.size_of("LONGNA~3.TXT");
        const bool neighbours = t2 == 2 && t3 == 3;
        f.hfs.unlink("LONGNA~2.TXT");
        wcheck("HFW-100",
               "stable 8.3: after deleting LONGNA~1.TXT, LONGNA~2.TXT still names the same "
               "file, and deleting it does not touch its neighbour ([alias] S03C1..S03CC)",
               s1 == 1 && s2 == 2 && s3 == 3 && neighbours && !f.has("Long Name b.txt") &&
                   f.has("Long Name c.txt") && f.size_of("LONGNA~3.TXT") == 3);
        f.file("Long Name d.txt", "dddd");
        const uint32_t d1 = f.size_of("LONGNA~1.TXT");
        f.hfs.rename("LONGNA~3.TXT", "Long Name e.txt");
        const uint32_t e2 = f.size_of("LONGNA~2.TXT");
        const uint32_t d1b = f.size_of("LONGNA~1.TXT");
        Fx n(base, "al5", true);
        n.file("Long Name a.txt", "a"); n.file("Long Name b.txt", "bb");
        n.size_of("LONGNA~1.TXT");
        n.hfs.unlink("LONGNA~1.TXT"); n.hfs.unlink("LONGNA~2.TXT");
        n.file("Long Name b.txt", "bbb");
        const uint32_t freed = n.size_of("LONGNA~1.TXT");
        const bool tail2 = n.size_of("LONGNA~2.TXT") == 0xFFFFFFFFu;
        // host-side removal (no guest call to free the alias): the next listing forgets it
        Fx hs(base, "al6", true);
        hs.file("Long Name a.txt", "a"); hs.file("Long Name b.txt", "bb");
        hs.size_of("LONGNA~1.TXT");
        fs::remove(hs.root / "Long Name a.txt"); fs::remove(hs.root / "Long Name b.txt");
        hs.size_of("LONGNA~1.TXT");
        hs.file("Long Name b.txt", "bbbb");
        const uint32_t host_freed = hs.size_of("LONGNA~1.TXT");
        // a guest rename frees the alias of the name that went, at once
        Fx rn(base, "al7", true);
        rn.file("Long Name a.txt", "a"); rn.file("Long Name b.txt", "bb"); rn.file("Long Name c.txt", "ccc");
        rn.size_of("LONGNA~1.TXT");
        rn.hfs.unlink("LONGNA~1.TXT");
        rn.hfs.rename("LONGNA~3.TXT", "Long Name e.txt");
        rn.file("Long Name c.txt", "c-again");
        const uint32_t renamed_freed = rn.size_of("LONGNA~1.TXT");
        Fx g(base, "al2", true);
        g.file("Long Name a.txt", "a");
        const uint32_t a1 = g.size_of("LONGNA~1.TXT");
        g.file("LONGNA~1.TXT", "native");
        wcheck("HFW-101",
               "stable 8.3: a new long name takes the lowest free tail, a renamed file gets a "
               "fresh alias, and a native 8.3 host name always wins and displaces the long name "
               "([alias] S03CC..S03D8)",
               d1 == 4 && e2 == 3 && d1b == 4 && a1 == 1 && g.size_of("LONGNA~1.TXT") == 6 &&
                   g.size_of("LONGNA~2.TXT") == 1 && freed == 3 && tail2 && host_freed == 4 &&
                   renamed_freed == 7);
        Fx h(base, "al3", true);
        h.dir("sub"); h.file("sub/x.txt", "x");
        const uint8_t cd = h.hfs.chdir("SUB");
        std::string cwd;
        h.hfs.getcwd(cwd);
        wcheck("HFW-102",
               "any-case path stores the HOST spelling: F_CHDIR \"SUB\" for a host \"sub\" "
               "leaves F_GETCWD \"C:/SUB/\" and the snapshot CWD \"sub\" (a real test where the "
               "host answers to both spellings: Windows, macOS; a control on Linux)",
               cd == H::kOk && cwd == "C:/SUB/" && h.hfs.cwd_for_snapshot() == "sub");
        // The host's OWN alias must never reach a file: it answers differently
        // from the listing, and with F_UNLINK the file it names is destroyed.
        Fx k(base, "al4", true);
        k.file("Long Name b.txt", "BBB");        // created first: NTFS numbers it ~1
        k.file("Long Name a.txt", "A");          // jnext's sorted listing numbers THIS ~1
        bool precondition = true;
        std::string alias_seen = "(no host alias on this platform: control)";
        bool alias_is_listed = false;      // the host's alias for b is one of the names jnext lists
        std::string host_alias;
#ifdef _WIN32
        {
            wchar_t shortw[MAX_PATH] = {0};
            const std::wstring longw = (k.root / "Long Name b.txt").wstring();
            const DWORD n = GetShortPathNameW(longw.c_str(), shortw, MAX_PATH);
            std::string full;
            for (DWORD i = 0; i < n; ++i) full.push_back(static_cast<char>(shortw[i]));
            host_alias = fs::path(full).filename().string();
            alias_seen = host_alias;
            // The precondition: the host really has an alias for b (NTFS numbers it by
            // creation order, LONGNA~1.TXT; wine hashes one, LONG~PSN.TXT), else this
            // row proves nothing.
            precondition = n > 0 && !host_alias.empty() && host_alias != "Long Name b.txt";
            std::string up = host_alias;
            for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            alias_is_listed = up == "LONGNA~1.TXT" || up == "LONGNA~2.TXT";
        }
#endif
#ifdef __APPLE__
        {
            // An NFD-named file answers to its NFC spelling on APFS, which the listing
            // does not know.
            put(k.root / "e\xCC\x81.txt", "NFD");
        }
#endif
        const uint32_t via_alias = k.size_of("LONGNA~1.TXT");
        // The host's own alias, when it is not a name jnext lists: refused, never b.
        bool host_alias_refused = true;
        if (!host_alias.empty() && !alias_is_listed) {
            H::StatInfo hs;
            host_alias_refused = k.hfs.stat(host_alias, hs) != H::kOk &&
                                 k.hfs.unlink(host_alias) != H::kOk;
        }
        const uint8_t del = k.hfs.unlink("LONGNA~1.TXT");
        bool apple_ok = true;
#ifdef __APPLE__
        {
            H::StatInfo nfc;
            apple_ok = k.hfs.stat("\xC3\xA9.txt", nfc) != H::kOk && k.has("e\xCC\x81.txt");
        }
#endif
        wcheck("HFW-103",
               "the host's own alias never reaches a different file than the listing names: "
               "\"LONGNA~1.TXT\" is the file jnext lists under it (\"Long Name a.txt\"), the "
               "one a host numbering by creation order calls that is untouched, and an alias "
               "the listing does not know is refused (Windows: its own short name for the first "
               "file made; macOS: an NFC spelling of an NFD name; Linux: a control with no host "
               "alias)",
               precondition && apple_ok && host_alias_refused && via_alias == 1 && del == H::kOk &&
                   !k.has("Long Name a.txt") && k.has("Long Name b.txt") &&
                   k.read("Long Name b.txt") == "BBB",
               "precondition=" + std::to_string(precondition) + " alias=" + alias_seen +
                   " via_alias=" + std::to_string(via_alias));
    }

    // ══ Containment for the new calls ═══════════════════════════════════════
    {
        Fx f(base, "ct/root", true);
        put(base / "ct" / "outside.txt", "OUT");
        put(base / "ct" / "o2.txt", "OUT2");
        const std::string outside_before = slurp(base / "ct" / "outside.txt");
        f.file("in.txt", "in");
        wcheck("HFW-105",
               "F_UNLINK, F_TRUNCATE and F_CHMOD of \"../x\" are esx_enotdir (as NextZXOS "
               "answers an escape, [dotdot] S002F..S0031), of a bare \"..\" esx_enoent "
               "([dotdot] S0037, S0029, S002A) and the file outside is intact",
               f.hfs.unlink("../outside.txt") == H::kEnotdir &&
                   f.hfs.truncate("../outside.txt", 1) == H::kEnotdir &&
                   f.hfs.chmod("../outside.txt", 0, H::kChmodWrite) == H::kEnotdir &&
                   f.hfs.unlink("C:/../outside.txt") == H::kEnotdir &&
                   f.hfs.unlink("..") == H::kEnoent &&
                   f.hfs.truncate("C:/..", 1) == H::kEnoent &&
                   f.hfs.chmod("..", 0, H::kChmodWrite) == H::kEnoent &&
                   slurp(base / "ct" / "outside.txt") == outside_before &&
                   host_writable(base / "ct" / "outside.txt"));
        wcheck("HFW-106",
               "F_RENAME with the source or the destination outside the root is refused "
               "(esx_enotdir, [dotdot] S001A, S001B; a bare \"..\": source esx_enoent, destination "
               "esx_einval, S002B, S002C) and nothing moves",
               f.hfs.rename("../outside.txt", "in2.txt") == H::kEnotdir &&
                   f.hfs.rename("in.txt", "../moved.txt") == H::kEnotdir &&
                   f.hfs.rename("..", "in2.txt") == H::kEnoent &&
                   f.hfs.rename("in.txt", "C:/..") == H::kEinval && f.has("in.txt") && !fs::exists(base / "ct" / "moved.txt") &&
                   fs::exists(base / "ct" / "outside.txt"));
        wcheck("HFW-107",
               "F_MKDIR and F_RMDIR of \"../x\" and a wildcard F_UNLINK of \"../*.txt\" are "
               "refused (esx_enotdir, [dotdot] S0015, S002E, S002F); a bare \"..\" is esx_einval for "
               "F_MKDIR, esx_enoent for F_RMDIR, esx_enotdir for F_OPENDIR and F_CHDIR "
               "(S0028, S0038, S0034, S0035); nothing outside changes",
               f.hfs.mkdir("../newdir") == H::kEnotdir && f.hfs.rmdir("../x") == H::kEnotdir &&
                   f.hfs.mkdir("..") == H::kEinval && f.hfs.rmdir("..") == H::kEnoent && [&] {
                       uint8_t dh = 0;
                       return f.hfs.opendir("C:/..", H::kDirLfnOnly, dh) == H::kEnotdir &&
                              f.hfs.chdir("..") == H::kEnotdir;
                   }() &&
                   f.hfs.unlink("../*.txt") == H::kEnotdir && !fs::exists(base / "ct" / "newdir") &&
                   fs::exists(base / "ct" / "outside.txt") && fs::exists(base / "ct" / "o2.txt"));
    }

    // ══ Aliases and collisions through F_RENAME (review round 1) ═════════════
    {
        const uint32_t none = 0xFFFFFFFFu;
        // A directory keeps its 8.3 aliases when it is renamed: after a delete inside
        // made the fresh numbering differ, a short name still names the same file.
        Fx f(base, "rk", true);
        f.file("D/Long Name a.txt", "a"); f.file("D/Long Name b.txt", "bb");
        f.file("D/Long Name c.txt", "ccc");
        f.size_of("D/LONGNA~1.TXT");
        f.hfs.unlink("D/LONGNA~1.TXT");
        const uint8_t r1 = f.hfs.rename("D", "E");
        wcheck("HFW-140",
               "F_RENAME of a directory in place keeps the 8.3 aliases of what is inside: "
               "after deleting LONGNA~1.TXT, LONGNA~2.TXT and ~3 still name b and c in the "
               "renamed directory ([alias] S03C1..S03CC)",
               r1 == H::kOk && f.size_of("E/LONGNA~1.TXT") == none &&
                   f.size_of("E/LONGNA~2.TXT") == 2 && f.size_of("E/LONGNA~3.TXT") == 3);
        Fx g(base, "rk2", true);
        g.file("D/Long Name a.txt", "a"); g.file("D/Long Name b.txt", "bb");
        g.file("D/Long Name c.txt", "ccc"); g.dir("T");
        g.size_of("D/LONGNA~1.TXT");
        g.hfs.unlink("D/LONGNA~1.TXT");
        const uint8_t r2 = g.hfs.rename("D", "T");
        wcheck("HFW-141",
               "and the same when it is renamed INTO an existing directory ([alias] S03C1..S03CC)",
               r2 == H::kOk && g.size_of("T/D/LONGNA~1.TXT") == none &&
                   g.size_of("T/D/LONGNA~2.TXT") == 2 && g.size_of("T/D/LONGNA~3.TXT") == 3);

        // A name already held in the target directory is esx_edevicebusy whatever the
        // host's case rules are, and an 8.3 alias counts as the name it stands for.
        Fx c(base, "rc", true);
        c.file("D/A2.TXT", "held"); c.file("a2.txt", "moving");
        const uint8_t c1 = c.hfs.rename("a2.txt", "D");
        wcheck("HFW-142",
               "F_RENAME into a directory that holds a case variant of the name is "
               "esx_edevicebusy on every host and nothing moves ([rename] S0233; FAT is "
               "case-insensitive, [unlink] S0080)",
               c1 == H::kEdevicebusy && c.read("D/A2.TXT") == "held" && c.has("a2.txt") &&
                   c.read("a2.txt") == "moving" && !lists_exactly(c.root / "D", "a2.txt"));
        Fx a(base, "ra", true);
        a.file("E/Long Name.txt", "long"); a.file("LONGNA~1.TXT", "native");
        const uint32_t before = a.size_of("E/LONGNA~1.TXT");
        const uint8_t a1 = a.hfs.rename("/LONGNA~1.TXT", "E");
        wcheck("HFW-143",
               "F_RENAME of a native 8.3 name into a directory where a long name holds that "
               "alias is esx_edevicebusy, nothing moves, and the long-named file keeps its "
               "alias (a later delete by that short name must not reach the moved file)",
               a1 == H::kEdevicebusy && before == 4 && a.size_of("E/LONGNA~1.TXT") == 4 &&
                   a.has("LONGNA~1.TXT") && !a.has("E/LONGNA~1.TXT") && a.has("E/Long Name.txt"));

        // Unmeasured branches, pinned so they cannot drift: a wildcard whose first
        // match is a directory is esx_einval as F_TRUNCATE of a directory is ([trunc]
        // S02C2); a directory renamed into its own existing subdirectory is refused
        // before the host is asked; a wildcard F_CHMOD never touches "." or "..".
        Fx t(base, "rt", true);
        t.dir("w/nodot"); t.file("w/f.txt", "12345");
        wcheck("HFW-144",
               "F_TRUNCATE with a wildcard whose first match is a directory is esx_einval "
               "and truncates nothing (unmeasured; follows [trunc] S02EE)",
               t.hfs.truncate("w/n*", 1) == H::kEinval && t.size_of("w/f.txt") == 5);
        t.dir("P/Q");
        wcheck("HFW-145",
               "F_RENAME of a directory into an existing directory inside it is esx_eexist "
               "(not a host failure) and nothing moves",
               t.hfs.rename("P", "P/Q") == H::kEexist && t.has("P/Q") && !t.has("P/Q/P"));
        t.dir("cs"); t.file("cs/a.txt", "a");
        wcheck("HFW-146",
               "wildcard F_CHMOD in a subdirectory skips \".\" and \"..\": A_WRITE clear on "
               "\"*\" matches only the file, so it is Fc=0 and not esx_enosys "
               "for a directory",
               t.hfs.chmod("cs/a*.txt", 0, H::kChmodWrite) == H::kOk && !host_writable(t.root / "cs/a.txt") &&
                   t.hfs.chmod("cs/*.*", 0, H::kChmodWrite) == H::kOk);
    }

    // ══ A new long name never takes an existing file's 8.3 alias (review round 2) ═
    {
        // "Save Game 1" sorts BEFORE "Save Game 2", which holds SAVEGA~1.SAV; FAT
        // fixes an alias when its file is created, so the newcomer gets ~2.
        auto kept = [&](Fx& f) {
            return f.size_of("S/SAVEGA~1.SAV") == 2 && f.size_of("S/SAVEGA~2.SAV") == 1;
        };
        Fx a(base, "n1a", true);
        a.file("S/Save Game 2.sav", "22"); a.size_of("S/SAVEGA~1.SAV");
        uint8_t h = 0;
        const uint8_t oc = a.hfs.open("S/Save Game 1.sav", H::kModeWrite | H::kModeCreatTrunc, h);
        std::size_t wrote = 0;
        a.hfs.write(h, reinterpret_cast<const uint8_t*>("1"), 1, wrote);
        a.hfs.close(h);
        wcheck("HFW-147",
               "a long name CREATED next to an aliased one that sorts after it takes the lowest "
               "free tail: the existing file keeps SAVEGA~1.SAV ([alias] a new name takes the "
               "lowest free tail, aliases stay put)",
               oc == H::kOk && kept(a));
        Fx b(base, "n1b", true);
        b.file("S/Save Game 2.sav", "22"); b.file("S/tmp.sav", "1"); b.size_of("S/SAVEGA~1.SAV");
        const uint8_t rb = b.hfs.rename("S/tmp.sav", "S/Save Game 1.sav");
        wcheck("HFW-148",
               "a file RENAMED to such a name leaves the existing file's alias alone",
               rb == H::kOk && kept(b));
        Fx c(base, "n1c", true);
        c.file("S/Save Game 2.sav", "22"); c.file("Save Game 1.sav", "1"); c.size_of("S/SAVEGA~1.SAV");
        const uint8_t rc = c.hfs.rename("Save Game 1.sav", "S");
        wcheck("HFW-149",
               "a file MOVED into the directory leaves the existing file's alias alone",
               rc == H::kOk && kept(c));
        Fx d(base, "n1d", true);
        d.file("S/Save Game 2.sav", "22"); d.size_of("S/SAVEGA~1.SAV");
        const uint8_t md = d.hfs.mkdir("S/Save Game 1.sav");
        H::StatInfo dst;
        wcheck("HFW-150",
               "a directory MADE with such a name leaves the existing file's alias alone and "
               "takes ~2 (F_STAT of a directory is esx_einval)",
               md == H::kOk && d.size_of("S/SAVEGA~1.SAV") == 2 &&
                   d.hfs.stat("S/SAVEGA~2.SAV", dst) == H::kEinval);

        // A long-named file moved into a directory holding a same-prefix long name is
        // not a collision: it gets a FRESH alias there (as a renamed file does).
        Fx m1(base, "n2a", true);
        m1.file("A/Save Game 1.sav", "1"); m1.file("B/Save Game 2.sav", "22");
        m1.size_of("B/SAVEGA~1.SAV");
        const uint8_t mv = m1.hfs.rename("A/Save Game 1.sav", "B");
        wcheck("HFW-151",
               "moving a long name into a directory holding a same-prefix long name is Fc=0: "
               "the moved file gets a fresh alias and the existing one keeps its own",
               mv == H::kOk && m1.size_of("B/SAVEGA~1.SAV") == 2 &&
                   m1.size_of("B/SAVEGA~2.SAV") == 1);
        Fx m2(base, "n2b", true);
        m2.file("A/Save Game 1.sav", "1"); m2.file("B/Save Game 2.sav", "22");
        m2.size_of("B/SAVEGA~1.SAV");
        wcheck("HFW-152",
               "the into-directory spelling and the explicit-name spelling of the same move "
               "answer alike (Fc=0 both)",
               m2.hfs.rename("A/Save Game 1.sav", "B/Save Game 4.sav") == H::kOk && mv == H::kOk);
        Fx t1(base, "n2c", true);
        t1.file("D/Save Game.sav", "held"); t1.file("SAVE GAME.SAV", "moving");
        wcheck("HFW-153",
               "a long name held in the target in another case is a collision (esx_edevicebusy): "
               "the case term on its own, a long name has no matching 8.3 form",
               t1.hfs.rename("SAVE GAME.SAV", "D") == H::kEdevicebusy && t1.has("SAVE GAME.SAV"));
        std::string cwd;
        Fx t2(base, "n2d", true);
        t2.file("f.txt", "x");
        wcheck("HFW-154",
               "F_GETCWD A=$FF of \"../x\" at the root is esx_enotdir, not an internal marker; "
               "a wildcard under a file or a missing directory is esx_enotdir too",
               t2.hfs.getcwd_of("../x", cwd) == H::kEnotdir &&
                   t2.hfs.unlink("f.txt/*") == H::kEnotdir && t2.hfs.unlink("nodir/*") == H::kEnotdir);
    }

    // ══ The dispatcher: registers, claims, and what a refusal looks like ═════
    {
        const fs::path droot = base / "disp";
        fs::create_directories(droot);
        put(droot / "a.txt", "AAAA");
        put(droot / "b.txt", "BBBBB");
        put(droot / "big.bin", std::string(100, 'z'));
        Emulator emu;
        emu.init(make_cfg(droot, true));

        Z80Registers r{};
        poke_str(emu, kName, "c:/a.txt");
        r.IX = kName;
        const bool h110 = esx(emu, 0xAD, r);
        wcheck("HFW-110",
               "$AD F_UNLINK takes the filespec in IX; a drive-qualified \"c:/a.txt\" deletes "
               "it, Fc=0",
               h110 && !cy(r) && !fs::exists(droot / "a.txt"));

        Z80Registers rn{};
        poke_str(emu, kName, "b.txt");
        poke_str(emu, kName2, "bb.txt");
        rn.IX = kName;
        rn.DE = kName2;
        const bool h111 = esx(emu, 0xB0, rn);
        wcheck("HFW-111",
               "$B0 F_RENAME takes the source in IX and the destination in DE (swapped, the "
               "row fails: bb.txt does not exist)",
               h111 && !cy(rn) && fs::exists(droot / "bb.txt") && !fs::exists(droot / "b.txt"));

        Z80Registers mk{};
        poke_str(emu, kName, "newd");
        mk.IX = kName;
        const bool h112a = esx(emu, 0xAA, mk) && !cy(mk) && fs::is_directory(droot / "newd");
        Z80Registers rm{};
        rm.IX = kName;
        const bool h112b = esx(emu, 0xAB, rm) && !cy(rm) && !fs::exists(droot / "newd");
        wcheck("HFW-112",
               "$AA F_MKDIR and $AB F_RMDIR take the path in IX",
               h112a && h112b);

        Z80Registers tr{};
        poke_str(emu, kName, "big.bin");
        tr.IX = kName;
        tr.BC = 0x0001;           // BCDE = $00012345
        tr.DE = 0x2345;
        const bool h113 = esx(emu, 0xAE, tr);
        wcheck("HFW-113",
               "$AE F_TRUNCATE: size in BCDE with B the HIGH byte ($00012345 = 74565 bytes)",
               h113 && !cy(tr) && fs::file_size(droot / "big.bin") == 74565);

        // F_FTRUNCATE: A=handle, BCDE. Handle 1 is the in-memory stub file: not ours.
        Z80Registers op{};
        poke_str(emu, kName, "big.bin");
        op.IX = kName;
        op.BC = static_cast<uint16_t>((H::kModeRead | H::kModeWrite) << 8);
        esx(emu, 0x9A, op);
        const uint8_t fh = rega(op);
        Z80Registers ft{};
        ft.AF = static_cast<uint16_t>(fh << 8);
        ft.BC = 0;
        ft.DE = 40;
        const bool h114a = esx(emu, 0xA2, ft) && !cy(ft) && fs::file_size(droot / "big.bin") == 40;
        Z80Registers ft1{};
        ft1.AF = static_cast<uint16_t>(1 << 8);
        ft1.BC = 0;
        ft1.DE = 5;
        const bool claimed1 = esx(emu, 0xA2, ft1);   // not handled at all: another back end's
        Z80Registers cl{};
        cl.AF = static_cast<uint16_t>(fh << 8);
        esx(emu, 0x9B, cl);
        wcheck("HFW-114",
               "$A2 F_FTRUNCATE: A=handle, BCDE=size; handle 1 (the in-memory file) is not "
               "claimed and the host file is untouched",
               h114a && !claimed1 && fs::file_size(droot / "big.bin") == 40);

        Z80Registers ch{};
        poke_str(emu, kName, "bb.txt");
        ch.IX = kName;
        ch.BC = 0x0001;           // B=values $00, C=mask $01: clear A_WRITE
        const bool h115a = esx(emu, 0xAF, ch) && !cy(ch);
        const bool ro_now = !host_writable(droot / "bb.txt");
        Z80Registers ch2{};
        ch2.IX = kName;
        ch2.BC = 0x0100;          // swapped: B=$01 values, C=$00 mask: changes nothing
        const bool h115b = esx(emu, 0xAF, ch2) && !cy(ch2);
        wcheck("HFW-115",
               "$AF F_CHMOD: B=values, C=mask (B=$00 C=$01 makes it read-only; the swapped "
               "B=$01 C=$00 changes nothing)",
               h115a && ro_now && h115b && !host_writable(droot / "bb.txt"));

        Z80Registers bad{};
        poke_str(emu, kName, "missing.txt");
        bad.IX = kName;
        const bool h116 = esx(emu, 0xAD, bad);
        wcheck("HFW-116",
               "a refusal is Fc=1 with the code in A (F_UNLINK of a missing file: esx_enoent)",
               h116 && cy(bad) && rega(bad) == H::kEnoent, "A=" + hx(rega(bad)));

        // Without a root none of them is claimed (a bare --esxdos-stub, no NEX).
        EmulatorConfig bare;
        bare.type = MachineType::ZXN_ISSUE2;
        bare.esxdos_stub = true;
        bare.rewind_buffer_frames = 0;
        Emulator be;
        be.init(bare);
        bool any = false;
        for (uint8_t code : {0xAD, 0xB0, 0xAA, 0xAB, 0xAE, 0xA2, 0xAF}) {
            Z80Registers z{};
            z.IX = kName;
            z.DE = kName2;
            any = any || esx(be, code, z);
        }
        wcheck("HFW-117",
               "without a root none of the seven calls is claimed (a bare --esxdos-stub falls "
               "through to $0008)",
               !any);
    }

    // ══ Rewind: a host change is not undone ═════════════════════════════════
    {
        const fs::path rroot = base / "rew";
        fs::create_directories(rroot);
        put(rroot / "a.txt", "0123456789");
        Emulator emu;
        emu.init(make_cfg(rroot, true));
        Z80Registers op{};
        poke_str(emu, kName, "a.txt");
        op.IX = kName;
        op.BC = static_cast<uint16_t>(H::kModeRead << 8);
        esx(emu, 0x9A, op);
        const uint8_t h = rega(op);
        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buffer(measure.position());
        StateWriter w(buffer.data(), buffer.size());
        emu.save_state(w);
        Z80Registers cl{};
        cl.AF = static_cast<uint16_t>(h << 8);
        esx(emu, 0x9B, cl);
        Z80Registers ul{};
        ul.IX = kName;
        esx(emu, 0xAD, ul);
        StateReader rd(buffer.data(), buffer.size());
        const bool loaded = emu.load_state(rd);
        Z80Registers rr{};
        rr.AF = static_cast<uint16_t>(h << 8);
        rr.IX = kBuf;
        rr.BC = 4;
        const bool handled = esx(emu, 0x9D, rr);
        wcheck("HFW-120",
               "rewind: restoring a snapshot taken before an F_UNLINK does not bring the file "
               "back, and its handle is not reopened (F_READ is esx_ebadf)",
               loaded && !fs::exists(rroot / "a.txt") && handled && cy(rr) &&
                   rega(rr) == H::kEbadf, "A=" + hx(rega(rr)));

        put(rroot / "c.txt", "abcdefghij");
        Z80Registers op2{};
        poke_str(emu, kName, "c.txt");
        op2.IX = kName;
        op2.BC = static_cast<uint16_t>(H::kModeRead << 8);
        esx(emu, 0x9A, op2);
        const uint8_t h2 = rega(op2);
        StateWriter m2;
        emu.save_state(m2);
        std::vector<uint8_t> buf2(m2.position());
        StateWriter w2(buf2.data(), buf2.size());
        emu.save_state(w2);
        Z80Registers cl2{};
        cl2.AF = static_cast<uint16_t>(h2 << 8);
        esx(emu, 0x9B, cl2);
        Z80Registers rnm{};
        poke_str(emu, kName2, "d.txt");
        rnm.IX = kName;
        rnm.DE = kName2;
        esx(emu, 0xB0, rnm);
        StateReader rd2(buf2.data(), buf2.size());
        const bool loaded2 = emu.load_state(rd2);
        Z80Registers r2{};
        r2.AF = static_cast<uint16_t>(h2 << 8);
        r2.IX = kBuf;
        r2.BC = 4;
        const bool handled2 = esx(emu, 0x9D, r2);
        wcheck("HFW-121",
               "rewind: restoring a snapshot taken before an F_RENAME leaves the rename, and "
               "the handle to the old path is not reopened (F_READ is esx_ebadf)",
               loaded2 && fs::exists(rroot / "d.txt") && !fs::exists(rroot / "c.txt") &&
                   handled2 && cy(r2) && rega(r2) == H::kEbadf, "A=" + hx(rega(r2)));
    }

    // ══ M_GETERR ($93) ═════════════════════════════════════════════════════
    {
        const fs::path groot = base / "gerr";
        fs::create_directories(groot);
        Emulator emu;
        emu.init(make_cfg(groot, false));
        auto geterr = [&](uint8_t code, uint8_t b, uint16_t de, Z80Registers& r) {
            r = Z80Registers{};
            r.AF = static_cast<uint16_t>(code << 8);
            r.BC = static_cast<uint16_t>(b << 8);
            r.DE = de;
            return esx(emu, 0x93, r);
        };
        auto text = [&](uint16_t at, bool& terminated) {
            std::string s;
            terminated = false;
            for (int i = 0; i < 32; ++i) {
                const uint8_t c = emu.mmu().read(static_cast<uint16_t>(at + i));
                s.push_back(static_cast<char>(c & 0x7F));
                if (c & 0x80) { terminated = true; break; }
            }
            return s;
        };
        Z80Registers r{};
        bool t = false;
        const bool h1 = geterr(5, 1, kBuf, r);
        const std::string m5 = text(kBuf, t);
        wcheck("GETERR-01",
               "M_GETERR B=1 A=5 writes \"No such file or dir\" to DE with bit 7 on its last "
               "byte; Fc=0, A=$02 ([geterr] S03E5)",
               h1 && !cy(r) && rega(r) == 0x02 && m5 == "No such file or dir" && t);
        Z80Registers r2{};
        bool t2 = false, t3 = false;
        geterr(24, 1, kBuf, r2);
        const std::string m24 = text(kBuf, t2);
        geterr(31, 1, kBuf, r2);
        const std::string m31 = text(kBuf, t3);
        wcheck("GETERR-02",
               "codes 24 and 31, the read-only answers, give \"Read only\" and \"Device busy\" "
               "([geterr] S03F8, S03FF)",
               m24 == "Read only" && m31 == "Device busy" && t2 && t3);
        const uint8_t custom[] = {'C', 'u', 's', 't', 'o', 'm', ' ', 'm', 's', 'g' | 0x80};
        for (std::size_t i = 0; i < sizeof(custom); ++i)
            emu.mmu().write(static_cast<uint16_t>(kName + i), static_cast<uint8_t>(custom[i]));
        Z80Registers r3{};
        r3.AF = 0;
        r3.BC = 0x0100;
        r3.DE = kBuf + 0x40;
        r3.IX = kName;
        const bool h3 = esx(emu, 0x93, r3);
        bool t4 = false;
        const std::string m0 = text(kBuf + 0x40, t4);
        wcheck("GETERR-03",
               "A=0 copies the bit-7-terminated message at IX to DE ([geterr] S0405)",
               h3 && !cy(r3) && m0 == "Custom msg" && t4);
        Z80Registers r4{};
        bool t5 = false, t6 = false;
        geterr(0x20, 1, kBuf, r4);
        const std::string mu = text(kBuf, t5);
        geterr(0xFF, 1, kBuf, r4);
        const std::string mf = text(kBuf, t6);
        wcheck("GETERR-04",
               "out-of-range codes ($20, $FF) give \"Unknown error\" ([geterr] S0400, S0403)",
               mu == "Unknown error" && mf == "Unknown error" && t5 && t6);
        for (int i = 0; i < 40; ++i)
            emu.mmu().write(static_cast<uint16_t>(kBuf + 0x80 + i), 0xEE);
        Z80Registers r5{};
        geterr(24, 1, kBuf + 0x80, r5);
        bool t7 = false;
        const std::string s24 = text(kBuf + 0x80, t7);
        const uint16_t after = static_cast<uint16_t>(kBuf + 0x80 + s24.size());
        wcheck("GETERR-05",
               "nothing is written past the terminator: the bytes after \"Read only\" keep "
               "their value",
               emu.mmu().read(after) == 0xEE && emu.mmu().read(static_cast<uint16_t>(after + 5)) == 0xEE);
        Z80Registers r6{};
        const bool b0 = geterr(5, 0, kBuf, r6);
        EmulatorConfig bare;
        bare.type = MachineType::ZXN_ISSUE2;
        bare.esxdos_stub = true;
        bare.rewind_buffer_frames = 0;
        Emulator be;
        be.init(bare);
        Z80Registers r7{};
        r7.AF = static_cast<uint16_t>(5 << 8);
        r7.BC = 0x0100;
        r7.DE = kBuf;
        const bool bare_handled = esx(be, 0x93, r7);
        // An unterminated custom message (no bit 7 within 32 bytes) copies 32 bytes and
        // not one more: the buffer is 32 bytes.
        for (int i = 0; i < 40; ++i) {
            emu.mmu().write(static_cast<uint16_t>(kName + i), 'A');
            emu.mmu().write(static_cast<uint16_t>(kBuf + 0x100 + i), 0xEE);
        }
        Z80Registers r8{};
        r8.AF = 0;
        r8.BC = 0x0100;
        r8.DE = kBuf + 0x100;
        r8.IX = kName;
        const bool h8 = esx(emu, 0x93, r8);
        wcheck("GETERR-07",
               "an unterminated custom message copies 32 bytes and not one past the buffer",
               h8 && emu.mmu().read(kBuf + 0x100 + 31) == 'A' &&
                   emu.mmu().read(kBuf + 0x100 + 32) == 0xEE &&
                   emu.mmu().read(kBuf + 0x100 + 39) == 0xEE);
        wcheck("GETERR-06",
               "served with a host root; B=0 (a BASIC report on a Next, which does not "
               "return) gets the default refusal, not a message; not claimed by a bare "
               "--esxdos-stub with no root, so NextZXOS still answers it there",
               h1 && !b0 && !bare_handled);
    }

    std::error_code cleanup;
    // Windows will not delete a read-only file: clear the bit first.
    for (const auto& e : fs::recursive_directory_iterator(base, cleanup)) {
        std::error_code pe;
        if (!e.is_directory())
            fs::permissions(e.path(), fs::perms::owner_write, fs::perm_options::add, pe);
    }
    fs::remove_all(base, cleanup);
}
