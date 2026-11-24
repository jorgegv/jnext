#include "core/image_lock.h"

#include "core/sdcard_provisioner.h"
#include "core/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <process.h>
#elif !defined(__EMSCRIPTEN__)
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace sdcard {

namespace {

namespace fs = std::filesystem;

std::string canonical_key(const std::string& path)
{
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    if (ec) p = fs::path(path);
    fs::path c = fs::weakly_canonical(p, ec);
    std::string key = (ec ? p.lexically_normal() : c).generic_string();
#if defined(_WIN32) || defined(__APPLE__)
    // Case-insensitive volumes: Card.IMG and card.img are one file.
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
    return key;
}

#ifndef __EMSCRIPTEN__
std::string lock_file_for(const std::string& key, const std::string& lock_dir)
{
    const std::string digest =
        sha256_hex(std::vector<uint8_t>(key.begin(), key.end()));
    std::string dir = lock_dir.empty() ? default_state_dir() + "/locks" : lock_dir;
    return dir + "/" + digest.substr(0, 16) + ".lock";
}

std::string message(ImageLock::Kind kind, const std::string& path, const std::string& pid)
{
    if (kind == ImageLock::Kind::Writes)
        return "the SD-writes file " + path + " is in use by another jnext (process " + pid + ")";
    return "the SD card image " + path + " is in use by another jnext (process " + pid +
           "); give each jnext its own card (--sdcard FILE)";
}

// The pid line of a lock file, "?" when the holder has not written it yet.
std::string holder_pid(const std::string& lock_file)
{
    std::ifstream in(lock_file, std::ios::binary);
    std::string pid;
    if (in) std::getline(in, pid);
    while (!pid.empty() && (pid.back() == '\r' || pid.back() == ' ')) pid.pop_back();
    return pid.empty() ? "?" : pid;
}
#endif  // !__EMSCRIPTEN__

}  // namespace

ImageLock::ImageLock(ImageLock&& o) noexcept
    : key_(std::move(o.key_)), held_(o.held_)
#ifdef _WIN32
    , handle_(o.handle_)
#else
    , fd_(o.fd_)
#endif
{
    o.held_ = false;
#ifdef _WIN32
    o.handle_ = nullptr;
#else
    o.fd_ = -1;
#endif
}

ImageLock& ImageLock::operator=(ImageLock&& o) noexcept
{
    if (this != &o) {
        release();
        key_  = std::move(o.key_);
        held_ = o.held_;
#ifdef _WIN32
        handle_ = o.handle_; o.handle_ = nullptr;
#else
        fd_ = o.fd_; o.fd_ = -1;
#endif
        o.held_ = false;
    }
    return *this;
}

ImageLock::~ImageLock() { release(); }

void ImageLock::release()
{
#ifdef __EMSCRIPTEN__
    held_ = false;
#elif defined(_WIN32)
    if (handle_) { CloseHandle(static_cast<HANDLE>(handle_)); handle_ = nullptr; }
    held_ = false;
#else
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }   // closing drops the flock
    held_ = false;
#endif
}

std::optional<ImageLock> ImageLock::acquire(const std::string& path, Kind kind,
                                            std::string& why, const std::string& lock_dir)
{
    ImageLock lk;
    lk.key_ = canonical_key(path);
#ifdef __EMSCRIPTEN__
    (void)kind; (void)why; (void)lock_dir;
    lk.held_ = true;
    return lk;
#else
    const std::string file = lock_file_for(lk.key_, lock_dir);
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string body = std::to_string(
#ifdef _WIN32
        static_cast<long>(_getpid())
#else
        static_cast<long>(::getpid())
#endif
    ) + "\n" + lk.key_ + "\n";
#ifdef _WIN32
    HANDLE h = CreateFileW(fs::path(file).wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = "cannot create the lock file " + file + " for " + path;
        return std::nullopt;
    }
    // One byte far past the content: the pid stays readable by the process that is refused.
    OVERLAPPED ov{};
    ov.Offset = 0x10000000;
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        CloseHandle(h);
        why = message(kind, path, holder_pid(file));
        return std::nullopt;
    }
    SetFilePointer(h, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(h);
    DWORD n = 0;
    WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &n, nullptr);
    lk.handle_ = h;
#else
    const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        why = "cannot create the lock file " + file + " for " + path;
        return std::nullopt;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        why = message(kind, path, holder_pid(file));
        return std::nullopt;
    }
    if (::ftruncate(fd, 0) == 0) {
        const ssize_t w = ::write(fd, body.data(), body.size());
        (void)w;
    }
    lk.fd_ = fd;
#endif
    lk.held_ = true;
    return lk;
#endif
}

namespace sd_locks {
namespace {
struct SlotLocks {
    std::optional<ImageLock> image;
    std::optional<ImageLock> writes;
};
std::mutex& mu() { static std::mutex m; return m; }
bool g_enabled = false;   // guarded by mu()
SlotLocks* slots() { static SlotLocks s[2]; return s; }

// `path` kept from `have` when it is the same card (`carried` says so), else taken now.
bool take(std::optional<ImageLock>& have, const std::string& path, ImageLock::Kind kind,
          std::optional<ImageLock>& out, bool& carried, std::string& why)
{
    out.reset();
    carried = false;
    if (path.empty()) return true;
    if (have && have->key() == canonical_key(path)) { out = std::move(have); have.reset(); carried = true; return true; }
    out = ImageLock::acquire(path, kind, why);
    return out.has_value();
}
}  // namespace

void set_enabled(bool on)
{
    std::lock_guard<std::mutex> g(mu());
    g_enabled = on;
    if (!on) { slots()[0].image.reset(); slots()[0].writes.reset(); slots()[1].image.reset(); slots()[1].writes.reset(); }
}

bool hold(int slot, const std::string& image, const std::string& writes_file, std::string& why)
{
    if (slot < 0 || slot > 1) { why = "no such SD card slot"; return false; }
    std::lock_guard<std::mutex> g(mu());
    if (!g_enabled) return true;
    SlotLocks& s = slots()[slot];
    std::optional<ImageLock> img, wr;
    bool img_carried = false, wr_carried = false;
    if (!take(s.image, image, ImageLock::Kind::Image, img, img_carried, why)) return false;   // nothing moved
    if (!take(s.writes, writes_file, ImageLock::Kind::Writes, wr, wr_carried, why)) {
        // A refusal changes nothing: an image lock carried out of the slot goes back to it, one taken
        // just now is dropped (it was never the slot's).
        if (img_carried) s.image = std::move(img);
        return false;
    }
    s.image  = std::move(img);    // the old locks, if not carried over, die here
    s.writes = std::move(wr);
    return true;
}

void release(int slot)
{
    if (slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> g(mu());
    slots()[slot].image.reset();
    slots()[slot].writes.reset();
}

bool holds(int slot, const std::string& image)
{
    if (slot < 0 || slot > 1) return false;
    std::lock_guard<std::mutex> g(mu());
    const auto& i = slots()[slot].image;
    return i && i->key() == canonical_key(image);
}

}  // namespace sd_locks
}  // namespace sdcard
