#pragma once

// GH #420 - where the Source tab looks for the source file a map names.
//
// Qt-free, so the same walk runs in the Linux, SDL-only and Windows trees and
// so a later surface that prints a resolved path (GH #346) can call it. The
// list of directories lives in the Qt debugger manager; this file only walks.

#include <string>
#include <string_view>
#include <vector>

namespace source_search {

/// Separates directories in the one-string spelling of the list (jnext.conf,
/// --set paths.source_dirs): ':' where a path may not hold one, ';' on Windows
/// where a drive letter does.
#ifdef _WIN32
inline constexpr char kListSeparator = ';';
#else
inline constexpr char kListSeparator = ':';
#endif

/// The file the Source tab shows for `recorded` (the name as the map store holds
/// it), or "" when there is none. In order:
///  1. an absolute `recorded` that exists;
///  2. a relative `recorded` joined to the directory of `map_file` (when not
///     empty) and then to each of its parents;
///  3. `dirs`, trying the longest trailing part of `recorded` first (all of it,
///     then all but its first component, ... then its file name alone), and
///     within one length the directories in the order given.
/// A candidate must be a regular file. Leading ".." components of `recorded`
/// are dropped for step 3, so a search never leaves the directory it names.
std::string resolve(const std::string& recorded, const std::string& map_file,
                    const std::vector<std::string>& dirs);

/// The directory to add to the list so that resolve() finds `picked` for
/// `recorded`: `picked` without the trailing components it shares with
/// `recorded`. "" when the file names differ (case-sensitive).
std::string root_for(const std::string& recorded, const std::string& picked);

/// "a:b" -> {"a","b"}; "" -> {}. Empty entries are dropped.
std::vector<std::string> split_list(std::string_view text);

/// The inverse of split_list().
std::string join_list(const std::vector<std::string>& dirs);

} // namespace source_search
