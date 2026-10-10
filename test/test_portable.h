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
//   jtp::FullDisk                  a path whose writes fail (a full disk)
//   jtp::process_id_string()       getpid() as text
//   jtp::running_as_root()         geteuid() == 0 (false on Windows)
//
// Windows caveat: the CRT cannot hold an EMPTY variable, so set_env(name, "")
// removes it there; the product code under test treats unset and empty alike.
#pragma once

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <csignal>
#include <sys/resource.h>
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

// A path a program can open for writing but whose writes FAIL when the file is
// saved -- what a full disk does. Linux has /dev/full for it; macOS has no such
// device, so there the path is an ordinary temp file and, for the object's
// lifetime, the process's file-size limit is 0 (SIGXFSZ ignored) so that every
// write to it fails with EFBIG. While the object lives NO file of this process
// can grow -- including the harness's captured stdout and row-ID file -- so keep
// it to the statements that need it and let it die before reporting a row.
// Under wine /dev/full is wine's own mapping of the host device. (GH #214)
class FullDisk {
public:
    FullDisk() {
#ifdef _WIN32
        path_ = "/dev/full";
#else
        struct stat st;
        if (::stat("/dev/full", &st) == 0) { path_ = "/dev/full"; return; }
        path_ = (std::filesystem::temp_directory_path() /
                 ("jnext-fulldisk-" + process_id_string())).string();
        { std::ofstream touch(path_, std::ios::binary); }
        ::getrlimit(RLIMIT_FSIZE, &old_limit_);
        struct rlimit zero = {0, old_limit_.rlim_max};
        old_action_ = ::signal(SIGXFSZ, SIG_IGN);
        limited_ = ::setrlimit(RLIMIT_FSIZE, &zero) == 0;
#endif
    }
    ~FullDisk() {
#ifndef _WIN32
        if (limited_) {
            ::setrlimit(RLIMIT_FSIZE, &old_limit_);
            ::signal(SIGXFSZ, old_action_);
            std::remove(path_.c_str());
        }
#endif
    }
    FullDisk(const FullDisk&) = delete;
    FullDisk& operator=(const FullDisk&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
#ifndef _WIN32
    struct rlimit old_limit_ {};
    void (*old_action_)(int) = SIG_DFL;
    bool limited_ = false;
#endif
};

}  // namespace jtp
