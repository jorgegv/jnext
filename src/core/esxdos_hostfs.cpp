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

}  // namespace

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

bool EsxdosHostFs::configure(const std::string& root, bool writable,
                             std::string& error)
{
    for (auto& f : files_) { if (f.stream.is_open()) f.stream.close(); f = FileHandle{}; }
    for (auto& d : dirs_)  d = DirHandle{};
    cwd_.clear();
    active_ = false;
    writable_ = false;
    root_.clear();

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

    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t next = std::string::npos;
        for (std::size_t j = i; j < s.size(); ++j)
            if (is_sep(s[j])) { next = j; break; }
        const std::string part =
            s.substr(i, next == std::string::npos ? std::string::npos : next - i);
        i = (next == std::string::npos) ? s.size() + 1 : next + 1;

        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (comps.empty()) return kEpath;   // would leave the root
            comps.pop_back();
            continue;
        }
        if (part.size() > 255) return kEinval;
        if (reserved_device_name(part)) return kEacces;
        comps.push_back(part);
    }

    // Walk the host tree, matching each component case-insensitively (FAT is
    // case-insensitive; Linux is not). An exact match always wins; otherwise
    // the lexicographically smallest case-insensitive match is taken, so the
    // answer is the same on every run and on every host.
    fs::path p = root_;
    bool missing = false;
    for (const std::string& want : comps) {
        if (!missing) {
            std::error_code ec;
            const fs::path exact = p / want;
            const fs::file_status st = fs::symlink_status(exact, ec);
            if (!ec && fs::exists(st)) {
                if (fs::is_symlink(st)) return kEacces;
                p = exact;
                continue;
            }
            std::string best;
            for (fs::directory_iterator it(p, ec), end; !ec && it != end;
                 it.increment(ec)) {
                const std::string have = it->path().filename().string();
                if (!iequal(have, want)) continue;
                if (best.empty() || have < best) best = have;
            }
            if (!best.empty()) {
                const fs::path picked = p / best;
                std::error_code ec2;
                if (fs::is_symlink(fs::symlink_status(picked, ec2))) return kEacces;
                p = picked;
                continue;
            }
            missing = true;   // and so is everything below it
        }
        p /= want;
    }

    if (!contained(p)) return kEacces;
    out = p;
    if (components) *components = comps;
    return kOk;
}

// ── 8.3 synthesis and timestamps ─────────────────────────────────────────

std::string EsxdosHostFs::short_name(const std::string& name, unsigned ordinal)
{
    if (name == "." || name == "..") return name;

    const std::size_t dot = name.rfind('.');
    const std::string raw_base = dot == std::string::npos ? name
                                                          : name.substr(0, dot);
    const std::string raw_ext  = dot == std::string::npos ? std::string()
                                                          : name.substr(dot + 1);

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
    if (const uint8_t err = resolve(guest_path, host)) return err;

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    const bool exists = !ec && fs::exists(st);
    if (exists && fs::is_symlink(st)) return kEacces;
    if (exists && fs::is_directory(st)) return kEisdir;
    if (exists && !fs::is_regular_file(st)) return kEacces;

    switch (creat) {
        case 0x00:  if (!exists) return kEnoent; break;           // open_exist
        case kModeCreatNoExist: if (exists) return kEexist; break;
        case kModeOpenCreat:    break;
        case kModeCreatTrunc:   break;
        default: return kEinval;
    }
    if (!exists && creat == 0x00) return kEnoent;

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
    if (!writable_) out.attr |= kAttrReadOnly;
    std::error_code ec2;
    const auto mtime = fs::last_write_time(p, ec2);
    if (!ec2) dos_timestamp(mtime, out.date, out.time);
    return kOk;
}

