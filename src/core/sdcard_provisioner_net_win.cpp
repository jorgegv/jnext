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
// This file implements the same four functions as the POSIX twin
// (sdcard_provisioner_net_curl.cpp): http_download, transport_reason, sha256_hex,
// sha256_file, same contracts, same error semantics: the failure wording documented
// in sdcard_provisioner.h (transport_reason sorts the WinHTTP error into the same
// kinds the libcurl twin sorts its CURLcode into, so both say the same words; an
// HTTP error status says the same through http_status_reason), the partial-file
// contract documented there (`<dest>.part` kept for resuming) and the tick / stall /
// cancel one (the ProgressFn at least every tick whatever the network does;
// false aborts with "cancelled by user"; no body byte for the stall timeout ends
// the attempt). The decisions (append / truncate / retry, the record, the
// keep-or-remove rule, the per-tick decision) are shared code, the curl twin's
// too. The
// whole-file #ifdef pair keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt
// correct on every platform: exactly one twin has content per build.
//
// ASYNCHRONOUS WinHTTP (WINHTTP_FLAG_ASYNC), because the contract cannot be met
// synchronously: a synchronous WinHttpReadData blocks its thread until data
// arrives or the receive timeout fires, so with the cable pulled the GUI thread
// sat inside it, Windows ghosted the window as "Not Responding", and nothing was
// said until the network came back (#358). Microsoft forbids ending a
// synchronous call from another thread (WinHttpCloseHandle remarks: "an
// application should never call WinHttpCloseHandle on a synchronous request"),
// so the only way out of a blocked read is its own timeout. In async mode this
// thread waits on a condition variable (SRW lock) for the next completion or the
// next tick by the clock, and a cancel or a stall is simply "close the request
// handle and return". It is not a kernel event on purpose: under wine without
// ntsync every kernel-object wait goes through the single-threaded wineserver,
// which also delivers its timeout, so a wineserver that does not run for T
// seconds delays the tick by T (GH #415: a 1 s stop gave a 1.2 s tick gap, the CI
// failure of PROV-STALL-08). A condition-variable wait is a futex inside the
// process under wine (RtlSleepConditionVariableSRW, RtlWaitOnAddress,
// NtWaitForAlertByThreadId) and a kernel wait on Windows, and is available from
// Windows Vista, so Windows 7 is covered. On real Windows an event worked too;
// this is about not depending on another process. That holds
// only while no WinHTTP call made on this thread waits on the network.
// WinHttpReceiveResponse may: wine >= 11.7 reads the headers inside the call, in
// its caller (GH #415: no tick, no stall note, no cancel for the whole header
// wait). Microsoft says the same of WinHttpSendRequest and WinHttpQueryDataAvailable
// ("can operate either synchronously or asynchronously"), and this thread still
// calls those and WinHttpReadData; what makes them safe is wine's source and
// measurement, not the documentation: SendRequest is always queued (wine 11.0 and
// 11.20), and QueryDataAvailable and ReadData complete in the caller only with data
// already buffered. So the receive is started from the SENDREQUEST_COMPLETE
// completion callback, Microsoft's reentrant pattern, and this thread no longer
// waits for the server's answer inside a WinHTTP call. Established: wine 11.0 and
// 11.20 (source and tests); Windows only by inference from the documentation.
// Remaining gap: a SendRequest that completed synchronously and delivered
// SENDREQUEST_COMPLETE on the calling thread would run the receive on this thread
// again. Impossible under wine; unknown on Windows.
//
// Behaviour replicated from the curl path (only what it actually USED):
//   - follow redirects (WinHTTP follows them inside
//     WinHttpSendRequest/ReceiveResponse by default; the default policy
//     refuses https->http downgrades, which is strictly safer than curl's)
//   - HTTP status >= 400 is a failure (CURLOPT_FAILONERROR)
//   - 30 s connect timeout; the stall timeout (not WinHTTP's receive timeout,
//     which is only a backstop 30 s behind it) ends a silent transfer
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

#include <atomic>
#include <chrono>
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

