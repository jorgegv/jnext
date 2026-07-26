// Windows-native network + hash backend of the SD-card provisioner (GH #108
// Phase B): WinHTTP for the download, BCrypt/CNG for SHA-256.
//
// Chosen so the Windows packages carry no curl/OpenSSL DLLs at all — fedora's
// libcrypto-3-x64.dll statically imports PathCchRemoveFileSpec
// (api-ms-win-core-path-l1-1-0.dll, Windows 8+), which was the SINGLE import
// keeping the SDL-only bundle off Windows 7 (doc/design/WINDOWS-COMPAT-PLAN.md
// §2). WinHTTP and CNG are OS components present since long before Windows 7
// SP1: every API used here is Win7-available (WinHTTP 5.1 base API is XP
// SP2-era; BCrypt is Vista-era), and the SDL-only build pins
// _WIN32_WINNT=0x0601 so any post-Win7 API would fail to COMPILE, not load.
//
// This file implements the same three functions as the POSIX twin
// (sdcard_provisioner_net_curl.cpp): default_http_download, sha256_hex,
// sha256_file — same contracts, same error semantics (partial file removed on
// failure, ProgressFn false aborts with "download cancelled by user"). The
// whole-file #ifdef pair keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt
// correct on every platform: exactly one twin has content per build.
//
// Behaviour replicated from the curl path (only what it actually USED):
//   - follow redirects (WinHTTP sync API follows them inside
//     WinHttpSendRequest/ReceiveResponse by default; the default policy
//     refuses https->http downgrades, which is strictly safer than curl's)
//   - HTTP status >= 400 is a failure (CURLOPT_FAILONERROR)
//   - 30 s connect timeout; a 120 s receive stall aborts (curl used
//     LOW_SPEED_LIMIT 1 B/s over 120 s — same intent)
//   - "jnext-sdcard-provisioner" User-Agent
//   - server certificate validated against the OS store — WinHTTP does that
//     natively, which is exactly what the curl path needed
//     CURLSSLOPT_NATIVE_CA for on Windows
// TLS 1.2 is opted into explicitly (WINHTTP_OPTION_SECURE_PROTOCOLS): Win7's
// WinHTTP defaults to TLS 1.0, and specnext.com requires 1.2. On a Win7
// without the TLS-1.2 update (KB3140245) the option call fails and we fall
// back to OS defaults rather than refusing to try.
#ifdef _WIN32

#include "core/sdcard_provisioner.h"
#include "core/log.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

// mingw-w64 defines these in winhttp.h; the #ifndef is a guard for older
// header sets only (values are the documented Windows SDK constants).
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

namespace sdcard {

namespace {

bool file_exists_win(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// UTF-8 -> UTF-16 (WinHTTP is a wide-char API; jnext strings are UTF-8 — the
// exe embeds the activeCodePage=UTF-8 manifest, GH #62).
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &w[0], n);
    return w;
}

// Human-readable-ish WinHTTP failure: name the codes a user can act on, keep
// the rest numeric (all are documented ERROR_WINHTTP_* values).
std::string winhttp_error(const char* stage, DWORD code) {
    const char* name = nullptr;
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:   name = "name not resolved"; break;
        case ERROR_WINHTTP_CANNOT_CONNECT:      name = "cannot connect"; break;
        case ERROR_WINHTTP_TIMEOUT:             name = "timed out"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR:    name = "connection aborted"; break;
        case ERROR_WINHTTP_SECURE_FAILURE:      name = "TLS/certificate failure"; break;
        case ERROR_WINHTTP_REDIRECT_FAILED:     name = "redirect failed"; break;
        default: break;
    }
    std::string e = std::string("download failed: ") + stage;
    if (name) e += std::string(": ") + name;
    e += " (WinHTTP error " + std::to_string(code) + ")";
    return e;
}

