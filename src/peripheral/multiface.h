#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

/// Multiface peripheral — Wave 1 B1 of Task 8 (TASK-8-MULTIFACE-PLAN.md).
///
/// Mirrors the FPGA-core's `multiface.vhd` end-to-end (197 lines). The VHDL
/// implements three historical variants — Multiface 1, Multiface 128, and
/// Multiface +3 — selected by NR 0x0A bits 7:6 (`nr_0a_mf_type`). All three
/// share the same internal four-flip-flop state machine plus mode-conditional
/// port-decode behaviour.
///
/// Header comment block (multiface.vhd:22-48) summarises the per-mode
/// I/O protocol verbatim:
///
///   MF1 (mf_mode_i = "10" or any "01"):
///     Port write 1F,9F:           NMI_ACTIVE off
///     Port read  1F:              MF_ENABLE  off
///     Port read  9F:              MF_ENABLE  on
///   MF3 (mf_mode_i = "00"):
///     Port write BF,3F:           NMI_ACTIVE off
///     Port write BF:              INVISIBLE  on
///     Port read  BF:              MF_ENABLE  off
///     Port read  3F:              if INVISIBLE off: MF_ENABLE on
///     Port read  1F3F / 7F3F:     if INVISIBLE off: returns 1ffd / 7ffd
///   Common:
///     0x66 instruction fetch:     MF_ENABLE on if NMI_ACTIVE on
///     button + NMI_ACTIVE off:    NMI_ACTIVE on (and INVISIBLE off on MF3)
///
/// B1 scope: state machine + button-source consumer + ROM/RAM buffers + the
/// per-mode-decoded boolean strobes (driven by Wave B2's port handler in a
/// later mini-branch). NO MMU overlay (Wave 1 E), NO port-decode dispatch
/// (Wave 1 B2), NO MF+3 readback mux (Wave 1 B3).
///
/// VHDL authority: `cores/zxnext/src/device/multiface.vhd:1-197`.
class Multiface {
public:
    static constexpr int kRomSize = 0x2000;  // 8 KB
    static constexpr int kRamSize = 0x2000;  // 8 KB

    Multiface();

    /// Reset.
    ///
    /// `hard=true` performs the VHDL `reset_i='1'` reset sequence:
    ///   - port_io_dly := '0'      (multiface.vhd:126)
    ///   - nmi_active  := '0'      (multiface.vhd:141)
    ///   - invisible   := '1'      (multiface.vhd:156)
    ///   - mf_enable   := '0'      (multiface.vhd:175)
    /// `hard=false` is currently identical (the VHDL spec has only one
    /// reset_i pin); the parameter exists so future soft-vs-hard
    /// asymmetry (e.g. preserving RAM contents across hard reset for
    /// state-restore) has a single API entry point.
    ///
    /// RAM is zero-cleared on `hard=true` only. ROM is preserved.
    void reset(bool hard = true);

    // ── Wiring inputs ────────────────────────────────────────────────────

    /// VHDL `enable_i = port_multiface_io_en` (zxnext.vhd:2415-2416, NR 0x83
    /// bit 1). When false, the internal `reset` signal is forced high
    /// (`reset = reset_i OR NOT enable_i`, multiface.vhd:103) so all four
    /// flip-flops are held in their reset state. Re-enabling does NOT
    /// auto-restore prior state — the FFs come out of held-reset cleanly.
    void set_enabled(bool en);
    bool is_enabled() const { return enabled_; }

    /// VHDL `mf_mode_i` from NR 0x0A bits 7:6 (nr_0a_mf_type).
    /// Decode (multiface.vhd:112-116):
    ///   "00" -> mode_p3
    ///   "11" -> mode_48 (= MF1)
    ///   "01" / "10" -> mode_128
    /// Only the low two bits are observed; callers may pass the raw NR
    /// 0x0A high-nibble shifted right or just the 2-bit field.
    void set_mode(uint8_t mf_type);

    /// Raw 2-bit `nr_0a_mf_type` value last set via set_mode(). Wave 1 B2
    /// port-dispatch needs the per-bit value (not just the mode_p3/_128/_48
    /// triple) because `mode_128` covers both `01` and `10`, which select
    /// DIFFERENT enable/disable port LSBs per VHDL zxnext.vhd:2612-2613.
    uint8_t mf_type() const { return mf_type_; }

    // ── Producer/consumer hooks ──────────────────────────────────────────

    /// One-shot button press. VHDL `button_pulse <= button_i AND NOT
    /// nmi_active` (multiface.vhd:135) — the press only takes effect when
    /// no NMI is currently held. We model this as: this call raises
    /// button_i for one clock and runs the FF process once, then drops
    /// button_i. Subsequent presses while nmi_active=1 are no-ops, matching
    /// VHDL.
    void button_press();

