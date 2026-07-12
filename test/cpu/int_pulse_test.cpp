// Z80Cpu /INT pulse-window test (machine-aware INT_PULSE_TSTATES).
//
// Oracle: VHDL zxnext.vhd:2017-2033 — pulse_count_end formula
//   pulse_count_end <= pulse_count(5) and (machine_timing_48 or
//                       machine_timing_p3 or pulse_count(2));
// → 48K and +3 release /INT after 32 cycles (bit 5 alone).
// → 128K, Pentagon and Next-default need bit 5 AND bit 2 → 36 cycles.
//
// V18R-CPU-01 (post-fix): the drop arm is now UNCONDITIONAL on IFF1, as
// per VHDL — `pulse_int_n` returns to '1' as soon as `pulse_count_end=1`,
// regardless of IFF1 state. The Z80's M1 sample sees /INT high once the
// pulse window has closed, so the INT is not accepted — even if IFF1
// became '1' between the request and the sample (e.g. via EI/RETI).
//
// Execute() flow exercised here (post-V18R-CPU-01):
//   if (int_pending_) {
//       if (tstates - int_requested_at_ > pulse_width) {
//           int_pending_ = false;          // pulse expired, INT discarded
//       } else if (iff1) {
//           // service IM1 → push PC, jump 0x0038
//       }
//   }
//
// Test idiom (probe whether INT is still pending after `delta` T-states
// while IFF1 was held off):
//   1) Build a Z80Cpu with 64K of NOPs, IFF1=0.
//   2) Set FUSE tstates=0, request_interrupt() — latches int_requested_at_=0.
//   3) Set FUSE tstates=delta directly. (No instruction has executed yet.)
//   4) Call execute() — IFF1 still 0; the pulse-window arm decides whether
//      int_pending_ stays set or is cleared. The NOP at PC=0 runs; PC→1
//      and tstates advances by 4.
//   5) Re-enable IFF1, call execute() again at PC=1.
//      - At the second execute(), the pulse-window arm checks again
//        (with the +4-from-NOP T-state advance). If the pulse has expired
//        across either boundary, int_pending_ is cleared → NOP runs again
//        (PC=2).
//      - If int_pending_ persists, IM1 fires: PC=1 is pushed, jump 0x0038.
//   6) Final PC distinguishes the two outcomes (0x0002 vs 0x0038).
//
// Note (V18R-CPU-01): the relevant boundary therefore lies at
//     delta + 4(NOP) > pulse_width
// → discarded if delta > pulse_width - 4.
// 48K/+3 (width=32): discarded for delta>=29.
// 128K/Pent/Next (width=36): discarded for delta>=33.

#include "cpu/z80_cpu.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

class RamMemory : public MemoryInterface {
public:
    uint8_t ram[65536];
    RamMemory() { std::memset(ram, 0, sizeof(ram)); }
    uint8_t read(uint16_t addr) override { return ram[addr]; }
    void    write(uint16_t addr, uint8_t val) override { ram[addr] = val; }
};

class NullIo : public IoInterface {
public:
    uint8_t in(uint16_t port) override { return static_cast<uint8_t>(port >> 8); }
    void    out(uint16_t, uint8_t) override {}
};

// Returns true if the /INT pulse was discarded after `delta` T-states with
// IFF1=0; false if the pulse persisted and IM1 was serviced once IFF1
// was re-enabled. Configures `is_48_or_p3` on Z80Cpu before requesting.
static bool int_was_discarded(bool is_48_or_p3, uint32_t delta_tstates) {
    RamMemory  mem;
    NullIo     io;
    Z80Cpu     cpu(mem, io);

    cpu.set_machine_timing_48_or_p3(is_48_or_p3);

    Z80Registers r{};
    r.PC     = 0x0000;
    r.SP     = 0xFFFE;   // valid; even alignment.
    r.IFF1   = 0;        // interrupts disabled at request time
    r.IFF2   = 0;
    r.IM     = 1;        // IM1 — ignores supplied vector
    r.halted = false;
    cpu.set_registers(r);
    std::memset(mem.ram, 0x00, sizeof(mem.ram));   // 64K NOPs

    *fuse_z80_tstates_ptr() = 0;
    cpu.request_interrupt(0xFF);                   // int_requested_at_ = 0

    // Jump the FUSE counter forward without executing anything; this is
    // how we control the (tstates - int_requested_at_) delta seen by the
    // pulse-window arm in execute().
    *fuse_z80_tstates_ptr() = delta_tstates;

    // First execute() with IFF1=0 — runs the pulse-window arm.
    //  • delta >  pulse_width → int_pending_ ← false; NOP runs; PC=1.
    //  • delta <= pulse_width → int_pending_ stays; NOP runs; PC=1.
    cpu.execute();

    // Second execute() with IFF1=1 — if int_pending_ persisted, IM1 services
    // here and PC jumps to 0x0038. If it was discarded, NOP runs and PC=2.
    r = cpu.get_registers();
    r.IFF1 = 1;
    r.IFF2 = 1;
    cpu.set_registers(r);
    cpu.execute();

    const uint16_t post_pc = cpu.get_registers().PC;
    return post_pc == 0x0002;  // discarded if NOP ran twice; persisted if IM1 fired
}

