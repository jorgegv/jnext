#include "debug/source_file.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

std::string source_file_problem(const std::string& path)
{
    // A FIFO or device would block or never end; a missing file is left to
    // the open, which reports it.
    std::error_code ec;
    const auto st = std::filesystem::status(path, ec);
    if (!ec && std::filesystem::exists(st) && !std::filesystem::is_regular_file(st))
        return "not a regular file";
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size > SOURCE_FILE_MAX_BYTES)
        return "file is " + std::to_string(size) + " bytes, over the "
             + std::to_string(SOURCE_FILE_MAX_BYTES) + "-byte limit";
    return {};
}

namespace {

// The one table of program formats the debug sidecars care about: by extension (case
// ignored), whether the machine loads the program over the frames after the load call
// (the tape formats), and whether a z88dk `<stem>.map` is looked for beside it.
struct ProgramFormat {
    const char* ext;
    bool        loads_later;
    bool        z88dk_map;
};
constexpr ProgramFormat kProgramFormats[] = {
    {".nex", false, true},
    {".sna", false, true},
    {".tap", true,  true},
    {".tzx", true,  false},
    {".wav", true,  false},
};

const ProgramFormat* program_format(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const ProgramFormat& f : kProgramFormats)
        if (ext == f.ext) return &f;
    return nullptr;
}

}  // namespace

bool program_loads_later(const std::string& path)
{
    const ProgramFormat* f = program_format(path);
    return f && f->loads_later;
}

bool program_has_z88dk_map(const std::string& path)
{
    const ProgramFormat* f = program_format(path);
    return f && f->z88dk_map;
}
