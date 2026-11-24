#pragma once
// One jnext per SD card image (GH #555).
//
// Two jnext processes on one card image each cache the FAT and write sectors the other never
// sees, which corrupts the card. An ImageLock is the guard: an advisory, exclusive, per-process
// lock on a PATH (the canonical one, so ./x.img, a/../x.img and a symlink to it are one lock),
// released when the object dies and by the OS when the process does, kill -9 included. There is
// no stale-lock logic: a left-over lock file is inert.
//
// The lock is a small file under <state dir>/locks/, named by a hash of the canonical path, not
// the card itself, so a read-only card or directory needs no sidecar, and an upgrade that renames
// files under a fixed path keeps the lock. Hard links are therefore different cards (the test
// harness gives each concurrent instance its own hard link of the run clone on purpose).
//
// POSIX: flock(LOCK_EX|LOCK_NB). Windows: LockFileEx on a byte past the content, so the holder's
// pid stays readable. Emscripten: a no-op that always succeeds (one page, one in-memory file system).
#include "core/held_files.h"
#include <optional>
#include <string>

namespace sdcard {

class ImageLock {
public:
    enum class Kind { Image, Writes };

    ImageLock() = default;
    ImageLock(ImageLock&& o) noexcept;
    ImageLock& operator=(ImageLock&& o) noexcept;
    ImageLock(const ImageLock&) = delete;
    ImageLock& operator=(const ImageLock&) = delete;
    ~ImageLock();

    /// Lock `path` (it need not exist). `lock_dir` empty means <default_state_dir()>/locks.
    /// On a refusal returns nullopt and `why` holds the user-facing message:
    ///   the SD card image <path> is in use by another jnext (process <pid>); give each jnext its own card (--sdcard FILE)
    ///   the SD-writes file <path> is in use by another jnext (process <pid>)
    static std::optional<ImageLock> acquire(const std::string& path, Kind kind, std::string& why,
                                            const std::string& lock_dir = {});

    /// The canonical key the lock is taken on (what two spellings of one card share).
    const std::string& key() const { return key_; }
    bool held() const { return held_; }

private:
    void release();
    std::string key_;
    bool        held_ = false;
    held_files::Hold hold_;   // the lock file under <state dir>/locks: trashing it defeats the second-jnext refusal
#ifdef _WIN32
    void*       handle_ = nullptr;
#else
    int         fd_ = -1;
#endif
};

/// The locks of this process, by card slot (0 = first card, 1 = second): the image and its
/// SD-writes file. Startup takes them in main(); a live card change moves them with the card.
namespace sd_locks {
/// Locking is OFF until a frontend turns it on (main() does, once, before it takes its cards):
/// the Emulator class is also built many times in one process by the unit suites and rebuilt by a
/// cold boot, and a process-wide registry must not refuse a card to a second Emulator of the same
/// process. While off, hold() succeeds without locking anything and holds() is false.
void set_enabled(bool on);
/// Take `image` (and `writes_file` when not empty) for `slot`, then drop what the slot held before.
/// A path the slot already holds is kept, not taken twice. False and `why` on a refusal, with the
/// slot's previous locks untouched. An empty `image` just releases the slot.
bool hold(int slot, const std::string& image, const std::string& writes_file, std::string& why);
void release(int slot);
/// True while `slot` holds the lock on `image` (the canonical key is compared).
bool holds(int slot, const std::string& image);
}  // namespace sd_locks

}  // namespace sdcard
