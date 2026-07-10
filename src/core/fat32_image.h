#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Host-side FAT32 volume read/write + "recluster" support (Task 27).
///
/// This module is the native C++ reimplementation of the FAT32 cluster-count
/// fix that `tools/fix-sdcard-image.sh` performs with mtools. It exists so
/// jnext can fix a freshly downloaded SD image IN-PROCESS (no external
/// mtools / bash dependency, works on Windows).
///
/// Background: the shipped 1 GB CSpect / NextZXOS image uses 32 KB clusters
/// (sectors_per_cluster = 64), yielding only ~32 758 data clusters — below
/// the FAT32 spec minimum of 65 525. A strict FatFs (like tbblue.fw's, and
/// like real FAT tooling) classifies volumes with <= 65 525 clusters as
/// FAT16 and then rejects this one because its BPB is FAT32-shaped
/// (root_entries = 0). The fix reformats the partition with 8 KB clusters
/// (sectors_per_cluster = 16, ~130 938 clusters, a valid FAT32) while
/// preserving the whole file tree.
///
/// The three pieces are exposed separately so each is independently unit
/// testable offline (no network, no external tooling):
///   1. fat32_compute_geometry() — pure arithmetic (reserved / FAT size /
///      cluster count / FAT-type classification) for a fresh FAT32 volume.
///   2. fat32_read_tree()        — recursive read of the whole directory
///      tree (files + subdirs, long names via VFAT LFN).
///   3. fat32_write_volume()     — write a fresh FAT32 volume (boot sector,
///      FSInfo, two FATs, data region with VFAT LFN entries) into an
///      existing image's partition, in place.
///
/// Only the FAT32-LBA, MBR-partitioned, 512-byte-sector layout used by the
/// TBBlue distribution is supported. ASCII file names only (sufficient for
/// the NextZXOS distribution; non-ASCII LFN code units are stored/emitted
/// by their low byte).

// ---------------------------------------------------------------------------
// In-memory directory tree
// ---------------------------------------------------------------------------

struct Fat32Node {
    std::string          name;      // display name (long name if present)
    bool                 is_dir = false;
    std::vector<uint8_t> data;      // file contents (empty for directories)
    std::vector<Fat32Node> children; // directory entries (empty for files)
};

// The root directory's direct children.
struct Fat32Tree {
    std::vector<Fat32Node> root;
};

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

struct Fat32FormatGeom {
    uint32_t total_sectors       = 0;  // partition size in sectors (BPB TotSec32)
    uint16_t bytes_per_sector    = 512;
    uint8_t  sectors_per_cluster = 0;
    uint16_t reserved_sectors    = 32;
    uint8_t  num_fats            = 2;
    uint32_t fat_size_sectors    = 0;  // per-FAT size (BPB FATSz32)
    uint32_t cluster_count       = 0;  // count of data clusters
    bool     valid               = false; // geometry could be satisfied
    bool     is_fat32            = false; // cluster_count > 65525
};

/// Compute a fresh-format FAT32 geometry for a partition of `total_sectors`
/// with the requested `sectors_per_cluster`. Iteratively sizes the FAT so it
/// can address every data cluster. `is_fat32` is set per the FatFs rule
/// (cluster_count > 65525). `valid` is false only if the partition is too
/// small to hold even the reserved + FAT regions.
Fat32FormatGeom fat32_compute_geometry(uint32_t total_sectors,
                                       uint8_t  sectors_per_cluster,
                                       uint16_t bytes_per_sector = 512,
                                       uint8_t  num_fats         = 2,
                                       uint16_t reserved_sectors = 32);

// ---------------------------------------------------------------------------
// Read / write
// ---------------------------------------------------------------------------

/// Locate the first FAT32-LBA partition in the MBR of `image_path`.
/// On success sets `part_lba_out` (partition first sector) and returns true.
bool fat32_find_partition(const std::string& image_path, uint32_t& part_lba_out);

/// Read the entire directory tree of the FAT32 partition starting at
/// `part_lba` (sector index) in `image_path`. Long names are reconstructed
/// from VFAT LFN entries. Returns true on success.
bool fat32_read_tree(const std::string& image_path, uint32_t part_lba,
                     Fat32Tree& out);

/// Insert or replace a file at `path` (forward-slash separated, case
/// preserved but matched case-insensitively) within `tree`, creating any
/// missing intermediate directories. Used to inject config.ini.
void fat32_tree_upsert(Fat32Tree& tree, const std::string& path,
                       const std::vector<uint8_t>& data);

/// Write a fresh FAT32 volume described by `geom` + `tree` into the partition
/// at `part_lba` of `image_path`, in place. Overwrites the reserved region,
/// both FATs, and the used data clusters; the file is not resized and unused
/// clusters are left as-is (marked free in the FAT). `hidden_sectors` is the
/// number of sectors before the partition (== part_lba for a single-partition
/// image) and is written to the BPB. Returns true on success, else sets `err`.
bool fat32_write_volume(const std::string& image_path, uint32_t part_lba,
                        const Fat32FormatGeom& geom, const Fat32Tree& tree,
                        std::string& err);
