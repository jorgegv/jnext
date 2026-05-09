// Z80Cpu /INT pulse-window test (machine-aware INT_PULSE_TSTATES).
//
// Oracle: VHDL zxnext.vhd:2033 — pulse_count_end formula
//   pulse_count_end <= pulse_count(5) and (machine_timing_48 or
//                       machine_timing_p3 or pulse_count(2));
// → 48K and +3 release /INT after 32 cycles (bit 5 alone).
// → 128K, Pentagon and Next-default need bit 5 AND bit 2 → 36 cycles.
//
// Z80Cpu uses the same width (with `>` strict comparator at z80_cpu.cpp:421)
// to expire a pending /INT that the CPU never acknowledged because IFF1=0
// (e.g. inside an ISR). This test validates both branches.
//
// Execute() flow we exercise:
//   if (int_pending_) {
//       if (tstates - int_requested_at_ > pulse_width && !iff1) {
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
//      int_pending_ stays set or is cleared. The NOP at PC=0 runs; PC→1.
//   5) Re-enable IFF1, call execute() again at PC=1.
//      - If int_pending_ was cleared, NOP runs again and PC→2.
//      - If int_pending_ persists, IM1 fires: PC=1 is pushed, jump 0x0038.
//   6) Final PC distinguishes the two outcomes (0x0002 vs 0x0038).

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

    // 48K / +3 timing — pulse width 32, expires at delta > 32 → first
    // discard at delta = 33.
    {
        const bool d32 = int_was_discarded(true, 32);  // 32 ≤ 32 → live
        const bool d33 = int_was_discarded(true, 33);  // 33 > 32  → discarded
        check(res, "INT-PULSE-48K-edge-32",
              !d32, "48K/+3 (width=32): delta=32 must NOT discard /INT");
        check(res, "INT-PULSE-48K-past-33",
              d33,  "48K/+3 (width=32): delta=33 must discard /INT");
    }

    // 128K / Pentagon / Next-default — pulse width 36, expires at delta > 36
    // → first discard at delta = 37.
    {
        const bool d33 = int_was_discarded(false, 33);
        const bool d36 = int_was_discarded(false, 36);
        const bool d37 = int_was_discarded(false, 37);
        check(res, "INT-PULSE-128K-inside-33",
              !d33, "128K/Pent/Next (width=36): delta=33 must NOT discard");
        check(res, "INT-PULSE-128K-edge-36",
              !d36, "128K/Pent/Next (width=36): delta=36 must NOT discard");
        check(res, "INT-PULSE-128K-past-37",
              d37,  "128K/Pent/Next (width=36): delta=37 must discard");
    }

    // The 4-T-state divergence band that this fix addresses: at delta=33
    // through delta=36 the 48K/+3 pulse is gone but the 128K/Pent/Next
    // pulse is still live. This is exactly the window where the previous
    // hardcoded 32 was wrong for non-48K/+3 machines.
    {
        const bool short_expired = int_was_discarded(true,  33);
        const bool long_live     = !int_was_discarded(false, 33);
        check(res, "INT-PULSE-DELTA33-48K-expires",
              short_expired, "48K/+3 pulse must expire at delta=33");
        check(res, "INT-PULSE-DELTA33-128K-live",
              long_live,     "128K/Pent/Next pulse must still be live at delta=33");
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
    std::printf("Total: %4d  Passed: %4d  Failed: %4d\n",
                res.total, res.passed, res.failed);
    return res.failed == 0 ? 0 : 1;
}
