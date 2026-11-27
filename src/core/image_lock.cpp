#include "core/image_lock.h"

#include "core/sdcard_provisioner.h"
#include "core/sha256.h"
#include "esp01/esp_socket.h"

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

// The first 4 KiB of a lock file ("" when it cannot be read).
std::string read_lock_file(const std::string& lock_file)
{
    std::ifstream in(lock_file, std::ios::binary);
    std::string text(4096, '\0');
    if (!in) return {};
    in.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    return text;
}

std::string pid_text(const LockHolder& h) { return h.pid > 0 ? std::to_string(h.pid) : std::string("?"); }

long this_pid()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}
#endif  // !__EMSCRIPTEN__

}  // namespace

namespace {

bool all_digits(const std::string& v, size_t max_len)
{
    if (v.empty() || v.size() > max_len) return false;
    for (char c : v) if (c < '0' || c > '9') return false;
    return true;
}

// A numeric IP: only the characters of one, then the platform's own numeric parse.
bool numeric_ip(const std::string& v)
{
    if (v.empty() || v.size() > 45) return false;
    for (char c : v) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.';
        if (!ok) return false;
    }
    esp::IpAddress ip;
    return esp::parse_ip(v, ip);
}

// The process-wide ZRCP endpoint. Its own mutex: ImageLock::acquire() reads it while sd_locks::hold()
// holds the slot mutex, and set_zrcp_endpoint() takes the slot mutex first (always that order).
struct Endpoint { std::string address; uint16_t port = 0; };
std::mutex& ep_mu() { static std::mutex m; return m; }
Endpoint& ep() { static Endpoint e; return e; }
#ifndef __EMSCRIPTEN__
Endpoint endpoint_now() { std::lock_guard<std::mutex> g(ep_mu()); return ep(); }
#endif

}  // namespace

// A pid is at most 9 digits (long is 32 bits on Windows: stol would throw on more); anything else is unknown (0).
static long pid_of(const std::string& v)
{
    return all_digits(v, 9) ? std::stol(v) : 0;
}

LockHolder parse_lock_body(const std::string& whole)
{
    const std::string text = whole.size() > 4096 ? whole.substr(0, 4096) : whole;
    LockHolder h;
    // Complete lines only: whatever follows the last newline is dropped.
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t nl; (nl = text.find('\n', start)) != std::string::npos; start = nl + 1) {
        std::string line = text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    if (lines.empty()) return h;
    if (all_digits(lines[0], 10)) {           // the older, unlabelled form
        h.pid = pid_of(lines[0]);
        return h;
    }
    struct Field { int count = 0; std::string value; bool present() const { return count > 0; } };
    Field pid, addr, port;
    for (const std::string& line : lines) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string label = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);   // exactly one separating space
        Field* f = label == "pid" ? &pid : label == "zrcp_listening_address" ? &addr
                 : label == "zrcp_listening_port" ? &port : nullptr;
        if (!f) continue;
        ++f->count;
        f->value = std::move(value);
    }
    if (pid.count == 1) h.pid = pid_of(pid.value);
    if (port.count != 1 || !all_digits(port.value, 5)) return h;
    const long p = std::stol(port.value);
    if (p < 1 || p > 65535) return h;
    std::string address = "127.0.0.1";
    if (addr.present()) {
        if (addr.count != 1 || !numeric_ip(addr.value)) return h;
        address = addr.value;
    }
    h.has_endpoint = true;
    h.address = address;
    h.port = static_cast<uint16_t>(p);
    return h;
}

ImageLock::ImageLock(ImageLock&& o) noexcept
    : kind_(o.kind_), key_(std::move(o.key_)), held_(o.held_), hold_(std::move(o.hold_))
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
        kind_ = o.kind_;
        key_  = std::move(o.key_);
        held_ = o.held_;
        hold_ = std::move(o.hold_);
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
    hold_.reset();
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

#ifndef __EMSCRIPTEN__
namespace {
// The labelled body of a lock on `key` as this process would write it now.
std::string lock_body(ImageLock::Kind kind, const std::string& key)
{
    std::string b = "pid: " + std::to_string(this_pid()) + "\n" +
                    (kind == ImageLock::Kind::Writes ? "writes: " : "image: ") + key + "\n";
    const Endpoint e = endpoint_now();
    if (e.port != 0)
        b += "zrcp_listening_address: " + e.address + "\nzrcp_listening_port: " + std::to_string(e.port) + "\n";
    return b;
}
}  // namespace
#endif

