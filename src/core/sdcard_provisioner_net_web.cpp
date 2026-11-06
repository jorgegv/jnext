// WebAssembly (Emscripten) backend of the SD-card provisioner: no download (a
// browser build never downloads the SD image itself; the page supplies it) and
// a portable SHA-256 (FIPS 180-4), because there is no OpenSSL and no BCrypt.
//
// It implements the same four functions as the POSIX and Windows twins
// (sdcard_provisioner_net_curl.cpp, sdcard_provisioner_net_win.cpp):
// http_download, transport_reason, sha256_hex, sha256_file, with the contracts
// documented in sdcard_provisioner.h and sha256.h: a download that fails says so
// with a bare single-prefix reason, sha256_* return lowercase hex and "" on
// error (an unreadable path, and a directory, JNSI-P26b). The whole-file #ifdef
// keeps file(GLOB_RECURSE) in src/core/CMakeLists.txt correct on every
// platform: exactly one of the three twins has content per build.
#ifdef __EMSCRIPTEN__

#include "core/sdcard_provisioner.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sdcard {

namespace {

// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the
// cube roots of the first 64 primes.
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
public:
    void update(const uint8_t* p, size_t len) {
        total_ += len;
        while (len) {
            const size_t k = std::min(len, sizeof buf_ - fill_);
            std::memcpy(buf_ + fill_, p, k);
            fill_ += k;
            p += k;
            len -= k;
            if (fill_ == sizeof buf_) {
                block(buf_);
                fill_ = 0;
            }
        }
    }

    // Pads (0x80, zeros to 56 mod 64, the 64-bit BIT length big-endian) and
    // returns the digest as lowercase hex.
    std::string hex() {
        const uint64_t bits = total_ * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (fill_ != 56) update(&zero, 1);
        uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        update(len_be, 8);
        static const char digits[] = "0123456789abcdef";
        std::string out;
        for (uint32_t v : h_)
            for (int s = 28; s >= 0; s -= 4) out.push_back(digits[(v >> s) & 15]);
        return out;
    }

private:
    void block(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 |
                   uint32_t(p[4 * i + 2]) << 8 | uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    // FIPS 180-4 section 5.3.3: the initial hash value.
    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t  buf_[64];
    size_t   fill_ = 0;
    uint64_t total_ = 0;
};

}  // namespace

bool http_download(const std::string&, const std::string&, const ProgressFn&,
                   std::string& err, const DownloadTimings&) {
    err = "downloads are not available in the browser build";
    return false;
}

std::string transport_reason(long) {
    return "no network access in the browser build";
}

std::string sha256_hex(const std::vector<uint8_t>& bytes) {
    Sha256 s;
    s.update(bytes.data(), bytes.size());
    return s.hex();
}

std::string sha256_file(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return {};   // JNSI-P26b
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    Sha256 s;
    std::vector<uint8_t> buf(1 << 20);
    for (;;) {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) s.update(buf.data(), static_cast<size_t>(n));
        if (f.bad()) return {};
        if (f.eof()) break;
        if (f.fail()) return {};
    }
    return s.hex();
}

}  // namespace sdcard

#endif  // __EMSCRIPTEN__
