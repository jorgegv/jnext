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

/// The newest `.jns` regular file directly inside `dir`: greatest
/// modification time, ties to the greater name stem (so `...-HHMMSS-02` beats
/// `...-HHMMSS`). Empty with `why` set if there is none. Never throws.
std::string latest_quick_snapshot(const std::string& dir, std::string& why);
