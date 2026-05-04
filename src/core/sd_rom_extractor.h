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
