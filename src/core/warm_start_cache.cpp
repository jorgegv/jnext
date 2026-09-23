#include "core/warm_start_cache.h"

#include "core/log.h"
#include "core/sdcard_provisioner.h"

#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace warm_start {
namespace {

// ---------------------------------------------------------------------------
// File layout — a PLAIN header followed by a DEFLATED payload
// ---------------------------------------------------------------------------
//
// The payload is zlib-compressed and the 96-byte header is not, deliberately.
//
// The header carries the four identity fields the loader validates BEFORE it
// is willing to trust a byte of the payload. Compressing it would mean
// inflating ~2.3 MB of a file that has not yet been shown to be this build's,
// this machine's or this SD image's — i.e. doing the expensive work on
// unvalidated input, and sizing the output buffer from a length read out of
// that same input. Every refusal below (wrong magic, wrong machine, wrong
// digest, wrong plain length, truncation) is answerable from 96 plain bytes,
// and that ordering — establish identity, then decompress — is what keeps the
// length guard meaningful. A guard that runs after the thing it guards is not
// one.
//
// There is a mechanical reason pulling the same way: zlib's `uncompress()`
// wants the exact uncompressed size up front. That number IS `plain_bytes`,
// and it lives in the plain header. Compressing the header would leave the
// reader with no way to learn its own output size except by trusting the
// stream to tell it.
//
// The cost of keeping it plain is 96 bytes out of ~130 KB.
//
// Measured on the real recording (2026-09-23): 2 293 061 plain bytes, of
// which RAM is 91.5 % and 89.5 % of the whole file is zero. deflate level 9
// takes it to ~129 KB — 5.6 % of the original. zlib rather than zstd because
// zlib is ALREADY a required dependency (`find_package(ZLIB REQUIRED)`,
// CMakeLists.txt:151; rzx.h, szx_loader.cpp and sdcard_provisioner.cpp all
// use it), and a new dependency for the last 30 KB of a local cache file is
// not a trade this project makes.
//
//   offset  size  field
//   ------  ----  ---------------------------------------------------------
//        0     8  magic "JNEXTWS2"
//        8     4  state-stream format version (u32 LE)
//       12     4  machine type                (u32 LE)
//       16     8  PLAIN payload length        (u64 LE)  <- identity
//       24    64  SD image SHA-256, ASCII hex, NUL-padded
//       88     8  STORED payload length       (u64 LE)  <- bytes on disk
//       96   ...  zlib stream, exactly `stored` bytes
//
// "JNEXTWS2" — 8 bytes, no terminator. The trailing digit is a FILE-layout
// generation, distinct from Identity::format_version (which versions the
// state stream inside). It went 1 -> 2 here because the payload stopped being
// the raw stream; the stream itself did not change, so kFormatVersion did
// not. A v1 file therefore fails at the magic — the first and cheapest check
// — rather than being inflated as though it were compressed.
constexpr char kMagic[8] = {'J', 'N', 'E', 'X', 'T', 'W', 'S', '2'};

// Header field offsets. Written and read from the same constants so the two
// sides cannot drift.
constexpr size_t kOffMagic        = 0;    // 8
constexpr size_t kOffFormatVer    = 8;    // u32
constexpr size_t kOffMachineType  = 12;   // u32
constexpr size_t kOffPlainBytes   = 16;   // u64 — uncompressed stream length
constexpr size_t kOffSha256       = 24;   // 64 ASCII hex chars, NUL-padded
constexpr size_t kShaFieldBytes   = 64;
constexpr size_t kOffStoredBytes  = 88;   // u64 — compressed payload length

void put_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
void put_u64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t get_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[static_cast<size_t>(i)];
    return v;
}

/// Deflate `src` at maximum level. Empty on failure.
///
/// Level 9 rather than the default 6: this is written once per SD image and
/// read on every load, so the asymmetry is entirely in favour of spending the
/// compressor's time. The input is ~90 % zeros, which deflate handles well at
/// any level; 9 costs a fraction of a second on top of a 3.5 s boot.
std::vector<uint8_t> deflate_buffer(const uint8_t* src, size_t src_len)
{
    const uLong bound = compressBound(static_cast<uLong>(src_len));
    std::vector<uint8_t> out(static_cast<size_t>(bound));
    uLongf out_len = static_cast<uLongf>(bound);
    if (compress2(out.data(), &out_len, src, static_cast<uLong>(src_len),
                  Z_BEST_COMPRESSION) != Z_OK)
        return {};
    out.resize(static_cast<size_t>(out_len));
    return out;
}

