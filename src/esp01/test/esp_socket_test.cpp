// Emulated ESP-01 transport unit tests (GH #25, branch 2 — socket layer).
//
// Two halves, both hermetic:
//
//   1. THE ADDRESS POLICY, exhaustively. It is a pure function of its
//      arguments, so the whole security matrix — every denied range, both
//      boundaries of each, the IPv4-in-IPv6 bypass forms, and the owner's
//      RFC1918-is-ALLOWED carve-out — is checked without opening a socket.
//
//   2. THE TRANSPORT, against an IN-PROCESS LISTENER bound to 127.0.0.1:0.
//      No DNS, no external host, no fixed port. Note the deliberate tension
//      this exposes: the default policy DENIES loopback, so these rows can
//      only exist because the policy is an injectable parameter rather than an
//      `if` hard-coded into connect. That is the point of the split.
//
// PORTABILITY OF THE SUITE ITSELF, stated rather than glossed over. The policy
// half is pure C++17 and runs anywhere the module builds. The transport half's
// SCAFFOLDING — the in-process `Listener` below — is POSIX-only: it uses
// <arpa/inet.h>, <netinet/in.h>, <poll.h>, <sys/socket.h> and <unistd.h>
// directly, because a hermetic transport test needs a peer and the module
// deliberately does not ship one. A consumer on Windows must supply Winsock
// equivalents for `Listener` alone; nothing in the module under test needs
// porting, only the test's peer. (jnext itself never hits this: every Windows
// target builds with -DENABLE_TESTS=OFF.)
//
//   3. ASYNCHRONOUS NAME RESOLUTION, against an INJECTED resolver. A name
//      lookup runs on its own thread now, so the properties worth proving are
//      timing and lifetime properties — poll() returning while a lookup is
//      outstanding, a transport destroyed mid-lookup — and neither can be
//      proved against a real DNS server without a flaky test. The `ResolveFn`
//      parameter of `make_socket_transport` exists for that, and the rows below
//      drive it through a gate they open themselves, so nothing is timed
//      against the network at all.
//
// NOT TESTED HERE, said plainly:
//   * A real DNS server. The ASYNC rows use the injected resolver and TRACE-04
//     uses `localhost` (answered from /etc/hosts), so no row contacts a name
//     server; what the platform's `getaddrinfo` does with a genuine WAN name is
//     out of scope for a hermetic suite.
//   * The Winsock twin (esp_socket_win.cpp). It cannot be built or run on the
//     Linux dev host, and every Windows target sets -DENABLE_TESTS=OFF, so this
//     suite never compiles there. The twin is kept to syscall shims for exactly
//     that reason — and the async resolver added none of its own code to it,
//     because the resolver thread is portable `std::thread` in the shared file.
//
// Run: ./build/test/esp_socket_test

#include "esp01/esp_at.h"
#include "esp01/esp_log.h"
#include "esp01/esp_socket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace esp;

// ── Tiny test harness (matches resume_guard_test.cpp style) ───────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const std::string& desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc.c_str());
    }
}

static void skip(const char* id, const char* why) {
    ++g_total;
    ++g_skip;
    std::printf("  SKIP %s: %s\n", id, why);
}

// ── Log capture, through the module's own seam ────────────────────────────
//
// File-scope rather than captured by reference because `set_log_sink` installs
// a `std::function` that outlives the capture window's scope by exactly one
// statement (the restore), and a sink holding a reference to a dead local is a
// trap waiting for whoever adds the next row. This costs nothing and cannot
// dangle.
using LogLine  = std::pair<LogLevel, std::string>;
static std::vector<LogLine> g_log;

/// Run `work` with the seam pointed at `g_log` and the threshold at `level`,
/// then restore silence. Returns what was captured.
static std::vector<LogLine> capture_log(LogLevel level, const std::function<void()>& work) {
    g_log.clear();
    set_log_sink([](LogLevel l, const std::string& m) { g_log.emplace_back(l, m); });
    set_log_threshold(level);
    work();
    set_log_sink(nullptr);
    set_log_threshold(LogLevel::Info);
    return g_log;
}

// ── Policy helpers ────────────────────────────────────────────────────────

static const char* reason_name(DenyReason r) { return deny_reason_text(r); }

static void expect_verdict(const char* id, const IpAddress& ip, DenyReason want,
                           const AddressPolicy& policy) {
    const DenyReason got = classify_address(ip, policy);
    check(id, to_string(ip) + " → " + reason_name(want) + " (got " + reason_name(got) + ")",
          got == want);
}

static void expect_default(const char* id, const IpAddress& ip, DenyReason want) {
    expect_verdict(id, ip, want, AddressPolicy{});
}

static IpAddress v6(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d,
                    std::uint16_t e, std::uint16_t f, std::uint16_t g, std::uint16_t h) {
    const std::uint16_t   groups[8] = {a, b, c, d, e, f, g, h};
    std::array<std::uint8_t, 16> raw{};
    for (int i = 0; i < 8; ++i) {
        raw[i * 2]     = static_cast<std::uint8_t>(groups[i] >> 8);
        raw[i * 2 + 1] = static_cast<std::uint8_t>(groups[i] & 0xFF);
    }
    return ipv6(raw);
}

/// 2002:AABB:CCDD::<tail> — a 6to4 address whose tunnel endpoint is a.b.c.d.
static IpAddress sixtofour(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                           std::uint8_t d, std::uint16_t tail = 1) {
    std::array<std::uint8_t, 16> raw{};
    raw[0]  = 0x20;
    raw[1]  = 0x02;
    raw[2]  = a; raw[3] = b; raw[4] = c; raw[5] = d;
    raw[14] = static_cast<std::uint8_t>(tail >> 8);
    raw[15] = static_cast<std::uint8_t>(tail & 0xFF);
    return ipv6(raw);
}

/// ::ffff:a.b.c.d — the IPv4-mapped form a dual-stack getaddrinfo emits.
static IpAddress v4mapped(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    std::array<std::uint8_t, 16> raw{};
    raw[10] = 0xff;
    raw[11] = 0xff;
    raw[12] = a; raw[13] = b; raw[14] = c; raw[15] = d;
    return ipv6(raw);
}

// ── In-process TCP listener (POSIX only; see the header note) ─────────────

namespace {

void sleep_ms(int ms) {
    timespec ts{};
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    ::nanosleep(&ts, nullptr);
}

class Listener {
public:
    bool start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int on = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in sa{};
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port        = 0;  // kernel picks a free port — no fixed port, ever
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) return false;
        if (::listen(fd_, 4) != 0) return false;

        sockaddr_in bound{};
        socklen_t   len = sizeof(bound);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len) != 0) return false;
        port_ = ntohs(bound.sin_port);
        return port_ != 0;
    }

    /// Accept one pending connection, waiting up to `timeout_ms`.
    int accept_one(int timeout_ms) {
        pollfd p{};
        p.fd     = fd_;
        p.events = POLLIN;
        if (::poll(&p, 1, timeout_ms) <= 0) return -1;
        return ::accept(fd_, nullptr, nullptr);
    }

    std::uint16_t port() const { return port_; }

    void stop() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }
    ~Listener() { stop(); }

private:
    int           fd_   = -1;
    std::uint16_t port_ = 0;
};

/// Read up to `want` bytes from `fd`, waiting up to `timeout_ms` in total.
std::string read_some(int fd, std::size_t want, int timeout_ms) {
    std::string out;
    for (int waited = 0; waited < timeout_ms && out.size() < want; waited += 5) {
        char    buf[256];
        pollfd  p{};
        p.fd     = fd;
        p.events = POLLIN;
        if (::poll(&p, 1, 5) > 0) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
    }
    return out;
}

