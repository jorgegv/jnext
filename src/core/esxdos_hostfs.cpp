#include "core/esxdos_hostfs.h"



#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <system_error>

namespace fs = std::filesystem;

namespace {

/// The 8.3 character set FAT allows, beyond A-Z and 0-9. Anything else becomes
/// '_' (FAT's own long-name-to-short-name rule). Lower case is folded up
/// because a short name has no case.
bool short_name_char_ok(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) return true;
    return std::strchr("$%'-_@~`!(){}^#&", c) != nullptr;
}

/// Device names that are not file names on Windows, whatever the extension.
/// Refused on EVERY host so a directory that works on Linux does not become a
/// device write on Windows — and so the refusal is testable on this host.
bool reserved_device_name(const std::string& part) {
    static const char* kNames[] = {"CON", "PRN", "AUX", "NUL"};
    std::string stem = part.substr(0, part.find('.'));
    for (char& c : stem) c = static_cast<char>(std::toupper(
        static_cast<unsigned char>(c)));
    for (const char* n : kNames)
        if (stem == n) return true;
    if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 ||
                             stem.compare(0, 3, "LPT") == 0) &&
        stem[3] >= '1' && stem[3] <= '9')
        return true;
    return false;
}

bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

/// True when `name` already IS a valid 8.3 name, so no `~N` tail is needed.
bool fits_8_3(const std::string& name) {
    if (name.empty() || name.size() > 12) return false;
    const std::size_t dot = name.rfind('.');
    const std::string base = dot == std::string::npos ? name : name.substr(0, dot);
    const std::string ext  = dot == std::string::npos ? std::string()
                                                      : name.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3) return false;
    if (ext.find('.') != std::string::npos) return false;
    for (char c : base) if (!short_name_char_ok(c)) return false;
    for (char c : ext)  if (!short_name_char_ok(c)) return false;
    return true;
}

bool is_wild(const std::string& s) {
    return s.find_first_of("*?") != std::string::npos;
}

/// A wildcard anywhere in a guest path, its drive qualifier ('*:' is the
/// current drive, not a wildcard) excepted.
bool path_has_wild(const std::string& p) {
    std::size_t b = 0;
    if (p.size() >= 2 && p[1] == ':' &&
        (p[0] == '*' || p[0] == '$' || std::isalpha(static_cast<unsigned char>(p[0]))))
        b = 2;
    return is_wild(p.substr(b));
}

/// A guest path split into the part up to and including its last separator (or
/// drive colon) and the final component.
struct Leaf {
    std::string prefix;
    std::string leaf;
    std::size_t drive = 0;   // length of the drive qualifier ("C:") at the start of prefix
};

Leaf split_leaf(const std::string& p) {
    std::size_t b = 0;
    if (p.size() >= 2 && p[1] == ':' &&
        (p[0] == '*' || p[0] == '$' || std::isalpha(static_cast<unsigned char>(p[0]))))
        b = 2;
    const std::size_t cut = p.find_last_of("/\\");
    const std::size_t at = (cut == std::string::npos || cut < b) ? b : cut + 1;
    return Leaf{p.substr(0, at), p.substr(at), b};
}

/// Characters FAT does not allow in a name, refused by NextZXOS with esx_einval
/// in EVERY call (F_STAT, F_OPEN, F_MKDIR, F_RENAME: measured). '*' and '?' are
/// wildcards and are handled per call; ':' is refused in resolve().
bool fat_forbidden(char c) { return c == '"' || c == '<' || c == '>' || c == '|'; }

bool half_match(const std::string& p, const std::string& n) {
    std::size_t j = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const char c = p[i];
        if (c == '*' && i + 1 == p.size()) return true;
        if (c == '?') {
            if (j < n.size()) ++j;
            continue;
        }
        if (j >= n.size() ||
            std::toupper(static_cast<unsigned char>(c)) !=
                std::toupper(static_cast<unsigned char>(n[j])))
            return false;
        ++j;
    }
    return j == n.size();
}

/// resolve()'s kBareDotDot is a bare '..' above the root. NextZXOS answers it as it
/// does any missing last component, which for a call that looks a name up is
/// esx_enoent (MEASURED, GH #457).
uint8_t lookup_miss(uint8_t err) { return err == EsxdosHostFs::kBareDotDot ? EsxdosHostFs::kEnoent : err; }

/// A name a call that CREATES one can give an entry: no wildcard characters
/// (NextZXOS answers esx_einval, measured), and no trailing space (NextZXOS keeps
/// it, but a host may strip it, so a name that works on Linux would reach a
/// different entry on Windows: refused on every host, a declared divergence).
bool valid_new_name(const fs::path& host) {
    const std::string n = host.filename().string();
    return !is_wild(n) && (n.empty() || n.back() != ' ');
}

}  // namespace

bool EsxdosHostFs::list_host_dir(const fs::path& dir, std::vector<Listed>& out) const
{
    out.clear();
    std::error_code ec;
    std::vector<fs::directory_entry> raw;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec))
        raw.push_back(*it);
    if (ec) return false;
    std::sort(raw.begin(), raw.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return a.path().filename().string() <
                         b.path().filename().string();
              });

    for (const fs::directory_entry& de : raw) {
        std::error_code ec2;
        const fs::file_status dst = fs::symlink_status(de.path(), ec2);
        if (ec2) continue;
        // A symlink is not traversed, so it is not listed either: listing it
        // would advertise a name that every other call refuses.
        if (fs::is_symlink(dst)) continue;
        const bool is_dir = fs::is_directory(dst);
        if (!is_dir && !fs::is_regular_file(dst)) continue;
        Listed l;
        l.path = de.path();
        l.lfn = de.path().filename().string();
        l.is_dir = is_dir;
        out.push_back(std::move(l));
    }

    // Aliases. A name that already IS an 8.3 name keeps itself (uppercased) and
    // always wins; the others keep the alias they were given, or get the lowest
    // free `~N` (the first listing walks the sorted order, so it numbers exactly
    // as a fresh FAT volume filled in that order would).
    std::map<std::string, std::string>& table = aliases_[dir.string()];
    std::vector<std::string> used;
    auto taken = [&used](const std::string& x) {
        return std::find(used.begin(), used.end(), x) != used.end();
    };
    std::vector<bool> aliased(out.size(), false);
    for (std::size_t i = 0; i < out.size(); ++i) {
        Listed& l = out[i];
        const std::string sfn = EsxdosHostFs::short_name(l.lfn, 0);
        if (fits_8_3(l.lfn) && !taken(sfn)) {
            l.sfn = sfn;
            used.push_back(sfn);
        } else {
            aliased[i] = true;
        }
    }
    for (auto it = table.begin(); it != table.end();) {      // forget the vanished
        bool there = false;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (aliased[i] && out[i].lfn == it->first) { there = true; break; }
        it = there ? std::next(it) : table.erase(it);
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!aliased[i]) continue;
        Listed& l = out[i];
        const auto have = table.find(l.lfn);
        if (have != table.end() && !taken(have->second)) {
            l.sfn = have->second;
        } else {
            unsigned n = 1;
            std::string sfn;
            do { sfn = EsxdosHostFs::short_name(l.lfn, n++); }
            while (taken(sfn) && n < 1000);
            l.sfn = sfn;
            table[l.lfn] = sfn;
        }
        used.push_back(l.sfn);
    }
    return true;
}

// ── Configuration ────────────────────────────────────────────────────────

bool EsxdosHostFs::validate_root(const std::string& root, std::string& error,
                                 fs::path* canonical)
{
    if (root.empty()) { error = "empty directory name"; return false; }
    std::error_code ec;
    const fs::path resolved = fs::canonical(fs::path(root), ec);
    if (ec) {
        error = "cannot resolve '" + root + "': " + ec.message();
        return false;
    }
    if (!fs::is_directory(resolved, ec) || ec) {
        error = "'" + root + "' is not a directory";
        return false;
    }
    if (canonical) *canonical = resolved;
    return true;
}

void EsxdosHostFs::clear()
{
    for (auto& f : files_) { if (f.stream.is_open()) f.stream.close(); f = FileHandle{}; }
    for (auto& d : dirs_)  d = DirHandle{};
    cwd_.clear();
    aliases_.clear();
    active_ = false;
    writable_ = false;
    root_.clear();
}

