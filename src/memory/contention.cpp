#include "contention.h"

void ContentionModel::build(MachineType type) {
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    if (type == MachineType::PENTAGON) return;

    // Active display: vc in [64, 255], hc in [0, 255] (pixel area)
    // Contention pattern: hc_adj = (hc & 0xF) + 1; active when hc_adj[3:2] != 0
    // i.e. hc_adj in 4..15, equivalently (hc & 0xF) in 3..14
    // Delays (hc mod 8 within contended window): {6,5,4,3,2,1,0,0}
    //
    // ZX128K / +3: memory contention LUT is identical to 48K.
    // The additional 128K port 0xFD family contention (which adds ~1 wait state
    // during active display hc window) is handled in port_dispatch, not here.
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
    return (addr >= 0x4000 && addr <= 0x7FFF);
}
