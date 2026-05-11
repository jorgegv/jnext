#pragma once
#include <cstdint>
#include <array>

enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, ZX_PLUS3 };

// VHDL `machine_timing_48 / _128 / _p3 / _pentagon` (zxnext.vhd:1282-1285)
// — one-hot per-frame-latched signals derived combinationally from
// `eff_nr_03_machine_timing` (zxnext.vhd:5761-5777). This is the
// **tim_sel** axis (NR 0x03 bits 6:4), distinct from the **typ_sel**
// axis (NR 0x03 bits 2:0) which drives `nr_03_machine_type` →
// `MachineType` above.
//
// VHDL surfaces that consume `machine_timing_*` (not `machine_type_*`):
//
//   Wired in jnext via this `machine_timing_` axis (V24-MEM-01 fix):
//     * :4490-4492 — `mem_contend` per-machine memory-page decode
//                    (ContentionModel::is_contended_access /
//                    contention_tick + Mmu::mem_contend_for_).
//     * :4481      — `i_contention_en` Pentagon-disable AND-term
//                    (ContentionModel::is_contended_access /
//                    contention_tick).
//     * :2594      — `port_7ffd_active` 128K/+3-only port-contend
//                    (ContentionModel::port_contend).
//
//   VHDL keys on `machine_timing_*` but jnext currently still keys on
//   `MachineType` (typ_sel) — pre-existing inconsistencies flagged by
//   the V24-MEM-01 reviewer; out-of-scope for D3 (separate follow-ups):
//     * :2033      — pulse_count_end (48K/+3 32-cycle width; otherwise
//                    36) — IM2/CPU pulse mode (emulator NR 0x03 handler
//                    still drives `is_48_or_p3` via MachineType).
//     * :2589      — `port_p3_float` floating-bus gate at port 0x0FFD
//                    (emulator.cpp port 0x0FFD handler gates on
//                    `config_.type == MachineType::ZX_PLUS3`).
//     * :2771      — `port_fffd_rd` AY-read alias at port 0xBFFD on +3
//                    (emulator.cpp port 0xBFFD handler gates on
//                    `config_.type == MachineType::ZX_PLUS3`).
//     * :4513      — `port_ff_dat_ula` floating-bus / $FF default at
//                    port 0xFF (emulator.cpp port 0xFF handler gates
//                    on `config_.type ∈ {ZX48K, ZX128K}`).
//
// Surfaces that consume `machine_type_*` (not `machine_timing_*`):
//   * :2981-3008 — `sram_rom` derivation (per-machine ROM bank decode).
//   * :2986-3005 — altrom override per machine.
//
// V24-MEM-01 / V25-MEM-01 fix (Task 2 Pass-24/25 audit): the contention
// surfaces in jnext previously keyed on `MachineType` (typ_sel-derived),
// which silently aligned on the canonical boot path (NextZXOS always
// writes NR 0x03 with matching tim_sel == typ_sel) but diverged when a
// user wrote NR 0x03 with `tim_sel != typ_sel`. Splitting this axis here
// matches the VHDL one-to-one. The port-decode + pulse_count_end
// surfaces listed above were explicitly out-of-scope for D3 and remain
// on the typ_sel axis pending separate follow-ups.
enum class MachineTimingMode {
    Timing48 = 0,        // VHDL machine_timing_48     (nr_03_machine_timing = "000"/"001")
    Timing128 = 1,       // VHDL machine_timing_128    (nr_03_machine_timing(1:0) = "10")
    TimingPlus3 = 2,     // VHDL machine_timing_p3     (nr_03_machine_timing(1:0) = "11")
    TimingPentagon = 3,  // VHDL machine_timing_pentagon (nr_03_machine_timing(2)='1')
};

// Map a `MachineType` (typ_sel axis) to its canonical `MachineTimingMode`
// (tim_sel axis). Used at Emulator::init and at hard/soft reset, where the
// canonical NextZXOS boot path aligns both axes from a single CLI
// machine-type choice (real Next firmware writes NR 0x03 with bits 6:4 ==
// bits 2:0). After init, the two axes evolve independently via NR 0x03's
// two write-gated commit paths (zxnext.vhd:5124-5135 vs :5137-5145).
inline constexpr MachineTimingMode default_machine_timing_for(MachineType t) {
    switch (t) {
        case MachineType::ZX48K:      return MachineTimingMode::Timing48;
        case MachineType::ZX128K:     return MachineTimingMode::Timing128;
        case MachineType::ZX_PLUS3:   return MachineTimingMode::TimingPlus3;
        case MachineType::ZXN_ISSUE2: return MachineTimingMode::Timing128;
    }
    return MachineTimingMode::Timing128;
}