bool EsxdosHostFs::configure(const std::string& root, bool writable,
                             std::string& error)
{
    clear();

    fs::path canonical;
    if (!validate_root(root, error, &canonical)) return false;
    root_ = canonical;
    writable_ = writable;
    active_ = true;
    return true;
}

// ── Path confinement ─────────────────────────────────────────────────────
//
// WHAT THIS CLOSES, AND WHAT IT DOES NOT.
//
// The primary containment is LEXICAL and cannot be defeated at all, because it
// never consults the host filesystem: the guest path is split into components,
// `.` is dropped, `..` pops, and a pop that would leave the root is refused
// outright. A path that escapes is therefore never even constructed, so there
// is nothing for a race to act on. A guest ABSOLUTE path is absolute WITHIN the
// root — `/etc/passwd` means `<root>/etc/passwd`, never the host's.
//
// Symlinks are the part that does need the filesystem. Every component that
// exists is checked with symlink_status() and refused if it is a link, so a
// link planted in the served directory before the call cannot be traversed.
// The resolved path is then canonicalised and re-checked against the canonical
// root, and — for opens — checked AGAIN immediately after the host open.
//
// RESIDUAL, STATED HONESTLY: a symlink created between the component check and
// the open is not fully excluded on this design. Closing it completely means an
// `openat(O_NOFOLLOW)` component walk, which has no portable form across the
// Linux, macOS and MinGW builds jnext ships. The double check makes the attack
// require winning a race in both directions, and the attacker must already have
// write access to the directory the USER chose to serve — jnext is not a
// privileged service and runs as the user whose files these are. This is an
// accepted bound, not an oversight.
//
// WHAT THE TESTS CAN AND CANNOT PIN — read this before "simplifying" a check.
//
// The symlink refusals are deliberately REDUNDANT, and redundancy is exactly
// what makes an individual one impossible to pin behaviourally: neutralise any
// single `symlink_status()` here and the observable answer does not change,
// because another layer refuses the same input. A reviewer measured that
// (GH #31): all seven neutralised at once still gave 147/147 green, and only a
// coordinated two-site swap reddens a row. So there is NO row that proves any
// one of these calls is load-bearing, and none is claimed to.
//
// What the rows do pin, per call surface, is that the surface has not lost ALL
// of its protection:
//
//   * the escaping case (a link whose target is outside the root) is caught by
//     contained() below whatever any stat believed, because weakly_canonical()
//     resolves links — that is the real backstop, and HFS-10/HFS-11 pin it;
//   * the policy case (a link whose target is INSIDE the root) has no backstop
//     — containment cannot object to it — so it is pinned once per call:
//     HFS-12 for stat(), HFS-90 for open(), HFS-91 for opendir(), HFS-92 for
//     chdir(). Those are the rows that go red if a call surface stops refusing
//     links altogether.
//
// One measured subtlety, so nobody re-derives it from scratch. open() and
// stat() carry a THIRD, implicit refusal: they ask symlink_status(), which
// never reports a link as a regular file, so their is_regular_file/type gate
// rejects one even with the explicit check deleted. Deleting that check alone
// therefore changes nothing observable, and the mutation that reddens HFS-90
// and HFS-12 is the realistic mistake — swapping symlink_status() for
// status(), so the link looks like its target. opendir() and chdir() have no
// such implicit gate (a link to a directory IS a directory through status()),
// so deleting their explicit check alone does redden HFS-91 and HFS-92.
//
// The consequence to accept: a single-site regression is invisible to the test
// suite BY CONSTRUCTION, and the system still refuses the input. If you delete
// a check here because "no test covers it", you are removing a layer, not dead
// code — and the per-call rows above will keep passing until the last one goes.

bool EsxdosHostFs::contained(const fs::path& p) const
{
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    if (ec) return false;
    auto m = std::mismatch(root_.begin(), root_.end(), c.begin(), c.end());
    return m.first == root_.end();
}

uint8_t EsxdosHostFs::resolve(const std::string& guest_path, fs::path& out,
                              std::vector<std::string>* components) const
{
    if (!active_) return kEnoent;
    if (guest_path.empty()) return kEinval;
    if (guest_path.size() > kMaxPath) return kEinval;
    if (guest_path.find('\0') != std::string::npos) return kEinval;

    std::string s = guest_path;

    // Drive qualifier. esx_drive_current '*' and esx_drive_system '$'
    // (esxapi.def:129-130), plus the `c:/...` form NextZXOS itself writes.
    // ALL of them mean "inside the root" here — there is one volume.
    if (s.size() >= 2 && s[1] == ':' &&
        (s[0] == '*' || s[0] == '$' ||
         std::isalpha(static_cast<unsigned char>(s[0]))))
        s = s.substr(2);
    if (s.empty()) s = ".";

    // BOTH '/' and '\\' separate components, on every host.
    //
    // The guest is writing FAT paths, and FAT forbids '\\' inside a name while
    // accepting it as a separator, so a program that sends one means a
    // separator. Honouring that is also what keeps this routine
    // PLATFORM-INDEPENDENT: if '\\' were an ordinary character, then
    // "a\\..\\..\\etc\\passwd" would stay one component here and be re-split by
    // std::filesystem::path on a Windows build but not on a POSIX one, so the
    // lexical walk below — the part that cannot be defeated — would enforce
    // different things on different platforms and only the canonical
    // back-stop would agree. The cost is that a host file whose name really
    // contains a backslash is unreachable; FAT cannot name such a file, so no
    // FAT-expecting guest can ask for one.
    auto is_sep = [](char c) { return c == '/' || c == '\\'; };
    const bool absolute = is_sep(s.front());
    std::vector<std::string> comps = absolute ? std::vector<std::string>{} : cwd_;

    std::vector<std::string> parts;
    for (std::size_t i = 0; i <= s.size();) {
        std::size_t next = std::string::npos;
        for (std::size_t j = i; j < s.size(); ++j)
            if (is_sep(s[j])) { next = j; break; }
        const std::string part =
            s.substr(i, next == std::string::npos ? std::string::npos : next - i);
        i = (next == std::string::npos) ? s.size() + 1 : next + 1;
        if (!part.empty() && part != ".") parts.push_back(part);
    }

    for (std::size_t k = 0; k < parts.size(); ++k) {
        std::string part = parts[k];
        if (part == "..") {
            // Above the root. NextZXOS treats it as a name that is not in the
            // root directory (MEASURED, GH #457): esx_enotdir when more path
            // follows, and for a bare '..' what it answers for any missing last
            // component, which depends on the call. The first is answered here;
            // the second is reported as kBareDotDot and each call maps it (see
            // lookup_miss() and the per-call maps). The escaping path is never
            // formed either way.
            if (comps.empty()) return k + 1 < parts.size() ? kEnotdir : kBareDotDot;
            comps.pop_back();
            continue;
        }
        // FAT drops trailing dots from a name: "dot." and "dot" are the same
        // entry (MEASURED: F_MKDIR "dot." then F_STAT of both finds it).
        while (!part.empty() && part.back() == '.') part.pop_back();
        if (part.empty()) return kEinval;
        if (part.size() > 255) return kEinval;
        if (reserved_device_name(part)) return kEacces;
        // A ':' inside a component (the drive qualifier was stripped above)
        // cannot be part of a FAT name, and NextZXOS refuses it with esx_einval
        // ($07): MEASURED (GH #214) by running a probe .nex under the real
        // NextZXOS booted from the distro 24.11 SD image, `dot/a:x` -> A=$07 with
        // carry set from F_STAT, F_OPENDIR, F_CHDIR, F_OPEN (read) and F_OPEN
        // (create), against $05 for the missing `dot/nosuch`. Answered here, the
        // same on every host: left to the host walk a Linux directory simply
        // lacks the entry ($05 for a last component, $11 for one before it), and
        // Windows reads "a:x" as a drive-relative path, `root / "a:x"` REPLACES
        // the root, and contained() then refused it with esx_eacces.
        if (part.find(':') != std::string::npos) return kEinval;
        // '"', '<', '>' and '|' get the same answer in every call, F_STAT
        // included (MEASURED, GH #457).
        for (char c : part)
            if (fat_forbidden(c)) return kEinval;
        comps.push_back(part);
    }

    // Walk the host tree, matching each component case-insensitively (FAT is
    // case-insensitive; Linux is not). An exact match always wins; otherwise
    // the lexicographically smallest case-insensitive match is taken, so the
    // answer is the same on every run and on every host. Failing both, the
    // component may be an entry's 8.3 name (GH #301): FAT answers to either
    // name, and F_GETCWD hands the guest 8.3 names to build paths from.
    //
    // `actual` collects the components as they are spelled on the HOST, which
    // is what chdir() stores.
    //
    // Only the LAST component may be missing (F_OPEN can create it). Every
    // one before it must be an existing directory, or the answer is
    // esx_enotdir — MEASURED under NextZXOS (GH #301): "nonexist/x.txt" and
    // "T/README/x" (through a FILE) both get $11 from F_OPEN, F_STAT,
    // F_OPENDIR and F_CHDIR, and so does a create in a missing directory.
    //
    // A name counts as "exact" only when the directory LISTING holds it byte
    // for byte, not when the host merely answers to it (GH #457): a
    // case-insensitive host (Windows, macOS) answers to "SUB" for "sub", and an
    // NTFS volume to its own 8.3 alias of a long name, numbered by its own
    // creation order and not by this listing. Taking those would store the
    // guest's spelling (which format_path() can then no longer find) or reach a
    // different file than the one the listing shows under that name, and with
    // F_UNLINK that file is destroyed. A name the host answers to that the
    // listing does not know is refused.
    fs::path p = root_;
    std::vector<std::string> actual;
    for (const std::string& want : comps) {
        std::error_code dec;
        if (!fs::is_directory(fs::symlink_status(p, dec)) || dec) return kEnotdir;

        std::error_code ec;
        const fs::path exact = p / want;
        const fs::file_status st = fs::symlink_status(exact, ec);
        const bool host_has = !ec && fs::exists(st);
        if (host_has && fs::is_symlink(st)) return kEacces;

        std::vector<Listed> listed;
        const bool have_list = list_host_dir(p, listed);
        std::string best;
        if (have_list) {
            for (const Listed& l : listed)
                if (l.lfn == want) { best = l.lfn; break; }
            if (best.empty())
                for (const Listed& l : listed) {
                    if (!iequal(l.lfn, want)) continue;
                    if (best.empty() || l.lfn < best) best = l.lfn;
                }
            // The short names are unique within a listing, so at most one
            // entry can answer. list_host_dir() lists no symlink, so an 8.3
            // name never reaches one.
            if (best.empty())
                for (const Listed& l : listed)
                    if (iequal(l.sfn, want)) { best = l.lfn; break; }
        }
        if (!best.empty()) {
            const fs::path picked = p / best;
            std::error_code ec2;
            if (fs::is_symlink(fs::symlink_status(picked, ec2))) return kEacces;
            p = picked;
            actual.push_back(best);
            continue;
        }
        if (host_has) return kEacces;     // a host-only alias: see above
        // Missing. Fine as the last component; anything after it fails the
        // is-a-directory check at the top of the next pass.
        p /= want;
        actual.push_back(want);
    }

    if (!contained(p)) return kEacces;
    out = p;
    if (components) *components = actual;
    return kOk;
}

