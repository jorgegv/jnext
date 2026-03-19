#pragma once
#include <cstdint>
#include <array>

enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, PENTAGON };

class ContentionModel {
public:
    void build(MachineType type);
    uint8_t delay(uint16_t hc, uint16_t vc) const;
    bool is_contended_address(uint16_t addr) const;
private:
    // lut_[vc][hc] — vc 0..319, hc 0..455
    std::array<std::array<uint8_t, 456>, 320> lut_{};
    MachineType type_ = MachineType::ZXN_ISSUE2;
};
