// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.5 CAP-IN, the LEVEL half of input injection.
//
// Work package B1 of epic #276. IN-02 / IN-03 / IN-04 are level sets over
// accessors that already exist, plus `Keyboard::set_matrix_bit`, which §4 names
// and this branch made public (the full list of additions is in
// `debugger_inspect.cpp`'s banner). They are here rather than in
// `debugger_pending.cpp` because nothing about them needs machinery a later
// package brings.
//
// GH #276 B4 — IN-01 `press_key`, the timed PULSE, is here too now, over the
// reworked `Keyboard::queue_auto_type()` (it APPENDS: the 4-frame released gap
// between entries, a held key never stranded, the MAX_AUTO_TYPE_KEYS overflow
// reported as a refusal AND a count).
//
// ── THE INJECTION ORDERING (§4.5, REQ-dsl-20) — B4 ──────────────────────────
//
// "Every IN-01 pulse append and every IN-02 level set issued during frame N is
// applied in end_of_frame BEFORE the auto-type state machine ticks", so the
// guest sees it from frame N+1 — the frame `--delayed-keypress-frames N` lands
// on — and a level set from a MID-frame handler is invisible to the rest of
// frame N. B1 applied `set_key` / `set_extended_key` at once, which let a
// handler change what the rest of its own frame read. They now QUEUE the level
// (`Keyboard::queue_matrix_level` / `queue_extended_level`) and the edge's
// `tick_auto_type()` applies it first. A pulse needs no queue of its own: it is
// appended to the auto-type queue, which only that same tick consumes. The edge
// runs the `Frame` delivery before the tick, so a `Frame` handler's injection
// makes that edge too (`Emulator::end_of_frame()`).
//
// NOT IN-03 / IN-04. The contract names IN-01 and IN-02; `set_joystick` and
// `press_nmi` stay immediate as B1 built them. DECIDED (manager, 2026-09-28,
// B4 O3): the DSL design's "joystick at the next frame boundary" is the DSL
// engine's to honour — it queues its own joystick action for the frame edge —
// and not the backend's contract.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include "input/joystick.h"
#include "input/keyboard.h"
#include "peripheral/nmi_source.h"