// ── 8.3 synthesis and timestamps ─────────────────────────────────────────

std::string EsxdosHostFs::short_name(const std::string& name, unsigned ordinal)
{
    if (name == "." || name == "..") return name;

    // FAT's basis-name rule strips LEADING periods before the name is split
    // into base and extension, so a leading dot never starts an extension.
    // Measured (GH #301): the card's own short names, as NextZXOS lists them,
    // are .hidden -> HIDDEN~1, .gitignore -> GITIGN~1, .git -> GIT~1,
    // .a.b -> A~1.B, ..dd -> DD~1, .x -> X~1. Without this, ".hidden" split as
    // base "" / ext "hidden" and came out "~1.HID".
    const std::string bare = name.substr(
        std::min(name.find_first_not_of('.'), name.size()));
    const std::size_t dot = bare.rfind('.');
    const std::string raw_base = dot == std::string::npos ? bare
                                                          : bare.substr(0, dot);
    const std::string raw_ext  = dot == std::string::npos ? std::string()
                                                          : bare.substr(dot + 1);

    auto filter = [](const std::string& in, std::size_t limit) {
        std::string out;
        for (char c : in) {
            if (c == ' ' || c == '.') continue;      // FAT drops both
            out.push_back(short_name_char_ok(c)
                              ? static_cast<char>(std::toupper(
                                    static_cast<unsigned char>(c)))
                              : '_');
            if (out.size() == limit) break;
        }
        return out;
    };

    std::string ext = filter(raw_ext, 3);
    std::string base;
    if (ordinal == 0) {
        base = filter(raw_base, 8);
    } else {
        const std::string tail = "~" + std::to_string(ordinal);
        const std::size_t room = tail.size() >= 8 ? 1 : 8 - tail.size();
        base = filter(raw_base, room) + tail;
    }
    if (base.empty()) base = ordinal == 0 ? "_" : "_~" + std::to_string(ordinal);
    return ext.empty() ? base : base + "." + ext;
}

void EsxdosHostFs::dos_timestamp(fs::file_time_type mtime, uint16_t& date,
                                 uint16_t& time)
{
    // C++17 has no portable file_clock -> system_clock cast (that is C++20's
    // std::chrono::clock_cast), so use the documented offset idiom.
    const auto sctp = std::chrono::time_point_cast<
        std::chrono::system_clock::duration>(
        mtime - fs::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    // ROUND to the nearest second, do not truncate. The idiom above samples
    // two clocks at slightly different instants, so an mtime that is exactly
    // 08:55:04 arrives as 08:55:03.999999 about half the time — and to_time_t
    // truncates, which would report 08:55:03 on those runs and 08:55:04 on the
    // others. The sub-second part carries no information here (FAT's own
    // resolution is two seconds), so rounding removes the wobble instead of
    // preserving a value that was never meaningful.
    std::time_t when = std::chrono::system_clock::to_time_t(
        sctp + std::chrono::milliseconds(500));

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &when);
#else
    localtime_r(&when, &tm);
#endif

    // MS-DOS packing. The epoch is 1980, and the year field is 7 bits, so
    // anything outside 1980..2107 is clamped rather than wrapped into a
    // plausible-looking wrong date.
    int year = tm.tm_year + 1900;
    if (year < 1980) year = 1980;
    if (year > 2107) year = 2107;
    date = static_cast<uint16_t>(((year - 1980) << 9) |
                                 ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    time = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) |
                                 (tm.tm_sec / 2));
}

// ── Handle slots ─────────────────────────────────────────────────────────

EsxdosHostFs::FileHandle* EsxdosHostFs::file_slot(uint8_t handle)
{
    if (handle < kFirstFileHandle || handle >= kFirstFileHandle + kFileHandles)
        return nullptr;
    return &files_[handle - kFirstFileHandle];
}

const EsxdosHostFs::FileHandle* EsxdosHostFs::file_slot(uint8_t handle) const
{
    if (handle < kFirstFileHandle || handle >= kFirstFileHandle + kFileHandles)
        return nullptr;
    return &files_[handle - kFirstFileHandle];
}

EsxdosHostFs::DirHandle* EsxdosHostFs::dir_slot(uint8_t handle)
{
    if (handle < kFirstDirHandle || handle >= kFirstDirHandle + kDirHandles)
        return nullptr;
    return &dirs_[handle - kFirstDirHandle];
}

const EsxdosHostFs::DirHandle* EsxdosHostFs::dir_slot(uint8_t handle) const
{
    if (handle < kFirstDirHandle || handle >= kFirstDirHandle + kDirHandles)
        return nullptr;
    return &dirs_[handle - kFirstDirHandle];
}

// ── File calls ───────────────────────────────────────────────────────────

