#pragma once
//
// The `.jns` snapshot's SD-card media identity — GH #27 stage S7.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §11 (The SD card question),
// §11.3 (the two-tier identity and the six-row restore-behaviour matrix).
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
// digesting a gigabyte is exactly the kind of work that must not become a
// dependency of the descriptor layer's own test binary.
//
// ── THE TWO TIERS, AND WHY THERE ARE TWO ─────────────────────────────────
//
// jnext opens the SD image READ-WRITE and persists guest writes. So "the same
// image" is not a stable thing, and the whole-image digest that is exactly
// right for the warm-start cache — where a boot provably does not write — is
// the WRONG identity here: it changes the first time NextZXOS touches a
// directory entry, and a snapshot would report a mismatch against the very
// card it was taken on. An identity that cries wolf gets ignored, and an
// ignored identity is worse than none (§11.1).
//
//   Tier 1, `identity`      — "is this the same card?" Size, MBR partition
//                             table, FAT32 `BS_VolID`. A mismatch REFUSES.
//   Tier 2, `content_stamp` — "has it changed since?" Whole-image SHA-256 and
//                             mtime, the warm-start cache's mechanism reused
//                             verbatim. A mismatch WARNS — except when the SD
//                             FSM was mid-transfer at capture, where it
//                             refuses (§11.3's last row, enforced by the
//                             container). An UNKNOWN stamp is a third case and
//                             not a mismatch: the container says the contents
//                             could not be compared, and still refuses
//                             mid-transfer, because that row requires a MATCH
//                             and an absence is not one.
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
/// `why` IS ALSO SET ON SUCCESS when only Tier 2 failed — the Tier-1 identity
/// that decides refusals was read, so the card is usable and the return value
/// is true, and `why` says which half is missing. **Branch on the return value,
/// never on `why` being empty.**
///
/// COST, STATED RATHER THAN HIDDEN. Tier 2 digests the WHOLE image: measured
/// 0.49 s warm and 0.72 s cold on the canonical 1 GB card, paid once per save
/// and once per load. §11.3 recommends shipping it EAGER and measuring, and
/// that is what this does — `sd_identity_test` row `JNSI-P33` re-measures it on
/// every run and PRINTS the figure, so the number above is one the suite
/// reproduces rather than a claim nobody re-checks. The lazy variant
/// (`want_content_stamp = false`) exists for callers that have already
/// established Tier 1 does not match, where digesting a gigabyte to fill a
/// field nobody will read is pure waste; it is NOT a way to skip the check,
/// and `JNSI-P23` says so.
bool describe_sdcard_for_snapshot(const std::string& image_path,
                                  bool read_only,
                                  jns::SdCardInfo& out,
                                  std::string& why,
                                  bool want_content_stamp = true);

/// The Tier-2 half on its own: the whole-image SHA-256 and the file's mtime
/// as ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`).
///
/// Exposed separately because the two tiers answer different questions and
/// fail independently: an image whose BPB is unreadable still has a digest,
/// and a digest that fails (an I/O error part-way through a gigabyte) must
/// not discard a Tier-1 identity that was read successfully.
bool read_sd_image_content_stamp(const std::string& image_path,
                                 std::string& sha256_out,
                                 std::string& mtime_utc_out,
                                 std::string& why);

}  // namespace jnext
