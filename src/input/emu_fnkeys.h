#pragma once
#include <cstdint>
#include <functional>

/// ZX Spectrum Next "Multiface" F-key emulation FSM.
///
/// VHDL reference:
///   input/membrane/emu_fnkeys.vhd:53-202 — 7-state FSM consuming the M1
///     and Reset buttons plus the membrane rows/cols, producing a 10-bit
///     fnkey strobe vector `o_fnkeys[10:1]`.
///   zxnext.vhd:6340-6349 — top-level edge-detect uses hotkeys_0/_1 to
///     turn each F-key strobe into a one-cycle hotkey_* pulse, gated on
///     NR 0x06 for some keys (F3, F5, F6, F8) and on `port_divmmc_io_en`
///     for F10.
///   zxnext.vhd:5789-5791 — F8 (`hotkey_cpu_speed`) increments
///     `nr_07_cpu_speed`.
///   zxnext.vhd:5839-5841 — F3 (`hotkey_5060`) toggles `nr_05_5060`.
///   zxnext.vhd:5849-5852 — F2 (`hotkey_scandouble`) toggles
///     `nr_05_scandouble_en`.
///   zxnext.vhd:5861-5863 — F7 (`hotkey_scanlines`) increments
///     `nr_09_scanlines`.
///   zxnext.vhd:5165-5166 — NR 0x06 bit-decode (hotkey_5060_en bit 5,
///     hotkey_cpu_speed_en bit 7).
///
/// State machine (VHDL :61):
///   S_IDLE → either of:
///     • Reset button asserted (`i_button_reset_n = '0'`)  → S_RESET_CHECK
///     • M1 button asserted    (`i_button_m1_n    = '0'`)  → S_MF_ROW_A11
///   S_MF_ROW_A11   — local_rows forced to "11110111" (A11=0); latches
///                    `local_fnkeys[5:1] = NOT i_cols[4:0]` from the
///                    1/2/3/4/5 row (F1..F5 detect).
///   S_MF_ROW_A12   — local_rows forced to "11101111" (A12=0); latches
///                    `local_fnkeys[10:6] = NOT (i_cols[0]&...&i_cols[4])`
///                    from the 0/9/8/7/6 row (F10..F6 — note the bit
///                    reversal on the col field).
///   S_MF_CHECK     — if M1 still held, go back to S_MF_ROW_A11; else
///                    advance to S_MF_DONE.
///   S_MF_DONE      — emits the F9 (Multiface NMI) strobe iff:
///                      (timer not expired) AND (cancel_nmi = 0).
///                    `cancel_nmi` is set when ANY column pulse occurred
///                    during the M1-hold sampling window (VHDL :171-177)
///                    — i.e. the user "cancels" the M1 NMI by holding
///                    M1 and pressing any other key, which the FSM
///                    interprets as a "selected F-key", not as a bare M1
///                    NMI.
///   S_RESET_CHECK  — wait for reset button release.
///   S_RESET_DONE   — emit either F1 (hard reset, long press) or F4
///                    (soft reset, short press) based on the press
///                    duration timer.
///
/// Output vector `local_fnkeys[10:1]`: bit i = 1 indicates F(i) is
/// "strobed". F1..F5 are latched on entering S_MF_ROW_A11 and F6..F10
/// on entering S_MF_ROW_A12; both are held through S_MF_CHECK.
/// S_MF_DONE replaces the vector with the F9 term only, S_RESET_DONE
/// with F1 or F4, and S_IDLE clears it. The Z80 top-level edge-detector
/// (`hotkeys_0/_1` at zxnext.vhd:6328-6334) then pulse-emits one-cycle
/// hotkey_X strobes on the rising edge of the corresponding fnkey bit.
///
/// In the JNEXT C++ port, the **side-effect strobes** for F2/F3/F7/F8
/// are emitted directly by `tick()` on the rising edge of the
/// corresponding `local_fnkeys` bit, i.e. on the tick that enters
/// S_MF_ROW_A11 (F2, F3) or S_MF_ROW_A12 (F7, F8). The host
/// installs side-effect callbacks via `set_*_callback()` (one per key)
/// and the strobes are gated by a `nr_06_hotkey_*_en` snapshot the
/// host pushes via `set_nr_06_hotkey_enables()` before the tick.
///
/// F1/F4/F9/F10 reset/NMI dispatch is handled by the host's
/// `Emulator::on_hotkey_f1_hard_reset()` / `_f4_soft_reset()` /
/// `_f9_mf_nmi()` / `_f10_divmmc_nmi()` (G152). EmuFnKeys does NOT
/// re-dispatch them — its callbacks for those slots are intentionally
/// absent (G132 is scoped to F2/F3/F7/F8 only). F5/F6 (G147) are
/// out-of-scope for G132 and tracked separately.
///
/// Sequencing convention: one `tick()` is one `i_CLK_EN` period, i.e.
/// one `CLK_28_DEBOUNCE_EN` pulse: 2^18 CLK_28 cycles of emulated time,
/// about 9.36 ms (zxnext_top_issue2.vhd:313, :1168-1171, :1177, :1967).
/// The long-press PERIOD is BUTTON_PERIOD_MS / CLOCK_EN_PERIOD_MS =
/// 1000 / 10 = 100 ticks (emu_fnkeys.vhd:55), about 936 ms. The FSM's
/// per-CLK_28 state steps are collapsed to one per tick, which the
/// timer cannot see (it loads only in S_IDLE). Nothing in production
/// ticks the FSM on a clock; only `simulate_mf_fkey_press()` drives it.
/// Tests can override the period via the constructor.
class EmuFnKeys {
public:
    enum class State : uint8_t {
        IDLE        = 0,
        MF_ROW_A11  = 1,
        MF_ROW_A12  = 2,
        MF_CHECK    = 3,
        MF_DONE     = 4,
        RESET_CHECK = 5,
        RESET_DONE  = 6
    };

