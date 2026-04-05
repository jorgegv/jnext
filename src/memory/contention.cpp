#include "contention.h"

void ContentionModel::build(MachineType type) {
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    if (type == MachineType::PENTAGON || type == MachineType::ZXN_ISSUE2) return;

    // Active display: vc in [64, 255], hc in [0, 255] (pixel area)
    // The ULA fetches data in 8-T-state cycles. During the fetch window,
    // contended memory reads are delayed by {6,5,4,3,2,1,0,0} T-states
    // depending on hc position within the 8T cycle.
    //
    // 48K / 128K / +3: same contention pattern.
    // Pentagon: no contention (returned early above).
    // ZX Next: no contention (returned early above).
    static const uint8_t pattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    for (int vc = 64; vc <= 255; ++vc) {
        for (int hc = 0; hc <= 255; ++hc) {
            int hc_adj = (hc & 0xF) + 1;
            if ((hc_adj & 0xC) != 0) {  // hc_adj[3:2] != 0
                lut_[vc][hc] = pattern[hc & 7];
            }
        }
    }
}

uint8_t ContentionModel::delay(uint16_t hc, uint16_t vc) const {
    if (vc >= 320 || hc >= 456) return 0;
    return lut_[vc][hc];
}

bool ContentionModel::is_contended_address(uint16_t addr) const {
    // Screen memory (bank 5) is always contended on 48K/128K/+3
    if (addr >= 0x4000 && addr <= 0x7FFF) return true;
    // On 128K/+3, banks 1,3,5,7 mapped to 0xC000-0xFFFF are also contended.
    // We check this via the contended_c000_ flag set by the MMU bank switch.
    if (addr >= 0xC000 && contended_c000_) return true;
    return false;
}
