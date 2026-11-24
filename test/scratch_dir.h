#pragma once
// A scratch directory for a suite that builds large sparse files (GH #555): named "<prefix>-<pid>-<n>"
// under the temp directory, and, before making it, the directories of the same prefix whose process is
// gone are removed (a suite that was killed or crashed leaves them, and the temp directory is often a
// RAM-backed tmpfs). POSIX only: on Windows nothing is swept.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <csignal>
#include <cerrno>
#include <unistd.h>
#endif

inline std::filesystem::path make_scratch_dir(const std::string& prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
#ifdef _WIN32
    const long pid = static_cast<long>(_getpid());
#else
    const long pid = static_cast<long>(getpid());
    for (const auto& e : fs::directory_iterator(base, ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind(prefix + "-", 0) != 0) continue;
        const long other = std::strtol(n.c_str() + prefix.size() + 1, nullptr, 10);
        if (other > 0 && other != pid && kill(static_cast<pid_t>(other), 0) != 0 && errno == ESRCH) {
            std::error_code rec;
            fs::remove_all(e.path(), rec);
        }
    }
#endif
    const fs::path dir = base / (prefix + "-" + std::to_string(pid) + "-" +
                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    return dir;
}