    // zxnext_top_issue2.vhd:1961-1962 (issue 4 :1791-1792, issue 5 :1969-1970)
    static constexpr uint32_t kClockEnPeriodMs = 10;
    static constexpr uint32_t kButtonPeriodMs  = 1000;
    // emu_fnkeys.vhd:55 PERIOD (natural division) = 100 ticks of i_CLK_EN
    static constexpr uint32_t kPeriodTicks = kButtonPeriodMs / kClockEnPeriodMs;

    /// Period for the long-press timer in `tick()` calls.
    /// VHDL default: BUTTON_PERIOD_MS / CLOCK_EN_PERIOD_MS = 100.
    explicit EmuFnKeys(uint32_t button_period_ticks = kPeriodTicks)
        : period_ticks_(button_period_ticks) { reset(); }

    /// Reset to power-on / `i_reset='1'` state.
    void reset();

    // ── FSM inputs (one membrane scan worth per tick) ─────────────────────

    /// `i_button_m1_n` (active-low). VHDL :46.
    void set_button_m1_n(bool n)    { button_m1_n_    = n; }
    /// `i_button_reset_n` (active-low). VHDL :47.
    void set_button_reset_n(bool n) { button_reset_n_ = n; }

    /// Set the input row vector `i_rows[7:0]`. VHDL :40. The VHDL signal
    /// is the membrane row enable lines from the keyboard scanner — the
    /// FSM passes them through (`local_rows <= i_rows`) when in S_IDLE
    /// and overrides them when scanning for F-keys (S_MF_ROW_A11/A12).
    void set_input_rows(uint8_t rows) { input_rows_ = rows; }

    /// Set the input col vector `i_cols[4:0]` (bits 7:5 ignored). VHDL :43.
    /// During S_MF_ROW_A11 / A12 the FSM samples these to latch which
    /// F-keys are pressed. Active-LOW (VHDL membrane convention): bit=0
    /// means the key in that column is depressed, bit=1 means released.
    void set_input_cols(uint8_t cols) { input_cols_ = cols & 0x1F; }

    /// Convenience: simulate "F-key idx pressed" via a logical model
    /// that mirrors the membrane's row-scan semantics. idx ∈ [1..10].
    /// F1..F5 live on physical row 3 (A11); F6..F10 on row 4 (A12).
    /// The corresponding col bit is asserted ONLY when the FSM is
    /// scanning the matching row — i.e. tick() consults
    /// `pressed_fkey_idx_` and synthesises `input_cols_` per the
    /// current state, so a single physical key press is invisible to
    /// the OTHER row scan (matching VHDL membrane behavior — keys on a
    /// non-selected row do NOT affect the col output).
    ///
    /// Also drives `set_button_m1_n(false)` to assert the M1 button —
    /// without it, the FSM stays in S_IDLE and the F-key never strobes.
    /// idx = 0 (or out-of-range) clears the logical press.
    void press_membrane_fkey(int idx);

