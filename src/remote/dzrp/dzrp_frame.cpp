#include "remote/dzrp/dzrp_frame.h"

#include <algorithm>

namespace jnext {
namespace remote {
namespace dzrp {

std::size_t FrameParser::wanted() const {
    if (ready_ || failed()) return 0;
    if (have_ < COMMAND_HEADER_BYTES) return COMMAND_HEADER_BYTES - have_;
    return COMMAND_HEADER_BYTES + length_ - have_;
}

std::size_t FrameParser::feed(const std::uint8_t* data, std::size_t n, TimePoint now) {
    std::size_t taken = 0;
    while (taken < n && wanted() > 0) {
        if (have_ < COMMAND_HEADER_BYTES) {
            header_[have_++] = data[taken++];
            if (have_ == 4) {
                length_ = static_cast<std::uint32_t>(header_[0]) |
                          (static_cast<std::uint32_t>(header_[1]) << 8) |
                          (static_cast<std::uint32_t>(header_[2]) << 16) |
                          (static_cast<std::uint32_t>(header_[3]) << 24);
                // Refused BEFORE a byte of it is read: the length is the
                // client's claim, and buffering it first is how a desynced or
                // hostile stream becomes a host out of memory.
                if (length_ > MAX_PAYLOAD_BYTES) {
                    error_ = "frame length " + std::to_string(length_) +
                             " exceeds the " + std::to_string(MAX_PAYLOAD_BYTES) +
                             "-byte cap";
                    break;
                }
            } else if (have_ == 5 && header_[4] == 0) {
                error_ = "sequence number 0 (reserved for notifications)";
                break;
            } else if (have_ == COMMAND_HEADER_BYTES) {
                cmd_.seq = header_[4];
                cmd_.id  = header_[5];
                cmd_.payload.clear();
                cmd_.payload.reserve(length_);
            }
        } else {
            const std::size_t k = std::min(n - taken, wanted());
            cmd_.payload.insert(cmd_.payload.end(), data + taken, data + taken + k);
            taken += k;
            have_ += k;
        }
        if (have_ == COMMAND_HEADER_BYTES + length_) ready_ = true;
    }
    if (taken > 0) last_progress_ = now;
    return taken;
}

Command FrameParser::take() {
    Command out = std::move(cmd_);
    cmd_    = Command{};
    have_   = 0;
    length_ = 0;
    ready_  = false;
    return out;
}

bool FrameParser::stalled(TimePoint now) const {
    return partial() && now - last_progress_ >= CHUNK_TIMEOUT;
}

void FrameParser::reset() {
    have_   = 0;
    length_ = 0;
    ready_  = false;
    error_.clear();
    cmd_ = Command{};
}

std::vector<std::uint8_t> encode_command(std::uint8_t seq, std::uint8_t id,
                                         const std::uint8_t* payload, std::size_t n) {
    if (seq == 0 || n > MAX_PAYLOAD_BYTES) return {};
    // The command convention: the length counts the PAYLOAD ONLY.
    const std::uint32_t len = static_cast<std::uint32_t>(n);
    std::vector<std::uint8_t> out;
    out.reserve(COMMAND_HEADER_BYTES + n);
    out.push_back(static_cast<std::uint8_t>(len));
    out.push_back(static_cast<std::uint8_t>(len >> 8));
    out.push_back(static_cast<std::uint8_t>(len >> 16));
    out.push_back(static_cast<std::uint8_t>(len >> 24));
    out.push_back(seq);
    out.push_back(id);
    if (n > 0) out.insert(out.end(), payload, payload + n);
    return out;
}

std::vector<std::uint8_t> encode_command(std::uint8_t seq, std::uint8_t id,
                                         const std::vector<std::uint8_t>& payload) {
    return encode_command(seq, id, payload.data(), payload.size());
}

// ── ResponseParser ─────────────────────────────────────────────────────────

constexpr std::uint8_t PREAMBLE_MARKER = 0xA5;

std::size_t ResponseParser::wanted() const {
    if (ready_ || failed()) return 0;
    if (hunting_) return 1;
    if (hdr_have_ < 4) return 4 - hdr_have_;
    return length_ - body_have_;
}

std::size_t ResponseParser::feed(const std::uint8_t* data, std::size_t n, TimePoint now) {
    std::size_t taken = 0;
    while (taken < n && wanted() > 0) {
        if (hunting_) {
            const std::uint8_t b = data[taken++];
            if (b == PREAMBLE_MARKER) {
                hunting_ = false;
                started_ = true;
            } else {
                ++swallowed_;
            }
            continue;
        }
        started_ = true;
        if (hdr_have_ < 4) {
            hdr_[hdr_have_++] = data[taken++];
            if (hdr_have_ == 4) {
                length_ = static_cast<std::uint32_t>(hdr_[0]) |
                          (static_cast<std::uint32_t>(hdr_[1]) << 8) |
                          (static_cast<std::uint32_t>(hdr_[2]) << 16) |
                          (static_cast<std::uint32_t>(hdr_[3]) << 24);
                // Refused BEFORE a byte of the body is read (the remote's claim).
                if (length_ == 0) {
                    error_ = "response length 0 (no sequence byte)";
                    break;
                }
                if (length_ > MAX_PAYLOAD_BYTES) {
                    error_ = "response length " + std::to_string(length_) + " exceeds the " +
                             std::to_string(MAX_PAYLOAD_BYTES) + "-byte cap";
                    break;
                }
                resp_.payload.clear();
                resp_.payload.reserve(length_ - 1);
            }
        } else {
            if (body_have_ == 0) {
                resp_.seq = data[taken++];
                body_have_ = 1;
                if (resp_.seq == 0 && length_ == 1) {
                    error_ = "notification without an id";
                    break;
                }
            } else {
                const std::size_t k = std::min(n - taken, wanted());
                resp_.payload.insert(resp_.payload.end(), data + taken, data + taken + k);
                taken += k;
                body_have_ += k;
            }
        }
        if (hdr_have_ == 4 && body_have_ == length_) ready_ = true;
    }
    if (taken > 0) last_progress_ = now;
    return taken;
}

void ResponseParser::start_next() {
    hdr_have_  = 0;
    length_    = 0;
    body_have_ = 0;
    ready_     = false;
    started_   = false;
    hunting_   = opt_.preamble;
}

Response ResponseParser::take() {
    Response out = std::move(resp_);
    resp_        = Response{};
    start_next();
    return out;
}

bool ResponseParser::stalled(TimePoint now) const {
    return partial() && now - last_progress_ >= CHUNK_TIMEOUT;
}

void ResponseParser::reset() {
    start_next();
    error_.clear();
    resp_      = Response{};
    swallowed_ = 0;
}

std::vector<std::uint8_t> encode_response(std::uint8_t seq, const std::uint8_t* payload,
                                          std::size_t n) {
    // The response convention: the length counts FROM THE SEQ BYTE.
    const std::uint32_t len = static_cast<std::uint32_t>(n + 1);
    std::vector<std::uint8_t> out;
    out.reserve(5 + n);
    out.push_back(static_cast<std::uint8_t>(len));
    out.push_back(static_cast<std::uint8_t>(len >> 8));
    out.push_back(static_cast<std::uint8_t>(len >> 16));
    out.push_back(static_cast<std::uint8_t>(len >> 24));
    out.push_back(seq);
    if (n > 0) out.insert(out.end(), payload, payload + n);
    return out;
}

std::vector<std::uint8_t> encode_response(std::uint8_t                     seq,
                                          const std::vector<std::uint8_t>& payload) {
    return encode_response(seq, payload.data(), payload.size());
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
