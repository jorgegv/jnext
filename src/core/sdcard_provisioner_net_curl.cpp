// POSIX (Linux/macOS) network + hash backend of the SD-card provisioner:
// libcurl for the download, OpenSSL EVP for SHA-256. Moved verbatim out of
// sdcard_provisioner.cpp in GH #108 Phase B, which gave Windows its own
// OS-native twin (sdcard_provisioner_net_win.cpp: WinHTTP + BCrypt) so the
// Windows packages ship no curl/OpenSSL DLLs — fedora's libcrypto-3-x64.dll
// imports PathCchRemoveFileSpec (Windows 8+), which was the single import
// keeping the SDL-only bundle off Windows 7.
//
// Both twins implement the same four functions declared in
// sdcard_provisioner.h (http_download, transport_reason, sha256_hex, sha256_file)
// with identical contracts: the failure wording documented there (transport_reason
// sorts the CURLcode into the same kinds the WinHTTP twin sorts its codes into, so
// both say the same words; an HTTP error status says the same through
// http_status_reason; only this twin names a proxy that refuses the tunnel), the
// partial-file contract documented there (`<dest>.part` kept for resuming) and the
// tick / stall / cancel one (the ProgressFn is called at least every tick whatever
// the network does; false aborts with "cancelled by user"; no body byte for the
// stall timeout ends the attempt). The whole-file #ifdef pair keeps
// file(GLOB_RECURSE) in src/core/CMakeLists.txt correct on every platform: exactly
// one twin has content per build.
//
// The transfer runs on libcurl's MULTI interface. curl_easy_perform with an
// XFERINFO callback is called often while data flows but slows to about one call
// a second when nothing arrives (libcurl's documentation of
// CURLOPT_XFERINFOFUNCTION; measured here: 1.00 s gaps through a 15 s stall), so
// the window repainted at about 1 Hz through a stall and nothing named it. Here
// the loop wakes every tick. (A file:// URL, a test seam and the Flatpak build's
// source, is read by libcurl inside one curl_multi_perform call: no ticks,
// nothing to cancel, and it needs none.)
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
// What a transport failure says (GH #385): the CURLcode sorted into the shared
// kinds of net_reason, so libcurl and WinHTTP say the same words. The meanings
// are libcurl-errors(3); only codes libcurl 7.66 already has are named, because
// the packaged targets include older distributions.
// ---------------------------------------------------------------------------
namespace {
NetFailure curl_kind(CURLcode c) {
    switch (c) {
    case CURLE_UNSUPPORTED_PROTOCOL:
    case CURLE_URL_MALFORMAT:
        return NetFailure::BadAddress;
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:
        return NetFailure::NameNotFound;
    case CURLE_COULDNT_CONNECT:
        return NetFailure::CannotConnect;
    case CURLE_GOT_NOTHING:
        return NetFailure::NoAnswer;
    case CURLE_HTTP2:
    case CURLE_PARTIAL_FILE:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_HTTP2_STREAM:
        return NetFailure::ConnectionLost;
    case CURLE_OPERATION_TIMEDOUT:
        return NetFailure::TimedOut;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
        return NetFailure::Tls;
    case CURLE_TOO_MANY_REDIRECTS:
        return NetFailure::Redirect;
    default:
        return NetFailure::Other;
    }
}
} // namespace

namespace {
// libcurl's own code and text, the part of a reason that follows the plain words.
std::string curl_raw(CURLcode c) {
    return "curl error " + std::to_string(static_cast<int>(c)) + ": " + curl_easy_strerror(c);
}
} // namespace

std::string transport_reason(long code) {
    const CURLcode c = static_cast<CURLcode>(code);
    return net_reason(curl_kind(c), curl_raw(c));
}

namespace {
// The reason of a transfer libcurl reported as failed (GH #385). CURLOPT_FAILONERROR turns an HTTP
// status of 400 or more into one generic code that loses the status, so it is read off the transfer:
// a 404 says 404, like WinHTTP. A proxy that will not open the tunnel (its answer to CONNECT is not a
// 2xx) leaves the transfer with no response status of its own, and the code libcurl gives for it
// depends on the answer (8.18 measured: 22 for a 4xx or 5xx, 56 for a 3xx); the proxy's answer is the
// reason, whichever code came with it. `proxy_code` is CURLINFO_HTTP_CONNECTCODE, 0 when there was none.
// Codes 1 (unsupported protocol) and 3 (malformed URL) are about the ADDRESS only when libcurl never
// got as far as connecting (`connects`, CURLINFO_NUM_CONNECTS, is 0); after a connection they are about
// what the server sent, a reply that is not HTTP or a redirect to something libcurl cannot use, and
// saying "the address is not a valid http or https URL" of a good one would be untrue.
std::string failure_reason(CURLcode rc, long http_code, long proxy_code, long connects) {
    if (rc == CURLE_HTTP_RETURNED_ERROR && http_code >= 400) return http_status_reason(http_code);
    if (proxy_code >= 300)
        return net_reason(NetFailure::CannotConnect,
                          "the proxy answered HTTP " + std::to_string(proxy_code) + " to the connection request");
    if ((rc == CURLE_UNSUPPORTED_PROTOCOL || rc == CURLE_URL_MALFORMAT) && connects > 0)
        return net_reason(NetFailure::Other, curl_raw(rc));
    return transport_reason(rc);
}
} // namespace

