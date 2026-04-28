#include "input/emu_fnkeys.h"

void EmuFnKeys::reset() {
    // VHDL :89-95, :106-110, :169-170, :181-184 — reset path.
    state_              = State::IDLE;
    next_state_         = State::IDLE;
    button_m1_n_        = true;
    button_reset_n_     = true;
    input_rows_         = 0xFF;
    input_cols_         = 0x1F;
    rows_filtered_      = 0xFF;
    cols_filtered_      = 0x1F;
    local_fnkeys_       = 0;
    prev_fnkeys_        = 0;
    cancel_nmi_         = false;
    timer_count_        = 0;
    pressed_fkey_idx_   = 0;
}

EmuFnKeys::State EmuFnKeys::compute_next_state() const {
    // VHDL :114-157 — combinational next-state process. Note the VHDL
    // sensitivity list is (state, i_button_m1_n, i_button_reset_n) and the
    // body is a plain CASE on `state`. We reproduce it 1:1.
    switch (state_) {
        case State::IDLE:
            // VHDL :118-125
            if (!button_reset_n_) return State::RESET_CHECK;   // reset button low → reset path
            if (!button_m1_n_)    return State::MF_ROW_A11;    // M1 button low → MF scan path
            return State::IDLE;
        case State::MF_ROW_A11:
            // VHDL :127-128
            return State::MF_ROW_A12;
        case State::MF_ROW_A12:
            // VHDL :130-131
            return State::MF_CHECK;
        case State::MF_CHECK:
            // VHDL :133-138 — loop back to A11 while M1 is still held;
            // advance to DONE on release.
            if (button_m1_n_) return State::MF_DONE;           // M1 released → finish
            return State::MF_ROW_A11;                          // M1 still held → re-scan
        case State::MF_DONE:
            // VHDL :140-141
            return State::IDLE;
        case State::RESET_CHECK:
            // VHDL :143-148 — wait for reset release.
            if (button_reset_n_) return State::RESET_DONE;
            return State::RESET_CHECK;
        case State::RESET_DONE:
            // VHDL :150-151
            return State::IDLE;
    }
    // VHDL :153-154 — defensive default.
    return State::IDLE;
}

void EmuFnKeys::update_filtered_outputs() {
    // VHDL :159 — local_rows passes through in IDLE; forced to "11110111"
    // (A11=0) during MF_ROW_A11 and "11101111" (A12=0) during MF_ROW_A12.
    if (state_ == State::IDLE)              rows_filtered_ = input_rows_;
    else if (state_ == State::MF_ROW_A11)   rows_filtered_ = 0xF7;
    else                                    rows_filtered_ = 0xEF;
    // VHDL :160 — local_cols passes through in IDLE; forced all-released
    // (others => '1') everywhere else, so the user's M1+key combo is not
    // visible to the rest of the matrix during the scan.
    cols_filtered_ = (state_ == State::IDLE) ? input_cols_ : 0x1F;
}

