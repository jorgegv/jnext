#pragma once
#include <cstdint>

class StateWriter;
class StateReader;

/// NMI Source / central arbiter subsystem.
///
/// Models the central NMI source pipeline from `cores/zxnext/src/zxnext.vhd`
/// lines 2089-2170 (plus the NR 0x02 software-NMI strobes at 3830-3872 and
/// the NR 0x02 readback at 5891).
///
/// The subsystem owns:
///   * Three combinational producers (`nmi_assert_mf`, `nmi_assert_divmmc`,
///     `nmi_assert_expbus` — VHDL:2089-2091) with their NR 0x06 / NR 0x81
///     enable gates.
///   * Three priority latches (`nmi_mf`, `nmi_divmmc`, `nmi_expbus`) with
///     MF > DivMMC > ExpBus priority and the VHDL:2095-2116 set / clear
///     logic.
///   * The 4-state FSM (`S_NMI_IDLE -> S_NMI_FETCH -> S_NMI_HOLD ->
///     S_NMI_END`) from VHDL:2120-2162.
///   * The `nmi_generate_n` output that drives Z80 `/NMI` (VHDL:2164-2170,
///     `z80_nmi_n <= nmi_generate_n` at VHDL:1841).
///
/// This Phase-1 scaffold lands the class + API + reset defaults. Phase-2
/// waves populate the producers, gates, and consumer-feedback hooks.
///
/// Priority chain (VHDL:2097-2105) — each latch set combinationally from
/// its assert signal and gated by the higher-priority latches being clear
/// plus the per-consumer "active" mask:
///
///   nmi_mf     set iff nmi_assert_mf AND NOT port_e3_reg(7)
///   nmi_divmmc set iff nmi_assert_divmmc AND NOT mf_is_active AND NOT nmi_mf
///   nmi_expbus set iff nmi_assert_expbus AND NOT (nmi_mf OR nmi_divmmc)
///
/// Clear conditions (VHDL:2102-2105): any of { reset, nr_03_config_mode,
/// FSM in S_NMI_END }.
///
/// `nmi_generate_n` is active-low (VHDL:2164-2170):
///   '0' while FSM is in S_NMI_FETCH (request taken, /NMI asserted) or
///   while FSM is in S_NMI_IDLE AND nmi_activated (immediate assertion on
///   entry to FETCH, plus the NR 0x81 bit-5 debounce-disable fast path for
///   the ExpBus producer).
///
/// Phase-1 notes:
///   * Producers and gate setters are implemented and store state; FSM
///     transitions run on tick().
///   * Consumer feedback inputs (mf_nmi_hold / mf_is_active) default to
///     `false` — real MF values arrive with Task 8 (Multiface).
///   * ExpBus pin defaults to inactive (`true`, active-low high) per
///     VHDL `i_BUS_NMI_n` power-on.
///   * `observe_m1_fetch(pc, m1, mreq)` drives FETCH -> HOLD only when
///     pc == 0x0066 AND m1 AND mreq — matches the `mf_a_0066` signal
///     pattern in VHDL:2135-2138.
///   * `observe_cpu_wr(wr_n)` drives END -> IDLE on the rising edge of
///     wr_n (matches VHDL:2149-2162 `cpu_wr_n = 1` completion of the
///     handler's last bus cycle).
///   * Stackless NMI (NR 0xC0 bit 3) is explicitly out of scope — Wave D
///     cut per plan Q1. The FSM models the standard push-vector path only.
class NmiSource {
public:
    /// Which producer latched the current request.
    enum class Src : uint8_t {
        None   = 0,
        Mf     = 1,
        DivMmc = 2,
        ExpBus = 3,
    };

    /// FSM state — VHDL:2120-2162 `nmi_state_t`.
    enum class State : uint8_t {
        Idle  = 0,  ///< S_NMI_IDLE
        Fetch = 1,  ///< S_NMI_FETCH
        Hold  = 2,  ///< S_NMI_HOLD
        End   = 3,  ///< S_NMI_END
    };

    NmiSource();

    /// Hard reset — clears all latches and FSM to IDLE.
    /// VHDL:2120 (FSM power-on `S_NMI_IDLE`), VHDL:2095-2105 (latches
    /// clear on i_reset). Gate flags reset to their VHDL power-on values
    /// (all '0').
    void reset();

    // ---------------------------------------------------------------
    // Producer inputs (Waves A/B wire these up).
    // ---------------------------------------------------------------

    /// Sticky setter for the MF hotkey / Multiface button line.
    /// VHDL `hotkey_m1` is an edge pulse; `set_mf_button(true)` models
    /// the pressed state and `set_mf_button(false)` releases it.
    void set_mf_button(bool v);

