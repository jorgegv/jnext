// Address policy for the emulated ESP-01 transport (GH #25, branch 2).
//
// PURE and PORTABLE by design: no sockets, no DNS, no platform headers, no
// logging. Everything here is a function of its arguments, which is what makes
// the security gate exhaustively unit-testable offline — the whole deny matrix
// is checked in esp_socket_test without opening a single socket.
//
// Policy source: owner decision on GH #25, 2026-07-28. RFC1918 private ranges
// are ALLOWED (the emulated ESP must reach machines on the user's LAN); the
// deny set is the host itself and the host's own infrastructure — loopback,
// link-local, and the cloud metadata endpoints that live inside link-local.

#include "peripheral/esp_socket.h"

#include <cstdio>

namespace esp {
namespace {

// --- IPv4 range predicates (operate on an already-normalized V4 address) ----

bool v4_unspecified(const std::uint8_t* b) { return b[0] == 0; }               // 0.0.0.0/8
bool v4_loopback(const std::uint8_t* b)    { return b[0] == 127; }             // 127.0.0.0/8
bool v4_link_local(const std::uint8_t* b)  { return b[0] == 169 && b[1] == 254; }  // 169.254/16
bool v4_multicast_reserved(const std::uint8_t* b) {
    // 224.0.0.0/4 multicast and 240.0.0.0/4 reserved (which contains the
    // 255.255.255.255 limited broadcast) — one mask covers both halves.
    return (b[0] & 0xE0) == 0xE0;
}
bool v4_private(const std::uint8_t* b) {
    if (b[0] == 10) return true;                                    // 10/8
    if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return true;       // 172.16/12
    if (b[0] == 192 && b[1] == 168) return true;                    // 192.168/16
    if (b[0] == 100 && (b[1] & 0xC0) == 0x40) return true;          // 100.64/10 CGNAT
    return false;
}
bool v4_cloud_metadata(const std::uint8_t* b) {
    // 169.254.169.254 — AWS IMDS, GCP, Azure, Oracle, DigitalOcean, OpenStack.
    if (b[0] == 169 && b[1] == 254 && b[2] == 169 && b[3] == 254) return true;
    // 100.100.100.200 — Alibaba Cloud. Sits in CGNAT (100.64/10), which is
    // ALLOWED as a private range, so it needs its own rule or it slips through.
    if (b[0] == 100 && b[1] == 100 && b[2] == 100 && b[3] == 200) return true;
    return false;
}

// --- IPv6 range predicates -------------------------------------------------

bool v6_all_zero(const std::uint8_t* b, int from, int to) {
    for (int i = from; i < to; ++i)
        if (b[i] != 0) return false;
    return true;
}
bool v6_unspecified(const std::uint8_t* b) { return v6_all_zero(b, 0, 16); }   // ::
bool v6_loopback(const std::uint8_t* b) {
    return v6_all_zero(b, 0, 15) && b[15] == 1;                                // ::1
}
bool v6_link_local(const std::uint8_t* b) {
    return b[0] == 0xfe && (b[1] & 0xC0) == 0x80;                              // fe80::/10
}
bool v6_multicast(const std::uint8_t* b) { return b[0] == 0xff; }              // ff00::/8
bool v6_unique_local(const std::uint8_t* b) { return (b[0] & 0xFE) == 0xFC; }  // fc00::/7
bool v6_cloud_metadata(const std::uint8_t* b) {
    // fd00:ec2::254 — the AWS IMDS IPv6 endpoint. Inside fc00::/7, which is
    // ALLOWED as the IPv6 equivalent of RFC1918, so it needs its own rule.
    static const std::uint8_t kAwsV6[16] = {0xfd, 0x00, 0x0e, 0xc2, 0, 0, 0, 0,
                                            0,    0,    0,    0,    0, 0, 0x02, 0x54};
    for (int i = 0; i < 16; ++i)
        if (b[i] != kAwsV6[i]) return false;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------

IpAddress ipv4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    IpAddress ip;
    ip.family   = IpFamily::V4;
    ip.bytes[0] = a;
    ip.bytes[1] = b;
    ip.bytes[2] = c;
    ip.bytes[3] = d;
    return ip;
}

IpAddress ipv6(const std::array<std::uint8_t, 16>& raw) {
    IpAddress ip;
    ip.family = IpFamily::V6;
    ip.bytes  = raw;
    return ip;
}

bool operator==(const IpAddress& a, const IpAddress& b) {
    if (a.family != b.family) return false;
    const int n = (a.family == IpFamily::V4) ? 4 : 16;
    for (int i = 0; i < n; ++i)
        if (a.bytes[i] != b.bytes[i]) return false;
    return true;
}

std::string to_string(const IpAddress& ip) {
    char buf[64];
    if (ip.family == IpFamily::V4) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip.bytes[0], ip.bytes[1],
                      ip.bytes[2], ip.bytes[3]);
        return buf;
    }
    // Full 8-group form, no "::" elision: a log line has to be unambiguous,
    // not short, and an elided address cannot be grepped for reliably.
    std::string out;
    for (int i = 0; i < 8; ++i) {
        const unsigned group =
            (static_cast<unsigned>(ip.bytes[i * 2]) << 8) | ip.bytes[i * 2 + 1];
        std::snprintf(buf, sizeof(buf), "%x", group);
        if (i) out += ':';
        out += buf;
    }
    return out;
}

