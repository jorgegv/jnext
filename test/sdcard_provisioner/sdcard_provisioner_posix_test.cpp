// SD-card provisioner: the rows that need libcurl's own behaviour (GH #385), POSIX only.
//
// These rows exercise what only libcurl does, so the WinHTTP twin has no counterpart of them
// and they cannot exist on Windows. They live in this sibling suite (`# os: posix` in
// test/unit-tests.conf), as the symlink rows of sdcard_file_add_test live in
// sdcard_file_add_posix_test, rather than as rows with a compiled-out arm. The rest of the
// provisioner's rows, including every other reason, run on every OS in sdcard_provisioner_test.
//
//   PROV-REASON-06  A proxy that will not open the tunnel. The proxy is put in front of the
//                  real backend through the environment (https_proxy), the way a user behind
//                  one does; the WinHTTP twin takes its proxy from the machine's WinHTTP
//                  settings, never from the environment. The transfer then has no response
//                  status of its own (CURLINFO_RESPONSE_CODE is 0), so the status words of an
//                  HTTP error must not be used (the curl twin once could say 'HTTP 0'); the
//                  proxy's answer is the reason, whichever code libcurl gave the failure.
//   PROV-REASON-07  A server that is reached but answers something libcurl cannot use: a reply
//                  that is not HTTP, or a redirect to a scheme or a URL it cannot use. libcurl
//                  reports those with the codes it also uses for an unusable ADDRESS (1, 3),
//                  but the address was fine and a connection was made, so the reason must not
//                  say the address is not valid (REASON-05 pins that wording for the address
//                  that really is unusable, with no connection made).
//
// Oracle: libcurl's documentation. CURLINFO_HTTP_CONNECTCODE is "the last received HTTP proxy
// response code to a CONNECT request" (0 when there was none); CURLOPT_FAILONERROR fails a
// transfer whose HTTP status is 400 or more; CURLINFO_NUM_CONNECTS is the number of new
// connections made. The wording is the plan's (gh385-plan.md section 1: 'could not connect to
// the server', 'the transfer failed', each followed by the backend's own detail in
// parentheses); the detail of 06 is the proxy's answer, not libcurl's generic 'HTTP response
// code said error'. A 2xx answer to CONNECT opens the tunnel, so it is 06's control: that
// failure (TLS over a tunnel the proxy then drops) is not the proxy's refusal.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/sdcard_provisioner.h"
#include "range_server.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <curl/curl.h>   // curl_easy_strerror: the code and text that follow the words

#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

std::string g_tmpdir;

