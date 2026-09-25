#include "core/clock.h"
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
    d.i32("cpu_divisor", cpu_divisor_);
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
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
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
