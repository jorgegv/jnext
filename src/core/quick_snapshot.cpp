#include "core/quick_snapshot.h"

#include "core/jns_snapshot.h"
#include "core/screenshot.h"
#include "core/sdcard_provisioner.h"

#include "core/log.h"

#include <algorithm>
#include <filesystem>
#include <vector>
#include <system_error>

namespace fs = std::filesystem;

std::string default_quick_snapshot_dir()
{
    return sdcard::default_state_dir() + "/snapshots";
}

std::string auto_quick_snapshot_path(const std::string& dir, std::time_t when)
{
    return auto_timestamped_path(dir, ".jns", when);
}

std::string latest_quick_snapshot(const std::string& dir, std::string& why)
{
    why = "no snapshot in '" + dir + "'";
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return {};

    std::string best;
    std::string best_stem;
    fs::file_time_type best_time{};
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        if (!it->is_regular_file(e2) || e2) continue;
        const std::string name = it->path().filename().string();
        if (!jnext::is_jns_path(name)) continue;
        const fs::file_time_type t = it->last_write_time(e2);
        if (e2) continue;
        const std::string stem = it->path().stem().string();
        if (best.empty() || t > best_time || (t == best_time && stem > best_stem)) {
            best      = it->path().string();
            best_stem = stem;
            best_time = t;
        }
    }
    if (!best.empty()) why.clear();
    return best;
}

bool is_quick_snapshot_name(const std::string& n)
{
    // jnext-YYYYMMDD-HHMMSS.jns (25 chars) or jnext-YYYYMMDD-HHMMSS-NN.jns (28)
    auto digits = [&](size_t from, size_t count) {
        if (from + count > n.size()) return false;
        for (size_t i = from; i < from + count; ++i)
            if (n[i] < '0' || n[i] > '9') return false;
        return true;
    };
    if (n.compare(0, 6, "jnext-") != 0 || !digits(6, 8) || n.size() < 15 || n[14] != '-' ||
        !digits(15, 6))
        return false;
    if (n.size() == 25) return n.compare(21, 4, ".jns") == 0;
    if (n.size() == 28) return n[21] == '-' && digits(22, 2) && n.compare(24, 4, ".jns") == 0;
    return false;
}

int prune_quick_snapshots(const std::string& dir, int keep)
{
    if (keep <= 0) return 0;
    struct E { fs::file_time_type t; std::string stem; fs::path path; };
    std::vector<E> files;
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return 0;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        if (!it->is_regular_file(e2) || e2) continue;
        if (!is_quick_snapshot_name(it->path().filename().string())) continue;
        const fs::file_time_type t = it->last_write_time(e2);
        if (e2) continue;
        files.push_back({t, it->path().stem().string(), it->path()});
    }
    if (static_cast<int>(files.size()) <= keep) return 0;
    // Newest first; the surplus is the tail.
    std::sort(files.begin(), files.end(), [](const E& a, const E& b) {
        return a.t != b.t ? a.t > b.t : a.stem > b.stem;
    });
    int deleted = 0;
    for (size_t i = static_cast<size_t>(keep); i < files.size(); ++i) {
        std::error_code e3;
        if (fs::remove(files[i].path, e3) && !e3) {
            ++deleted;
        } else {
            Log::platform()->warn("quick snapshot: could not delete old '{}'",
                                  files[i].path.string());
        }
    }
    if (deleted) Log::platform()->info("quick snapshot: pruned {} old snapshot(s), keeping {}",
                                       deleted, keep);
    return deleted;
}
