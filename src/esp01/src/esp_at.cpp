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
    {"AT+CIPSERVER=",  true,  &AtEngine::cmd_cipserver},
    {"AT+CIPMUX=",     true,  &AtEngine::cmd_cipmux},
    {"AT+UART_CUR=",   true,  &AtEngine::cmd_uart},
    {"AT+UART_DEF=",   true,  &AtEngine::cmd_uart},
    {"AT+UART=",       true,  &AtEngine::cmd_uart},
};
const std::size_t AtEngine::kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

AtEngine::AtEngine(EspTransport& transport, EspListener* listener)
    : listener_(listener) {
    // Slot 0 borrows the host's transport — `owned=false`, so clearing the slot
    // can never free an object the host still holds. Slots 1..4 start empty and
    // are filled with OWNED transports by `accept_connections()`, which is what
    // makes them inert until then: every per-slot loop skips a null.
    conn_[SINGLE_CID].transport = TransportPtr(&transport, TransportOwnership{/*owned=*/false});
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
    if (c.protocol == Protocol::Udp) {
        // ONE `AT+CIPSEND` IS ONE DATAGRAM. Queued whole and kept whole: if a
        // second CIPSEND completes while the first is still unsent, they must
        // leave as two datagrams, not as one concatenation the peer would read
        // as a single malformed message.
        c.tx_datagrams.push_back(payload_);
    } else {
        for (std::uint8_t b : payload_) c.tx.push_back(b);
    }
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
        c.multiplexed   = false;
        c.protocol      = Protocol::Tcp;  // power-on default
        c.local_port    = 0;
        c.clear_buffers();
        release_inbound(cid);
    }
    // The server does not survive the reboot either. Real firmware requires
    // `AT+CIPSERVER` to be re-issued after a restart, and design doc §13.4
    // makes it a security property rather than only a fidelity one: a listening
    // port that outlived the module that opened it is exactly how this leaks.
    if (listener_ && listener_->listening()) {
        log_info("AT+RST — closing the server on port {}", listener_->port());
        listener_->close();
    }
    mode_        = Mode::Command;
    payload_len_ = 0;
    payload_.clear();
    deferred_.clear();
    echo_ = false;  // power-on default — see simplification (2) in the header
    // Back to the power-on default, which is the value nextsync depends on
    // never having to ask for.
    cipmux_ = false;

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
    if (!take_quoted(rest, proto)) {
        log_debug("AT+CIPSTART has no quoted protocol — answering ERROR");
        queue_error();
        return;
    }
    Protocol protocol = Protocol::Tcp;
    if (ieq(proto, "TCP")) {
        protocol = Protocol::Tcp;
    } else if (ieq(proto, "UDP")) {
        protocol = Protocol::Udp;
    } else {
        // SSL/"UDPv6"/anything else: still no consumer, still refused.
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

    std::uint32_t port = 0;
    if (!parse_uint(take_field(rest), 65535, port) || port == 0) {
        log_debug("AT+CIPSTART has no usable port — answering ERROR");
        queue_error();
        return;
    }

    // THE TRAILING ARGUMENTS ARE NOT THE SAME ARGUMENTS. For TCP the fourth
    // field is a KEEPALIVE that NXtel really does send
    // (`AT+CIPSTART="TCP","nx.nxtel.org",23280,7200`, esp.asm/NXterm.asm) and
    // that we accept and ignore. For UDP it is `<local port>`, optionally
    // followed by `<mode>` — a different meaning at the same position, which is
    // why this cannot be one shared "trailing number" parse.
    std::uint16_t local_port = 0;
    if (protocol == Protocol::Tcp) {
        if (!rest.empty()) {
            std::uint32_t keepalive = 0;
            if (!parse_uint(take_field(rest), 7200, keepalive) || !rest.empty()) {
                log_debug("AT+CIPSTART trailing arguments unparseable — answering ERROR");
                queue_error();
                return;
            }
        }
    } else if (!rest.empty()) {
        std::uint32_t lport = 0;
        if (!parse_uint(take_field(rest), 65535, lport)) {
            log_debug("AT+CIPSTART=\"UDP\" local port unparseable — answering ERROR");
            queue_error();
            return;
        }
        local_port = static_cast<std::uint16_t>(lport);
        if (!rest.empty()) {
            std::uint32_t mode = 0;
            if (!parse_uint(take_field(rest), 2, mode) || !rest.empty()) {
                log_debug("AT+CIPSTART=\"UDP\" mode unparseable — answering ERROR");
                queue_error();
                return;
            }
            if (mode != 0) {
                // Refused, not ignored — see simplification (4c). Modes 1 and 2
                // re-point the peer at whoever last sent us a datagram, which
                // this transport (a `connect()`ed socket, by design) cannot do
                // and which would need its own security decision about who is
                // allowed to become the peer.
                log_debug("AT+CIPSTART=\"UDP\" mode {} unsupported (peer is fixed) — "
                           "answering ERROR", mode);
                queue_error();
                return;
            }
        }
    }

    c.host       = host;
    c.port       = static_cast<std::uint16_t>(port);
    c.protocol   = protocol;
    c.local_port = local_port;

    if (!c.transport->begin_connect(c.host, c.port, protocol, local_port)) {
        log_warn("{} connection to {}:{} refused before it started",
                  protocol_text(protocol), c.host, c.port);
        queue_error();
        return;
    }
    // The reply is DEFERRED until poll() sees the transport settle. There is
    // no synchronous connect in the transport interface by design, and
    // answering OK before the socket is up would let the guest send into
    // nothing.
    c.connecting       = true;
    c.connect_deadline = std::chrono::steady_clock::now() + connect_timeout_;
    log_debug("AT+CIPSTART accepted: {} {}:{} (local port {}) on cid {} — reply deferred to "
              "poll() (deadline {} ms)",
               protocol_text(protocol), c.host, c.port, local_port, SINGLE_CID,
               connect_timeout_.count());
}

