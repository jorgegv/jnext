#include "core/quick_snapshot.h"

#include "core/jns_snapshot.h"
#include "core/screenshot.h"
#include "core/sdcard_provisioner.h"

#include <filesystem>
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
