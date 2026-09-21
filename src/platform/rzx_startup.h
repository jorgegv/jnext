#pragma once

// The command-line RZX requests, shared by the three frontends (HeadlessApp,
// QtApp, SdlApp) so that none of them carries its own copy.
//
// Kept out of platform/emulator_boot.h on purpose: these log, and core/log.h
// brings spdlog's `fmt` namespace with it, which emulator_boot.h's many
// includers (unit suites among them) must not inherit.

#include "core/emulator.h"
#include "core/log.h"

#include <string>

/// The command-line RZX requests — `--rzx-play FILE` (which `--load x.rzx` and a
/// bare `x.rzx` argument become in main.cpp) and `--rzx-record FILE` — for all
/// three frontends. Returns false when the playback file failed to load; the
/// caller then exits non-zero, the same contract as a failed `--load` (the
/// error is logged and the machine keeps running without the recording).
///
/// Every frontend calls this at the TOP OF ITS run(), never from init().
/// main.cpp calls set_rzx_play()/set_rzx_record() AFTER app.init(), so a
/// frontend that read them inside init() always saw them empty: QtApp did, and
/// command-line RZX playback and recording silently did nothing in the GUI.
/// run() is the one point that every setter main.cpp calls is bound to precede
/// (it blocks until the session ends), so reading them there cannot depend on
/// the order in which the setters and init() were called.
inline bool emulator_start_rzx(Emulator& emu, const std::string& play_file,
                               const std::string& record_file) {
    bool ok = true;
    if (!play_file.empty() && !emu.load_rzx(play_file)) {
        Log::platform()->error("RZX: failed to load '{}'", play_file);
        ok = false;
    }
    if (!record_file.empty())
        emu.start_rzx_recording(record_file);
    return ok;
}

/// Finish an RZX recording still running at exit — this is what writes the
/// file. Every frontend calls it from shutdown().
inline void emulator_finish_rzx(Emulator& emu) {
    if (emu.rzx_recorder().is_recording())
        emu.stop_rzx_recording();
}
