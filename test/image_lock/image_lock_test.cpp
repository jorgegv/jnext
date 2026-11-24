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

#include <fstream>
#include <optional>
#include <string>

using namespace imglock_test;
using sdcard::ImageLock;

namespace {
std::string slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
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
    return finish(d);
}
