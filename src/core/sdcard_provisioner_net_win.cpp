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
// sha256_file — same contracts, same error semantics (the partial-file
// contract documented in sdcard_provisioner.h: `<dest>.part` kept for
// resuming, ProgressFn false aborts with "cancelled by user"). The decisions
// (append / truncate / retry, the record, the keep-or-remove rule) are
// PartialDownload's, shared with the curl twin. The
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
    std::string e = stage;
    if (name) e += std::string(": ") + name;
    e += " (WinHTTP error " + std::to_string(code) + ")";
    return e;
}

// RAII for the three WinHTTP handles — every early return closes them.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
};

// A response header as a narrow string ("" when the response has none).
std::string query_header(HINTERNET req, DWORD what) {
    wchar_t buf[1024] = {0};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, buf, &sz, WINHTTP_NO_HEADER_INDEX))
        return {};
    std::string out;
    for (const wchar_t* p = buf; *p; ++p) out.push_back(*p < 128 ? static_cast<char>(*p) : '?');
    return out;
}

enum class Outcome { Done, Retry, Failed };

Outcome attempt_download(const std::string& url, const std::string& dest_path, const ProgressFn& progress,
                         bool allow_resume, std::string& err) {
    PartialDownload part(url, dest_path, allow_resume);
    if (part.complete()) {   // only its rename was missing: no request (GH #358)
        if (!part.finish(err)) return Outcome::Failed;
        if (!regular_file_exists(dest_path)) { err = "produced no file: " + dest_path; return Outcome::Failed; }
        return Outcome::Done;
    }

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
        err = "bad URL: " + url;
        part.fail(err, false);
        return Outcome::Failed;
    }
    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring object = path.data();
    object += extra.data(); // "" when the URL has no query part
    if (object.empty()) object = L"/";

    // From here every failure is a transport one: settle the partial (kept
    // when it holds bytes and a validator) and report a bare reason.
    auto transport_fail = [&](const std::string& why, bool clean_eof) {
        err = why;
        part.fail(err, clean_eof);
        return Outcome::Failed;
    };

    WinHttpHandle ses;
    ses.h = WinHttpOpen(L"jnext-sdcard-provisioner", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses.h) return transport_fail(winhttp_error("open", GetLastError()), false);

    // resolve / connect / send = 30 s (curl CONNECTTIMEOUT 30); a receive
    // stalled for 120 s aborts (curl LOW_SPEED_LIMIT 1 B/s over 120 s).
    WinHttpSetTimeouts(ses.h, 30000, 30000, 30000, 120000);

    if (secure) {
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                       WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        // Best effort — see the file header about unpatched Win7.
        WinHttpSetOption(ses.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
    }

    WinHttpHandle con;
    con.h = WinHttpConnect(ses.h, host.data(), uc.nPort, 0);
    if (!con.h) return transport_fail(winhttp_error("connect", GetLastError()), false);

    WinHttpHandle req;
    req.h = WinHttpOpenRequest(con.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) return transport_fail(winhttp_error("request", GetLastError()), false);

    // Resuming: ask for the rest, and only if the file is still the one we
    // started (If-Range). The added headers go to a redirect's target too.
    std::wstring extra_headers;
    if (part.ranged())
        extra_headers = widen("Range: " + part.range_value() + "\r\nIf-Range: " + part.if_range_value() + "\r\n");
    if (!WinHttpSendRequest(req.h, extra_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra_headers.c_str(),
                            extra_headers.empty() ? 0 : static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr))
        return transport_fail(winhttp_error("transfer", GetLastError()), false);

    HttpResponseInfo resp;
    {
        DWORD status = 0;
        DWORD status_sz = sizeof(status);
        if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX))
            return transport_fail(winhttp_error("status", GetLastError()), false);
        resp.status = static_cast<long>(status);
        resp.content_range = query_header(req.h, WINHTTP_QUERY_CONTENT_RANGE);
        resp.etag          = query_header(req.h, WINHTTP_QUERY_ETAG);
        resp.last_modified = query_header(req.h, WINHTTP_QUERY_LAST_MODIFIED);
        const std::string cl = query_header(req.h, WINHTTP_QUERY_CONTENT_LENGTH);
        if (!cl.empty()) {
            resp.has_content_length = true;
            resp.content_length = std::strtoull(cl.c_str(), nullptr, 10);
        }
    }
    switch (part.begin(resp, err)) {
        case PartialDownload::Begin::Retry: return Outcome::Retry;
        case PartialDownload::Begin::Error: return Outcome::Failed;   // begin() settled the partial
        case PartialDownload::Begin::Ok:    break;
    }

    std::vector<char> buf(1 << 16);
    uint64_t got_total = 0;
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), static_cast<DWORD>(buf.size()), &got))
            return transport_fail(winhttp_error("read", GetLastError()), false);
        if (got == 0) break; // end of body
        if (!part.write(buf.data(), got)) return transport_fail("write error on " + partial_path(dest_path), false);
        got_total += got;
        if (!part.report(progress, got_total)) {
            part.cancelled();   // caller cancelled — same as curl's abort path
            err = "cancelled by user";
            return Outcome::Failed;
        }
    }
    if (!part.finish(err)) return Outcome::Failed;
    if (!regular_file_exists(dest_path)) {
        err = "produced no file: " + dest_path;
        return Outcome::Failed;
    }
    return Outcome::Done;
}

} // namespace

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err) {
    Log::emulator()->info("sdcard: downloading via WinHTTP: {}", url);
    // At most two attempts: the first may ask for the rest of a partial; if the
    // server refuses that (416, or a 206 that is not the range asked for) the
    // second is a plain GET. Never a loop.
    for (int attempt = 0; attempt < 2; ++attempt) {
        err.clear();
        const Outcome o = attempt_download(url, dest_path, progress, attempt == 0, err);
        if (o == Outcome::Done) return true;
        if (o == Outcome::Failed) return false;
    }
    err = "the server keeps refusing a plain request";
    return false;
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