uint8_t EsxdosHostFs::open(const std::string& guest_path, uint8_t mode,
                           uint8_t& handle)
{
    if (!active_) return kEnoent;

    // +3DOS header read/write (esx_mode_use_header). A host file has no
    // header, and inventing one would make the guest load 8 bytes of fiction
    // as a load address. Refused, not faked.
    if (mode & kModeUseHeader) return kEnosys;
    if ((mode & (kModeRead | kModeWrite)) == 0) return kEinval;

    const bool want_write = (mode & kModeWrite) != 0;
    const uint8_t creat = mode & kModeCreatMask;
    if (!writable_ && (want_write || creat != 0)) return kErdonly;

    fs::path host;
    if (uint8_t err = resolve(guest_path, host)) {
        // A bare '..' above the root: a create is an invalid name for NextZXOS
        // (esx_einval) and a plain open a missing one (esx_enoent): measured.
        if (err == kBareDotDot) err = creat != 0 ? kEinval : kEnoent;
        return err;
    }

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    const bool exists = !ec && fs::exists(st);
    // A name a create cannot make: wildcard characters (esx_einval, measured)
    // and a trailing space, which NextZXOS keeps and a host may not.
    if (!exists && creat != 0 && !valid_new_name(host)) return kEinval;
    if (exists && fs::is_symlink(st)) return kEacces;
    // A directory opened as a file is esx_einval — measured under NextZXOS
    // (GH #301), which answers F_OPEN and F_STAT of a directory both that way.
    if (exists && fs::is_directory(st)) return kEinval;
    if (exists && !fs::is_regular_file(st)) return kEacces;

    switch (creat) {
        case 0x00:  if (!exists) return kEnoent; break;           // open_exist
        case kModeCreatNoExist: if (exists) return kEexist; break;
        case kModeOpenCreat:    break;
        case kModeCreatTrunc:   break;
        default: return kEinval;
    }
    if (!exists && creat == 0x00) return kEnoent;
    // A read-only FILE refuses every mode that writes, truncates or may create
    // over it (MEASURED, GH #457: $02 $03 $08 $0C $0D $0E -> esx_erdonly; $04
    // answered esx_eexist above first; $01 opens). Decided from the file's
    // permission bits, never from what the OS would let a write do.
    if (exists && host_read_only(host) &&
        (want_write || creat == kModeOpenCreat || creat == kModeCreatTrunc))
        return kErdonly;

    int slot = -1;
    for (int i = 0; i < kFileHandles; ++i)
        if (!files_[i].open) { slot = i; break; }
    if (slot < 0) return kEnfile;

    FileHandle& f = files_[slot];
    std::ios::openmode om = std::ios::binary | std::ios::in;
    if (want_write || creat != 0) om |= std::ios::out;

    if (!exists && (creat == kModeCreatNoExist || creat == kModeOpenCreat ||
                    creat == kModeCreatTrunc)) {
        // fstream with in|out will not create the file; make it first.
        std::ofstream make(host, std::ios::binary);
        if (!make) return kEio;
    } else if (exists && creat == kModeCreatTrunc) {
        std::ofstream trunc(host, std::ios::binary | std::ios::trunc);
        if (!trunc) return kEio;
    }

    f.stream.open(host, om);
    if (!f.stream.is_open()) return kEacces;

    // Second containment check, AFTER the open (see the comment block above).
    if (!contained(host)) { f.stream.close(); return kEacces; }

    f.open = true;
    f.writable = want_write;
    f.path = host.string();
    f.position = 0;
    handle = static_cast<uint8_t>(kFirstFileHandle + slot);
    return kOk;
}

uint8_t EsxdosHostFs::close(uint8_t handle)
{
    if (FileHandle* f = file_slot(handle)) {
        if (!f->open) return kEbadf;
        f->stream.close();
        f->open = false;
        f->path.clear();
        f->position = 0;
        return kOk;
    }
    if (DirHandle* d = dir_slot(handle)) {   // F_CLOSE also closes directories
        if (!d->open) return kEbadf;
        *d = DirHandle{};
        return kOk;
    }
    return kEbadf;
}

uint8_t EsxdosHostFs::read(uint8_t handle, std::size_t want,
                           std::vector<uint8_t>& out)
{
    FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;
    out.assign(want, 0);
    if (want == 0) return kOk;
    f->stream.clear();
    f->stream.seekg(static_cast<std::streamoff>(f->position), std::ios::beg);
    if (!f->stream) { f->stream.clear(); out.clear(); return kEio; }
    f->stream.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(want));
    const std::streamsize got = f->stream.gcount();
    f->stream.clear();       // EOF short read is not an error (F_READ notes)
    out.resize(static_cast<std::size_t>(got));
    f->position += static_cast<uint64_t>(got);
    return kOk;
}

uint8_t EsxdosHostFs::write(uint8_t handle, const uint8_t* data,
                            std::size_t count, std::size_t& written)
{
    written = 0;
    FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;
    if (!writable_ || !f->writable) return kErdonly;
    if (count == 0) return kOk;
    f->stream.clear();
    f->stream.seekp(static_cast<std::streamoff>(f->position), std::ios::beg);
    if (!f->stream) { f->stream.clear(); return kEio; }
    f->stream.write(reinterpret_cast<const char*>(data),
                    static_cast<std::streamsize>(count));
    if (!f->stream) { f->stream.clear(); return kEio; }
    f->stream.flush();
    written = count;
    f->position += count;
    return kOk;
}

uint8_t EsxdosHostFs::seek(uint8_t handle, uint8_t whence, uint32_t distance,
                           uint32_t& position)
{
    FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;

    std::error_code ec;
    const uint64_t size = fs::file_size(fs::path(f->path), ec);
    const uint64_t have = ec ? 0 : size;

    uint64_t next;
    switch (whence) {
        case kSeekSet: next = distance; break;
        case kSeekFwd: next = f->position + distance; break;
        case kSeekBwd:
            if (distance > f->position) return kEinval;
            next = f->position - distance;
            break;
        default: return kEinval;
    }
    // Seeking past the end is how esxDOS reports a bad request here; a read at
    // that position would return nothing anyway.
    if (next > have) return kEinval;
    f->position = next;
    position = static_cast<uint32_t>(next);
    return kOk;
}

uint8_t EsxdosHostFs::fgetpos(uint8_t handle, uint32_t& position) const
{
    const FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;
    position = static_cast<uint32_t>(
        f->position > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : f->position);
    return kOk;
}

uint8_t EsxdosHostFs::fstat(uint8_t handle, StatInfo& out) const
{
    const FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;

    std::error_code ec;
    const fs::path p(f->path);
    const uint64_t size = fs::file_size(p, ec);
    out.size = ec ? 0 : static_cast<uint32_t>(
        std::min<uint64_t>(size, 0xFFFFFFFFULL));
    out.attr = kAttrArchive;
    if (!writable_ || host_read_only(p)) out.attr |= kAttrReadOnly;
    std::error_code ec2;
    const auto mtime = fs::last_write_time(p, ec2);
    if (!ec2) dos_timestamp(mtime, out.date, out.time);
    return kOk;
}

uint8_t EsxdosHostFs::stat(const std::string& guest_path, StatInfo& out)
{
    if (!active_) return kEnoent;
    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return lookup_miss(err);

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    if (fs::is_symlink(st)) return kEacces;

    // F_STAT of a DIRECTORY is esx_einval: measured under NextZXOS (GH #301),
    // where it fails exactly as F_OPEN of one does. Answering it with a
    // directory entry would tell a program developed against the host
    // directory something the Next will not.
    if (fs::is_directory(st)) return kEinval;

    out = StatInfo{};
    if (fs::is_regular_file(st)) {
        out.attr = kAttrArchive;
        std::error_code ec2;
        const uint64_t size = fs::file_size(host, ec2);
        out.size = ec2 ? 0 : static_cast<uint32_t>(
            std::min<uint64_t>(size, 0xFFFFFFFFULL));
    } else {
        return kEacces;
    }
    if (!writable_ || host_read_only(host)) out.attr |= kAttrReadOnly;
    if (!host.filename().empty() && host.filename().string().front() == '.')
        out.attr |= kAttrHidden;
    std::error_code ec3;
    const auto mtime = fs::last_write_time(host, ec3);
    if (!ec3) dos_timestamp(mtime, out.date, out.time);
    return kOk;
}

uint8_t EsxdosHostFs::sync(uint8_t handle)
{
    FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;
    f->stream.flush();
    return kOk;
}

// ── Calls that change the host folder (GH #457) ──────────────────────────
//
// Every answer below is NextZXOS 2.09's, MEASURED by the kept probe
// (test/00regression/esxdos-write-probe.py, recorded in test/fixtures/
// esxdos-write-probe/nextzxos.txt). The comments name the recorded group. Order
// matters and is the measured one: every check that does not write first, then
// the write-protected answer (esx_edevicebusy), then the host change.

