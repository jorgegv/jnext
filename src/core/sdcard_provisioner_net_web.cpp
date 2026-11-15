// WebAssembly (Emscripten) backend of the SD-card provisioner: no download (a
// browser build never downloads the SD image itself; the page supplies it) and
// a portable SHA-256 (core/sha256_stream.h), because there is no OpenSSL and no BCrypt.
//
// It implements the same four functions as the POSIX and Windows twins
// (sdcard_provisioner_net_curl.cpp, sdcard_provisioner_net_win.cpp):
// http_download, transport_reason, sha256_hex, sha256_file, with the contracts
// documented in sdcard_provisioner.h and sha256.h: a download that fails says so
// with a bare single-prefix reason, sha256_* return lowercase hex and "" on
// error (an unreadable path, and a directory, JNSI-P26b). The whole-file #ifdef
// keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt correct on every
// platform: exactly one of the three twins has content per build.
#ifdef __EMSCRIPTEN__

#include "core/sdcard_provisioner.h"
#include "core/sha256_stream.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sdcard {

bool http_download(const std::string&, const std::string&, const ProgressFn&,
                   std::string& err, const DownloadTimings&) {
    err = "downloads are not available in the browser build";
    return false;
}

std::string transport_reason(long) {
    return "no network access in the browser build";
}

std::string sha256_hex(const std::vector<uint8_t>& bytes) {
    Sha256Stream s;
    s.update(bytes.data(), bytes.size());
    return s.final_hex();
}

std::string sha256_file(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return {};   // JNSI-P26b
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    Sha256Stream s;
    std::vector<uint8_t> buf(1 << 20);
    for (;;) {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) s.update(buf.data(), static_cast<size_t>(n));
        if (f.bad()) return {};
        if (f.eof()) break;
        if (f.fail()) return {};
    }
    return s.final_hex();
}

}  // namespace sdcard

#endif  // __EMSCRIPTEN__
