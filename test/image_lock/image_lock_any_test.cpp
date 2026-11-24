// One jnext per SD card image (GH #555): the rows every target runs, the WebAssembly build
// included, where the lock is a no-op that always succeeds (one page, one in-memory file system).
// Oracle: the contract in the design (the issue and its plan): acquire on a free path succeeds and
// reports itself held; the per-slot holder keeps a path it already has and drops one it moved
// away from. On a native target "drops" is observable as the path being takeable again; under
// Emscripten everything is takeable, which is the same answer.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/image_lock.h"
#include "image_lock_common.h"

#include <string>

using namespace imglock_test;
using sdcard::ImageLock;

int main() {
    const std::string d = scratch("any");
    use_state_dir(d + "/state");
    const std::string locks = d + "/locks";
    std::string why;
    {
        auto a = ImageLock::acquire(d + "/a.img", ImageLock::Kind::Image, why, locks);
        check("SDLOCK-08", "an acquire on a free path succeeds and reports itself held on every target",
              a && a->held() && !a->key().empty(), why);
    }
    {
        // Off by default: a process that builds several Emulators must not be refused its own cards.
        const std::string held_img = d + "/held.img";
        auto outside = ImageLock::acquire(held_img, ImageLock::Kind::Image, why, locks);
        std::string off_why;
        const bool off1 = sdcard::sd_locks::hold(1, held_img, {}, off_why);
        check("SDLOCK-13", "while locking is not enabled hold() succeeds even on a card another holder has, and keeps nothing",
              off1 && !sdcard::sd_locks::holds(1, held_img), off_why);
        outside.reset();
        sdcard::sd_locks::set_enabled(true);
        const std::string a_img = d + "/slot-a.img", b_img = d + "/slot-b.img";
        const bool h1 = sdcard::sd_locks::hold(1, a_img, {}, why);
        const bool h2 = sdcard::sd_locks::hold(1, a_img, {}, why);   // the same card again: kept, not taken twice
        check("SDLOCK-09", "hold() on a card the slot already holds succeeds again (it is kept, not taken twice)",
              h1 && h2 && sdcard::sd_locks::holds(1, a_img), why);
        const bool h3 = sdcard::sd_locks::hold(1, b_img, {}, why);   // the slot moves to another card
        check("SDLOCK-10", "moving a slot to another card releases the old one: it is takeable again, the new one is held",
              h3 && sdcard::sd_locks::holds(1, b_img) && !sdcard::sd_locks::holds(1, a_img) &&
                  ImageLock::acquire(a_img, ImageLock::Kind::Image, why).has_value(), why);
        sdcard::sd_locks::release(1);
        check("SDLOCK-12", "release() drops the slot's locks",
              !sdcard::sd_locks::holds(1, b_img) && ImageLock::acquire(b_img, ImageLock::Kind::Image, why).has_value(), why);
        sdcard::sd_locks::set_enabled(false);
    }
    return finish(d);
}
