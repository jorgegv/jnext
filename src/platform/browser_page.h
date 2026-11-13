#pragma once
#include "core/cli_options.h"
#include <cctype>
#include <string>

// What a browser page refuses, beyond cli::browser_refusal (the build's own
// table): flags and program formats that cannot work in a page. Pure, no
// Emscripten include.
namespace browser_page {

inline const char* option_refusal(cli::OptId id)
{
    switch (id) {
    case cli::OptId::Headless:
        return "not available in a browser page (it would block the tab for ever)";
    case cli::OptId::RzxPlay:
    case cli::OptId::RzxRecord:
        return "RZX is not available in a browser page";
    default:
        return nullptr;
    }
}

/// nullptr when `path` is a format the browser page loads (.nex .tap .tzx programs,
/// .jns .sna .szx .z80 snapshots, any case), else the reason. Keep `.nex, .tap, .tzx`
/// as the reason's prefix: the rows that pin a refusal match it.
inline const char* program_refusal(const std::string& path)
{
    const auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".nex" || ext == ".tap" || ext == ".tzx" ||
            ext == ".jns" || ext == ".sna" || ext == ".szx" || ext == ".z80") return nullptr;
    }
    return "a browser page loads only .nex, .tap, .tzx, .jns, .sna, .szx, .z80 programs";
}

}  // namespace browser_page
