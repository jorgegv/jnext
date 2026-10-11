#pragma once

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
class StreamDigest {
public:
    static constexpr std::uint64_t kInterval = 256;

    /// Feed one byte; true when it completes a checkpoint.
    bool feed(std::uint8_t b) {
        hash_ = (hash_ ^ b) * 16777619u;
        return (++off_ % kInterval) == 0;
    }
    std::uint64_t offset() const { return off_; }
    std::uint32_t hash() const { return hash_; }
    /// Eight lower-case hex digits.
    std::string hash_hex() const {
        static const char* d = "0123456789abcdef";
        std::string s(8, '0');
        for (int i = 0; i < 8; ++i) s[7 - i] = d[(hash_ >> (4 * i)) & 0xF];
        return s;
    }

private:
    std::uint32_t hash_ = 2166136261u;
    std::uint64_t off_ = 0;
};

}  // namespace esp
