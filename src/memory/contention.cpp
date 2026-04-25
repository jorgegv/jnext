#include "contention.h"

void ContentionModel::build(MachineType type) {
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    for (int i = 0; i < 4; ++i) contended_slot_[i] = false;

    // Reset VHDL-faithful gate inputs to their power-on defaults and then
    // seed pentagon_timing_ from the committed machine type so callers
    // don't have to remember to flip the flag after build().
    //
    // NOTE: this clears all four contention gate inputs (mem_active_page,
    // cpu_speed, pentagon_timing, contention_disable) — safe today because
    // build() is only called from Emulator::init(). If ever hot-called at
    // runtime to switch machine types, re-seed cpu_speed from NR 0x07 and
    // contention_disable from NR 0x08 after the build() call.
    mem_active_page_    = 0;
    cpu_speed_          = 0;
    contention_disable_ = false;
    pentagon_timing_    = (type == MachineType::PENTAGON);

    if (type == MachineType::PENTAGON || type == MachineType::ZXN_ISSUE2) return;

    // Active display: vc in [64, 255], hc in [0, 255] (pixel area)
    // The ULA fetches data in 8-T-state cycles. During the fetch window,
    // contended memory reads are delayed by {6,5,4,3,2,1,0,0} T-states
    // depending on hc position within the 8T cycle.
    //
    // VHDL (zxula.vhd line 583):
    //   wait_s when (hc_adj[3:2] != "00") OR (hc_adj[3:1] == "000" AND timing_p3)
    //
    // 48K/128K: contention when hc_adj[3:2] != 0  (hc_adj in {4..15})
    // +3:       also when hc_adj[3:1] == 0         (hc_adj in {0,1,4..15})
    static const uint8_t pattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    const bool is_p3 = (type == MachineType::ZX_PLUS3);

    for (int vc = 64; vc <= 255; ++vc) {
        for (int hc = 0; hc <= 255; ++hc) {
            int hc_adj = (hc & 0xF) + 1;
            bool contend = (hc_adj & 0xC) != 0;           // hc_adj[3:2] != 0
            if (is_p3) contend |= (hc_adj & 0xE) == 0;    // hc_adj[3:1] == 0
            if (contend) {
                lut_[vc][hc] = pattern[hc & 7];
            }
        }
    }

    // Default contended slot: slot 1 (0x4000-0x7FFF) = bank 5, always contended
    // on 48K/128K/+3 in normal paging mode.
    contended_slot_[1] = true;
}

uint8_t ContentionModel::delay(uint16_t hc, uint16_t vc) const {
    if (vc >= 320 || hc >= 456) return 0;
    return lut_[vc][hc];
}

bool ContentionModel::is_contended_address(uint16_t addr) const {
    int slot = addr >> 14;  // 0x0000→0, 0x4000→1, 0x8000→2, 0xC000→3
    return contended_slot_[slot];
}

bool ContentionModel::is_contended_access() const {
    // VHDL zxnext.vhd:4481 — i_contention_en =
    //     (not eff_nr_08_contention_disable)
    // AND (not machine_timing_pentagon)
    // AND (not cpu_speed(1)) AND (not cpu_speed(0)).
    // Any non-zero cpu_speed disables contention.
    //
    // WONT — expansion-bus override (zxnext.vhd:5816-5820):
    //     if expbus_en = '0' then cpu_speed <= nr_07_cpu_speed;
    //     else                    cpu_speed <= expbus_speed;
    // jnext has no NextBUS (`expbus_*`) emulation: see expbus probes in
    // src/peripheral/nmi_source.cpp / src/cpu/im2.cpp which only model
    // the NMI / IM2 enable bits, not the bus-master speed override. We
    // therefore drive `cpu_speed_` directly from NR 0x07 with no
    // expbus_speed substitution path. Documented as WONT per
    // feedback_wont_taxonomy.md; revisit only if NextBUS emulation is
    // ever added.
    if (contention_disable_) return false;
    if (pentagon_timing_)    return false;
    if (cpu_speed_ != 0)     return false;

    // VHDL zxnext.vhd:4489 — mem_contend = '0' when mem_active_page(7:4) /= "0000".
    if ((mem_active_page_ & 0xF0) != 0) return false;

    const uint8_t low = mem_active_page_ & 0x0F;
    switch (type_) {
        case MachineType::ZX48K:
            // VHDL zxnext.vhd:4490 — 48K: contend iff mem_active_page(3:1) = "101".
            return ((low >> 1) & 0x07) == 0x05;
        case MachineType::ZX128K:
            // VHDL zxnext.vhd:4491 — 128K: contend iff mem_active_page(1) = '1' (odd banks).
            return (low & 0x02) != 0;
        case MachineType::ZX_PLUS3:
            // VHDL zxnext.vhd:4492 — +3: contend iff mem_active_page(3) = '1' (banks >= 4).
            return (low & 0x08) != 0;
        case MachineType::PENTAGON:
        case MachineType::ZXN_ISSUE2:
            // Pentagon gated off upstream; ZXN_ISSUE2 has no timing-mode line here.
            return false;
    }
    return false;
}

bool ContentionModel::port_contend(uint16_t cpu_a, bool port_ulap_io_en) const {
    // VHDL zxnext.vhd:4496 —
    //     port_contend <= (not cpu_a(0)) or port_7ffd_active
    //                                    or port_bf3b
    //                                    or port_ff3b;
    //
    // Term 1 — even-port: every CPU IORQ to an even port asserts
    // contention regardless of timing mode or NR gating.
    if ((cpu_a & 0x0001) == 0) return true;

    // Term 2 — ULA+ index/data ports (zxnext.vhd:2685-2686):
    //     port_bf3b <= port_bfxx_msb and port_3b_lsb and port_ulap_io_en;
    //     port_ff3b <= port_ffxx_msb and port_3b_lsb and port_ulap_io_en;
    // i.e. asserted iff the full 16-bit address is exactly 0xBF3B / 0xFF3B
    // AND `port_ulap_io_en` (NR 0x82 bit 8 → internal_port_enable(24),
    // zxnext.vhd:2439) is set. The ULA+ ports are odd (low byte 0x3B),
    // so without this OR-term the even-port test above would already
    // have rejected them.
    if (port_ulap_io_en && (cpu_a == 0xBF3B || cpu_a == 0xFF3B)) {
        return true;
    }

    // Term 3 — port_7ffd_active (zxnext.vhd:2594):
    //     port_7ffd_active <= '1' when port_7ffd = '1'
    //                              and (s128_timing_hw_en = '1'
    //                                or p3_timing_hw_en = '1')
    //                         else '0';
    // PHASE-B: not driven by the bare class. The full Emulator owns the
    // 128K/+3 machine-timing select AND the `port_7ffd_io_en`
    // (NR 0x82 bit 1) gate AND the `port_7ffd` address-decode rules
    // (cpu_a(15)='0' AND (cpu_a(14)='1' OR not p3_timing) AND port_fd
    // AND not port_1ffd). Phase B rows CT-IO-05/06 will exercise this
    // term via a full-Emulator harness; the bare-class accessor
    // intentionally drops it. See doc/testing/CONTENTION-TEST-PLAN-DESIGN.md
    // §8 row notes.

    return false;
}