    /// One-cycle strobe helper — raises the MF button for this tick and
    /// lowers it on the next tick(). Matches `hotkey_m1` edge semantics.
    void strobe_mf_button();

    /// Sticky setter for the DivMMC hotkey (drive button) line.
    /// VHDL `hotkey_drive` edge pulse.
    void set_divmmc_button(bool v);

    /// One-cycle strobe helper for the DivMMC button.
    void strobe_divmmc_button();

    /// NR 0x02 write — decodes bit 3 (MF software NMI) and bit 2 (DivMMC
    /// software NMI) per VHDL:3830-3872 (`nmi_cpu_02_we` /
    /// `nmi_cu_02_we` -> `nmi_sw_gen_mf` / `nmi_sw_gen_divmmc`).
    /// Copper MOVE-to-NR 0x02 reaches here via `NextReg::write` dispatch
    /// (no extra hook — audit confirmed in plan §"Copper NR-write path").
    void nr_02_write(uint8_t v);

    /// NR 0x02 readback per VHDL:5891. Bits 3 / 2 auto-clear when the
    /// FSM transitions through `S_NMI_END`.
    uint8_t nr_02_read() const;

    /// IO-trap producer stub (NR 0xD8 FDC trap path). Unused in this
    /// plan; present for VHDL-faithfulness of the producer count.
    void strobe_iotrap();

    /// Drive the ExpBus /NMI pin directly. Active-low: `true` = idle,
    /// `false` = asserting. VHDL `i_BUS_NMI_n` power-on is high.
    void set_expbus_nmi_n(bool v);

    // ---------------------------------------------------------------
    // Gate-register inputs (Wave C wires these).
    // ---------------------------------------------------------------

    /// NR 0x06 bit 3 — `button_m1_nmi_en` (VHDL:1110). Gates the MF
    /// producer.
    void set_mf_enable(bool v);

    /// NR 0x06 bit 4 — `button_drive_nmi_en` (VHDL:1109). Gates the
    /// DivMMC producer.
    void set_divmmc_enable(bool v);

    /// NR 0x81 bit 5 — `expbus_nmi_debounce_disable` (VHDL:1222).
    /// When set, ExpBus assertion is immediate (debounce path bypassed).
    void set_expbus_debounce_disable(bool v);

    /// `nr_03_config_mode` (VHDL:2102-2105). When true, force-clears all
    /// three request latches every cycle and holds the FSM in IDLE.
    void set_config_mode(bool v);

    // ---------------------------------------------------------------
    // Consumer-feedback inputs (Wave B wires DivMMC; MF stubbed).
    // ---------------------------------------------------------------

    /// MF consumer hold — VHDL `mf_disable_nmi` (Task 8). Stubbed
    /// `false` in this plan; Task 8 replaces the stub.
    void set_mf_nmi_hold(bool v);

    /// MF currently active — blocks DivMMC latch per VHDL:2099. Stubbed
    /// `false` in this plan; Task 8 replaces the stub.
    void set_mf_is_active(bool v);

    /// DivMMC consumer hold — VHDL `o_disable_nmi` (divmmc.vhd:150) =
    /// `automap_held OR button_nmi`.  Wave B routes this from
    /// `DivMmc::is_nmi_hold()`.
    void set_divmmc_nmi_hold(bool v);

    /// Port 0xE3 bit 7 (CONMEM). Blocks MF latch per VHDL:2098.
    /// Wave B routes this from `DivMmc::is_conmem()`.
    void set_divmmc_conmem(bool v);

    // ---------------------------------------------------------------
    // Z80-side observer inputs (Wave A/B/E wire these).
    // ---------------------------------------------------------------

    /// Observe an M1 fetch. When `pc == 0x0066 AND m1 AND mreq`, the
    /// FSM advances FETCH -> HOLD (VHDL:2135-2138). Other M1 fetches
    /// are ignored.
    void observe_m1_fetch(uint16_t pc, bool m1, bool mreq);

    /// Observe the CPU `WR_n` line. A rising edge drives END -> IDLE
    /// per VHDL:2149-2162 (the handler's last bus cycle completes).
    void observe_cpu_wr(bool wr_n);

    /// Observe a RETN instruction. Optional auxiliary END advance;
    /// the FSM already advances via `observe_cpu_wr` rising-edge, so
    /// this is a no-op in the default build. Present for future
    /// Task-8 / stackless-NMI hooks.
    void observe_retn();

    // ---------------------------------------------------------------
    // Outputs — consumed by Emulator per tick.
    // ---------------------------------------------------------------

