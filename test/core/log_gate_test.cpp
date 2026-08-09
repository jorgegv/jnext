// Log change-gate test (Task 24, 2026-07-12).
//
// Guest state changes must log ONLY when the value actually changes. See the
// level policy in src/core/log.h.
//
// The bug that started Task 24: NR 0x07 (CPU speed) logged on EVERY write, at
// info. Programs rewrite it constantly — some demos change speed every frame —
// so the console flooded. Demoting it to debug is only half the fix: ungated, it
// would still flood anyone running `--log-level emulator=debug`, which is
// exactly the person who needs to read the log.
//
// This drives a real Emulator through the real port path (OUT 0x243B / 0x253B)
// and counts the log lines the NR 0x07 write handler actually emits, by hanging
// a ringbuffer sink off the emulator logger.
//
// It lives in its own binary because nextreg_integration_test — the natural home
// — has a local `fmt()` helper that collides with spdlog's `fmt` namespace.
//
// LOGHOT-01..07 were added later and guard the OTHER half of the same concern:
// the hot-path should_log() gates on NextREG and port access must skip the cost
// when the level is off WITHOUT ever suppressing a line when it is on.
//
// Run: ./build/test/log_gate_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

/// Write a NextREG through the real port path, exactly as Z80 code would.
void nr_write(Emulator& emu, uint8_t reg, uint8_t val)
{
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

}  // namespace

