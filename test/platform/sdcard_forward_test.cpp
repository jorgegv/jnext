// `jnext --sdcard-file-add` forwarded to a running jnext (GH #568): src/platform/sdcard_forward.* (the client),
// the ZRCP `sd-file-add` command it speaks to, and DebugServers' publication of the server's endpoint in the SD
// card locks.
//
// ORACLE: the owner's design on the issue (connect to the running jnext's ZRCP server, run the copy there, return
// the copy's own exit status, say what is wrong otherwise with exit status 5), the lock-file format in
// core/image_lock.h, and the hostile-input contract in platform/sdcard_forward.h (loopback only, bounded time and
// size, only the exact reply forms are believed). A real ZrcpServer on a real loopback socket (port 0) is pumped on
// the main thread while the client runs on a std::thread; "a fake listener" is a raw esp listener the main thread
// serves by hand. No card is touched here: the server's hook is a stub (the real one is sdmanager::remote_add,
// tested by SDRA-* and, with a running guest, by SDCOH-R01).
//
// Run: ./build/test/sdcard_forward_test

#include "platform/sdcard_forward.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/image_lock.h"
#include "debug/debugger.h"
#include "esp01/esp_socket.h"
#include "platform/debug_servers.h"
#include "remote/zrcp/zrcp_server.h"
#include "../row_id.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace z = jnext::remote::zrcp;
using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using sdcard::LockHolder;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

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

long my_pid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

void nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

// A machine, its backend and a ZRCP server on a real loopback socket.
struct Srv {
    Emulator                emu;
    std::unique_ptr<Debugger> dbg;
    std::unique_ptr<z::ZrcpServer> zrcp;
    std::vector<z::SdFileAddRequest> seen;
    z::SdFileAddReply reply{0, "Copied x"};
    std::uint16_t port = 0;

    Srv() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        zrcp = std::make_unique<z::ZrcpServer>(*dbg);
        zrcp->set_sd_file_add([this](const z::SdFileAddRequest& r) { seen.push_back(r); return reply; });
        zrcp->server().open("127.0.0.1", 0);
        dbg->add_service(zrcp->server());
        port = zrcp->server().port();
    }
    ~Srv() { zrcp.reset(); dbg.reset(); }
    void pump() { dbg->pump(PumpBudget{}); }
    LockHolder holder() const {
        LockHolder h;
        h.pid = my_pid();
        h.has_endpoint = true;
        h.address = "127.0.0.1";
        h.port = port;
        return h;
    }
};

sdforward::ForwardRequest sample() {
    sdforward::ForwardRequest r;
#ifdef _WIN32
    r.image = "C:\\cards\\a.img";
    r.host = "C:\\src\\p.bin";
#else
    r.image = "/cards/a.img";
    r.host = "/src/p.bin";
#endif
    r.card_path = "/DOT/P";
    r.slot = 0;
    return r;
}

// Run the client on a thread while `serve` runs on this one (it gets the thread's done flag); returns the client's
// status and text.
struct Outcome { int status = -1; std::string text; };
Outcome run_client(const LockHolder& h, const sdforward::ForwardRequest& r,
                   const std::function<void(const std::atomic<bool>&)>& serve, unsigned welcome_ms = 10000,
                   unsigned reply_ms = 20000) {
    Outcome o;
    std::atomic<bool> done{false};
    std::thread t([&] {
        o.status = sdforward::forward_file_add(h, r, o.text, welcome_ms, reply_ms);
        done = true;
    });
    serve(done);
    while (!done) nap();
    t.join();
    return o;
}
Outcome via_server(Srv& s, const sdforward::ForwardRequest& r) {
    return run_client(s.holder(), r, [&](const std::atomic<bool>& done) {
        while (!done) { s.pump(); nap(); }
    });
}