bool EsxdosHostFs::host_read_only(const fs::path& host)
{
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::is_regular_file(st)) return false;
    return (st.permissions() & fs::perms::owner_write) == fs::perms::none;
}

void EsxdosHostFs::forget_alias(const fs::path& host) const
{
    const auto dir = aliases_.find(host.parent_path().string());
    if (dir != aliases_.end()) dir->second.erase(host.filename().string());
    const std::string key = host.string();
    for (auto it = aliases_.begin(); it != aliases_.end();) {
        const bool under = it->first.size() > key.size() &&
                           it->first.compare(0, key.size(), key) == 0 &&
                           (it->first[key.size()] == '/' || it->first[key.size()] == '\\');
        it = (it->first == key || under) ? aliases_.erase(it) : std::next(it);
    }
}

bool EsxdosHostFs::is_open_file(const fs::path& host) const
{
    for (const FileHandle& f : files_) {
        if (!f.open) continue;
        std::error_code ec;
        if (fs::equivalent(fs::path(f.path), host, ec) && !ec) return true;
    }
    return false;
}

bool EsxdosHostFs::wildcard_match(const std::string& pattern, const std::string& name)
{
    auto split = [](const std::string& s, std::string& a, std::string& b) {
        const std::size_t dot = s.rfind('.');
        a = dot == std::string::npos ? s : s.substr(0, dot);
        b = dot == std::string::npos ? std::string() : s.substr(dot + 1);
    };
    std::string pn, pe, nn, ne;
    split(pattern, pn, pe);
    split(name, nn, ne);
    return half_match(pn, nn) && half_match(pe, ne);
}

bool EsxdosHostFs::wildcard_entries(const fs::path& dir, const std::string& leaf,
                                    std::vector<Listed>& hits) const
{
    hits.clear();
    std::vector<Listed> listed;
    if (!list_host_dir(dir, listed)) return false;
    if (dir != root_) {
        for (const char* dots : {".", ".."}) {
            Listed l;
            l.path = dir / dots;
            l.lfn = l.sfn = dots;
            l.is_dir = true;
            listed.insert(listed.begin() + (dots[1] ? 1 : 0), l);
        }
    }
    for (const Listed& l : listed)
        if (wildcard_match(leaf, l.lfn) || wildcard_match(leaf, l.sfn))
            hits.push_back(l);
    return true;
}

uint8_t EsxdosHostFs::wild_lookup(const std::string& guest, std::vector<Listed>& hits) const
{
    const Leaf lf = split_leaf(guest);
    // A wildcard in a directory component finds nothing (measured: esx_enoent).
    if (is_wild(lf.prefix.substr(lf.drive))) return kEnoent;
    // The directory part is resolved itself, so the escape rules of resolve()
    // decide first. (Not through a probe name in it: an unlisted entry of that
    // name, a symlink, would make every wildcard call there esx_eacces.)
    fs::path dir;
    if (const uint8_t err = resolve(lf.prefix.empty() ? std::string(".") : lf.prefix, dir))
        return err == kBareDotDot ? kEnotdir : err;   // more path follows: not a bare '..'
    std::error_code dec;
    if (!fs::is_directory(fs::symlink_status(dir, dec)) || dec) return kEnotdir;
    if (!wildcard_entries(dir, lf.leaf, hits)) return kEio;
    return hits.empty() ? kEnoent : kOk;
}

uint8_t EsxdosHostFs::unlink_file(const fs::path& host)
{
    // A file one of our handles has open: refused here, before any host call, so
    // that a host which would refuse (Windows) or allow (POSIX) the delete never
    // decides the answer (measured: esx_eacces, for a read or a write handle).
    if (is_open_file(host)) return kEacces;
    if (host_read_only(host)) return kErdonly;
    if (!writable_) return kEdevicebusy;
    std::error_code ec;
    if (!fs::remove(host, ec) || ec) return kEio;
    forget_alias(host);
    return kOk;
}

uint8_t EsxdosHostFs::unlink(const std::string& guest_path)
{
    if (!active_) return kEnoent;
    if (path_has_wild(guest_path)) {
        // Every matching FILE goes, in name order, and the first refusal stops
        // the walk (measured: group wildmix). Directories, and "." and "..", are
        // matched and passed over; a pattern that matches nothing is esx_enoent.
        std::vector<Listed> hits;
        if (const uint8_t err = wild_lookup(guest_path, hits)) return err;
        for (const Listed& h : hits) {
            if (h.is_dir) continue;
            if (const uint8_t err = unlink_file(h.path)) return err;
        }
        return kOk;
    }
    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return lookup_miss(err);
    if (host == root_) return kEinval;                  // measured: "C:/" is esx_einval
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    // A directory: Fc=0 and nothing deleted, empty or not (measured, and
    // odd: it is what NextZXOS does, and F_RMDIR is the call that removes one).
    if (fs::is_directory(st)) return kOk;
    return unlink_file(host);
}

void EsxdosHostFs::close_streams_under(const fs::path& dir)
{
    for (FileHandle& f : files_) {
        if (!f.open) continue;
        const fs::path fp(f.path);
        const auto m = std::mismatch(dir.begin(), dir.end(), fp.begin(), fp.end());
        if (m.first == dir.end() && f.stream.is_open()) f.stream.close();
    }
}

void EsxdosHostFs::reopen_streams(const fs::path& from, const fs::path& to)
{
    for (FileHandle& f : files_) {
        if (!f.open || f.stream.is_open()) continue;
        fs::path fp(f.path);
        if (!to.empty()) {
            fs::path rel = fp.lexically_relative(from);
            fp = to / rel;
        }
        std::ios::openmode om = std::ios::binary | std::ios::in;
        if (f.writable) om |= std::ios::out;
        f.stream.clear();
        f.stream.open(fp, om);
        if (!f.stream.is_open()) { f = FileHandle{}; continue; }
        f.path = fp.string();
    }
    if (to.empty()) return;
    for (DirHandle& d : dirs_) {
        if (!d.open) continue;
        const fs::path dp(d.path);
        const auto m = std::mismatch(from.begin(), from.end(), dp.begin(), dp.end());
        if (m.first == from.end()) d.path = (to / dp.lexically_relative(from)).string();
    }
}

