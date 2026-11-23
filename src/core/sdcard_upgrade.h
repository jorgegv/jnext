#pragma once
// The card-set side of an SD card upgrade (GH #555 S5): carrying the user's files onto the new
// card, and switching the whole set (card, raw, sidecars, overlays) in a crash-safe sequence that
// keeps the old set as a backup in "previous/". provision_sd_card (sdcard_provisioner.cpp) owns
// the download and the policy; this file owns what happens to the files.
//
// Layout, all beside the default card (<sdcard dir>):
//   upgrade-work/            scratch: new raw, new card, the old card as made, the step journal
//   upgrade-work/SWITCHING   the switch plan; present = roll the switch FORWARD at the next start
//   previous/                the backup: the old card, raw, sidecars; overlays/ holds the old
//                            overlays' bytes; upgrade-report.txt the last upgrade's report
//   overlays.list            absolute paths of the --sdcard-writes files bound over the default card
#include "core/sdcard_carry.h"

#include <string>
#include <vector>

namespace sdcard::upgrade {

extern const char* const kWorkDir;        ///< "upgrade-work"
extern const char* const kPreviousDir;    ///< "previous"
extern const char* const kSwitchMarker;   ///< "SWITCHING"
extern const char* const kOverlayList;    ///< "overlays.list"
extern const char* const kReportName;     ///< "upgrade-report.txt"

/// Everything one upgrade needs to know about the default location and the set being built.
struct Plan {
    std::string dir;                       ///< <sdcard dir>
    std::string card, raw;                 ///< the current default pair (cspect-next-1gb-fixed.img / cspect-next-1gb.img)
    std::string new_card, new_raw;         ///< the new pair, inside <dir>/upgrade-work/
    std::string new_distro;                ///< distribution (zip name) of the new pair
    std::string new_raw_sha;               ///< SHA-256 (hex) of the new raw
    int         new_recipe = 0;            ///< the recipe the new card was made with
    bool        carry = true;              ///< false: a fresh start (nothing carried, overlays set aside)
    std::vector<std::string> overlays;     ///< the --sdcard-writes FILEs bound over the current card
};

/// <dir>/upgrade-work and <dir>/previous.
std::string work_dir(const std::string& dir);
std::string previous_dir(const std::string& dir);

/// The overlay files to carry: `explicit_files` (the command line's) plus every entry of
/// <dir>/overlays.list that still exists and binds to `card` (size + mtime, or the SHA-256 of the
/// card). Entries that no longer qualify are dropped from the list (rewritten). De-duplicated.
std::vector<std::string> usable_overlays(const std::string& dir, const std::string& card,
                                         const std::vector<std::string>& explicit_files);
/// Append `file` to <dir>/overlays.list when it is not there yet (best effort).
void register_overlay(const std::string& dir, const std::string& file);

/// Does the writes file `file` belong to the backup card in previous/ (it binds to it by size and
/// mtime, or digest)? Used to explain a refusal: the file is for the card you upgraded away from.
bool file_belongs_to_backup(const std::string& dir, const std::string& file);

/// Does the writes file `file` belong to `card` (its header's size and mtime or digest)? Nothing is bound.
bool file_binds_to_card(const std::string& card, const std::string& file);

/// Why an overlay cannot be carried, "" when every existing one can: it is not a writes file, it was made
/// for another card, or it rewrites the partition table or the boot sector (a card with such a file is not
/// the card jnext made). Reads the files' headers only.
std::string overlay_problem(const std::string& card, const std::vector<std::string>& files);

/// The distribution (zip name) of the card in previous/ (24.11 without a sidecar).
std::string backup_distribution(const std::string& dir);

/// The free space the whole upgrade needs under `dir`: the zip, the new raw, the new card, the
/// old card as made, plus the overlays (a bound over-estimate: three images and the zip).
uint64_t space_needed(const std::string& card, uint64_t zip_bytes, const std::vector<std::string>& overlays);

/// Carry the user's files from the current card onto `plan.new_card` (direct layer), then rebuild
/// each overlay as "<FILE>.new" bound to the new card's final identity (overlay layer), and verify
/// all of it. The old card, raw and overlays are only read. On failure `err` says why and every
/// "<FILE>.new" it made is removed (the caller removes the work directory). `report` is filled.
bool carry_over(const Plan& plan, sdcard::carry::Report& report, std::string& err);

/// The switch: writes the plan to <work>/SWITCHING, then runs the steps (see the .cpp). From the
/// first step on the only recovery is forward. `report_text` goes into previous/.
bool switch_set(const Plan& plan, const std::string& report_text, std::string& err);
/// Crash recovery, at the start of every provisioning: SWITCHING present = finish the switch;
/// a work directory without it = delete it (the old set was never touched). `note` says what was
/// done (empty when nothing). True unless a roll-forward itself failed.
bool recover(const std::string& dir, std::string& note, std::string& err);

/// Test seam: when set, the named switch step (1..6) fails with an error, once, as a crash would
/// stop it. 0 = off.
extern int g_fail_switch_step_for_test;

}  // namespace sdcard::upgrade
