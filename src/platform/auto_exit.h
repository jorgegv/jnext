#pragma once

// --delayed-automatic-exit(-frames) is a hard bound: it always fires. What it
// must not do is cut off command-line work that was deferred to a later frame
// and has not happened yet — a --load waiting out its boot delay, an --inject
// with --inject-delay, a --rzx-record waiting for that load, a
// --delayed-keypress or --delayed-nmi still to come, a --joy-uart-rx stream
// still in its --joy-uart-rx-delay-frames hold, an edge of the scheduled ESP
// outage (--esp-delayed-disassociate-frames / -associate-frames) not yet
// reached — and then report success:
// the run did not do what was asked, and exiting 0 hides it. The same contract
// as a --delayed-screenshot still outstanding at exit (see the frontends'
// shutdown()): an error, and the run exits non-zero.
//
// Shared by the three frontends (HeadlessApp, QtApp, SdlApp); each calls it
// where its automatic exit fires, with the work it holds itself (the machine's
// own scheduled work is read from the emulator here).

#include "core/emulator.h"
#include "core/log.h"
#include "peripheral/joy_uart_source.h"

#include <initializer_list>
#include <string>
#include <vector>

/// One piece of deferred command-line work: the option that asked for it,
/// what it names (a file, a key), and whether it is still to come.
struct DeferredWork {
    const char* option;
    std::string what;
    bool        pending;
};

/// Called as the automatic exit fires. Logs an error for each piece of
/// `frontend_work`, and of the scheduled work `emu` holds, still pending, and
/// returns false when there is any: the caller exits non-zero.
inline bool auto_exit_finds_no_deferred_work(const Emulator& emu,
                                             std::initializer_list<DeferredWork> frontend_work) {
    const EmulatorConfig& cfg  = emu.config();
    const JoyUartSource*  uart = emu.joy_uart_source();
    const int             down = cfg.esp_disassociate_frame, up = cfg.esp_associate_frame;
    std::vector<DeferredWork> work(frontend_work);
    // An ESP edge falls due in the frame whose esp_frames() equals it, and is
    // applied before that frame counts itself (advance_esp_schedule_frame()).
    work.push_back({"--joy-uart-rx", cfg.joy_uart_rx_file, uart && uart->waiting()});
    work.push_back({"--esp-delayed-disassociate-frames", std::to_string(down),
                    down >= 0 && emu.esp_frames() <= down});
    work.push_back({"--esp-delayed-associate-frames", std::to_string(up),
                    up >= 0 && emu.esp_frames() <= up});
    bool ok = true;
    for (const DeferredWork& w : work) {
        if (!w.pending) continue;
        Log::platform()->error(
            "{}: '{}' never happened — --delayed-automatic-exit fired first. "
            "Exiting non-zero.", w.option, w.what);
        ok = false;
    }
    return ok;
}
