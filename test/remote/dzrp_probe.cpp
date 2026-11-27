#include "dzrp_probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "remote/tcp_link.h"

namespace jnext {
namespace remote {
namespace dzrp {

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string uri;
    int  timeout_ms           = 2000;
    int  connect_timeout_ms   = 5000;
    int  reconnect_delay_ms   = 1000;
    bool hex                  = false;
    bool send_unsupported     = false;
    int  unsupported_id       = 0;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> reads;
    std::vector<std::uint8_t> nextregs;
};

// ── Strict number parsers (a typo is a usage error, never a silent 0) ───────

bool parse_number(const std::string& s, int base, unsigned long max, unsigned long& out) {
    if (s.empty() || s.size() > 12) return false;
    unsigned long v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base) return false;
        v = v * static_cast<unsigned long>(base) + static_cast<unsigned long>(d);
        if (v > max) return false;
    }
    out = v;
    return true;
}

std::string strip_0x(const std::string& s) {
    return s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? s.substr(2) : s;
}

void usage(std::FILE* out) {
    std::fputs(
        "usage: dzrp_client_probe [OPTIONS] tcp:HOST:PORT\n"
        "  The R0.5 checkpoint. On a real Next running dezogif_ng: press the NMI button\n"
        "  FIRST, then run this (a TCP connect while the program runs makes the ESP-01 print\n"
        "  CONNECT, which breaks it).\n"
        "  Sequence: connect; CMD_INIT; (jnext / dezogif 2.2 profiles)\n"
        "  CMD_GET_SUPPORTED_COMMANDS; CMD_GET_REGISTERS; CMD_READ_MEM 16 bytes at PC, at SP,\n"
        "  and at 0x0000, 0x2000 .. 0xE000 (one per slot); CMD_GET_TBBLUE_REG 0x00, 0x01,\n"
        "  0x50..0x57; CMD_CLOSE.\n"
        "  --timeout-ms N          reply timeout (default 2000)\n"
        "  --connect-timeout-ms N  (default 5000)\n"
        "  --read ADDR:LEN         an extra CMD_READ_MEM, hex address, LEN 1..65535 (repeatable)\n"
        "  --nextreg REG           an extra CMD_GET_TBBLUE_REG, hex (repeatable)\n"
        "  --hex                   dump every reply's raw payload bytes\n"
        "  --send-unsupported ID   LAST, ONCE: send command ID, which must be OUTSIDE the\n"
        "                          profile's set (else exit 2), bypassing the client's gate;\n"
        "                          report a reply or \"no reply within N ms\"; close; wait\n"
        "                          --reconnect-delay-ms; reconnect and send CMD_INIT to show\n"
        "                          whether the remote serves a fresh session; CMD_CLOSE\n"
        "  --reconnect-delay-ms N  (default 1000)\n"
        "  --help\n"
        "Exit: 0 every step ran (the outcome of --send-unsupported is reported, never judged);\n"
        "      1 connect / CMD_INIT / a command failed (refused, timeout, link lost, protocol\n"
        "        error), or the reconnect failed; 2 usage error.\n",
        out);
}

