#pragma once

#include <cstdint>
#include <string>

/// The largest SLD or Memory.txt file loaded. Real builds write a few MB at
/// most; the cap keeps a wrong file from taking minutes and hundreds of MB.
constexpr std::uintmax_t SOURCE_FILE_MAX_BYTES = 64u * 1024 * 1024;

/// Why `path` must not be loaded as an SLD or Memory.txt, checked before it
/// is opened (not a regular file, or over the cap), or an empty string when
/// it may be.
std::string source_file_problem(const std::string& path);

/// Whether the program at `path` is one whose bytes are not in memory when its
/// load call returns: the tape formats (.tap, .tzx, .wav, by extension, case
/// ignored), which the machine loads over the following frames.
bool program_loads_later(const std::string& path);