    /// Release the logical F-key press AND raise M1 / Reset buttons.
    /// Equivalent to `press_membrane_fkey(0)` + `set_button_m1_n(true)`
    /// + `set_button_reset_n(true)`.
    void release_membrane();

    // ── FSM filtered outputs (consumed downstream) ────────────────────────

    /// `o_rows_filtered[7:0]` — the rows the rest of the keyboard
    /// scanner sees. While the FSM is hunting for F-keys it forces only
    /// row 3 or row 4 active so the rest of the matrix appears released.
    uint8_t rows_filtered() const { return rows_filtered_; }

    /// `o_cols_filtered[4:0]` — VHDL :160 forces (others => '1') (all
    /// keys appear released) outside S_IDLE, so user presses during a
    /// fnkey scan don't bleed into the regular matrix.
    uint8_t cols_filtered() const { return cols_filtered_; }

    // ── FSM strobe outputs (one per F-key, level-high during DONE) ────────

    /// True iff the fnkey-i bit is asserted in the latched `local_fnkeys`
    /// vector. F1..F10 are latched in MF_ROW_A11/A12 and held through
    /// MF_CHECK; MF_DONE carries only the F9 term, RESET_DONE F1 or F4,
    /// and IDLE clears them.
    /// VHDL :77 — `o_fnkeys <= local_fnkeys`. The host edge-detects on
    /// these (zxnext.vhd:6330-6334).
    bool fnkey(int idx) const;
    /// Raw 10-bit vector. Bit i (0-based) = fnkey(i+1).
    uint16_t fnkeys_raw() const { return local_fnkeys_; }

    /// Current FSM state (test introspection).
    State state() const { return state_; }

    // ── NR 0x06 hotkey-enable snapshot (host-pushed) ──────────────────────

    /// VHDL zxnext.vhd:5165-5166 + :6342, :6347. The host calls this
    /// once per tick (or whenever NR 0x06 changes) to push the live
    /// hotkey-enable bits. `cpu_speed_en` gates F8, F5, F6 (we only
    /// emit F8 here); `_5060_en` gates F3.
    void set_nr_06_hotkey_enables(bool cpu_speed_en, bool _5060_en) {
        nr_06_hotkey_cpu_speed_en_ = cpu_speed_en;
        nr_06_hotkey_5060_en_      = _5060_en;
    }

    // ── Side-effect callbacks (host installs once at construction) ────────

    /// F2 — `hotkey_scandouble` (zxnext.vhd:5849-5852). Called once on
    /// the rising edge of fnkey(2) (the tick that enters MF_ROW_A11).
    /// VHDL has no NR-06 gate on F2 (zxnext.vhd:6341).
    using Callback = std::function<void()>;
    void set_f2_scandouble_callback(Callback cb) { cb_f2_ = std::move(cb); }
    /// F3 — `hotkey_5060` (zxnext.vhd:5839-5841). Gated on
    /// `nr_06_hotkey_5060_en` (VHDL :6342).
    void set_f3_5060_callback     (Callback cb) { cb_f3_ = std::move(cb); }
    /// F7 — `hotkey_scanlines` (zxnext.vhd:5861-5863). VHDL :6346 has
    /// no NR-06 gate.
    void set_f7_scanlines_callback(Callback cb) { cb_f7_ = std::move(cb); }
    /// F8 — `hotkey_cpu_speed` (zxnext.vhd:5789-5791). Gated on
    /// `nr_06_hotkey_cpu_speed_en` (VHDL :6347).
    void set_f8_cpu_speed_callback(Callback cb) { cb_f8_ = std::move(cb); }

    // ── FSM advance ───────────────────────────────────────────────────────

