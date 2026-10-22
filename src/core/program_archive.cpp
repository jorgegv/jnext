#include "core/program_archive.h"

#include "core/sdcard_provisioner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_set>

namespace fs = std::filesystem;

namespace program_archive {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ext_of(const std::string& path) {
    const auto dot = path.rfind('.');
    return dot == std::string::npos ? std::string() : lower(path.substr(dot));
}

/// Last component, splitting on both separators.
std::string base_of(const std::string& name) {
    const auto p = name.find_last_of("/\\");
    return p == std::string::npos ? name : name.substr(p + 1);
}

std::string stem_of(const std::string& name) {
    const std::string b = base_of(name);
    const auto dot = b.rfind('.');
    return lower(dot == std::string::npos ? b : b.substr(0, dot));
}

/// APPNOTE 4.4.17: no drive, no leading slash, no `..`; both separators.
bool unsafe_name(const std::string& n) {
    if (n.empty() || n[0] == '/' || n[0] == '\\') return true;
    if (n.size() >= 2 && n[1] == ':' &&
        std::isalpha(static_cast<unsigned char>(n[0]))) return true;
    size_t start = 0;
    while (start <= n.size()) {
        size_t end = n.find_first_of("/\\", start);
        if (end == std::string::npos) end = n.size();
        if (n.compare(start, end - start, "..") == 0) return true;
        start = end + 1;
    }
    return false;
}

std::string safe_basename(const std::string& name) {
    std::string b = base_of(name);
    for (auto& c : b) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E || std::string("<>:\"|?*").find(c) != std::string::npos) c = '_';
    }
    return b;
}

std::string join_names(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size() && i < 20; ++i) s += (i ? ", " : "") + display_name(v[i]);
    if (v.size() > 20) s += " and " + std::to_string(v.size() - 20) + " more";
    return s;
}

}  // namespace

std::string display_name(const std::string& name) {
    std::string out;
    auto esc = [&out](unsigned char c) {
        static const char* d = "0123456789ABCDEF";
        out += "\\x";
        out += d[c >> 4];
        out += d[c & 15];
    };
    for (size_t i = 0; i < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == 0x7F) { esc(c); ++i; continue; }
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        // RFC 3629 well-formed sequence?
        size_t n = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool ok = n && i + n <= name.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(name[i + k]) & 0xC0) == 0x80;
        if (ok) {
            const unsigned char c1 = static_cast<unsigned char>(name[i + 1]);
            if (c == 0xE0 && c1 < 0xA0) ok = false;                 // overlong
            if (c == 0xED && c1 >= 0xA0) ok = false;               // surrogate
            if (c == 0xF0 && c1 < 0x90) ok = false;                // overlong
            if (c == 0xF4 && c1 >= 0x90) ok = false;               // above U+10FFFF
        }
        if (ok && c == 0xC2 && static_cast<unsigned char>(name[i + 1]) < 0xA0) ok = false;   // C1 control
        if (!ok) { esc(c); ++i; continue; }
        out.append(name, i, n);
        i += n;
    }
    return out;
}

bool is_program_ext(const std::string& path) {
    const std::string e = ext_of(path);
    return e == ".nex" || e == ".jns" || e == ".sna" || e == ".szx" || e == ".z80" ||
           e == ".tap" || e == ".tzx" || e == ".wav" || e == ".rzx";
}

bool is_archive_path(const std::string& path) {
    if (is_program_ext(path) || ext_of(path) == ".pok") return false;
    if (ext_of(path) == ".zip") return true;
    std::ifstream f(path, std::ios::binary);
    unsigned char m[4] = {};
    if (!f.read(reinterpret_cast<char*>(m), 4)) return false;
    return m[0] == 'P' && m[1] == 'K' &&
           ((m[2] == 3 && m[3] == 4) || (m[2] == 5 && m[3] == 6));
}

// ---------------------------------------------------------------- ScratchDir

ScratchDir::~ScratchDir() {
    if (proc_.empty()) return;
    std::error_code ec;
    fs::remove_all(proc_, ec);
}

std::string ScratchDir::new_slot(std::string& why) {
    std::error_code ec;
    if (proc_.empty()) {
        fs::create_directories(root_, ec);
        if (ec) { why = "cannot create " + root_ + ": " + ec.message(); return {}; }
        // Leftovers of a crash or SIGKILL. 7 days, not 1: another instance's
        // long session must never be swept.
        const auto cutoff = fs::file_time_type::clock::now() - age_;
        for (fs::directory_iterator it(root_, ec), endit; !ec && it != endit; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2) && fs::last_write_time(it->path(), e2) < cutoff && !e2)
                fs::remove_all(it->path(), e2);
        }
        ec.clear();
        std::random_device rd;
        std::mt19937_64 g((static_cast<uint64_t>(rd()) << 32) ^ rd());
        char hex[17];
        snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(g()));
        const std::string dir = root_ + "/" + hex;
        fs::create_directories(dir, ec);
        if (ec) { why = "cannot create " + dir + ": " + ec.message(); return {}; }
        proc_ = dir;
    }
    const std::string slot = proc_ + "/" + std::to_string(next_++);
    fs::create_directories(slot, ec);
    if (ec) { why = "cannot create " + slot + ": " + ec.message(); return {}; }
    return slot;
}

