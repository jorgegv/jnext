// POSIX (Linux/macOS) network + hash backend of the SD-card provisioner:
// libcurl for the download, OpenSSL EVP for SHA-256. Moved verbatim out of
// sdcard_provisioner.cpp in GH #108 Phase B, which gave Windows its own
// OS-native twin (sdcard_provisioner_net_win.cpp: WinHTTP + BCrypt) so the
// Windows packages ship no curl/OpenSSL DLLs — fedora's libcrypto-3-x64.dll
// imports PathCchRemoveFileSpec (Windows 8+), which was the single import
// keeping the SDL-only bundle off Windows 7.
//
// Both twins implement the same three functions declared in
// sdcard_provisioner.h with identical contracts (the partial-file contract
// documented there: `<dest>.part` kept for resuming, ProgressFn false aborts
// with "cancelled by user"). The
// whole-file #ifdef pair keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt
// correct on every platform: exactly one twin has content per build.
#ifndef _WIN32

#include "core/sdcard_provisioner.h"
#include "core/log.h"

#include <curl/curl.h>
#include <openssl/evp.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sdcard {

// ---------------------------------------------------------------------------
// Download (default impl uses libcurl — no shell-out)
// ---------------------------------------------------------------------------
namespace {
// One libcurl attempt's state. The FILE is opened lazily, at the first body
// byte, by PartialDownload::begin(), which decides from the headers of the LAST
// response (redirects send several) whether to append, truncate or retry.
struct CurlAttempt {
    PartialDownload* part = nullptr;
    const ProgressFn* fn = nullptr;
    HttpResponseInfo resp;
    std::string err;          // set when begin() refused the response
    bool retry = false;       // begin() asked for one plain GET
    bool aborted = false;     // the caller cancelled
    bool write_failed = false;
    uint64_t got = 0;         // body bytes of THIS response
};

// Returns false when the transfer must stop (retry / error already recorded).
bool begin_once(CurlAttempt& a) {
    if (a.part->begun()) return true;
    switch (a.part->begin(a.resp, a.err)) {
        case PartialDownload::Begin::Ok:    return true;
        case PartialDownload::Begin::Retry: a.retry = true; return false;
        default:                            return false;
    }
}

size_t curl_header(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* a = static_cast<CurlAttempt*>(userdata);
    parse_response_line(std::string(ptr, size * nmemb), a->resp);
    return size * nmemb;
}

size_t curl_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* a = static_cast<CurlAttempt*>(userdata);
    if (!begin_once(*a)) return 0;   // 0 aborts the transfer (CURLE_WRITE_ERROR)
    if (!a->part->write(ptr, size * nmemb)) { a->write_failed = true; return 0; }
    a->got += size * nmemb;
    return size * nmemb;
}

// CURLOPT_XFERINFOFUNCTION: the caller's ProgressFn gets the bytes already on
// disk added to this response's; returning nonzero aborts the transfer.
int curl_xferinfo(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* a = static_cast<CurlAttempt*>(p);
    if (a->fn && *a->fn && !a->part->report(*a->fn, a->got)) { a->aborted = true; return 1; }
    return 0;
}

enum class Outcome { Done, Retry, Failed };