// Decode the 3-bit `nr_03_machine_timing` field (post-:5126-5131
// normalisation) into one of the four one-hot `machine_timing_*`
// modes per VHDL zxnext.vhd:5761-5777. Bit 2 (=Pentagon) takes
// priority; otherwise bits[1:0]=2 → 128, =3 → +3, anything else → 48.
inline constexpr MachineTimingMode decode_nr_03_machine_timing(uint8_t v) {
    if ((v & 0x04) != 0) return MachineTimingMode::TimingPentagon;
    if ((v & 0x03) == 0x02) return MachineTimingMode::Timing128;
    if ((v & 0x03) == 0x03) return MachineTimingMode::TimingPlus3;
    return MachineTimingMode::Timing48;
}

class ContentionModel {
public:
    void build(MachineType type);

    /// Verify9-memory: hot-rebuild the contention LUT + per-machine
    /// decode without touching the dynamic gate state
    /// (mem_active_page, cpu_speed, pending_cpu_speed, contention_disable
    /// /shadow, contended_slot_[]). Used by the NR 0x03 machine-timing
    /// commit path so a runtime Next→48K/128K/+3/Pentagon switch
    /// updates the LUT (the +3 path adds the `hc_adj[3:1]=000` corner
    /// per VHDL zxula.vhd:582-583) without resetting paging or
    /// contention-disable state. `build()` (called from Emulator::init)
    /// remains the full-reset entry point.
    void rebuild_for_type(MachineType type);

    uint8_t delay(uint16_t hc, uint16_t vc) const;
    bool is_contended_address(uint16_t addr) const;

    /// Update contention flag for a 16K slot (0-3, mapping 0x0000/0x4000/0x8000/0xC000).
    void set_contended_slot(int slot, bool v) {
        if (slot >= 0 && slot < 4) contended_slot_[slot] = v;
    }

    // ── VHDL-faithful combined-gate inputs ────────────────────────────
    // Model the signals feeding the VHDL contention enable gate
    // (zxnext.vhd:4481) and the memory-side mem_contend decode
    // (zxnext.vhd:4489-4493). Used by is_contended_access() AND by the
    // per-cycle `contention_tick()` runtime path — Verify9-memory
    // confirmed the FUSE Z80 callbacks (src/cpu/z80_cpu.cpp) push the
    // mem_active_page on every memory cycle and the NR 0x82 / NR 0x07
    // / NR 0x08 / NR 0x03 dispatchers push their gate updates on every
    // write, so all production paths consume these gate inputs.

    /// 8-bit SRAM page index seen at the CPU cycle's active address
    /// (mem_active_page in zxnext.vhd:4489-4493). High bits [7:4] gate
    /// "high pages never contend"; low bits select the timing-mode bit.
    void set_mem_active_page(uint8_t page) { mem_active_page_ = page; }

    /// 2-bit CPU speed (NR 0x07 bits 1:0): 0=3.5 MHz, 1=7, 2=14, 3=28.
    /// Any non-zero speed disables contention per zxnext.vhd:4481.
    /// **Immediate** commit of the effective gate; both pending shadow
    /// and effective fields are updated. Used by tests that need a known
    /// gate state without modelling the deferred bus-idle commit edge.
    /// Production NR 0x07 dispatch should instead call
    /// set_pending_cpu_speed() and let
    /// commit_pending_cpu_speed_on_bus_idle() latch on the next bus-idle
    /// CLK_CPU rising edge per zxnext.vhd:5796-5828.
    void set_cpu_speed(uint8_t speed_2bit) {
        cpu_speed_         = speed_2bit & 0x03;
        pending_cpu_speed_ = cpu_speed_;
    }

