#include "joy_uart_link.h"

#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace {

#ifndef _WIN32
/// Ignore SIGPIPE once, the first time any endpoint is built.
///
/// A FIFO whose reader has gone away raises SIGPIPE on `write()`, and the
/// default disposition TERMINATES the process — so a DeZog session simply
/// closing its end would kill the emulator. Ignoring it turns the same event
/// into an `EPIPE` return, which is what the lazy-reopen path below is written
/// against. Installed lazily rather than at startup so a run with no serial
/// cable keeps the process's default signal disposition untouched.
void ignore_sigpipe_once() {
    static bool done = false;
    if (done) return;
    done = true;
    ::signal(SIGPIPE, SIG_IGN);
}

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}
#endif

} // namespace

// ══════════════════════════════════════════════════════════════════════
// JoyUartEndpoint
// ══════════════════════════════════════════════════════════════════════

JoyUartEndpoint::~JoyUartEndpoint() {
#ifndef _WIN32
    if (tx_fd_ >= 0 && !shared_fd_) ::close(tx_fd_);
    if (rx_fd_ >= 0)                ::close(rx_fd_);
#endif
}

#ifdef _WIN32

std::unique_ptr<JoyUartEndpoint> JoyUartEndpoint::open_fifo(const std::string& base,
                                                            std::string& error) {
    (void)base;
    error = "named-pipe serial cables are not supported on Windows";
    return nullptr;
}

std::unique_ptr<JoyUartEndpoint> JoyUartEndpoint::open_pty(std::string& error) {
    error = "pseudo-terminal serial cables are not supported on Windows";
    return nullptr;
}

void JoyUartEndpoint::open_tx_if_needed() {}

std::size_t JoyUartEndpoint::read(uint8_t*, std::size_t) { return 0; }
std::size_t JoyUartEndpoint::write(const uint8_t*, std::size_t) { return 0; }

#else   // POSIX

namespace {

/// Make sure `path` exists and is a FIFO. Creating it is a convenience that
/// matters: the two names are jnext's convention, so making the user mkfifo
/// them by hand is an invitation to create `<base>.rx` where jnext expects
/// `<base>.tx` and then debug a cable that is wired backwards.
///
/// An existing path that is NOT a fifo is refused rather than used: opening a
/// regular file there would succeed, read EOF forever and accept every write,
/// i.e. produce a run that looks like a working cable and talks to nobody.
bool ensure_fifo(const std::string& path, std::string& error) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            error = path + ": exists and is not a FIFO";
            return false;
        }
        return true;
    }
    if (errno != ENOENT) {
        error = path + ": " + std::strerror(errno);
        return false;
    }
    if (::mkfifo(path.c_str(), 0600) != 0) {
        error = path + ": mkfifo: " + std::strerror(errno);
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<JoyUartEndpoint> JoyUartEndpoint::open_fifo(const std::string& base,
                                                            std::string& error) {
    ignore_sigpipe_once();

    const std::string rx_path = base + ".rx";
    const std::string tx_path = base + ".tx";
    if (!ensure_fifo(rx_path, error)) return nullptr;
    if (!ensure_fifo(tx_path, error)) return nullptr;

    // O_RDONLY | O_NONBLOCK on a FIFO succeeds immediately even with no writer
    // — that is the property the whole design rests on. (O_WRONLY | O_NONBLOCK
    // does the opposite and fails with ENXIO, which is why the TX side is
    // opened lazily in open_tx_if_needed() instead of here.)
    const int rx_fd = ::open(rx_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (rx_fd < 0) {
        error = rx_path + ": " + std::strerror(errno);
        return nullptr;
    }

    std::unique_ptr<JoyUartEndpoint> ep(new JoyUartEndpoint());
    ep->rx_fd_       = rx_fd;
    ep->tx_path_     = tx_path;
    ep->description_ = "FIFO pair " + rx_path + " (host->Next) / " + tx_path + " (Next->host)";
    return ep;
}

std::unique_ptr<JoyUartEndpoint> JoyUartEndpoint::open_pty(std::string& error) {
    ignore_sigpipe_once();

    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) {
        error = std::string("posix_openpt: ") + std::strerror(errno);
        return nullptr;
    }
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        error = std::string("grantpt/unlockpt: ") + std::strerror(errno);
        ::close(master);
        return nullptr;
    }
    const char* slave = ::ptsname(master);
    if (slave == nullptr) {
        error = std::string("ptsname: ") + std::strerror(errno);
        ::close(master);
        return nullptr;
    }

    // RAW, and this is not optional. A pty's default line discipline echoes,
    // translates CR/LF and interprets ^C — every one of which corrupts a binary
    // debug protocol. The termios is shared by both ends of the pair, so
    // setting it on the master is what makes the slave a clean byte pipe. A
    // client that sets its own termios later (DeZog's serial layer does) only
    // re-confirms it.
    struct termios tio{};
    if (::tcgetattr(master, &tio) != 0) {
        error = std::string("tcgetattr: ") + std::strerror(errno);
        ::close(master);
        return nullptr;
    }
    ::cfmakeraw(&tio);
    if (::tcsetattr(master, TCSANOW, &tio) != 0) {
        error = std::string("tcsetattr: ") + std::strerror(errno);
        ::close(master);
        return nullptr;
    }
    if (!set_nonblocking(master)) {
        error = std::string("fcntl O_NONBLOCK: ") + std::strerror(errno);
        ::close(master);
        return nullptr;
    }

    std::unique_ptr<JoyUartEndpoint> ep(new JoyUartEndpoint());
    ep->rx_fd_       = master;
    ep->tx_fd_       = master;
    ep->shared_fd_   = true;
    ep->description_ = std::string("pty ") + slave;
    return ep;
}

