#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sdcard {

// SHA256 helpers (OpenSSL EVP on Linux/macOS, BCrypt/CNG on Windows, in the
// per-platform sdcard_provisioner_net_* backends). `sha256_hex` digests an
// in-memory buffer; `sha256_file` digests a whole file. Both return a
// lowercase hex string ("" on error, e.g. the file cannot be read). Here, apart
// from the provisioner, so code that only hashes need not include it.
std::string sha256_hex(const std::vector<uint8_t>& bytes);
std::string sha256_file(const std::string& path);

}  // namespace sdcard