void AtEngine::cmd_cipsend(const std::string& args)   { begin_send(args, "AT+CIPSEND"); }
void AtEngine::cmd_cipsendex(const std::string& args) { begin_send(args, "AT+CIPSENDEX"); }

void AtEngine::begin_send(const std::string& args, const char* name) {
    // THE ARGUMENT LIST DEPENDS ON THE MODE, and it is read from the mode
    // rather than sniffed from the text: `AT+CIPSEND=<length>` under CIPMUX=0,
    // `AT+CIPSEND=<link ID>,<length>` under CIPMUX=1 (ESP-AT AT+CIPSEND).
    // Deciding by "does it contain a comma" would silently accept the wrong
    // form for the mode, and the failure would land as a payload length the
    // guest never meant.
    std::string   rest = args;
    std::size_t   cid  = SINGLE_CID;
    if (cipmux_) {
        std::uint32_t id = 0;
        if (!parse_uint(take_field(rest), MAX_CONNECTIONS - 1, id)) {
            log_debug("{} link id \"{}\" out of range — answering ERROR", name, escape(args));
            queue_error();
            return;
        }
        cid = id;
    }

    if (!conn_[cid].open) {
        log_debug("{} on cid {} which is not open — answering ERROR", name, cid);
        queue_error();
        return;
    }

    std::uint32_t len = 0;
    if (!parse_uint(rest, MAX_SEND_LEN, len) || len == 0) {
        log_debug("{} length \"{}\" out of range — answering ERROR", name, escape(args));
        queue_error();
        return;
    }

    payload_cid_ = cid;
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
    log_info("{} connection to {}:{} closed by the guest (AT+CIPCLOSE)",
             protocol_text(c.protocol), c.host, c.port);
    c.transport->close();
    c.open          = false;
    c.connecting    = false;
    c.close_pending = false;
    c.clear_buffers();
    // A connection genuinely closed, so CLOSED is honest here — and NXtel's
    // 5-byte `OSED\r` window is what it looks for.
    queue("\r\nCLOSED\r\n\r\nOK\r\n");
}

