// AT command engine for the emulated ESP-01 (GH #25, branch 3 of 6).
// Rationale, evidence and the deliberate modelling simplifications are all in
// esp_at.h — read that first; this file only implements what it states.

#include "esp01/esp_at.h"

#include "esp01/esp_log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

namespace esp {

namespace {

/// Case-insensitive prefix test; `rest` receives what follows the prefix. An
/// EXACT match is the case where `rest` comes back empty, which is how the
/// dispatch table serves both kinds of entry with one comparison.
bool iprefix(const std::string& s, const char* lit, std::string& rest) {
    std::size_t i = 0;
    for (; lit[i] != '\0'; ++i) {
        if (i >= s.size()) return false;
        if (std::toupper(static_cast<unsigned char>(s[i])) !=
            std::toupper(static_cast<unsigned char>(lit[i])))
            return false;
    }
    rest.assign(s, i, std::string::npos);
    return true;
}

/// Case-insensitive whole-string compare. Command NAMES are folded; command
/// ARGUMENTS never are, so a hostname keeps its case.
bool ieq(const std::string& s, const char* lit) {
    std::string rest;
    return iprefix(s, lit, rest) && rest.empty();
}

/// Parse a run of decimal digits filling the WHOLE of `s`. Returns false on an
/// empty string, any non-digit, or a value above `limit` — so a malformed
/// argument becomes `ERROR` rather than a silently clamped number.
bool parse_uint(const std::string& s, std::uint32_t limit, std::uint32_t& out) {
    if (s.empty()) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
        if (v > limit) return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}

/// Render bytes for a log line with C escapes, so CR/LF framing and the
/// trailing space of the `> ` prompt are visible rather than invisible.
std::string escape(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(len + 8);
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint8_t c = data[i];
        switch (c) {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\\': out += "\\\\"; break;
            default:
                if (c >= 0x20 && c < 0x7F) {
                    out += static_cast<char>(c);
                } else {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                    out += buf;
                }
        }
    }
    return out;
}

std::string escape(const std::string& s) {
    return escape(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// Pull one `"…"`-quoted field off the front of `s`, consuming it (and any
/// single following comma). Returns false if `s` does not start with a quote
/// or the quote is unterminated.
bool take_quoted(std::string& s, std::string& out) {
    if (s.empty() || s.front() != '"') return false;
    const std::size_t end = s.find('"', 1);
    if (end == std::string::npos) return false;
    out.assign(s, 1, end - 1);
    s.erase(0, end + 1);
    if (!s.empty() && s.front() == ',') s.erase(0, 1);
    return true;
}

/// Pull one unquoted comma-delimited field off the front of `s`.
std::string take_field(std::string& s) {
    const std::size_t comma = s.find(',');
    std::string field;
    if (comma == std::string::npos) {
        field.swap(s);
    } else {
        field.assign(s, 0, comma);
        s.erase(0, comma + 1);
    }
    return field;
}

}  // namespace

// ─── Dispatch table ───────────────────────────────────────────────────
//
// Scanned in order, first match wins. PREFIX entries are listed longest-first
// where one name could shadow another, so that the more specific command is
// reached: `AT+CIPSENDEX=` before `AT+CIPSEND=`, `AT+UART_CUR=`/`_DEF=` before
// `AT+UART=`. (They happen to diverge before the shorter name ends, so order
// is belt-and-braces rather than load-bearing — but the next command added may
// not be so lucky.)
const AtEngine::CommandEntry AtEngine::kCommands[] = {
    {"AT",             false, &AtEngine::cmd_at},
    {"ATE0",           false, &AtEngine::cmd_echo_off},
    {"ATE1",           false, &AtEngine::cmd_echo_on},
    {"AT+RST",         false, &AtEngine::cmd_reset},
    {"AT+CIPCLOSE",    false, &AtEngine::cmd_cipclose},
    {"AT+GMR",         false, &AtEngine::cmd_gmr},
    {"AT+CWJAP?",      false, &AtEngine::cmd_cwjap},
    {"AT+CIPSTA?",     false, &AtEngine::cmd_cipsta},
    {"AT+CIFSR",       false, &AtEngine::cmd_cifsr},
    {"AT+CIPDNS_CUR?", false, &AtEngine::cmd_cipdns},
    {"AT+CIPSTART=",   true,  &AtEngine::cmd_cipstart},
    {"AT+CIPSENDEX=",  true,  &AtEngine::cmd_cipsendex},
    {"AT+CIPSEND=",    true,  &AtEngine::cmd_cipsend},
    {"AT+CIPMUX=",     true,  &AtEngine::cmd_cipmux},
    {"AT+UART_CUR=",   true,  &AtEngine::cmd_uart},
    {"AT+UART_DEF=",   true,  &AtEngine::cmd_uart},
    {"AT+UART=",       true,  &AtEngine::cmd_uart},
};
const std::size_t AtEngine::kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

AtEngine::AtEngine(EspTransport& transport) {
    // v1.0 gives a transport to slot 0 and to nothing else, which is what
    // makes the other slots inert without any extra guard: every per-slot
    // loop skips a slot with no transport.
    conn_[SINGLE_CID].transport = &transport;
}

// ─── Guest TX -> engine ───────────────────────────────────────────────

void AtEngine::receive(std::uint8_t byte) {
    if (conn_[SINGLE_CID].connecting) {
        // A connect is in flight and its OK/ERROR has not been decided yet.
        // Real firmware answers `busy p...`, which is on the never-emit list,
        // so instead the input waits: nothing is lost and nothing is answered
        // out of order.
        deferred_.push_back(byte);
        // `log_hex_byte` rather than a `{:#04x}` spec: the seam substitutes
        // `{}` and ignores specs on purpose (esp_log.h), and an ostream would
        // print a `uint8_t` as a character.
        log_trace("rx byte {} deferred (connect in flight)", log_hex_byte(byte));
        refresh_tick_gate();
        return;
    }
    feed(byte);
    refresh_tick_gate();
}

void AtEngine::feed(std::uint8_t byte) {
    // THE LF OF A CRLF TERMINATOR BELONGS TO THE TERMINATOR, IN EVERY MODE.
    // This is not cosmetic. NXtel sends `AT+CIPSEND=3\r\n` and then, as a
    // separate write, its payload (esp.asm:44-46). If the LF is still
    // in-flight when the command switches to payload mode it becomes payload
    // byte 1 — the guest's own line terminator is transmitted to the peer, the
    // real first byte is displaced, and every byte after it is off by one.
    const bool terminator_lf = expect_lf_;
    expect_lf_ = false;
    if (terminator_lf && byte == '\n') return;

    if (mode_ == Mode::Payload) {
        payload_.push_back(byte);
        if (payload_.size() >= payload_len_) finish_payload();
        return;
    }

    if (byte == '\r') {
        expect_lf_ = true;
        // Echo the terminator whole, and BEFORE the reply — with the echo
        // state as it was when the line arrived, so `ATE1` does not retro-echo
        // its own line and `ATE0` does not lose its terminator.
        if (echo_) queue("\r\n");
        dispatch_line();
        line_.clear();
        line_overflow_ = false;
        return;
    }
    // A stray LF — one that does not follow a CR — can never be part of an AT
    // command, so it is dropped rather than terminating a second, empty line.
    // Together with the terminator rule above, that is what makes nextsync's
    // `\r\n` probe, and NXtel's trailing payload CR,LF, produce exactly ONE
    // `ERROR` and not two.
    if (byte == '\n') return;

    // Echo is command-mode only and byte-by-byte — real firmware never echoes
    // a CIPSEND payload either.
    if (echo_) queue_raw(&byte, 1);

    if (line_.size() < MAX_COMMAND_LEN) {
        line_.push_back(static_cast<char>(byte));
    } else {
        line_overflow_ = true;
    }
}

void AtEngine::dispatch_line() {
    if (line_overflow_) {
        // Refused WHOLE. Running the truncated prefix would be worse than
        // useless: 512 characters of an `AT+CIPSTART=` is still a valid
        // `AT+CIPSTART=`, so a truncate-and-run reading would open a
        // connection to a host the guest never named.
        log_debug("AT line over {} bytes — refusing the whole line", MAX_COMMAND_LEN);
        queue_error();
        return;
    }

    log_debug("AT <- \"{}\"", escape(line_));

    // The empty line. nextsync sends it as its very first byte pair and again
    // after every baud switch, and parses `ERROR` as proof the link is alive.
    if (line_.empty()) {
        queue_error();
        return;
    }

    std::string rest;
    for (std::size_t i = 0; i < kCommandCount; ++i) {
        const CommandEntry& e = kCommands[i];
        if (!iprefix(line_, e.name, rest)) continue;
        if (!e.prefix && !rest.empty()) continue;  // exact entry, extra text
        (this->*e.handler)(rest);
        return;
    }

    log_debug("unsupported command \"{}\" — answering ERROR", escape(line_));
    queue_error();
}

void AtEngine::finish_payload() {
    Connection& c = conn_[payload_cid_];
    log_debug("payload complete: {} byte(s) from the guest for cid {}", payload_.size(),
               payload_cid_);
    log_trace("payload bytes: {}", escape(payload_.data(), payload_.size()));

    // Offer the whole payload to the socket at once; whatever the kernel will
    // not take is buffered and retried from poll(). `SEND OK` is emitted as
    // soon as the bytes are ACCEPTED BY THE ENGINE — `SEND FAIL` is on the
    // never-emit list, so a later socket failure surfaces as `CLOSED`, which
    // is a state the NextZXOS driver does understand.
    for (std::uint8_t b : payload_) c.tx.push_back(b);
    payload_.clear();
    payload_len_ = 0;
    mode_ = Mode::Command;

    flush_outbound(payload_cid_);
    queue("\r\nSEND OK\r\n");
}

// ─── Command handlers ─────────────────────────────────────────────────

void AtEngine::cmd_at(const std::string&) { queue_ok(); }

void AtEngine::cmd_echo_off(const std::string&) {
    echo_ = false;
    log_debug("echo off");
    queue_ok();
}

void AtEngine::cmd_echo_on(const std::string&) {
    echo_ = true;
    log_debug("echo on");
    queue_ok();
}

void AtEngine::cmd_reset(const std::string&) {
    // A module reboot. Any live connection dies with it, and deliberately
    // WITHOUT a `CLOSED`: the guest asked for the reset, and an unsolicited
    // CLOSED is exactly what ESPATreadme.TXT:92 warns leaves the driver in an
    // unknown state.
    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (!c.transport) continue;
        if (c.open || c.connecting) {
            log_info("AT+RST — dropping the connection to {}:{}", c.host, c.port);
            c.transport->close();
        }
        c.open          = false;
        c.connecting    = false;
        c.close_pending = false;
        c.rx.clear();
        c.tx.clear();
    }
    mode_        = Mode::Command;
    payload_len_ = 0;
    payload_.clear();
    deferred_.clear();
    echo_ = false;  // power-on default — see simplification (2) in the header

    // `ready` is on the never-emit list; the two WIFI URCs are what the
    // NextZXOS driver actually looks for after a reset.
    queue("\r\nOK\r\n\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n");
}

void AtEngine::cmd_cipstart(const std::string& args) {
    Connection& c = conn_[SINGLE_CID];

    // `|| c.connecting` is UNREACHABLE today and kept deliberately. Reaching
    // it needs a command dispatched while a connect is in flight, which
    // `receive()` prevents by deferring every byte, and which the replay loop
    // in `resolve_connect` prevents by stopping the moment `connecting` is set
    // again. Both guards are NON-LOCAL, so a reader of this function cannot
    // see them — and `open || connecting` is one coherent "the slot is busy"
    // predicate, which per-slot CIPSTART (issue #154) will need in full. The
    // contrast with the pacing reset deleted in `tick()` is deliberate: that
    // was a redundant STATE MUTATION, which can silently paper over a bug;
    // this is a redundant GUARD, which can only refuse something twice.
    if (c.open || c.connecting) {
        // Real firmware says `ALREADY CONNECTED`, which nothing parses.
        log_debug("AT+CIPSTART while a connection exists — answering ERROR");
        queue_error();
        return;
    }

    std::string rest = args;
    std::string proto;
    if (!take_quoted(rest, proto) || !ieq(proto, "TCP")) {
        // UDP and SSL are out of scope for v1.0 and have no consumer.
        log_debug("AT+CIPSTART protocol \"{}\" unsupported — answering ERROR", escape(proto));
        queue_error();
        return;
    }

    std::string host;
    if (!take_quoted(rest, host) || host.empty()) {
        log_debug("AT+CIPSTART has no usable host — answering ERROR");
        queue_error();
        return;
    }

    // Port, then an OPTIONAL keepalive that NXtel really does send
    // (`AT+CIPSTART="TCP","nx.nxtel.org",23280,7200`, esp.asm/NXterm.asm) and
    // that we accept and ignore.
    std::uint32_t port = 0;
    if (!parse_uint(take_field(rest), 65535, port) || port == 0) {
        log_debug("AT+CIPSTART has no usable port — answering ERROR");
        queue_error();
        return;
    }
    if (!rest.empty()) {
        std::uint32_t keepalive = 0;
        if (!parse_uint(take_field(rest), 7200, keepalive) || !rest.empty()) {
            log_debug("AT+CIPSTART trailing arguments unparseable — answering ERROR");
            queue_error();
            return;
        }
    }

    c.host = host;
    c.port = static_cast<std::uint16_t>(port);

    if (!c.transport->begin_connect(c.host, c.port)) {
        log_warn("connection to {}:{} refused before it started", c.host, c.port);
        queue_error();
        return;
    }
    // The reply is DEFERRED until poll() sees the transport settle. There is
    // no synchronous connect in the transport interface by design, and
    // answering OK before the socket is up would let the guest send into
    // nothing.
    c.connecting       = true;
    c.connect_deadline = std::chrono::steady_clock::now() + connect_timeout_;
    log_debug("AT+CIPSTART accepted: {}:{} on cid {} — reply deferred to poll() (deadline {} ms)",
               c.host, c.port, SINGLE_CID, connect_timeout_.count());
}

void AtEngine::cmd_cipsend(const std::string& args)   { begin_send(args, "AT+CIPSEND"); }
void AtEngine::cmd_cipsendex(const std::string& args) { begin_send(args, "AT+CIPSENDEX"); }

void AtEngine::begin_send(const std::string& args, const char* name) {
    if (!conn_[SINGLE_CID].open) {
        log_debug("{} with no open connection — answering ERROR", name);
        queue_error();
        return;
    }

    std::uint32_t len = 0;
    if (!parse_uint(args, MAX_SEND_LEN, len) || len == 0) {
        log_debug("{} length \"{}\" out of range — answering ERROR", name, escape(args));
        queue_error();
        return;
    }

    payload_cid_ = SINGLE_CID;
    payload_len_ = len;
    payload_.clear();
    payload_.reserve(len);
    mode_ = Mode::Payload;

    // THE prompt. Three parsers need these exact bytes and none of them has a
    // timeout; emitting anything else hangs the guest forever.
    queue("\r\nOK\r\n> ");
    log_debug("{}={} accepted — '>' prompt issued, awaiting {} payload byte(s)", name, len,
               len);
}

void AtEngine::cmd_cipclose(const std::string&) {
    Connection& c = conn_[SINGLE_CID];
    if (!c.open && !c.connecting) {
        // nextsync loops AT+CIPCLOSE up to 10 times WHILE ERROR IS NOT SEEN,
        // so "nothing was open" must really answer ERROR or it spins.
        log_debug("AT+CIPCLOSE with nothing open — answering ERROR");
        queue_error();
        return;
    }
    log_info("connection to {}:{} closed by the guest (AT+CIPCLOSE)", c.host, c.port);
    c.transport->close();
    c.open          = false;
    c.connecting    = false;
    c.close_pending = false;
    c.rx.clear();
    c.tx.clear();
    // A connection genuinely closed, so CLOSED is honest here — and NXtel's
    // 5-byte `OSED\r` window is what it looks for.
    queue("\r\nCLOSED\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cipmux(const std::string& args) {
    if (args == "0") {
        queue_ok();
        return;
    }
    // Refused, not ignored: nextsync never sends AT+CIPMUX at all and its
    // `+IPD` byte FSM cannot survive the multiplexed form, so silently
    // accepting =1 would promise something that breaks the one client that
    // cannot ask for it back. The connection TABLE is sized for it (issue
    // #154); the COMMAND is not implemented.
    log_debug("AT+CIPMUX={} unsupported (single-connection only) — answering ERROR",
               escape(args));
    queue_error();
}

void AtEngine::cmd_uart(const std::string& args) {
    std::string rest = args;
    std::uint32_t baud = 0;
    if (!parse_uint(take_field(rest), 5000000, baud) || baud == 0) {
        log_debug("AT+UART baud \"{}\" unparseable — answering ERROR", escape(args));
        queue_error();
        return;
    }
    requested_baud_ = baud;
    // Nothing to do beyond acknowledging: guest-bound pacing is read from the
    // channel's LIVE prescaler on every tick, so the guest reprogramming its
    // own side is all that is needed for the rate to follow. nextsync does not
    // even wait for this OK — it switches immediately and re-probes with the
    // empty line.
    log_debug("AT+UART baud set to {} — pacing follows the live prescaler", baud);
    queue_ok();
}

// ── Static diagnostics ────────────────────────────────────────────────
//
// Canned, constant replies whose only job is to keep NXtel's Network Settings
// screen from hanging (c31.asm:147-196). NXtel does not parse whole lines — it
// runs `strstr` for a short anchor and prints up to a terminator — so each
// reply below is shaped around the anchor it must contain. Every value is
// synthetic per the owner's SSID decision.

void AtEngine::cmd_gmr(const std::string&) {
    // Anchors: "T version:" (inside "AT version:") and "DK version:" (inside
    // "SDK version:"), each printed up to the following '(' ON THE SAME LINE.
    // The parenthesised text is therefore invisible to NXtel, which is where
    // the honest "this is not real firmware" marker goes.
    queue("\r\nAT version:1.7.4.0(jnext emulated ESP-01)\r\n"
          "SDK version:3.0.4(jnext emulated ESP-01)\r\n"
          "compile time:Jan  1 2026 00:00:00\r\n"
          "Bin version(Wroom 02):1.7.4\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cwjap(const std::string&) {
    // Anchors: `CWJAP:"` for the SSID, then the first `","` for the AP MAC;
    // both printed up to the next '"'.
    queue(std::string("\r\n+CWJAP:\"") + SSID + "\",\"" + AP_BSSID + "\",1,-55\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cipsta(const std::string&) {
    // Anchors: `gateway:"` and `netmask:"`.
    queue(std::string("\r\n+CIPSTA:ip:\"") + STA_IP + "\"\r\n"
          "+CIPSTA:gateway:\"" + GATEWAY_IP + "\"\r\n"
          "+CIPSTA:netmask:\"" + NETMASK + "\"\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cifsr(const std::string&) {
    // Anchors: `TAIP,"` and `TAMAC,"`.
    queue(std::string("\r\n+CIFSR:STAIP,\"") + STA_IP + "\"\r\n"
          "+CIFSR:STAMAC,\"" + STA_MAC + "\"\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cipdns(const std::string&) {
    // Anchor: `+CIPDNS_CUR:` (and LF + the same for the second server), each
    // printed up to a CR.
    queue(std::string("\r\n+CIPDNS_CUR:") + DNS1 + "\r\n+CIPDNS_CUR:" + DNS2 + "\r\n\r\nOK\r\n");
}

// ─── Engine -> guest ──────────────────────────────────────────────────

void AtEngine::queue(const char* text) {
    queue_raw(reinterpret_cast<const std::uint8_t*>(text), std::char_traits<char>::length(text));
}

void AtEngine::queue(const std::string& text) {
    queue_raw(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

void AtEngine::queue_raw(const std::uint8_t* data, std::size_t len) {
    if (len == 0) return;
    log_debug("AT -> \"{}\"", escape(data, len));
    for (std::size_t i = 0; i < len; ++i) out_.push_back(data[i]);
    refresh_tick_gate();
}

void AtEngine::queue_ipd_header(std::size_t cid, bool multiplexed, std::size_t len) {
    char hdr[32];
    const int n = multiplexed
                      ? std::snprintf(hdr, sizeof(hdr), "\r\n+IPD,%u,%u:",
                                      static_cast<unsigned>(cid), static_cast<unsigned>(len))
                      : std::snprintf(hdr, sizeof(hdr), "\r\n+IPD,%u:",
                                      static_cast<unsigned>(len));
    queue_raw(reinterpret_cast<const std::uint8_t*>(hdr), static_cast<std::size_t>(n));
}

// ─── Wall-clock half ──────────────────────────────────────────────────

void AtEngine::poll() {
    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (!c.transport) continue;  // v1.0: every slot but SINGLE_CID
        c.transport->poll();
        resolve_connect(cid);
        flush_outbound(cid);
        drain_socket(cid);
        note_peer_close(cid);
    }
    frame_ipd();
    refresh_tick_gate();
}

void AtEngine::resolve_connect(std::size_t cid) {
    Connection& c = conn_[cid];
    if (!c.connecting) return;

    switch (c.transport->state()) {
        case TransportState::Resolving:
        case TransportState::Connecting:
            if (std::chrono::steady_clock::now() < c.connect_deadline) {
                return;  // still in flight; keep deferring guest input
            }
            // DEADLINE BLOWN. Without this the answer comes only when the OS
            // abandons the handshake — ~127 s on Linux for a host that
            // silently black-holes SYN — and NXtel's post-`Connect` wait
            // (esp.asm:39-40) has no timeout and runs under `di`, so the guest
            // does not slow down, it FREEZES. Answering ERROR is AT-protocol
            // behaviour and belongs here, not in the transport: the transport
            // has no idea a guest is blocked on a reply.
            log_warn("connection to {}:{} timed out after {} ms — answering ERROR", c.host,
                      c.port, connect_timeout_.count());
            c.connecting = false;
            c.open       = false;
            c.transport->close();  // slot returns to Idle/Closed and is reusable
            queue_error();
            break;
        case TransportState::Connected:
            c.connecting = false;
            c.open       = true;
            log_info("connection to {}:{} opened ({})", c.host, c.port,
                      to_string(c.transport->peer_address()));
            queue_ok();
            break;
        default:
            // Failed, Closed or a transport that never left Idle. `FAIL` is
            // not emitted (see header simplification 4) and no CLOSED either,
            // because no connection was ever established.
            c.connecting = false;
            c.open       = false;
            log_warn("connection to {}:{} failed: {}", c.host, c.port,
                      c.transport->last_error().empty() ? "refused" : c.transport->last_error());
            c.transport->close();
            queue_error();
            break;
    }

    // Replay anything the guest typed while the connect was in flight, in
    // order. The loop re-checks `connecting` because a deferred line may
    // itself be another AT+CIPSTART.
    while (!deferred_.empty() && !conn_[SINGLE_CID].connecting) {
        const std::uint8_t b = deferred_.front();
        deferred_.pop_front();
        feed(b);
    }
}

void AtEngine::flush_outbound(std::size_t cid) {
    Connection& c = conn_[cid];
    if (c.tx.empty() || !c.open) return;

    // Copy to a contiguous staging buffer: the transport takes a pointer and
    // a length, and a deque is not contiguous.
    std::vector<std::uint8_t> staged(c.tx.begin(), c.tx.end());
    const std::size_t sent = c.transport->send(staged.data(), staged.size());
    for (std::size_t i = 0; i < sent; ++i) c.tx.pop_front();

    if (sent != 0) {
        log_debug("flushed {}/{} byte(s) to the peer on cid {}, {} still queued", sent,
                   staged.size(), cid, c.tx.size());
    } else if (!c.tx.empty()) {
        log_trace("peer send buffer full on cid {}, {} byte(s) still queued", cid, c.tx.size());
    }
}

void AtEngine::drain_socket(std::size_t cid) {
    Connection& c = conn_[cid];
    if (!c.open) return;

    std::uint8_t buf[RECV_CHUNK];
    std::size_t  total = 0;
    for (int i = 0; i < RECV_MAX_ITER; ++i) {
        const std::size_t n = c.transport->recv(buf, sizeof(buf));
        if (n == 0) break;
        for (std::size_t j = 0; j < n; ++j) c.rx.push_back(buf[j]);
        total += n;
        if (c.transport->state() != TransportState::Connected) break;
    }
    if (total != 0) {
        log_debug("buffered {} byte(s) from the peer on cid {} ({} awaiting +IPD framing)",
                   total, cid, c.rx.size());
    }
}

void AtEngine::note_peer_close(std::size_t cid) {
    Connection& c = conn_[cid];
    if (!c.open) return;
    if (c.transport->state() == TransportState::Connected) return;

    log_info("connection to {}:{} closed by the peer", c.host, c.port);
    c.open = false;
    c.tx.clear();
    // The CLOSED notification waits until everything already received has been
    // framed and drained — telling the guest the connection is gone while its
    // last bytes are still queued would lose them.
    c.close_pending = true;
}

// ─── Emulated-time half ───────────────────────────────────────────────

bool AtEngine::wire_is_quiet() const {
    return out_.empty() && mode_ == Mode::Command && !conn_[SINGLE_CID].connecting &&
           line_.empty();
}

void AtEngine::frame_ipd() {
    if (!wire_is_quiet()) return;

    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (c.rx.empty()) continue;

        const std::size_t n = std::min(c.rx.size(), MAX_IPD_CHUNK);

        // The chunk is whatever has accumulated, capped at MAX_IPD_CHUNK — and
        // because a chunk is only cut when the guest-bound queue has fully
        // drained, a busy peer produces few large chunks rather than many
        // small ones, which is what keeps nextsync inside its
        // 5-chunks-per-packet budget.
        log_debug("+IPD framing {} byte(s) on cid {}, {} left buffered", n, cid, c.rx.size() - n);
        queue_ipd_header(cid, /*multiplexed=*/false, n);
        for (std::size_t i = 0; i < n; ++i) {
            out_.push_back(c.rx.front());
            c.rx.pop_front();
        }
        refresh_tick_gate();
        return;  // one chunk per quiet moment — the next follows when this drains
    }

    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (!c.close_pending) continue;
        c.close_pending = false;
        queue("\r\nCLOSED\r\n");
        return;
    }
}

void AtEngine::tick(std::uint32_t elapsed_ticks, std::uint32_t ticks_per_byte) {
    if (ticks_per_byte == 0) ticks_per_byte = 1;  // a zero rate would stall the pacer

    // Cut a new +IPD the moment the wire falls quiet, so the pacer never
    // stalls with data buffered behind it.
    if (out_.empty()) frame_ipd();

    if (out_.empty()) {
        // Idle: return WITHOUT banking, because `pace_accum_ += elapsed_ticks`
        // below is the only place ticks are ever added. That is what stops a
        // long quiet period from accumulating credit and then releasing a
        // burst at unbounded speed the instant data appears — precisely the
        // FIFO overrun this pacing exists to prevent.
        //
        // There is deliberately no `pace_accum_ = 0` here. It used to be, and
        // it was unkillable by mutation: `out_` is drained ONLY by the loop
        // below, which zeroes the accumulator itself when it empties the
        // queue, so this branch can never be reached with a non-zero
        // accumulator. If a future change drains `out_` from anywhere else,
        // that invariant breaks and the reset belongs back here.
        refresh_tick_gate();
        return;
    }

    pace_accum_ += elapsed_ticks;
    std::size_t released = 0;
    while (!out_.empty() && pace_accum_ >= ticks_per_byte) {
        pace_accum_ -= ticks_per_byte;
        send_to_guest(out_.front());
        out_.pop_front();
        ++released;
    }

    if (out_.empty()) {
        // THE reset that matters. Without it the sub-byte remainder survives
        // into the next burst, so the first byte of a reply queued directly by
        // a later command (not via poll()) can leave up to a whole byte-time
        // early. Bounded, but it is exactly the pacing slip this whole
        // mechanism exists to prevent.
        pace_accum_ = 0;
        frame_ipd();
    }
    if (released != 0) {
        log_trace("paced {} byte(s) to the guest at {} ticks/byte ({} queued)", released,
                  ticks_per_byte, out_.size());
    }
    refresh_tick_gate();
}

std::size_t AtEngine::payload_outstanding() const {
    if (mode_ != Mode::Payload) return 0;
    return payload_len_ - payload_.size();
}

void AtEngine::refresh_tick_gate() {
    // Raised while there is anything to release OR anything waiting to be
    // framed; lowered otherwise, which is what keeps the per-instruction call
    // site free. The per-slot scan is 5 iterations of a deque-empty test and
    // only ever runs from inside the gated path or from `receive`.
    bool work = !out_.empty();
    for (std::size_t cid = 0; !work && cid < MAX_CONNECTIONS; ++cid) {
        work = !conn_[cid].rx.empty() || conn_[cid].close_pending;
    }
    tick_wanted_ = work;
}

}  // namespace esp