// ---------------------------------------------------------------------------
// Download (default impl uses libcurl — no shell-out)
// ---------------------------------------------------------------------------
namespace {
// One libcurl attempt's state. The FILE is opened lazily, at the first body
// byte, by PartialDownload::begin(), which decides from the headers of the LAST
// response (redirects send several) whether to append, truncate or retry.
struct CurlAttempt {
    PartialDownload* part = nullptr;
    HttpResponseInfo resp;
    std::string err;          // set when begin() refused the response
    bool retry = false;       // begin() asked for one plain GET
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

enum class Outcome { Done, Retry, Failed };

Outcome attempt_download(const std::string& url, const std::string& dest, const ProgressFn& progress,
                         bool allow_resume, const DownloadTimings& timings, std::string& err) {
    PartialDownload part(url, dest, allow_resume);
    if (part.complete()) {   // only its rename was missing: no request (GH #358)
        if (!part.finish(err)) return Outcome::Failed;
        if (!regular_file_exists(dest)) { err = "produced no file: " + dest; return Outcome::Failed; }
        return Outcome::Done;
    }
    CurlAttempt a;
    a.part = &part;

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
    // No progress callback and no LOW_SPEED_* abort: the loop below owns both
    // (download_step: the ProgressFn each tick, and the stall timeout).
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "jnext-sdcard-provisioner");
    // TLS verification stays at libcurl defaults (peer + host on).

    CURLM* multi = curl_multi_init();
    if (!multi) {
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        err = "curl_multi_init failed";
        return Outcome::Failed;
    }
    std::string multi_err;   // curl's multi interface itself failed
    if (const CURLMcode mc = curl_multi_add_handle(multi, curl); mc != CURLM_OK) multi_err = curl_multi_strerror(mc);

    // Wake every tick, data or not. download_step() reports to the ProgressFn,
    // honours its false, and ends the attempt once no body byte has arrived for
    // the stall timeout (counted from here, then from the last byte).
    StallWatch watch;
    bool stopped = false;    // download_step() ended the attempt: err set, partial settled
    int running = 1;
    while (multi_err.empty()) {
        CURLMcode mc = curl_multi_perform(multi, &running);
        if (mc != CURLM_OK) { multi_err = curl_multi_strerror(mc); break; }
        if (!download_step(part, progress, a.got, watch, timings, err)) { stopped = true; break; }
        if (!running) break;
        mc = curl_multi_poll(multi, nullptr, 0, timings.tick_ms, nullptr);
        if (mc != CURLM_OK) { multi_err = curl_multi_strerror(mc); break; }
    }

    CURLcode rc = CURLE_OK;
    if (!stopped && multi_err.empty()) {
        int queued = 0;
        while (const CURLMsg* msg = curl_multi_info_read(multi, &queued))
            if (msg->msg == CURLMSG_DONE) rc = msg->data.result;
    }
    long http_code = 0, proxy_code = 0, connects = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_getinfo(curl, CURLINFO_HTTP_CONNECTCODE, &proxy_code);   // an HTTP proxy's answer to CONNECT
    curl_easy_getinfo(curl, CURLINFO_NUM_CONNECTS, &connects);         // connections made (none is reused: one handle per attempt)
    curl_multi_remove_handle(multi, curl);   // an unfinished transfer: its connection is closed
    curl_easy_cleanup(curl);
    curl_multi_cleanup(multi);
    curl_slist_free_all(headers);

    if (stopped) return Outcome::Failed;
    if (!multi_err.empty()) {
        err = multi_err;
        part.fail(err, false);
        return Outcome::Failed;
    }

    if (rc == CURLE_OK && !part.begun()) begin_once(a);   // an empty body never called write
    if (a.retry) return Outcome::Retry;
    if (rc != CURLE_OK && http_code == 416 && part.ranged()) {
        discard_partial(dest);
        return Outcome::Retry;
    }
    if (!a.err.empty()) { err = a.err; return Outcome::Failed; }   // begin() refused (it settled the partial)
    if (a.write_failed) {
        err = "write error on " + partial_path(dest);
        part.fail(err, false);
        return Outcome::Failed;
    }
    if (rc != CURLE_OK) {
        err = failure_reason(rc, http_code, proxy_code, connects);
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

bool http_download(const std::string& url, const std::string& dest_path,
                   const ProgressFn& progress, std::string& err, const DownloadTimings& timings) {
    Log::emulator()->info("sdcard: downloading via libcurl: {}", url);
    // At most two attempts: the first may ask for the rest of a partial; if the
    // server refuses that (416, or a 206 that is not the range asked for) the
    // second is a plain GET. Never a loop, so a misbehaving server is asked twice.
    for (int attempt = 0; attempt < 2; ++attempt) {
        err.clear();
        const Outcome o = attempt_download(url, dest_path, progress, attempt == 0, timings, err);
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