uint8_t EsxdosHostFs::rename(const std::string& from, const std::string& to)
{
    if (!active_) return kEnoent;
    if (path_has_wild(from) || path_has_wild(to)) return kEinval;   // measured
    fs::path src;
    std::vector<std::string> scomps;
    if (const uint8_t err = resolve(from, src, &scomps)) return lookup_miss(err);
    // "." and ".." cannot be renamed (measured: esx_eacces; changelog 2.00).
    const std::string sleaf = split_leaf(from).leaf;
    if (sleaf == "." || sleaf == "..") return kEacces;
    if (src == root_) return kEinval;
    std::error_code ec;
    const fs::file_status sst = fs::symlink_status(src, ec);
    if (ec || !fs::exists(sst)) return kEnoent;
    const bool src_dir = fs::is_directory(sst);
    if (!src_dir && is_open_file(src)) return kEacces;

    fs::path dst;
    std::vector<std::string> dcomps;
    // An empty destination is the current directory (measured: the source went
    // into it), as "." is.
    if (uint8_t err = resolve(to.empty() ? std::string(".") : to, dst, &dcomps))
        return err == kBareDotDot ? kEinval : err;           // measured: a bare '..' as destination
    fs::path target = dst;
    std::vector<std::string> tcomps = dcomps;
    const fs::file_status dst_st = fs::symlink_status(dst, ec);
    if (!ec && fs::exists(dst_st)) {
        // An existing FILE is never replaced (POSIX rename(2) and MinGW's would
        // replace it: refused here so the host does not decide): esx_eexist,
        // also for a case variant of the source itself. An existing DIRECTORY
        // receives the source inside it, and a name already there is
        // esx_edevicebusy (measured).
        if (!fs::is_directory(dst_st)) return kEexist;
        target = dst / src.filename();
        tcomps.push_back(src.filename().string());
        const auto m = std::mismatch(src.begin(), src.end(), target.begin(), target.end());
        if (src_dir && m.first == src.end()) return kEexist;     // into its own subtree
        // A name already there is decided by what the volume would answer to, not
        // by what the host does (GH #457 review): the listing's names in any case,
        // and 8.3 aliases in both directions (the source's own, and the one a long
        // name in the target holds). The host's own answer is a further guard.
        std::string sname = src.filename().string(), ssfn;
        std::vector<Listed> sib, held;
        if (list_host_dir(src.parent_path(), sib))
            for (const Listed& l : sib)
                if (l.lfn == sname) { ssfn = l.sfn; break; }
        if (list_host_dir(dst, held))
            for (const Listed& l : held)
                if (iequal(l.lfn, sname) || iequal(l.sfn, sname) ||
                    (!ssfn.empty() && (iequal(l.lfn, ssfn) || iequal(l.sfn, ssfn))))
                    return kEdevicebusy;
        std::error_code ec2;
        if (fs::exists(fs::symlink_status(target, ec2))) return kEdevicebusy;
    } else {
        if (!valid_new_name(dst)) return kEinval;
        const auto m = std::mismatch(src.begin(), src.end(), target.begin(), target.end());
        if (src_dir && m.first == src.end()) return kEexist;     // into its own subtree
    }
    if (!writable_) return kEdevicebusy;

    if (src_dir) close_streams_under(src);
    fs::rename(src, target, ec);
    if (ec) {
        if (src_dir) reopen_streams(src, fs::path());
        return kEio;
    }
    // The source name is free again, and takes its alias with it.
    if (const auto d = aliases_.find(src.parent_path().string()); d != aliases_.end())
        d->second.erase(src.filename().string());
    if (src_dir) {
        reopen_streams(src, target);
        // The aliases kept for what was inside a moved directory follow it: they
        // are looked up by host path.
        const std::string old_key = src.string();
        // The directory's own table is filed under `target` itself: its relative
        // path to `src` is "." and `target / "."` is a key nothing looks up.
        std::vector<std::pair<std::string, std::map<std::string, std::string>>> moved;
        for (auto it = aliases_.begin(); it != aliases_.end();) {
            const bool own = it->first == old_key;
            const bool under = own ||
                (it->first.size() > old_key.size() &&
                 it->first.compare(0, old_key.size(), old_key) == 0 &&
                 (it->first[old_key.size()] == '/' || it->first[old_key.size()] == '\\'));
            if (under) {
                moved.emplace_back(
                    own ? target.string()
                        : (target / fs::path(it->first).lexically_relative(src)).string(),
                    std::move(it->second));
                it = aliases_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& m : moved) aliases_[m.first] = std::move(m.second);
    }
    // The working directory follows its directory, as the volume's own would.
    if (src_dir && cwd_.size() >= scomps.size() &&
        std::equal(scomps.begin(), scomps.end(), cwd_.begin())) {
        std::vector<std::string> next = tcomps;
        next.insert(next.end(), cwd_.begin() + scomps.size(), cwd_.end());
        cwd_ = next;
    }
    return kOk;
}

uint8_t EsxdosHostFs::mkdir(const std::string& guest_path)
{
    if (!active_) return kEnoent;
    fs::path host;
    if (uint8_t err = resolve(guest_path, host))
        return err == kBareDotDot ? kEinval : err;           // measured: "C:/.." is esx_einval
    if (host == root_) return kEinval;                  // measured, not esx_eexist
    std::error_code ec;
    if (fs::exists(fs::symlink_status(host, ec))) return kEexist;
    // A wildcard in the name is esx_einval too (measured): valid_new_name().
    if (!valid_new_name(host)) return kEinval;
    if (!writable_) return kEdevicebusy;
    return fs::create_directory(host, ec) && !ec ? kOk : kEio;
}

uint8_t EsxdosHostFs::rmdir(const std::string& guest_path)
{
    if (!active_) return kEnoent;
    if (path_has_wild(guest_path)) return kEinval;      // measured
    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return lookup_miss(err);
    if (host == root_) return kEinval;
    const std::string leaf = split_leaf(guest_path).leaf;
    if (leaf == "." || leaf == "..") return kEacces;    // measured
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    if (!fs::is_directory(st)) return kEnotdir;
    fs::path cwdp = root_;
    for (const std::string& c : cwd_) cwdp /= c;
    if (host == cwdp) return kEacces;                   // the current directory
    // Empty means NOTHING in it on the host, an unlisted symlink included.
    if (fs::directory_iterator(host, ec) != fs::directory_iterator()) return kEnospc;
    if (!writable_) return kEdevicebusy;
    if (!fs::remove(host, ec) || ec) return kEio;
    forget_alias(host);
    return kOk;
}

uint8_t EsxdosHostFs::truncate_file(const fs::path& host, uint32_t size)
{
    if (is_open_file(host)) return kEacces;
    if (host_read_only(host)) return kErdonly;
    // NextZXOS answers Fc=0 on a write-protected card and loses the write
    // (measured). A write that did not happen is not an answer to copy: refused
    // with what every other write gets. A declared divergence.
    if (!writable_) return kEdevicebusy;
    std::error_code ec;
    fs::resize_file(host, size, ec);      // the extension reads as zeroes
    return ec ? kEio : kOk;
}

uint8_t EsxdosHostFs::truncate(const std::string& guest_path, uint32_t size)
{
    if (!active_) return kEnoent;
    fs::path host;
    if (path_has_wild(guest_path)) {
        // A wildcard truncates the FIRST match only (measured).
        std::vector<Listed> hits;
        if (const uint8_t err = wild_lookup(guest_path, hits)) return err;
        const Listed* first = nullptr;
        for (const Listed& h : hits)
            if (h.lfn != "." && h.lfn != "..") { first = &h; break; }
        if (!first) return kEnoent;
        if (first->is_dir) return kEinval;
        return truncate_file(first->path, size);
    }
    if (const uint8_t err = resolve(guest_path, host)) return lookup_miss(err);
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    if (fs::is_directory(st)) return kEinval;           // measured
    return truncate_file(host, size);
}

uint8_t EsxdosHostFs::ftruncate(uint8_t handle, uint32_t size)
{
    FileHandle* f = file_slot(handle);
    if (!f || !f->open) return kEbadf;
    if (!f->writable) return kErdonly;                  // measured: a read handle
    // The stream is closed around the resize on every host (Windows will not
    // resize a file another handle holds open); the position is jnext's own and
    // is untouched, also past the new end (measured).
    f->stream.close();
    std::error_code ec;
    fs::resize_file(fs::path(f->path), size, ec);
    f->stream.clear();
    f->stream.open(fs::path(f->path), std::ios::binary | std::ios::in | std::ios::out);
    if (!f->stream.is_open()) { *f = FileHandle{}; return kEio; }
    return ec ? kEio : kOk;
}

uint8_t EsxdosHostFs::chmod_file(const fs::path& host, uint8_t values, uint8_t mask)
{
    // Only what the host can store AND F_STAT/F_READDIR will then report as
    // NextZXOS would: A_WRITE (the inverse of the read-only attribute; the host
    // write permission), A_READ (inert, measured) and A_ARCH (always reported
    // set here, so setting it is a no-op and clearing it is not storable).
    // Anything else is refused, not faked: hidden and system, and the archive
    // bit cleared, have no host form.
    constexpr uint8_t kServed = kChmodWrite | kChmodRead | kChmodArch;
    if (mask & ~kServed) return kEnosys;
    if ((mask & kChmodArch) && !(values & kChmodArch)) return kEnosys;
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec) return kEio;
    // A directory has no stored attribute here, and NextZXOS reports a changed
    // one through F_READDIR (measured), so only the inert bit is served.
    if (fs::is_directory(st) && (mask & (kChmodWrite | kChmodArch))) return kEnosys;
    if (!(mask & kChmodWrite)) return kOk;
    const fs::perms w = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    if (values & kChmodWrite)
        fs::permissions(host, fs::perms::owner_write, fs::perm_options::add, ec);
    else
        fs::permissions(host, w, fs::perm_options::remove, ec);
    return ec ? kEio : kOk;
}