    /// Active-low NMI output to the Z80 (`'0'` asserts /NMI).
    /// VHDL:2164-2170, `z80_nmi_n <= nmi_generate_n` at VHDL:1841.
    bool nmi_generate_n() const;

    /// True when any of the three request latches is set. Feeds the
    /// `nmi_activated` term in VHDL:2007 (`im2_dma_delay` OR chain).
    bool is_activated() const;

    State state() const { return state_; }
    Src   latched() const;

    // ---------------------------------------------------------------
    // Per-cycle advance.
    // ---------------------------------------------------------------

    /// Advance the FSM + re-evaluate producer / priority combinational
    /// logic. `master_cycles` is the 28 MHz tick count since the last
    /// call, matching the CTC / UART / Md6 tick signatures.
    void tick(uint32_t master_cycles);

    // ---------------------------------------------------------------
    // State persistence.
    // ---------------------------------------------------------------

    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

    // ---------------------------------------------------------------
    // Test accessors — VHDL signal names for unit-test rows.
    // ---------------------------------------------------------------

    bool nmi_mf()     const { return nmi_mf_; }
    bool nmi_divmmc() const { return nmi_divmmc_; }
    bool nmi_expbus() const { return nmi_expbus_; }

    bool mf_button()     const { return mf_button_; }
    bool divmmc_button() const { return divmmc_button_; }
    bool nmi_sw_gen_mf()     const { return nmi_sw_gen_mf_; }
    bool nmi_sw_gen_divmmc() const { return nmi_sw_gen_divmmc_; }

    bool mf_enable()              const { return mf_enable_; }
    bool divmmc_enable()          const { return divmmc_enable_; }
    bool expbus_debounce_disable() const { return expbus_debounce_disable_; }
    bool config_mode()            const { return config_mode_; }

    bool mf_nmi_hold()      const { return mf_nmi_hold_; }
    bool mf_is_active()     const { return mf_is_active_; }
    bool divmmc_nmi_hold()  const { return divmmc_nmi_hold_; }
    bool divmmc_conmem()    const { return divmmc_conmem_; }
    bool expbus_nmi_n()     const { return expbus_nmi_n_; }

    /// Combinational `nmi_assert_*` per VHDL:2089-2091, exposed for
    /// tests that probe the producer layer before the priority latches.
    bool nmi_assert_mf()     const;
    bool nmi_assert_divmmc() const;
    bool nmi_assert_expbus() const;

private:
    // Re-evaluate combinational producers + priority latches + FSM
    // transition. Called from tick() and from any setter that might
    // change the combinational state.
    void recompute_();

    // ---- Producer inputs (raw lines from outside world) ----
    bool mf_button_     = false;
    bool divmmc_button_ = false;
    bool expbus_nmi_n_  = true;   // active-low, idle = '1'

    // One-cycle strobe flags — raised by strobe_*(), consumed in tick()
    // after combinational producers observe them.
    bool strobe_mf_button_pending_     = false;
    bool strobe_divmmc_button_pending_ = false;

    // NR 0x02 software-NMI strobes (one-shot). VHDL:3830-3872.
    bool nmi_sw_gen_mf_     = false;
    bool nmi_sw_gen_divmmc_ = false;

    // IO-trap stub (VHDL:3830-3872 chain — `nmi_gen_iotrap`). Not
    // currently routed to a producer; kept for a future wave.
    bool iotrap_strobe_pending_ = false;

    // ---- Gate flags ----
    bool mf_enable_              = false;  // NR 0x06 bit 3
    bool divmmc_enable_          = false;  // NR 0x06 bit 4
    bool expbus_debounce_disable_ = false; // NR 0x81 bit 5
    bool config_mode_            = false;  // nr_03_config_mode

    // ---- Consumer-feedback inputs ----
    bool mf_nmi_hold_     = false;
    bool mf_is_active_    = false;
    bool divmmc_nmi_hold_ = false;
    bool divmmc_conmem_   = false;   // port 0xE3 bit 7

    // ---- Priority latches (VHDL:2095-2116) ----
    bool nmi_mf_     = false;
    bool nmi_divmmc_ = false;
    bool nmi_expbus_ = false;

    // ---- FSM ----
    State state_ = State::Idle;

    // ---- NR 0x02 readback pending bits (cleared at S_NMI_END) ----
    // VHDL:5891 auto-clear semantics. Track MF / DivMMC software-NMI
    // acceptance independently; each bit clears when the corresponding
    // latch clears (which happens at END).
    bool nr_02_pending_mf_     = false;
    bool nr_02_pending_divmmc_ = false;

    // ---- Prev-cycle edge tracking ----
    bool prev_wr_n_ = true;
};