void AtEngine::cmd_cipmux(const std::string& args) {
    if (args != "0" && args != "1") {
        log_debug("AT+CIPMUX={} is not a mode (0 or 1) — answering ERROR", escape(args));
        queue_error();
        return;
    }
    const bool want = (args == "1");

    // A NO-OP IS ALWAYS ACCEPTED, and that is what keeps `AT+CIPMUX=0` — the
    // line NXtel sends at init and the one v1.0 always answered OK — behaving
    // exactly as it always did (simplification 8c). Only a real CHANGE is
    // subject to the guards below.
    if (want == cipmux_) {
        queue_ok();
        return;
    }

    // Real firmware: "This mode can only be changed after all connections are
    // disconnected" (ESP-AT AT+CIPMUX). The hazard is concrete rather than
    // bureaucratic — the peer of a live connection was promised one `+IPD`
    // framing and has no way to renegotiate, and nextsync's reader would not
    // even fail cleanly on the other one (see `queue_ipd_header`).
    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        if (!conn_[cid].open && !conn_[cid].connecting) continue;
        log_debug("AT+CIPMUX={} while cid {} is live — answering ERROR", args, cid);
        queue_error();
        return;
    }
    // Real firmware, same command: to go back to single-connection mode "you
    // should delete the server first". A server is a promise of `<id>,CONNECT`
    // and `+IPD,<id>,…` to whoever connects next, so leaving it up under
    // CIPMUX=0 would mean framing the next client cannot have asked for.
    if (!want && listener_ && listener_->listening()) {
        log_debug("AT+CIPMUX=0 while the server is listening on port {} — answering ERROR",
                  listener_->port());
        queue_error();
        return;
    }

    cipmux_ = want;
    log_debug("AT+CIPMUX={} accepted — {} connection mode", args,
              cipmux_ ? "multiple" : "single");
    queue_ok();
}

void AtEngine::cmd_cipserver(const std::string& args) {
    // `<mode>` 0 = delete, 1 = create (ESP-AT AT+CIPSERVER). Only those two
    // exist, and the second takes a port.
    std::string   rest = args;
    std::uint32_t mode = 0;
    if (!parse_uint(take_field(rest), 1, mode)) {
        log_debug("AT+CIPSERVER mode \"{}\" is not 0 or 1 — answering ERROR", escape(args));
        queue_error();
        return;
    }

    if (mode == 0) {
        if (!rest.empty()) {
            // ESP-AT's `<close_all>` argument. Refused rather than ignored, for
            // the `AT+CIPMUX=1` reason: accepting it would promise a choice
            // about existing connections that this engine does not make.
            log_debug("AT+CIPSERVER=0 takes no further argument — answering ERROR");
            queue_error();
            return;
        }
        if (!listener_ || !listener_->listening()) {
            // ERROR, where real firmware says OK — simplification (8b). The one
            // evidenced sender of this line turns the server off at init and
            // has always received `ERROR` here (v1.0 had no such command at
            // all), so this is the spelling that changes nothing for it.
            log_debug("AT+CIPSERVER=0 with no server running — answering ERROR");
            queue_error();
            return;
        }
        const std::uint16_t was = listener_->port();
        listener_->close();
        // Established connections are deliberately left alone: the guest asked
        // to stop ACCEPTING, and dropping a live debug session because its
        // listener was retired would be a surprise nothing asked for.
        log_info("AT+CIPSERVER=0 — stopped listening on port {}", was);
        queue_ok();
        return;
    }

    // "A TCP/SSL server can only be created when multiple connections are
    // activated (AT+CIPMUX=1)" — ESP-AT, AT+CIPSERVER. Checked before the port
    // so that a guest which forgot the prerequisite is not also told its port
    // is wrong.
    if (!cipmux_) {
        log_debug("AT+CIPSERVER=1 needs AT+CIPMUX=1 first — answering ERROR");
        queue_error();
        return;
    }

    std::uint32_t port = 0;
    if (!parse_uint(take_field(rest), 65535, port) || port == 0 || !rest.empty()) {
        // Port 0 is refused although the platform layer accepts it: it means
        // "let the OS choose", and a guest that named no port has no way to be
        // told which one it got.
        log_debug("AT+CIPSERVER=1 has no usable port — answering ERROR");
        queue_error();
        return;
    }

    if (listener_ && listener_->listening()) {
        log_debug("AT+CIPSERVER=1 while already listening on port {} — answering ERROR",
                  listener_->port());
        queue_error();
        return;
    }
    if (!listener_) {
        // No server capability was built. Indistinguishable from a failed bind
        // on purpose — see the constructor's note.
        log_warn("AT+CIPSERVER=1,{} but this ESP has no listener — answering ERROR", port);
        queue_error();
        return;
    }

    if (!listener_->open(static_cast<std::uint16_t>(port))) {
        // NO FALL BACK OF ANY KIND, which is the whole of design doc §13.4's
        // consequence: not to another port, and above all not to a wider bind
        // address. The listener has already logged what went wrong.
        log_warn("AT+CIPSERVER=1,{} failed to bind ({}) — answering ERROR", port,
                 listener_->last_error());
        queue_error();
        return;
    }
    log_info("AT+CIPSERVER=1,{} — listening", listener_->port());
    queue_ok();
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
    // Splitting this loop in two changes NOTHING for v1.0: only `SINGLE_CID`
    // is ever given a transport, so "poll every slot, then service every slot"
    // and the old "poll and service each slot in turn" execute the identical
    // sequence of calls. When CIPMUX (issue #154) populates more slots they
    // will differ in interleaving only — every slot is still polled before it
    // is serviced, which is the ordering that matters.
    advance_transports();
    service_transports();
}

