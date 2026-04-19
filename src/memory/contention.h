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

    // ── VHDL-faithful combined-gate inputs ────────────────────────────
    // Model the four signals feeding the VHDL contention enable gate
    // (zxnext.vhd:4481) and the memory-side mem_contend decode
    // (zxnext.vhd:4489-4493). Used by is_contended_access(); they do NOT
    // alter the existing delay() / is_contended_address() / set_contended_slot()
    // surface — runtime tick-loop integration is out of scope for this
    // change.

    /// 8-bit SRAM page index seen at the CPU cycle's active address
    /// (mem_active_page in zxnext.vhd:4489-4493). High bits [7:4] gate
    /// "high pages never contend"; low bits select the timing-mode bit.
    void set_mem_active_page(uint8_t page) { mem_active_page_ = page; }

    /// 2-bit CPU speed (NR 0x07 bits 1:0): 0=3.5 MHz, 1=7, 2=14, 3=28.
    /// Any non-zero speed disables contention per zxnext.vhd:4481.
    void set_cpu_speed(uint8_t speed_2bit) { cpu_speed_ = speed_2bit & 0x03; }

    /// machine_timing_pentagon (zxnext.vhd:4481) — Pentagon never contends.
    /// Normally set by build() from MachineType, but exposed so runtime
    /// machine-type changes (if ever wired) can update it.
    void set_pentagon_timing(bool pt) { pentagon_timing_ = pt; }

    /// NR 0x08 bit 6 — eff_nr_08_contention_disable (zxnext.vhd:4481).
    void set_contention_disable(bool cd) { contention_disable_ = cd; }

    uint8_t mem_active_page()    const { return mem_active_page_; }
    uint8_t cpu_speed()          const { return cpu_speed_; }
    bool    pentagon_timing()    const { return pentagon_timing_; }
    bool    contention_disable() const { return contention_disable_; }

    /// VHDL-faithful combined gate (zxnext.vhd:4481 AND 4489-4493).
    /// Returns true iff a memory access under the current inputs would
    /// trigger mem_contend='1' AND i_contention_en='1'.
    bool is_contended_access() const;

private:
    // lut_[vc][hc] — vc 0..319, hc 0..455
    std::array<std::array<uint8_t, 456>, 320> lut_{};
    MachineType type_ = MachineType::ZXN_ISSUE2;
    bool contended_slot_[4] = {false, false, false, false};

    // VHDL-faithful gate inputs (defaults match power-on semantics:
    // zxnext.vhd:1300 nr_07 cpu_speed ← "00", zxnext.vhd:1380 nr_08
    // contention_disable ← '0', mem_active_page starts 0x00).
    uint8_t mem_active_page_   = 0;
    uint8_t cpu_speed_         = 0;
    bool    pentagon_timing_   = false;
    bool    contention_disable_ = false;
};