uint8_t EsxdosHostFs::chmod(const std::string& guest_path, uint8_t values, uint8_t mask)
{
    if (!active_) return kEnoent;
    if (path_has_wild(guest_path)) {
        // Every matching entry, "." and ".." excluded; the first refusal stops.
        std::vector<Listed> hits;
        if (const uint8_t err = wild_lookup(guest_path, hits)) return err;
        if (!writable_) return kEdevicebusy;
        for (const Listed& h : hits) {
            if (h.lfn == "." || h.lfn == "..") continue;
            if (const uint8_t err = chmod_file(h.path, values, mask)) return err;
        }
        return kOk;
    }
    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return lookup_miss(err);
    if (host == root_) return kEinval;
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(host, ec))) return kEnoent;
    if (!writable_) return kEdevicebusy;                // measured: even for a mask of 0
    return chmod_file(host, values, mask);
}

// ── Directory calls ──────────────────────────────────────────────────────

uint8_t EsxdosHostFs::opendir(const std::string& guest_path, uint8_t mode,
                              uint8_t& handle, uint8_t sf)
{
    if (!active_) return kEnoent;

    // A listing mode is SERVED only where the answer is exactly what NextZXOS
    // gives, and REFUSED everywhere else: a guest that asked for a filtered,
    // sorted listing and silently got an unfiltered unsorted one is worse off
    // than one told "not supported" (jnext(1)). What NextZXOS gives was
    // MEASURED (GH #301), not read off the API document: a probe NEX run from
    // the NextZXOS Browser listed one directory in each mode, and these modes
    // returned the plain listing entry for entry, in the same order:
    //
    //   * esx_mode_sf_enable with no exclude bit and no esx_sf_sort_enable in
    //     C ($00, and $07: the sort-key and reverse bits are inert without
    //     sort_enable). NXModPlayer opens every listing after its first this
    //     way. Exclusion and sorting are not served.
    //   * esx_mode_use_wildcards. The pattern that filters is the one passed
    //     to F_READDIR, not this one: opened with "*.TXT" and read with "*.*"
    //     the listing is complete, opened with "*.*" and read with "*.TXT" it
    //     is filtered. So it is checked there — see readdir().
    //
    // The +3DOS header mode is still refused: a host file has no header.
    if (mode & kModeUseHeader) return kEnosys;
    if ((mode & kDirSfEnable) && (sf & kSfNotServedMask)) return kEnosys;
    const uint8_t name_mode = mode & kDirNameMask;
    if (name_mode != kDirShortOnly && name_mode != kDirLfnOnly &&
        name_mode != kDirLfnAndShort)
        return kEinval;

    fs::path host;
    if (const uint8_t err = resolve(guest_path, host))
        return err == kBareDotDot ? kEnotdir : err;    // a bare '..': measured esx_enotdir

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnotdir;   // measured, see resolve()
    if (fs::is_symlink(st)) return kEacces;
    if (!fs::is_directory(st)) return kEnotdir;

    // Re-check containment here as open() does. resolve()'s own trailing check
    // already covers this path, so this is defence in depth, not the barrier —
    // but having one of the two entry points re-check and the other not was an
    // inconsistency a reader would have to resolve by guessing (GH #31 review).
    if (!contained(host)) return kEacces;

    int slot = -1;
    for (int i = 0; i < kDirHandles; ++i)
        if (!dirs_[i].open) { slot = i; break; }
    if (slot < 0) return kEnfile;

    // The listing is taken ONCE, here. FAT enumeration is a walk over a
    // directory that is not changing under the guest; a host directory can
    // change mid-walk, and a snapshot is the only way the entry stream stays
    // self-consistent (and the only way two runs agree).
    std::vector<DirEntry> entries;

    // FAT subdirectories carry '.' and '..'; the FAT32 root does not, which is
    // why esx_sf_exclude_dots exists. Reproduce that.
    if (host != root_) {
        for (const char* dots : {".", ".."}) {
            DirEntry e;
            e.attr = kAttrDirectory;
            e.lfn = dots;
            e.sfn = dots;
            entries.push_back(e);
        }
    }

    std::vector<Listed> listed;
    if (!list_host_dir(host, listed)) return kEio;

    for (const Listed& l : listed) {
        DirEntry e;
        e.lfn = l.lfn;
        e.sfn = l.sfn;
        e.attr = l.is_dir ? kAttrDirectory : kAttrArchive;
        if (!writable_ || (!l.is_dir && host_read_only(l.path))) e.attr |= kAttrReadOnly;
        if (!e.lfn.empty() && e.lfn.front() == '.') e.attr |= kAttrHidden;
        if (!l.is_dir) {
            std::error_code ec3;
            const uint64_t size = fs::file_size(l.path, ec3);
            e.size = ec3 ? 0 : static_cast<uint32_t>(
                std::min<uint64_t>(size, 0xFFFFFFFFULL));
        }
        std::error_code ec4;
        const auto mtime = fs::last_write_time(l.path, ec4);
        if (!ec4) dos_timestamp(mtime, e.date, e.time);
        entries.push_back(std::move(e));
    }

    DirHandle& d = dirs_[slot];
    d.open = true;
    d.path = host.string();
    d.mode = static_cast<uint8_t>(name_mode | (mode & kDirUseWildcards));
    d.index = 0;
    d.entries = std::move(entries);
    handle = static_cast<uint8_t>(kFirstDirHandle + slot);
    return kOk;
}

uint8_t EsxdosHostFs::readdir(uint8_t handle, DirEntry& out, bool& have,
                              const std::string& wildcard)
{
    have = false;
    DirHandle* d = dir_slot(handle);
    if (!d || !d->open) return kEbadf;
    // A wildcard handle is filtered HERE, by the pattern this call passes
    // (measured, see opendir()). NextZXOS matches the pattern against the 8.3
    // name (measured: "*" lists only the entries whose short name has no
    // extension), so "*.*" — eight-and-three of anything — matches every
    // entry, '.' and '..' included: the measured listing was the plain one.
    // That is the one pattern answered; any other would need NextZXOS's
    // matcher reproduced exactly, and is refused instead.
    if ((d->mode & kDirUseWildcards) && wildcard != kMatchEveryEntry)
        return kEnosys;
    if (d->index >= d->entries.size()) return kOk;   // A=0, Fc=0: no more
    out = d->entries[d->index++];
    have = true;
    return kOk;
}

uint8_t EsxdosHostFs::rewinddir(uint8_t handle)
{
    DirHandle* d = dir_slot(handle);
    if (!d || !d->open) return kEbadf;
    d->index = 0;
    return kOk;
}

uint8_t EsxdosHostFs::telldir(uint8_t handle, uint32_t& position) const
{
    const DirHandle* d = dir_slot(handle);
    if (!d || !d->open) return kEbadf;
    position = static_cast<uint32_t>(d->index);
    return kOk;
}

uint8_t EsxdosHostFs::seekdir(uint8_t handle, uint32_t position)
{
    DirHandle* d = dir_slot(handle);
    if (!d || !d->open) return kEbadf;
    if (position > d->entries.size()) return kEinval;
    d->index = position;
    return kOk;
}

uint8_t EsxdosHostFs::dir_name_mode(uint8_t handle) const
{
    const DirHandle* d = dir_slot(handle);
    return d && d->open ? static_cast<uint8_t>(d->mode & kDirNameMask)
                        : kDirShortOnly;
}

bool EsxdosHostFs::dir_uses_wildcards(uint8_t handle) const
{
    const DirHandle* d = dir_slot(handle);
    return d && d->open && (d->mode & kDirUseWildcards) != 0;
}

// F_GETCWD answers in NextZXOS's own form (GH #301), MEASURED from a probe
// run under NextZXOS: the drive, then every component as its 8.3 name, each
// followed by '/' — `C:/` at the root, `C:/AAA/T/SUBDIR~1/` below it (the
// LFN there was "sub dir long"). A program builds its paths as cwd + name, so
// the trailing '/' is load-bearing: NXModPlayer asked for "/NXModPlayermods"
// off the old "/NXModPlayer" form. The one drive is C: (M_GETSETDRV).
uint8_t EsxdosHostFs::format_path(const std::vector<std::string>& comps,
                                  std::string& out) const
{
    out = "C:/";
    fs::path walk = root_;
    for (const std::string& comp : comps) {
        std::vector<Listed> listed;
        if (!list_host_dir(walk, listed)) return kEio;
        const Listed* hit = nullptr;
        for (const Listed& l : listed)
            if (l.lfn == comp) { hit = &l; break; }
        // A directory removed on the host since the guest entered it has no
        // short name left to print; say so rather than invent one.
        if (!hit || !hit->is_dir) return kEnoent;
        out += hit->sfn;
        out += "/";
        walk /= comp;
    }
    if (out.size() > kMaxPath) return kEinval;
    return kOk;
}