ScratchDir& process_scratch() {
    static ScratchDir s(sdcard::default_state_dir() + "/archive-load");
    return s;
}

// ------------------------------------------------------------- ProgramArchive

bool ProgramArchive::open(const std::string& path, std::string& why, uint64_t max_archive) {
    programs_.clear();
    data_.clear();
    std::error_code ec;
    const uintmax_t sz = fs::file_size(path, ec);
    if (ec) { why = "cannot be read (" + ec.message() + ")"; return false; }
    if (sz > max_archive) {
        why = "is " + std::to_string(sz) + " bytes, over the " +
              std::to_string(max_archive) + "-byte limit";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    data_.resize(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(sz)) && sz) {
        data_.clear();
        why = "cannot be read";
        return false;
    }
    if (!reader_.open(data_.data(), data_.size(), why)) { data_.clear(); return false; }

    std::unordered_set<std::string> seen;   // linear, not a find() per member
    for (const auto& e : reader_.entries()) {
        if (e.is_directory()) continue;
        const std::string b = base_of(e.name);
        if (e.name.rfind("__MACOSX/", 0) == 0 || b.rfind("._", 0) == 0) continue;
        if (!is_program_ext(b)) continue;
        if (!seen.insert(e.name).second) {
            why = "holds two members named " + display_name(e.name);
            programs_.clear();
            data_.clear();
            return false;
        }
        programs_.push_back(e.name);
    }
    return true;
}

std::vector<std::string> ProgramArchive::member_names() const {
    std::vector<std::string> v;
    for (const auto& e : reader_.entries())
        if (!e.is_directory()) v.push_back(e.name);
    return v;
}

std::string ProgramArchive::no_program_why() const {
    const auto n = member_names();
    return "holds no program JNEXT loads (.nex, .jns, .sna, .szx, .z80, .tap, .tzx, .wav, .rzx); it holds: " +
           (n.empty() ? std::string("nothing") : join_names(n));
}

std::string ProgramArchive::several_why() const {
    return "holds " + std::to_string(programs_.size()) + " programs: " +
           join_names(programs_) + "; unzip it and load the one you want";
}

const jnext::zip::InterchangeEntry* ProgramArchive::find(const std::string& member) const {
    for (const auto& e : reader_.entries())
        if (e.name == member && !e.is_directory()) return &e;
    return nullptr;
}

bool ProgramArchive::read_member(const std::string& member, std::vector<uint8_t>& out,
                                 std::string& why) const {
    out.clear();
    if (unsafe_name(member)) {
        why = display_name(member) + " has an unsafe path (absolute or with ..)";
        return false;
    }
    const auto* e = find(member);
    if (!e) { why = display_name(member) + " is not in the archive"; return false; }
    std::string r;
    if (!reader_.read(*e, out, r)) { why = display_name(member) + " " + r; return false; }
    return true;
}

uint64_t ProgramArchive::member_size(const std::string& member) const {
    const auto* e = find(member);
    return e ? e->uncomp_size : 0;
}

bool ProgramArchive::extract(const std::string& member, ScratchDir& scratch,
                             std::string& out_path, std::string& why) const {
    out_path.clear();
    std::vector<uint8_t> bytes;
    if (!read_member(member, bytes, why)) return false;
    const std::string slot = scratch.new_slot(why);
    if (slot.empty()) return false;
    const std::string dest = slot + "/" + safe_basename(member);
    const std::string part = slot + "/.part";
    {
        std::ofstream o(part, std::ios::binary | std::ios::trunc);
        if (!o.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size())) && !bytes.empty()) {
            why = display_name(member) + " could not be written to " + slot;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(part, dest, ec);
    if (ec) { why = display_name(member) + " could not be written to " + slot + ": " + ec.message(); return false; }
    out_path = fs::absolute(dest, ec).string();
    return true;
}

std::string ProgramArchive::matching_pok(const std::string& program) const {
    std::vector<std::string> poks;
    for (const auto& n : member_names())
        if (ext_of(base_of(n)) == ".pok") poks.push_back(n);
    const std::string st = stem_of(program);
    for (const auto& n : poks)
        if (stem_of(n) == st) return n;
    return poks.size() == 1 ? poks[0] : std::string();
}

bool extract_single_program(const std::string& archive, std::string& out_path,
                            std::string& why, std::string* member) {
    ProgramArchive a;
    if (!a.open(archive, why)) return false;
    if (a.programs().empty()) { why = a.no_program_why(); return false; }
    if (a.programs().size() > 1) { why = a.several_why(); return false; }
    if (member) *member = a.programs()[0];
    return a.extract(a.programs()[0], out_path, why);
}

}  // namespace program_archive