    /// CPU M1 + MREQ + address observer. VHDL `fetch_66 <= '1' when
    /// cpu_a_0066_i='1' AND cpu_m1_n_i='0' AND nmi_active='1' else '0'`
    /// (multiface.vhd:169) — the combinational signal that latches
    /// `mf_enable <= '1'` when MREQ is also low (line 176). The
    /// `mf_enable_eff` output also OR's `fetch_66` directly so the same
    /// M1 sees the overlay (line 186, one-cycle bypass).
    ///
    /// Call this on every M1 prefetch — same hook DivMmc::check_automap
    /// uses; the Emulator's on_m1_prefetch / on_m1_cycle lambda is the
    /// single seam.
    ///
    /// `pc` is the address being fetched; `mreq_low` mirrors VHDL's
    /// `cpu_mreq_n_i='0'` (true during the fetch's memory cycle).
    ///
    /// Task 27 C-M1 — inline fast path: when `m1_quiescent_()` proves
    /// the clock edge cannot change any FF (see the predicate for the
    /// per-FF proof), the call reduces to a few byte loads. Behaviour
    /// is identical except that the trace-level clock_edge_ log line is
    /// not emitted for these provably state-preserving edges.
    void on_m1(uint16_t pc, bool mreq_low) {
        if (m1_quiescent_()) return;
        on_m1_clock_(pc, mreq_low);
    }

    /// VHDL `cpu_retn_seen_i` pulse. Clears nmi_active (line 144) AND
    /// mf_enable (line 178) for one cycle. Emulator latches
    /// Im2Controller::retn_seen_this_cycle() during decode and calls this
    /// after RETN executes, before the returned-to opcode is predecoded.
    void on_retn_seen();

    // ── Port-strobe hooks (driven by Wave B2 port handlers) ──────────────
    //
    // These are the four `port_mf_*` inputs to multiface.vhd. B1 exposes
    // the API shape but no Z80 port writes drive them yet; B2 will wire
    // the per-mode decode at zxnext.vhd:2612-2616 to invoke these on the
    // appropriate port-IO strobes.
    //
    // Each call models a single rising edge on the corresponding VHDL
    // input pin: `active=true` pulses the line for one clock and runs
    // the FF process once. `active=false` is a no-op (the lines are
    // edge-pulses, not steady-state; the port_io_dly edge detector at
    // multiface.vhd:122-131 expects rising-edge transitions only).

    /// Models a rising-edge pulse on `port_mf_enable_rd_i`.
    /// VHDL effects: latches `mf_enable <= NOT invisible_eff` (line 180-181).
    void on_port_enable_rd(bool active);
    /// Models a rising-edge pulse on `port_mf_enable_wr_i`.
    /// VHDL effects: clears nmi_active (line 144) iff port_io_dly=0;
    /// sets invisible (line 159) iff mode_p3=1 AND port_io_dly=0.
    void on_port_enable_wr(bool active);
    /// Models a rising-edge pulse on `port_mf_disable_rd_i`.
    /// VHDL effects: clears mf_enable (line 178); also clears nmi_active
    /// when mode_p3=1 AND port_io_dly=0 (line 144 third disjunct).
    void on_port_disable_rd(bool active);
    /// Models a rising-edge pulse on `port_mf_disable_wr_i`.
    /// VHDL effects: clears nmi_active (line 144) iff port_io_dly=0;
    /// sets invisible (line 159) iff mode_p3=0 AND port_io_dly=0.
    void on_port_disable_wr(bool active);

    // ── State accessors ──────────────────────────────────────────────────

    /// VHDL `mf_enabled_o = mf_enable_eff = mf_enable OR fetch_66`
    /// (multiface.vhd:186, 190). This is the signal Wave 1 E will use to
    /// gate the slot-0 ROM/RAM overlay.
    bool is_mem_active() const { return mf_enable_ || fetch_66_live_; }

    /// VHDL `nmi_disable_o = nmi_active` (multiface.vhd:191). High while
    /// the Multiface is holding /NMI; consumed by NmiSource as
    /// `mf_nmi_hold` via Emulator::run_frame pre-tick fan-out (Task 8).
    bool is_nmi_hold() const { return nmi_active_; }

    /// VHDL `mf_is_active = mf_mem_en OR mf_nmi_hold` (zxnext.vhd:2099).
    /// Drives the DivMMC retn_seen AND-NOT gate at zxnext.vhd:4111 (Wave
    /// 1 F-gate) and the MF-vs-DivMMC priority mask at zxnext.vhd:2098.
    bool is_active() const { return is_mem_active() || is_nmi_hold(); }

