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

    // ── Per-cycle contention runtime API ──────────────────────────────
    /// VHDL `o_cpu_contend` / `o_cpu_wait_n` mirror (zxula.vhd:579-600 +
    /// zxnext.vhd:4481-4496). Returns the number of T-states the CPU
    /// should add to the current bus cycle's budget. Caller passes the
    /// Z80 control-line state, the 16-bit CPU bus address, and the
    /// current raster position (hc, vc) in the 7 MHz pixel-tick domain
    /// matching VHDL i_hc / i_vc (9-bit counters, range 0..hc_max /
    /// 0..vc_max per machine).
    ///
    /// Active-LOW VHDL signals are passed in the `*_n` form: pass
    /// `mreq_n=false` for an asserted MREQ, etc. (i.e. use the same
    /// polarity as the FUSE `cpu_mreq_n` line). The four gate inputs
    /// (cpu_speed, contention_disable, pentagon_timing, mem_active_page)
    /// are consulted via the existing accessors — caller must update
    /// `mem_active_page` BEFORE calling for memory cycles.
    ///
    /// `port_ulap_io_en` mirrors NR 0x82 bit 4; pass false if not
    /// modelled.
    ///
    /// Returns 0 for non-contending cycles, or the per-phase delay
    /// pattern entry (`{6,5,4,3,2,1,0,0}[hc & 7]`) for contending ones.
    /// Both 48K/128K (`o_cpu_contend`) and +3 (`o_cpu_wait_n`) paths
    /// are covered — caller does not need to pre-classify.
    uint8_t contention_tick(bool mreq_n, bool iorq_n, bool rd_n, bool wr_n,
                            uint16_t cpu_a, uint16_t hc, uint16_t vc,
                            bool port_ulap_io_en = false) const;

    /// VHDL-faithful `port_contend` decode (zxnext.vhd:4496):
    ///     port_contend <= (not cpu_a(0))
    ///                   or port_7ffd_active
    ///                   or port_bf3b
    ///                   or port_ff3b;
    ///
    /// where `port_bf3b`/`port_ff3b` are gated by `port_ulap_io_en`
    /// (zxnext.vhd:2685-2686) and `port_7ffd_active` is gated by 128K/+3
    /// timing (zxnext.vhd:2594).
    ///
    /// Returns true iff a CPU IORQ at @p cpu_a would assert
    /// `port_contend='1'` under the supplied gate inputs:
    ///   * even-port term: `(not cpu_a(0))` — bit 0 of @p cpu_a == 0.
    ///   * ULA+ term:      asserted only when @p port_ulap_io_en is true
    ///                     AND the address matches `0xBF3B` (index) or
    ///                     `0xFF3B` (data) — full 16-bit decode per
    ///                     zxnext.vhd:2685-2686 (`port_bfxx_msb`/`port_ffxx_msb`
    ///                     match the high byte, `port_3b_lsb` matches the
    ///                     low byte, AND `port_ulap_io_en`).
    ///
    /// **Bare-class limitation**: this overload does NOT consume the
    /// `port_7ffd_active` term (zxnext.vhd:2594) — that signal is gated
    /// by full machine-timing-128 / -p3 selection AND `port_7ffd_io_en`
    /// (NR 0x82 bit 1) AND a valid `port_7ffd` address decode, all of
    /// which only the full `Emulator` can drive truthfully today. Phase B
    /// rows CT-IO-05/06 will exercise that term once the runtime wiring
    /// lands. Calling this accessor for `cpu_a == 0x7FFD` therefore
    /// returns `(not cpu_a(0)) == 0` (i.e. the odd-bit term only) and
    /// will under-report contention for 128K/+3 — that is expected and
    /// caller-documented.
    bool port_contend(uint16_t cpu_a, bool port_ulap_io_en) const;

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