void AtEngine::advance_transports() {
    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (!c.transport) continue;  // a slot no connection has been put in
        // Nothing here reads or writes engine state, which is what lets a
        // threaded host run it outside its own lock — see the header.
        c.transport->poll();
    }
    // The listener belongs on this side of the split for the same reason and by
    // the same rule: its `poll()` is the accept SYSCALL and touches no engine
    // state, while `accept()` — which puts a connection in a slot — is engine
    // work and runs below, under the lock.
    if (listener_) listener_->poll();
}

void AtEngine::service_transports() {
    // FIRST, so a connection accepted this pass is drained and framed in this
    // pass too. It also fixes the order of the two things the guest hears about
    // a new peer: `<id>,CONNECT` is queued here, which makes the wire non-quiet,
    // so the peer's first `+IPD` can only be framed after it.
    accept_connections();

    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];
        if (!c.transport) continue;
        resolve_connect(cid);
        flush_outbound(cid);
        drain_socket(cid);
        note_peer_close(cid);
    }
    frame_ipd();
    refresh_tick_gate();
}

void AtEngine::accept_connections() {
    if (!listener_) return;

    while (std::unique_ptr<EspTransport> adopted = listener_->accept()) {
        std::size_t cid = MAX_CONNECTIONS;
        for (std::size_t i = FIRST_INBOUND_CID; i < MAX_CONNECTIONS; ++i) {
            if (!conn_[i].transport) {
                cid = i;
                break;
            }
        }
        if (cid == MAX_CONNECTIONS) {
            // Every inbound slot is taken. Closing at once is the honest
            // answer: the peer learns immediately that it has nowhere to go,
            // which is better than a connection that is open on its side and
            // invisible on ours. Real firmware refuses past its own ceiling
            // too; ours is one lower because slot 0 is reserved
            // (simplification 8a).
            log_warn("inbound connection dropped — all {} inbound slots are in use",
                     MAX_CONNECTIONS - FIRST_INBOUND_CID);
            adopted->close();
            continue;
        }

        Connection& c = conn_[cid];
        c.transport   = TransportPtr(adopted.release(), TransportOwnership{/*owned=*/true});
        c.open        = true;
        c.connecting  = false;
        c.close_pending = false;
        c.protocol    = Protocol::Tcp;  // no listener speaks anything else
        c.local_port  = 0;
        c.clear_buffers();
        // An inbound connection exists only because the guest asked for a
        // server, and a server needs CIPMUX=1, so this is always true here. It
        // is read rather than assumed because it is the value that decides the
        // wire format for the life of the connection.
        c.multiplexed = cipmux_;
        c.host        = to_string(c.transport->peer_address());
        c.port        = 0;  // the peer's ephemeral port is of no use to the guest

        log_info("inbound connection from {} accepted as cid {}", c.host, cid);
        // `[<conn_id>,]CONNECT` — "A network connection of which ID is
        // <conn_id> is established" (ESP-AT, AT Messages table). The bracketed
        // id is present exactly when the session is multiplexed, which an
        // inbound connection always is.
        queue("\r\n" + std::to_string(cid) + ",CONNECT\r\n");
    }
}

