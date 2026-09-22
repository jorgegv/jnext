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
// LOGHOT-08..23 extend the same guard to the Z80N opcode path, the Copper, and
// the per-access trace/debug calls of the DMA, UART, CTC and SPI (GH #244).
//
// Run: ./build/test/log_gate_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "peripheral/copper.h"
#include "peripheral/ctc.h"
#include "peripheral/dma.h"
#include "peripheral/spi.h"
#include "peripheral/uart.h"
#include "platform/emulator_boot.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstddef>
#include <cstdio>
#include <initializer_list>
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

/// A ring sink hung off one logger for the life of a scope, with the logger's
/// level restored on the way out. count() counts the captured lines containing
/// a needle; the ring is large enough that no row here overruns it.
struct LogTap {
    std::shared_ptr<spdlog::logger>                     log;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt>  ring;
    spdlog::level::level_enum                           saved;

    explicit LogTap(std::shared_ptr<spdlog::logger> l)
        : log(std::move(l)),
          ring(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(4096)),
          saved(log->level()) { log->sinks().push_back(ring); }
    ~LogTap() { log->set_level(saved); log->sinks().pop_back(); }

    void level(spdlog::level::level_enum lv) { log->set_level(lv); }
    size_t size() const { return ring->last_formatted().size(); }
    int count(const char* needle) const {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find(needle) != std::string::npos) ++n;
        return n;
    }
};

/// An SPI slave that answers every byte with its complement.
struct EchoSpiDevice : SpiDevice {
    uint8_t exchange(uint8_t tx) override { return static_cast<uint8_t>(~tx); }
};

/// "name=count " for each needle, for a row's failure detail.
std::string counts(const LogTap& t, std::initializer_list<const char*> needles)
{
    std::string out;
    for (const char* n : needles)
        out += std::string("[") + n + "]=" + std::to_string(t.count(n)) + " ";
    return out;
}