// 0 = parsed, 1 = --help, 2 = usage error
int parse_args(int argc, char** argv, Options& o, std::FILE* out) {
    auto bad = [&](const std::string& why) {
        std::fprintf(out, "dzrp_client_probe: %s\n", why.c_str());
        usage(out);
        return 2;
    };
    bool have_uri = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](std::string& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::string v;
        unsigned long n = 0;
        if (a == "--help") {
            usage(out);
            return 1;
        } else if (a == "--hex") {
            o.hex = true;
        } else if (a == "--timeout-ms" || a == "--connect-timeout-ms" ||
                   a == "--reconnect-delay-ms") {
            if (!need(v)) return bad(a + " needs a value");
            const bool zero_ok = a == "--reconnect-delay-ms";
            if (!parse_number(v, 10, 3600000, n) || (n == 0 && !zero_ok))
                return bad("bad number for " + a + ": " + v);
            (a == "--timeout-ms" ? o.timeout_ms
             : a == "--connect-timeout-ms" ? o.connect_timeout_ms : o.reconnect_delay_ms) =
                static_cast<int>(n);
        } else if (a == "--send-unsupported") {
            if (!need(v)) return bad(a + " needs a value");
            if (o.send_unsupported) return bad("--send-unsupported is allowed once");
            if (!parse_number(v, 10, 255, n)) return bad("bad command id: " + v);
            o.send_unsupported = true;
            o.unsupported_id   = static_cast<int>(n);
        } else if (a == "--read") {
            if (!need(v)) return bad(a + " needs a value");
            const std::size_t colon = v.find(':');
            unsigned long addr = 0, len = 0;
            if (colon == std::string::npos ||
                !parse_number(strip_0x(v.substr(0, colon)), 16, 0xFFFF, addr) ||
                !parse_number(v.substr(colon + 1), 10, 65535, len) || len == 0)
                return bad("bad --read, expected HEXADDR:LEN: " + v);
            o.reads.push_back({static_cast<std::uint16_t>(addr), static_cast<std::uint16_t>(len)});
        } else if (a == "--nextreg") {
            if (!need(v)) return bad(a + " needs a value");
            if (!parse_number(strip_0x(v), 16, 0xFF, n)) return bad("bad register: " + v);
            o.nextregs.push_back(static_cast<std::uint8_t>(n));
        } else if (a.size() > 1 && a[0] == '-') {
            return bad("unknown option " + a);
        } else {
            if (have_uri) return bad("more than one address");
            o.uri    = a;
            have_uri = true;
        }
    }
    if (!have_uri) return bad("no address given");
    return 0;
}

// ── Output helpers ──────────────────────────────────────────────────────────

std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t max = static_cast<std::size_t>(-1)) {
    std::string s;
    char b[4];
    for (std::size_t i = 0; i < n && i < max; ++i) {
        std::snprintf(b, sizeof(b), "%02X ", p[i]);
        s += b;
    }
    if (!s.empty()) s.pop_back();
    if (n > max) s += " ...";
    return s;
}

double ms(std::chrono::microseconds us) { return static_cast<double>(us.count()) / 1000.0; }

std::uint16_t w16(const std::vector<std::uint8_t>& p, std::size_t at) {
    return static_cast<std::uint16_t>(p[at] | (p[at + 1] << 8));
}

// ── Reply decoders (each checks the size before every index) ────────────────

constexpr std::size_t REG_FIXED_BYTES = 29;   // 12 words, R, I, IM, reserved, slot count

bool decode_registers(const std::vector<std::uint8_t>& p, std::string& text) {
    if (p.size() < REG_FIXED_BYTES) return false;
    const std::size_t slots = p[REG_FIXED_BYTES - 1];
    if (p.size() < REG_FIXED_BYTES + slots) return false;
    char b[256];
    std::snprintf(b, sizeof(b),
                  "  PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n"
                  "  AF'=%04X BC'=%04X DE'=%04X HL'=%04X R=%02X I=%02X IM=%u\n  slots",
                  w16(p, 0), w16(p, 2), w16(p, 4), w16(p, 6), w16(p, 8), w16(p, 10), w16(p, 12),
                  w16(p, 14), w16(p, 16), w16(p, 18), w16(p, 20), w16(p, 22), p[24], p[25],
                  static_cast<unsigned>(p[26]));
    text = b;
    for (std::size_t i = 0; i < slots; ++i) {
        std::snprintf(b, sizeof(b), " %zu:%02X", i, p[REG_FIXED_BYTES + i]);
        text += b;
    }
    return true;
}

struct Probe {
    const Options&   o;
    const ProbeEnv&  env;
    std::FILE*       out;
    int              commands = 0;
    int              failed   = 0;
    std::vector<double> rtts;

    Probe(const Options& opts, const ProbeEnv& e, std::FILE* f) : o(opts), env(e), out(f) {}

