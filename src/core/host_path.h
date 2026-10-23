#pragma once

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace jnext {

/// UTF-8 in, UTF-8 out: `path` made absolute against the current directory
/// and lexically normalised. Symlinks are NOT resolved (the name the user
/// chose is kept) and the file need not exist. Native separators.
/// Never throws: on any error returns `path` unchanged.
///
/// Explicit u8path/u8string, because jnext's narrow strings are UTF-8 on every
/// platform and fs::path(std::string) / .string() use the narrow code page on
/// Windows (GH #401).
inline std::string absolute_host_path(const std::string& path)
{
    try {
        std::error_code ec;
        const std::filesystem::path abs =
            std::filesystem::absolute(std::filesystem::u8path(path), ec);
        if (ec) return path;
        return abs.lexically_normal().u8string();   // std::string in C++17
    } catch (const std::exception&) {
        return path;
    }
}

}  // namespace jnext