void JoyUartEndpoint::open_tx_if_needed() {
    if (tx_fd_ >= 0 || tx_path_.empty()) return;
    // ENXIO here is the normal state, not a failure: it means no reader has
    // attached to the FIFO yet. Retried on every flush, so a peer that starts
    // late — or restarts — is picked up within one frame.
    const int fd = ::open(tx_path_.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;
    tx_fd_ = fd;
}

std::size_t JoyUartEndpoint::read(uint8_t* buf, std::size_t cap) {
    if (rx_fd_ < 0 || cap == 0) return 0;
    const ssize_t n = ::read(rx_fd_, buf, cap);
    if (n > 0) return static_cast<std::size_t>(n);
    // n == 0 is EOF: for a FIFO that is "no writer attached", which is a
    // perfectly ordinary state both before the peer starts and after it stops.
    // The descriptor is KEPT: it holds the pipe object alive, so the next
    // writer to open the path attaches to the same pipe and its bytes arrive
    // here without anything being re-opened.
    //
    // n < 0 with EAGAIN/EWOULDBLOCK means a writer is attached but silent.
    // EIO is the pty form of "no slave open right now". EINTR is a signal.
    // None of them is an error; everything else is recorded for the one-shot
    // report and the session continues, because a cable that has gone quiet
    // must not take the emulator down with it.
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EIO && errno != EINTR) {
        last_error_ = std::strerror(errno);
        ++faults_;
    }
    return 0;
}

std::size_t JoyUartEndpoint::write(const uint8_t* buf, std::size_t len) {
    if (len == 0) return 0;
    open_tx_if_needed();
    if (tx_fd_ < 0) return 0;          // nothing plugged in; caller counts the loss
    const ssize_t n = ::write(tx_fd_, buf, len);
    if (n > 0) return static_cast<std::size_t>(n);
    if (n < 0 && (errno == EPIPE || errno == EBADF)) {
        // The reader went away. Drop the descriptor and fall back to the lazy
        // open, so a peer that reconnects is picked up on a later flush. On a
        // pty the descriptor is shared with the read side, so it is kept —
        // a pty with no slave is not a broken pipe, it is an idle one.
        if (!shared_fd_) {
            ::close(tx_fd_);
            tx_fd_     = -1;
            peer_lost_ = true;     // the link discards the stale conversation
        }
        return 0;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        last_error_ = std::strerror(errno);
        ++faults_;
    }
    return 0;
}

#endif  // _WIN32

// ══════════════════════════════════════════════════════════════════════
// JoyUartLink
// ══════════════════════════════════════════════════════════════════════

JoyUartLink::JoyUartLink(std::unique_ptr<JoyUartEndpoint> endpoint, int connector)
    : endpoint_(std::move(endpoint)),
      connector_((connector != 0) ? 1 : 0) {}

JoyUartLink::~JoyUartLink() = default;

const std::string& JoyUartLink::describe() const { return endpoint_->describe(); }