/// Drive poll() until the transport reaches `want` or settles in a terminal
/// state. Bounded: loopback resolves this in microseconds, so a timeout here
/// is a real failure, not a slow machine.
bool pump_until(EspTransport& t, TransportState want, int timeout_ms = 2000) {
    for (int waited = 0; waited <= timeout_ms; waited += 2) {
        t.poll();
        if (t.state() == want) return true;
        if (t.state() == TransportState::Failed || t.state() == TransportState::Closed)
            return false;  // settled somewhere else
        sleep_ms(2);
    }
    return false;
}

/// Poll recv() until the state changes away from Connected, or time runs out.
bool pump_recv_until_not_connected(EspTransport& t, int timeout_ms = 2000) {
    std::uint8_t buf[64];
    for (int waited = 0; waited <= timeout_ms; waited += 2) {
        t.recv(buf, sizeof(buf));
        if (t.state() != TransportState::Connected) return true;
        sleep_ms(2);
    }
    return false;
}

/// The policy the socket rows run under: identical to production except that
/// loopback is reachable, because the listener lives on 127.0.0.1.
AddressPolicy loopback_ok() {
    AddressPolicy p;
    p.deny_loopback = false;
    return p;
}

/// Milliseconds since `t0`.
long since_ms(std::chrono::steady_clock::time_point t0) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count());
}

/// Spin until `pred()` or `timeout_ms` elapses. Returns what `pred()` last said.
bool wait_until(const std::function<bool()>& pred, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 2) {
        if (pred()) return true;
        sleep_ms(2);
    }
    return pred();
}

// ── The injected resolver the ASYNC rows drive ────────────────────────────
//
// EVERYTHING HERE IS FILE-SCOPE, and that is the point rather than laziness.
// A resolver function is copied into a DETACHED thread that may still be
// running long after the row which started it has returned — that is the exact
// property ASYNC-07/08 exist to prove — so anything it touches must outlive
// main()'s scopes. A lambda capturing a row's local by reference would dangle
// in precisely the case these rows call safe. All cross-thread state is atomic
// for the same reason: the suite must be clean under TSan, not merely quiet.
namespace fake_dns {

/// Every gated lookup parks here until a row opens the gate. A row that never
/// opens it is exercising the resolver-that-never-answers case on purpose
/// (ASYNC-09); main() opens it on the way out so no thread is left parked.
std::atomic<bool> gate{false};
std::atomic<int>  entered{0};    ///< lookups that reached the resolver at all
std::atomic<int>  completed{0};  ///< lookups that ran to the end of the resolver
std::atomic<bool> off_thread{false};
std::atomic<bool> literal_leak{false};

/// Set once, before any resolver thread can exist, so comparing against it
/// from a resolver is a read of an already-published value.
std::thread::id main_thread_id;

/// Answers 127.0.0.1 — deliberately an address the DEFAULT policy DENIES, so
/// ASYNC-05 can prove the gate is applied to what the RESOLVER said rather
/// than to the name the guest typed. Caps its own wait so a forgotten gate
/// cannot wedge the suite.
bool gated(const std::string&, std::vector<IpAddress>& out, std::string& err) {
    entered.fetch_add(1);
    if (std::this_thread::get_id() != main_thread_id) off_thread.store(true);
    const bool opened = wait_until([] { return gate.load(); }, 5000);
    if (!opened) {
        err = "the gate was never opened";
        completed.fetch_add(1);
        return false;
    }
    out.push_back(ipv4(127, 0, 0, 1));
    completed.fetch_add(1);
    return true;
}

/// MUST NEVER BE CALLED. An IP literal has to take the synchronous
/// AI_NUMERICHOST path, which does not go through a ResolveFn at all.
bool never(const std::string&, std::vector<IpAddress>&, std::string& err) {
    literal_leak.store(true);
    err = "the resolver was consulted for an IP literal";
    return false;
}

/// Appends an address and THEN throws, which is the nastier half of the
/// contract: the module has to both survive the exception and discard the
/// half-produced list. The address appended is a perfectly connectable one
/// under `loopback_ok()`, so a module that kept it would connect somewhere.
bool throws_after_appending(const std::string&, std::vector<IpAddress>& out,
                            std::string&) {
    entered.fetch_add(1);
    out.push_back(ipv4(127, 0, 0, 1));
    throw std::runtime_error("dns library blew up");
}

}  // namespace fake_dns

}  // namespace

// ── Tests ─────────────────────────────────────────────────────────────────

