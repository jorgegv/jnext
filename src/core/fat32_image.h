#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Host-side FAT32 volume read + "recluster" support (Task 27).
///
/// jnext fixes a freshly downloaded SD image IN-PROCESS (no external mtools /
/// bash dependency, works on Windows). Background: the shipped 1 GB CSpect /
/// NextZXOS image uses 32 KB clusters (sectors_per_cluster = 64), yielding only
/// ~32 758 data clusters — below the FAT32 spec minimum of 65 525. A strict
/// FatFs (like tbblue.fw's) classifies volumes with <= 65 525 clusters as FAT16
/// and then rejects this one because its BPB is FAT32-shaped (root_entries = 0).
/// The fix reformats the partition with 8 KB clusters (~131 k clusters, a valid
/// FAT32) while preserving the whole file tree.
///
/// Two pieces:
///   1. A lenient hand-rolled READER (fat32_find_partition / fat32_read_tree)
///      slurps the whole tree of the under-clustered source into memory. FatFs
///      itself cannot read the source (it rejects the sub-65 525-cluster
///      FAT32-shaped BPB — see fatfs_format.cpp), so the read side stays
///      hand-rolled.
///   2. A WRITER (fat32_format_and_populate) that reformats the partition to a
///      spec-valid FAT32 with 8 KB clusters and re-emits the tree, implemented
///      on top of the vendored ChaN FatFs (f_mkfs + f_write). This retires the
///      former hand-rolled VFAT-LFN volume writer.
///
/// Only the FAT32-LBA, MBR-partitioned, 512-byte-sector layout used by the
/// TBBlue distribution is supported. ASCII file names only (sufficient for the
/// NextZXOS distribution).

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
// Read (lenient, hand-rolled — the source is under-clustered)
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

// ---------------------------------------------------------------------------
// Write (via vendored ChaN FatFs — implemented in fatfs_format.cpp)
// ---------------------------------------------------------------------------

/// Reformat the FAT32 partition at `part_lba` (size `total_sectors` sectors)
/// of `image_path` IN PLACE as a spec-valid FAT32 with 8 KB clusters (via
/// FatFs f_mkfs), then write the whole `tree` into it (directories + files via
/// f_mkdir / f_open / f_write). The image's MBR and the partition's start LBA
/// and size are preserved. Returns true on success, else sets `err`.
bool fat32_format_and_populate(const std::string& image_path, uint32_t part_lba,
                               uint32_t total_sectors, const Fat32Tree& tree,
                               std::string& err);
