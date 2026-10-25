#pragma once

// ---------------------------------------------------------------------------
// File > Recent Files list logic (GH #406), kept free of Qt so a Linux unit
// test reaches every rule; MainWindow holds the QStringList that
// AppConfigData persists and calls these on its std::string copy.
//
// The list is most-recent-first and capped at kMax. Two spellings of one file
// (a symlink, "a/../b") are one entry: entries are compared by key(), which is
// the canonical path while the file exists and the lexically normalised
// absolute path when it does not (a vanished file must still be removable).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace recent_files {

inline constexpr std::size_t kMax = 10;

using List = std::vector<std::string>;

/// The absolute, lexically normalised form that is stored and shown.
inline std::string normalise(const std::string& path)
{
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(path, ec);
    if (ec) return path;
    return p.lexically_normal().string();
}

/// The identity two spellings of one file share.
inline std::string key(const std::string& path)
{
    std::error_code ec;
    std::filesystem::path c = std::filesystem::canonical(path, ec);
    return ec ? normalise(path) : c.string();
}

/// Record a successful load: `path` goes to the front, any earlier spelling of
/// the same file is dropped, and the list is cut to kMax.
inline void push(List& list, const std::string& path)
{
    if (path.empty()) return;
    const std::string k = key(path);
    for (auto it = list.begin(); it != list.end();) {
        if (key(*it) == k) it = list.erase(it);
        else ++it;
    }
    list.insert(list.begin(), normalise(path));
    if (list.size() > kMax) list.resize(kMax);
}

/// Drop `path` (any spelling of it) from the list.
inline void remove(List& list, const std::string& path)
{
    const std::string k = key(path);
    for (auto it = list.begin(); it != list.end();) {
        if (key(*it) == k) it = list.erase(it);
        else ++it;
    }
}

/// What a load dialog is seeded with: the most recent file, when it still
/// exists and sits in `last_dir` (or `last_dir` is empty), so the dialog opens
/// with that file selected; otherwise `last_dir` itself (the pre-#406 seed).
/// A file recorded from another directory is not offered, because the user's
/// last browse (last_dir) has moved on from it.
inline std::string dialog_seed(const List& list, const std::string& last_dir)
{
    if (list.empty()) return last_dir;
    const std::string& f = list.front();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(f, ec)) return last_dir;
    if (!last_dir.empty() &&
        key(std::filesystem::path(f).parent_path().string()) != key(last_dir))
        return last_dir;
    return f;
}

}  // namespace recent_files