    Clock::time_point now() const { return env.base.clock ? env.base.clock() : Clock::now(); }
    bool real_clock() const { return !env.base.clock; }

    ClientConfig config() const {
        ClientConfig c = env.base;
        c.response_timeout = std::chrono::milliseconds(o.timeout_ms);
        return c;
    }

    std::unique_ptr<ClientLink> make_link(const LinkSpec& spec) const {
        if (env.make_link) return env.make_link(spec);
        TcpLinkOptions to;
        to.connect_timeout = std::chrono::milliseconds(o.connect_timeout_ms);
        return std::unique_ptr<ClientLink>(new TcpLink(spec.host, spec.port, to));
    }

    void turn(DzrpClient& c) {
        if (c.state() == SessionState::Connecting) {
            c.step();
            if (real_clock()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            c.poll(1);
        }
        if (env.wait_hook) env.wait_hook();
    }

    // Connect and CMD_INIT (and CMD_GET_SUPPORTED_COMMANDS): returns true when Ready.
    bool open_session(DzrpClient& c, const char* what, double& connect_ms) {
        const Clock::time_point t0 = now();
        if (!c.start()) {
            std::fprintf(out, "%-28s FAILED  %s\n", what, c.error().c_str());
            return false;
        }
        const Clock::time_point limit = t0 + std::chrono::milliseconds(o.connect_timeout_ms) +
                                        3 * std::chrono::milliseconds(o.timeout_ms);
        Clock::time_point connected = t0;
        bool got_connected = false;
        for (long i = 0; i < 20000000; ++i) {
            turn(c);
            if (!got_connected && c.state() != SessionState::Connecting) {
                got_connected = true;
                connected     = now();
            }
            if (c.state() == SessionState::Ready || c.state() == SessionState::Failed ||
                c.state() == SessionState::Closed)
                break;
            if (now() > limit) break;
        }
        connect_ms = ms(std::chrono::duration_cast<std::chrono::microseconds>(connected - t0));
        return c.state() == SessionState::Ready;
    }

    void note(double rtt_ms) { rtts.push_back(rtt_ms); }

    void fail_line(const char* what, const std::string& why) {
        ++failed;
        std::fprintf(out, "%-28s FAILED  %s\n", what, why.c_str());
    }

    // One command through the client; prints its line and detail.
    bool command(DzrpClient& c, std::uint8_t id, std::vector<std::uint8_t> payload,
                 const std::string& tag, const std::function<bool(const Reply&, std::string&)>& decode) {
        ++commands;
        const std::string nm = command_name(id);
        const Reply r = c.request(id, std::move(payload));
        if (r.status != Status::Ok) {
            std::fprintf(out, "%-19s seq %2u   %s%s%s\n", nm.c_str(), static_cast<unsigned>(r.seq),
                         status_name(r.status), c.error().empty() ? "" : ": ", c.error().c_str());
            ++failed;
            return false;
        }
        note(ms(r.rtt));
        std::string detail;
        const bool ok = decode(r, detail);
        std::fprintf(out, "%-19s seq %2u   %s  %8.1f ms  %s\n", nm.c_str(),
                     static_cast<unsigned>(r.seq), ok ? "ok" : "MALFORMED", ms(r.rtt), tag.c_str());
        if (!ok) {
            ++failed;
            std::fprintf(out, "  malformed (%zu bytes): %s\n", r.payload.size(),
                         hex_bytes(r.payload.data(), r.payload.size(), 64).c_str());
        } else if (!detail.empty()) {
            std::fprintf(out, "%s\n", detail.c_str());
        }
        if (o.hex)
            std::fprintf(out, "  raw: %s\n", hex_bytes(r.payload.data(), r.payload.size()).c_str());
        return ok;
    }

    bool read_mem(DzrpClient& c, std::uint16_t addr, std::uint16_t len, const char* label) {
        std::vector<std::uint8_t> pl = {0, static_cast<std::uint8_t>(addr),
                                        static_cast<std::uint8_t>(addr >> 8),
                                        static_cast<std::uint8_t>(len),
                                        static_cast<std::uint8_t>(len >> 8)};
        char tag[64];
        std::snprintf(tag, sizeof(tag), "%s %04X+%u", label, addr, static_cast<unsigned>(len));
        return command(c, CMD_READ_MEM, std::move(pl), tag,
                       [&](const Reply& r, std::string& d) {
                           if (r.payload.size() != len) return false;
                           d = "  " + hex_bytes(r.payload.data(), r.payload.size(), 32);
                           return true;
                       });
    }

    bool nextreg(DzrpClient& c, std::uint8_t reg) {
        char tag[32];
        std::snprintf(tag, sizeof(tag), "NR 0x%02X", reg);
        return command(c, CMD_GET_TBBLUE_REG, {reg}, tag, [&](const Reply& r, std::string& d) {
            if (r.payload.size() != 1) return false;
            char b[48];
            std::snprintf(b, sizeof(b), "  NR 0x%02X = 0x%02X", reg, r.payload[0]);
            d = b;
            return true;
        });
    }

    // The normal sequence. Returns false at the first failed command (the session is then dead).
    bool sequence(DzrpClient& c) {
        std::vector<std::uint8_t> regs_payload;
        std::uint16_t pc = 0, sp = 0;
        bool have_regs = false;
        if (!command(c, CMD_GET_REGISTERS, {}, "", [&](const Reply& r, std::string& d) {
                if (!decode_registers(r.payload, d)) return false;
                pc = w16(r.payload, 0);
                sp = w16(r.payload, 2);
                have_regs = true;
                return true;
            }))
            return false;
        if (!have_regs) return false;
        if (!read_mem(c, pc, 16, "PC")) return false;
        if (!read_mem(c, sp, 16, "SP")) return false;
        for (unsigned slot = 0; slot < 8; ++slot)
            if (!read_mem(c, static_cast<std::uint16_t>(slot * 0x2000), 16, "slot")) return false;
        for (std::uint8_t r : {std::uint8_t{0x00}, std::uint8_t{0x01}})
            if (!nextreg(c, r)) return false;
        for (unsigned r = 0x50; r <= 0x57; ++r)
            if (!nextreg(c, static_cast<std::uint8_t>(r))) return false;
        for (const auto& rd : o.reads)
            if (!read_mem(c, rd.first, rd.second, "read")) return false;
        for (std::uint8_t r : o.nextregs)
            if (!nextreg(c, r)) return false;
        return true;
    }

    void print_session(DzrpClient& c, double connect_ms, bool with_probe_line) {
        std::fprintf(out, "%-28s ok  %18.1f ms\n", "connect", connect_ms);
        note(ms(c.init_rtt()));
        ++commands;
        std::fprintf(out, "%-19s seq %2u   ok  %8.1f ms  reply %s\n", "CMD_INIT", 1u,
                     ms(c.init_rtt()), hex_bytes(c.remote().raw.data(), c.remote().raw.size(), 12).c_str());
        std::fprintf(out, "  remote \"%s\"  DZRP %u.%u.%u  machine %u (%s)  profile %s  window %d\n",
                     c.remote().name.c_str(), static_cast<unsigned>(c.remote().version[0]),
                     static_cast<unsigned>(c.remote().version[1]),
                     static_cast<unsigned>(c.remote().version[2]),
                     static_cast<unsigned>(c.remote().machine),
                     c.remote().machine == DZRP_MACHINE_ZXNEXT ? "ZXNEXT" : "?",
                     profile_name(c.profile()), c.window());
        if (with_probe_line) {
            note(ms(c.probe_rtt()));
            ++commands;
            std::fprintf(out, "%-19s seq %2u   ok  %8.1f ms  %zu commands in the set\n",
                         "CMD_GET_SUPPORTED_COMMANDS", 2u, ms(c.probe_rtt()), c.command_set().count());
        }
    }

    // The unsupported command, on the detached link, past the gate.
    void unsupported(std::unique_ptr<ClientLink> link) {
        const std::uint8_t id = static_cast<std::uint8_t>(o.unsupported_id);
        const std::vector<std::uint8_t> frame = encode_command(1, id, nullptr, 0);
        const std::string what = "unsupported " + std::to_string(o.unsupported_id);
        if (!link->write(frame.data(), frame.size())) {
            std::fprintf(out, "%-28s write failed\n", what.c_str());
            return;
        }
        ResponseParser parser;
        const Clock::time_point t0    = now();
        const Clock::time_point limit = t0 + std::chrono::milliseconds(o.timeout_ms);
        bool replied = false;
        std::uint8_t buf[256];
        while (!replied && now() < limit) {
            link->poll(1);
            if (env.wait_hook) env.wait_hook();
            for (;;) {
                const std::size_t n = link->read(buf, sizeof(buf));
                if (n == 0) break;
                std::size_t off = 0;
                while (off < n && !replied) {
                    const std::size_t k = parser.feed(buf + off, n - off, now());
                    off += k;
                    if (parser.ready() || parser.failed()) replied = true;
                    else if (k == 0) break;
                }
                if (replied) break;
            }
            if (link->state() == LinkState::Failed || link->state() == LinkState::Closed) break;
        }
        if (replied && parser.ready()) {
            const Response r = parser.take();
            std::fprintf(out, "%-19s seq %2u   reply of %zu payload bytes%s\n", what.c_str(),
                         static_cast<unsigned>(r.seq), r.payload.size(),
                         r.payload.empty() ? " (seq only)" : "");
        } else if (replied) {
            std::fprintf(out, "%-19s seq  1   malformed reply: %s\n", what.c_str(),
                         parser.error().c_str());
        } else if (link->state() == LinkState::Failed || link->state() == LinkState::Closed) {
            std::fprintf(out, "%-19s seq  1   the remote closed the link without a reply\n",
                         what.c_str());
        } else {
            std::fprintf(out, "%-19s seq  1   no reply within %d ms\n", what.c_str(), o.timeout_ms);
        }
        link->close();
    }

    int summary(int rc) {
        double mn = 0, med = 0, mx = 0;
        if (!rtts.empty()) {
            std::vector<double> s = rtts;
            std::sort(s.begin(), s.end());
            mn = s.front();
            mx = s.back();
            med = s[s.size() / 2];
        }
        std::fprintf(out, "summary: %d commands, %d failed; round trip min %.1f / median %.1f / "
                          "max %.1f ms\n", commands, failed, mn, med, mx);
        return rc;
    }
};

}  // namespace