/// Inflate `src` and require EXACTLY `expect_plain` bytes out.
///
/// The expected size is a parameter rather than something learned from the
/// stream, and the equality is checked rather than the buffer merely being
/// large enough. zlib's `uncompress()` already refuses a stream that wants
/// MORE room than it was given (Z_BUF_ERROR); the explicit comparison
/// afterwards catches the other direction — a stream that inflates to FEWER
/// bytes than the header promised, which would otherwise hand the caller a
/// buffer with a tail of whatever it was resized with. Downstream,
/// `Emulator::load_state` reads that buffer as a sequence of sized fields, so
/// a short stream is precisely the shape that deserialises into the wrong
/// fields instead of failing.
bool inflate_exact(const uint8_t* src, size_t src_len, uint64_t expect_plain,
                   std::vector<uint8_t>& out, std::string& why)
{
    out.assign(static_cast<size_t>(expect_plain), 0);
    uLongf dest_len = static_cast<uLongf>(expect_plain);
    const int rc = uncompress(out.data(), &dest_len, src, static_cast<uLong>(src_len));
    if (rc != Z_OK) {
        out.clear();
        why = "the compressed state stream does not inflate (zlib error " +
              std::to_string(rc) + ")";
        return false;
    }
    if (static_cast<uint64_t>(dest_len) != expect_plain) {
        out.clear();
        why = "the state stream inflates to " + std::to_string(dest_len) +
              " bytes, but its header declares " + std::to_string(expect_plain);
        return false;
    }
    return true;
}

}  // namespace

std::string cache_dir()
{
    // Derived from the SD directory rather than re-resolving $JNEXT_CONFIG_DIR
    // /$HOME: the cache belongs beside the image it was recorded from, and one
    // resolution means the two cannot end up in different trees when the
    // regression suite points the variable at a per-run directory (GH #65).
    std::filesystem::path sd(sdcard::default_sdcard_dir());
    return (sd.parent_path() / "warm-start").string();
}

std::string cache_path(uint8_t machine_type)
{
    return cache_dir() + "/warm-start-m" + std::to_string(static_cast<int>(machine_type)) +
           ".jwss";
}

