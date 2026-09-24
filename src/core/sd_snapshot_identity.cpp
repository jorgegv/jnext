#include "core/sd_snapshot_identity.h"

#include "core/log.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_provisioner.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace jnext {

namespace {

namespace fs = std::filesystem;

/// `fs::file_time_type` -> `YYYY-MM-DDTHH:MM:SSZ`.
///
/// C++17 has no portable `file_clock` -> `system_clock` cast (that is C++20's
/// `std::chrono::clock_cast`), so this uses the same documented offset idiom
/// `EsxdosHostFs::dos_timestamp` uses, and rounds to the nearest second for
/// the same reason: the idiom samples two clocks at slightly different
/// instants, so an mtime of exactly :04 arrives as :03.999999 about half the
/// time and truncation would report a different string on alternate runs. A
/// stamp that wobbles is a stamp that produces spurious Tier-2 warnings.
std::string iso8601_utc(fs::file_time_type mtime) {
    const auto sctp = std::chrono::time_point_cast<
        std::chrono::system_clock::duration>(
        mtime - fs::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const std::time_t when = std::chrono::system_clock::to_time_t(
        sctp + std::chrono::milliseconds(500));

    std::tm tm{};
#ifdef _WIN32
    if (gmtime_s(&tm, &when) != 0) return {};
#else
    if (gmtime_r(&when, &tm) == nullptr) return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return {};
    }
    return buf;
}

}  // namespace

bool read_sd_image_content_stamp(const std::string& image_path,
                                 std::string& sha256_out,
                                 std::string& mtime_utc_out,
                                 std::string& why) {
    sha256_out.clear();
    mtime_utc_out.clear();
    why.clear();

    std::error_code ec;
    const fs::file_time_type mtime = fs::last_write_time(image_path, ec);
    if (ec) {
        why = "cannot stat SD image '" + image_path + "': " + ec.message();
        return false;
    }
    mtime_utc_out = iso8601_utc(mtime);
    if (mtime_utc_out.empty()) {
        why = "cannot format the mtime of SD image '" + image_path + "'";
        return false;
    }

    sha256_out = sdcard::sha256_file(image_path);
    if (sha256_out.empty()) {
        why = "SHA-256 of SD image '" + image_path + "' failed";
        mtime_utc_out.clear();
        return false;
    }
    return true;
}

bool describe_sdcard_for_snapshot(const std::string& image_path,
                                  bool read_only,
                                  jns::SdCardInfo& out,
                                  std::string& why,
                                  bool want_content_stamp) {
    out = jns::SdCardInfo{};
    why.clear();

    SdImageIdentity id;
    if (!read_sd_image_identity(image_path, id, why)) {
        // `out` keeps `present == false`. A caller that ignores the return
        // value therefore records "no card", not a card with half an
        // identity — which would compare unequal against itself on reload and
        // refuse for a reason nobody could act on.
        return false;
    }

    out.present         = true;
    out.mounted_path    = image_path;
    out.read_only       = read_only;
    out.identity.image_bytes     = id.image_bytes;
    out.identity.mbr_partition_table_sha256      = id.mbr_partition_table_sha256;
    out.identity.fat32_volume_id = id.fat32_volume_id;
    out.identity.partition_lba   = id.partition_lba;
    out.vollab                   = id.fat32_bs_vollab;

    if (!want_content_stamp) return true;

    std::string stamp_why;
    if (!read_sd_image_content_stamp(image_path, out.content_sha256,
                                     out.content_mtime_utc, stamp_why)) {
        // Tier 2 failing does NOT discard Tier 1. The identity that decides
        // refusals was read; only the drift check is unavailable, and the
        // container distinguishes an UNKNOWN stamp from a CHANGED one — it
        // says "could not be compared" rather than quoting an empty string as
        // the new digest, and still refuses when the card was mid-transfer,
        // because §11.3's last row requires a match there. Say so and carry on.
        Log::emulator()->warn("snapshot: SD content stamp unavailable: {}",
                              stamp_why);
        why = stamp_why;
    }
    return true;
}

}  // namespace jnext
