#include "remote/client_link.h"

namespace jnext {
namespace remote {

static bool valid_dns_name(const std::string& h, std::string& why) {
    if (h.empty()) {
        why = "empty host";
        return false;
    }
    if (h.size() > 253) {
        why = "host name longer than 253 characters";
        return false;
    }
    bool label_empty = true;
    for (char c : h) {
        if (c == '.') {
            if (label_empty) {
                why = "empty label in host name";
                return false;
            }
            label_empty = true;
            continue;
        }
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            why = "invalid character in host name";
            return false;
        }
        label_empty = false;
    }
    if (label_empty) {
        why = "empty label in host name";
        return false;
    }
    return true;
}

static bool valid_ipv6_literal(const std::string& h, std::string& why) {
    bool colon = false;
    for (char c : h) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (c == ':') colon = true;
        else if (!hex && c != '.') {
            why = "invalid character in IPv6 address";
            return false;
        }
    }
    if (!colon || h.size() > 45) {
        why = "not an IPv6 address";
        return false;
    }
    return true;
}

static bool parse_port(const std::string& p, std::uint16_t& out, std::string& why) {
    if (p.empty()) {
        why = "missing port";
        return false;
    }
    if (p.size() > 5) {
        why = "port out of range";
        return false;
    }
    unsigned v = 0;
    for (char c : p) {
        if (c < '0' || c > '9') {
            why = "port is not a decimal number";
            return false;
        }
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v < 1 || v > 65535) {
        why = "port out of range 1..65535";
        return false;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

bool parse_link_uri(const std::string& uri, LinkSpec& out, std::string& error) {
    const std::string tcp = "tcp:";
    if (uri.compare(0, 7, "serial:") == 0) {
        error = "serial links are not in this build";
        return false;
    }
    if (uri.compare(0, tcp.size(), tcp) != 0) {
        error = "unsupported link scheme (expected tcp:HOST:PORT)";
        return false;
    }
    const std::string rest = uri.substr(tcp.size());
    std::string       host, port;
    if (!rest.empty() && rest[0] == '[') {
        const std::size_t close = rest.find(']');
        if (close == std::string::npos) {
            error = "unbalanced bracket in host";
            return false;
        }
        host = rest.substr(1, close - 1);
        if (close + 1 >= rest.size() || rest[close + 1] != ':') {
            error = "missing port";
            return false;
        }
        port = rest.substr(close + 2);
        std::string why;
        if (!valid_ipv6_literal(host, why)) {
            error = why;
            return false;
        }
    } else {
        const std::size_t colon = rest.rfind(':');
        if (colon == std::string::npos) {
            error = "missing port";
            return false;
        }
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
        if (host.find(':') != std::string::npos) {
            error = "an IPv6 address must be in brackets, [::1]";
            return false;
        }
        std::string why;
        if (!valid_dns_name(host, why)) {
            error = why;
            return false;
        }
    }
    std::uint16_t p = 0;
    std::string   why;
    if (!parse_port(port, p, why)) {
        error = why;
        return false;
    }
    out.kind = LinkKind::Tcp;
    out.host = host;
    out.port = p;
    return true;
}

}  // namespace remote
}  // namespace jnext