// A raw peer served by hand: a listener and the one connection it accepted.
struct Fake {
    std::unique_ptr<esp::EspListener> lsn = esp::make_socket_listener(esp::ipv4(127, 0, 0, 1));
    std::unique_ptr<esp::EspTransport> conn;
    Fake() { lsn->open(0); }
    LockHolder holder() const {
        LockHolder h;
        h.pid = my_pid();
        h.has_endpoint = true;
        h.address = "127.0.0.1";
        h.port = lsn->port();
        return h;
    }
    bool accept(unsigned ms = 5000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            lsn->poll();
            conn = lsn->accept();
            if (conn) return true;
            nap();
        }
        return false;
    }
    void send(const std::string& s) {
        std::size_t at = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (at < s.size() && std::chrono::steady_clock::now() < end) {
            conn->poll();
            at += conn->send(reinterpret_cast<const std::uint8_t*>(s.data()) + at, s.size() - at);
            if (at < s.size()) nap();
        }
    }
    // read until `text` ends with `end` (or ms pass)
    std::string read_until(const std::string& end, unsigned ms = 5000) {
        std::string text;
        const auto stop = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        std::uint8_t buf[4096];
        while (std::chrono::steady_clock::now() < stop) {
            conn->poll();
            const std::size_t n = conn->recv(buf, sizeof(buf));
            text.append(reinterpret_cast<const char*>(buf), n);
            if (text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0) break;
            if (n == 0) nap();
        }
        return text;
    }
};

bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

}  // namespace

