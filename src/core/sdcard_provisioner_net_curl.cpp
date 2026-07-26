// POSIX (Linux/macOS) network + hash backend of the SD-card provisioner:
// libcurl for the download, OpenSSL EVP for SHA-256. Moved verbatim out of
// sdcard_provisioner.cpp in GH #108 Phase B, which gave Windows its own
// OS-native twin (sdcard_provisioner_net_win.cpp: WinHTTP + BCrypt) so the
// Windows packages ship no curl/OpenSSL DLLs — fedora's libcrypto-3-x64.dll
// imports PathCchRemoveFileSpec (Windows 8+), which was the single import
// keeping the SDL-only bundle off Windows 7.
//
// Both twins implement the same three functions declared in
// sdcard_provisioner.h with identical contracts (partial file removed on
// failure, ProgressFn false aborts with "download cancelled by user"). The
// whole-file #ifdef pair keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt
// correct on every platform: exactly one twin has content per build.
#ifndef _WIN32

#include "core/sdcard_provisioner.h"
#include "core/log.h"

#include <curl/curl.h>
#include <openssl/evp.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace sdcard {

// ---------------------------------------------------------------------------
// Download (default impl uses libcurl — no shell-out)
// ---------------------------------------------------------------------------
namespace {
bool file_exists_posix(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// libcurl write callback: append received bytes to the destination FILE*.
size_t curl_write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* f = static_cast<FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, f);
}

// libcurl progress (xferinfo) callback context: wraps the caller's ProgressFn.
struct CurlProgressCtx {
    const ProgressFn* fn = nullptr;
    bool aborted = false;
};
// CURLOPT_XFERINFOFUNCTION: forward (dlnow, dltotal) to the ProgressFn.
// Returning nonzero makes curl abort the transfer with
// CURLE_ABORTED_BY_CALLBACK, which we surface as a download failure.
int curl_xferinfo(void* p, curl_off_t dltotal, curl_off_t dlnow,
                  curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<CurlProgressCtx*>(p);
    if (ctx && ctx->fn && *ctx->fn) {
        const bool keep_going = (*ctx->fn)(static_cast<uint64_t>(dlnow),
                                           static_cast<uint64_t>(dltotal));
        if (!keep_going) { ctx->aborted = true; return 1; }
    }
    return 0;
}
} // namespace

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err) {
    Log::emulator()->info("sdcard: downloading via libcurl: {}", url);

    FILE* out = std::fopen(dest_path.c_str(), "wb");
    if (!out) { err = "cannot create output file: " + dest_path; return false; }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(out);
        std::remove(dest_path.c_str());
        err = "curl_easy_init failed";
        return false;
    }

    CurlProgressCtx prog_ctx;
    prog_ctx.fn = progress ? &progress : nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // follow redirects
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);    // HTTP >= 400 -> error
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_ctx);
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

    const bool flush_ok = (std::fflush(out) == 0);
    std::fclose(out);

    if (rc != CURLE_OK) {
        std::remove(dest_path.c_str());
        if (rc == CURLE_ABORTED_BY_CALLBACK || prog_ctx.aborted)
            err = "download cancelled by user";
        else
            err = std::string("download failed: ") + curl_easy_strerror(rc);
        return false;
    }
    if (!flush_ok) {
        std::remove(dest_path.c_str());
        err = "download failed: write/flush error on " + dest_path;
        return false;
    }
    if (!file_exists_posix(dest_path)) {
        err = "download produced no file: " + dest_path;
        return false;
    }
    return true;
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