uint8_t EsxdosHostFs::stat(const std::string& guest_path, StatInfo& out)
{
    if (!active_) return kEnoent;
    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return err;

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    if (fs::is_symlink(st)) return kEacces;

    out = StatInfo{};
    if (fs::is_directory(st)) {
        out.attr = kAttrDirectory;
    } else if (fs::is_regular_file(st)) {
        out.attr = kAttrArchive;
        std::error_code ec2;
        const uint64_t size = fs::file_size(host, ec2);
        out.size = ec2 ? 0 : static_cast<uint32_t>(
            std::min<uint64_t>(size, 0xFFFFFFFFULL));
    } else {
        return kEacces;
    }
    if (!writable_) out.attr |= kAttrReadOnly;
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

// ── Directory calls ──────────────────────────────────────────────────────

uint8_t EsxdosHostFs::opendir(const std::string& guest_path, uint8_t mode,
                              uint8_t& handle)
{
    if (!active_) return kEnoent;

    // Wildcards, sorting/filtering and +3DOS headers are REFUSED rather than
    // half-implemented: each changes what the entry stream contains, and a
    // guest that asked for a filtered, sorted listing and silently got an
    // unfiltered unsorted one is worse off than one told "not supported".
    // The two modes real software was measured to use (`.ls` asks for
    // esx_mode_lfn_only and esx_mode_short_only, with no other bits) are
    // exactly the ones served.
    if (mode & (kDirUseWildcards | kDirSfEnable | kModeUseHeader))
        return kEnosys;
    const uint8_t name_mode = mode & kDirNameMask;
    if (name_mode != kDirShortOnly && name_mode != kDirLfnOnly &&
        name_mode != kDirLfnAndShort)
        return kEinval;

    fs::path host;
    if (const uint8_t err = resolve(guest_path, host)) return err;

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
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

    std::vector<fs::directory_entry> raw;
    for (fs::directory_iterator it(host, ec), end; !ec && it != end;
         it.increment(ec))
        raw.push_back(*it);
    if (ec) return kEio;

    // Sorted by host name so the entry order is identical on every run and on
    // every host; directory_iterator order is unspecified.
    std::sort(raw.begin(), raw.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return a.path().filename().string() <
                         b.path().filename().string();
              });

    std::vector<std::string> used_short;
    for (const fs::directory_entry& de : raw) {
        std::error_code ec2;
        const fs::file_status dst = fs::symlink_status(de.path(), ec2);
        if (ec2) continue;
        // A symlink is not traversed, so it is not listed either — listing it
        // would advertise a name that every other call refuses.
        if (fs::is_symlink(dst)) continue;
        const bool is_dir = fs::is_directory(dst);
        if (!is_dir && !fs::is_regular_file(dst)) continue;

        DirEntry e;
        e.lfn = de.path().filename().string();
        e.attr = is_dir ? kAttrDirectory : kAttrArchive;
        if (!writable_) e.attr |= kAttrReadOnly;
        if (!e.lfn.empty() && e.lfn.front() == '.') e.attr |= kAttrHidden;
        if (!is_dir) {
            std::error_code ec3;
            const uint64_t size = fs::file_size(de.path(), ec3);
            e.size = ec3 ? 0 : static_cast<uint32_t>(
                std::min<uint64_t>(size, 0xFFFFFFFFULL));
        }
        std::error_code ec4;
        const auto mtime = fs::last_write_time(de.path(), ec4);
        if (!ec4) dos_timestamp(mtime, e.date, e.time);

        // 8.3 synthesis: a name that already IS 8.3 keeps itself (uppercased);
        // anything else gets the FAT `~N` tail, with N chosen so the short
        // names are unique within this listing. Deterministic because `raw` is
        // sorted.
        std::string sfn = short_name(e.lfn, 0);
        const bool native = fits_8_3(e.lfn);
        auto taken = [&used_short](const std::string& s) {
            return std::find(used_short.begin(), used_short.end(), s) !=
                   used_short.end();
        };
        if (!native || taken(sfn)) {
            unsigned n = 1;
            do { sfn = short_name(e.lfn, n++); } while (taken(sfn) && n < 1000);
        }
        used_short.push_back(sfn);
        e.sfn = sfn;
        entries.push_back(std::move(e));
    }

    DirHandle& d = dirs_[slot];
    d.open = true;
    d.path = host.string();
    d.mode = name_mode;
    d.index = 0;
    d.entries = std::move(entries);
    handle = static_cast<uint8_t>(kFirstDirHandle + slot);
    return kOk;
}

uint8_t EsxdosHostFs::readdir(uint8_t handle, DirEntry& out, bool& have)
{
    have = false;
    DirHandle* d = dir_slot(handle);
    if (!d || !d->open) return kEbadf;
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
    return d && d->open ? d->mode : kDirShortOnly;
}

uint8_t EsxdosHostFs::getcwd(std::string& out) const
{
    if (!active_) return kEnoent;
    out = "/";
    for (std::size_t i = 0; i < cwd_.size(); ++i) {
        out += cwd_[i];
        if (i + 1 < cwd_.size()) out += "/";
    }
    if (out.size() > kMaxPath) return kEinval;
    return kOk;
}

uint8_t EsxdosHostFs::chdir(const std::string& guest_path)
{
    if (!active_) return kEnoent;
    fs::path host;
    std::vector<std::string> comps;
    if (const uint8_t err = resolve(guest_path, host, &comps)) return err;

    std::error_code ec;
    const fs::file_status st = fs::symlink_status(host, ec);
    if (ec || !fs::exists(st)) return kEnoent;
    if (fs::is_symlink(st)) return kEacces;
    if (!fs::is_directory(st)) return kEnotdir;

    // Store the components as they exist on the HOST, so getcwd() answers with
    // the real spelling rather than whatever case the guest happened to type.
    std::vector<std::string> actual;
    fs::path walk = root_;
    for (const std::string& want : comps) {
        std::error_code ec2;
        std::string picked = want;
        for (fs::directory_iterator it(walk, ec2), end; !ec2 && it != end;
             it.increment(ec2)) {
            const std::string have = it->path().filename().string();
            if (have == want) { picked = have; break; }
            if (iequal(have, want) && (picked == want)) picked = have;
        }
        actual.push_back(picked);
        walk /= picked;
    }
    cwd_ = actual;
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
