#pragma once

#include <cstdint>
#include <string>

/// The largest SLD or Memory.txt file loaded. Real builds write a few MB at
/// most; the cap keeps a wrong file from taking minutes and hundreds of MB.
constexpr std::uintmax_t SOURCE_FILE_MAX_BYTES = 64u * 1024 * 1024;

/// Why `path` must not be loaded as an SLD or Memory.txt, checked before it
/// is opened, or an empty string when it may be.
std::string source_file_problem(const std::string& path);