    /// VHDL `mf_port_en_o` (multiface.vhd:195) — the "MF +3 / MF128 port
    /// readback enable" signal. Drives the +3 readback mux in Wave 1 B3.
    /// Returns true when:
    ///   port_mf_enable_rd_i = '1'
    ///   AND invisible_eff = '0'
    ///   AND (mode_128 = '1' OR mode_p3 = '1')
    /// `invisible_eff = invisible AND NOT mode_48` (line 165).
    bool mf_port_en() const { return mf_port_en_; }

    // ── Direct FF accessors (for tests and integration debug) ────────────

    bool nmi_active() const { return nmi_active_; }
    bool invisible() const { return invisible_; }
    bool mf_enable() const { return mf_enable_; }
    bool port_io_dly() const { return port_io_dly_; }

    bool mode_p3() const { return mode_p3_; }
    bool mode_128() const { return mode_128_; }
    bool mode_48() const { return mode_48_; }

    /// VHDL `invisible_eff = invisible AND NOT mode_48` (multiface.vhd:165).
    bool invisible_eff() const { return invisible_ && !mode_48_; }

    // ── External SRAM backing (Task 26 item 5) ───────────────────────────
    //
    // VHDL zxnext.vhd:3029-3036 hard-wires the Multiface memory window
    // ($0000-$3FFF when mf_mem_en=1) to external SRAM: the ROM half
    // (cpu_a(13)=0) maps to physical page 0x0A (read-only), the RAM half
    // (cpu_a(13)=1) maps to page 0x0B. `sram_pre_bank5` is forced '0'
    // (:3033) so it is the external SRAM chip, NOT the internal bank-5
    // dual-port VRAM (which shares the numeric page value only). On the
    // real Next, tbblue.fw loads `enNextMf.rom` into page 0x0A via the
    // config-mode NR $04=$05 window during boot.
    //
    // Emulator wires these Next-only (mirroring the DivMmc
    // set_ram_backing(ram_.page_ptr(16)) precedent). When set, rom_data()/
    // ram_data() return the external pages so the MMU overlay reads/writes
    // physical SRAM 0x0A/0x0B. STANDALONE machines (48k/128k/plus3), where
    // a real standalone Multiface had its own private RAM/ROM chip, keep
    // the private buffers (backing left null). save_state() always
    // serialises the PRIVATE buffer: on Next the live MF RAM lives in page
    // 0x0B and is round-tripped by the main SRAM snapshot, so serialising
    // the (unused) private buffer is harmless and schema-stable; on
    // standalone the private buffer IS the RAM and is saved correctly.
    void set_rom_backing(uint8_t* base) { rom_ext_ = base; }
    void set_ram_backing(uint8_t* base) { ram_ext_ = base; }

    // ── ROM/RAM buffer access (consumed by Wave 1 E MMU overlay) ─────────

    const uint8_t* rom_data() const { return rom_ext_ ? rom_ext_ : rom_.data(); }
    uint8_t*       ram_data()       { return ram_ext_ ? ram_ext_ : ram_.data(); }
    const uint8_t* ram_data() const { return ram_ext_ ? ram_ext_ : ram_.data(); }

    /// Load the 8 KB Multiface ROM from a byte buffer (extracted from SD
    /// `/MACHINES/NEXT/enNextMf.rom`). Mirrors `DivMmc::load_rom_bytes`
    /// from Wave 0.3: short buffers are padded with 0xFF, oversized ones
    /// truncated.
    bool load_rom_bytes(const uint8_t* data, size_t size);

    // ── Save / load state ────────────────────────────────────────────────

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    // Mode decode (multiface.vhd:105-118).
    bool mode_p3_  = false;
    bool mode_128_ = false;
    bool mode_48_  = false;

    // Raw 2-bit nr_0a_mf_type last latched (mode_128 covers both `01` and
    // `10`; the per-bit value is needed to disambiguate the port LSB).
    uint8_t mf_type_ = 0x00;

    // Four flip-flops (multiface.vhd:81-99).
    bool nmi_active_ = false;   // line 92
    bool invisible_  = true;    // line 94 (reset value '1' per line 156)
    bool mf_enable_  = false;   // line 98
    bool port_io_dly_ = false;  // line 89

    // VHDL `fetch_66` is combinational; we expose it transiently inside
    // on_m1() and clear it before returning so `is_mem_active()` reflects
    // the same one-cycle bypass that VHDL's `mf_enable_eff` provides on
    // the M1 of the 0x0066 fetch itself.
    bool fetch_66_live_ = false;

