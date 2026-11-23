#include "core/sdcard_backup.h"

#include "core/log.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_provisioner.h"
#include "core/sdcard_upgrade.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace sdcard::backup {

int g_fail_step_for_test = 0;

namespace {

std::string read_line(const std::string& path) {
    std::ifstream f(path);
    std::string l;
    if (f) std::getline(f, l);
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
    return l;
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    if (!f) return false;
    for (const std::string& l : lines) f << l << "\n";
    f.flush();
    return static_cast<bool>(f);
}

bool read_lines(const std::string& path, std::vector<std::string>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (!l.empty()) out.push_back(l);
    }
    return true;
}

// The files of a set, by name.
std::vector<std::string> set_names() {
    const std::string c = kFixedImageFileName, r = kImageFileName;
    return {c, c + kDistroSuffix, c + kFixedImageRecipeSuffix, c + kDeclinedSuffix, r, r + ".sha256", r + kDistroSuffix};
}

// "sn-emulator-26.11a.zip" -> {26, 11, 'a'}; false when it has no version.
bool parse_version(const std::string& name, int& major, int& minor, int& letter) {
    size_t i = name.find_first_of("0123456789");
    if (i == std::string::npos) return false;
    major = std::atoi(name.c_str() + i);
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i >= name.size() || name[i] != '.') return false;
    ++i;
    if (i >= name.size() || !std::isdigit(static_cast<unsigned char>(name[i]))) return false;
    minor = std::atoi(name.c_str() + i);
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    letter = (i < name.size() && std::islower(static_cast<unsigned char>(name[i]))) ? name[i] : 0;
    return true;
}

uint64_t tree_bytes(const fs::path& p) {
    std::error_code ec;
    uint64_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(p, ec))
        if (e.is_regular_file(ec)) total += static_cast<uint64_t>(e.file_size(ec));
    return total;
}

std::string work_dir(const std::string& dir) { return dir + "/go-back-work"; }

bool move_to(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) { err = "cannot move '" + from.string() + "' to '" + to.string() + "': " + ec.message(); return false; }
    return true;
}

struct Swap {
    std::string dir;
    std::string declined;                       ///< the distribution being left
    std::vector<std::pair<std::string, std::string>> out;   ///< overlays leaving: tag, path
};

bool parse_swap(const std::string& dir, Swap& s) {
    std::vector<std::string> lines;
    if (!read_lines(work_dir(dir) + "/SWAPPING", lines)) return false;
    s.dir = dir;
    for (const std::string& l : lines) {
        if (l.rfind("declined=", 0) == 0) s.declined = l.substr(9);
        else if (l.rfind("ov=", 0) == 0) {
            const size_t tab = l.find('\t');
            if (tab != std::string::npos) s.out.push_back({l.substr(3, tab - 3), l.substr(tab + 1)});
        }
    }
    return true;
}

bool done(const std::string& dir, int k) { return fs::exists(work_dir(dir) + "/g" + std::to_string(k) + ".done"); }
void mark(const std::string& dir, int k) { std::ofstream(work_dir(dir) + "/g" + std::to_string(k) + ".done") << "ok\n"; }

// 1. the current set and its overlays out of the way, into work/cur
bool step1(const Swap& s, std::string& err) {
    const fs::path cur = fs::path(work_dir(s.dir)) / "cur";
    std::error_code ec;
    fs::create_directories(cur / "overlays", ec);
    for (const std::string& n : set_names()) {
        const fs::path src = fs::path(s.dir) / n;
        if (fs::exists(src, ec) && !fs::exists(cur / n, ec) && !move_to(src, cur / n, err)) return false;
    }
    std::vector<std::string> list;
    for (const auto& [tag, path] : s.out) {
        if (fs::exists(path, ec) && !fs::exists(cur / "overlays" / tag, ec) && !move_to(path, cur / "overlays" / tag, err)) return false;
        list.push_back(tag + "\t" + path);
    }
    if (!write_lines((cur / "overlays" / "LIST").string(), list)) { err = "cannot write the overlay list"; return false; }
    return true;
}

