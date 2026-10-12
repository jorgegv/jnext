#include "audio/pi_audio.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

PiAudio::~PiAudio() { close(); }

uint16_t PiAudio::to_i2s(int16_t sample) {
    // -32768..32767 → 0..1023 with 0 → 0x200: shift into offset binary and
    // keep the top 10 bits, which is the hardware's resolution.
    return static_cast<uint16_t>((static_cast<int32_t>(sample) + 32768) >> 6);
}

void PiAudio::push(int16_t left, int16_t right) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {          // full: the emulator is not consuming
        dropped_.fetch_add(1, std::memory_order_relaxed);
        // What is in the ring is now older than the frame just lost; the
        // consumer flushes it when it next pops (see pop()).
        overflowed_.store(true, std::memory_order_release);
        return;
    }
    const uint32_t i = head % kCapacity;
    ring_[2 * i]     = left;
    ring_[2 * i + 1] = right;
    head_.store(head + 1, std::memory_order_release);
    received_.fetch_add(1, std::memory_order_relaxed);
}

bool PiAudio::pop(int16_t& left, int16_t& right) {
    const uint32_t head = head_.load(std::memory_order_acquire);
    uint32_t tail = tail_.load(std::memory_order_relaxed);
    uint32_t available = head - tail;

    // The ring overflowed while nothing consumed (a pause longer than the
    // ring): everything in it predates the frames that were dropped, so it is
    // flushed, and playback resumes on fresh audio after the prebuffer.
    // Trimming it instead would play a stale moment from deep in the pause.
    if (overflowed_.exchange(false, std::memory_order_acq_rel)) {
        dropped_.fetch_add(available, std::memory_order_relaxed);
        tail += available;
        tail_.store(tail, std::memory_order_release);
        available = 0;
        playing_ = false;
        return false;
    }

    if (!playing_) {
        if (available < kPrebuffer) return false;      // still filling
        playing_ = true;
    }
    if (available == 0) {                              // ran dry: silence, then refill
        playing_ = false;
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (available > kMaxLatency) {                     // built up: drop the oldest
        const uint32_t skip = available - kTarget;
        tail += skip;
        available -= skip;
        dropped_.fetch_add(skip, std::memory_order_relaxed);
    }
    const uint32_t i = tail % kCapacity;
    left  = ring_[2 * i];
    right = ring_[2 * i + 1];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

// The stream parser both transports share. Each time a writer opens the pipe,
// its stream starts with QEMU's 44-byte WAV header; after that, interleaved
// little-endian int16 L/R. A read can split a frame, so leftover bytes carry
// over to the next one.
void PiAudio::writer_gone(Stream& st) {
    // The next writer starts a fresh stream, header included.
    st.header_left = kHeader;
    st.carry.clear();
}

void PiAudio::consume(const uint8_t* data, std::size_t got, Stream& st) {
    std::size_t& header_left = st.header_left;
    std::vector<uint8_t>& carry = st.carry;
    std::size_t off = 0;
    if (header_left > 0) {
        const std::size_t skip = std::min(header_left, got);
        header_left -= skip;
        off = skip;
    }
    carry.insert(carry.end(), data + off, data + got);
    // A new writer can follow the last one with no read in between (QEMU
    // restarted at once), so its header is not always preceded by the writer-gone reset
    // (an EOF read): it is also recognised in-stream, at a frame boundary, by
    // "RIFF" ... "WAVE". Bytes that could still turn out to be one wait in
    // `carry` for the next read; real audio would need three consecutive
    // frames spelling those letters to be mistaken for a header.
    std::size_t pos = 0;
    while (header_left == 0 && carry.size() - pos >= 4) {
        const std::size_t rest = carry.size() - pos;
        const uint8_t* b = &carry[pos];
        if (std::memcmp(b, "RIFF", 4) == 0) {
            if (rest < 12) break;                    // wait: maybe a header
            if (std::memcmp(b + 8, "WAVE", 4) == 0) {
                const std::size_t skip = std::min(kHeader, rest);
                pos += skip;
                header_left = kHeader - skip;        // the rest on the next read
                continue;
            }
        }
        push(static_cast<int16_t>(b[0] | (b[1] << 8)), static_cast<int16_t>(b[2] | (b[3] << 8)));
        pos += 4;
    }
    carry.erase(carry.begin(), carry.begin() + static_cast<std::ptrdiff_t>(pos));
}

// The transport is per OS: the FIFO below, and on Windows a named pipe in
// pi_audio_win.cpp (the only TU of this library that includes <windows.h>).
#ifndef _WIN32

bool PiAudio::open(const std::string& fifo_path, std::string& error) {
    close();
    struct stat st{};
    if (::stat(fifo_path.c_str(), &st) != 0) {
        if (::mkfifo(fifo_path.c_str(), 0600) != 0) {
            error = fifo_path + ": mkfifo: " + std::strerror(errno);
            return false;
        }
    } else if (!S_ISFIFO(st.st_mode)) {
        error = fifo_path + ": exists and is not a FIFO";
        return false;
    }
    // O_NONBLOCK so the open does not wait for a writer: QEMU opens its end
    // later, and its own open() would block until a reader exists — this one.
    fd_ = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        error = fifo_path + ": " + std::strerror(errno);
        return false;
    }
    stop_ = false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void PiAudio::close() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void PiAudio::run() {
    // Each time a writer opens the FIFO, its stream starts with QEMU's 44-byte
    // WAV header; after that, interleaved little-endian int16 L/R. A read can
    // split a frame, so leftover bytes carry over to the next one.
    Stream st;
    std::vector<uint8_t> buf(1 << 16);
    bool no_writer = true;   // until a read says otherwise (data, or EAGAIN)
    while (!stop_) {
        pollfd p{fd_, POLLIN, 0};
        // Wake up for stop_ at least every 100 ms. On a timeout read anyway:
        // macOS's poll() does not report a FIFO whose writer has gone, and
        // only the non-blocking read's EOF (0, versus EAGAIN while a writer is
        // still there) tells the stream ended, so the next one's header is
        // skipped and a half frame of the old one is not glued to it.
        // With no writer the timeout is 20 ms: a poll() that began before a
        // new writer connected is not always woken by its data (a macOS CI
        // runner took the pause plus a whole 100 ms timeout to deliver a
        // reconnecting writer's first frame), and 20 ms bounds that wait.
        if (::poll(&p, 1, no_writer ? 20 : 100) < 0) continue;
        const ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            // No writer: QEMU has not opened its end yet, or closed it. The
            // next writer starts a fresh stream, header included.
            writer_gone(st);
            no_writer = true;
            eof_reads_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (n > 0 || errno == EAGAIN) no_writer = false;   // a writer is there
        if (n < 0) {
            if (errno == EAGAIN) idle_reads_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        consume(buf.data(), static_cast<std::size_t>(n), st);
    }
}

#endif
