// One jnext per SD card image (GH #555): the ImageLock rows that need POSIX file-system objects
// (a symlink; a hard link). Oracle: the contract in the design (the issue and its plan): the key
// is the canonical PATH, so a symlink is the same card and a hard link, a different path, is not
// (the test harness gives each concurrent instance its own hard link on purpose).
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/image_lock.h"
#include "image_lock_common.h"

#include <fstream>
#include <string>

using namespace imglock_test;
using sdcard::ImageLock;

int main() {
    const std::string d = scratch("posix");
    const std::string locks = d + "/locks";
    const std::string card = d + "/x.img";
    { std::ofstream(card, std::ios::binary) << "card"; }
    std::error_code ec;
    fs::create_symlink(card, d + "/link.img", ec);
    const bool sym_made = !ec;
    fs::create_hard_link(card, d + "/hard.img", ec);
    const bool hard_made = !ec;

    std::string why;
    auto a = ImageLock::acquire(card, ImageLock::Kind::Image, why, locks);
    auto via_link = ImageLock::acquire(d + "/link.img", ImageLock::Kind::Image, why, locks);
    check("SDLOCK-03b", "a symlink to the card is the same lock", sym_made && a && !via_link);
    auto via_hard = ImageLock::acquire(d + "/hard.img", ImageLock::Kind::Image, why, locks);
    check("SDLOCK-06", "a hard link is a separate lock (the documented behaviour: the key is the path, not the inode)",
          hard_made && a && via_hard && via_hard->held(), why);
    return finish(d);
}