// What a transport failure says (GH #385): the WinHTTP / Win32 error sorted into
// the shared kinds of net_reason, so WinHTTP and libcurl say the same words. The
// names are winhttp.h's. Wine reports a timeout as WSAETIMEDOUT (10060), not as
// ERROR_WINHTTP_TIMEOUT; both are a timeout.
NetFailure winhttp_kind(DWORD code) {
    constexpr DWORD kWsaTimedOut = 10060;   // WSAETIMEDOUT, spelled out: no winsock header here
    if (code == kWsaTimedOut) return NetFailure::TimedOut;
    switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:           return NetFailure::NameNotFound;
    case ERROR_WINHTTP_CANNOT_CONNECT:              return NetFailure::CannotConnect;
    case ERROR_WINHTTP_TIMEOUT:                     return NetFailure::TimedOut;
    case ERROR_WINHTTP_CONNECTION_ERROR:            return NetFailure::ConnectionLost;
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:     return NetFailure::NoAnswer;
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:     return NetFailure::Tls;
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:         return NetFailure::BadAddress;
    case ERROR_WINHTTP_REDIRECT_FAILED:             return NetFailure::Redirect;
    default:                                        return NetFailure::Other;
    }
}

// RAII for the session and connect handles: every early return closes them.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
};

// What the status callback tells the thread that waits for a completion. One per
// request, on the heap, and OWNED BY THE REQUEST HANDLE once the callback is
// installed: the callback frees it on WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING, the
// last callback a handle gets. That is what lets an abort return AT ONCE after
// WinHttpCloseHandle while WinHTTP may still complete or cancel a pending read
// into `buf` (the SendRequest remarks: keep the buffers valid until the callback
// says the operation is over). Nothing touches it after the close.
struct AsyncState {
    // The completion signal: `seq` counts the completions signalled so far, under
    // `lock`; the waiter compares it with the count it has consumed. No kernel object
    // (see the file header).
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
    unsigned seq = 0;
    std::atomic<DWORD> last{0};    // WINHTTP_CALLBACK_STATUS_* of the latest completion
    std::atomic<DWORD> value{0};   // DATA_AVAILABLE: bytes available; READ_COMPLETE: bytes read
    std::atomic<DWORD> error{0};   // REQUEST_ERROR: dwError
    std::vector<char> buf;         // the read buffer: DownloadTimings::read_bytes (64 KiB in production)
    std::wstring headers;          // SendRequest's extra headers, valid until SENDREQUEST_COMPLETE
    // The close handoff (GH #415). The callback thread can sit inside
    // WinHttpReceiveResponse for the whole header wait (wine >= 11.7), and
    // Microsoft forbids closing a handle while another thread is inside a call
    // on it (Concurrency in WinHTTP). All three are guarded by `lock`. SRW locks are
    // not recursive: nothing takes it while holding it.
    bool in_receive = false;   // the callback is inside WinHttpReceiveResponse
    bool abandoned = false;    // the waiting thread gave up meanwhile: the callback closes the handle
    bool closed = false;       // the waiting thread closed the handle itself
    explicit AsyncState(size_t read_bytes) : buf(read_bytes) {}
    AsyncState(const AsyncState&) = delete;
    AsyncState& operator=(const AsyncState&) = delete;
    // A completion happened: count it and wake the waiter.
    void signal() {
        AcquireSRWLockExclusive(&lock);
        ++seq;
        ReleaseSRWLockExclusive(&lock);
        WakeAllConditionVariable(&cv);
    }
};

// Scoped exclusive hold of an SRW lock.
struct SrwGuard {
    SRWLOCK& l;
    explicit SrwGuard(SRWLOCK& lk) : l(lk) { AcquireSRWLockExclusive(&l); }
    ~SrwGuard() { ReleaseSRWLockExclusive(&l); }
    SrwGuard(const SrwGuard&) = delete;
    SrwGuard& operator=(const SrwGuard&) = delete;
};

// Runs on a WinHTTP thread (or, for a fast completion, inside the call that
// started the operation). It stores atomics and signals, and on
// SENDREQUEST_COMPLETE starts the receive (see the file header): no logging, no
// globals, no PartialDownload. CALLBACK = __stdcall, which the i686 builds need.
void CALLBACK status_cb(HINTERNET h, DWORD_PTR ctx, DWORD status, LPVOID info, DWORD len) {
    AsyncState* st = reinterpret_cast<AsyncState*>(ctx);
    if (!st) return;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: {
        {
            SrwGuard lk(st->lock);
            if (st->closed) return;   // the waiting thread already closed the handle
            st->in_receive = true;
        }
        // May wait for the headers right here (wine >= 11.7), and HEADERS_AVAILABLE
        // or REQUEST_ERROR may be delivered nested inside this very call: they set
        // `last` themselves, so this branch must not overwrite it afterwards.
        const BOOL ok = WinHttpReceiveResponse(h, nullptr);
        const DWORD why = ok ? 0 : GetLastError();
        bool close_it;
        {
            SrwGuard lk(st->lock);
            st->in_receive = false;
            close_it = st->abandoned;
        }
        if (close_it) {
            WinHttpCloseHandle(h);   // HANDLE_CLOSING frees `st`: nothing touches it after this
            return;
        }
        if (!ok) {
            st->error = why;
            st->last = WINHTTP_CALLBACK_STATUS_REQUEST_ERROR;
        }
        st->signal();   // wake the waiter: it may be holding a HEADERS_AVAILABLE until in_receive is clear
        return;
    }
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        st->value = info ? *static_cast<DWORD*>(info) : 0;
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        st->value = len;
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        st->error = info ? static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        delete st;   // the last callback for this handle: the state was its own
        return;
    default:
        return;
    }
    st->last = status;
    st->signal();
}

