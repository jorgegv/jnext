#pragma once

// ---------------------------------------------------------------------------
// GH #323 — the machine a .sna / .szx / .z80 snapshot was saved on.
//
// THE single table that identifies it: the three snapshot loaders
// (Emulator::load_sna/szx/z80), the frontends' boot-machine choice
// (emulator_boot_machine()) and RZX playback (rzx::recorded_machine(), for the
// snapshot a recording embeds) all read it, so a file means the same machine
// everywhere.
//
// Oracles: libspectrum 1.5.0 (sna.c:80-88, szx.c:41-58 and :2613-2680, z80.c
// :270-285, :320-352, :405-565) for what each field says; the tbblue firmware
// (boot.c, menu.def) and ports.txt for what the Next can be.
//
// The table lives in snapshot_machine.cpp (jnext_core); this header only
// declares it, so a change to one entry rebuilds one file.
// ---------------------------------------------------------------------------

#include "core/emulator_config.h"   // MachineType

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The verdict on one snapshot file: the machine to run it on, or why jnext
/// cannot run it. `why` is empty when `ok`.
struct SnapshotMachine {
    bool        ok      = false;
    MachineType machine = MachineType::ZX48K;
    std::string why;
};

/// The machine `bytes` (a snapshot of type `ext`: "sna", "szx" or "z80", lower
/// case) was saved on, or the reason jnext cannot run it.
SnapshotMachine snapshot_machine(const std::vector<uint8_t>& bytes, const std::string& ext);

/// The lower-case extension of `path` without the dot ("" when none).
inline std::string snapshot_file_ext(const std::string& path) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

/// snapshot_machine() of the file at `path`. The type is `ext` when given (a
/// loader knows which format it was asked for, whatever the file is called),
/// else the path's extension, case-insensitively. Unreadable -> not ok.
SnapshotMachine snapshot_file_machine(const std::string& path, const std::string& ext = {});

/// True when `path` is a snapshot jnext can load: snapshot_file_machine() says
/// ok AND the type's own loader parses it (snapshot_machine.cpp). On false,
/// `why` says why. Touches no machine.
bool snapshot_file_loadable(const std::string& path, std::string& why);
