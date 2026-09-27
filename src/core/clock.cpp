#include "core/clock.h"
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

Clock::Clock()
    : cycle_(0)
    , cpu_divisor_(8)               // default: 3.5 MHz (÷8 from 28 MHz)
    , pending_cpu_speed_(CpuSpeed::MHZ_3_5)  // shadow tracks effective at reset
{
}

void Clock::tick(int n)
{
    cycle_ += static_cast<uint64_t>(n);
}

void Clock::reset()
{
    cycle_ = 0;
}

bool Clock::cpu_enable() const
{
    // A rising edge occurs when cycle_ is a multiple of cpu_divisor_.
    return (cycle_ % static_cast<uint64_t>(cpu_divisor_)) == 0;
}

bool Clock::pixel_enable() const
{
    // Pixel clock: 28 MHz / 4 = 7 MHz.
    return (cycle_ % 4ULL) == 0;
}

void Clock::set_cpu_speed(CpuSpeed speed)
{
    // Immediate path — used by reset / init / direct config. Drives both
    // shadow and effective so subsequent bus-idle commits are no-ops.
    pending_cpu_speed_ = speed;
    cpu_divisor_       = cpu_speed_divisor(speed);
}

void Clock::set_pending_cpu_speed(CpuSpeed speed)
{
    // VHDL zxnext.vhd:5788-5789 — immediate update of nr_07_cpu_speed on
    // nr_07_we. The effective `cpu_speed` register is deferred until the
    // next bus-idle CLK_CPU rising edge (zxnext.vhd:5809,5816-5817).
    pending_cpu_speed_ = speed;
}

void Clock::commit_pending_cpu_speed_on_bus_idle(bool bus_idle, bool dma_holds_bus)
{
    // VHDL zxnext.vhd:5809 — assignment fires on every CLK_CPU rising
    // edge while
    //     cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND dma_holds_bus='0'.
    // Caller folds those four signals into a single `bus_idle` boolean;
    // `dma_holds_bus` is exposed separately so DMA-aware callers can
    // inhibit the commit even when the CPU is between bus cycles.
    if (!bus_idle || dma_holds_bus) return;
    cpu_divisor_ = cpu_speed_divisor(pending_cpu_speed_);
}