    /// Update the **shadow** value of NR 0x07 cpu_speed only — the next
    /// commit_pending_cpu_speed_on_bus_idle() call with bus-idle=true
    /// will promote it to the effective gate. Models the latch at
    /// zxnext.vhd:5788-5789 (immediate on nr_07_we) feeding the
    /// bus-idle-gated assignment at 5816-5817:
    ///     cpu_speed <= nr_07_cpu_speed only when
    ///     cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND
    ///     dma_holds_bus='0'.
    /// This is the production NR 0x07 write path.
    void set_pending_cpu_speed(uint8_t speed_2bit) {
        pending_cpu_speed_ = speed_2bit & 0x03;
    }

    /// Commit pending → effective cpu_speed when the CPU bus is idle.
    /// VHDL zxnext.vhd:5809 — the assignment fires on every CLK_CPU
    /// rising edge that satisfies
    ///     cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND dma_holds_bus='0'.
    /// Caller folds those four signals into a single `bus_idle` boolean;
    /// `dma_holds_bus` is exposed separately for symmetry with the VHDL
    /// gate and for DMA-aware callers. The commit is a no-op when the
    /// gate is false. Idempotent.
    void commit_pending_cpu_speed_on_bus_idle(bool bus_idle, bool dma_holds_bus = false) {
        if (!bus_idle || dma_holds_bus) return;
        cpu_speed_ = pending_cpu_speed_;
    }

    /// NR 0x08 bit 6 — eff_nr_08_contention_disable (zxnext.vhd:4481).
    /// **Immediate** commit of the effective gate; both shadow and
    /// effective fields are updated. Used by Phase-A bare-class tests
    /// that need a known gate state without modelling the deferred
    /// `hc(8)` commit edge. Production NR 0x08 dispatch should instead
    /// call set_contention_disable_shadow() and let the per-tick
    /// commit_contention_disable_on_hc() helper latch on the next
    /// `hc(8)='1'` window per zxnext.vhd:5822-5823.
    void set_contention_disable(bool cd) {
        contention_disable_        = cd;
        contention_disable_shadow_ = cd;
    }

    /// Update the **shadow** value of NR 0x08 bit 6 only — the next
    /// `commit_contention_disable_on_hc()` call with `hc(8)='1'` will
    /// promote it to the effective gate. Models the latch at
    /// zxnext.vhd:5800-5823:
    ///     nr_08_contention_disable    <- shadow (immediate on NR 0x08 we)
    ///     eff_nr_08_contention_disable <- shadow only when hc(8)='1'
    /// This is the production NR 0x08 write path.
    void set_contention_disable_shadow(bool cd) {
        contention_disable_shadow_ = cd;
    }

    /// Commit the shadow → effective gate per VHDL:5822-5823. The VHDL
    /// process latches `eff_nr_08_contention_disable` from the shadow
    /// on every CPU bus-idle cycle while `hc(8)='1'`. A 9-bit `hc`
    /// counter has bit 8 set when `hc >= 256` — the right-border /
    /// horizontal-blank window. Caller invokes this once per
    /// instruction (or per-access) with the live raster `hc`; commits
    /// are no-ops while `hc < 256`. Idempotent.
    void commit_contention_disable_on_hc(uint16_t hc) {
        if ((hc & 0x100) != 0) {
            contention_disable_ = contention_disable_shadow_;
        }
    }

    /// Verify9-memory: NR 0x82 bit 1 mirror, gates `port_7ffd_active`
    /// (zxnext.vhd:2594). When set AND machine timing is 128K/+3 AND
    /// the address is a port_7ffd write, the VHDL OR-term at :4496
    /// asserts `port_contend='1'`. Pre-fix the bare-class
    /// `port_contend()` accessor dropped this term ("port_7ffd_active"
    /// gated by machine-timing-128/-p3 selection) — port 0x7FFD writes
    /// on 128K/+3 missed contention. Now wired here so the runtime
    /// caller can push NR 0x82 bit 1 directly.
    void set_port_7ffd_io_en(bool en) { port_7ffd_io_en_ = en; }
    bool port_7ffd_io_en() const { return port_7ffd_io_en_; }

