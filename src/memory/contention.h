#pragma once
#include <cstdint>
#include <array>

enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, ZX_PLUS3, PENTAGON };

class ContentionModel {
public:
    void build(MachineType type);
    uint8_t delay(uint16_t hc, uint16_t vc) const;
    bool is_contended_address(uint16_t addr) const;

    /// Update contention flag for a 16K slot (0-3, mapping 0x0000/0x4000/0x8000/0xC000).
    void set_contended_slot(int slot, bool v) {
        if (slot >= 0 && slot < 4) contended_slot_[slot] = v;
    }

private:
    // lut_[vc][hc] — vc 0..319, hc 0..455
    std::array<std::array<uint8_t, 456>, 320> lut_{};
    MachineType type_ = MachineType::ZXN_ISSUE2;
    bool contended_slot_[4] = {false, false, false, false};
};
