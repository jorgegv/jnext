#include "contention.h"

void ContentionModel::build(MachineType type) {
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    for (int i = 0; i < 4; ++i) contended_slot_[i] = false;

    // Reset VHDL-faithful gate inputs to their power-on defaults and then
    // seed pentagon_timing_ from the committed machine type so callers
    // don't have to remember to flip the flag after build().
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
