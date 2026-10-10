// Portability shims for the unit suites (GH #214): the few POSIX calls a suite
// uses for its own scaffolding, in a form that builds on Linux, macOS and
// Windows (MinGW). Only scaffolding belongs here -- a call the PRODUCT code
// makes is not hidden behind a shim; it gets a platform arm in the product.
//
//   jtp::set_env / jtp::unset_env   setenv(.., 1) / unsetenv
//   jtp::make_temp_dir(prefix)     mkdtemp: a fresh directory under the host's
//                                 temp directory ("/tmp" does not exist on a
//                                 native Windows host); "" on failure
//   jtp::make_dir(path)            mkdir(path, 0755); 0 on success
//   jtp::local_time(t, tm)         localtime_r
//   jtp::process_id_string()       getpid() as text
//   jtp::running_as_root()         geteuid() == 0 (false on Windows)
//
// Windows caveat: the CRT cannot hold an EMPTY variable, so set_env(name, "")
// removes it there; the product code under test treats unset and empty alike.
#pragma once

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace jtp {

inline int set_env(const char* name, const char* value) {
#ifdef _WIN32
    return ::_putenv_s(name, value);
#else
    return ::setenv(name, value, 1);
#endif
}

inline int unset_env(const char* name) {
#ifdef _WIN32
    return ::_putenv_s(name, "");   // an empty value removes the variable
#else
    return ::unsetenv(name);
#endif
}

inline std::string make_temp_dir(const std::string& prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) return {};
    static const char alnum[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string name = prefix;
        for (int i = 0; i < 6; ++i) name += alnum[gen() % (sizeof(alnum) - 1)];
        const fs::path p = base / name;
        // create_directory is false (no error) when it already exists: try another name.
        if (fs::create_directory(p, ec) && !ec) return p.string();
    }
    return {};
}

inline int make_dir(const std::string& path) {
#ifdef _WIN32
    return ::_mkdir(path.c_str());
#else
    return ::mkdir(path.c_str(), 0755);
#endif
}

inline bool running_as_root() {
#ifdef _WIN32
    return false;   // no uid; file permissions are not what they are on POSIX
#else
    return ::geteuid() == 0;
#endif
}

inline std::string process_id_string() {
#ifdef _WIN32
    return std::to_string(::_getpid());
#else
    return std::to_string(::getpid());
#endif
}

inline bool local_time(std::time_t t, std::tm& out) {
#ifdef _WIN32
    return ::localtime_s(&out, &t) == 0;
#else
    return ::localtime_r(&t, &out) != nullptr;
#endif
}

}  // namespace jtp