    // VHDL `enable_i` (multiface.vhd:64). Modelled via the combinational
    // `reset = reset_i OR NOT enable_i` at line 103: when enabled_=false,
    // every clock-edge-process call ends with the FFs forced to reset.
    bool enabled_ = false;

    // VHDL `mf_port_en_o` is combinational (line 195). Recomputed at the
    // end of every clock-edge-process call so the public accessor returns
    // the most recent steady-state value.
    bool mf_port_en_ = false;

    std::array<uint8_t, kRomSize> rom_;
    std::array<uint8_t, kRamSize> ram_;

    // External SRAM backing (Task 26 item 5). Non-owning; when non-null the
    // MMU overlay reads/writes these (physical SRAM pages 0x0A/0x0B on
    // Next) instead of the private buffers above. Null on standalone
    // machines. Not serialised (they point into Ram, which owns the state).
    uint8_t* rom_ext_ = nullptr;
    uint8_t* ram_ext_ = nullptr;

    // ── Internal helpers ─────────────────────────────────────────────────

    /// Run one rising-edge clock cycle of the four FF processes with the
    /// given combinational input pulses. Mirrors `process(clock_i)` blocks
    /// at multiface.vhd:122-131, 137-148, 152-163, 171-184.
    /// All four port_* inputs and the button input are CONSUMED by this
    /// call (the caller passes the cycle's edge-pulse values; on return
    /// they're "spent").
    void clock_edge_(bool button,
                     bool port_en_rd,
                     bool port_en_wr,
                     bool port_dis_rd,
                     bool port_dis_wr,
                     bool a_0066,
                     bool m1_low,
                     bool mreq_low,
                     bool retn_seen);

    /// Recompute the combinational `mf_port_en_o` signal per VHDL line 195.
    /// This depends on the `port_mf_enable_rd_i` input present on THIS cycle,
    /// so we pass it through; outside of clock_edge_ it stays `false` (the
    /// VHDL signal is purely combinational and falls back to '0' between
    /// strobes).
    void update_mf_port_en_(bool port_en_rd);

    /// Task 27 C-M1 — true iff an on_m1() clock edge (button=0, all four
    /// port pulses=0, retn=0, m1_low=1, any pc/mreq) is a provable no-op
    /// on every FF and combinational output, so the edge may be skipped.
    ///
    /// Enabled case (clock_edge_ body vs multiface.vhd):
    ///   * fetch_66 (vhd:169) = a_0066 AND m1 AND nmi_active_prev — with
    ///     nmi_active_=0 it is 0 REGARDLESS of pc, so no pc term needed.
    ///   * port_io_dly (vhd:122-131) <= OR(port pulses)=0 — no-op iff
    ///     already 0 (gated below).
    ///   * nmi_active (vhd:137-148): button_pulse=0; clear path needs
    ///     retn OR port pulses — all 0 — unchanged.
    ///   * invisible (vhd:152-163): button_pulse=0, inv_set needs port
    ///     pulses — unchanged.
    ///   * mf_enable (vhd:171-184): fetch_66=0, port/retn inputs 0 —
    ///     unchanged (any current value, 0 or 1, is preserved).
    ///   * fetch_66_live_ <= fetch_66=0 — no-op iff already 0 (gated).
    ///   * mf_port_en_ (vhd:195) <= port_en_rd AND … = 0 — no-op iff
    ///     already 0 (gated). NOTE: this term is redundant-by-invariant
    ///     (mf_port_en_=1 only arises on a port-strobe edge, which also
    ///     latches port_io_dly_=1 on the SAME edge, and both are cleared
    ///     together by the next edge — so !port_io_dly_ already covers
    ///     it; mutation-equivalent). Kept deliberately: the predicate's
    ///     safety argument stays local to the FF list instead of
    ///     depending on a cross-field invariant that future strobe
    ///     plumbing could break.
    /// Disabled case (vhd:103 reset = NOT enable_i): clock_edge_ forces
    /// the reset state {nmi=0, invisible=1, mf_enable=0, dly=0,
    /// fetch66=0, port_en=0} — no-op iff the state already equals it.
    bool m1_quiescent_() const {
        if (enabled_) {
            return !nmi_active_ && !port_io_dly_ &&
                   !fetch_66_live_ && !mf_port_en_;
        }
        return !nmi_active_ && invisible_ && !mf_enable_ &&
               !port_io_dly_ && !fetch_66_live_ && !mf_port_en_;
    }

    /// Out-of-line tail of on_m1() (Task 27 C-M1 split): the original
    /// clock_edge_ dispatch, unchanged.
    void on_m1_clock_(uint16_t pc, bool mreq_low);
};
