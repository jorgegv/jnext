#include "platform/sdcard_forward.h"

#ifndef __EMSCRIPTEN__

#include <array>
#include <chrono>
#include <memory>
#include <thread>

#include "remote/zrcp/zrcp_format.h"
#include "remote/zrcp/zrcp_server.h"

namespace sdforward {

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kReplyCap = 64 * 1024;

bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::string process_of(const sdcard::LockHolder& h) { return h.pid > 0 ? std::to_string(h.pid) : std::string("?"); }

std::string subject_of(const ForwardRequest& r) {
    return r.writes_lock ? "the SD-writes file " + r.writes : "the SD card image " + r.image;
}

int fail(std::string& out, const std::string& text) {
    out = "error: --sdcard-file-add: " + text;
    return 5;
}

// Read until the received text ends with `end` (true), or `ms` pass, the peer closes, or `cap` bytes arrive (false;
// `why` says which).
bool read_until(esp::EspTransport& t, std::string& buf, const std::string& end, unsigned ms, std::string& why) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    std::uint8_t chunk[4096];
    for (;;) {
        t.poll();
        const std::size_t n = t.recv(chunk, sizeof(chunk));
        if (n) {
            buf.append(reinterpret_cast<const char*>(chunk), n);
            if (buf.size() > kReplyCap) { why = "its answer is longer than 64 KiB"; return false; }
        }
        if (ends_with(buf, end)) return true;
        if (t.state() == esp::TransportState::Failed || t.state() == esp::TransportState::Closed) {
            // a last read may still hold bytes the peer sent before it closed
            if (n == 0) { why = "it closed the connection"; return false; }
            continue;
        }
        if (Clock::now() >= deadline) { why = "it did not answer in time"; return false; }
        if (n == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool send_all(esp::EspTransport& t, const std::string& text, unsigned ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    std::size_t at = 0;
    while (at < text.size()) {
        t.poll();
        if (t.state() != esp::TransportState::Connected) return false;
        at += t.send(reinterpret_cast<const std::uint8_t*>(text.data()) + at, text.size() - at);
        if (at < text.size()) {
            if (Clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

}  // namespace

bool connect_address(const std::string& recorded, esp::IpAddress& out, std::string& why) {
    esp::IpAddress ip;
    if (!esp::parse_ip(recorded, ip)) {
        why = "the running jnext's ZRCP server is recorded on '" + recorded + "', which is not an IP address";
        return false;
    }
    if (ip.family == esp::IpFamily::V6) {
        bool zero_prefix = true;   // :: and ::1 (checked before normalize(), which reads ::/96 as IPv4)
        for (int i = 0; i < 15; ++i) zero_prefix = zero_prefix && ip.bytes[static_cast<std::size_t>(i)] == 0;
        if (zero_prefix && ip.bytes[15] <= 1) {
            std::array<std::uint8_t, 16> lo{};
            lo[15] = 1;
            out = esp::ipv6(lo);
            return true;
        }
        ip = esp::normalize(ip);
    }
    if (ip.family == esp::IpFamily::V4) {
        const bool any = ip.bytes[0] == 0 && ip.bytes[1] == 0 && ip.bytes[2] == 0 && ip.bytes[3] == 0;
        if (any || ip.bytes[0] == 127) {
            out = esp::ipv4(127, 0, 0, 1);
            return true;
        }
    }
    why = "the running jnext's ZRCP server listens on " + recorded +
          ", which is not this machine's loopback; it cannot take the copy (give it --debug-listen-address 127.0.0.1 or 0.0.0.0)";
    return false;
}

int forward_file_add(const sdcard::LockHolder& holder, const ForwardRequest& req, std::string& out, unsigned welcome_ms,
                     unsigned reply_ms) {
    const std::string proc = process_of(holder);
    if (!holder.has_endpoint) {
        out = "error: " + subject_of(req) + " is in use by another jnext (process " + proc +
              "), which is not listening on ZRCP. Restart it with --zrcp-port PORT and run this command again: the file "
              "will be copied through it.";
        return 5;
    }
    esp::IpAddress to;
    std::string why;
    if (!connect_address(holder.address, to, why)) return fail(out, why);
    const std::string where = esp::to_string(to) + ":" + std::to_string(holder.port);

    esp::AddressPolicy policy;
    policy.deny_loopback = false;     // loopback is the only place this client goes
    policy.deny_unspecified = false;
    std::unique_ptr<esp::EspTransport> t = esp::make_socket_transport(policy);
    if (!t->begin_connect(esp::to_string(to), holder.port)) return fail(out, "cannot start a connection to " + where);
    const auto deadline = Clock::now() + std::chrono::milliseconds(welcome_ms);
    for (;;) {
        t->poll();
        const esp::TransportState s = t->state();
        if (s == esp::TransportState::Connected) break;
        if (s == esp::TransportState::Failed || s == esp::TransportState::Closed || Clock::now() >= deadline)
            return fail(out, "the running jnext (process " + proc + ") cannot be reached on " + where + " (" +
                                 (s == esp::TransportState::Failed ? t->last_error() : std::string("no answer")) + ")");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const std::string prompt = jnext::remote::zrcp::PROMPT;
    std::string buf, bad;
    if (!read_until(*t, buf, prompt, welcome_ms, bad)) {
        if (buf.rfind("Error. Another ZRCP client is connected", 0) == 0)
            return fail(out, "another ZRCP client is connected to the running jnext (process " + proc +
                                 ") (a debugger?); disconnect it and run the command again");
        return fail(out, "what listens on " + where + " did not answer as ZRCP (" + bad + ")");
    }
    if (buf != jnext::remote::zrcp::WELCOME)
        return fail(out, "what listens on " + where + " did not answer as ZRCP (an unexpected welcome)");

    namespace z = jnext::remote::zrcp;
    std::string line = "sd-file-add " + z::encode_arg(req.image) + " " + z::encode_arg(req.host) + " " +
                       z::encode_arg(req.card_path);
    if (req.force) line += " --force";
    if (req.slot >= 0) line += " --card " + std::string(req.slot == 0 ? "1" : "2");
    if (req.writes_given) line += " --writes " + z::encode_arg(req.writes);
    line += "\n";
    if (!send_all(*t, line, welcome_ms)) return fail(out, "the running jnext (process " + proc + ") closed the connection");

    buf.clear();
    if (!read_until(*t, buf, "\n" + prompt, reply_ms, bad))
        return fail(out, "the running jnext (process " + proc + ") on " + where + " gave no usable answer (" + bad + ")");
    send_all(*t, "quit\n", 1000);
    t->close();

    const std::string body = buf.substr(0, buf.size() - prompt.size() - 1);
    auto decode = [](const std::string& s) {
        std::string d;
        return z::decode_arg(s, d) ? d : s;
    };
    if (body.rfind("OK. ", 0) == 0) {
        out = decode(body.substr(4)) + " (by the running jnext, process " + proc + ")";
        return 0;
    }
    if (body.rfind("Error. [", 0) == 0 && body.size() > 9 && body[9] == ']' && body[8] >= '1' && body[8] <= '6' &&
        body.compare(9, 2, "] ") == 0) {
        out = "error: --sdcard-file-add: " + decode(body.substr(11));
        return body[8] - '0';
    }
    if (body == "Unknown command")
        return fail(out, "the running jnext (process " + proc + ") does not have the ZRCP command sd-file-add");
    return fail(out, "the running jnext (process " + proc + ") on " + where + " answered with a protocol error");
}

}  // namespace sdforward

#endif  // !__EMSCRIPTEN__
