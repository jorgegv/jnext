// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.5 CAP-IN, the LEVEL half of input injection.
//
// Work package B1 of epic #276. IN-02 / IN-03 / IN-04 are level sets over
// accessors that already exist (plus `Keyboard::set_matrix_bit`, made public as
// one of B1's five accessor additions), and they are here rather than in
// `debugger_pending.cpp` because nothing about them needs machinery a later
// package brings.
//
// IN-01 `press_key` — the timed PULSE, with the APPEND semantics §4.5 spells
// out — is NOT here: §10.1 assigns it to B4, which owns the auto-type queue
// rework it needs (the 4-frame released gap, the never-strand-a-pulse rule, the
// MAX_AUTO_TYPE_KEYS overflow that reports both a refusal and a count). Queuing
// it through today's `queue_auto_type()` would RESET the in-flight entry's frame
// counters, which is exactly the behaviour B4 exists to fix — so B1 does not
// pretend to offer it.
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

    impl_->emu.keyboard().set_matrix_bit(row, col, pressed);
    impl_->log_mutate_range(by, "key matrix",
                            "[" + std::to_string(row) + "," + std::to_string(col) +
                                (pressed ? "] pressed" : "] released"));
    return Result::Ok;
}

// IN-02 — one of the 16 extended keys. `id` is the `Keyboard::ExtKey` numbering,
// which is aligned 1:1 with the NR 0xB0 / 0xB1 readback bits.
Result Debugger::set_extended_key(ClientId by, int id, bool pressed) {
    // Out-of-range ids are REFUSED, not ignored (the published header says so).
    // `Keyboard::set_extended_key` ignores them, which is right for a host key
    // handler and wrong for a verb whose caller wants an answer.
    if (id < 0 || id > 15) return Result::RefusedUnavailable;

    impl_->emu.keyboard().set_extended_key(id, pressed);
    impl_->log_mutate_range(by, "extended key " + std::to_string(id),
                            pressed ? "pressed" : "released");
    return Result::Ok;
}

// IN-03 — a connector's 12-bit button state, as `Joystick` holds it.
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