struct Result {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

static void check(Result& res, const char* name, bool ok, const char* detail = "") {
    res.total++;
    if (ok) {
        res.passed++;
        std::printf("[PASS] %s\n", name);
    } else {
        res.failed++;
        std::printf("[FAIL] %s%s%s\n", name, detail[0] ? "  " : "", detail);
    }
}

}  // namespace

int main() {
    Result res;

    // 48K / +3 timing — pulse width 32. Discard boundary at the SECOND
    // execute() lies at (delta_initial + 4_NOP) > 32, i.e. delta>=29.
    {
        const bool d28 = int_was_discarded(true, 28);  // 28+4=32, not >32 → live
        const bool d29 = int_was_discarded(true, 29);  // 29+4=33, >32      → discarded
        check(res, "INT-PULSE-48K-edge-28",
              !d28, "48K/+3 (width=32): delta=28 must NOT discard /INT");
        check(res, "INT-PULSE-48K-past-29",
              d29,  "48K/+3 (width=32): delta=29 must discard /INT");
    }

    // 128K / Pentagon / Next-default — pulse width 36. Discard boundary
    // at second execute lies at (delta_initial + 4_NOP) > 36, i.e. delta>=33.
    {
        const bool d32 = int_was_discarded(false, 32);
        const bool d33 = int_was_discarded(false, 33);
        check(res, "INT-PULSE-128K-edge-32",
              !d32, "128K/Pent/Next (width=36): delta=32 must NOT discard");
        check(res, "INT-PULSE-128K-past-33",
              d33,  "128K/Pent/Next (width=36): delta=33 must discard");
    }

    // The 4-T-state divergence band that the original Pass-1 fix addressed:
    // at delta=29 through delta=32 the 48K/+3 pulse is gone (post-NOP) but
    // the 128K/Pent/Next pulse is still live. This is exactly the window
    // where the previous hardcoded 32 was wrong for non-48K/+3 machines.
    {
        const bool short_expired = int_was_discarded(true,  29);
        const bool long_live     = !int_was_discarded(false, 32);
        check(res, "INT-PULSE-DELTA-DIVERGENCE-48K-expires",
              short_expired, "48K/+3 pulse must expire across delta=29 boundary");
        check(res, "INT-PULSE-DELTA-DIVERGENCE-128K-live",
              long_live,     "128K/Pent/Next pulse must still be live at delta=32");
    }

    // V18R-CPU-01 discriminative regression — pulse-expired drop must be
    // UNCONDITIONAL on IFF1. We pick delta such that the FIRST execute()
    // runs the NOP (IFF1=0 → no accept, no expiry yet) but the SECOND
    // execute()'s entry tstates exceed the pulse window AND IFF1=1.
    //
    // 48K/+3, width=32: delta=30 → after first NOP tstates=34 > 32.
    //   - With V18R-CPU-01 fix: drop fires unconditionally → NOP runs at
    //     second exec → PC=2 → discarded=true.
    //   - Pre-fix (drop gated on !IFF1): IFF1=1 at second exec → drop arm
    //     skipped → accept arm runs → INT serviced → PC=0x0038.
    //
    // Same model for 128K/Pent/Next (width=36): delta=34 → after first NOP
    // tstates=38 > 36.
    {
        const bool d_48K_30 = int_was_discarded(true,  30);
        const bool d_128_34 = int_was_discarded(false, 34);
        check(res, "INT-PULSE-V18R-CPU-01-48K-pulse-expired-iff1-up",
              d_48K_30,
              "V18R-CPU-01: 48K/+3 delta=30 must DROP at 2nd execute() "
              "even though IFF1=1 (VHDL /INT line went high at T=32)");
        check(res, "INT-PULSE-V18R-CPU-01-128K-pulse-expired-iff1-up",
              d_128_34,
              "V18R-CPU-01: 128K/Pent/Next delta=34 must DROP at 2nd "
              "execute() even though IFF1=1 (VHDL /INT line went high at T=36)");
    }

    // Setter / getter round-trip + default value (preserves byte-identical
    // legacy behaviour for FUSE Z80 test runs that drive Z80Cpu directly).
    {
        RamMemory mem;
        NullIo    io;
        Z80Cpu    cpu(mem, io);
        check(res, "INT-PULSE-default-is-true",
              cpu.machine_timing_48_or_p3(),
              "Z80Cpu default must keep machine_48_or_p3_=true (32-cycle width)");
        cpu.set_machine_timing_48_or_p3(false);
        check(res, "INT-PULSE-setter-false",
              !cpu.machine_timing_48_or_p3(),
              "setter(false) must take effect");
        cpu.set_machine_timing_48_or_p3(true);
        check(res, "INT-PULSE-setter-true",
              cpu.machine_timing_48_or_p3(),
              "setter(true) must take effect");
    }

    std::printf("\nZ80Cpu /INT pulse-window test results\n");
    std::printf("=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                res.total, res.passed, res.failed, 0);
    return res.failed == 0 ? 0 : 1;
}
