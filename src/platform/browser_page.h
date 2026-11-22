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
    case cli::OptId::RzxRecord:
        return "not available in a browser page (record from JavaScript: player.startRzxRecording(), "
               "player.stopRzxRecording())";
    default:
        return nullptr;
    }
}

/// nullptr when `path` is a format the browser page loads (.nex .tap .tzx .wav programs, .rzx recordings,
/// .jns .sna .szx .z80 snapshots, any case), else the reason. The reason starts
/// "a browser page loads only .nex, .tap, .tzx, " (BRR-05 pins that prefix; the guide quotes
/// the text); later formats are appended. BRR-03, WEBAPI-11 and BRW-23 match the
/// ".nex, .tap, .tzx" substring.
inline const char* program_refusal(const std::string& path)
{
    const auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".nex" || ext == ".tap" || ext == ".tzx" ||
            ext == ".jns" || ext == ".sna" || ext == ".szx" || ext == ".z80" || ext == ".wav" ||
            ext == ".rzx") return nullptr;
    }
    return "a browser page loads only .nex, .tap, .tzx, .jns, .sna, .szx, .z80, .wav, .rzx programs";
}

/// True when the member a `.zip` page file would load is a `.nex` (any case): the one archive
/// a browser page accepts, because a NEX also gets its directory in the archive (GH #541).
inline bool archive_program_allowed(const std::string& member)
{
    const auto slash = member.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? member : member.substr(slash + 1);
    const auto dot = base.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = base.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".nex";
}

}  // namespace browser_page
