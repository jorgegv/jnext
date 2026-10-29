#include "core/sd_snapshot_identity.h"

#include "core/sd_rom_extractor.h"

namespace jnext {

bool describe_sdcard_for_snapshot(const std::string& image_path,
                                  bool read_only,
                                  jns::SdCardInfo& out,
                                  std::string& why) {
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

    return true;
}

}  // namespace jnext
