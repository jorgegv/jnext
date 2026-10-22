#pragma once
//
// The `.jns` snapshot's SD-card media identity — GH #27 stage S7.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §11 (The SD card question),
// §11.3 (the identity and the restore-behaviour matrix).
//
// WHAT THIS FILE IS. The PRODUCER of `jns::SdCardInfo`. The container
// (`src/save/jns_container.{h,cpp}`) owns the RULES — which differences
// refuse, which warn and which are ignored — and does no filesystem I/O at
// all, which is what keeps its rows cheap and its dependencies none. This
// file is the other half: it reads a real image off disk and fills the struct
// those rules compare.
//
// The split is deliberate and load-bearing. `snapshot_test` links
// `jnext_save` alone (design §16.1), so the rules are provable without an
// emulator, a card or a filesystem; this file lives in `jnext_core` because
// reading an image off disk must not become a dependency of the descriptor
// layer's own test binary.
//
// ── ONE TIER, AND WHY THERE IS NO SECOND (GH #396) ──────────────────────
//
// jnext opens the SD image READ-WRITE and persists guest writes. So "the same
// image" is not a stable thing: a whole-image digest changes the first time
// NextZXOS touches a directory entry.
//
//   Tier 1, `identity` - "is this the same card?" Size, MBR partition table,
//                        FAT32 `BS_VolID`. A mismatch REFUSES. It reads two
//                        sectors.
//
// There used to be a Tier 2, `content_stamp`: a whole-image SHA-256 and mtime
// ("has it changed since?"), computed on every save, probe and load on the UI
// thread. That is O(card): 0.6-1.2 s per GB measured, tens of seconds on a
// large card, a frozen window on every Quick Save and Quick Load. The owner
// removed it (2026-10-22): jnext now ASSUMES the mounted card is the one the
// snapshot was taken with, in the state it was left. Changing the card between
// saving and loading is AT THE USER'S RISK - NextZXOS keeps FAT and directory
// state in RAM, and a snapshot restored over a changed card can corrupt it.
// `.jns` files written by jnext 1.1.0-1.1.75 carry a `content_stamp`; it is
// ignored on read.
//
// `informational.fat32_bs_vollab` is CARRIED AND NEVER COMPARED. It is a
// stale copy of the authoritative root-directory label, and a tool that
// rewrites it would produce a false refusal on the same physical card.

#include <string>

#include "save/jns_container.h"

namespace jnext {

/// Fill `out` for the image mounted at `image_path`.
///
/// `out.present` is set true and every field this build can derive is filled.
/// Returns false with `why` naming the defect when the image cannot be read
/// or is not an MBR + FAT32-LBA image at all; `out` is then left with
/// `present == false` so a caller that ignores the return value records "no
/// card" rather than a half-filled identity.
///
/// Tier 1 only: a few sectors, never the whole image.
bool describe_sdcard_for_snapshot(const std::string& image_path,
                                  bool read_only,
                                  jns::SdCardInfo& out,
                                  std::string& why);

}  // namespace jnext