int main() {
    // SDFWD-01
    {
        auto map = [](const std::string& in, std::string* why = nullptr) {
            esp::IpAddress ip;
            std::string w;
            const bool ok = sdforward::connect_address(in, ip, w);
            if (why) *why = w;
            return ok ? esp::to_string(ip) : std::string("REFUSED");
        };
        const std::string v4 = "127.0.0.1", v6 = esp::to_string(esp::ipv6([] { std::array<std::uint8_t, 16> a{}; a[15] = 1; return a; }()));
        std::string w1, w2, w3;
        const bool maps = map("0.0.0.0") == v4 && map("127.5.5.5") == v4 && map("127.0.0.1") == v4 && map("::") == v6 &&
                          map("::1") == v6 && map("::ffff:127.0.0.1") == v4;
        const bool refuses = map("192.168.1.5", &w1) == "REFUSED" && map("fe80::1", &w2) == "REFUSED" &&
                             map("10.0.0.1", &w3) == "REFUSED" && map("localhost") == "REFUSED";
        check("SDFWD-01", "connect_address maps 0.0.0.0, 127/8 and ::ffff:127.0.0.1 to 127.0.0.1 and :: and ::1 to ::1, and refuses a LAN, link-local or non-numeric address with the documented sentence",
              maps && refuses && contains(w1, "listens on 192.168.1.5, which is not this machine's loopback; it cannot take the copy (give it --debug-listen-address 127.0.0.1 or 0.0.0.0)"),
              w1);
    }
    // SDFWD-02
    {
        Srv s;
        sdforward::ForwardRequest r = sample();
        r.force = true;
        const Outcome o = via_server(s, r);
        const std::string tail = " (by the running jnext, process " + std::to_string(my_pid()) + ")";
        check("SDFWD-02", "end to end over a real socket: the hook gets the absolute paths and the card path as sent, the client returns 0 and its text is the hook's sentence plus '(by the running jnext, process N)'",
              o.status == 0 && s.seen.size() == 1 && s.seen[0].image == r.image && s.seen[0].host == r.host &&
                  s.seen[0].card_path == "/DOT/P" && s.seen[0].force && s.seen[0].slot == 0 && !s.seen[0].writes_given &&
                  o.text == "Copied x" + tail,
              std::to_string(o.status) + " " + o.text);
    }
    // SDFWD-14
    {
        Srv s;
        s.reply = {0, std::string("Copied 100% of we ird%20.bin\x01 done")};
        const Outcome o = via_server(s, sample());
        check("SDFWD-14", "the client prints the DECODED text of an OK reply: a sentence holding % and a control byte comes out as sent",
              o.status == 0 && o.text.rfind(std::string("Copied 100% of we ird%20.bin\x01 done (by the running jnext, process "), 0) == 0, o.text);
    }
    // SDFWD-03
    {
        bool all = true;
        std::string detail;
        for (int st : {2, 3, 4, 6}) {
            Srv s;
            s.reply = {st, "failed " + std::to_string(st) + "\nsecond line 100%"};
            const Outcome o = via_server(s, sample());
            const std::string want = "error: --sdcard-file-add: failed " + std::to_string(st) + "\nsecond line 100%";
            if (o.status != st || o.text != want) { all = false; detail += std::to_string(st) + ":" + std::to_string(o.status) + " " + o.text + " | "; }
        }
        check("SDFWD-03", "hook statuses 2, 3, 4 and 6 come back as the client's exit status, printed as 'error: --sdcard-file-add: <text>' with a newline in the text kept whole",
              all, detail);
    }
    // SDFWD-04
    {
        LockHolder h;
        h.pid = 77;
        sdforward::ForwardRequest r = sample();
        const Outcome o = run_client(h, r, [](const std::atomic<bool>&) {});
        check("SDFWD-04", "a holder with no endpoint is 5, with the lock sentence and the advice to restart it with --zrcp-port PORT",
              o.status == 5 && o.text.rfind("error: the SD card image " + r.image + " is in use by another jnext (process 77), which is not listening on ZRCP", 0) == 0 &&
                  contains(o.text, "--zrcp-port PORT"), o.text);
    }
    // SDFWD-05
    {
        Srv s;
        // a first client (a debugger) holds the server's one connection
        std::unique_ptr<esp::EspTransport> first = esp::make_socket_transport([] { esp::AddressPolicy p; p.deny_loopback = false; return p; }());
        first->begin_connect("127.0.0.1", s.port);
        std::string got;
        std::uint8_t buf[512];
        for (int i = 0; i < 2000 && got.find("command> ") == std::string::npos; ++i) {
            first->poll();
            s.pump();
            const std::size_t n = first->recv(buf, sizeof(buf));
            got.append(reinterpret_cast<const char*>(buf), n);
            nap();
        }
        const Outcome o = run_client(s.holder(), sample(), [&](const std::atomic<bool>& done) {
            while (!done) { s.pump(); nap(); }
        }, 3000);
        check("SDFWD-05", "a second ZRCP client already connected (the server's one-client rule) is 5, 'another ZRCP client is connected', and the hook is not called",
              got.find("Welcome") != std::string::npos && o.status == 5 && contains(o.text, "another ZRCP client is connected") && s.seen.empty(),
              o.text);
    }
    // SDFWD-06
    {
        std::uint16_t port;
        {
            auto l = esp::make_socket_listener(esp::ipv4(127, 0, 0, 1));
            l->open(0);
            port = l->port();
        }
        LockHolder h;
        h.pid = my_pid();
        h.has_endpoint = true;
        h.address = "127.0.0.1";
        h.port = port;
        const Outcome o = run_client(h, sample(), [](const std::atomic<bool>&) {}, 3000);
        check("SDFWD-06", "a recorded port with nothing listening is 5, naming 127.0.0.1:<port> and 'cannot be reached'",
              o.status == 5 && contains(o.text, "127.0.0.1:" + std::to_string(port)) && contains(o.text, "cannot be reached"), o.text);
    }
    // SDFWD-07
    {
        Fake f;
        const Outcome o = run_client(f.holder(), sample(), [&](const std::atomic<bool>&) { f.accept(); f.read_until("never", 700); }, 300);
        check("SDFWD-07", "a listener that accepts and says nothing is 5 within the welcome bound: 'did not answer as ZRCP'",
              o.status == 5 && contains(o.text, "did not answer as ZRCP"), o.text);
    }
    // SDFWD-08
    {
        Fake f;
        const Outcome o = run_client(f.holder(), sample(), [&](const std::atomic<bool>&) { f.accept(); f.send("SSH-2.0-OpenSSH_9.9\r\n"); f.read_until("never", 700); }, 300);
        Fake g;
        const Outcome o2 = run_client(g.holder(), sample(), [&](const std::atomic<bool>&) { g.accept(); g.send("something else\ncommand> "); g.read_until("never", 700); }, 300);
        check("SDFWD-08", "a listener that sends a non-ZRCP banner, or a prompt after a welcome that is not ZRCP's, is 5, 'did not answer as ZRCP'",
              o.status == 5 && contains(o.text, "did not answer as ZRCP") && o2.status == 5 &&
                  contains(o2.text, "did not answer as ZRCP"), o.text + " | " + o2.text);
    }
    auto fake_zrcp = [&](const std::string& answer, bool close_after = false, unsigned reply_ms = 5000) {
        Fake f;
        return run_client(f.holder(), sample(), [&](const std::atomic<bool>&) {
            f.accept();
            f.send(z::WELCOME);
            f.read_until("\n", 3000);
            f.send(answer);
            if (close_after) { f.conn->close(); return; }
            f.read_until("quit\n", 700);
        }, 3000, reply_ms);
    };
    // SDFWD-09
    {
        const Outcome o = fake_zrcp("Unknown command\ncommand> ");
        check("SDFWD-09", "a ZRCP server that answers 'Unknown command' (an older jnext) is 5, 'does not have the ZRCP command sd-file-add'",
              o.status == 5 && contains(o.text, "does not have the ZRCP command sd-file-add"), o.text);
    }
    // SDFWD-10
    {
        Fake f;
        const Outcome o = run_client(f.holder(), sample(), [&](const std::atomic<bool>&) {
            f.accept();
            f.send(z::WELCOME);
            f.read_until("\n", 3000);
            f.send(std::string(200 * 1024, 'x'));
            f.read_until("never", 500);
        }, 3000, 10000);
        check("SDFWD-10", "a server that floods with no prompt is cut at 64 KiB: 5, without hanging",
              o.status == 5 && contains(o.text, "64 KiB"), o.text);
    }
    // SDFWD-11
    {
        const Outcome a = fake_zrcp("Error. [9] nope\ncommand> ");
        const Outcome b = fake_zrcp("Error. [0] nope\ncommand> ");
        const Outcome c = fake_zrcp("garbage\ncommand> ");
        check("SDFWD-11", "a reply carrying Error [9] or Error [0], or no known form, is 5, a protocol error",
              a.status == 5 && b.status == 5 && c.status == 5 && contains(a.text, "protocol error") && contains(b.text, "protocol error") &&
                  contains(c.text, "protocol error"), a.text + " | " + b.text + " | " + c.text);
    }
    // SDFWD-12
    {
        const Outcome o = fake_zrcp("OK. Cop", true);
        check("SDFWD-12", "a server that closes the connection mid-reply is 5, never a success", o.status == 5 && !contains(o.text, "Copied"), o.text);
    }
    // SDFWD-13
    {
        const std::string dir = (fs::temp_directory_path() / ("jnext-sdfwd-" + std::to_string(my_pid()))).string();
        fs::remove_all(dir);
        fs::create_directories(dir);
#ifdef _WIN32
        _putenv_s("JNEXT_CONFIG_DIR", dir.c_str());
#else
        setenv("JNEXT_CONFIG_DIR", dir.c_str(), 1);
#endif
        const std::string img = dir + "/a.img", wr = dir + "/a.jsdw";
        { std::ofstream(img, std::ios::binary) << "card"; }
        sdcard::sd_locks::set_enabled(true);
        std::string why;
        const bool held = sdcard::sd_locks::hold(0, img, wr, why);
        auto bodies = [&] {
            std::string all;
            for (auto& e : fs::directory_iterator(dir + "/locks")) {
                std::ifstream in(e.path(), std::ios::binary);
                all += std::string(std::istreambuf_iterator<char>(in), {}) + "|";
            }
            return all;
        };
        const std::string before = bodies();
        std::string with;
        std::uint16_t bound = 0;
        bool ok13 = false;
        {
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZX48K;
            cfg.zrcp_port = 0;
            emu.init(cfg);
            Debugger dbg(emu);
            DebugServers servers;
            const bool started = servers.start(dbg, cfg, [](const z::SdFileAddRequest&) { return z::SdFileAddReply{0, "ok"}; });
            LockHolder probe;
            std::string w2;
            sdcard::ImageLock::acquire(img, sdcard::ImageLock::Kind::Image, w2, {}, &probe);
            bound = probe.port;
            with = bodies();
            ok13 = (held && started && bound != 0 && probe.has_endpoint && probe.address == "127.0.0.1" &&
                      with.find("zrcp_listening_port: " + std::to_string(bound)) != std::string::npos &&
                      with.find("writes: ") != std::string::npos &&
                      with.find("zrcp_listening_port: " + std::to_string(bound), with.find("zrcp_listening_port: " + std::to_string(bound)) + 1) != std::string::npos &&
                      before.find("zrcp_") == std::string::npos);
        }
        const std::string after = bodies();
        check("SDFWD-13", "DebugServers with --zrcp-port 0 puts the BOUND port in the slot's image and writes locks (a refused second holder reads it), and withdraws it when destroyed",
              ok13 && after.find("zrcp_") == std::string::npos, with + " / " + after);
        sdcard::sd_locks::release(0);
        sdcard::sd_locks::set_enabled(false);
        fs::remove_all(dir);
    }
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