    /// Advance the FSM by one `i_CLK_EN`-period tick. Computes the new
    /// state from the current inputs, updates the local_fnkeys latch
    /// per the VHDL processes, and fires the F2/F3/F7/F8 side-effect
    /// callbacks on the rising edge of their bits (gated by NR 0x06).
    ///
    /// One call is one CLK_28_DEBOUNCE_EN period. No production code
    /// ticks it on a clock: the host keys drive it through
    /// `simulate_mf_fkey_press()`; tests also step it directly.
    void tick();

    /// Helper used by both frontends (host F-key taps) and the tests to
    /// drive a complete MF F-key press cycle:
    ///   1. press M1 + the F-key column,
    ///   2. tick() through MF_ROW_A11 → MF_ROW_A12 → MF_CHECK,
    ///   3. release M1,
    ///   4. tick() one more time to enter MF_DONE.
    /// The side-effect callbacks fire earlier, on tick 1 (F1..F5) or
    /// tick 2 (F6..F10). Returns the fnkey vector at MF_DONE, which
    /// holds only the F9 term: 0 after an F-key press (`cancel_nmi`).
    uint16_t simulate_mf_fkey_press(int idx);

private:
    // VHDL :61 state encoding.
    State    state_      = State::IDLE;
    State    next_state_ = State::IDLE;

    // VHDL :46-47 button inputs (active-low, idle '1').
    bool     button_m1_n_    = true;
    bool     button_reset_n_ = true;

    // VHDL :40 / :43 membrane inputs.
    uint8_t  input_rows_ = 0xFF;
    uint8_t  input_cols_ = 0x1F;

    // VHDL :74-75 filtered outputs.
    uint8_t  rows_filtered_ = 0xFF;
    uint8_t  cols_filtered_ = 0x1F;

    // VHDL :70 latched fnkey strobes (10 bits, idx 1..10 = bits 0..9).
    uint16_t local_fnkeys_ = 0;

    // Previous-tick value of `local_fnkeys_` — the VHDL top-level
    // edge-detector (zxnext.vhd:6328-6334) registers `hotkeys_1` as the
    // 1-tick-old value of `hotkeys_0` and the actual hotkey_X pulses are
    // computed as `hotkeys_0(N) and not hotkeys_1(N)`. We mirror that
    // here so a *rising edge* of any fnkey bit fires the side-effect
    // callback exactly once per tap.
    uint16_t prev_fnkeys_ = 0;

    // VHDL :69 cancel_nmi latch.
    bool     cancel_nmi_   = false;

    // VHDL :56-57 long-press timer.
    uint32_t timer_count_  = 0;
    uint32_t period_ticks_ = kPeriodTicks;

    // High-level convenience: the F-key index the user is "logically
    // pressing" (1..10), or 0 if no key is pressed. tick() consults this
    // to synthesise input_cols_ in a row-scan-aware way:
    //   - During MF_ROW_A11: only F1..F5 (idx 1..5) drive cols low;
    //     F6..F10 are invisible (their physical row is not selected).
    //   - During MF_ROW_A12: only F6..F10 drive cols low; F1..F5 are
    //     invisible.
    //   - During IDLE / MF_CHECK / MF_DONE / RESET_*: cols are released
    //     (0x1F) — the FSM is not sampling the keyboard.
    // This matches the VHDL membrane behavior — keys on a non-selected
    // row do NOT affect the col output.
    //
    // 0 means "no F-key logically pressed". Tests that drive cols
    // directly via set_input_cols() bypass this entirely and exercise
    // the raw FSM.
    int      pressed_fkey_idx_ = 0;

    // NR 0x06 hotkey-enable shadow (defaults '1' per zxnext.vhd:1107-1108).
    bool     nr_06_hotkey_cpu_speed_en_ = true;
    bool     nr_06_hotkey_5060_en_      = true;

    // Side-effect callbacks (only F2/F3/F7/F8 in scope for G132).
    Callback cb_f2_;
    Callback cb_f3_;
    Callback cb_f7_;
    Callback cb_f8_;

    // Helper: compute next_state from current state + button inputs (VHDL
    // :114-157 combinational process).
    State compute_next_state() const;

    // Helper: compute filtered rows / cols (VHDL :159-160 combinational).
    void update_filtered_outputs();

    // Helper: fire side-effect callbacks for F2/F3/F7/F8 on rising edges.
    void fire_mf_side_effects();
};