bool load(const Identity& want, std::vector<uint8_t>& out, std::string& why)
{
    const std::string path = cache_path(want.machine_type);

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        why = "no cached state at " + path;
        return false;
    }
    if (size < kHeaderBytes) {
        why = "cached state at " + path + " is shorter than its own header";
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        why = "cannot open " + path;
        return false;
    }

    uint8_t hdr[kHeaderBytes]{};
    f.read(reinterpret_cast<char*>(hdr), kHeaderBytes);
    if (!f) {
        why = "cannot read the header of " + path;
        return false;
    }

    if (std::memcmp(hdr + kOffMagic, kMagic, sizeof(kMagic)) != 0) {
        why = path + " is not a jnext warm-start file";
        return false;
    }

    const uint32_t ver    = get_u32(hdr + kOffFormatVer);
    const uint32_t mtype  = get_u32(hdr + kOffMachineType);
    const uint64_t plain  = get_u64(hdr + kOffPlainBytes);
    const uint64_t stored = get_u64(hdr + kOffStoredBytes);

    char sha[kShaFieldBytes + 1]{};
    std::memcpy(sha, hdr + kOffSha256, kShaFieldBytes);
    const std::string stored_sha(sha);

    // Each mismatch names BOTH values: a stale cache that silently served the
    // wrong machine is the worst failure this mechanism can have, so when it
    // is refused the log has to say precisely which of the four keys moved.
    if (ver != want.format_version) {
        why = "state-format version " + std::to_string(ver) + " != " +
              std::to_string(want.format_version);
        return false;
    }
    if (mtype != want.machine_type) {
        why = "recorded for machine type " + std::to_string(mtype) + ", not " +
              std::to_string(static_cast<int>(want.machine_type));
        return false;
    }
    if (stored_sha != want.sd_image_sha256) {
        why = "recorded from a different SD image (" + stored_sha + " != " +
              want.sd_image_sha256 + ")";
        return false;
    }
    // The PLAIN length is the identity check — what this build's save_state
    // produces — and it is the one compared against `want`. The STORED length
    // below is a property of the file, never of the machine, so it is checked
    // against the file's own size and against nothing else. Keeping the two
    // comparisons apart is what stops a compressed length ever being mistaken
    // for the guard that keeps Ram::load_state inside its buffer.
    if (plain != want.plain_bytes) {
        why = "state stream is " + std::to_string(plain) + " bytes, this build writes " +
              std::to_string(want.plain_bytes);
        return false;
    }
    if (size != kHeaderBytes + stored) {
        why = path + " is truncated (" + std::to_string(size) + " bytes on disk, header "
              "declares " + std::to_string(kHeaderBytes + stored) + ")";
        return false;
    }
    if (stored == 0) {
        why = path + " declares an empty compressed payload";
        return false;
    }
    // The stored length SIZES AN ALLOCATION, and unlike the plain length it is
    // not compared against anything this build computed — only against the
    // file's own size, so a 4 GB file in the cache directory would be a 4 GB
    // read. Bound it by what deflate can actually produce from `plain` bytes:
    // compressBound() is zlib's own worst case (the input plus a few parts per
    // thousand), so no legitimate stream can exceed it and the check needs no
    // invented margin. Placed AFTER the plain-length check, because that is
    // what makes `plain` trustworthy enough to derive a bound from.
    if (stored > static_cast<uint64_t>(compressBound(static_cast<uLong>(plain)))) {
        why = path + " declares a " + std::to_string(stored) +
              "-byte compressed payload, more than deflate can produce from " +
              std::to_string(plain) + " bytes";
        return false;
    }

    std::vector<uint8_t> packed(static_cast<size_t>(stored));
    f.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(stored));
    if (!f) {
        why = "cannot read the state stream of " + path;
        return false;
    }

    if (!inflate_exact(packed.data(), packed.size(), plain, out, why)) return false;

    why.clear();
    return true;
}

bool store(const Identity& id, const std::vector<uint8_t>& state, std::string& why)
{
    if (state.size() != id.plain_bytes) {
        why = "refusing to write a header that disagrees with its own payload";
        return false;
    }
    if (id.sd_image_sha256.size() > kShaFieldBytes) {
        why = "SD image digest does not fit the header field";
        return false;
    }

    const std::string dir = cache_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir)) {
        why = "cannot create " + dir;
        return false;
    }

    const std::vector<uint8_t> packed = deflate_buffer(state.data(), state.size());
    if (packed.empty()) {
        why = "the state stream did not compress";
        return false;
    }

    uint8_t hdr[kHeaderBytes]{};
    std::memcpy(hdr + kOffMagic, kMagic, sizeof(kMagic));
    put_u32(hdr + kOffFormatVer, id.format_version);
    put_u32(hdr + kOffMachineType, id.machine_type);
    put_u64(hdr + kOffPlainBytes, id.plain_bytes);
    std::memcpy(hdr + kOffSha256, id.sd_image_sha256.data(), id.sd_image_sha256.size());
    put_u64(hdr + kOffStoredBytes, static_cast<uint64_t>(packed.size()));

    // Write to a sibling temporary and rename: a jnext killed mid-write must
    // not leave a half-recording that the length check would reject on every
    // later run without ever replacing it.
    const std::string final_path = cache_path(id.machine_type);
    const std::string tmp_path   = final_path + ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f) {
            why = "cannot open " + tmp_path + " for writing";
            return false;
        }
        f.write(reinterpret_cast<const char*>(hdr), kHeaderBytes);
        f.write(reinterpret_cast<const char*>(packed.data()),
                static_cast<std::streamsize>(packed.size()));
        f.flush();
        if (!f) {
            why = "cannot write " + tmp_path;
            f.close();
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        why = "cannot rename " + tmp_path + " to " + final_path + ": " + ec.message();
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    why.clear();
    return true;
}

}  // namespace warm_start