    /// V15-CPU-NIT-03 (reviewer-promoted): NR 0x85 bit 0 mirror, gates
    /// `port_bf3b` / `port_ff3b` (ULA+ index/data) contention OR-terms
    /// at zxnext.vhd:4496. The VHDL signal is
    ///     port_ulap_io_en <= internal_port_enable(24);
    /// and `internal_port_enable = nr_85 & nr_84 & nr_83 & nr_82` so
    /// bit 24 = NR 0x85 bit 0.
    /// Pre-fix the CPU-side `fuse_z80_readport`/`writeport` callers in
    /// `src/cpu/z80_cpu.cpp` did not propagate this gate (they relied
    /// on the default `port_ulap_io_en=false` parameter), so ULA+ port
    /// IORQs to $BF3B/$FF3B during the active raster window on a
    /// 128K/+3 machine missed the per-cycle contention stretch. Same
    /// pattern as Verify9-memory's `port_7ffd_io_en_` fix — push the
    /// shadow in from the Emulator's NR 0x85 write handler so the
    /// CPU-side bus callbacks don't need a parameter.
    void set_port_ulap_io_en(bool en) { port_ulap_io_en_ = en; }
    bool port_ulap_io_en() const { return port_ulap_io_en_; }

    // ── V24-MEM-01 / V25-MEM-01 fix — machine_timing_ axis split ──────
    // VHDL anchor: zxnext.vhd:5761-5777 (combinational decode of
    // `eff_nr_03_machine_timing` into one-hot `machine_timing_48 / _128 /
    // _p3 / _pentagon`); :6694-6703 (latch `eff_nr_03_machine_timing <=
    // nr_03_machine_timing` on `video_frame_sync='1'`). The contention
    // surfaces (`mem_contend` at :4489-4493, `i_contention_en` at :4481,
    // `port_7ffd_active` at :2594) consume the one-hot
    // `machine_timing_*`, NOT the `machine_type_*` set fed by NR 0x03
    // bits 2:0 (typ_sel).
    //
    // The pair (`pending_machine_timing_`, `machine_timing_`) mirrors the
    // (`nr_03_machine_timing`, `eff_nr_03_machine_timing`) latch pair in
    // VHDL. NR 0x03 writes the shadow immediately (`set_pending`); the
    // emulator's per-frame seam calls `commit_pending_machine_timing()`
    // at the video-frame edge to promote it.
    //
    // Power-on default: VHDL :1099 `nr_03_machine_timing := "011"` (+3
    // timing) AND :1377 `eff_nr_03_machine_timing := "011"`. We follow
    // the same default but Emulator::init explicitly pushes the
    // tim_sel matching the CLI MachineType, so this default only
    // matters for unit-test fixtures that bypass Emulator.

    /// Immediate-commit: set BOTH the shadow and effective fields. Used
    /// by tests that need a known timing mode without modelling the
    /// video-frame commit edge. Production NR 0x03 dispatch should
    /// instead call set_pending_machine_timing() and let
    /// commit_pending_machine_timing() promote on the next frame edge.
    void set_machine_timing(MachineTimingMode m) {
        machine_timing_         = m;
        pending_machine_timing_ = m;
    }

    /// Update the **shadow** value (mirror of `nr_03_machine_timing`)
    /// only — the next commit_pending_machine_timing() call will
    /// promote it to the effective field. Models VHDL :5121-5135's
    /// gated write to nr_03_machine_timing (immediate on the gated
    /// NR 0x03 write).
    void set_pending_machine_timing(MachineTimingMode m) {
        pending_machine_timing_ = m;
    }

    /// Commit shadow → effective per VHDL :6694-6703 (`video_frame_sync
    /// = '1'`). Idempotent — re-running on the same frame edge is a
    /// no-op. Production callers invoke this from Emulator::run_frame()
    /// at the per-frame seam (one commit per video frame).
    void commit_pending_machine_timing() {
        machine_timing_ = pending_machine_timing_;
    }

    MachineTimingMode machine_timing() const { return machine_timing_; }
    MachineTimingMode pending_machine_timing() const { return pending_machine_timing_; }