IpAddress normalize(const IpAddress& ip) {
    if (ip.family == IpFamily::V4) return ip;
    const std::uint8_t* b = ip.bytes.data();

    // IPv4-mapped: ::ffff:a.b.c.d — what a dual-stack getaddrinfo hands back,
    // and the obvious way to smuggle 127.0.0.1 past a V4-only loopback check.
    if (v6_all_zero(b, 0, 10) && b[10] == 0xff && b[11] == 0xff)
        return ipv4(b[12], b[13], b[14], b[15]);

    // NAT64 well-known prefix 64:ff9b::/96 (RFC 6052) — a genuinely
    // translated path to the embedded V4 address on a NAT64 network.
    if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b &&
        v6_all_zero(b, 4, 12))
        return ipv4(b[12], b[13], b[14], b[15]);

    // IPv4-compatible ::a.b.c.d (deprecated by RFC 4291, still resolvable).
    // ::0.0.0.x is excluded so that :: and ::1 keep their own V6 identities
    // rather than becoming 0.0.0.0 / 0.0.0.1.
    if (v6_all_zero(b, 0, 12) && !v6_all_zero(b, 12, 15))
        return ipv4(b[12], b[13], b[14], b[15]);

    return ip;
}

const char* deny_reason_text(DenyReason r) {
    switch (r) {
        case DenyReason::None:                return "allowed";
        case DenyReason::Loopback:            return "loopback address";
        case DenyReason::LinkLocal:           return "link-local address";
        case DenyReason::CloudMetadata:       return "cloud metadata address";
        case DenyReason::Unspecified:         return "unspecified address";
        case DenyReason::MulticastOrReserved: return "multicast/reserved address";
        case DenyReason::Private:             return "private address";
    }
    return "unknown";
}

DenyReason classify_address(const IpAddress& raw, const AddressPolicy& policy) {
    const IpAddress    ip = normalize(raw);
    const std::uint8_t* b = ip.bytes.data();
    const bool         v4 = (ip.family == IpFamily::V4);

    // Rule ORDER matters, and only for two reasons — both real:
    //   * cloud metadata is INSIDE link-local (v4) and inside ULA (v6), so it
    //     must be tested first or it would be reported as the vaguer reason;
    //   * a rule whose flag is OFF must fall through rather than return
    //     "allowed", so that (say) disabling deny_cloud_metadata still leaves
    //     169.254.169.254 denied by deny_link_local.
    if (policy.deny_cloud_metadata &&
        (v4 ? v4_cloud_metadata(b) : v6_cloud_metadata(b)))
        return DenyReason::CloudMetadata;

    if (policy.deny_loopback && (v4 ? v4_loopback(b) : v6_loopback(b)))
        return DenyReason::Loopback;

    if (policy.deny_link_local && (v4 ? v4_link_local(b) : v6_link_local(b)))
        return DenyReason::LinkLocal;

    if (policy.deny_unspecified && (v4 ? v4_unspecified(b) : v6_unspecified(b)))
        return DenyReason::Unspecified;

    if (policy.deny_multicast_reserved &&
        (v4 ? v4_multicast_reserved(b) : v6_multicast(b)))
        return DenyReason::MulticastOrReserved;

    if (policy.deny_private && (v4 ? v4_private(b) : v6_unique_local(b)))
        return DenyReason::Private;

    return DenyReason::None;
}

bool select_candidate(const std::vector<IpAddress>& candidates,
                      const AddressPolicy& policy, IpAddress& chosen,
                      DenyReason& reason) {
    reason = DenyReason::None;
    if (candidates.empty()) return false;

    DenyReason first_deny = DenyReason::None;
    bool       have_v6    = false;
    IpAddress  best_v6;

    for (const auto& c : candidates) {
        const DenyReason d = classify_address(c, policy);
        if (d != DenyReason::None) {
            if (first_deny == DenyReason::None) first_deny = d;
            continue;
        }
        // IPv4 wins outright: the real ESP-01's AT firmware has no IPv6 stack,
        // and every evidenced consumer is IPv4-only.
        if (normalize(c).family == IpFamily::V4) {
            chosen = c;
            return true;
        }
        if (!have_v6) {
            have_v6 = true;
            best_v6 = c;
        }
    }

    if (have_v6) {
        chosen = best_v6;
        return true;
    }
    reason = first_deny;
    return false;
}

}  // namespace esp