// 2. the backup's set into place, its overlays back to their paths, the left distribution declined
bool step2(const Swap& s, std::string& err) {
    const fs::path prev = upgrade::previous_dir(s.dir);
    std::error_code ec;
    for (const std::string& n : set_names()) {
        if (fs::exists(prev / n, ec) && !move_to(prev / n, fs::path(s.dir) / n, err)) return false;
    }
    std::vector<std::string> incoming, list;
    read_lines((prev / "overlays" / "LIST").string(), incoming);
    for (const std::string& l : incoming) {
        const size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        const std::string tag = l.substr(0, tab), path = l.substr(tab + 1);
        if (fs::exists(prev / "overlays" / tag, ec)) {
            // The swap has begun: a target that cannot be written now (a directory that vanished, a drive that
            // is gone) must not stop it, or the set would stay half-swapped. The directory is made when it can be;
            // when the file still cannot be restored its copy stays in the backup (the new previous/), and says where.
            fs::create_directories(fs::path(path).parent_path(), ec);
            ec.clear();
            fs::copy_file(prev / "overlays" / tag, path, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                const fs::path keep = fs::path(work_dir(s.dir)) / "cur" / "overlays" / ("unrestored-" + tag);
                std::error_code mec;
                fs::create_directories(keep.parent_path(), mec);
                fs::copy_file(prev / "overlays" / tag, keep, fs::copy_options::overwrite_existing, mec);
                Log::emulator()->warn("sdcard: could not restore the writes file {} ({}); its copy is kept in {}",
                                      path, ec.message(), (fs::path(upgrade::previous_dir(s.dir)) / "overlays" / ("unrestored-" + tag)).string());
                continue;
            }
        }
        list.push_back(path);
    }
    write_lines(s.dir + "/" + upgrade::kOverlayList, list);
    const std::string card = s.dir + "/" + kFixedImageFileName;
    if (!s.declined.empty()) write_lines(card + kDeclinedSuffix, {s.declined});
    return true;
}

// 3. what is left of the old backup goes
bool step3(const Swap& s, std::string& err) {
    std::error_code ec;
    const fs::path cur = fs::path(work_dir(s.dir)) / "cur";
    if (fs::exists(cur, ec)) {
        // the report of the last upgrade goes with the backup it describes: it stays with the set that is the backup now
        const fs::path rep = fs::path(upgrade::previous_dir(s.dir)) / upgrade::kReportName;
        if (fs::exists(rep, ec) && !fs::exists(cur / upgrade::kReportName, ec))
            fs::copy_file(rep, cur / upgrade::kReportName, fs::copy_options::overwrite_existing, ec);
        fs::remove_all(upgrade::previous_dir(s.dir), ec);
    }
    if (ec) { err = "cannot remove the old backup: " + ec.message(); return false; }
    return true;
}

// 4. the set that was current is the backup now
bool step4(const Swap& s, std::string& err) {
    std::error_code ec;
    const fs::path cur = fs::path(work_dir(s.dir)) / "cur";
    if (fs::exists(cur, ec) && !move_to(cur, upgrade::previous_dir(s.dir), err)) return false;
    fs::remove_all(work_dir(s.dir), ec);
    return true;
}

bool run_steps(const Swap& s, std::string& err) {
    using Fn = bool (*)(const Swap&, std::string&);
    const Fn steps[4] = {step1, step2, step3, step4};
    for (int k = 1; k <= 4; ++k) {
        if (k < 4 && done(s.dir, k)) continue;
        if (g_fail_step_for_test == k) { g_fail_step_for_test = 0; err = "interrupted at step " + std::to_string(k) + " (test)"; return false; }
        if (!steps[k - 1](s, err)) return false;
        if (k < 4) mark(s.dir, k);
    }
    return true;
}

}  // namespace

int compare_distributions(const std::string& a, const std::string& b) {
    int am, an, al, bm, bn, bl;
    if (!parse_version(a, am, an, al) || !parse_version(b, bm, bn, bl)) return 2;
    if (am != bm) return am < bm ? -1 : 1;
    if (an != bn) return an < bn ? -1 : 1;
    if (al != bl) return al < bl ? -1 : 1;
    return 0;
}

Status card_set_status(const std::string& dir, const std::string& current_distro) {
    Status s;
    const std::string card = dir + "/" + kFixedImageFileName;
    std::error_code ec;
    s.card_present = fs::is_regular_file(card, ec);
    if (s.card_present) {
        const std::string d = read_line(card + kDistroSuffix);
        s.card_distro = d.empty() ? std::string(kLegacyDistribution) : d;
        const int c = compare_distributions(s.card_distro, current_distro);
        s.relation = c == 2 ? Relation::Unknown : c < 0 ? Relation::Older : c == 0 ? Relation::Current : Relation::Newer;
        s.pristine = carry::pristine_base_ok(card, s.pristine_why);
    }
    const fs::path prev = upgrade::previous_dir(dir);
    s.backup = fs::is_regular_file(prev / kFixedImageFileName, ec);
    if (s.backup) {
        s.backup_bytes = tree_bytes(prev);
        const std::string d = read_line((prev / (std::string(kFixedImageFileName) + kDistroSuffix)).string());
        s.backup_distro = d.empty() ? std::string(kLegacyDistribution) : d;
    }
    return s;
}