std::size_t JoyUartLink::faults() const { return endpoint_->faults(); }
const std::string& JoyUartLink::last_error() const { return endpoint_->last_error(); }

void JoyUartLink::poll() {
    // THE REPLAY GATE for the descriptor half, and the only copy of it: the
    // owner raises `inert_` and leaves the enforcing to the three places that
    // actually touch something — here, `tick()` and `send_to_host()`. Reading
    // the endpoint during a replayed frame would CONSUME host bytes the
    // resumed timeline still needs; see the class comment.
    if (inert_) return;

    flush_tx();

    // Read until the endpoint has nothing more, or until the queue cap is
    // reached. Stopping at the cap rather than dropping is deliberate: the
    // unread bytes stay in the pipe, so the host's own write() blocks or
    // returns EAGAIN and the link degrades into backpressure instead of
    // silent truncation.
    uint8_t buf[4096];
    while (rx_queue_.size() < RX_QUEUE_MAX) {
        std::size_t want = RX_QUEUE_MAX - rx_queue_.size();
        if (want > sizeof(buf)) want = sizeof(buf);
        const std::size_t got = endpoint_->read(buf, want);
        if (got == 0) break;
        rx_queue_.insert(rx_queue_.end(), buf, buf + got);
        received_ += got;
    }
}

void JoyUartLink::tick(uint32_t master_cycles, uint32_t byte_ticks) {
    if (inert_ || !sink_ || rx_queue_.empty()) return;
    if (byte_ticks == 0) byte_ticks = 1;      // as UartChannel does for prescaler 0

    if (timer_ == 0) timer_ = byte_ticks;     // arm on the first tick with work

    // A single call can span more than one byte time — `master_cycles` is one
    // Z80 instruction (tens of ticks) and `byte_ticks` is prescaler *
    // frame_bits, which a guest may legally program down to ~10. So loop, and
    // carry the remainder into the next byte's timer rather than discarding it.
    uint32_t remaining = master_cycles;
    while (remaining >= timer_) {
        remaining -= timer_;
        timer_ = byte_ticks;
        const uint8_t byte = rx_queue_.front();
        rx_queue_.pop_front();
        sink_(byte);
        if (rx_queue_.empty()) return;        // timer_ stays armed for the next byte
    }
    timer_ -= remaining;
}

void JoyUartLink::send_to_host(uint8_t byte) {
    // A replayed frame re-transmits bytes the guest already sent once. Sending
    // them again would duplicate them at the peer, which is not recoverable —
    // the same argument as EspUartAdapter::set_inert, and the reason the whole
    // cable is gated rather than just the read side.
    if (inert_) return;

    if (tx_queue_.size() >= TX_QUEUE_MAX) {
        ++unsent_;                 // peer has stopped reading; drop newest
        return;
    }
    tx_queue_.push_back(byte);
    // Flushed immediately rather than at the frame seam: this is a
    // request/response debug protocol, and holding a reply for up to a frame
    // (20 ms) would add that to every round trip for no gain. The call rate is
    // bounded by the guest's own baud — `deliver_tx_byte` runs at byte
    // boundaries — so it is ~230 writes/frame at the default 115200.
    flush_tx();
}

void JoyUartLink::flush_tx() {
    while (!tx_queue_.empty()) {
        // One contiguous run at a time: a deque is not contiguous, so copy the
        // front span into a stack buffer. The buffer is sized so that a full
        // frame's traffic at any realistic baud leaves in one or two calls.
        uint8_t buf[512];
        std::size_t n = tx_queue_.size();
        if (n > sizeof(buf)) n = sizeof(buf);
        for (std::size_t i = 0; i < n; ++i) buf[i] = tx_queue_[i];
        const std::size_t wrote = endpoint_->write(buf, n);
        if (wrote == 0) {
            // A peer that had been there and left takes the conversation with
            // it: both queues hold half of an exchange with a process that has
            // exited, and giving that to whoever connects next is worse than
            // losing it. See flush_tx()'s declaration for the whole argument.
            if (endpoint_->take_peer_lost()) {
                unsent_ += tx_queue_.size();
                tx_queue_.clear();
                rx_queue_.clear();
                timer_ = 0;
            }
            return;                        // else: nothing plugged in, or peer is full
        }
        tx_queue_.erase(tx_queue_.begin(),
                        tx_queue_.begin() + static_cast<std::ptrdiff_t>(wrote));
        sent_ += wrote;
    }
}