void AtEngine::release_inbound(std::size_t cid) {
    if (cid < FIRST_INBOUND_CID) return;  // slot 0's transport is borrowed
    // Frees the accepted transport (and with it the socket) via the deleter,
    // and returns the slot to the pool: a null `transport` is exactly what
    // `accept_connections` looks for.
    conn_[cid].transport.reset();
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
            // The framing this connection will use for the rest of its life,
            // fixed here because this is the moment the peer exists. An
            // outbound connection made by a CIPMUX=0 session therefore keeps
            // the unmultiplexed `+IPD` whatever happens later — which is the
            // property nextsync's reader depends on.
            c.multiplexed = cipmux_;
            log_info("{} connection to {}:{} opened ({})", protocol_text(c.protocol), c.host,
                      c.port, to_string(c.transport->peer_address()));
            if (c.protocol == Protocol::Udp) {
                // Real firmware answers `CONNECT` then `OK` for BOTH protocols.
                // TCP's bare `OK` is pre-existing v1.0 behaviour that this
                // change deliberately does not touch — see simplification (4b)
                // for why, and for what would have to be gathered to change it.
                // For UDP it is required: newt's `net_connect_udp` reads ONE
                // line and returns false unless it starts with `CONNECT`.
                //
                // NO LEADING CRLF BEFORE `CONNECT`, unlike every other reply
                // here, and the exception is the firmware's rather than ours.
                // `CONNECT` is a STATUS line printed before the result code,
                // not a result code; the CRLF that renders as the blank line
                // between them belongs to the `\r\nOK\r\n` that follows. That
                // is what makes the familiar transcript
                //     AT+CIPSTART=…
                //     CONNECT
                //
                //     OK
                // come out with the blank line AFTER `CONNECT` and not before.
                // Getting this wrong is not cosmetic: newt reads exactly ONE
                // line and compares it, so a leading CRLF hands it an empty
                // line, `net_connect_udp` returns false and the tool gives up
                // without sending anything. Found by running the real dot
                // command against a real NTP server, not by reading the code.
                queue("CONNECT\r\n\r\nOK\r\n");
            } else {
                queue_ok();
            }
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
    if (!c.open) return;

    if (c.protocol == Protocol::Udp) {
        // ALL-OR-NOTHING, one datagram per send, oldest first. A short return
        // cannot happen on a datagram socket (`EspTransport::send`), so the
        // only two outcomes are "gone" and "the buffer was full, try next
        // poll" — and re-offering a remainder would put a truncated second
        // datagram on the wire, which is why there is no partial-pop branch
        // here at all.
        while (!c.tx_datagrams.empty()) {
            const std::vector<std::uint8_t>& dg = c.tx_datagrams.front();
            const std::size_t sent = c.transport->send(dg.data(), dg.size());
            if (sent == 0) {
                log_trace("peer send buffer full on cid {}, {} datagram(s) still queued", cid,
                          c.tx_datagrams.size());
                return;
            }
            log_debug("sent a {}-byte datagram to the peer on cid {}, {} still queued",
                      dg.size(), cid, c.tx_datagrams.size() - 1);
            c.tx_datagrams.pop_front();
        }
        return;
    }

    if (c.tx.empty()) return;

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

    if (c.protocol == Protocol::Udp) {
        // One `recv` is at most one datagram, and the buffer must be able to
        // hold the whole of it — see MAX_DATAGRAM. A datagram is stored as a
        // unit so that `frame_ipd` can emit exactly one `+IPD` for it.
        std::uint8_t buf[MAX_DATAGRAM];
        std::size_t  count = 0;
        for (int i = 0; i < RECV_MAX_ITER; ++i) {
            const std::size_t n = c.transport->recv(buf, sizeof(buf));
            if (n == 0) break;
            c.rx_datagrams.emplace_back(buf, buf + n);
            ++count;
            if (c.transport->state() != TransportState::Connected) break;
        }
        if (count != 0) {
            log_debug("buffered {} datagram(s) from the peer on cid {} ({} awaiting +IPD "
                      "framing)", count, cid, c.rx_datagrams.size());
        }
        return;
    }

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

    // UDP HAS NO ORDERLY CLOSE, so this path is reached for a datagram
    // connection only on a genuine socket ERROR — most often an ICMP port
    // unreachable, which Linux reports as ECONNREFUSED on the next call
    // against a `connect()`ed UDP socket. That really is "the peer is not
    // there", so it is reported exactly as a TCP failure is rather than being
    // swallowed: a guest waiting for an answer that can never come is worse
    // off than one told the connection is gone.
    log_info("{} connection to {}:{} closed by the peer", protocol_text(c.protocol), c.host,
             c.port);
    c.open = false;
    c.tx.clear();
    c.tx_datagrams.clear();
    // The CLOSED notification waits until everything already received has been
    // framed and drained — telling the guest the connection is gone while its
    // last bytes are still queued would lose them.
    c.close_pending = true;
}

