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

bool program_loads_later(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".tap" || ext == ".tzx" || ext == ".wav";
}