// The request handle and its state. Once the callback is installed the state is
// the callback's, so closing the handle is all the destructor does for it; if the
// callback could not be installed no HANDLE_CLOSING will come and the state is
// deleted here.
struct AsyncRequest {
    HINTERNET h = nullptr;
    AsyncState* st = nullptr;
    bool callback_owns_state = false;
    AsyncRequest() = default;
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;
    ~AsyncRequest() {
        if (h && callback_owns_state) {
            {
                SrwGuard lk(st->lock);
                if (st->in_receive) {   // the callback is inside a call on the handle: it closes it
                    st->abandoned = true;
                    return;             // `st` and `h` are the callback's now
                }
                st->closed = true;
            }
        }
        if (h) WinHttpCloseHandle(h);
        if (!callback_owns_state) delete st;
    }
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
                         bool allow_resume, const DownloadTimings& timings, std::string& err) {
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
        // The caller names the URL (GH #385); here is only why it is no address.
        const DWORD why = wurl.empty() ? ERROR_WINHTTP_INVALID_URL : GetLastError();
        err = net_reason(NetFailure::BadAddress, "WinHTTP error " + std::to_string(why));
        part.fail(err, false);
        return Outcome::Failed;
    }
    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring object = path.data();
    object += extra.data(); // "" when the URL has no query part
    if (object.empty()) object = L"/";

    // From here every failure is a transport one: settle the partial (kept
    // when it holds bytes and a validator) and report its reason in plain words.
    auto transport_fail = [&](const std::string& why, bool clean_eof) {
        err = why;
        part.fail(err, clean_eof);
        return Outcome::Failed;
    };

    WinHttpHandle ses;
    ses.h = WinHttpOpen(L"jnext-sdcard-provisioner", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!ses.h) return transport_fail(transport_reason(GetLastError()), false);

    // resolve / connect / send = 30 s (curl CONNECTTIMEOUT 30). The receive
    // timeout is only a BACKSTOP, 30 s behind the stall timeout the wait loop
    // below enforces first (tick-accurate, and the same rule as the curl twin).
    WinHttpSetTimeouts(ses.h, 30000, 30000, 30000, timings.stall_timeout_ms + 30000);

    if (secure) {
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                       WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        // Best effort — see the file header about unpatched Win7.
        WinHttpSetOption(ses.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
    }

    WinHttpHandle con;
    con.h = WinHttpConnect(ses.h, host.data(), uc.nPort, 0);
    if (!con.h) return transport_fail(transport_reason(GetLastError()), false);

    AsyncRequest req;
    req.h = WinHttpOpenRequest(con.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) return transport_fail(transport_reason(GetLastError()), false);

    // The state, then the callback, BEFORE any asynchronous call. If either
    // cannot be installed no HANDLE_CLOSING will come, and ~AsyncRequest frees
    // the state itself.
    req.st = new AsyncState(timings.read_bytes);
    DWORD_PTR ctx = reinterpret_cast<DWORD_PTR>(req.st);
    if (!WinHttpSetOption(req.h, WINHTTP_OPTION_CONTEXT_VALUE, &ctx, sizeof(ctx)) ||
        WinHttpSetStatusCallback(req.h, status_cb,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        return transport_fail(transport_reason(GetLastError()), false);
    req.callback_owns_state = true;   // from here the callback frees it, on HANDLE_CLOSING
    AsyncState& st = *req.st;

    // The per-tick decision, shared with the curl twin, runs after every
    // completion and on every idle tick.
    StallWatch watch;
    uint64_t got_total = 0;   // body bytes of this response written so far

    // Wait for the completion `want`, one tick at a time. On Ended the attempt is
    // over (download_step has set `err` and settled the partial) and nothing is
    // waited for: the caller returns, the handles close, WinHTTP cleans up behind.
    //
    // The tick is by the clock: a deadline that persists across wakes that are not
    // the awaited completion, and restarts only after a tick's download_step. No
    // kernel-object wait (see the file header). `seen` is the count of completions
    // consumed so far, per request: a completion delivered inline, inside the WinHTTP
    // call that started the operation and before this runs, is not lost.
    enum class Wait { Done, Error, Ended };
    unsigned seen = 0;
    using Clock = std::chrono::steady_clock;
    const auto tick = std::chrono::milliseconds(timings.tick_ms);
    auto wait_for = [&](DWORD want) {
        auto deadline = Clock::now() + tick;
        for (;;) {
            bool signalled;
            bool in_receive;
            {
                SrwGuard lk(st.lock);
                while (st.seq == seen) {   // loops over spurious and stolen wakeups
                    const auto now = Clock::now();
                    if (now >= deadline) break;
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) +
                                      std::chrono::milliseconds(1);   // round up
                    SleepConditionVariableSRW(&st.cv, &st.lock, static_cast<DWORD>(left.count()), 0);
                }
                signalled = st.seq != seen;
                seen = st.seq;
                in_receive = st.in_receive;
            }
            if (signalled) {
                const DWORD last = st.last.load();
                if (last == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return Wait::Error;
                // A call on the handle that is still running (the callback's receive)
                // must have returned before the next operation begins.
                if (last == want && !in_receive) return Wait::Done;
                continue;   // some other notification: keep waiting for ours, same deadline
            }
            if (!download_step(part, progress, got_total, watch, timings, err)) return Wait::Ended;
            deadline = Clock::now() + tick;
        }
    };
    // wait_for() mapped to an Outcome; false = return `out` now.
    Outcome out = Outcome::Failed;
    auto await = [&](DWORD want) {
        switch (wait_for(want)) {
        case Wait::Done:  return true;
        case Wait::Ended: out = Outcome::Failed; return false;
        case Wait::Error: out = transport_fail(transport_reason(st.error.load()), false); return false;
        }
        return false;
    };

    // Resuming: ask for the rest, and only if the file is still the one we
    // started (If-Range). The added headers go to a redirect's target too.
    if (part.ranged())
        st.headers = widen("Range: " + part.range_value() + "\r\nIf-Range: " + part.if_range_value() + "\r\n");
    if (!WinHttpSendRequest(req.h, st.headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : st.headers.c_str(),
                            st.headers.empty() ? 0 : static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, ctx))
        return transport_fail(transport_reason(GetLastError()), false);
    // SENDREQUEST_COMPLETE starts the receive from the callback (GH #415).
    if (!await(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)) return out;

    HttpResponseInfo resp;
    {
        DWORD status = 0;
        DWORD status_sz = sizeof(status);
        if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX))
            return transport_fail(transport_reason(GetLastError()), false);
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

    // The body, sized by what WinHTTP says is available: a fixed 64 KiB read
    // would sit waiting to fill its buffer under wine, so the bytes that arrived
    // before a stall never reached the .part. n == 0 is the end of the body. A read
    // never asks for more than the buffer holds; wine reports at most 8 KiB
    // available, so only a buffer smaller than that (PROV-STALL-10) reaches the clamp.
    for (;;) {
        if (!WinHttpQueryDataAvailable(req.h, nullptr))
            return transport_fail(transport_reason(GetLastError()), false);
        if (!await(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)) return out;
        const DWORD avail = st.value.load();
        if (avail == 0) break;   // end of body
        const DWORD n = avail < st.buf.size() ? avail : static_cast<DWORD>(st.buf.size());
        if (!WinHttpReadData(req.h, st.buf.data(), n, nullptr))
            return transport_fail(transport_reason(GetLastError()), false);
        if (!await(WINHTTP_CALLBACK_STATUS_READ_COMPLETE)) return out;
        const DWORD got = st.value.load();
        if (!part.write(st.buf.data(), got)) return transport_fail("write error on " + partial_path(dest_path), false);
        got_total += got;
        if (!download_step(part, progress, got_total, watch, timings, err)) return Outcome::Failed;
    }
    if (!part.finish(err)) return Outcome::Failed;
    if (!regular_file_exists(dest_path)) {
        err = "produced no file: " + dest_path;
        return Outcome::Failed;
    }
    return Outcome::Done;
}

} // namespace

std::string transport_reason(long code) {
    const DWORD c = static_cast<DWORD>(code);
    return net_reason(winhttp_kind(c), "WinHTTP error " + std::to_string(c));
}

bool http_download(const std::string& url, const std::string& dest_path,
                   const ProgressFn& progress, std::string& err, const DownloadTimings& timings) {
    Log::emulator()->info("sdcard: downloading via WinHTTP: {}", url);
    // At most two attempts: the first may ask for the rest of a partial; if the
    // server refuses that (416, or a 206 that is not the range asked for) the
    // second is a plain GET. Never a loop.
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
