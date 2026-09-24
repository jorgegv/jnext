#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Host-side FAT32 file extractor for ZX Spectrum Next SD card images.
///
/// Wave 0.2 of the Task 8 Multiface plan introduces a HOST-SIDE FAT32
/// reader so Emulator::init can pull named ROM files (enNxtmmc.rom,
/// enNextMf.rom, 48.rom, ...) directly from the SD image at startup.
///
/// This is INDEPENDENT of the in-emulator SPI/SD-card path
/// (src/peripheral/sd_card.cpp), which remains the block-level transport
/// for Z80 software at runtime. The two paths coexist by design — one
/// serves the host (init-time named-file lookup), the other serves the
/// guest (runtime block I/O).
///
/// The reader implements a focused read-only FAT32-LBA parser:
///   - MBR with FAT32-LBA partition (type 0x0B / 0x0C)
///   - 8.3 short-name lookup (case-insensitive on input)
///   - FAT32 cluster-chain traversal up to 0x0FFFFFF8 EOC
///   - File data accumulation up to declared file size
///
/// Long file name (LFN) entries (attribute 0x0F) are skipped during
/// directory scans; lookup falls back to short-name matching, which is
/// sufficient for the canonical TBBlue distribution where every
/// human-readable name has a corresponding 8.3 SFN entry on disk
/// (e.g. enNxtmmc.rom ↔ ENNXTMMC.ROM).
///
/// Not supported (out of scope for Wave 0.2):
///   - FAT12 / FAT16 / exFAT
///   - Writes
///   - LFN-only files (none on TBBlue images)
///   - GPT-partitioned images (TBBlue distribution is MBR-only)
///
/// Extract a single named file from the FAT32 partition of a Next SD image.
///
/// `sd_image_path` points at a raw .img file containing an MBR + FAT32 LBA
/// partition (the standard TBBlue distribution format).
///
/// `sd_path` is a forward-slash-separated path inside the FAT32 filesystem,
/// case-insensitive. A leading '/' is optional. Examples:
///   "/MACHINES/NEXT/enNxtmmc.rom"
///   "machines/next/enNxtmmc.rom"
///   "/TBBLUE.FW"
///
/// On success: returns true, fills `out` with the file bytes (resized
/// exactly to the file's stored size), and `bytes_read_out` (if non-null)
/// is set to the same size.
/// On failure (file not found, image not a FAT32 partition, malformed FAT,
/// I/O error, etc.): returns false, leaves `out` empty, logs the reason
/// via Log::emulator()->error().
bool extract_sd_rom(const std::string& sd_image_path,
                    const std::string& sd_path,
                    std::vector<uint8_t>& out,
                    std::size_t* bytes_read_out = nullptr);

// ---------------------------------------------------------------------------
// SD image TIER-1 IDENTITY — GH #27 S7, design §11.3
// ---------------------------------------------------------------------------

/// "Is this the same card?", answered from bytes a FILE WRITE DOES NOT TOUCH.
///
/// jnext opens the SD image READ-WRITE and persists guest writes, so a
/// whole-image digest — the right identity for the warm-start cache, where a
/// boot provably does not write — is the WRONG identity for a snapshot: it
/// changes the first time NextZXOS touches a directory entry, and the snapshot
/// would then report a mismatch against the very card it was taken on. An
/// identity that cries wolf gets ignored, and an ignored identity is worse
/// than none (design §11.1).
///
/// So Tier 1 is the image size, the MBR partition table and the FAT32 boot
/// sector's volume serial. The whole-image digest is Tier 2, and it WARNS
/// rather than refuses (`read_sd_image_content_stamp`, sd_snapshot_identity.h).
struct SdImageIdentity {
    /// Size of the image file in bytes.
    uint64_t image_bytes = 0;

    /// SHA-256 of the MBR PARTITION TABLE — the 64-byte table at offset 0x1BE
    /// plus the 2-byte 0x55AA signature, 66 bytes — lower-case hex.
    ///
    /// NOT the whole 512-byte sector, although the field name would allow it.
    /// §11.3 names "the MBR partition table", and the narrower window is the
    /// one that survives the same argument that removed `BS_VolLab` from the
    /// refusal test: the first 446 bytes are BOOTSTRAP CODE, which `fdisk`,
    /// `syslinux` and several imaging tools rewrite without touching the
    /// partitioning. Digesting them would produce a false refusal on the same
    /// physical card, which is the cries-wolf failure §11.1 exists to avoid.
    std::string mbr_sha256;

    /// `BS_VolID`, BPB offset 0x43, 4 bytes little-endian, rendered as exactly
    /// 8 lower-case hex digits. The genuinely stable identifier, and what the
    /// Tier-1 refusal rests on.
    ///
    /// EMPTY when the boot sector carries no extended signature
    /// (`BS_BootSig` != 0x29 at offset 0x42): the field is then not defined by
    /// the FAT specification and whatever sits there is not a volume serial.
    /// An empty serial makes `jns::SdIdentity::populated()` false, and the
    /// reader refuses rather than treating two absences as one card — the
    /// behaviour `JNSI-13` already pins.
    std::string fat32_volume_id;

    /// LBA of the FAT32 partition's first sector, from the MBR partition table.
    uint32_t partition_lba = 0;

    /// `BS_VolLab`, BPB offset 0x47, 11 bytes, space-padded — INFORMATIONAL
    /// ONLY, NEVER COMPARED (design §11.3). It is a STALE COPY: the
    /// authoritative FAT32 volume label is the root-directory entry carrying
    /// ATTR_VOLUME_ID, which this file's own FAT walk explicitly skips. The
    /// two disagree routinely, and a tool that rewrites `BS_VolLab` (several
    /// do, writing both) would produce a false refusal on the same physical
    /// card.
    ///
    /// Bytes outside printable ASCII (0x20-0x7E) are replaced with '?'. The
    /// label is OEM-charset on disk and this string lands in `manifest.json`,
    /// which must be valid UTF-8; carrying the raw bytes would make a
    /// high-byte label produce an unparseable manifest. Trailing spaces are
    /// PRESERVED — the field is 11 bytes wide by specification and trimming it
    /// would silently normalise two different labels into one.
    ///
    /// EMPTY when `BS_BootSig` != 0x29, for the same reason as the serial.
    std::string fat32_bs_vollab;
};

/// Read `SdImageIdentity` from an SD image. Returns false with `why` naming
/// the defect (unreadable, no MBR signature, no FAT32-LBA partition, a BPB
/// that fails the same validation `extract_sd_rom` applies).
///
/// THE NEW EXPORTED ENTRY POINT design §11.3 calls for. jnext already parsed
/// the MBR and the BPB host-side, but both parsers live in this file's
/// anonymous namespace, so this is two fields plus an export rather than a
/// pure two-field addition. "Already parses it" and "already exposes it" are
/// not the same claim.
bool read_sd_image_identity(const std::string& sd_image_path,
                            SdImageIdentity& out, std::string& why);
