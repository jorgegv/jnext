#include "core/snapshot_file.h"

#include "core/emulator.h"
#include "core/nex_saver.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

// Does `path` END with `suffix`, ignoring case? The extension question asked of
// the tail, so a dot in a DIRECTORY name ("/tmp/v1.szx/out") cannot answer it.
bool ends_with_ci(const std::string& path, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    if (path.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(
            std::tolower(static_cast<unsigned char>(path[path.size() - n + i])));
        if (a != suffix[i]) return false;
    }
    return true;
}

}  // namespace

bool save_snapshot_file(Emulator& emu, const std::string& path, std::string& error,
                        size_t& bytes_out) {
    error.clear();
    bytes_out = 0;
    if (path.empty()) {
        error = "no file name";
        return false;
    }

    // `.jns` assembles a manifest, an SD identity and blob declarations rather
    // than one flat buffer, so it writes its own file and reports its own
    // reason.
    if (ends_with_ci(path, ".jns")) {
        if (!emu.save_jns_file(path)) {
            error = emu.last_jns_error();
            return false;
        }
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        bytes_out = f ? static_cast<size_t>(f.tellg()) : 0;
        return true;
    }

    std::vector<uint8_t> data;
    if (ends_with_ci(path, ".szx")) {
        // A classic-Spectrum interchange format: 48K/128K/+2A/+3 only (see
        // SzxSaver's SCOPE); a Next is refused with a reason, not truncated.
        SzxSaver::SaveResult r = SzxSaver::save(emu);
        if (!r.ok) {
            error = r.error;
            return false;
        }
        data = std::move(r.data);
    } else if (ends_with_ci(path, ".nex")) {
        data = NexSaver::save(emu).data;
    } else {
        // GH #274 — the 48K form only; a machine it cannot represent is
        // refused with a reason.
        data = SnaSaver::save(emu, &error);
    }
    if (data.empty()) {
        if (error.empty()) error = "the saver produced no data";
        return false;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    f.close();
    if (!f) {
        error = "short write to '" + path + "'";
        return false;
    }
    bytes_out = data.size();
    return true;
}