void ImageLock::refresh_body()
{
#ifdef __EMSCRIPTEN__
    // a no-op
#else
    if (!held_) return;
    const std::string body = lock_body(kind_, key_);
#  ifdef _WIN32
    HANDLE h = static_cast<HANDLE>(handle_);
    if (!h) return;
    SetFilePointer(h, 0, nullptr, FILE_BEGIN);
    DWORD n = 0;
    WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &n, nullptr);
    SetEndOfFile(h);
#  else
    if (fd_ < 0) return;
    const ssize_t w = ::pwrite(fd_, body.data(), body.size(), 0);
    if (w == static_cast<ssize_t>(body.size())) { const int r = ::ftruncate(fd_, static_cast<off_t>(body.size())); (void)r; }
#  endif
#endif
}

std::optional<ImageLock> ImageLock::acquire(const std::string& path, Kind kind,
                                            std::string& why, const std::string& lock_dir,
                                            LockHolder* holder)
{
    ImageLock lk;
    lk.kind_ = kind;
    lk.key_ = canonical_key(path);
#ifdef __EMSCRIPTEN__
    (void)why; (void)lock_dir; (void)holder;
    lk.held_ = true;
    return lk;
#else
    const std::string file = lock_file_for(lk.key_, lock_dir);
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string body = lock_body(kind, lk.key_);
    auto refused = [&]() {
        const LockHolder h = parse_lock_body(read_lock_file(file));
        if (holder) *holder = h;
        why = message(kind, path, pid_text(h));
    };
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
        refused();
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
        refused();
        return std::nullopt;
    }
    if (::ftruncate(fd, 0) == 0) {
        const ssize_t w = ::write(fd, body.data(), body.size());
        (void)w;
    }
    lk.fd_ = fd;
#endif
    lk.held_ = true;
    lk.hold_ = held_files::Hold(file);
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
          std::optional<ImageLock>& out, bool& carried, std::string& why, LockHolder* holder)
{
    out.reset();
    carried = false;
    if (path.empty()) return true;
    if (have && have->key() == canonical_key(path)) { out = std::move(have); have.reset(); carried = true; return true; }
    out = ImageLock::acquire(path, kind, why, {}, holder);
    return out.has_value();
}
}  // namespace

void set_enabled(bool on)
{
    std::lock_guard<std::mutex> g(mu());
    g_enabled = on;
    if (!on) { slots()[0].image.reset(); slots()[0].writes.reset(); slots()[1].image.reset(); slots()[1].writes.reset(); }
}

bool hold(int slot, const std::string& image, const std::string& writes_file, std::string& why,
          LockHolder* holder)
{
    if (slot < 0 || slot > 1) { why = "no such SD card slot"; return false; }
    std::lock_guard<std::mutex> g(mu());
    if (!g_enabled) return true;
    SlotLocks& s = slots()[slot];
    std::optional<ImageLock> img, wr;
    bool img_carried = false, wr_carried = false;
    if (!take(s.image, image, ImageLock::Kind::Image, img, img_carried, why, holder)) return false;   // nothing moved
    if (!take(s.writes, writes_file, ImageLock::Kind::Writes, wr, wr_carried, why, holder)) {
        // A refusal changes nothing: an image lock carried out of the slot goes back to it, one taken
        // just now is dropped (it was never the slot's).
        if (img_carried) s.image = std::move(img);
        return false;
    }
    s.image  = std::move(img);    // the old locks, if not carried over, die here
    s.writes = std::move(wr);
    return true;
}

void set_zrcp_endpoint(const std::string& address, uint16_t port)
{
    std::lock_guard<std::mutex> g(mu());
    {
        std::lock_guard<std::mutex> e(ep_mu());
        ep().address = port ? address : std::string();
        ep().port = port;
    }
    for (int i = 0; i < 2; ++i) {
        if (slots()[i].image) slots()[i].image->refresh_body();
        if (slots()[i].writes) slots()[i].writes->refresh_body();
    }
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
