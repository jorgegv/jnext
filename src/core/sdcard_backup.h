#pragma once
// The backup an SD card upgrade leaves in <sdcard dir>/previous/ (GH #555 S6): its state, what the
// menus offer for it, and the two verbs that act on it: go back to the previous card (a swap: the
// current set becomes the backup) and delete the backup.
#include <cstdint>
#include <string>

namespace sdcard::backup {

/// How the default card's distribution relates to the one jnext downloads now.
enum class Relation { Older, Current, Newer, Unknown };

/// -1, 0, 1 for distribution a older than, equal to, newer than b (zip names such as
/// "sn-emulator-26.11a.zip"); 2 when either has no recognisable version.
int compare_distributions(const std::string& a, const std::string& b);

/// Everything the menus and the verbs need to know about the card set at the default location.
struct Status {
    bool        card_present = false;
    std::string card_distro;       ///< the card's distribution (24.11 when it has no sidecar)
    Relation    relation = Relation::Unknown;
    bool        pristine = false;  ///< the card can be compared with the one it came from
    std::string pristine_why;      ///< the first reason when not
    bool        backup = false;    ///< <dir>/previous/ holds a card
    uint64_t    backup_bytes = 0;  ///< the sum of the files of previous/
    std::string backup_distro;     ///< the distribution of the backup card
};

/// The state of the set in `dir` against `current_distro` (what jnext downloads now).
Status card_set_status(const std::string& dir, const std::string& current_distro);

/// What the three File-menu entries do, from a status and whether slot 0 holds the default card.
struct MenuState {
    bool     upgrade_enabled = false;   ///< slot 0 holds the default card and it is older
    bool     go_back_enabled = false;   ///< a backup exists
    bool     delete_enabled = false;    ///< a backup exists
    uint64_t backup_bytes = 0;
};
MenuState menu_state(const Status& s, bool slot0_is_default);

/// "1.9 GB", "340 MB", "12 KB": the size in the delete entry's label.
std::string size_label(uint64_t bytes);

enum class Result { Ok, NoBackup, Failed };

/// Swap the current set and the backup (the card, original image, sidecars and overlays); records
/// that the distribution just left is not to be offered again. `err` says why on Failed, and nothing
/// has changed then. A crash part-way is finished by recover() at the next start.
Result go_back(const std::string& dir, std::string& err);
/// Remove <dir>/previous/ and nothing else.
Result delete_backup(const std::string& dir, std::string& err);
/// Finish an interrupted go_back (a marker in <dir>/go-back-work/); true unless that failed.
bool recover(const std::string& dir, std::string& err);

/// Test seam: the go_back step (1..4) that fails once, as a crash would. 0 = off.
extern int g_fail_step_for_test;

}  // namespace sdcard::backup