/// True when every needle was captured at least once.
bool all_seen(const LogTap& t, std::initializer_list<const char*> needles)
{
    for (const char* n : needles)
        if (t.count(n) == 0) return false;
    return true;
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

    // --- GATE-03: the gate clears on a hard reset -------------------------------
    // NR 0x07 returns to its power-on 3.5 MHz across a reset, so a write back to
    // the pre-reset speed is a REAL change and must log. If the gate is not
    // cleared, that line is swallowed as "unchanged". Driven through the
    // production hard reset (GH #239): the frontend cold boot, which
    // reconstructs the Emulator.
    nr_write(emu, 0x07, 0x03);                                // 7 -> 28 MHz  (3 lines)
    const int before_reset = speed_lines();
    emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 again
    std::snprintf(detail, sizeof(detail),
                  "lines before reset=%d, after the post-reset write=%d "
                  "(want %d; a swallowed write leaves it at %d)",
                  before_reset, speed_lines(), before_reset + 1, before_reset);
    check("GATE-03", "the gate clears on a hard reset (frontend cold boot), so the "
          "first post-reset write logs",
          before_reset == 3 && speed_lines() == 4, detail);

    // --- LOGGATE-04: ... and on a soft reset -----------------------------------
    // NR 0x07 is in the `reset` flip-flop domain (zxnext.vhd:1300 reset "00";
    // the one reset wire covers soft as well as hard, zxnext_top_issue2.vhd:840),
    // so a SOFT reset (NR 0x02 bit 0, the guest path) also returns it to
    // 3.5 MHz. Before GH #239 the gate was cleared only by the in-place
    // Emulator::reset(), which a soft reset never called: the machine came back
    // at 3.5 MHz while the gate still said 28, and the guest's write back to
    // 28 MHz was swallowed.
    const int before_soft = speed_lines();                    // 28 MHz again (4 lines)
    nr_write(emu, 0x02, 0x01);                                // RESET_SOFT
    const uint8_t speed_after_soft = emu.nextreg().read(0x07) & 0x03;
    nr_write(emu, 0x07, 0x03);                                // 3.5 -> 28 again
    std::snprintf(detail, sizeof(detail),
                  "NR 0x07 speed after soft reset=%u (want 0), lines before=%d "
                  "after the post-reset write=%d (want %d; a swallowed write leaves %d)",
                  speed_after_soft, before_soft, speed_lines(), before_soft + 1,
                  before_soft);
    check("LOGGATE-04", "the gate clears on a soft reset too (NR 0x07 returns to "
          "3.5 MHz, zxnext.vhd:1300), so the first post-reset write logs",
          before_soft == 4 && speed_after_soft == 0 && speed_lines() == 5, detail);

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

    // ==========================================================================
    // LOGHOT-08..23 — GH #244: the remaining per-access log calls get the same
    // should_log() guard as LOGHOT-01..07, and the same two directions of proof.
    //
    // THE ROWS THAT MATTER ARE THE ENABLED ONES: a guard that silences a line
    // while its level is on is a debugging facility quietly lost. Each enabled
    // row drives every guarded call site of its group once, with ONLY that
    // group's logger raised, and requires every site's line. Trace-level sites
    // are driven at exactly trace; debug-level sites at exactly debug, so a
    // guard written with the wrong level (should_log(trace) around a debug())
    // is caught too. The level-off rows are the cheap direction.
    //
    // Every guard was mutation-tested in the suppressing direction (its
    // condition replaced by `false`) and each turned its group's enabled row
    // red; see the GH #244 commit for the list.

    // --- LOGHOT-08 / 09: the Z80N opcode trace (per Z80N opcode) --------------
    {
        LogTap t(Log::cpu());
        auto run_swapnib = [&]() {
            emu.mmu().write(0x8000, 0xED);           // SWAPNIB = ED 23
            emu.mmu().write(0x8001, 0x23);
            Z80Registers r = emu.cpu().get_registers();
            r.PC = 0x8000;
            emu.cpu().set_registers(r);
            (void)emu.cpu().execute();
        };
        t.level(spdlog::level::trace);
        run_swapnib();
        const int on = t.count("Z80N opcode ED 0x23 at PC=0x8000");
        std::snprintf(detail, sizeof(detail), "lines=%d want 1 (a suppressing guard gives 0)", on);
        check("LOGHOT-08", "a Z80N opcode logs its trace line with cpu at trace",
              on == 1, detail);

        const size_t before = t.size();
        t.level(spdlog::level::info);
        run_swapnib();
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-09", "no Z80N opcode trace line is emitted with the level off",
              t.size() == before, detail);
    }

    // --- LOGHOT-10..13: the Copper (per WAIT, per MOVE, per upload byte) ------
    // A two-instruction program — WAIT(line 0, h 0) then MOVE NR 0x40 = 0x55 —
    // uploaded through all five upload paths: NR 0x61 (address), NR 0x63
    // (stored byte, then word) for the WAIT, NR 0x60 (MSB, then LSB) for the
    // MOVE.
    {
        LogTap t(Log::copper());
        NextReg nr;
        auto upload_and_run = [&](Copper& cop) {
            cop.write_reg_0x62(0x00);          // stop, address high = 0
            cop.write_reg_0x61(0x00);          // address low = 0
            cop.write_reg_0x63(0x80);          // word 0 MSB (stored)
            cop.write_reg_0x63(0x00);          // word 0 = 0x8000 WAIT(v=0, h=0)
            cop.write_reg_0x60(0x40);          // word 1 MSB
            cop.write_reg_0x60(0x55);          // word 1 = 0x4055 MOVE NR 0x40,0x55
            cop.write_reg_0x62(0x40);          // mode 01: run from 0
            cop.execute(0, 0, nr);             // the mode-change edge executes nothing
            cop.execute(12, 0, nr);            // hc 12 >= (0<<3)+12: WAIT satisfied
            cop.execute(13, 0, nr);            // MOVE
        };
        t.level(spdlog::level::trace);
        Copper cop;
        upload_and_run(cop);
        const bool wait_ok = t.count("WAIT satisfied at cvc=0") == 1;
        const bool move_ok = t.count("MOVE nextreg[0x40] = 0x55") == 1;
        std::string d = counts(t, {"WAIT satisfied at cvc=0", "MOVE nextreg[0x40] = 0x55"});
        check("LOGHOT-10", "a satisfied Copper WAIT logs its trace line with copper at trace",
              wait_ok, d);
        check("LOGHOT-11", "a Copper MOVE logs its trace line with copper at trace",
              move_ok, d);
        const std::initializer_list<const char*> upload = {
            "reg 0x61: write_addr low", "reg 0x63: stored data = 0x80",
            "reg 0x63: write word", "reg 0x60: write MSB", "reg 0x60: write LSB"};
        check("LOGHOT-12", "each of the five Copper upload paths (NR 0x61, NR 0x63 "
              "stored/word, NR 0x60 MSB/LSB) logs its trace line at trace",
              all_seen(t, upload), counts(t, upload));

        const size_t before = t.size();
        t.level(spdlog::level::info);
        Copper quiet;
        upload_and_run(quiet);
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-13", "no Copper trace or debug line is emitted with the level off",
              t.size() == before, detail);
    }

    // --- LOGHOT-14 / 15: the DMA's per-port-access traces ---------------------
    // WR0 0x79 announces port-A-low/high and length-low/high parameter bytes
    // (four sub-bytes); 0xC6 matches no base register; 0xFF is a WR6 command
    // the Z80-DMA does not define; and a port read.
    {
        LogTap t(Log::dma());
        auto drive = [](Dma& dma) {
            dma.write(0x79, false);
            for (uint8_t b : {0x00, 0x80, 0x10, 0x00}) dma.write(b, false);
            dma.write(0xC6, false);
            dma.write(0xFF, false);
            (void)dma.read();
        };
        t.level(spdlog::level::trace);
        Dma dma;
        drive(dma);
        const std::initializer_list<const char*> sites = {
            "DMA wr_seq sub-byte", "DMA write: unrecognized base byte 0xc6",
            "R6: unhandled command 0xff", "DMA read:"};
        check("LOGHOT-14", "each DMA per-access trace (parameter byte, unrecognized "
              "base byte, unhandled WR6 command, port read) logs at trace",
              all_seen(t, sites) && t.count("DMA wr_seq sub-byte") == 4,
              counts(t, sites));

        const size_t before = t.size();
        t.level(spdlog::level::info);
        Dma quiet;
        drive(quiet);
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-15", "no DMA trace line is emitted with the level off",
              t.size() == before, detail);
    }

    // --- LOGHOT-16..18: the UART's per-byte and per-port-read calls -----------
    {
        LogTap t(Log::uart());
        t.level(spdlog::level::trace);
        Uart u;
        u.write(3, 0x41);                      // 0x133B TX byte
        u.inject_rx(0, 0x5A);                  // a received byte
        (void)u.read(0);                       // 0x143B RX
        (void)u.read(1);                       // 0x153B select
        (void)u.read(2);                       // 0x163B frame
        (void)u.read(3);                       // 0x133B status
        const std::initializer_list<const char*> trace_sites = {
            "TX write 0x41, FIFO size=", "RX inject 0x5a (err=0)",
            "RX read 0x5a, FIFO size=", "ch0 RX read 0x5a", "ch0 select read",
            "ch0 frame read", "ch0 status read"};
        check("LOGHOT-16", "each UART per-byte / per-port-read trace (TX byte, RX "
              "inject, RX byte, and the RX/select/frame/status port reads) logs at trace",
              all_seen(t, trace_sites), counts(t, trace_sites));

        // The debug-level ones, at exactly debug: a TX byte, and the two
        // FIFO-full drops (64-byte TX FIFO, 512-byte RX FIFO, neither drained
        // without tick()).
        Uart full;
        t.level(spdlog::level::info);          // fill both FIFOs quietly...
        for (int i = 0; i < 64; ++i) full.write(3, 0x20);
        for (int i = 0; i < 512; ++i) full.inject_rx(0, 0x11);
        t.level(spdlog::level::debug);         // ...then overflow each at debug
        full.write(3, 0xEE);
        full.inject_rx(0, 0x22);
        const std::initializer_list<const char*> debug_sites = {
            "ch0 TX write 0xee", "TX FIFO full, byte 0xee dropped",
            "RX FIFO overflow, byte 0x22 dropped"};
        check("LOGHOT-17", "each UART per-byte debug line (TX byte, TX FIFO full, RX "
              "FIFO overflow) logs at debug",
              all_seen(t, debug_sites), counts(t, debug_sites));

        const size_t before = t.size();
        t.level(spdlog::level::info);
        Uart quiet;
        quiet.write(3, 0x41);
        quiet.inject_rx(0, 0x5A);
        for (int reg = 0; reg < 4; ++reg) (void)quiet.read(reg);
        for (int i = 0; i < 70; ++i) quiet.write(3, 0x20);
        for (int i = 0; i < 520; ++i) quiet.inject_rx(0, 0x11);
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-18", "no UART per-byte trace or debug line is emitted with the "
              "level off", t.size() == before, detail);
    }

    // --- LOGHOT-19..21: the CTC's per-event and per-read calls ----------------
    // Channel 0 as a free-running timer with time constant 1 (control word
    // 0x05: control, time constant follows, timer, prescaler 16), ticked long
    // enough for several ZC/TO events; each fires the interrupt callback and
    // triggers channel 1. Then a port read and an external trigger.
    {
        LogTap t(Log::ctc());
        auto drive = [](Ctc& ctc) {
            ctc.on_interrupt = [](int) {};
            ctc.write(0, 0x05);
            ctc.write(0, 0x01);
            ctc.tick(40);                      // a couple of ZC/TO events
            (void)ctc.read(0);
            ctc.trigger(0);
        };
        t.level(spdlog::level::trace);
        Ctc ctc;
        drive(ctc);
        const std::initializer_list<const char*> trace_sites = {
            "ZC/TO! reload=0x01", "ch0 ZC/TO -> trigger ch1", "read ch0 =",
            "external trigger ch0"};
        check("LOGHOT-19", "each CTC per-event / per-read trace (ZC/TO reload, "
              "daisy-chain trigger, port read, external trigger) logs at trace",
              all_seen(t, trace_sites), counts(t, trace_sites));

        t.level(spdlog::level::debug);
        const size_t mark = t.size();
        Ctc irq;
        drive(irq);
        int irq_lines = 0;
        {
            const auto lines = t.ring->last_formatted();
            for (size_t i = mark; i < lines.size(); ++i)
                if (lines[i].find("ch0 ZC/TO -> interrupt") != std::string::npos) ++irq_lines;
        }
        std::snprintf(detail, sizeof(detail), "interrupt lines=%d want >0", irq_lines);
        check("LOGHOT-20", "the CTC per-ZC/TO interrupt debug line logs at debug",
              irq_lines > 0, detail);

        const size_t before = t.size();
        t.level(spdlog::level::info);
        Ctc quiet;
        drive(quiet);
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-21", "no CTC per-event trace or debug line is emitted with the "
              "level off", t.size() == before, detail);
    }

    // --- LOGHOT-22 / 23: the SPI's per-byte debug lines -----------------------
    // ~250k of these on a NextZXOS boot: every byte to and from the SD card.
    {
        LogTap t(Log::spi());
        EchoSpiDevice dev;
        auto drive = [&](SpiMaster& spi) {
            spi.attach_device(0, &dev);
            spi.write_cs(0xFE);                // select SD 0
            spi.write_data(0x11);
            (void)spi.read_data();
        };
        t.level(spdlog::level::debug);
        SpiMaster spi;
        drive(spi);
        const std::initializer_list<const char*> sites = {
            "write tx=0x11, rx=0xee", "read \xe2\x86\x92 returning prev=0xee"};
        check("LOGHOT-22", "SPI write_data and read_data each log their per-byte "
              "debug line at debug", all_seen(t, sites), counts(t, sites));

        const size_t before = t.size();
        t.level(spdlog::level::info);
        SpiMaster quiet;
        drive(quiet);
        std::snprintf(detail, sizeof(detail), "ring grew by %zu want 0", t.size() - before);
        check("LOGHOT-23", "no SPI per-byte debug line is emitted with the level off",
              t.size() == before, detail);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
