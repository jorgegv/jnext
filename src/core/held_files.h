#pragma once

// ---------------------------------------------------------------------------
// The host files and folders the running process keeps open, writes to again, or serves: one
// registry, written by the holders themselves (GH #558).
//
// WHY. The SD card file manager must never delete, trash, replace or copy a file the machine is
// using: an SD image, an SD-writes file (reopened BY PATH on every commit, so a trashed one comes
// back without its header), an image lock file (a second jnext would take a fresh lock on the same
// card), a recording, a log, a FIFO, the NextPi overlay. A list kept in the manager would miss every
// holder added after it; so each holder records the path it holds WHILE it holds it, and the manager
// reads this registry. A new holder that records nothing is the only way to be missed, and the
// developer guide says so.
//
// HOW. `Hold` is an RAII token: construct it with the path when the file is opened, destroy or
// reset() it when it is closed. It is copyable (a copy holds too) and movable. The registry is a
// reference-counted multiset behind a mutex; add/remove are for open and close, never a hot path.
// `output_files::begin/end` (the writers of the output options) feed it too.
// ---------------------------------------------------------------------------

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace held_files {

namespace detail {
// Never destroyed: a Hold in an object with static storage (a global device, a recorder) is released AFTER
// function-local statics built later have gone, and must still find a live registry.
inline std::mutex& mu() { static std::mutex* m = new std::mutex; return *m; }
inline std::map<std::string, int>& counts() { static auto* c = new std::map<std::string, int>; return *c; }
}  // namespace detail

inline void add(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> g(detail::mu());
    ++detail::counts()[path];
}

inline void remove(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> g(detail::mu());
    auto it = detail::counts().find(path);
    if (it == detail::counts().end()) return;
    if (--it->second <= 0) detail::counts().erase(it);
}

/// Every path held right now, as the holders spelt them.
inline std::vector<std::string> snapshot() {
    std::lock_guard<std::mutex> g(detail::mu());
    std::vector<std::string> v;
    for (const auto& kv : detail::counts()) v.push_back(kv.first);
    return v;
}

/// Holds `path` for as long as it lives.
class Hold {
public:
    Hold() = default;
    explicit Hold(std::string path) : path_(std::move(path)) { add(path_); }
    Hold(const Hold& o) : path_(o.path_) { add(path_); }
    Hold(Hold&& o) noexcept : path_(std::move(o.path_)) { o.path_.clear(); }
    Hold& operator=(const Hold& o) {
        if (this != &o) { reset(); path_ = o.path_; add(path_); }
        return *this;
    }
    Hold& operator=(Hold&& o) noexcept {
        if (this != &o) { reset(); path_ = std::move(o.path_); o.path_.clear(); }
        return *this;
    }
    ~Hold() { reset(); }
    void reset() { remove(path_); path_.clear(); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace held_files