void EmuFnKeys::fire_mf_side_effects() {
    // Detect rising edges of the F2/F3/F7/F8 fnkey bits — VHDL
    // zxnext.vhd:6328-6334 + :6341-6347 model the same edge:
    //
    //   process (i_CLK_28) ...
    //     hotkeys_1 <= hotkeys_0;
    //     hotkeys_0 <= i_SPKEY_FUNCTION(10 downto 1);
    //   ...
    //   hotkey_scandouble <= hotkeys_0(2) and not hotkeys_1(2);
    //   hotkey_5060       <= hotkeys_0(3) and not hotkeys_1(3) and
    //                        nr_06_hotkey_5060_en;
    //   hotkey_scanlines  <= hotkeys_0(7) and not hotkeys_1(7);
    //   hotkey_cpu_speed  <= hotkeys_0(8) and not hotkeys_1(8) and
    //                        nr_06_hotkey_cpu_speed_en;
    //
    // i.e. each `hotkey_X` is a 1-tick pulse on the 0→1 transition of
    // the corresponding fnkey strobe — which the VHDL FSM latches DURING
    // S_MF_ROW_A11 (F1..F5) / S_MF_ROW_A12 (F6..F10) and clears on
    // S_MF_DONE/S_IDLE.
    //
    // We are called every tick from `tick()` AFTER local_fnkeys is
    // updated; rising edges are computed against `prev_fnkeys_`.
    //
    // F1 / F4 (reset) and F9 / F10 (NMI) edges are intentionally NOT
    // dispatched here — those are owned by Emulator::on_hotkey_f*()
    // (G152). F5 / F6 (expansion-bus enable/disable) are out-of-scope
    // for G132 and tracked under G147.

    const uint16_t cur  = local_fnkeys_;
    const uint16_t prev = prev_fnkeys_;
    const uint16_t rising = static_cast<uint16_t>(cur & ~prev);

    // F2 → bit 1 — `hotkey_scandouble` (zxnext.vhd:6341). NO NR-06 gate.
    if ((rising & 0x0002) && cb_f2_) cb_f2_();

    // F3 → bit 2 — `hotkey_5060` (zxnext.vhd:6342). Gated on
    // `nr_06_hotkey_5060_en` (NR 0x06 bit 5).
    if ((rising & 0x0004) && nr_06_hotkey_5060_en_ && cb_f3_) cb_f3_();

    // F7 → bit 6 — `hotkey_scanlines` (zxnext.vhd:6346). NO NR-06 gate.
    if ((rising & 0x0040) && cb_f7_) cb_f7_();

    // F8 → bit 7 — `hotkey_cpu_speed` (zxnext.vhd:6347). Gated on
    // `nr_06_hotkey_cpu_speed_en` (NR 0x06 bit 7).
    if ((rising & 0x0080) && nr_06_hotkey_cpu_speed_en_ && cb_f8_) cb_f8_();
}