int main() {
    std::printf("\n======================================================\n");
    std::printf("ESP-01 socket transport unit tests (GH #25 branch 2)\n");
    std::printf("======================================================\n\n");

    const AddressPolicy kDefault;

    // Published before any resolver thread can exist, so a resolver reading it
    // is reading an already-visible value (ASYNC-03 compares against it).
    fake_dns::main_thread_id = std::this_thread::get_id();

    // ═══ POL-LB — loopback, both boundaries, and the mapped bypass ═════════
    expect_default("POL-LB-01", ipv4(127, 0, 0, 1),       DenyReason::Loopback);
    expect_default("POL-LB-02", ipv4(127, 0, 0, 0),       DenyReason::Loopback);
    expect_default("POL-LB-03", ipv4(127, 255, 255, 255), DenyReason::Loopback);
    expect_default("POL-LB-04", ipv4(126, 255, 255, 255), DenyReason::None);
    expect_default("POL-LB-05", ipv4(128, 0, 0, 0),       DenyReason::None);
    expect_default("POL-LB-06", v6(0, 0, 0, 0, 0, 0, 0, 1), DenyReason::Loopback);
    // ::2 is NOT loopback and must not be swept up by it.
    expect_default("POL-LB-07", v6(0, 0, 0, 0, 0, 0, 0, 2), DenyReason::None);
    // The one-line bypass: an IPv4-mapped loopback must still be denied.
    expect_default("POL-LB-08", v4mapped(127, 0, 0, 1),   DenyReason::Loopback);
    // And the override the socket rows below depend on actually overrides.
    expect_verdict("POL-LB-09", ipv4(127, 0, 0, 1), DenyReason::None, loopback_ok());
    expect_verdict("POL-LB-10", v6(0, 0, 0, 0, 0, 0, 0, 1), DenyReason::None, loopback_ok());

    // ═══ POL-LL — link-local, both boundaries, v4 and v6 ═══════════════════
    expect_default("POL-LL-01", ipv4(169, 254, 0, 0),     DenyReason::LinkLocal);
    expect_default("POL-LL-02", ipv4(169, 254, 255, 255), DenyReason::LinkLocal);
    expect_default("POL-LL-03", ipv4(169, 253, 255, 255), DenyReason::None);
    expect_default("POL-LL-04", ipv4(169, 255, 0, 0),     DenyReason::None);
    expect_default("POL-LL-05", v6(0xfe80, 0, 0, 0, 0, 0, 0, 1), DenyReason::LinkLocal);
    expect_default("POL-LL-06", v6(0xfebf, 0xffff, 0xffff, 0xffff,
                                   0xffff, 0xffff, 0xffff, 0xffff), DenyReason::LinkLocal);
    // fe7f:: is just below fe80::/10 and fec0:: just above it.
    expect_default("POL-LL-07", v6(0xfe7f, 0, 0, 0, 0, 0, 0, 1), DenyReason::None);
    expect_default("POL-LL-08", v6(0xfec0, 0, 0, 0, 0, 0, 0, 1), DenyReason::None);

    // ═══ POL-MD — cloud metadata ═══════════════════════════════════════════
    // Reported as CloudMetadata, not the vaguer LinkLocal it also matches.
    expect_default("POL-MD-01", ipv4(169, 254, 169, 254), DenyReason::CloudMetadata);
    // Alibaba's endpoint lives inside CGNAT, which is an ALLOWED private range,
    // so it only stays denied because metadata is its own rule.
    expect_default("POL-MD-02", ipv4(100, 100, 100, 200), DenyReason::CloudMetadata);
    expect_default("POL-MD-03", ipv4(100, 100, 100, 199), DenyReason::None);
    // AWS IMDS over IPv6 — inside fc00::/7 ULA, likewise allowed as private.
    expect_default("POL-MD-04", v6(0xfd00, 0x0ec2, 0, 0, 0, 0, 0, 0x0254),
                   DenyReason::CloudMetadata);
    expect_default("POL-MD-05", v6(0xfd00, 0x0ec2, 0, 0, 0, 0, 0, 0x0253),
                   DenyReason::None);
    expect_default("POL-MD-06", v4mapped(169, 254, 169, 254), DenyReason::CloudMetadata);
    {
        // Turning the metadata rule OFF must FALL THROUGH to link-local rather
        // than returning "allowed" — the rule-order property the code relies on.
        AddressPolicy p;
        p.deny_cloud_metadata = false;
        expect_verdict("POL-MD-07", ipv4(169, 254, 169, 254), DenyReason::LinkLocal, p);
        // ...and with BOTH off, it really is allowed (proves nothing else denies it).
        p.deny_link_local = false;
        expect_verdict("POL-MD-08", ipv4(169, 254, 169, 254), DenyReason::None, p);
    }

    // ═══ POL-PRIV — RFC1918 is ALLOWED (owner decision, GH #25 2026-07-28) ═
    expect_default("POL-PRIV-01", ipv4(10, 0, 0, 1),         DenyReason::None);
    expect_default("POL-PRIV-02", ipv4(10, 255, 255, 255),   DenyReason::None);
    expect_default("POL-PRIV-03", ipv4(172, 16, 0, 1),       DenyReason::None);
    expect_default("POL-PRIV-04", ipv4(172, 31, 255, 255),   DenyReason::None);
    expect_default("POL-PRIV-05", ipv4(192, 168, 1, 1),      DenyReason::None);
    expect_default("POL-PRIV-06", ipv4(100, 64, 0, 1),       DenyReason::None);
    expect_default("POL-PRIV-07", v6(0xfd12, 0x3456, 0, 0, 0, 0, 0, 1), DenyReason::None);
    {
        // The flag exists and works — including both 172.16/12 boundaries.
        AddressPolicy p;
        p.deny_private = true;
        expect_verdict("POL-PRIV-08", ipv4(10, 0, 0, 1),   DenyReason::Private, p);
        expect_verdict("POL-PRIV-09", ipv4(192, 168, 1, 1), DenyReason::Private, p);
        expect_verdict("POL-PRIV-10", ipv4(172, 16, 0, 0),  DenyReason::Private, p);
        expect_verdict("POL-PRIV-11", ipv4(172, 15, 255, 255), DenyReason::None, p);
        expect_verdict("POL-PRIV-12", ipv4(172, 32, 0, 0),  DenyReason::None, p);
        expect_verdict("POL-PRIV-13", ipv4(100, 64, 0, 1),  DenyReason::Private, p);
        expect_verdict("POL-PRIV-14", ipv4(100, 63, 255, 255), DenyReason::None, p);
        expect_verdict("POL-PRIV-15", v6(0xfd12, 0, 0, 0, 0, 0, 0, 1),
                       DenyReason::Private, p);
        expect_verdict("POL-PRIV-16", v6(0xfc00, 0, 0, 0, 0, 0, 0, 1),
                       DenyReason::Private, p);
        expect_verdict("POL-PRIV-17", v6(0xfe00, 0, 0, 0, 0, 0, 0, 1),
                       DenyReason::None, p);
    }

    // ═══ POL-RSV — unspecified, multicast, reserved ════════════════════════
    expect_default("POL-RSV-01", ipv4(0, 0, 0, 0),           DenyReason::Unspecified);
    expect_default("POL-RSV-02", ipv4(0, 255, 255, 255),     DenyReason::Unspecified);
    expect_default("POL-RSV-03", ipv4(1, 0, 0, 0),           DenyReason::None);
    expect_default("POL-RSV-04", v6(0, 0, 0, 0, 0, 0, 0, 0), DenyReason::Unspecified);
    expect_default("POL-RSV-05", ipv4(224, 0, 0, 1),         DenyReason::MulticastOrReserved);
    expect_default("POL-RSV-06", ipv4(223, 255, 255, 255),   DenyReason::None);
    expect_default("POL-RSV-07", ipv4(240, 0, 0, 0),         DenyReason::MulticastOrReserved);
    expect_default("POL-RSV-08", ipv4(255, 255, 255, 255),   DenyReason::MulticastOrReserved);
    expect_default("POL-RSV-09", v6(0xff02, 0, 0, 0, 0, 0, 0, 1),
                   DenyReason::MulticastOrReserved);

    // ═══ NORM — IPv4-in-IPv6 normalization ═════════════════════════════════
    check("NORM-01", "::ffff:1.2.3.4 unwraps to 1.2.3.4",
          normalize(v4mapped(1, 2, 3, 4)) == ipv4(1, 2, 3, 4));
    {
        std::array<std::uint8_t, 16> nat64{};
        nat64[1] = 0x64; nat64[2] = 0xff; nat64[3] = 0x9b;
        nat64[12] = 1; nat64[13] = 2; nat64[14] = 3; nat64[15] = 4;
        check("NORM-02", "64:ff9b::1.2.3.4 unwraps to 1.2.3.4",
              normalize(ipv6(nat64)) == ipv4(1, 2, 3, 4));
        // ...and the NAT64 route to loopback is therefore closed too.
        nat64[12] = 127; nat64[13] = 0; nat64[14] = 0; nat64[15] = 1;
        expect_default("NORM-03", ipv6(nat64), DenyReason::Loopback);
    }
    {
        std::array<std::uint8_t, 16> compat{};
        compat[12] = 1; compat[13] = 2; compat[14] = 3; compat[15] = 4;
        check("NORM-04", "IPv4-compatible ::1.2.3.4 unwraps to 1.2.3.4",
              normalize(ipv6(compat)) == ipv4(1, 2, 3, 4));
    }
    check("NORM-05", ":: keeps its IPv6 identity (not 0.0.0.0)",
          normalize(v6(0, 0, 0, 0, 0, 0, 0, 0)) == v6(0, 0, 0, 0, 0, 0, 0, 0));
    check("NORM-06", "::1 keeps its IPv6 identity (not 0.0.0.1)",
          normalize(v6(0, 0, 0, 0, 0, 0, 0, 1)) == v6(0, 0, 0, 0, 0, 0, 0, 1));
    check("NORM-07", "::0.0.0.5 is not unwrapped either",
          normalize(v6(0, 0, 0, 0, 0, 0, 0, 5)) == v6(0, 0, 0, 0, 0, 0, 0, 5));
    check("NORM-08", "an ordinary IPv6 address is unchanged",
          normalize(v6(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1)) ==
              v6(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1));
    check("NORM-09", "an IPv4 address is returned unchanged",
          normalize(ipv4(9, 9, 9, 9)) == ipv4(9, 9, 9, 9));

    // ═══ POL-TUN — tunnelling prefixes: 6to4 in, Teredo/ISATAP out ═════════
    // A 6to4 address is not an alias for its endpoint (normalize() leaves it
    // alone) but packets for it are physically delivered there, so the policy
    // has to judge the endpoint too or 2002:7f00:1::1 launders 127.0.0.1.
    expect_default("POL-TUN-01", sixtofour(127, 0, 0, 1),       DenyReason::Loopback);
    expect_default("POL-TUN-02", sixtofour(169, 254, 169, 254), DenyReason::CloudMetadata);
    expect_default("POL-TUN-03", sixtofour(169, 254, 0, 1),     DenyReason::LinkLocal);
    expect_default("POL-TUN-04", sixtofour(93, 184, 216, 34),   DenyReason::None);
    // The endpoint is judged by the FULL policy, not a loopback-only check:
    // an RFC1918 endpoint is allowed by default and denied when the private
    // rule is switched on, exactly as the bare address would be.
    expect_default("POL-TUN-05", sixtofour(10, 0, 0, 1),        DenyReason::None);
    {
        AddressPolicy p;
        p.deny_private = true;
        expect_verdict("POL-TUN-06", sixtofour(10, 0, 0, 1), DenyReason::Private, p);
    }
    // Teredo (2001:0::/32) is deliberately NOT unwrapped: its embedded IPv4
    // fields are a public relay server and an obfuscated NATted client, so
    // neither can name a host on this machine. Even a Teredo address whose
    // SERVER field spells 127.0.0.1 stays allowed — pinning the exclusion so
    // it cannot be "fixed" silently.
    expect_default("POL-TUN-07",
                   v6(0x2001, 0x0000, 0x7f00, 0x0001, 0, 0, 0x80ff, 0xfffe),
                   DenyReason::None);
    // ISATAP is an interface-IDENTIFIER pattern, not a prefix: a rule keyed on
    // it would deny ordinary global addresses whose low 64 bits coincide. So
    // ::0:5efe:7f00:1 under a global prefix stays allowed...
    expect_default("POL-TUN-08",
                   v6(0x2001, 0x0db8, 0, 0, 0, 0x5efe, 0x7f00, 0x0001),
                   DenyReason::None);
    // ...while the place ISATAP actually lives is already denied outright.
    expect_default("POL-TUN-09",
                   v6(0xfe80, 0, 0, 0, 0, 0x5efe, 0x7f00, 0x0001),
                   DenyReason::LinkLocal);
    check("POL-TUN-10", "normalize() leaves a 6to4 address as IPv6",
          normalize(sixtofour(93, 184, 216, 34)) == sixtofour(93, 184, 216, 34));
    {
        // ...which is what stops select_candidate treating it as an IPv4
        // candidate. Order matters here: the 6to4 address comes FIRST, so if
        // it were folded into V4 it would win the IPv4 preference pass.
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {sixtofour(93, 184, 216, 34),
                                              ipv4(93, 184, 216, 34)};
        check("POL-TUN-11", "a 6to4 address does not win the IPv4 preference pass",
              select_candidate(cands, kDefault, chosen, why) &&
                  chosen == ipv4(93, 184, 216, 34));
    }
    {
        IpAddress ep;
        check("POL-TUN-12", "tunnel_endpoint() extracts the 6to4 gateway address",
              tunnel_endpoint(sixtofour(198, 51, 100, 7), ep) &&
                  ep == ipv4(198, 51, 100, 7));
        check("POL-TUN-13", "tunnel_endpoint() declines an ordinary IPv6 address",
              !tunnel_endpoint(v6(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1), ep));
        // 32.2.3.4 specifically: its first two octets are 0x20,0x02, so this
        // is the exact address that slips through if the family guard is ever
        // dropped and the 2002::/16 test is applied to raw bytes.
        check("POL-TUN-14", "tunnel_endpoint() declines an IPv4 address",
              !tunnel_endpoint(ipv4(32, 2, 3, 4), ep));
    }

    // ═══ SEL — candidate selection ═════════════════════════════════════════
    {
        IpAddress  chosen;
        DenyReason why = DenyReason::Loopback;
        check("SEL-01", "an empty candidate list selects nothing",
              !select_candidate({}, kDefault, chosen, why) && why == DenyReason::None);
    }
    {
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {v6(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1),
                                              ipv4(93, 184, 216, 34)};
        check("SEL-02", "IPv4 is preferred even when IPv6 comes first",
              select_candidate(cands, kDefault, chosen, why) &&
                  chosen == ipv4(93, 184, 216, 34));
    }
    {
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {ipv4(127, 0, 0, 1), ipv4(93, 184, 216, 34)};
        check("SEL-03", "a denied candidate is skipped for an allowed one",
              select_candidate(cands, kDefault, chosen, why) &&
                  chosen == ipv4(93, 184, 216, 34));
    }
    {
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {v6(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1)};
        check("SEL-04", "IPv6 is used when there is no IPv4 candidate",
              select_candidate(cands, kDefault, chosen, why) &&
                  chosen == v6(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1));
    }
    {
        IpAddress  chosen;
        DenyReason why = DenyReason::None;
        const std::vector<IpAddress> cands = {ipv4(127, 0, 0, 1),
                                              ipv4(169, 254, 169, 254)};
        check("SEL-05", "all-denied reports the FIRST candidate's reason",
              !select_candidate(cands, kDefault, chosen, why) &&
                  why == DenyReason::Loopback);
    }
    {
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {v6(0, 0, 0, 0, 0, 0, 0, 1),
                                              ipv4(127, 0, 0, 1)};
        check("SEL-06", "with loopback allowed, IPv4 loopback still wins over IPv6",
              select_candidate(cands, loopback_ok(), chosen, why) &&
                  chosen == ipv4(127, 0, 0, 1));
    }
    {
        IpAddress  chosen;
        DenyReason why;
        const std::vector<IpAddress> cands = {v6(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1),
                                              v4mapped(93, 184, 216, 34)};
        check("SEL-07", "a mapped IPv4 candidate counts as IPv4 and is returned verbatim",
              select_candidate(cands, kDefault, chosen, why) &&
                  chosen == v4mapped(93, 184, 216, 34));
    }

    // ═══ FMT — formatting / naming helpers used in every log line ══════════
    check("FMT-01", "IPv4 renders as a dotted quad",
          to_string(ipv4(192, 168, 0, 254)) == "192.168.0.254");
    check("FMT-02", "IPv6 renders in full, uncompressed 8-group form",
          to_string(v6(0, 0, 0, 0, 0, 0, 0, 1)) == "0:0:0:0:0:0:0:1");
    check("FMT-03", "IPv6 groups drop leading zeros but keep their positions",
          to_string(v6(0x2001, 0x0db8, 0, 0, 0, 0, 0, 0x0254)) ==
              "2001:db8:0:0:0:0:0:254");
    check("FMT-04", "every deny reason has distinct text",
          std::string(deny_reason_text(DenyReason::Loopback)) != "unknown" &&
              std::string(deny_reason_text(DenyReason::CloudMetadata)) !=
                  std::string(deny_reason_text(DenyReason::LinkLocal)));
    check("FMT-05", "every transport state has distinct text",
          std::string(transport_state_text(TransportState::Connecting)) !=
                  std::string(transport_state_text(TransportState::Connected)) &&
              std::string(transport_state_text(TransportState::Idle)) != "unknown");

    // ═══ SEAM — the logging seam itself (esp_log.h) ════════════════════════
    // The module logs through its own sink rather than through jnext's
    // spdlog wrapper, which is what lets a consumer bind their own. These rows
    // pin the seam's contract; the TRACE rows below then use it to assert what
    // the transport actually says. jnext's side of the binding — that the
    // `esp01` spdlog logger is registered and reachable from --log-level — is
    // tested where it lives, in test/esp/esp_uart_adapter_test.cpp.
    {
        check("SEAM-01", "the default threshold is info — the module's own quiet default",
              log_threshold() == LogLevel::Info);
        check("SEAM-02", "every level has distinct, non-'unknown' text",
              std::string(log_level_text(LogLevel::Trace)) != std::string(log_level_text(LogLevel::Debug)) &&
                  std::string(log_level_text(LogLevel::Warn)) != std::string(log_level_text(LogLevel::Error)) &&
                  std::string(log_level_text(LogLevel::Info)) != "unknown");
        check("SEAM-03", "a byte renders as two upper-case hex digits, not as a character",
              log_hex_byte(0x0A) == "0x0A" && log_hex_byte(0xFE) == "0xFE");

        // UNBOUND IS SILENT — and the row has to INSTALL a sink first, or it
        // proves nothing. Asserting silence before any sink has ever been
        // installed passes against a `set_log_sink` that ignores its argument
        // entirely (verified: that mutation survived the first version of this
        // row). So: install, prove it captures, clear, prove it stops.
        // `capture_log` restores the sink to nullptr after every window, so
        // the cleared state is also the state the rest of this suite runs in.
        const auto installed = capture_log(LogLevel::Trace, [] { log_error("captured"); });
        check("SEAM-04", "an installed sink receives the module's output",
              installed.size() == 1 && installed[0].second == "captured");
        g_log.clear();
        set_log_threshold(LogLevel::Trace);
        log_error("this must go nowhere at all");
        check("SEAM-04b",
              "...and clearing the sink restores silence, at any level",
              g_log.empty());
        set_log_threshold(LogLevel::Info);

        const auto below = capture_log(LogLevel::Info, [] {
            log_debug("chatter");
            log_info("worth saying");
        });
        check("SEAM-05", "the threshold drops everything below it and keeps the rest",
              below.size() == 1 && below[0].second == "worth saying" &&
                  below[0].first == LogLevel::Info);

        const auto lowered = capture_log(LogLevel::Trace, [] { log_trace("now visible"); });
        check("SEAM-06", "lowering the threshold lets trace through",
              lowered.size() == 1 && lowered[0].first == LogLevel::Trace);

        const auto fmt = capture_log(LogLevel::Info, [] {
            log_info("{} then {} then {}", 1, "two", 3.5);
            log_info("{{literal}} braces", 0);
            log_info("spec {:#04x} ignored, value substituted", 255);
            log_info("no placeholder", 42);
            log_info("two placeholders {} {}", 1);
        });
        check("SEAM-07", "{} substitutes positionally, in order, for mixed types",
              fmt.size() == 5 && fmt[0].second == "1 then two then 3.5");
        check("SEAM-08", "{{ and }} are literal braces", fmt[1].second == "{literal} braces");
        check("SEAM-09", "a format spec inside the braces is ignored, not printed",
              fmt[2].second == "spec 255 ignored, value substituted");
        check("SEAM-10",
              "surplus arguments and surplus placeholders are both harmless",
              fmt[3].second == "no placeholder" && fmt[4].second == "two placeholders 1 {}");
    }

    // ═══ TRACE — what the transport actually EMITS, and at which level ═════
    // The owner made tracing a first-class requirement with a hard default:
    // nothing on by default EXCEPT connection open/close (and a refusal, which
    // must never be silent). These rows pin that contract from the outside, by
    // capturing what the module hands to the seam — which is what makes them
    // runnable by a consumer who binds a different sink, and makes them a test
    // of the seam rather than of spdlog.
    {
        auto capture = [](LogLevel level, const std::function<void()>& work) {
            std::vector<std::string> lines;
            for (const auto& e : capture_log(level, work)) lines.push_back(e.second);
            return lines;
        };
        auto joined = [](const std::vector<std::string>& lines) {
            std::string all;
            for (const auto& line : lines) all += line;
            return all;
        };

        Listener l;
        if (!l.start()) {
            for (const char* id : {"TRACE-01", "TRACE-02", "TRACE-03"}) {
                ++g_total; ++g_skip;
                std::printf("  SKIP %s: could not bind a loopback listener\n", id);
            }
        } else {
            // (a) At debug, an IP-literal target must take the AI_NUMERICHOST
            //     path and say so — the evidence that no DNS lookup happened.
            const auto dbg = capture(LogLevel::Debug, [&] {
                auto t = make_socket_transport(loopback_ok());
                t->begin_connect("127.0.0.1", l.port());
                pump_until(*t, TransportState::Connected);
                const int srv = l.accept_one(500);
                if (srv >= 0) ::close(srv);
            });
            check("TRACE-01", "an IP literal is resolved without a DNS lookup",
                  joined(dbg).find("numeric address") != std::string::npos &&
                      joined(dbg).find("resolved '") == std::string::npos);

            // (b) At the DEFAULT level, a whole connect/send/recv/close cycle
            //     emits exactly two lines: opened and closed. Nothing else.
            const auto quiet = capture(LogLevel::Info, [&] {
                auto t = make_socket_transport(loopback_ok());
                t->begin_connect("127.0.0.1", l.port());
                pump_until(*t, TransportState::Connected);
                const int srv = l.accept_one(500);
                const std::uint8_t payload[3] = {'A', 'T', '\r'};
                t->send(payload, sizeof(payload));
                std::uint8_t rx[16];
                t->recv(rx, sizeof(rx));
                t->close();
                if (srv >= 0) ::close(srv);
            });
            check("TRACE-02",
                  "at the default level a full session logs open + close and nothing "
                  "else",
                  quiet.size() == 2 &&
                      quiet[0].find("connection OPENED") != std::string::npos &&
                      quiet[1].find("closed locally") != std::string::npos);

            // (c) A refusal is visible at the DEFAULT level too — the owner's
            //     "never silent about a connection made or refused" rule.
            const auto refused = capture(LogLevel::Info, [&] {
                auto t = make_socket_transport(kDefault);  // loopback denied
                t->begin_connect("127.0.0.1", l.port());
                t->poll();
            });
            check("TRACE-03", "a policy refusal is logged at the default level",
                  refused.size() == 1 &&
                      joined(refused).find("REFUSED by address policy") !=
                          std::string::npos);

            // (d) The other side of TRACE-01: a NAME must NOT take the numeric
            //     fast path. `localhost` is used deliberately — nsswitch
            //     answers it from /etc/hosts, so this stays hermetic (no DNS
            //     server is contacted) while still exercising the real
            //     getaddrinfo branch, which is the only thing that tells
            //     AI_NUMERICHOST being present apart from it being absent.
            const auto named = capture(LogLevel::Debug, [&] {
                auto t = make_socket_transport(loopback_ok());
                t->begin_connect("localhost", l.port());
                pump_until(*t, TransportState::Connected);
                const int srv = l.accept_one(500);
                if (srv >= 0) ::close(srv);
            });
            if (joined(named).find("cannot resolve") != std::string::npos) {
                ++g_total; ++g_skip;
                std::printf("  SKIP TRACE-04: this host cannot resolve 'localhost'\n");
            } else {
                check("TRACE-04",
                      "a host NAME takes the resolve path, not the numeric fast path",
                      joined(named).find("resolved 'localhost'") != std::string::npos &&
                          joined(named).find("numeric address") == std::string::npos);
            }
        }
    }

    // ═══ TR — transport state machine, no socket involved ══════════════════
    {
        auto t = make_socket_transport(kDefault);
        check("TR-01", "a fresh transport is Idle with no error",
              t && t->state() == TransportState::Idle && t->last_error().empty());
        check("TR-02", "an empty host is rejected outright, leaving the state alone",
              !t->begin_connect("", 1234) && t->state() == TransportState::Idle);
        check("TR-03", "port 0 is rejected outright",
              !t->begin_connect("127.0.0.1", 0) && t->state() == TransportState::Idle);
        check("TR-04", "close() on an idle transport stays Idle",
              (t->close(), t->state() == TransportState::Idle));
        std::uint8_t buf[4] = {0};
        check("TR-05", "send() before Connected moves no bytes",
              t->send(buf, sizeof(buf)) == 0);
        check("TR-06", "recv() before Connected moves no bytes",
              t->recv(buf, sizeof(buf)) == 0);
        check("TR-07", "poll() in Idle is a harmless no-op",
              (t->poll(), t->state() == TransportState::Idle));

        check("TR-08", "an accepted request parks in Resolving without resolving",
              t->begin_connect("127.0.0.1", 1234) &&
                  t->state() == TransportState::Resolving);
        check("TR-09", "a second request while busy is refused",
              !t->begin_connect("127.0.0.1", 5678) &&
                  t->state() == TransportState::Resolving);
    }

    // ═══ SEC — the security gate, exercised through the real transport ═════
    // The policy is not merely a function that exists: connect() consults it,
    // and a denied address never reaches a socket.
    {
        Listener l;
        if (!l.start()) {
            ++g_total; ++g_skip;
            std::printf("  SKIP SEC-01: could not bind a loopback listener\n");
        } else {
            auto t = make_socket_transport(kDefault);  // DEFAULT policy: loopback denied
            t->begin_connect("127.0.0.1", l.port());
            t->poll();
            check("SEC-01", "the default policy refuses a loopback connect",
                  t->state() == TransportState::Failed);
            check("SEC-02", "the refusal says WHY, naming the policy",
                  t->last_error().find("policy") != std::string::npos &&
                      t->last_error().find("loopback") != std::string::npos);
            check("SEC-03", "a refused connect never reached the listener",
                  l.accept_one(50) < 0);
        }
    }

    // ═══ SEC-DENY — a policy refusal is a VALUE, not a message (GH #161) ═══
    // Both outcomes below are `Failed` with a non-empty `last_error()`, so a
    // host that has to render a deliberate block differently from a network
    // fault can only do it by asking the transport. These rows pin that the
    // answer is right in BOTH directions: a refusal names its rule, and an
    // ordinary failure does not claim to be a refusal.
    //
    // No listener is needed: a denied address never reaches a socket, which is
    // exactly what SEC-03 above proves.
    {
        auto t = make_socket_transport(kDefault);  // DEFAULT policy: loopback denied
        t->begin_connect("127.0.0.1", 9);
        t->poll();
        check("SEC-04",
              "an address-policy refusal reports the rule that refused it, "
              "without anyone parsing last_error()",
              t->state() == TransportState::Failed &&
                  t->denial_reason() == DenyReason::Loopback);

        // Same transport, allowed target this time: the previous verdict must
        // not survive into the new attempt, or the next failure would render as
        // a security block that never happened. RFC 5737 TEST-NET-3, and never
        // polled, so nothing is resolved and no packet leaves the host.
        t->begin_connect("203.0.113.1", 9);
        check("SEC-05", "a fresh request clears the previous refusal verdict",
              t->denial_reason() == DenyReason::None);
    }
    {
        // An ordinary connect failure — nothing listening on a port the kernel
        // just handed back — must report NO deny reason.
        Listener l;
        std::uint16_t dead_port = 0;
        if (l.start()) {
            dead_port = l.port();
            l.stop();
        }
        if (dead_port == 0) {
            ++g_total; ++g_skip;
            std::printf("  SKIP SEC-06: could not obtain a closed loopback port\n");
        } else {
            auto t = make_socket_transport(loopback_ok());
            t->begin_connect("127.0.0.1", dead_port);
            for (int waited = 0; waited < 2000; waited += 2) {
                t->poll();
                if (t->state() == TransportState::Failed) break;
                sleep_ms(2);
            }
            check("SEC-06",
                  "a network failure reports no deny reason, so it cannot be "
                  "mistaken for a deliberate block",
                  t->state() == TransportState::Failed &&
                      t->denial_reason() == DenyReason::None);
        }
    }

    // ═══ NET — real loopback I/O against an in-process listener ════════════
    {
        Listener l;
        if (!l.start()) {
            for (const char* id : {"NET-01", "NET-02", "NET-03", "NET-04", "NET-05",
                                   "NET-06", "NET-07", "NET-08", "NET-09", "NET-10",
                                   "NET-11", "NET-12"}) {
                ++g_total; ++g_skip;
                std::printf("  SKIP %s: could not bind a loopback listener\n", id);
            }
        } else {
            auto t = make_socket_transport(loopback_ok());
            check("NET-01", "connect request accepted",
                  t->begin_connect("127.0.0.1", l.port()));
            check("NET-02", "the connect completes through poll() alone",
                  pump_until(*t, TransportState::Connected));
            const int server = l.accept_one(1000);
            check("NET-03", "the listener sees the connection", server >= 0);
            check("NET-04", "peer_address() is the loopback address connected to",
                  t->peer_address() == ipv4(127, 0, 0, 1));

            const char*       payload = "AT+CIPSEND=7\r\n";
            const std::size_t plen    = std::strlen(payload);
            const std::size_t sent =
                t->send(reinterpret_cast<const std::uint8_t*>(payload), plen);
            check("NET-05", "send() accepts the bytes",
                  sent == plen && t->state() == TransportState::Connected);
            check("NET-06", "the server receives exactly what was sent",
                  server >= 0 && read_some(server, plen, 1000) == std::string(payload));

            std::uint8_t rx[64];
            check("NET-07", "recv() with nothing pending returns 0 and stays Connected",
                  t->recv(rx, sizeof(rx)) == 0 &&
                      t->state() == TransportState::Connected);

            const char* reply = "+IPD,4:ping";
            if (server >= 0) {
                const ssize_t w = ::send(server, reply, std::strlen(reply), 0);
                (void)w;
            }
            std::string got;
            for (int waited = 0; waited < 1000 && got.size() < std::strlen(reply);
                 waited += 2) {
                const std::size_t n = t->recv(rx, sizeof(rx));
                got.append(reinterpret_cast<char*>(rx), n);
                if (n == 0) sleep_ms(2);
            }
            check("NET-08", "recv() returns exactly what the server sent",
                  got == std::string(reply));

            if (server >= 0) ::close(server);
            check("NET-09", "a peer close moves the transport to Closed",
                  pump_recv_until_not_connected(*t) &&
                      t->state() == TransportState::Closed);
            check("NET-10", "send/recv after Closed move no bytes",
                  t->send(rx, 1) == 0 && t->recv(rx, sizeof(rx)) == 0);

            // The transport is reusable: a second connect after a close works.
            check("NET-11", "a closed transport accepts a new connect",
                  t->begin_connect("127.0.0.1", l.port()) &&
                      pump_until(*t, TransportState::Connected));
            const int server2 = l.accept_one(1000);
            t->close();
            check("NET-12", "close() on a live connection ends in Closed and the "
                            "server sees EOF",
                  t->state() == TransportState::Closed && server2 >= 0 &&
                      read_some(server2, 1, 500).empty());
            if (server2 >= 0) ::close(server2);
        }
    }

    // ═══ NET-ERR — a refused connect surfaces as Failed, not a hang ════════
    {
        Listener l;
        std::uint16_t dead_port = 0;
        if (l.start()) {
            dead_port = l.port();
            l.stop();  // nothing is listening on dead_port any more
        }
        if (dead_port == 0) {
            for (const char* id : {"NET-ERR-01", "NET-ERR-02"}) {
                ++g_total; ++g_skip;
                std::printf("  SKIP %s: could not obtain a closed loopback port\n", id);
            }
        } else {
            auto t = make_socket_transport(loopback_ok());
            t->begin_connect("127.0.0.1", dead_port);
            bool settled = false;
            for (int waited = 0; waited < 2000; waited += 2) {
                t->poll();
                if (t->state() == TransportState::Failed) { settled = true; break; }
                sleep_ms(2);
            }
            check("NET-ERR-01", "a connect to a closed port ends in Failed", settled);
            check("NET-ERR-02", "the failure carries an explanatory error string",
                  !t->last_error().empty() &&
                      t->last_error().find("connect") != std::string::npos);
        }
    }

    // ═══ SIG — a dead peer must not take the emulator down with it ═════════
    // esp_socket_posix.cpp sets MSG_NOSIGNAL (Linux) / SO_NOSIGPIPE (BSD)
    // because without one of them a write to a closed peer raises SIGPIPE and
    // the DEFAULT disposition terminates the process. Every other row here
    // sends only while the connection is provably open, or after the transport
    // has already self-transitioned — where send() short-circuits before the
    // OS call and the flag is moot. So none of them can see the protection
    // disappear.
    //
    // The reachable path is a caller that WRITES without reading: send() never
    // detects EOF (only recv() does), so a blind write after an orderly peer
    // close goes out once, draws a RST, and the next one hits EPIPE. Unguarded
    // that is a signal, not an errno.
    //
    // It has to run in a forked child because the failure mode is the death of
    // the process running the assertions.
    {
        const pid_t pid = ::fork();
        if (pid < 0) {
            skip("SIG-01", "fork() unavailable on this host");
            skip("SIG-02", "fork() unavailable on this host");
        } else if (pid == 0) {
            // CHILD. Deliberately no printf: stdout is buffered and inherited,
            // so anything written here would be duplicated by the parent. The
            // exit code carries the verdict, and _exit() skips the shared
            // stdio flush for the same reason.
            Listener l;
            if (!l.start()) ::_exit(5);
            auto t = make_socket_transport(loopback_ok());
            if (!t->begin_connect("127.0.0.1", l.port())) ::_exit(5);
            if (!pump_until(*t, TransportState::Connected)) ::_exit(5);
            const int srv = l.accept_one(1000);
            if (srv < 0) ::_exit(5);
            ::close(srv);  // orderly close by the peer — and we never recv()

            const std::uint8_t buf[4] = {'A', 'T', '\r', '\n'};
            for (int i = 0; i < 200 && t->state() == TransportState::Connected; ++i) {
                t->send(buf, sizeof(buf));  // blind write into a dead peer
                sleep_ms(2);
            }
            if (t->state() != TransportState::Failed) ::_exit(3);
            if (t->last_error().empty()) ::_exit(4);
            ::_exit(0);
        } else {
            int status = 0;
            ::waitpid(pid, &status, 0);
            const bool setup_failed = WIFEXITED(status) && WEXITSTATUS(status) == 5;
            if (setup_failed) {
                skip("SIG-01", "the child could not set up a loopback connection");
                skip("SIG-02", "the child could not set up a loopback connection");
            } else {
                check("SIG-01",
                      "a blind send to a closed peer does not signal-kill the process",
                      !WIFSIGNALED(status));
                check("SIG-02",
                      "...and surfaces as Failed with an error string instead",
                      WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }

    // ═══ ASYNC — name resolution runs OFF the calling thread ═══════════════
    //
    // These rows run LAST, after SIG's fork(). A forked child inherits only the
    // forking thread, so forking while a resolver thread holds an allocator
    // lock is a classic child-side deadlock; ordering them after SIG means no
    // resolver thread has ever existed when fork() is called.
    //
    // What is being proved is the thing the synchronous version got wrong: a
    // lookup no longer blocks whoever calls poll(), so a Reset, a quit or a
    // close() during an AT+CIPSTART returns at once instead of waiting out the
    // resolver — and the AT+CIPSTART deadline finally covers resolution.
    {
        Listener l;
        if (!l.start()) {
            for (const char* id : {"ASYNC-01", "ASYNC-02", "ASYNC-03", "ASYNC-04",
                                   "ASYNC-05", "ASYNC-06", "ASYNC-07", "ASYNC-08", "ASYNC-10",
                                   "ASYNC-11", "ASYNC-12",
                                   "ASYNC-09"})
                skip(id, "could not bind a loopback listener");
        } else {
            // (01) An IP LITERAL STAYS SYNCHRONOUS. The resolver installed here
            //      fails loudly if it is called at all, and one poll() has to be
            //      enough to leave Resolving — both halves of "no thread was
            //      involved".
            {
                fake_dns::literal_leak.store(false);
                auto t = make_socket_transport(loopback_ok(), fake_dns::never);
                t->begin_connect("127.0.0.1", l.port());
                t->poll();
                const TransportState after_one_poll = t->state();
                check("ASYNC-01",
                      "an IP literal resolves synchronously in the first poll() and "
                      "never reaches the resolver",
                      !fake_dns::literal_leak.load() &&
                          after_one_poll != TransportState::Resolving);
                t->close();
                const int srv = l.accept_one(200);
                if (srv >= 0) ::close(srv);
            }

            // (11) THE poll() THAT STARTS THE LOOKUP IS ITSELF BOUNDED. Every
            //      other timing row here measures a call made AFTER the lookup
            //      is already under way, which leaves the most important call
            //      of all untimed: the one that hands the name to the resolver.
            //      Replacing `detach()` with `join()` puts the whole stall
            //      inside THIS call and leaves a populated job behind, so the
            //      later rows time an operation that no longer has anything to
            //      wait for and pass while the emulator froze one call earlier.
            //      (Found in review, not by me — the rows below were written
            //      measuring the wrong side of the operation they are named
            //      for.)
            {
                fake_dns::gate.store(false);
                const int entered0 = fake_dns::entered.load();

                auto t = make_socket_transport(loopback_ok(), fake_dns::gated);
                t->begin_connect("first-poll.test", l.port());

                const auto t0 = std::chrono::steady_clock::now();
                t->poll();  // THE call that starts the lookup
                const long first_poll_ms = since_ms(t0);

                // ...and it really did start one, so the measurement is of a
                // genuine lookup rather than of a no-op.
                const bool started = wait_until(
                    [&] { return fake_dns::entered.load() > entered0; }, 2000);
                check("ASYNC-11",
                      "the poll() that STARTS a lookup returns immediately instead of "
                      "waiting it out",
                      first_poll_ms < 100 && started &&
                          t->state() == TransportState::Resolving);
            }

            // (02..04) A NAME goes to the resolver thread. The gate is held shut,
            //      so the lookup is provably outstanding while poll() is hammered.
            {
                fake_dns::gate.store(false);
                fake_dns::off_thread.store(false);
                const int entered0 = fake_dns::entered.load();

                auto t = make_socket_transport(loopback_ok(), fake_dns::gated);
                t->begin_connect("gated.test", l.port());
                t->poll();  // starts the lookup
                const bool in_flight = wait_until(
                    [&] { return fake_dns::entered.load() > entered0; }, 2000);

                // 200 polls while the resolver is parked. If poll() waited for
                // the lookup this loop could not finish at all — the gate is
                // still shut and stays shut for another 5 s.
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 200; ++i) t->poll();
                const long poll_ms = since_ms(t0);
                check("ASYNC-02",
                      "200 poll()s during an outstanding lookup return promptly and "
                      "leave the transport in Resolving",
                      in_flight && poll_ms < 100 &&
                          t->state() == TransportState::Resolving);
                check("ASYNC-03", "the lookup ran on a thread other than the caller's",
                      fake_dns::off_thread.load());

                fake_dns::gate.store(true);  // let it answer
                const bool connected = pump_until(*t, TransportState::Connected, 3000);
                const int  srv       = l.accept_one(1000);
                check("ASYNC-04",
                      "opening the gate completes the connect through the async path, "
                      "to the address the resolver returned",
                      connected && srv >= 0 && t->peer_address() == ipv4(127, 0, 0, 1));
                if (srv >= 0) ::close(srv);
            }

            // (05) THE SECURITY GATE STILL BITES, and it bites the RESOLVED
            //      address. The name says nothing; the resolver answers
            //      127.0.0.1; the DEFAULT policy denies loopback. Refusal must
            //      happen on this thread, after the result is consumed, and
            //      nothing may reach the listener.
            {
                fake_dns::gate.store(true);  // answer immediately
                auto t = make_socket_transport(kDefault, fake_dns::gated);
                t->begin_connect("innocent-looking.test", l.port());
                bool refused = false;
                for (int waited = 0; waited < 3000 && !refused; waited += 2) {
                    t->poll();
                    if (t->state() == TransportState::Failed) refused = true;
                    else sleep_ms(2);
                }
                check("ASYNC-05",
                      "the address policy is enforced on the RESOLVED address, and the "
                      "refused connect never reached the listener",
                      refused && t->last_error().find("policy") != std::string::npos &&
                          t->last_error().find("loopback") != std::string::npos &&
                          l.accept_one(50) < 0);
            }

            // (06) ABANDONING a lookup is immediate, and its late answer is
            //      never consumed. This is the other half of the security
            //      argument: a result produced for one request can never be
            //      applied to a later state of the transport.
            {
                fake_dns::gate.store(false);
                const int entered0   = fake_dns::entered.load();
                const int completed0 = fake_dns::completed.load();

                auto t = make_socket_transport(loopback_ok(), fake_dns::gated);
                t->begin_connect("abandoned.test", l.port());
                t->poll();
                const bool in_flight = wait_until(
                    [&] { return fake_dns::entered.load() > entered0; }, 2000);

                // Captured BEFORE the close: the row is only meaningful if the
                // transport is genuinely still waiting on the lookup at that
                // moment. A synchronous resolve cannot be in this state at all
                // — its stall lands inside poll() instead — so asserting it is
                // what stops this row passing vacuously against one.
                const bool still_resolving = (t->state() == TransportState::Resolving);

                const auto t0 = std::chrono::steady_clock::now();
                t->close();  // mid-lookup
                const long close_ms = since_ms(t0);

                fake_dns::gate.store(true);  // the abandoned lookup answers NOW
                wait_until([&] { return fake_dns::completed.load() > completed0; }, 2000);
                for (int i = 0; i < 50; ++i) t->poll();
                check("ASYNC-06",
                      "close() during a lookup returns at once and the late result "
                      "never resurrects the transport",
                      in_flight && still_resolving && close_ms < 100 &&
                          t->state() == TransportState::Closed && l.accept_one(50) < 0);

                // (10) ...AND THE ABANDONED RESULT IS NEVER APPLIED TO THE NEXT
                //      REQUEST. This is the row that makes release() dropping
                //      the job load-bearing rather than tidy: the transport is
                //      reused for a DIFFERENT address, and the answer the
                //      orphaned lookup produced (127.0.0.1, already published
                //      by now) must not be what it connects to. A stale block
                //      still attached would be consumed by the very first
                //      poll(), silently substituting one host for another.
                t->begin_connect("127.0.0.2", l.port());
                t->poll();
                check("ASYNC-10",
                      "a lookup abandoned by close() is never applied to the next "
                      "connect",
                      t->peer_address() == ipv4(127, 0, 0, 2));
                t->close();
            }

            // (07..08) DESTROYING a transport mid-lookup — the shape
            //      emulator_cold_boot()'s `emu.~Emulator()` produces on Reset,
            //      and the case that measured 2000 ms before this change. The
            //      destructor must return at once, AND the orphaned thread must
            //      go on to finish safely: the result block is jointly owned, so
            //      it outlives the transport rather than dangling. The
            //      memory-safety half of that is what an ASan/TSan run of this
            //      suite checks; the row itself proves the sequence happens.
            {
                fake_dns::gate.store(false);
                const int entered0 = fake_dns::entered.load();

                auto t = make_socket_transport(loopback_ok(), fake_dns::gated);
                t->begin_connect("orphaned.test", l.port());
                t->poll();
                const bool in_flight = wait_until(
                    [&] { return fake_dns::entered.load() > entered0; }, 2000);

                // Same guard as ASYNC-06, and sampled at the same instant: the
                // lookup must still be outstanding when the destructor runs, or
                // the row measures nothing.
                const bool still_resolving = (t->state() == TransportState::Resolving);
                const int  completed_before_destroy = fake_dns::completed.load();

                const auto t0 = std::chrono::steady_clock::now();
                t.reset();  // ~SocketTransport() with the lookup still parked
                const long destroy_ms = since_ms(t0);
                check("ASYNC-07",
                      "destroying a transport mid-lookup returns immediately instead "
                      "of waiting out the resolver",
                      in_flight && still_resolving && destroy_ms < 100);

                fake_dns::gate.store(true);
                // The increment must land AFTER the destructor: that is the
                // claim — the result block outlives the transport that started
                // it, so the thread finishes writing into memory that is still
                // alive. (Whether that write is legal is what an ASan/TSan run
                // of this suite would prove; this row proves it happens at all.)
                const bool finished_after_destroy = wait_until(
                    [&] { return fake_dns::completed.load() > completed_before_destroy; },
                    3000);
                check("ASYNC-08",
                      "...and the orphaned lookup runs to completion AFTER it, into a "
                      "result block that outlived the transport",
                      finished_after_destroy);
            }

            // (09) THE AT+CIPSTART DEADLINE NOW BOUNDS NAME RESOLUTION. Design
            //      simplification 6(a) recorded the opposite — the deadline is
            //      checked in AtEngine::poll(), which a synchronous getaddrinfo
            //      never returned from in time to be checked. With the lookup on
            //      its own thread the check runs, so a resolver that never
            //      answers is answered ERROR on OUR schedule.
            //
            //      The gate is deliberately left SHUT for the whole row: this
            //      resolver would take 5 s to give up, and the deadline is 150
            //      ms. Reverting to a synchronous resolve makes the very first
            //      poll() take those 5 s and the row fails on elapsed time.
            {
                fake_dns::gate.store(false);
                auto tr = make_socket_transport(loopback_ok(), fake_dns::gated);
                AtEngine eng(*tr);
                eng.set_connect_timeout(std::chrono::milliseconds(150));

                std::string guest;
                eng.set_output([&guest](std::uint8_t b) {
                    guest.push_back(static_cast<char>(b));
                });
                for (unsigned char c :
                     std::string("AT+CIPSTART=\"TCP\",\"never-answers.test\",80\r\n"))
                    eng.receive(c);

                const auto t0        = std::chrono::steady_clock::now();
                bool       got_error = false;
                for (int waited = 0; waited < 4000 && !got_error; waited += 2) {
                    eng.poll();
                    for (int k = 0; k < 256 && eng.wants_tick(); ++k) eng.tick(1, 1);
                    if (guest.find("ERROR") != std::string::npos) got_error = true;
                    else sleep_ms(2);
                }
                const long ms = since_ms(t0);
                check("ASYNC-09",
                      "AT+CIPSTART's deadline bounds NAME RESOLUTION, not just the "
                      "TCP handshake",
                      got_error && ms < 1500);
            }

            // (12) A THROWING RESOLVER MUST NOT ABORT THE PROCESS. `ResolveFn`
            //      is a public seam documented for consumers with their own
            //      resolver, and an exception escaping a std::thread entry
            //      point is `std::terminate` — not an error a caller can
            //      handle, a process abort. It has to degrade to a failed
            //      lookup instead, and the addresses the resolver managed to
            //      append before throwing must be DISCARDED rather than
            //      connected to.
            //
            //      NOTE THE FAILURE MODE: without the guard this row does not
            //      print FAIL, the whole suite dies with SIGABRT — which the
            //      harness reports as a crashed suite, i.e. loudly. It is
            //      deliberately not forked (the way SIG-01/02 are): resolver
            //      threads from the rows above are still parked at this point,
            //      and forking with threads alive risks a child-side allocator
            //      deadlock, which would be a worse test than no test.
            {
                const int entered0 = fake_dns::entered.load();
                auto t = make_socket_transport(loopback_ok(),
                                               fake_dns::throws_after_appending);
                t->begin_connect("throwing.test", l.port());
                bool settled = false;
                for (int waited = 0; waited < 3000 && !settled; waited += 2) {
                    t->poll();
                    if (t->state() == TransportState::Failed) settled = true;
                    else sleep_ms(2);
                }
                check("ASYNC-12",
                      "a throwing resolver degrades to a failed lookup instead of "
                      "aborting the process, and its half-built address list is "
                      "discarded",
                      settled && fake_dns::entered.load() > entered0 &&
                          t->last_error().find("threw") != std::string::npos &&
                          l.accept_one(50) < 0);
            }

            // Leave no resolver thread parked at exit.
            fake_dns::gate.store(true);
            sleep_ms(20);
        }
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass,
                g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