int run_probe(int argc, char** argv, std::FILE* out, const ProbeEnv& env) {
    Options o;
    const int pr = parse_args(argc, argv, o, out);
    if (pr == 1) return 0;
    if (pr != 0) return 2;
    LinkSpec spec;
    std::string err;
    if (!parse_link_uri(o.uri, spec, err)) {
        std::fprintf(out, "dzrp_client_probe: %s: %s\n", o.uri.c_str(), err.c_str());
        usage(out);
        return 2;
    }

    Probe p(o, env, out);
    std::fprintf(out, "dzrp_client_probe: %s\n", o.uri.c_str());
    DzrpClient client(p.make_link(spec), p.config());
    client.set_wait_hook(env.wait_hook);
    double connect_ms = 0;
    if (!p.open_session(client, "connect", connect_ms)) {
        std::fprintf(out, "FAILED: %s\n", client.error().empty() ? "no session" : client.error().c_str());
        if (client.failure() == Status::Timeout || client.state() == SessionState::Initialising)
            std::fputs("  (on a dezogif_ng Next: press the NMI button before connecting)\n", out);
        ++p.commands;   // the failed connect / CMD_INIT counts as a failed command
        ++p.failed;
        return p.summary(1);
    }
    const bool probed = client.profile() == Profile::Jnext || client.profile() == Profile::Dezogif22;
    p.print_session(client, connect_ms, probed);

    if (o.send_unsupported && client.in_set(static_cast<std::uint8_t>(o.unsupported_id))) {
        std::fprintf(out, "dzrp_client_probe: command %d is in the profile's set; "
                          "--send-unsupported takes one the remote does not serve\n",
                     o.unsupported_id);
        client.close();
        return 2;
    }

    const bool seq_ok = p.sequence(client);
    int rc = seq_ok ? 0 : 1;
    if (!seq_ok) {
        if (client.state() == SessionState::Ready)
            std::fputs("stopped at the malformed reply\n", out);
        else
            std::fprintf(out, "session ended: %s\n", client.error().c_str());
        return p.summary(1);
    }

    if (!o.send_unsupported) {
        ++p.commands;
        const Reply r = client.request(CMD_CLOSE);
        if (r.status == Status::Ok) {
            p.note(ms(r.rtt));
            std::fprintf(out, "%-19s seq %2u   ok  %8.1f ms\n", "CMD_CLOSE",
                         static_cast<unsigned>(r.seq), ms(r.rtt));
        } else {
            ++p.failed;
            rc = 1;
            std::fprintf(out, "%-19s %s\n", "CMD_CLOSE", status_name(r.status));
        }
        client.close();
        return p.summary(rc);
    }

    // --send-unsupported: past the gate, on the live session's link, then a fresh session.
    std::unique_ptr<ClientLink> link = client.detach_link();
    if (!link) {
        std::fprintf(out, "dzrp_client_probe: internal error: a command is in flight\n");
        return p.summary(1);
    }
    p.unsupported(std::move(link));
    if (o.reconnect_delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(o.reconnect_delay_ms));
    DzrpClient again(p.make_link(spec), p.config());
    again.set_wait_hook(env.wait_hook);
    double c2 = 0;
    if (!p.open_session(again, "reconnect", c2)) {
        ++p.failed;
        std::fprintf(out, "%-28s FAILED  %s\n", "reconnect + CMD_INIT", again.error().c_str());
        return p.summary(1);
    }
    std::fprintf(out, "%-28s ok  %18.1f ms  \"%s\" DZRP %u.%u.%u\n", "reconnect + CMD_INIT", c2 + ms(again.init_rtt()),
                 again.remote().name.c_str(), static_cast<unsigned>(again.remote().version[0]),
                 static_cast<unsigned>(again.remote().version[1]),
                 static_cast<unsigned>(again.remote().version[2]));
    ++p.commands;
    p.note(ms(again.init_rtt()));
    const Reply r = again.request(CMD_CLOSE);
    if (r.status == Status::Ok) {
        p.note(ms(r.rtt));
        std::fprintf(out, "%-19s seq %2u   ok  %8.1f ms\n", "CMD_CLOSE", static_cast<unsigned>(r.seq),
                     ms(r.rtt));
    } else {
        ++p.failed;
        rc = 1;
        std::fprintf(out, "%-19s %s\n", "CMD_CLOSE", status_name(r.status));
    }
    ++p.commands;
    again.close();
    return p.summary(rc);
}

std::string run_probe_captured(int argc, char** argv, const ProbeEnv& env, int& rc) {
    namespace fs = std::filesystem;
    static int counter = 0;
    std::error_code ec;
    const fs::path  path = fs::temp_directory_path(ec) /
                          ("jnext_dzrp_probe_" +
                           std::to_string(Clock::now().time_since_epoch().count()) + "_" +
                           std::to_string(++counter) + ".txt");
    std::FILE* f = std::fopen(path.string().c_str(), "w+b");
    if (!f) {
        rc = -1;
        return {};
    }
    rc = run_probe(argc, argv, f, env);
    std::fflush(f);
    std::rewind(f);
    std::string out;
    char        buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    fs::remove(path, ec);
    return out;
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
