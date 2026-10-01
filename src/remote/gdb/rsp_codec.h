#pragma once

// ---------------------------------------------------------------------------
// The GDB Remote Serial Protocol's wire format (GH #281, epic #276 package G,
// WP-1). PURE: no backend, no socket, no emulator — bytes in, packets out.
//
// Design: doc/design/debug-subsystem/gdb-rsp-frontend.md §1.2 (what the real
// client, `z88dk-gdb`, requires of the wire) and §6.1 (the framing layer).
//
// ── THE WIRE ────────────────────────────────────────────────────────────────
//
//   packet     `$` body `#` xx   — xx = two hex digits, the sum of the body's
//                                  bytes mod 256 (the body AS SENT, escapes
//                                  included)
//   interrupt  one raw 0x03 byte OUTSIDE a packet (Ctrl-C). Inside a packet a
//              0x03 is data: `X` carries raw binary.
//   acks       `+` / `-` outside a packet. The server sends `+` for every good
//              packet and `-` for a bad or oversize one; it never waits for
//              one and never retransmits — `z88dk-gdb` never sends an ack and
//              never retransmits either (§1.2).
//   escape     `}` then the byte XOR 0x20. `X`'s binary data uses it for
//              `#`, `$`, `}` and `*`; a reply's binary data (`qXfer`) must use
//              it for the same four, `*` included because a real gdb reads a
//              raw `*` in a REPLY as run-length encoding. The server never
//              emits run-length encoding: `z88dk-gdb` does not decode it.
//
// Anything outside a packet that is none of the above is noise, counted and
// dropped: the parser resynchronises on the next `$`.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jnext {
namespace remote {
namespace gdb {

/// The largest packet BODY the server accepts, in bytes as sent. `qSupported`
/// advertises `PacketSize=4000`, which `z88dk-gdb` reads as DECIMAL (4000:
/// its `M` chunk is then (4000-16)/2 bytes) and a real gdb reads as HEX
/// (16384) — this bound holds either reading. A longer packet is dropped and
/// answered `-` (§6.1).
constexpr std::size_t MAX_PACKET_BODY = 16384;

/// The `PacketSize` value `qSupported` advertises. Decimal digits only, so the
/// two client families (decimal `sscanf` / hex `strtol`) both read a size the
/// server can take (§1.2).
constexpr char PACKET_SIZE_FIELD[] = "4000";

/// The Ctrl-C byte.
constexpr std::uint8_t INTERRUPT_BYTE = 0x03;

/// Sum of `body`'s bytes, mod 256.
std::uint8_t checksum(const std::string& body);

/// `}`-escape the four bytes RSP reserves: `#` `$` `}` `*`.
std::string escape_binary(const std::string& data);

/// Undo `escape_binary`. False — `out` unspecified — when the input ends in a
/// lone `}`.
bool unescape_binary(const std::string& in, std::string& out);

/// A complete packet: `$` + escaped body + `#` + checksum. Escaping is applied
/// unconditionally: a text reply contains none of the four bytes, so it is
/// unchanged, and a binary one (`qXfer`) is then always well formed.
std::string frame_packet(const std::string& body);

/// Lower-case hex, two digits per byte.
std::string to_hex(const std::uint8_t* data, std::size_t n);
std::string to_hex(const std::string& bytes);

/// The value of one hex digit, either case; -1 if it is not one.
int hex_digit(char c);

/// Decode an even-length hex string. False on an odd length or a non-hex
/// digit.
bool from_hex(const std::string& hex, std::string& out);

/// Parse 1..8 hex digits, and nothing else, as an unsigned value. False on an
/// empty string, a non-hex digit or more than 8 digits (no silent overflow).
bool parse_hex_u32(const std::string& s, std::uint32_t& out);

/// What the parser found.
struct RspEvent {
    enum class Kind : std::uint8_t {
        /// A well-formed packet; `body` is its payload AS SENT (escapes
        /// intact: only `X` decodes them, and only in its data part).
        Packet,
        /// A 0x03 outside a packet.
        Interrupt,
        /// A packet whose checksum did not match (or was not hex). Answer `-`.
        BadChecksum,
        /// A packet longer than the parser's limit; its body was dropped as
        /// it arrived. Answer `-`.
        Oversize,
    };
    Kind        kind = Kind::Packet;
    std::string body;
};

/// Incremental packet parser. `feed` appends raw bytes; `next` consumes them
/// until it has ONE event, so a caller can execute one command and leave the
/// rest waiting. Memory is bounded by the body limit plus what the caller
/// feeds: an oversize body is discarded as it arrives, never buffered.
class RspParser {
public:
    explicit RspParser(std::size_t max_body = MAX_PACKET_BODY) : max_body_(max_body) {}

    void feed(const std::uint8_t* data, std::size_t n);

    /// The next event, or false once every fed byte is consumed without one.
    bool next(RspEvent& ev);

    /// Fed bytes not consumed yet.
    std::size_t pending() const { return in_.size() - pos_; }

    /// Bytes dropped outside any packet (acks not counted), since the last
    /// `reset`.
    std::size_t noise() const { return noise_; }

    /// Forget everything: a new connection starts clean.
    void reset();

private:
    enum class State : std::uint8_t { Idle, Body, Sum1, Sum2 };

    std::size_t max_body_;
    std::string in_;
    std::size_t pos_  = 0;
    State       state_ = State::Idle;
    std::string body_;
    bool        oversize_ = false;
    char        sum_hi_   = 0;
    std::size_t noise_    = 0;
};

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
