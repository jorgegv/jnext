// One jnext per SD card image (GH #555): the ImageLock, native platforms.
//
// Oracle: the contract in the design (the issue and its plan): a card is held by at most one
// jnext at a time; the lock is keyed on the canonical PATH (spellings of one card are one lock),
// is released with its holder, and a left-over lock file never blocks. The refusal text is the
// one the user guide quotes. A second holder is simulated with a second acquire in this process:
// flock and LockFileEx conflict between two open handles of one process exactly as between two
// processes (SDLOCK-02 pins that this is what is being relied on).
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/image_lock.h"
#include "image_lock_common.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

using namespace imglock_test;
using sdcard::ImageLock;
using sdcard::LockHolder;
using sdcard::parse_lock_body;

namespace {
std::string slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
long my_pid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}
// The body of the lock file in `dir` whose content names `needle` ("" when none does).
std::string lock_body_naming(const std::string& dir, const std::string& needle) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string c = slurp(it->path().string());
        if (c.find(needle) != std::string::npos) return c;
    }
    return {};
}
size_t count_files(const std::string& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) ++n;
    return n;
}
}  // namespace

int main() {
    const std::string d = scratch("native");
    use_state_dir(d + "/state");
    const std::string locks = d + "/locks";
    const std::string card = d + "/x.img";
    { std::ofstream(card, std::ios::binary) << "card"; }

    {
        std::string why;
        auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
        std::string content;
        for (auto& e : fs::directory_iterator(locks)) content = slurp(e.path().string());
        check("SDLOCK-01", "an acquire on a path succeeds, holds, and the lock file carries the pid and the canonical path",
              a && a->held() && content.find('\n') != std::string::npos &&
                  content.find(fs::path(card).filename().string()) != std::string::npos,
              why + " / " + content);
    }
    {
        std::string why, why2;
        auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
        auto b = ImageLock::acquire(card, ImageLock::Kind::Image, why2, locks);
        const std::string want_pre = "the SD card image " + card + " is in use by another jnext (process ";
        check("SDLOCK-02", "a second acquire of the same path fails with the documented message naming the path and the pid",
              a && !b && why2.rfind(want_pre, 0) == 0 &&
                  why2.find("); give each jnext its own card (--sdcard FILE)") != std::string::npos &&
                  why2.find("(process ?)") == std::string::npos,
              why2);
        std::string why3;
        auto w1 = ImageLock::acquire(d + "/w.jsdw", ImageLock::Kind::Writes, why3, locks);
        auto w2 = ImageLock::acquire(d + "/w.jsdw", ImageLock::Kind::Writes, why3, locks);
        check("SDLOCK-02b", "the writes-file refusal has its own wording and no card advice",
              w1 && !w2 && why3.rfind("the SD-writes file " + d + "/w.jsdw is in use by another jnext (process ", 0) == 0 &&
                  why3.find("--sdcard") == std::string::npos,
              why3);
    }
    {
        std::string why;
        auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
        auto dot = ImageLock::acquire(d + "/./x.img", ImageLock::Kind::Image, why, locks);
        fs::create_directories(d + "/sub");
        auto up = ImageLock::acquire(d + "/sub/../x.img", ImageLock::Kind::Image, why, locks);
        check("SDLOCK-03", "./x.img and a/../x.img are the same lock as x.img", a && !dot && !up);
    }
    {
        std::string why;
        bool first = false;
        { auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks); first = a.has_value(); }
        auto b = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);   // free again
        std::optional<ImageLock> c = std::move(b);
        auto again = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
        check("SDLOCK-04", "the lock is released when its holder is destroyed; a moved lock is still held, by its new owner",
              first && c && c->held() && !again, why);
    }
    {
        const size_t before = count_files(d);
        std::string why;
        auto a = ImageLock::acquire(d + "/not-yet.jsdw", ImageLock::Kind::Writes, why, locks);
        check("SDLOCK-05", "a lock on a path that does not exist yet works and creates nothing at that path",
              a && a->held() && !fs::exists(d + "/not-yet.jsdw") && count_files(d) == before, why);
    }
    {
        // A lock file left by a dead holder: written but unlocked.
        std::string why;
        const std::string dead = d + "/dead.img";
        { auto a = ImageLock::acquire(dead, ImageLock::Kind::Image, why, locks); }
        std::string lock_file;
        for (auto& e : fs::directory_iterator(locks)) {
            if (slurp(e.path().string()).find("dead.img") != std::string::npos) lock_file = e.path().string();
        }
        { std::ofstream(lock_file, std::ios::binary | std::ios::trunc) << "99999999\n" << dead << "\n"; }
        auto b = ImageLock::acquire(dead, ImageLock::Kind::Image, why, locks);
        check("SDLOCK-07", "a stale lock file naming a dead pid does not block an acquire", !lock_file.empty() && b && b->held(), why);
    }
    {
        // sd_locks: a refusal leaves the slot's previous locks untouched.
        std::string why;
        const std::string a_img = d + "/a.img", b_img = d + "/b.img";
        // sd_locks takes its lock files under the default state dir: point it at the scratch dir.
        // (The state dir is $JNEXT_CONFIG_DIR, set by the harness for this suite.)
        sdcard::sd_locks::set_enabled(true);
        bool ok1 = sdcard::sd_locks::hold(0, a_img, {}, why);
        auto other = ImageLock::acquire(b_img, ImageLock::Kind::Image, why);   // default lock dir, as hold() uses
        bool refused = !sdcard::sd_locks::hold(0, b_img, {}, why);
        const std::string msg = why;
        bool kept = sdcard::sd_locks::holds(0, a_img);
        std::string w2;
        auto probe = ImageLock::acquire(a_img, ImageLock::Kind::Image, w2);
        check("SDLOCK-11", "a refused hold() leaves the slot's previous lock untouched and says why",
              ok1 && other && refused && kept && !probe && msg.find(b_img) != std::string::npos, msg);
        probe.reset(); other.reset();
        sdcard::sd_locks::release(0);
    }
    {
        // S2-2: a refused writes file changes nothing.
        sdcard::sd_locks::set_enabled(true);
        std::string why;
        const std::string x = d + "/x2.img", y = d + "/y2.img", w = d + "/held.jsdw";
        auto outside = ImageLock::acquire(w, ImageLock::Kind::Writes, why);
        const bool hx = sdcard::sd_locks::hold(0, x, {}, why);
        const bool refused = !sdcard::sd_locks::hold(0, x, w, why);     // the slot keeps X, the writes file is held elsewhere
        std::string w2;
        const bool x_kept = sdcard::sd_locks::holds(0, x) && !ImageLock::acquire(x, ImageLock::Kind::Image, w2).has_value();
        check("SDLOCK-14", "a hold() whose writes file is held elsewhere is refused and the slot keeps the image it had (still locked)",
              outside && hx && refused && x_kept && why.find(w) != std::string::npos, why);
        sdcard::sd_locks::release(0);
        std::string w3, w4;
        const bool refused2 = !sdcard::sd_locks::hold(1, y, w, w3);     // an EMPTY slot: nothing may be left locked
        auto probe = ImageLock::acquire(y, ImageLock::Kind::Image, w4);
        check("SDLOCK-15", "a hold() on an empty slot whose writes file is held elsewhere is refused and leaves its image UNlocked (nothing newly taken stays)",
              refused2 && !sdcard::sd_locks::holds(1, y) && probe.has_value(), w3 + w4);
        probe.reset();
        sdcard::sd_locks::set_enabled(false);
    }
    // GH #568: labelled lock bodies, the ZRCP endpoint, and the one strict parser. Oracle: the owner's
    // lock-file format (issue comments) and the contract in image_lock.h.
    const std::string pidline = "pid: " + std::to_string(my_pid()) + "\n";
    {
        std::string why;
        const std::string img = d + "/body.img", wr = d + "/body.jsdw";
        auto a = ImageLock::acquire(img, ImageLock::Kind::Image, why, locks);
        auto w = ImageLock::acquire(wr, ImageLock::Kind::Writes, why, locks);
        const std::string ba = lock_body_naming(locks, "body.img"), bw = lock_body_naming(locks, "body.jsdw");
        check("SDLOCK-16", "a lock body is exactly 'pid: <pid>' then 'image: <key>' (a writes lock says 'writes: <key>')",
              a && w && ba == pidline + "image: " + a->key() + "\n" && bw == pidline + "writes: " + w->key() + "\n",
              ba + "/" + bw);
    }
    {
        std::string why;
        LockHolder h;
        auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
        auto b = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks, &h);
        check("SDLOCK-17", "a refusal names the holder's exact pid (not a raw label line) and LockHolder.pid is that pid",
              a && !b && h.pid == my_pid() && !h.has_endpoint &&
                  why.find("(process " + std::to_string(my_pid()) + ")") != std::string::npos &&
                  why.find("pid:") == std::string::npos,
              why);
    }
    {
        sdcard::sd_locks::set_enabled(true);
        std::string why;
        const std::string i0 = d + "/e0.img", w0 = d + "/e0.jsdw", i1 = d + "/e1.img", w1 = d + "/e1.jsdw";
        const bool h0 = sdcard::sd_locks::hold(0, i0, w0, why), h1 = sdcard::sd_locks::hold(1, i1, w1, why);
        const std::string state_locks = d + "/state/locks";
        const std::string tail = "zrcp_listening_address: 127.0.0.1\nzrcp_listening_port: 4321\n";
        sdcard::sd_locks::set_zrcp_endpoint("127.0.0.1", 4321);
        bool all = h0 && h1;
        for (const char* n : {"e0.img", "e0.jsdw", "e1.img", "e1.jsdw"}) {
            const std::string b = lock_body_naming(state_locks, n);
            all = all && b.size() > tail.size() && b.compare(b.size() - tail.size(), tail.size(), tail) == 0 &&
                  b.rfind(pidline, 0) == 0;
        }
        std::string w2;
        const bool still = !ImageLock::acquire(i0, ImageLock::Kind::Image, w2).has_value() &&
                           !ImageLock::acquire(w1, ImageLock::Kind::Writes, w2).has_value();
        check("SDLOCK-18", "set_zrcp_endpoint adds the endpoint lines to the image AND writes locks of BOTH slots, and every lock stays held",
              all && still, lock_body_naming(state_locks, "e1.jsdw"));

        const std::string i2 = d + "/e2.img";
        const bool h2 = sdcard::sd_locks::hold(0, i2, {}, why);
        const std::string b2 = lock_body_naming(state_locks, "e2.img");
        check("SDLOCK-19", "a lock taken by hold() after the endpoint was set (a live card change) carries it",
              h2 && b2.size() > tail.size() && b2.compare(b2.size() - tail.size(), tail.size(), tail) == 0, b2);

        LockHolder hh;
        std::string w3;
        auto probe = ImageLock::acquire(i1, ImageLock::Kind::Image, w3, {}, &hh);
        check("SDLOCK-21", "a refusal of a lock that carries the endpoint reports address and port in LockHolder",
              !probe && hh.has_endpoint && hh.address == "127.0.0.1" && hh.port == 4321 && hh.pid == my_pid(), w3);

        sdcard::sd_locks::set_zrcp_endpoint("127.0.0.1", 0);
        bool clean = true;
        for (const char* n : {"e2.img", "e1.img", "e1.jsdw"}) {
            const std::string b = lock_body_naming(state_locks, n);
            clean = clean && b.find("zrcp_") == std::string::npos && b.rfind(pidline, 0) == 0 &&
                    std::count(b.begin(), b.end(), '\n') == 2;
        }
        check("SDLOCK-20", "set_zrcp_endpoint with port 0 removes both lines: the body is the plain two-line one again", clean,
              lock_body_naming(state_locks, "e1.img"));
        sdcard::sd_locks::release(0);
        sdcard::sd_locks::release(1);
        sdcard::sd_locks::set_enabled(false);
    }
    {
        const LockHolder legacy = parse_lock_body("12345\n/x.img\n");
        check("SDLOCK-22", "the older unlabelled body gives its pid and no endpoint",
              legacy.pid == 12345 && !legacy.has_endpoint);
    }
    {
        const std::string head = "pid: 7\nimage: /x\nzrcp_listening_address: 127.0.0.1\n";
        const LockHolder partial = parse_lock_body(head + "zrcp_listening_port: 100");
        const LockHolder full = parse_lock_body(head + "zrcp_listening_port: 100\n");
        check("SDLOCK-23", "a last line with no newline is ignored (a rewrite in flight); with its newline it counts",
              !partial.has_endpoint && partial.pid == 7 && full.has_endpoint && full.port == 100);
    }
    {
        auto port_of = [](const std::string& v) {
            return parse_lock_body("pid: 7\nzrcp_listening_address: 127.0.0.1\nzrcp_listening_port:" + v + "\n");
        };
        const bool bad = !port_of(" 0").has_endpoint && !port_of(" 65536").has_endpoint && !port_of(" -1").has_endpoint &&
                         !port_of(" 10000x").has_endpoint && !port_of("  10000").has_endpoint &&
                         !port_of(" " + std::string(30, '1')).has_endpoint && !port_of(" ").has_endpoint;
        const LockHolder top = port_of(" 65535"), low = port_of(" 1");
        check("SDLOCK-24", "ports 0, 65536, -1, trailing junk, a leading space, 30 digits and none are no endpoint; 65535 and 1 are accepted",
              bad && top.has_endpoint && top.port == 65535 && low.has_endpoint && low.port == 1);
    }
    {
        auto addr_of = [](const std::string& v) {
            return parse_lock_body("pid: 7\nzrcp_listening_address: " + v + "\nzrcp_listening_port: 99\n");
        };
        const LockHolder noaddr = parse_lock_body("pid: 7\nzrcp_listening_port: 99\n");
        check("SDLOCK-25", "an address of localhost, 1.2.3.4.5 or nothing is no endpoint; a port with no address line means 127.0.0.1",
              !addr_of("localhost").has_endpoint && !addr_of("1.2.3.4.5").has_endpoint && !addr_of("").has_endpoint &&
                  addr_of("::1").has_endpoint && noaddr.has_endpoint && noaddr.address == "127.0.0.1");
    }
    {
        const LockHolder twice = parse_lock_body("pid: 7\nzrcp_listening_port: 99\nzrcp_listening_port: 100\n");
        const LockHolder crlf = parse_lock_body("pid: 7\r\nzrcp_listening_address: 127.0.0.1\r\nzrcp_listening_port: 99\r\n");
        const LockHolder nul = parse_lock_body(std::string("pid: 7\nzrcp_listening_port: 9") + '\0' + "9\n");
        std::string big = "pid: 7\n";
        while (big.size() < (1u << 20)) big += "zzz: " + std::string(100, 'x') + "\n";
        big += "zrcp_listening_port: 99\n";
        const LockHolder b = parse_lock_body(big);
        // every label counts: a repeated address drops the endpoint, a repeated pid leaves the pid unknown
        const LockHolder addr2 = parse_lock_body("pid: 7\nzrcp_listening_address: 127.0.0.1\nzrcp_listening_address: 127.0.0.2\nzrcp_listening_port: 99\n");
        const LockHolder pid2 = parse_lock_body("pid: 7\npid: 8\nzrcp_listening_port: 99\n");
        check("SDLOCK-26", "a label given twice (port, address or pid) is ignored, CRLF is accepted, a NUL makes a value junk, a 1 MiB body is read only up to 4 KiB",
              !twice.has_endpoint && crlf.has_endpoint && crlf.port == 99 && crlf.pid == 7 && !nul.has_endpoint &&
                  b.pid == 7 && !b.has_endpoint && !addr2.has_endpoint && pid2.pid == 0 && pid2.has_endpoint);
    }
    {
        // long is 32 bits on Windows: a pid of 10 digits above 2^31 must not throw (std::stol would), it is an unknown pid
        // and the endpoint verdict is kept. This row runs under wine too.
        bool threw = false;
        LockHolder a, b, c, d;
        try {
            a = parse_lock_body("pid: 4294967295\nzrcp_listening_address: 127.0.0.1\nzrcp_listening_port: 99\n");
            b = parse_lock_body("4294967295\n/x\n");
            c = parse_lock_body("pid: 999999999\n");
            d = parse_lock_body("pid: 99999999999999999999999\n");
        } catch (...) {
            threw = true;
        }
        check("SDLOCK-28", "a pid of 10 digits or more (above 2^31) in a lock file does not throw: the pid is unknown (0), the endpoint verdict is kept; 9 digits are kept",
              !threw && a.pid == 0 && a.has_endpoint && a.port == 99 && b.pid == 0 && !b.has_endpoint && c.pid == 999999999 && d.pid == 0);
    }
    {
        const std::string notdir = d + "/notadir";
        { std::ofstream(notdir, std::ios::binary) << "x"; }
        std::string why;
        LockHolder h;
        auto a = ImageLock::acquire(d + "/c.img", ImageLock::Kind::Image, why, notdir, &h);
        check("SDLOCK-27", "'cannot create the lock file' leaves LockHolder empty (nothing to forward to)",
              !a && why.rfind("cannot create the lock file", 0) == 0 && h.pid == 0 && !h.has_endpoint, why);
    }
    return finish(d);
}
