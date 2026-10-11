#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace esp {

/// A running digest of a byte STREAM with a checkpoint every `kInterval` bytes
/// (GH #319). FNV-1a/32: not cryptographic, and not meant to be; it only has to
/// tell "the same bytes" from "a byte changed, lost or added", cheaply.
///
/// WHY. nextsync-func under wine once failed because the guest REJECTED a reply
/// jnext had received and framed without a log line out of place. Which hop
/// damaged it (socket, the ESP's receive buffer, its +IPD output, the UART's RX
/// FIFO, the guest's read) could not be told, because nothing recorded the BYTES
/// at any hop. Each hop feeds its own stream into one of these and logs a
/// `digest <name> off=<n> fnv=<h>` line at every checkpoint (debug level, only
/// when that level is wanted); two hops that should carry the same bytes carry
/// the same line, and the first checkpoint where they differ names the hop.
/// Checking costs a log line per 256 bytes, ~90 lines per stream for the 16 KB
/// NextSync transfer, and nothing at all unless the debug level is on.
///
/// CHECKPOINTS HAVE A BLIND SPOT -- the last < 256 bytes of a stream -- so the
/// callers also log an EXACT record (`digest-exact <stream> off=<n> fnv=<h>`) at
/// the moments that matter: the end of each frame, when the ESP's output queue
/// drains, and when the guest starts a command (with the bytes still queued).
///
/// SCOPE. One digest per hop, process-global in the ESP module (and keyed by UART
/// channel in uart.cpp): it describes ONE connection from power-on, and is
/// meaningless across CIPMUX connections or a hard reset mid-transfer. Written by
/// one thread per hop; the fields are relaxed atomics only so another thread's
/// exact-record read is not a data race.
class StreamDigest {
public:
    static constexpr std::uint64_t kInterval = 256;

    /// Feed one byte; true when it completes a checkpoint.
    bool feed(std::uint8_t b) {
        const std::uint32_t h = (hash_.load(std::memory_order_relaxed) ^ b) * 16777619u;
        const std::uint64_t o = off_.load(std::memory_order_relaxed) + 1;
        hash_.store(h, std::memory_order_relaxed);
        off_.store(o, std::memory_order_relaxed);
        return (o % kInterval) == 0;
    }
    std::uint64_t offset() const { return off_.load(std::memory_order_relaxed); }
    std::uint32_t hash() const { return hash_.load(std::memory_order_relaxed); }
    /// Eight lower-case hex digits.
    std::string hash_hex() const {
        static const char* d = "0123456789abcdef";
        std::string s(8, '0');
        const std::uint32_t h = hash();
        for (int i = 0; i < 8; ++i) s[7 - i] = d[(h >> (4 * i)) & 0xF];
        return s;
    }

private:
    std::atomic<std::uint32_t> hash_{2166136261u};
    std::atomic<std::uint64_t> off_{0};
};

}  // namespace esp
