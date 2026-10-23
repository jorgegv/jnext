#include "save/zip_interchange.h"

#include "save/zip_archive.h"

#include <algorithm>
#include <zlib.h>

namespace jnext {
namespace zip {

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t kSigEocd    = 0x06054b50;
constexpr uint32_t kSigLocator = 0x07064b50;   // ZIP64 EOCD locator
constexpr uint32_t kSigCentral = 0x02014b50;
constexpr uint32_t kSigLocal   = 0x04034b50;
constexpr size_t   kEocdLen    = 22;
constexpr size_t   kCentralLen = 46;
constexpr size_t   kLocalLen   = 30;

std::string method_name(uint16_t m) {
    switch (m) {
        case 1:  return "shrink";
        case 6:  return "implode";
        case 9:  return "deflate64";
        case 12: return "bzip2";
        case 14: return "LZMA";
        case 93: return "zstd";
        case 95: return "xz";
        case 98: return "PPMd";
        case 99: return "AES";
        default: return "";
    }
}

}  // namespace

bool InterchangeReader::open(const uint8_t* data, size_t len, std::string& why) {
    entries_.clear();
    data_ = nullptr;
    len_ = 0;

    if (len < kEocdLen) {
        why = "not a ZIP archive (no end-of-central-directory record)";
        return false;
    }
    // The EOCD ends exactly at the end of the buffer: scan back over at most a
    // 65535-byte comment for the signature whose comment length fits exactly.
    size_t eocd = static_cast<size_t>(-1);
    const size_t lo = len - kEocdLen > 65535 ? len - kEocdLen - 65535 : 0;
    for (size_t p = len - kEocdLen + 1; p-- > lo;) {
        if (rd32(data + p) == kSigEocd &&
            p + kEocdLen + rd16(data + p + 20) == len) {
            eocd = p;
            break;
        }
    }
    if (eocd == static_cast<size_t>(-1)) {
        why = "not a ZIP archive (no end-of-central-directory record)";
        return false;
    }
    const uint8_t* e = data + eocd;
    const uint16_t disk = rd16(e + 4), cd_disk = rd16(e + 6);
    const uint16_t n_here = rd16(e + 8), n_total = rd16(e + 10);
    const uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);

    if (n_total == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu ||
        (eocd >= 20 && rd32(data + eocd - 20) == kSigLocator)) {
        why = "the archive uses ZIP64, which JNEXT does not read";
        return false;
    }
    if (disk != 0 || cd_disk != 0 || n_here != n_total) {
        why = "the archive spans several disks (multi-disk), which JNEXT does not read";
        return false;
    }
    if (static_cast<uint64_t>(cd_off) + cd_size > eocd) {
        why = "the archive is damaged (its central directory lies outside the file)";
        return false;
    }

    std::vector<InterchangeEntry> out;
    size_t pos = cd_off;
    const size_t end = static_cast<size_t>(cd_off) + cd_size;
    for (unsigned i = 0; i < n_total; ++i) {
        if (end - pos < kCentralLen || rd32(data + pos) != kSigCentral) {
            why = "the archive is damaged (central directory entry " +
                  std::to_string(i + 1) + " is truncated or has a bad signature)";
            return false;
        }
        const uint8_t* c = data + pos;
        const size_t nlen = rd16(c + 28), elen = rd16(c + 30), clen = rd16(c + 32);
        if (end - pos - kCentralLen < nlen + elen + clen) {
            why = "the archive is damaged (central directory entry " +
                  std::to_string(i + 1) + " overruns the directory)";
            return false;
        }
        InterchangeEntry en;
        en.flags = rd16(c + 8);
        en.method = rd16(c + 10);
        en.crc32 = rd32(c + 16);
        en.comp_size = rd32(c + 20);
        en.uncomp_size = rd32(c + 24);
        en.local_offset = rd32(c + 42);
        en.central_extra_len = static_cast<uint16_t>(elen);
        en.name.assign(reinterpret_cast<const char*>(c + kCentralLen), nlen);
        if (en.comp_size == 0xFFFFFFFFu || en.uncomp_size == 0xFFFFFFFFu ||
            en.local_offset == 0xFFFFFFFFu) {
            why = "the archive uses ZIP64, which JNEXT does not read";
            return false;
        }
        if (rd16(c + 34) != 0) {
            why = "the archive spans several disks (multi-disk), which JNEXT does not read";
            return false;
        }
        out.push_back(std::move(en));
        pos += kCentralLen + nlen + elen + clen;
    }
    if (pos != end) {
        why = "the archive is damaged (its central directory disagrees with its entry count)";
        return false;
    }
    entries_ = std::move(out);
    data_ = data;
    len_ = len;
    return true;
}

bool InterchangeReader::read(const InterchangeEntry& e, std::vector<uint8_t>& out,
                             std::string& why) const {
    out.clear();
    if (e.flags & 0x0041) {   // bit 0 encrypted, bit 6 strong encryption
        why = "is encrypted; JNEXT does not read encrypted archives";
        return false;
    }
    if (e.method != 0 && e.method != 8) {
        const std::string n = method_name(e.method);
        why = "uses compression method " + std::to_string(e.method) +
              (n.empty() ? "" : " (" + n + ")") + "; JNEXT reads only stored and deflate";
        return false;
    }
    if (e.comp_size > max_member_ || e.uncomp_size > max_member_) {
        why = "declares " + std::to_string(std::max(e.comp_size, e.uncomp_size)) +
              " bytes, over the " + std::to_string(max_member_) + "-byte limit";
        return false;
    }
    if (!data_ || static_cast<uint64_t>(e.local_offset) + kLocalLen > len_ ||
        rd32(data_ + e.local_offset) != kSigLocal) {
        why = "has a damaged local header";
        return false;
    }
    const uint8_t* l = data_ + e.local_offset;
    const uint64_t data_off = static_cast<uint64_t>(e.local_offset) + kLocalLen +
                              rd16(l + 26) + rd16(l + 28);
    if (data_off + e.comp_size > len_) {
        why = "has data running past the end of the archive";
        return false;
    }
    const uint8_t* src = data_ + data_off;
    if (e.method == 0) {
        if (e.comp_size != e.uncomp_size) {
            why = "is stored but its compressed size differs from its size";
            return false;
        }
        out.assign(src, src + e.comp_size);
    } else {
        if (!inflate_exact(src, e.comp_size, e.uncomp_size, out, why)) {
            out.clear();
            return false;
        }
    }
    const uint32_t crc = static_cast<uint32_t>(
        ::crc32(0, out.empty() ? Z_NULL : out.data(), static_cast<uInt>(out.size())));
    if (crc != e.crc32) {
        out.clear();
        why = "fails its CRC-32 check";
        return false;
    }
    return true;
}

}  // namespace zip
}  // namespace jnext