Outcome attempt_download(const std::string& url, const std::string& dest, const ProgressFn& progress,
                         bool allow_resume, std::string& err) {
    PartialDownload part(url, dest, allow_resume);
    if (part.complete()) {   // only its rename was missing: no request (GH #358)
        if (!part.finish(err)) return Outcome::Failed;
        if (!regular_file_exists(dest)) { err = "produced no file: " + dest; return Outcome::Failed; }
        return Outcome::Done;
    }
    CurlAttempt a;
    a.part = &part;
    a.fn = progress ? &progress : nullptr;

    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return Outcome::Failed; }
    struct curl_slist* headers = nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // follow redirects
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);    // HTTP >= 400 -> error
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &a);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &a);
    // NOT CURLOPT_RESUME_FROM_LARGE: it turns a server's 200 (range ignored)
    // into CURLE_RANGE_ERROR and loses the transfer. CURLOPT_RANGE hands a 200
    // back as is, and PartialDownload::begin() decides what to do with it.
    std::string range;
    if (part.ranged()) {
        range = part.range_value().substr(6);   // "bytes=N-" -> "N-"
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        headers = curl_slist_append(headers, ("If-Range: " + part.if_range_value()).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &a);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);   // abort if the
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);  // stream stalls
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "jnext-sdcard-provisioner");
    // TLS verification stays at libcurl defaults (peer + host on).

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (rc == CURLE_OK && !part.begun()) begin_once(a);   // an empty body never called write
    if (a.retry) return Outcome::Retry;
    if (rc != CURLE_OK && http_code == 416 && part.ranged()) {
        discard_partial(dest);
        return Outcome::Retry;
    }
    if (!a.err.empty()) { err = a.err; return Outcome::Failed; }   // begin() refused (it settled the partial)
    if (a.aborted || rc == CURLE_ABORTED_BY_CALLBACK) {
        part.cancelled();
        err = "cancelled by user";
        return Outcome::Failed;
    }
    if (a.write_failed) {
        err = "write error on " + partial_path(dest);
        part.fail(err, false);
        return Outcome::Failed;
    }
    if (rc != CURLE_OK) {
        err = curl_easy_strerror(rc);
        part.fail(err, rc == CURLE_PARTIAL_FILE);
        return Outcome::Failed;
    }
    if (!part.finish(err)) return Outcome::Failed;
    if (!regular_file_exists(dest)) {
        err = "produced no file: " + dest;
        return Outcome::Failed;
    }
    return Outcome::Done;
}
} // namespace

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err) {
    Log::emulator()->info("sdcard: downloading via libcurl: {}", url);
    // At most two attempts: the first may ask for the rest of a partial; if the
    // server refuses that (416, or a 206 that is not the range asked for) the
    // second is a plain GET. Never a loop, so a misbehaving server is asked twice.
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
// SHA-256 via OpenSSL EVP
// ---------------------------------------------------------------------------
std::string sha256_hex(const std::vector<uint8_t>& bytes) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), md, &md_len, EVP_sha256(),
                   nullptr) != 1)
        return {};
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(md_len * 2);
    for (unsigned int i = 0; i < md_len; ++i) {
        out.push_back(hexd[md[i] >> 4]);
        out.push_back(hexd[md[i] & 0x0F]);
    }
    return out;
}

std::string sha256_file(const std::string& path) {
    // A directory opens as an ifstream on macOS (libc++) and reads as an empty
    // file instead of failing, which digested to the SHA-256 of nothing; Linux
    // fails the read. Refuse it everywhere (GH #214, JNSI-P26b).
    {
        std::error_code dir_ec;
        if (std::filesystem::is_directory(path, dir_ec)) return {};
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    std::string result;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) {
        std::vector<char> buf(1 << 20);
        bool ok = true;
        while (true) {
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = f.gcount();
            if (n > 0 &&
                EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) != 1) {
                ok = false; break;
            }
            if (f.bad()) { ok = false; break; }
            if (f.eof()) break;
        }
        if (ok) {
            unsigned char md[EVP_MAX_MD_SIZE];
            unsigned int md_len = 0;
            if (EVP_DigestFinal_ex(ctx, md, &md_len) == 1) {
                static const char* hexd = "0123456789abcdef";
                result.reserve(md_len * 2);
                for (unsigned int i = 0; i < md_len; ++i) {
                    result.push_back(hexd[md[i] >> 4]);
                    result.push_back(hexd[md[i] & 0x0F]);
                }
            }
        }
    }
    EVP_MD_CTX_free(ctx);
    return result;
}

} // namespace sdcard

#endif // !_WIN32