MenuState menu_state(const Status& s, bool slot0_is_default) {
    MenuState m;
    m.upgrade_enabled = slot0_is_default && s.card_present && s.relation == Relation::Older;
    m.go_back_enabled = s.backup;
    m.delete_enabled  = s.backup;
    m.backup_bytes    = s.backup ? s.backup_bytes : 0;
    return m;
}

std::string size_label(uint64_t bytes) {
    char t[32];
    if (bytes >= (1ull << 30)) std::snprintf(t, sizeof t, "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= (1ull << 20)) std::snprintf(t, sizeof t, "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else std::snprintf(t, sizeof t, "%.0f KB", static_cast<double>(bytes) / 1024.0);
    return t;
}

Result go_back(const std::string& dir, std::string& err) {
    const std::string card = dir + "/" + kFixedImageFileName;
    const fs::path prev = upgrade::previous_dir(dir);
    std::error_code ec;
    if (!fs::is_regular_file(prev / kFixedImageFileName, ec)) { err = "there is no previous SD card to go back to"; return Result::NoBackup; }
    if (fs::exists(work_dir(dir), ec)) { err = "an earlier go-back is unfinished: start jnext once to finish it"; return Result::Failed; }
    // Every writes file the backup holds is restored to the path it came from. That is checked BEFORE anything
    // moves: the directory is made when it is missing, and a path nothing can be written to refuses the whole
    // go back with the set exactly as it was.
    {
        std::vector<std::string> incoming;
        read_lines((prev / "overlays" / "LIST").string(), incoming);
        for (const std::string& l : incoming) {
            const size_t tab = l.find('\t');
            if (tab == std::string::npos) continue;
            const std::string path = l.substr(tab + 1);
            std::error_code pec;
            fs::create_directories(fs::path(path).parent_path(), pec);
            const std::string probe = path + ".jnext-probe";
            bool ok;
            {
                std::ofstream f(probe, std::ios::binary | std::ios::trunc);
                ok = static_cast<bool>(f);
            }
            fs::remove(probe, pec);
            if (!ok) {
                err = "cannot restore the writes file '" + path + "' (its folder cannot be written to); nothing was changed";
                return Result::Failed;
            }
        }
    }
    Swap s;
    s.dir = dir;
    const std::string d = read_line(card + kDistroSuffix);
    s.declined = fs::is_regular_file(card, ec) ? (d.empty() ? std::string(kLegacyDistribution) : d) : std::string();
    // The overlays that belong to the current card leave with it.
    int i = 0;
    for (const std::string& o : upgrade::usable_overlays(dir, card, {}))
        s.out.push_back({std::to_string(i++) + "-" + fs::path(o).filename().string(), o});
    fs::create_directories(work_dir(dir), ec);
    std::vector<std::string> lines = {"declined=" + s.declined};
    for (const auto& [tag, path] : s.out) lines.push_back("ov=" + tag + "\t" + path);
    if (!write_lines(work_dir(dir) + "/SWAPPING.tmp", lines) ||
        !move_to(work_dir(dir) + "/SWAPPING.tmp", work_dir(dir) + "/SWAPPING", err)) {
        fs::remove_all(work_dir(dir), ec);
        if (err.empty()) err = "cannot write the swap plan";
        return Result::Failed;
    }
    if (!run_steps(s, err)) return Result::Failed;
    Log::emulator()->info("sdcard: went back to the previous SD card ({})", read_line(card + kDistroSuffix));
    return Result::Ok;
}

Result delete_backup(const std::string& dir, std::string& err) {
    const fs::path prev = upgrade::previous_dir(dir);
    std::error_code ec;
    if (!fs::exists(prev, ec)) { err = "there is no previous SD card backup to delete"; return Result::NoBackup; }
    fs::remove_all(prev, ec);
    if (ec) { err = "cannot delete " + prev.string() + ": " + ec.message(); return Result::Failed; }
    return Result::Ok;
}

bool recover(const std::string& dir, std::string& err) {
    std::error_code ec;
    if (!fs::exists(work_dir(dir), ec)) return true;
    Swap s;
    if (!parse_swap(dir, s)) {
        // no plan: nothing was moved yet (the plan is written before step 1)
        fs::remove_all(work_dir(dir), ec);
        return true;
    }
    Log::emulator()->info("sdcard: finishing an interrupted go-back");
    return run_steps(s, err);
}

}  // namespace sdcard::backup