    uint8_t mem_active_page()    const { return mem_active_page_; }
    uint8_t cpu_speed()          const { return cpu_speed_; }
    uint8_t pending_cpu_speed()  const { return pending_cpu_speed_; }
    bool    contention_disable() const { return contention_disable_; }
    bool    contention_disable_shadow() const { return contention_disable_shadow_; }

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
    /// (cpu_speed, contention_disable, mem_active_page)
    /// are consulted via the existing accessors — caller must update
    /// `mem_active_page` BEFORE calling for memory cycles.
    ///
    /// `port_ulap_io_en` mirrors NR 0x85 bit 0 (zxnext.vhd:2439 —
    /// `port_ulap_io_en <= internal_port_enable(24)`; bit 24 is the
    /// first bit of nr_85). Pass false if not modelled — the model's
    /// internal `port_ulap_io_en_` shadow is OR-folded with the
    /// parameter so CPU-side seams that don't have NR access still
    /// see the correct gate.
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
    /// Verify9-memory class-(c) → class-(a) fix: the `port_7ffd_active`
    /// OR-term (zxnext.vhd:2594) is now consumed by this accessor. The
    /// gate inputs (`port_7ffd_io_en_` shadow + `type_`) are state on
    /// the model itself, pushed by the runtime caller (Emulator) on
    /// every NR 0x82 write. The address decode (`cpu_a(15)='0' AND
    /// (cpu_a(14)='1' OR NOT p3_timing) AND port_fd AND NOT port_1ffd`)
    /// is computed inline. Calling this accessor for `cpu_a == 0x7FFD`
    /// on 128K/+3 with NR 0x82 bit 1 set now returns true (matches VHDL
    /// :4496 OR-term firing); pre-fix the term was dropped and 128K/+3
    /// port 0x7FFD writes silently missed contention.
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
    // cpu_speed_ is the **effective** gate input consulted by
    // is_contended_access() / contention_tick(); pending_cpu_speed_ is
    // the shadow updated by every NR 0x07 write. The two values are
    // equal outside the bus-idle commit window
    // (zxnext.vhd:5796-5828 — the latch fires only when
    // mreq_n & iorq_n & m1_n & not dma_holds_bus).
    uint8_t cpu_speed_         = 0;
    uint8_t pending_cpu_speed_ = 0;
    // contention_disable_ is the **effective** signal consulted by
    // is_contended_access(); contention_disable_shadow_ is the latch
    // updated by every NR 0x08 bit-6 write. The two values are equal
    // outside of mid-line write windows (see VHDL:5822-5823 and
    // commit_contention_disable_on_hc()).
    bool    contention_disable_        = false;
    bool    contention_disable_shadow_ = false;
    // Verify9-memory: NR 0x82 bit 1 mirror (zxnext.vhd:2399). Gates the
    // `port_7ffd_active` OR-term in `port_contend()`. Power-on default
    // matches NR 0x82 bit 1 = '0' (zxnext.vhd:1394 — internal_port_enable
    // resets to "00..."). Pushed by Emulator's NR 0x82 write handler.
    bool    port_7ffd_io_en_           = false;
    // V15-CPU-NIT-03 (reviewer-promoted): NR 0x85 bit 0 mirror
    // (zxnext.vhd:2439 — port_ulap_io_en <= internal_port_enable(24);
    // bit 24 = first bit of nr_85). Gates `port_bf3b`/`port_ff3b`
    // OR-terms in `port_contend()`. The VHDL signal `nr_85` resets to
    // all-1 (zxnext.vhd:1229), so the runtime-correct default is true,
    // but Emulator's init() explicitly pushes the actual NR 0x85 bit 0
    // through set_port_ulap_io_en() so this default only matters in
    // unit-test fixtures that bypass Emulator. We mirror the
    // `port_7ffd_io_en_` convention (false default, explicit push) for
    // consistency.
    bool    port_ulap_io_en_           = false;
    // V24-MEM-01 / V25-MEM-01 fix — machine_timing axis split.
    //   pending_machine_timing_  ← shadow (NR 0x03 bits 6:4 immediate)
    //   machine_timing_          ← effective (video-frame-latched)
    // VHDL :1099/:1377 power-on default = "011" (+3 timing); kept here.
    // Emulator::init pushes the tim_sel matching the CLI MachineType so
    // this default only matters for unit-test fixtures.
    MachineTimingMode machine_timing_         = MachineTimingMode::TimingPlus3;
    MachineTimingMode pending_machine_timing_ = MachineTimingMode::TimingPlus3;
};
