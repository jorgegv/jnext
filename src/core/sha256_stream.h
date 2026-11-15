#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sdcard {

// Incremental SHA-256 (FIPS 180-4), portable and compiled on every platform. It is
// what the browser build's sha256_hex/sha256_file use (there is no OpenSSL or BCrypt
// there) and what core/minimal_card.cpp hashes the streamed distribution with. The
// curl and win backends keep OpenSSL/BCrypt for their one-shot helpers.
class Sha256Stream {
public:
    void update(const uint8_t* p, size_t len);
    // Pads (0x80, zeros to 56 mod 64, the 64-bit BIT length big-endian) and returns the
    // digest as lowercase hex. The object is finished: do not update it afterwards.
    std::string final_hex();

private:
    void block(const uint8_t* p);
    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t  buf_[64];
    size_t   fill_ = 0;
    uint64_t total_ = 0;
};

}  // namespace sdcard
