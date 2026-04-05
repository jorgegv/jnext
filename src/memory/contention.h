#pragma once
#include <cstdint>
#include <array>

enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, ZX_PLUS3, PENTAGON };

class ContentionModel {
public:
    void build(MachineType type);
    uint8_t delay(uint16_t hc, uint16_t vc) const;
    bool is_contended_address(uint16_t addr) const;

    /// Update whether 0xC000-0xFFFF is contended (128K banks 1,3,5,7).
    void set_contended_c000(bool v) { contended_c000_ = v; }

private:
    // lut_[vc][hc] — vc 0..319, hc 0..455
    std::array<std::array<uint8_t, 456>, 320> lut_{};
    MachineType type_ = MachineType::ZXN_ISSUE2;
    bool contended_c000_ = false;
};
