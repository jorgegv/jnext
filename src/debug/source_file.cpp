#include "debug/source_file.h"

#include <filesystem>

std::string source_file_problem(const std::string& path)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size > SOURCE_FILE_MAX_BYTES)
        return "file is " + std::to_string(size) + " bytes, over the "
             + std::to_string(SOURCE_FILE_MAX_BYTES) + "-byte limit";
    return {};
}