void test_proxy_refusal() {
    namespace fs = std::filesystem;
    // The proxy is found through the environment, as a user sets one. Nothing ambient may exempt the
    // address from it or outrank it (libcurl reads the lower case variable first; no_proxy exempts hosts).
    for (const char* v : {"no_proxy", "NO_PROXY", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"}) jtp::unset_env(v);

    // An https address on a name that never resolves: through a proxy libcurl sends CONNECT <name>:443 and
    // resolves nothing itself, so the proxy is the only thing that can answer.
    const std::string kTarget = "https://proxy-target.invalid/file.zip";
    const std::string kAuthority = "proxy-target.invalid:443";
    const std::string kCannotConnect = "could not connect to the server";

    struct Case { int status; bool refusal; };
    const Case cases[] = {
        {300, true},    // not a 2xx: libcurl 8.18 fails the transfer with code 56 here and with 22 for the rest
        {400, true},
        {403, true},
        {407, true},
        {502, true},
        {200, false},   // the tunnel is open; the proxy then drops it, so TLS fails: not a refusal
    };
    std::string bad;
    for (const Case& c : cases) {
        rs::Server proxy;
        if (!proxy.ok()) { bad += " [cannot listen on 127.0.0.1]"; continue; }
        rs::Opts o;
        o.status = c.status;
        proxy.set(o);
        jtp::set_env("https_proxy", proxy.url("").c_str());

        const std::string dir = g_tmpdir + "/proxy-" + std::to_string(c.status);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const std::string dest = dir + "/dest.bin";
        std::string err;
        const bool ok = sdcard::http_download(kTarget, dest, {}, err, {200, 5000});
        const std::vector<rs::Request> reqs = proxy.requests();
        // The positive control: the proxy was really asked, and for the tunnel to the right place.
        const bool asked = !reqs.empty() && std::all_of(reqs.begin(), reqs.end(), [&](const rs::Request& r) {
            return r.path == kAuthority;
        });
        const bool left = fs::exists(dest, ec) || fs::exists(sdcard::partial_path(dest), ec) ||
                          fs::exists(sdcard::resume_record_path(dest), ec);
        const std::string answered = "the proxy answered HTTP " + std::to_string(c.status);
        const bool right = c.refusal
                               ? err == kCannotConnect + " (" + answered + " to the connection request)"
                               : err.find("the proxy answered") == std::string::npos &&
                                     err.find(" (curl error ") != std::string::npos;
        if (ok || !asked || left || !right)
            bad += " [proxy " + std::to_string(c.status) + ": ok=" + std::to_string(ok) + " asked=" +
                   std::to_string(asked) + " requests=" + std::to_string(reqs.size()) + " left=" +
                   std::to_string(left) + " err='" + err + "']";
    }
    jtp::unset_env("https_proxy");
    check("PROV-REASON-06",
          "through the real libcurl backend, an HTTP proxy that answers the CONNECT request with 300, 400, 403, "
          "407 or 502 fails the download with exactly 'could not connect to the server (the proxy answered HTTP "
          "<status> to the connection request)': no status of an HTTP error ('HTTP 0') and no libcurl 'HTTP "
          "response code said error'; the proxy was asked for the tunnel to the right address, and nothing is "
          "left; one that answers 200 (a tunnel it then drops) is not worded as a refusal",
          bad.empty(), bad);
}

void test_reply_libcurl_cannot_use() {
    namespace fs = std::filesystem;
    // Direct, on loopback: no proxy in the way.
    for (const char* v : {"https_proxy", "HTTPS_PROXY", "http_proxy", "all_proxy", "ALL_PROXY"}) jtp::unset_env(v);
    jtp::set_env("no_proxy", "127.0.0.1,localhost");
    jtp::set_env("NO_PROXY", "127.0.0.1,localhost");

    const std::string kOther = "the transfer failed";
    struct Case { const char* what; std::string raw; int code; };
    const Case cases[] = {
        {"a reply that is not HTTP (an SSH banner)", "SSH-2.0-OpenSSH_9.0\r\n", CURLE_UNSUPPORTED_PROTOCOL},
        {"a redirect to a scheme libcurl does not do",
         "HTTP/1.1 302 Found\r\nLocation: gopher://example.invalid/x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
         CURLE_UNSUPPORTED_PROTOCOL},
        {"a redirect to a URL that is not one",
         "HTTP/1.1 302 Found\r\nLocation: http://\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
         CURLE_URL_MALFORMAT},
    };
    std::string bad;
    int n = 0;
    for (const Case& c : cases) {
        rs::Server srv;
        if (!srv.ok()) { bad += " [cannot listen on 127.0.0.1]"; continue; }
        rs::Opts o;
        o.raw = c.raw;
        srv.set(o);
        const std::string dir = g_tmpdir + "/reply-" + std::to_string(n++);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const std::string dest = dir + "/dest.bin";
        std::string err;
        const bool ok = sdcard::http_download(srv.url(), dest, {}, err, {200, 5000});
        const bool asked = !srv.requests().empty();   // the control: a connection was really made
        const bool left = fs::exists(dest, ec) || fs::exists(sdcard::partial_path(dest), ec) ||
                          fs::exists(sdcard::resume_record_path(dest), ec);
        const std::string want = kOther + " (curl error " + std::to_string(c.code) + ": " +
                                 curl_easy_strerror(static_cast<CURLcode>(c.code)) + ")";
        if (ok || !asked || left || err != want)
            bad += " [" + std::string(c.what) + ": ok=" + std::to_string(ok) + " asked=" + std::to_string(asked) +
                   " left=" + std::to_string(left) + " err='" + err + "']";
    }
    check("PROV-REASON-07",
          "through the real libcurl backend, a server that is reached but answers something libcurl cannot use (a "
          "reply that is not HTTP, a redirect to a scheme it does not do, a redirect to a URL that is not one) "
          "fails with 'the transfer failed' followed by libcurl's own code and text (curl error 1 or 3), never "
          "with 'the address is not a valid http or https URL': the address was fine and a connection was made",
          bad.empty(), bad);
}

}  // namespace

int main() {
    g_tmpdir = jtp::make_temp_dir("jnext_provposix_");
    if (g_tmpdir.empty()) { std::printf("cannot mkdtemp\n"); return 1; }

    test_proxy_refusal();
    test_reply_libcurl_cannot_use();

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    std::error_code ec;
    std::filesystem::remove_all(g_tmpdir, ec);
    return g_fail ? 1 : 0;
}
