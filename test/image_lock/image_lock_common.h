// Shared scaffolding of the image-lock suites (GH #555): the row helper and a scratch directory.
#pragma once

#include "../row_id.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <string>

namespace imglock_test {

namespace fs = std::filesystem;

inline int g_pass = 0, g_fail = 0, g_total = 0;

inline void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

// A fresh scratch directory per run: <tmp>/jnext-imglock-<name>-<clock>.
inline std::string scratch(const char* name) {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path p = fs::temp_directory_path() / (std::string("jnext-imglock-") + name + "-" + std::to_string(t));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}

// Point the state dir (where sd_locks::hold puts its lock files) at a scratch directory, so a
// suite never writes to the developer's own ~/.jnext.
inline void use_state_dir(const std::string& dir) {
#ifdef _WIN32
    _putenv_s("JNEXT_CONFIG_DIR", dir.c_str());
#else
    setenv("JNEXT_CONFIG_DIR", dir.c_str(), 1);
#endif
}

inline int finish(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}

}  // namespace imglock_test