void EmuFnKeys::tick() {
    // -------------------------------------------------------------------
    // VHDL :86-97 — timer process.
    // timer_set <= '1' when state = S_IDLE and (i_button_reset_n='0' or
    //                                            i_button_m1_n='0') else '0';
    // timer_expired <= '1' when timer_count = 0 else '0';
    //
    // On reset → 0; on timer_set → PERIOD; otherwise (when CLK_EN and not
    // expired) → decrement.
    // -------------------------------------------------------------------
    const bool timer_set =
        (state_ == State::IDLE) && (!button_reset_n_ || !button_m1_n_);
    if (timer_set) {
        timer_count_ = period_ticks_;
    } else if (timer_count_ > 0) {
        --timer_count_;
    }
    const bool timer_expired = (timer_count_ == 0);

    // -------------------------------------------------------------------
    // VHDL :103-112 — sequential state register. Capture next_state into
    // state at the rising clock edge.
    // -------------------------------------------------------------------
    next_state_  = compute_next_state();
    state_       = next_state_;

    // -------------------------------------------------------------------
    // High-level convenience: synthesise input_cols_ so a logical F-key
    // press only drives cols low while the matching row is being forced
    // by the FSM. F1..F5 are on physical row 3 (visible during
    // MF_ROW_A11) and F6..F10 are on physical row 4 (visible during
    // MF_ROW_A12). Outside those two states the FSM is not sampling the
    // membrane, so cols read all-released (0x1F). Mirrors VHDL membrane
    // behavior — keys on a non-selected row do NOT pull col bits low.
    //
    // Tests that call set_input_cols() directly bypass this entirely
    // (pressed_fkey_idx_ stays 0), exercising the raw VHDL FSM input.
    // -------------------------------------------------------------------
    if (pressed_fkey_idx_ != 0) {
        if (pressed_fkey_idx_ >= 1 && pressed_fkey_idx_ <= 5
            && state_ == State::MF_ROW_A11) {
            const int col = pressed_fkey_idx_ - 1;
            input_cols_ = static_cast<uint8_t>((0x1F & ~(1u << col))) & 0x1F;
        } else if (pressed_fkey_idx_ >= 6 && pressed_fkey_idx_ <= 10
                   && state_ == State::MF_ROW_A12) {
            const int col = 10 - pressed_fkey_idx_;
            input_cols_ = static_cast<uint8_t>((0x1F & ~(1u << col))) & 0x1F;
        } else {
            // Logical press in flight, but FSM not scanning matching row.
            input_cols_ = 0x1F;
        }
    }

    // Snapshot the previous fnkey vector BEFORE we update local_fnkeys_,
    // so the rising-edge detector can compare new-vs-old. Mirrors the
    // VHDL `hotkeys_1 <= hotkeys_0` register update on each clock.
    prev_fnkeys_ = local_fnkeys_;

    // -------------------------------------------------------------------
    // VHDL :166-177 — cancel_nmi latch. Set when ANY column bit is low
    // (i.e. some key is pressed) outside S_IDLE; cleared on entering
    // S_IDLE.
    //
    //   cancel_nmi <= '0'                                  when state = S_IDLE
    //   cancel_nmi <= cancel_nmi or NOT (i_cols(4..0) AND-reduce)  otherwise
    // -------------------------------------------------------------------
    if (state_ == State::IDLE) {
        cancel_nmi_ = false;
    } else {
        // Any col bit low (key pressed) → set cancel_nmi.
        const bool any_col_pressed = ((input_cols_ & 0x1F) != 0x1F);
        if (any_col_pressed) cancel_nmi_ = true;
    }

    // -------------------------------------------------------------------
    // VHDL :179-200 — local_fnkeys latch. Different states latch
    // different slices of the membrane cols.
    //
    //   if state = S_MF_ROW_A11:
    //     local_fnkeys[5:1] <= NOT i_cols[4:0]   (F1..F5 ← cols 0..4)
    //   if state = S_MF_ROW_A12:
    //     local_fnkeys[10:6] <= NOT (i_cols[0] & i_cols[1] & i_cols[2]
    //                                & i_cols[3] & i_cols[4])
    //                                              (F10..F6 — bit-reversed)
    //   if state = S_MF_DONE:
    //     local_fnkeys <= '0' & (NOT timer_expired AND NOT cancel_nmi)
    //                          & "00000000"        (F9 = MF NMI on short
    //                                              press if no key pressed)
    //   if state = S_RESET_DONE:
    //     long press  → "0000000001"  (F1 hard reset)
    //     short press → "0000001000"  (F4 soft reset)
    //   if state = S_IDLE:
    //     local_fnkeys <= (others => '0')  (clear all strobes)
    //
    // NB: The VHDL `state` reference inside this clocked process is the
    // POST-EDGE state — i.e. the value just latched from next_state. Our
    // `state_` variable above has already been updated, so we use it
    // directly here, matching VHDL semantics exactly.
    // -------------------------------------------------------------------
    switch (state_) {
        case State::MF_ROW_A11: {
            // F1..F5 in bits 0..4 (idx 1..5).
            // VHDL :185 — local_fnkeys(5 downto 1) <= NOT i_cols(4 downto 0)
            const uint8_t cols_inv = static_cast<uint8_t>(~input_cols_) & 0x1F;
            local_fnkeys_ = static_cast<uint16_t>(
                (local_fnkeys_ & 0x3E0) | cols_inv);  // preserve bits 5..9
            break;
        }
        case State::MF_ROW_A12: {
            // F6..F10 in bits 5..9 (idx 6..10), with the col bits MIRRORED.
            // VHDL :187 — local_fnkeys(10 downto 6) <=
            //   NOT (i_cols(0) & i_cols(1) & i_cols(2) & i_cols(3) & i_cols(4))
            // i.e. fnkey(6) ← NOT i_cols(4), fnkey(7) ← NOT i_cols(3),
            //      fnkey(8) ← NOT i_cols(2), fnkey(9) ← NOT i_cols(1),
            //      fnkey(10) ← NOT i_cols(0).
            const uint8_t c = input_cols_ & 0x1F;
            uint8_t mirrored = 0;
            mirrored |= ((~c) >> 4) & 0x01;  // bit 0 ← NOT c[4]
            mirrored |= ((~c) >> 2) & 0x02;  // bit 1 ← NOT c[3]
            mirrored |=  (~c)       & 0x04;  // bit 2 ← NOT c[2]
            mirrored |= ((~c) << 2) & 0x08;  // bit 3 ← NOT c[1]
            mirrored |= ((~c) << 4) & 0x10;  // bit 4 ← NOT c[0]
            local_fnkeys_ = static_cast<uint16_t>(
                (local_fnkeys_ & 0x01F) | (static_cast<uint16_t>(mirrored) << 5));
            break;
        }
        case State::MF_DONE: {
            // VHDL :189 — F9 = (NOT timer_expired) AND (NOT cancel_nmi).
            // (Multiface NMI on short press iff no other key was pressed
            // during the M1 hold.)
            const bool f9 = (!timer_expired) && (!cancel_nmi_);
            local_fnkeys_ = static_cast<uint16_t>(f9 ? 0x100 : 0x000);
            break;
        }
        case State::RESET_DONE: {
            // VHDL :191-195
            local_fnkeys_ = timer_expired ? 0x001  // F1 = hard reset (long)
                                          : 0x008; // F4 = soft reset (short)
            break;
        }
        case State::IDLE:
            // VHDL :196-197 — clear all fnkey strobes back to 0 once we're
            // back in IDLE.
            local_fnkeys_ = 0;
            break;
        default:
            // Transitional states leave local_fnkeys unchanged.
            break;
    }

    // Update filtered combinational outputs from the new state.
    update_filtered_outputs();

    // Fire side-effect callbacks on rising edges of F2/F3/F7/F8. The
    // VHDL top-level edge-detector at zxnext.vhd:6328-6334 strobes the
    // hotkey_X signal on the cycle the fnkey bit goes 0→1, which
    // happens during S_MF_ROW_A11 (F1..F5) and S_MF_ROW_A12 (F6..F10)
    // — NOT on entering S_MF_DONE, where local_fnkeys is reset to a
    // pure F9 strobe (VHDL :189).
    fire_mf_side_effects();
    // RESET_DONE F1/F4 dispatch is handled by Emulator::on_hotkey_f1/f4
    // via the GUI surface (G152); EmuFnKeys does not fire those callbacks.
}