// RAII for the three WinHTTP handles — every early return closes them.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
};

} // namespace

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err) {
    Log::emulator()->info("sdcard: downloading via WinHTTP: {}", url);

    // Same order as the curl path: fail on the output file before any network.
    FILE* out = std::fopen(dest_path.c_str(), "wb");
    if (!out) { err = "cannot create output file: " + dest_path; return false; }

    bool ok = false;
    bool aborted = false;
    {
        // Crack the URL into host / port / path(+query) / scheme.
        const std::wstring wurl = widen(url);
        std::vector<wchar_t> host(256, L'\0');
        std::vector<wchar_t> path(2048, L'\0');
        std::vector<wchar_t> extra(2048, L'\0');
        URL_COMPONENTSW uc{};
        uc.dwStructSize      = sizeof(uc);
        uc.lpszHostName      = host.data();
        uc.dwHostNameLength  = static_cast<DWORD>(host.size() - 1);
        uc.lpszUrlPath       = path.data();
        uc.dwUrlPathLength   = static_cast<DWORD>(path.size() - 1);
        uc.lpszExtraInfo     = extra.data();
        uc.dwExtraInfoLength = static_cast<DWORD>(extra.size() - 1);
        if (wurl.empty() || !WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            err = "download failed: bad URL: " + url;
            goto done;
        }
        {
        const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        std::wstring object = path.data();
        object += extra.data(); // "" when the URL has no query part
        if (object.empty()) object = L"/";

        WinHttpHandle ses;
        ses.h = WinHttpOpen(L"jnext-sdcard-provisioner",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses.h) { err = winhttp_error("open", GetLastError()); goto done; }

        // resolve / connect / send = 30 s (curl CONNECTTIMEOUT 30); a receive
        // stalled for 120 s aborts (curl LOW_SPEED_LIMIT 1 B/s over 120 s).
        WinHttpSetTimeouts(ses.h, 30000, 30000, 30000, 120000);

        if (secure) {
            DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
                           WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                           WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            // Best effort — see the file header about unpatched Win7.
            WinHttpSetOption(ses.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                             &protos, sizeof(protos));
        }

        WinHttpHandle con;
        con.h = WinHttpConnect(ses.h, host.data(), uc.nPort, 0);
        if (!con.h) { err = winhttp_error("connect", GetLastError()); goto done; }

        WinHttpHandle req;
        req.h = WinHttpOpenRequest(con.h, L"GET", object.c_str(), nullptr,
                                   WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   secure ? WINHTTP_FLAG_SECURE : 0);
        if (!req.h) { err = winhttp_error("request", GetLastError()); goto done; }

        if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr)) {
            err = winhttp_error("transfer", GetLastError());
            goto done;
        }

        // HTTP >= 400 is a failure, as with CURLOPT_FAILONERROR.
        DWORD status = 0;
        DWORD status_sz = sizeof(status);
        if (!WinHttpQueryHeaders(req.h,
                                 WINHTTP_QUERY_STATUS_CODE |
                                     WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                 &status_sz, WINHTTP_NO_HEADER_INDEX)) {
            err = winhttp_error("status", GetLastError());
            goto done;
        }
        if (status >= 400) {
            err = "download failed: HTTP status " + std::to_string(status);
            goto done;
        }

        // Content-Length (of the FINAL response, after redirects); 0 when the
        // server sent none — the ProgressFn contract for "total unknown".
        uint64_t total = 0;
        {
            wchar_t cl[64] = {0};
            DWORD cl_sz = sizeof(cl) - sizeof(wchar_t);
            if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, cl, &cl_sz,
                                    WINHTTP_NO_HEADER_INDEX)) {
                total = std::wcstoull(cl, nullptr, 10);
            }
        }

        std::vector<char> buf(1 << 16);
        uint64_t downloaded = 0;
        for (;;) {
            DWORD got = 0;
            if (!WinHttpReadData(req.h, buf.data(),
                                 static_cast<DWORD>(buf.size()), &got)) {
                err = winhttp_error("read", GetLastError());
                goto done;
            }
            if (got == 0) break; // end of body
            if (std::fwrite(buf.data(), 1, got, out) != got) {
                err = "download failed: write error on " + dest_path;
                goto done;
            }
            downloaded += got;
            if (progress && !progress(downloaded, total)) {
                aborted = true; // caller cancelled — same as curl's
                goto done;      // CURLE_ABORTED_BY_CALLBACK path
            }
        }
        ok = true;
        }
    }

done:
    if (aborted) err = "download cancelled by user";
    const bool flush_ok = (std::fflush(out) == 0);
    std::fclose(out);
    if (!ok || aborted) {
        std::remove(dest_path.c_str());
        if (err.empty()) err = "download failed";
        return false;
    }
    if (!flush_ok) {
        std::remove(dest_path.c_str());
        err = "download failed: write/flush error on " + dest_path;
        return false;
    }
    if (!file_exists_win(dest_path)) {
        err = "download produced no file: " + dest_path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SHA-256 via BCrypt/CNG (bcrypt.dll, Vista-era — Win7-safe). Explicit hash
// object buffer (BCRYPT_OBJECT_LENGTH) rather than the pass-NULL shorthand:
// the shorthand is only documented from Windows 7, the explicit form works
// everywhere.
// ---------------------------------------------------------------------------
namespace {

class BcryptSha256 {
public:
    BcryptSha256() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) != 0)
            return;
        DWORD obj_len = 0, cb = 0;
        if (BCryptGetProperty(alg_, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&obj_len),
                              sizeof(obj_len), &cb, 0) != 0 || obj_len == 0)
            return;
        obj_.resize(obj_len);
        if (BCryptCreateHash(alg_, &hash_, obj_.data(), obj_len, nullptr, 0,
                             0) != 0) {
            hash_ = nullptr;
            return;
        }
        ok_ = true;
    }
    ~BcryptSha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    bool ok() const { return ok_; }
    bool update(const void* p, size_t n) {
        if (n == 0) return true; // hashing nothing is a no-op, not an error
        return BCryptHashData(hash_,
                              reinterpret_cast<PUCHAR>(const_cast<void*>(p)),
                              static_cast<ULONG>(n), 0) == 0;
    }
    // Lowercase hex digest; "" on error. Single-shot (CNG finalises the hash).
    std::string finish_hex() {
        unsigned char md[32];
        if (BCryptFinishHash(hash_, md, sizeof(md), 0) != 0) return {};
        static const char* hexd = "0123456789abcdef";
        std::string out;
        out.reserve(sizeof(md) * 2);
        for (unsigned char b : md) {
            out.push_back(hexd[b >> 4]);
            out.push_back(hexd[b & 0x0F]);
        }
        return out;
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> obj_;
    bool ok_ = false;
};

} // namespace

std::string sha256_hex(const std::vector<uint8_t>& bytes) {
    BcryptSha256 h;
    if (!h.ok()) return {};
    if (!h.update(bytes.data(), bytes.size())) return {};
    return h.finish_hex();
}

std::string sha256_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    BcryptSha256 h;
    if (!h.ok()) return {};
    std::vector<char> buf(1 << 20);
    while (true) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0 && !h.update(buf.data(), static_cast<size_t>(n))) return {};
        if (f.bad()) return {};
        if (f.eof()) break;
    }
    return h.finish_hex();
}

} // namespace sdcard

#endif // _WIN32
