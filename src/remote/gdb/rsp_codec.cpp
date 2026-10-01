#include "remote/gdb/rsp_codec.h"

namespace jnext {
namespace remote {
namespace gdb {

namespace {

bool reserved(char c) { return c == '#' || c == '$' || c == '}' || c == '*'; }

const char kHex[] = "0123456789abcdef";

}  // namespace

std::uint8_t checksum(const std::string& body) {
    unsigned sum = 0;
    for (char c : body) sum += static_cast<unsigned char>(c);
    return static_cast<std::uint8_t>(sum & 0xFF);
}

std::string escape_binary(const std::string& data) {
    std::string out;
    out.reserve(data.size());
    for (char c : data) {
        if (reserved(c)) {
            out.push_back('}');
            out.push_back(static_cast<char>(c ^ 0x20));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool unescape_binary(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '}') {
            out.push_back(in[i]);
            continue;
        }
        if (++i == in.size()) return false;
        out.push_back(static_cast<char>(in[i] ^ 0x20));
    }
    return true;
}

std::string frame_packet(const std::string& body) {
    const std::string esc = escape_binary(body);
    const std::uint8_t sum = checksum(esc);
    std::string out;
    out.reserve(esc.size() + 4);
    out.push_back('$');
    out += esc;
    out.push_back('#');
    out.push_back(kHex[sum >> 4]);
    out.push_back(kHex[sum & 0x0F]);
    return out;
}

std::string to_hex(const std::uint8_t* data, std::size_t n) {
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

std::string to_hex(const std::string& bytes) {
    return to_hex(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool from_hex(const std::string& hex, std::string& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_digit(hex[i]);
        const int lo = hex_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool parse_hex_u32(const std::string& s, std::uint32_t& out) {
    if (s.empty() || s.size() > 8) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        const int d = hex_digit(c);
        if (d < 0) return false;
        v = (v << 4) | static_cast<std::uint32_t>(d);
    }
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// RspParser
// ---------------------------------------------------------------------------

void RspParser::feed(const std::uint8_t* data, std::size_t n) {
    // Compact first: what `next` consumed is gone, so the buffer holds only
    // bytes not yet parsed.
    if (pos_ > 0) {
        in_.erase(0, pos_);
        pos_ = 0;
    }
    in_.append(reinterpret_cast<const char*>(data), n);
}

void RspParser::reset() {
    in_.clear();
    pos_      = 0;
    state_    = State::Idle;
    body_.clear();
    oversize_ = false;
    sum_hi_   = 0;
    noise_    = 0;
}

bool RspParser::next(RspEvent& ev) {
    while (pos_ < in_.size()) {
        const char c = in_[pos_++];
        switch (state_) {
            case State::Idle:
                if (c == '$') {
                    state_    = State::Body;
                    body_.clear();
                    oversize_ = false;
                } else if (static_cast<std::uint8_t>(c) == INTERRUPT_BYTE) {
                    ev.kind = RspEvent::Kind::Interrupt;
                    ev.body.clear();
                    return true;
                } else if (c != '+' && c != '-') {
                    ++noise_;  // resynchronise on the next `$`
                }
                break;
            case State::Body:
                if (c == '#') {
                    state_ = State::Sum1;
                } else if (c == '$') {
                    // A `$` cannot occur unescaped inside a body: the packet in
                    // progress was cut off. Drop it and start over here.
                    noise_ += body_.size() + 1;
                    body_.clear();
                    oversize_ = false;
                } else if (body_.size() < max_body_) {
                    body_.push_back(c);
                } else {
                    oversize_ = true;  // keep consuming to the `#`; keep nothing
                }
                break;
            case State::Sum1:
                sum_hi_ = c;
                state_  = State::Sum2;
                break;
            case State::Sum2: {
                state_ = State::Idle;
                if (oversize_) {
                    ev.kind = RspEvent::Kind::Oversize;
                    ev.body.clear();
                    body_.clear();
                    oversize_ = false;
                    return true;
                }
                const int hi = hex_digit(sum_hi_);
                const int lo = hex_digit(c);
                const bool good =
                    hi >= 0 && lo >= 0 && ((hi << 4) | lo) == checksum(body_);
                ev.kind = good ? RspEvent::Kind::Packet : RspEvent::Kind::BadChecksum;
                ev.body.swap(body_);
                body_.clear();
                return true;
            }
        }
    }
    return false;
}

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