uint8_t EsxdosHostFs::getcwd(std::string& out) const
{
    if (!active_) return kEnoent;
    return format_path(cwd_, out);
}

uint8_t EsxdosHostFs::getcwd_of(const std::string& filespec,
                                std::string& out) const
{
    if (!active_) return kEnoent;
    // A=$FF: the working directory of `filespec`, "the filename part (after
    // the final /, \ or :) is ignored" (NextZXOS API, F_GETCWD). Measured:
    // "T/x" -> C:/AAA/T/, "x" and "C:x" -> the CWD, "T/sub dir long/" ->
    // C:/AAA/T/SUBDIR~1/. A directory that does not exist ("nonexist/x"), and
    // a path through a FILE ("T/README/x"), both got Fc=0 and a buffer of junk
    // from NextZXOS, which is not an answer to reproduce. They are refused
    // instead, with esx_enotdir. That code is INFERRED, not measured: it is
    // what NextZXOS answers for a missing or non-directory component in every
    // other call that was measured (see resolve()), not what it answers here.
    const std::size_t cut = filespec.find_last_of("/\\:");
    std::string dir = cut == std::string::npos ? std::string()
                                               : filespec.substr(0, cut + 1);
    // A ':' that ends the directory part is the separator, not a character of the
    // name before it: 'T/a:x' asks for the directory 'T/a'. (A bare drive
    // qualifier, "C:", is two characters and is left to resolve().) resolve()
    // refuses a ':' inside a component with esx_einval, which is the answer for
    // the calls NextZXOS was measured on; F_GETCWD A=$FF answered 'T/a:x' like a
    // missing directory (HFS-122), so it must not reach that rule.
    if (dir.size() > 2 && dir.back() == ':') dir.pop_back();
    if (dir.empty()) dir = ".";
    fs::path host;
    std::vector<std::string> comps;
    if (const uint8_t err = resolve(dir, host, &comps))
        return err == kBareDotDot ? kEnotdir : err;
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnotdir;   // measured, see resolve()
    if (fs::is_symlink(st)) return kEacces;
    if (!fs::is_directory(st)) return kEnotdir;
    return format_path(comps, out);
}

uint8_t EsxdosHostFs::chdir(const std::string& guest_path)
{
    if (!active_) return kEnoent;
    fs::path host;
    std::vector<std::string> comps;
    if (const uint8_t err = resolve(guest_path, host, &comps))
        return err == kBareDotDot ? kEnotdir : err;    // a bare '..': measured esx_enotdir

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnotdir;   // measured, see resolve()
    if (fs::is_symlink(st)) return kEacces;
    if (!fs::is_directory(st)) return kEnotdir;

    // resolve() hands back the components as they are spelled on the HOST,
    // so getcwd() can look each one up whatever spelling — any case, or the
    // 8.3 name — the guest happened to type.
    cwd_ = comps;
    return kOk;
}

uint32_t EsxdosHostFs::free_blocks() const
{
    if (!active_) return 0;
    std::error_code ec;
    const fs::space_info si = fs::space(root_, ec);
    const uint64_t bytes = ec ? 0 : si.available;
    uint64_t blocks = bytes / 512;
    // F_GETFREE hands the guest a 32-bit block count in BCDE
    // (asm_esx_f_getfree.asm). The count itself cannot overflow — a 512-byte
    // block count fits 32 bits up to 2 TiB — but a GUEST that multiplies it
    // back into a byte figure overflows its own 32-bit arithmetic above 4 GiB,
    // and a Z80 program's "free space" arithmetic is routinely 32-bit or less.
    // So the bound is chosen for the CONSUMER, not for the field: report at
    // most 4 GiB worth of blocks, the largest free figure whose byte value
    // still fits the register width a guest is likely to compute it in. A host
    // with terabytes free reports this ceiling instead of a true number, which
    // is the honest trade for a value no FAT-era program can use anyway.
    constexpr uint64_t kMaxBlocks = 4ULL * 1024 * 1024 * 1024 / 512;  // 8388608
    if (blocks > kMaxBlocks) blocks = kMaxBlocks;
    return static_cast<uint32_t>(blocks);
}

// ── Rewind / save-state ──────────────────────────────────────────────────
//
// An open host stream cannot be put in a snapshot, so only the reopenable
// triple travels and the stream is re-established on restore. That makes a
// rewind across READS exact: the guest's re-run re-reads from the offset it
// had. It cannot make a rewind across a WRITE exact — the host side effect has
// already happened and no snapshot can take it back. That asymmetry is the
// reason writes need --esxdos-stub-writable and reads do not.

std::vector<EsxdosHostFs::HandleSnapshot> EsxdosHostFs::snapshot() const
{
    std::vector<HandleSnapshot> out;
    for (int i = 0; i < kFileHandles; ++i) {
        if (!files_[i].open) continue;
        HandleSnapshot h;
        h.handle = static_cast<uint8_t>(kFirstFileHandle + i);
        h.is_dir = false;
        h.path = files_[i].path;
        h.position = files_[i].position;
        h.mode = files_[i].writable ? kModeWrite : kModeRead;
        out.push_back(std::move(h));
    }
    for (int i = 0; i < kDirHandles; ++i) {
        if (!dirs_[i].open) continue;
        HandleSnapshot h;
        h.handle = static_cast<uint8_t>(kFirstDirHandle + i);
        h.is_dir = true;
        h.path = dirs_[i].path;
        h.position = dirs_[i].index;
        h.mode = dirs_[i].mode;
        out.push_back(std::move(h));
    }
    return out;
}

void EsxdosHostFs::restore(const std::vector<HandleSnapshot>& handles)
{
    if (!active_) return;
    for (auto& f : files_) { if (f.stream.is_open()) f.stream.close(); f = FileHandle{}; }
    for (auto& d : dirs_) d = DirHandle{};

    for (const HandleSnapshot& h : handles) {
        if (h.is_dir) {
            DirHandle* d = dir_slot(h.handle);
            if (!d) continue;
            // Re-enumerate: the snapshot carries the position, not the list.
            // A host directory that changed since the snapshot yields a
            // different list, which is the same "host churn" caveat opendir
            // already documents — not a new one.
            uint8_t reopened = 0;
            const std::string rel = fs::path(h.path).lexically_relative(root_).string();
            if (opendir(rel.empty() ? "/" : "/" + rel, h.mode, reopened) == kOk) {
                DirHandle* nd = dir_slot(reopened);
                if (nd && nd != d) { *d = std::move(*nd); *nd = DirHandle{}; }
                d->index = std::min<std::size_t>(
                    static_cast<std::size_t>(h.position), d->entries.size());
            }
            continue;
        }
        FileHandle* f = file_slot(h.handle);
        if (!f) continue;
        if (!contained(fs::path(h.path))) continue;
        std::ios::openmode om = std::ios::binary | std::ios::in;
        const bool w = (h.mode & kModeWrite) != 0 && writable_;
        if (w) om |= std::ios::out;
        f->stream.open(fs::path(h.path), om);
        if (!f->stream.is_open()) continue;
        f->open = true;
        f->writable = w;
        f->path = h.path;
        f->position = h.position;
    }
}

std::string EsxdosHostFs::cwd_for_snapshot() const
{
    std::string out;
    for (std::size_t i = 0; i < cwd_.size(); ++i) {
        out += cwd_[i];
        if (i + 1 < cwd_.size()) out += "/";
    }
    return out;
}

void EsxdosHostFs::restore_cwd(const std::string& cwd)
{
    cwd_.clear();
    if (cwd.empty()) return;
    std::size_t i = 0;
    while (i <= cwd.size()) {
        const std::size_t next = cwd.find('/', i);
        const std::string part =
            cwd.substr(i, next == std::string::npos ? std::string::npos : next - i);
        i = (next == std::string::npos) ? cwd.size() + 1 : next + 1;
        if (!part.empty() && part != "." && part != "..") cwd_.push_back(part);
    }
}