namespace jnext {
namespace dbg {

// IN-02 — a LEVEL at a matrix position, for replay of recorded state. The DSL's
// bare `press` / `release`; only `press … for n` is IN-01.
Result Debugger::set_key(ClientId by, int row, int col, bool pressed) {
    // Refused, not clamped: a clamped injection presses the WRONG key, and a
    // caller that got the position wrong must find out rather than see an
    // emulation bug. `Keyboard::set_matrix_bit` guards too — this is the layer
    // that can report it.
    if (row < 0 || row > 7 || col < 0 || col > 4) return Result::RefusedUnavailable;

    // GH #276 B4 — queued for the next frame edge (REQ-dsl-20, above).
    impl_->emu.keyboard().queue_matrix_level(row, col, pressed);
    impl_->log_mutate_range(by, "key matrix",
                            "[" + std::to_string(row) + "," + std::to_string(col) +
                                (pressed ? "] pressed" : "] released") +
                                " at the next frame edge");
    return Result::Ok;
}

// IN-02 — one of the 16 extended keys. `id` is the `Keyboard::ExtKey` numbering,
// which is aligned 1:1 with the NR 0xB0 / 0xB1 readback bits.
Result Debugger::set_extended_key(ClientId by, int id, bool pressed) {
    // Out-of-range ids are REFUSED, not ignored (the published header says so).
    // `Keyboard::set_extended_key` ignores them, which is right for a host key
    // handler and wrong for a verb whose caller wants an answer.
    if (id < 0 || id > 15) return Result::RefusedUnavailable;

    // GH #276 B4 — queued for the next frame edge (REQ-dsl-20, above).
    impl_->emu.keyboard().queue_extended_level(id, pressed);
    impl_->log_mutate_range(by, "extended key " + std::to_string(id),
                            std::string(pressed ? "pressed" : "released") +
                                " at the next frame edge");
    return Result::Ok;
}

// IN-01 — a PULSE by matrix position: down for `hold_frames` frames, then up,
// with the 4-frame released gap after it. APPENDED behind whatever is queued.
Expected<size_t> Debugger::press_key(ClientId by, const MatrixKey& key, int hold_frames) {
    // Refused, not clamped, like IN-02: a clamped position presses the wrong
    // key. The second key is all-or-nothing — both of row2/col2 set, or both
    // absent (-1) — since `Keyboard::AutoKey` presses it iff row2 >= 0 and a
    // half-set pair would press a column nobody named.
    auto on_matrix = [](int r, int c) { return r >= 0 && r <= 7 && c >= 0 && c <= 4; };
    const bool second_absent = key.row2 == -1 && key.col2 == -1;
    if (!on_matrix(key.row1, key.col1) ||
        !(second_absent || on_matrix(key.row2, key.col2)))
        return make_refused<size_t>(Result::RefusedUnavailable);
    // A pulse of no frames is not a pulse: `tick_auto_type()` would press and
    // release in the same tick, and the guest would never see it.
    if (hold_frames < 1) return make_refused<size_t>(Result::RefusedUnavailable);

    const Keyboard::AutoKey ak{key.row1, key.col1, second_absent ? -1 : key.row2,
                               second_absent ? -1 : key.col2, hold_frames};
    const size_t queued = impl_->emu.keyboard().queue_auto_type({ak});

    std::string what = "[" + std::to_string(key.row1) + "," + std::to_string(key.col1) + "]";
    if (!second_absent)
        what += "+[" + std::to_string(key.row2) + "," + std::to_string(key.col2) + "]";
    // §4.5: on overflow, `RefusedUnavailable` AND the count queued — which for
    // one pulse is 0. The keyboard has already logged the truncation loudly;
    // the MUTATE line is for a mutation that HAPPENED, so a refused pulse has
    // none (the same rule every refused write in this API follows).
    if (queued == 0) return Expected<size_t>{Result::RefusedUnavailable, 0};
    impl_->log_mutate_range(by, "key pulse " + what,
                            "for " + std::to_string(hold_frames) + " frames, queued");
    return make_ok<size_t>(queued);
}

// IN-01 — the same, by NAME: the man page's `--delayed-keypress` vocabulary,
// through the ONE table (`key_name_to_matrix`), then the matrix form above.
//
// The name lookup's `bool` is not tested here, deliberately: on an unknown name
// `key_name_to_matrix()` leaves `key` DEFAULT-CONSTRUCTED (all four fields -1 —
// the published contract in inspect.h), and the matrix form refuses a first key
// off the matrix. A second test of the same fact would be a guard no row could
// tell from nothing (row IN-01-04 pins the refusal).
Expected<size_t> Debugger::press_key(ClientId by, const std::string& name, int hold_frames) {
    MatrixKey key;
    (void)key_name_to_matrix(name, key);
    return press_key(by, key, hold_frames);
}

// IN-03 — a connector's 12-bit button state, as `Joystick` holds it.
//
// IMMEDIATE, unlike IN-01/IN-02 (GH #276 B4, decision O3): the backend's
// REQ-dsl-20 ordering contract names pulses and level KEY sets only. A
// connector state set here is what the guest reads from its next port read —
// including later in the same frame. A client that wants frame-edge semantics
// (the DSL's `joystick n bits`) queues the call for the edge itself.
Result Debugger::set_joystick(ClientId by, JoystickSide side, uint16_t bits12) {
    Joystick& joy = impl_->emu.joystick();
    if (side == JoystickSide::Left) joy.set_joy_left(bits12);
    else                            joy.set_joy_right(bits12);
    impl_->log_mutate_range(
        by, side == JoystickSide::Left ? "joystick left" : "joystick right",
        "= 0x" + std::to_string(bits12 & 0x0FFF));
    return Result::Ok;
}

// IN-04 — the GH #209 NMI-button seam.
//
// A STROBE, not a level: the VHDL lines these model (`hotkey_m1`,
// `hotkey_drive`) are edge pulses, and `NmiSource` offers exactly that pair of
// helpers. A sticky `set_*_button(true)` would hold the button down forever,
// which is not what pressing it does.
//
// Whether the NMI is actually TAKEN is not this verb's business: NR 0x06 bit 3
// (Multiface) and bit 4 (DivMMC) gate each source in the hardware, and the
// gates live in `NmiSource`. `Ok` means the button was pressed, not that an NMI
// happened — the same as a real case button.
Result Debugger::press_nmi(ClientId by, NmiButton button) {
    NmiSource& nmi = impl_->emu.nmi_source();
    if (button == NmiButton::Mf) nmi.strobe_mf_button();
    else                         nmi.strobe_divmmc_button();
    impl_->log_mutate_range(by, "nmi button",
                            button == NmiButton::Mf ? "MF pressed" : "DRIVE pressed");
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