int main()
{
    std::printf("Log change-gate (Task 24) — guest state changes log only on CHANGE\n");
    std::printf("====================================================\n\n");

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);

    // Capture what the emulator logger emits, at debug (where the policy puts
    // guest-driven state changes).
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    auto logger = Log::emulator();
    logger->sinks().push_back(ring);
    logger->set_level(spdlog::level::debug);

    auto speed_lines = [&]() {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find("CPU speed changed") != std::string::npos) ++n;
        return n;
    };

    char detail[192];

    // --- GATE-01: rewriting the SAME speed logs once, not once per write -------
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 MHz
    const int after_change = speed_lines();
    for (int i = 0; i < 20; i++) nr_write(emu, 0x07, 0x03);   // same value, x20
    const int after_repeats = speed_lines();
    std::snprintf(detail, sizeof(detail),
                  "lines after the change=%d (want 1), after 20 identical rewrites=%d "
                  "(want 1; ungated gives 21)", after_change, after_repeats);
    check("GATE-01", "20 rewrites of the SAME CPU speed log only once",
          after_change == 1 && after_repeats == 1, detail);

    // --- GATE-02: a genuine change still logs ----------------------------------
    nr_write(emu, 0x07, 0x01);                                // 28 -> 7 MHz
    std::snprintf(detail, sizeof(detail), "lines=%d want 2", speed_lines());
    check("GATE-02", "an actual change of CPU speed does log", speed_lines() == 2, detail);

    // --- GATE-03: the gate clears on reset -------------------------------------
    // NR 0x07 returns to its power-on 3.5 MHz across a reset, so a write back to
    // the pre-reset speed is a REAL change and must log. If the gate is not
    // cleared, that line is swallowed as "unchanged".
    nr_write(emu, 0x07, 0x03);                                // 7 -> 28 MHz  (3 lines)
    const int before_reset = speed_lines();
    emu.reset();
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 again
    std::snprintf(detail, sizeof(detail),
                  "lines before reset=%d, after the post-reset write=%d "
                  "(want %d; a swallowed write leaves it at %d)",
                  before_reset, speed_lines(), before_reset + 1, before_reset);
    check("GATE-03", "the gate clears on reset, so the first post-reset write logs",
          before_reset == 3 && speed_lines() == 4, detail);

    logger->sinks().pop_back();

    // ==========================================================================
    // LOGHOT-01..07 — the hot-path should_log() guards on NextREG and port
    // access must not SUPPRESS anything.
    //
    // PortDispatch::read/write and NextReg::read/write carry an unconditional
    // spdlog trace() call on every access. spdlog gates the level inside the
    // callee for the format-string-with-arguments overload, so with tracing off
    // that is still an out-of-line call per access — read() peaks at 929k/s.
    // The fix wraps each call in `if (logger->should_log(trace))`.
    //
    // The risk of that fix is not a wrong number, it is a debugging facility
    // that quietly stops working. So the rows that matter are the ENABLED ones:
    // with the level on, every call site must still emit its line, with the
    // same text as before. The OFF rows are the cheap direction.
    //
    // A bare PortDispatch is used rather than the Emulator above because two of
    // read()'s three branches — the default-read/floating-bus one and the
    // unhandled one — are not reachable through a booted machine, where every
    // port a running guest touches has a handler.
    {
        auto pring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
        auto plog  = Log::port();
        const auto plog_saved = plog->level();
        plog->sinks().push_back(pring);

        PortDispatch pd;
        pd.register_handler(0x00FF, 0x00FE,
                            [](uint16_t) -> uint8_t { return 0xA5; },
                            [](uint16_t, uint8_t) {});

        // Count ring lines containing `needle`. The ring has no clear API, so
        // every row below uses a needle unique to its own call site and the
        // cumulative contents do not matter.
        auto count_lines = [&](const char* needle) {
            int n = 0;
            for (const auto& line : pring->last_formatted())
                if (line.find(needle) != std::string::npos) ++n;
            return n;
        };

        // --- LOGHOT-01: matched-handler read still logs when trace is ON ------
        plog->set_level(spdlog::level::trace);
        pd.read(0x00FE);
        int matched = count_lines("IN  port=0x00fe \xe2\x86\x92 0xa5");
        std::snprintf(detail, sizeof(detail),
                      "matched-handler IN lines=%d want 1 (a suppressing guard gives 0)",
                      matched);
        check("LOGHOT-01", "PortDispatch::read logs the matched-handler line at trace",
              matched == 1, detail);

        // --- LOGHOT-02: default-read (floating bus) branch --------------------
        pd.set_default_read([](uint16_t) -> uint8_t { return 0x5A; });
        pd.read(0x1234);
        int floating = count_lines("(default/floating)");
        std::snprintf(detail, sizeof(detail),
                      "default/floating IN lines=%d want 1", floating);
        check("LOGHOT-02", "PortDispatch::read logs the default/floating line at trace",
              floating == 1, detail);

        // --- LOGHOT-03: unhandled branch --------------------------------------
        {
            PortDispatch bare;                 // no handlers, no default read
            bare.read(0x4321);
        }
        int unhandled = count_lines("(unhandled)");
        std::snprintf(detail, sizeof(detail),
                      "unhandled IN lines=%d want 1", unhandled);
        check("LOGHOT-03", "PortDispatch::read logs the unhandled line at trace",
              unhandled == 1, detail);

        // --- LOGHOT-04: write ---------------------------------------------------
        pd.write(0x00FE, 0x07);
        int wrote = count_lines("OUT port=0x00fe \xe2\x86\x90 0x07");
        std::snprintf(detail, sizeof(detail), "OUT lines=%d want 1", wrote);
        check("LOGHOT-04", "PortDispatch::write logs at trace", wrote == 1, detail);

        // --- LOGHOT-05: with the level OFF, none of the four sites emit --------
        // The ring keeps everything logged so far, so measure the DELTA.
        const size_t before = pring->last_formatted().size();
        plog->set_level(spdlog::level::info);
        pd.read(0x00FE);
        pd.read(0x1234);
        pd.write(0x00FE, 0x07);
        { PortDispatch bare; bare.read(0x4321); }
        const size_t after = pring->last_formatted().size();
        std::snprintf(detail, sizeof(detail),
                      "ring grew by %zu want 0", after - before);
        check("LOGHOT-05", "no port trace line is emitted with the level off",
              after == before, detail);

        plog->set_level(plog_saved);
        plog->sinks().pop_back();
    }

    {
        auto nring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
        auto nlog  = Log::nextreg();
        const auto nlog_saved = nlog->level();
        nlog->sinks().push_back(nring);

        auto count = [&](const char* needle) {
            int n = 0;
            for (const auto& line : nring->last_formatted())
                if (line.find(needle) != std::string::npos) ++n;
            return n;
        };

        // --- LOGHOT-06: NextReg read AND write both log when trace is ON -------
        nlog->set_level(spdlog::level::trace);
        emu.nextreg().write(0x07, 0x00);
        (void)emu.nextreg().read(0x07);
        const int nr_writes = count("NextREG write reg=0x07");
        const int nr_reads  = count("NextREG read  reg=0x07");
        std::snprintf(detail, sizeof(detail),
                      "write lines=%d (want 1), read lines=%d (want 1); "
                      "a suppressing guard gives 0/0", nr_writes, nr_reads);
        check("LOGHOT-06", "NextReg::write and NextReg::read both log at trace",
              nr_writes == 1 && nr_reads == 1, detail);

        // --- LOGHOT-07: with the level off, neither emits ----------------------
        const size_t before = nring->last_formatted().size();
        nlog->set_level(spdlog::level::info);
        emu.nextreg().write(0x07, 0x01);
        (void)emu.nextreg().read(0x07);
        const size_t after = nring->last_formatted().size();
        std::snprintf(detail, sizeof(detail),
                      "ring grew by %zu want 0", after - before);
        check("LOGHOT-07", "no NextREG trace line is emitted with the level off",
              after == before, detail);

        nlog->set_level(nlog_saved);
        nlog->sinks().pop_back();
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