// GH #27 S3 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 12 bytes as block 0 of the 2 292 965-byte stream.
//
// No field here carries a DECLARED DEFAULT. §12.2 wants one "only when no
// honest default exists", and the gate that would keep a declared default
// honest — a JNSX row asserting declared == post-`reset()` — does not exist
// yet (it is S6's, with the rest of §12.2). A second copy of a power-on value
// with nothing checking it is the shape of the `--help` defect (GH #246), so
// S3 declares every key REQUIRED: a `.jns` that is missing the CPU divisor is
// not a snapshot of a machine.
void Clock::describe_state(jnext::save::StateDesc& d)
{
    d.u64("cycle", cycle_);

    // GH #289. `cpu_divisor_` is a DIVISOR, and it was restored from the
    // stream unchecked, so a crafted `.jns` carrying 0 divided by zero. Same
    // class as the auto-type coordinates: a value in range for its type and
    // not for what it is used for.
    //
    // WHERE, exactly — and not where it looks. `Clock::cpu_enable()` above
    // does `cycle_ % cpu_divisor_` and is the obvious candidate, but it has
    // NO CALLER anywhere outside this file (checked, not assumed; it is dead
    // in the running emulator, like the pre-computed contention LUT). The live
    // divisions are in `Emulator`: `rebase_fuse_tstates_()` computes
    // `(clock_.get() - frame_cycle_) / clock_.cpu_divisor()` from
    // `begin_new_frame()` (`emulator.cpp:8569`) and from
    // `tick_devices_after_instruction()` (`:10255`), so at least once per
    // frame, and `advance_fuse_tstates_()` does the same for a tape trap's charged
    // time (`:7970`). A 0 is SIGFPE there. A NEGATIVE value is worse than a
    // crash: `static_cast<uint64_t>(-8)` is 2^64-8, so every division answers
    // 0, the FUSE T-state counter stops advancing, and the machine quietly
    // stops keeping time instead of stopping. The same value is also a
    // MULTIPLIER at a dozen sites (`:9360`, `:9782`, `:10042`, …), where 0
    // makes every advance 0.
    //
    // THE LEGAL SET IS NOT SPELLED OUT HERE, deliberately. It is the IMAGE of
    // `cpu_speed_divisor()` (`emulator_config.h:39-47`) over `CpuSpeed`, which
    // is the one function the whole tree derives a divisor through
    // (`set_cpu_speed`, `commit_pending_cpu_speed_on_bus_idle`,
    // `pending_cpu_divisor`). Checking against that function rather than
    // against a restated `{8, 4, 2, 1}` means a change to the mapping is
    // followed automatically instead of leaving a second copy to go stale —
    // the `--help` defect's shape (GH #246). The enumerators are listed BY
    // NAME, so a renamed or removed speed fails to compile; only an ADDED
    // speed needs this array extended, and it needs `cpu_speed_divisor`'s own
    // switch extended in the same breath.
    //
    // Four speeds is the hardware's own count: `zxnext.vhd:1300` declares
    // `cpu_speed : std_logic_vector(1 downto 0)`, two bits, loaded from
    // `nr_07_cpu_speed` (`:1299`, also two bits) at `:5817`.
    //
    // REFUSED, not clamped or defaulted. There IS already a silent fallback
    // below in `load_state` — its `switch` maps an unknown divisor to
    // `MHZ_3_5` — and that is exactly the shape this refuses to extend: it
    // repaired the SHADOW and left the effective divisor holding the forged
    // value, which is how a 0 survived to reach the modulus. On a refusal the
    // divisor keeps its pre-load value and the shadow reconciles to it.
    //
    // Marshalled through a local so the member is not overwritten before it
    // can be judged; on the write path the store-back is the value just taken
    // (`state_desc.h` write-back shape (a)). The stream is unchanged: the same
    // `i32` in the same position, so the §17.1 byte-identity gate still sees
    // 12 bytes in block 0.
    int32_t cpu_divisor = cpu_divisor_;
    d.i32("cpu_divisor", cpu_divisor);
    static const CpuSpeed kSpeeds[] = { CpuSpeed::MHZ_3_5, CpuSpeed::MHZ_7,
                                       CpuSpeed::MHZ_14,  CpuSpeed::MHZ_28 };
    bool legal = false;
    for (CpuSpeed s : kSpeeds) {
        if (cpu_divisor == cpu_speed_divisor(s)) legal = true;
    }
    if (!legal) {
        Log::emulator()->error("Clock: snapshot CPU divisor {} is not one of "
                               "the four CPU speeds' divisors", cpu_divisor);
        d.fail("clock.cpu_divisor is not the divisor of any of the four CPU "
               "speeds");
    } else {
        cpu_divisor_ = cpu_divisor;
    }

    // pending_cpu_speed_ is intentionally NOT declared, to keep the
    // savestate format stable. On load() we reconcile the shadow to
    // mirror the effective divisor — any mid-flight pending NR 0x07
    // shadow that hadn't yet committed at save time is collapsed into
    // the effective state. The next NR 0x07 write re-establishes the
    // shadow normally.
}

void Clock::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Clock::load_state(StateReader& r)
{
    jnext::save::BinReadDesc d(r);
    describe_state(d);
    if (d.failed()) {
        // GH #289. The only way this fires is a CPU divisor no CPU speed maps
        // to. The divisor keeps its pre-load value rather than becoming the
        // right-hand side of a modulus the hardware has no setting for, the
        // stream stays in sync (the four bytes were consumed either way), and
        // the fault is named. A `.jns` REFUSES on the same latch; a rewind
        // slot has no return value to refuse with, so this log is where it is
        // visible — the same shape as `Ctc::load_state`.
        Log::emulator()->error("Clock::load_state: {}",
                               d.failure() ? d.failure() : "?");
    }
    // Reconcile shadow with effective: derive a CpuSpeed from the loaded
    // divisor so post-load NR 0x07 readback / commit paths behave
    // consistently. Map divisor → CpuSpeed (8→0, 4→1, 2→2, 1→3); fall
    // back to MHZ_3_5 on unexpected values.
    switch (cpu_divisor_) {
        case 1:  pending_cpu_speed_ = CpuSpeed::MHZ_28;  break;
        case 2:  pending_cpu_speed_ = CpuSpeed::MHZ_14;  break;
        case 4:  pending_cpu_speed_ = CpuSpeed::MHZ_7;   break;
        case 8:
        default: pending_cpu_speed_ = CpuSpeed::MHZ_3_5; break;
    }
}