bool EmuFnKeys::fnkey(int idx) const {
    if (idx < 1 || idx > 10) return false;
    return (local_fnkeys_ >> (idx - 1)) & 0x1;
}

void EmuFnKeys::press_membrane_fkey(int idx) {
    // High-level helper: record the logical F-key index. tick()
    // synthesises input_cols_ at scan time based on the FSM's current
    // state (MF_ROW_A11 sees only F1..F5; MF_ROW_A12 sees only F6..F10
    // — matching VHDL membrane behavior where a key on a non-selected
    // row does not pull col bits low).
    if (idx < 1 || idx > 10) {
        pressed_fkey_idx_ = 0;
        return;
    }
    pressed_fkey_idx_ = idx;
    // M1 button asserted (active-low → '0') so the FSM advances out of
    // S_IDLE on the next tick.
    button_m1_n_ = false;
}

void EmuFnKeys::release_membrane() {
    pressed_fkey_idx_ = 0;
    input_cols_       = 0x1F;
    button_m1_n_      = true;
    button_reset_n_   = true;
}

uint16_t EmuFnKeys::simulate_mf_fkey_press(int idx) {
    // Drive a complete press → release cycle through the FSM. The exact
    // tick sequence required to land in MF_DONE depends on when we
    // release M1; we hold M1 long enough for one A11 + A12 sample then
    // release, which is the minimum-correct "tap" sequence per VHDL :118
    // (IDLE → MF_ROW_A11), :128 (A11 → A12), :131 (A12 → MF_CHECK), :136
    // (CHECK → DONE if M1 released, else CHECK → A11).
    //
    // Tick 0 (entering): currently in IDLE, M1 not yet pressed.
    // After press_membrane_fkey():
    //   tick → IDLE; next_state evaluated as MF_ROW_A11 (M1 low) — state
    //          becomes MF_ROW_A11.
    //   tick → MF_ROW_A11 → MF_ROW_A12 (latches F1..F5 col bits).
    //   tick → MF_ROW_A12 → MF_CHECK   (latches F6..F10 col bits).
    //   release M1 BEFORE the next tick.
    //   tick → MF_CHECK → MF_DONE      (M1 high).
    press_membrane_fkey(idx);
    tick();                       // IDLE → MF_ROW_A11 (state register clocks)
    tick();                       // MF_ROW_A11 → MF_ROW_A12 (F1..F5 latch)
    tick();                       // MF_ROW_A12 → MF_CHECK   (F6..F10 latch)
    release_membrane();           // M1 high before next tick
    tick();                       // MF_CHECK → MF_DONE (fires callbacks)
    const uint16_t latched = local_fnkeys_;
    tick();                       // MF_DONE → IDLE (clears local_fnkeys)
    return latched;
}
