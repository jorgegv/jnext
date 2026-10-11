#pragma once

#include <ctime>
#include <string>

// GH #321 — Quick Save / Quick Load snapshot helpers (no Qt, no SDL; both
// frontends and the tests share them).

/// The folder quick snapshots live in: `<state dir>/snapshots`, the state dir
/// being sdcard::default_state_dir() ($JNEXT_CONFIG_DIR, else ~/.jnext).
std::string default_quick_snapshot_dir();

/// A collision-free `<dir>/jnext-YYYYMMDD-HHMMSS[-NN].jns` (local time), the
/// directory created on demand. Empty if the directory cannot be created or
/// no free name exists. Same naming function as the quick screenshots.
std::string auto_quick_snapshot_path(const std::string& dir, std::time_t when);

/// How many quick snapshots Quick Save keeps (`--quick-snapshot-keep`, the
/// `[snapshot] quick_keep` setting and the Preferences control): the newest N;
/// 0 means unlimited.
constexpr int kQuickSnapshotKeepDefault   = 20;
constexpr int kQuickSnapshotKeepUnlimited = 0;

/// Does `name` (a bare file name) match jnext's own quick-snapshot pattern,
/// `jnext-YYYYMMDD-HHMMSS[-NN].jns` exactly (lower-case, digits only)?
bool is_quick_snapshot_name(const std::string& name);

/// Delete the OLDEST quick snapshots beyond `keep` in `dir` (oldest = least
/// recent modification time, ties to the lesser name stem), and ONLY regular
/// files whose names match is_quick_snapshot_name(): a file the user put there
/// under another name, a directory, anything else is never touched.
/// A symbolic link is never deleted, whatever it is named or points at.
/// keep <= 0 (unlimited) deletes nothing. Returns the number deleted.
int prune_quick_snapshots(const std::string& dir, int keep);

/// The newest `.jns` regular file directly inside `dir`: greatest
/// modification time, ties to the greater name stem (so `...-HHMMSS-02` beats
/// `...-HHMMSS`). Empty with `why` set if there is none. Never throws.
std::string latest_quick_snapshot(const std::string& dir, std::string& why);
