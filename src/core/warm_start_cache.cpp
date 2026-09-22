#include "core/warm_start_cache.h"

#include "core/log.h"
#include "core/sdcard_provisioner.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace warm_start {
namespace {

// "JNEXTWS1" — 8 bytes, no terminator. The trailing digit is a FILE-layout
// generation, distinct from Identity::format_version (which versions the
// state stream inside). Bump it only if this header itself changes shape.
constexpr char kMagic[8] = {'J', 'N', 'E', 'X', 'T', 'W', 'S', '1'};

// Header field offsets. Written and read from the same constants so the two
// sides cannot drift.
constexpr size_t kOffMagic        = 0;    // 8
constexpr size_t kOffFormatVer    = 8;    // u32
constexpr size_t kOffMachineType  = 12;   // u32
constexpr size_t kOffStateBytes   = 16;   // u64
constexpr size_t kOffSha256       = 24;   // 64 ASCII hex chars, NUL-padded
constexpr size_t kShaFieldBytes   = 64;
// 88..95 reserved, written as zero.

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

    const uint32_t ver   = get_u32(hdr + kOffFormatVer);
    const uint32_t mtype = get_u32(hdr + kOffMachineType);
    const uint64_t bytes = get_u64(hdr + kOffStateBytes);

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
    if (bytes != want.state_bytes) {
        why = "state stream is " + std::to_string(bytes) + " bytes, this build writes " +
              std::to_string(want.state_bytes);
        return false;
    }
    if (size != kHeaderBytes + bytes) {
        why = path + " is truncated (" + std::to_string(size) + " bytes on disk, header "
              "declares " + std::to_string(kHeaderBytes + bytes) + ")";
        return false;
    }

    out.resize(static_cast<size_t>(bytes));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    if (!f) {
        out.clear();
        why = "cannot read the state stream of " + path;
        return false;
    }

    why.clear();
    return true;
}

bool store(const Identity& id, const std::vector<uint8_t>& state, std::string& why)
{
    if (state.size() != id.state_bytes) {
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

    uint8_t hdr[kHeaderBytes]{};
    std::memcpy(hdr + kOffMagic, kMagic, sizeof(kMagic));
    put_u32(hdr + kOffFormatVer, id.format_version);
    put_u32(hdr + kOffMachineType, id.machine_type);
    put_u64(hdr + kOffStateBytes, id.state_bytes);
    std::memcpy(hdr + kOffSha256, id.sd_image_sha256.data(), id.sd_image_sha256.size());

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
        f.write(reinterpret_cast<const char*>(state.data()),
                static_cast<std::streamsize>(state.size()));
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