// ─── Emulated-time half ───────────────────────────────────────────────

bool AtEngine::command_line_in_flight() const {
    if (line_.empty()) return false;
    // Every entry in `kCommands` begins "AT" (`AT`, `ATE0`, `AT+…`), so the
    // partial line is a command in the making only while it is still consistent
    // with that. A single 'A' counts: the guest may be one byte into `AT`.
    static const char kStart[] = "AT";
    for (std::size_t i = 0; i < 2 && i < line_.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(line_[i])) != kStart[i]) return false;
    }
    return true;
}

bool AtEngine::wire_is_quiet() const {
    return out_.empty() && mode_ == Mode::Command && !conn_[SINGLE_CID].connecting &&
           !command_line_in_flight();
}

void AtEngine::frame_ipd() {
    if (!wire_is_quiet()) return;

    for (std::size_t cid = 0; cid < MAX_CONNECTIONS; ++cid) {
        Connection& c = conn_[cid];

        if (!c.rx_datagrams.empty()) {
            // ONE DATAGRAM, ONE `+IPD`. No chunking and no coalescing: a
            // datagram's length IS its framing, so splitting one across two
            // `+IPD`s or merging two into one would hand the guest a message
            // boundary that never existed on the wire. The datagram cannot
            // exceed MAX_IPD_CHUNK because that is the size of the buffer it
            // was read into.
            std::vector<std::uint8_t> dg = std::move(c.rx_datagrams.front());
            c.rx_datagrams.pop_front();
            log_debug("+IPD framing a {}-byte datagram on cid {}, {} datagram(s) left "
                      "buffered", dg.size(), cid, c.rx_datagrams.size());
            queue_ipd_header(cid, c.multiplexed, dg.size());
            for (std::uint8_t b : dg) out_.push_back(b);
            refresh_tick_gate();
            return;
        }

        if (c.rx.empty()) continue;

        const std::size_t n = std::min(c.rx.size(), MAX_IPD_CHUNK);

        // The chunk is whatever has accumulated, capped at MAX_IPD_CHUNK — and
        // because a chunk is only cut when the guest-bound queue has fully
        // drained, a busy peer produces few large chunks rather than many
        // small ones, which is what keeps nextsync inside its
        // 5-chunks-per-packet budget.
        log_debug("+IPD framing {} byte(s) on cid {}, {} left buffered", n, cid, c.rx.size() - n);
        queue_ipd_header(cid, c.multiplexed, n);
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
        // `[<conn_id>,]CLOSED` — "A network connection of which ID is
        // <conn_id> ends" (ESP-AT, AT Messages table). The unprefixed form is
        // what NXtel's 5-byte `OSED\r` window looks for, so a CIPMUX=0 session
        // keeps seeing exactly the bytes v1.0 emitted.
        if (c.multiplexed)
            queue("\r\n" + std::to_string(cid) + ",CLOSED\r\n");
        else
            queue("\r\nCLOSED\r\n");
        // Told, therefore finished with: the slot goes back to the pool only
        // now, so a peer that reconnects immediately cannot be given the id of
        // a connection the guest has not yet heard about the end of.
        release_inbound(cid);
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

bool AtEngine::server_listening() const {
    return listener_ != nullptr && listener_->listening();
}

std::uint16_t AtEngine::server_port() const {
    return listener_ != nullptr ? listener_->port() : 0;
}

std::size_t AtEngine::inbound_connections() const {
    std::size_t n = 0;
    for (std::size_t cid = FIRST_INBOUND_CID; cid < MAX_CONNECTIONS; ++cid) {
        if (conn_[cid].open) ++n;
    }
    return n;
}

std::size_t AtEngine::pending_from_peer() const {
    const Connection& c = conn_[SINGLE_CID];
    std::size_t       n = c.rx.size();
    for (const std::vector<std::uint8_t>& dg : c.rx_datagrams) n += dg.size();
    return n;
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
        work = conn_[cid].has_peer_data() || conn_[cid].close_pending;
    }
    tick_wanted_ = work;
}

}  // namespace esp
