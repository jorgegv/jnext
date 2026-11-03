#pragma once

#include <cstdint>
#include <istream>
#include <string>

/// The largest SLD or Memory.txt file loaded. Real builds write a few MB at
/// most; the cap keeps a wrong file from taking minutes and hundreds of MB.
constexpr std::uintmax_t SOURCE_FILE_MAX_BYTES = 64u * 1024 * 1024;

/// Why `path` must not be loaded as an SLD or Memory.txt, checked before it
/// is opened (not a regular file, or over the cap), or an empty string when
/// it may be.
std::string source_file_problem(const std::string& path);

/// Skips a UTF-8 byte-order mark (EF BB BF) at the very start of a freshly opened
/// regular-file stream, so a text loader reads the file as if the mark were not
/// there. A mark anywhere else is ordinary text. Without one the stream is left at 0.
void skip_utf8_bom(std::istream& in);

/// Whether the program at `path` is one whose bytes are not in memory when its
/// load call returns: the tape formats (.tap, .tzx, .wav, by extension, case
/// ignored), which the machine loads over the following frames.
bool program_loads_later(const std::string& path);

/// Whether a z88dk `<stem>.map` is a sidecar of the program at `path`: a `.nex`,
/// `.tap` or `.sna` (by extension, case ignored; design 6.1).
bool program_has_z88dk_map(const std::string& path);
