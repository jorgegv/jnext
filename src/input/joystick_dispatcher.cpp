#include "input/joystick_dispatcher.h"
#include "input/joystick.h"

// =============================================================================
// JoystickDispatcher — host-side adapter from SDL gamepad events to Joystick.
//
// Closes plan rows JOY-WIRE-02/03/04 (gap G42) — see joystick_dispatcher.h
// header comment for the full design rationale.
//
// VHDL surface this dispatcher feeds into (already covered by KEMP-* /
// MD-* in input_test.cpp):
//
//   zxnext.vhd:3441-3442  JOY_LEFT[10:0] / JOY_RIGHT[10:0] raw vectors
//                         (12-bit incl. MODE; MODE used by MD6)
//   zxnext.vhd:3470-3506  port 0x1F / 0x37 mux (joyL_1f, joyR_1f, joyL_37,
//                         joyR_37) keyed by nr_05_joy0/joy1
//
// VHDL has no oracle for the SDL-event-to-bit translation itself — that is
// host-adapter work, equivalent to the Kempston pin-driver / MD6 shift
// register on real Next hardware which the FPGA does not specify.
// =============================================================================

namespace {

// Bit layout per zxnext.vhd:3441-3442 (also documented in joystick.h).
// Centralised here so the SDL → bit mapping below stays terse.
constexpr uint16_t JBIT_R     = 1u << 0;   // RIGHT
constexpr uint16_t JBIT_L     = 1u << 1;   // LEFT
constexpr uint16_t JBIT_D     = 1u << 2;   // DOWN
constexpr uint16_t JBIT_U     = 1u << 3;   // UP
constexpr uint16_t JBIT_B     = 1u << 4;   // Fire 1 (B)
constexpr uint16_t JBIT_C     = 1u << 5;   // Fire 2 (C)
constexpr uint16_t JBIT_A     = 1u << 6;   // MD A
constexpr uint16_t JBIT_START = 1u << 7;   // START
constexpr uint16_t JBIT_MODE  = 1u << 11;  // MODE (MD6 latch)

// The four digital direction bits — every direction source contributes to
// exactly these, because the Next's DB9 ports carry nothing else.
constexpr uint16_t DIR_MASK = JBIT_R | JBIT_L | JBIT_D | JBIT_U;

// Map an SDL_GameControllerButton to the corresponding 12-bit logical bit.
// Returns 0 for unmapped buttons (LEFTSHOULDER, RIGHTSHOULDER, GUIDE,
// triggers-as-buttons, paddle buttons, …).
inline uint16_t sdl_button_to_jbit(uint8_t sdl_button) {
    switch (sdl_button) {
    case SDL_CONTROLLER_BUTTON_A:             return JBIT_B;
    case SDL_CONTROLLER_BUTTON_B:             return JBIT_C;
    case SDL_CONTROLLER_BUTTON_X:             return JBIT_A;
    case SDL_CONTROLLER_BUTTON_Y:             return JBIT_MODE;
    case SDL_CONTROLLER_BUTTON_START:         return JBIT_START;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return JBIT_U;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return JBIT_D;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return JBIT_L;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return JBIT_R;
    default:                                  return 0;
    }
}

} // namespace

void JoystickDispatcher::reset()
{
    for (int i = 0; i < NUM_CONNECTORS; ++i) {
        const size_t s = static_cast<size_t>(i);
        bits_[s]         = 0;
        held_[s]         = 0;
        dir_restored_[s] = 0;
        dir_hat_[s].fill(0);
        axis_state_[s] = AxisState{};
        emit_to_joystick(i);
    }
}

void JoystickDispatcher::resync()
{
    // Read the canonical (just-restored) 12-bit vectors back from the bound
    // Joystick and adopt them as our shadow, so a subsequent single-bit
    // controller event OR/ANDs against the restored vector rather than a
    // stale one. Also re-seed axis_state_ from the direction bits so a later
    // partial axis update resolves L/R/U/D consistently.
    bits_[0] = joy_.joy_left_bits();
    bits_[1] = joy_.joy_right_bits();
    for (int i = 0; i < NUM_CONNECTORS; ++i) {
        const size_t s = static_cast<size_t>(i);
        const uint16_t v = bits_[s];
        // Buttons are unambiguous — they belong to held_. Directions are NOT:
        // we cannot know which source was holding one. Attributing them to any
        // real source strands them, because that source then owns a bit it
        // never set and only IT can clear: attribute to the axis latches and a
        // D-pad/hat release no longer releases (the latch never updates
        // without genuine axis motion); attribute to held_ and a hat release
        // has the same problem. Either way a rewind performed with a direction
        // held could leave it stuck ON for the rest of the session.
        //
        // So restored directions go in their own mask, owned by nobody, and
        // the FIRST direction event from ANY source drops it — at that moment
        // the live device state is authoritative and the restored guess is
        // worthless. Plain button events deliberately do NOT drop it, so a
        // fire press right after a rewind cannot cancel a restored direction
        // (that is the SL-DISP-01 guarantee).
        held_[s]         = static_cast<uint16_t>(v & ~DIR_MASK);
        dir_restored_[s] = static_cast<uint16_t>(v & DIR_MASK);
        dir_hat_[s].fill(0);
        axis_state_[s] = AxisState{};
    }
    // Deliberately no emit_to_joystick(): the Joystick is already correct;
    // we are syncing FROM it, not TO it.
}

void JoystickDispatcher::emit_to_joystick(int idx)
{
    // JOY-WIRE-04 routing policy: connector 0 → left, connector 1 → right.
    // The Joystick class itself owns the per-connector NR 0x05 mode latch
    // and the port 0x1F / 0x37 composer; this adapter only ships the raw
    // 12-bit vector.
    const uint16_t v = bits_[static_cast<size_t>(idx)];
    if (idx == 0) {
        joy_.set_joy_left(v);
    } else if (idx == 1) {
        joy_.set_joy_right(v);
    }
}

void JoystickDispatcher::set_source(int slot, JoySource src)
{
    if (slot < 0 || slot >= NUM_CONNECTORS) return;
    auto& cur = source_[static_cast<size_t>(slot)];
    if (cur == src) return;
    cur = src;
    // Releasing the connector: a direction/fire held under the old source
    // must not linger. Clear every contribution and push the zero.
    const size_t s = static_cast<size_t>(slot);
    bits_[s]         = 0;
    held_[s]         = 0;
    dir_restored_[s] = 0;
    dir_hat_[s].fill(0);
    axis_state_[s] = AxisState{};
    emit_to_joystick(slot);
}

uint16_t JoystickDispatcher::dir_from_axes(int idx) const
{
    const auto& st = axis_state_[static_cast<size_t>(idx)];
    uint16_t m = 0;
    if (st.left_active)  m |= JBIT_L;
    if (st.right_active) m |= JBIT_R;
    if (st.up_active)    m |= JBIT_U;
    if (st.down_active)  m |= JBIT_D;
    return m;
}

void JoystickDispatcher::recompute(int idx)
{
    const size_t i = static_cast<size_t>(idx);
    // Union of every contribution. Directions OR together so any source can
    // steer and releasing one never cancels another that is still held.
    uint16_t v = static_cast<uint16_t>(held_[i] | dir_restored_[i] | dir_from_axes(idx));
    for (uint16_t hat : dir_hat_[i]) v = static_cast<uint16_t>(v | hat);

    if (v != bits_[i]) {
        bits_[i] = v;
        emit_to_joystick(idx);
    }
}

void JoystickDispatcher::drop_restored_directions(int idx)
{
    // Called by every DIRECTION source before it updates its own mask: once a
    // real device reports a direction, the post-rewind guess is superseded.
    dir_restored_[static_cast<size_t>(idx)] = 0;
}

void JoystickDispatcher::apply_bit(int idx, uint16_t jbit, bool pressed)
{
    uint16_t& m = held_[static_cast<size_t>(idx)];
    m = static_cast<uint16_t>(pressed ? (m | jbit) : (m & ~jbit));
    recompute(idx);
}

void JoystickDispatcher::set_cursor_bit(int slot, CursorBit b, bool pressed)
{
    if (slot < 0 || slot >= NUM_CONNECTORS) return;
    // Gate: cursor-key input only reaches a connector whose source is
    // CursorKeys. This is the other half of the SDL-vs-keys mutual exclusion
    // (the SDL path checks source == Sdl below).
    if (source_[static_cast<size_t>(slot)] != JoySource::CursorKeys) return;
    uint16_t jbit = 0;
    switch (b) {
        case CursorBit::Up:    jbit = JBIT_U; break;
        case CursorBit::Down:  jbit = JBIT_D; break;
        case CursorBit::Left:  jbit = JBIT_L; break;
        case CursorBit::Right: jbit = JBIT_R; break;
        case CursorBit::Fire:  jbit = JBIT_B; break;   // Fire 1 → port 0x1F bit 4
    }
    if (jbit & DIR_MASK) drop_restored_directions(slot);
    apply_bit(slot, jbit, pressed);
}

void JoystickDispatcher::handle_button(int controller_idx, uint8_t sdl_button, bool pressed)
{
    if (controller_idx < 0 || controller_idx >= NUM_CONNECTORS) {
        // JOY-WIRE-04: indices 2+ are silently ignored. Real hardware has
        // exactly two physical pad headers.
        return;
    }
    // Task 79 gate: SDL input is dropped for a connector the user has
    // assigned to cursor keys.
    if (source_[static_cast<size_t>(controller_idx)] != JoySource::Sdl) {
        return;
    }
    const uint16_t bit = sdl_button_to_jbit(sdl_button);
    if (bit & DIR_MASK) drop_restored_directions(controller_idx);
    if (bit == 0) {
        // Unmapped buttons (shoulders, guide, triggers-as-button, …)
        // are silently dropped. The Kempston/MD6 protocol exposes only
        // the 9 buttons enumerated above.
        return;
    }
    apply_bit(controller_idx, bit, pressed);
}

void JoystickDispatcher::handle_axis(int controller_idx, uint8_t sdl_axis, int16_t value)
{
    if (controller_idx < 0 || controller_idx >= NUM_CONNECTORS) {
        return;
    }
    // Task 79 gate: SDL axes are dropped for a cursor-key connector.
    if (source_[static_cast<size_t>(controller_idx)] != JoySource::Sdl) {
        return;
    }
    auto& st = axis_state_[static_cast<size_t>(controller_idx)];

    // Resolve which D-pad pair this axis drives.
    // X axes (LEFTX / RIGHTX) → L (negative) / R (positive)
    // Y axes (LEFTY / RIGHTY) → U (negative) / D (positive — SDL Y is screen-down)
    bool is_x = false, is_y = false;
    switch (sdl_axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
    case SDL_CONTROLLER_AXIS_RIGHTX:
        is_x = true; break;
    case SDL_CONTROLLER_AXIS_LEFTY:
    case SDL_CONTROLLER_AXIS_RIGHTY:
        is_y = true; break;
    default:
        // Trigger axes (LEFT_TRIGGER / RIGHT_TRIGGER) are unmapped.
        // SDL exposes triggers as 0..32767 axis values, but they have
        // no Kempston/MD6 analogue, so we silently drop them.
        return;
    }

    // Apply symmetric digital threshold. A single axis update only
    // touches the L/R or U/D pair — never the opposite pair.
    //
    // Negative deflection past -threshold pulls L (or U) high; positive
    // past +threshold pulls R (or D) high. Note the asymmetry to handle
    // the int16 range (-32768..+32767): we use `< -AXIS_THRESHOLD` and
    // `> +AXIS_THRESHOLD` so a value of 0 always sits cleanly in the
    // deadzone.
    if (is_x) {
        st.left_active  = (value < -AXIS_THRESHOLD);
        st.right_active = (value > +AXIS_THRESHOLD);
    } else if (is_y) {
        st.up_active    = (value < -AXIS_THRESHOLD);
        st.down_active  = (value > +AXIS_THRESHOLD);
    }

    drop_restored_directions(controller_idx);
    // The latches ARE this source's mask (dir_from_axes reads them), so the
    // union recompute below both merges with the other sources and decides
    // whether anything actually changed.
    recompute(controller_idx);
}

void JoystickDispatcher::handle_raw_button(int connector_idx, uint8_t raw_button, bool pressed)
{
    if (connector_idx < 0 || connector_idx >= NUM_CONNECTORS) {
        return;
    }
    if (source_[static_cast<size_t>(connector_idx)] != JoySource::Sdl) {
        return;
    }
    // Positional mapping — a raw SDL_Joystick reports button INDICES with no
    // semantics attached, so there is nothing better available than "first
    // button is Fire 1". Deliberately conservative: buttons past index 4 are
    // dropped rather than guessed at.
    uint16_t bit = 0;
    switch (raw_button) {
    case 0: bit = JBIT_B;     break;   // Fire 1
    case 1: bit = JBIT_C;     break;   // Fire 2
    case 2: bit = JBIT_A;     break;   // MD3 A
    case 3: bit = JBIT_MODE;  break;   // MD6 MODE
    case 4: bit = JBIT_START; break;   // START
    default: return;
    }
    apply_bit(connector_idx, bit, pressed);
}

void JoystickDispatcher::handle_raw_axis(int connector_idx, uint8_t raw_axis, int16_t value)
{
    // Raw axis 0/1 are X/Y by universal convention (the SDL joystick API
    // guarantees nothing, but every stick and pad follows it). Fold them onto
    // the same code path as the controller left stick so the threshold,
    // deadzone and sticky axis_state_ logic are shared rather than duplicated.
    switch (raw_axis) {
    case 0: handle_axis(connector_idx, SDL_CONTROLLER_AXIS_LEFTX, value); break;
    case 1: handle_axis(connector_idx, SDL_CONTROLLER_AXIS_LEFTY, value); break;
    default: break;   // throttle / twist / extra sticks — unmapped
    }
}

void JoystickDispatcher::handle_raw_hat(int connector_idx, uint8_t hat_index, uint8_t hat_value)
{
    if (connector_idx < 0 || connector_idx >= NUM_CONNECTORS) {
        return;
    }
    if (hat_index >= MAX_HATS) {
        return;   // ignored rather than aliased onto hat 0, which would fight
    }
    if (source_[static_cast<size_t>(connector_idx)] != JoySource::Sdl) {
        return;
    }
    // SDL_HAT_* is a bitmask; the diagonals are just the two adjacent bits
    // OR'd together, so testing each direction independently handles all
    // nine positions. Centred (mask 0) clears THIS hat's mask only — other
    // hats and the analogue stick keep whatever they are holding.
    uint16_t m = 0;
    if (hat_value & SDL_HAT_UP)    m |= JBIT_U;
    if (hat_value & SDL_HAT_DOWN)  m |= JBIT_D;
    if (hat_value & SDL_HAT_LEFT)  m |= JBIT_L;
    if (hat_value & SDL_HAT_RIGHT) m |= JBIT_R;

    drop_restored_directions(connector_idx);
    dir_hat_[static_cast<size_t>(connector_idx)][hat_index] = m;
    recompute(connector_idx);
}

void JoystickDispatcher::map_instance_to_slot(int32_t sdl_instance_id, int slot)
{
    if (slot >= NUM_CONNECTORS) {
        // Reject — only two physical connectors. Slot 0/1 OK; -1 = unmap.
        slot = -1;
    }
    // First pass: update existing entry if present.
    for (auto& d : device_map_) {
        if (d.instance_id == sdl_instance_id) {
            d.slot = slot;
            if (slot < 0) d.instance_id = -1;
            return;
        }
    }
    // Second pass: claim a free slot for a new mapping (slot >= 0 only).
    if (slot < 0) return;  // unmap of a non-mapped device — nothing to do
    for (auto& d : device_map_) {
        if (d.instance_id < 0) {
            d.instance_id = sdl_instance_id;
            d.slot        = slot;
            return;
        }
    }
    // Table full — silently drop. Real SDL host can have many devices but
    // we route only two; further ones never reach handle_sdl_event.
}

int JoystickDispatcher::resolve_instance_to_slot(int32_t sdl_instance_id) const
{
    for (const auto& d : device_map_) {
        if (d.instance_id == sdl_instance_id) {
            return d.slot;
        }
    }
    return -1;
}

bool JoystickDispatcher::handle_sdl_event(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        const int slot = resolve_instance_to_slot(e.cbutton.which);
        if (slot < 0) return false;  // unmapped device
        handle_button(slot, e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
        return true;
    }
    case SDL_CONTROLLERAXISMOTION: {
        const int slot = resolve_instance_to_slot(e.caxis.which);
        if (slot < 0) return false;
        handle_axis(slot, e.caxis.axis, e.caxis.value);
        return true;
    }
    // Raw SDL_Joystick events (Task 83). Only devices GamepadHost opened as
    // raw joysticks reach here: for a device that IS a game controller SDL
    // emits BOTH families, and GamepadHost filters the JOY* copies out so a
    // single physical press cannot be applied twice.
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        const int slot = resolve_instance_to_slot(e.jbutton.which);
        if (slot < 0) return false;
        handle_raw_button(slot, e.jbutton.button, e.type == SDL_JOYBUTTONDOWN);
        return true;
    }
    case SDL_JOYAXISMOTION: {
        const int slot = resolve_instance_to_slot(e.jaxis.which);
        if (slot < 0) return false;
        handle_raw_axis(slot, e.jaxis.axis, e.jaxis.value);
        return true;
    }
    case SDL_JOYHATMOTION: {
        const int slot = resolve_instance_to_slot(e.jhat.which);
        if (slot < 0) return false;
        handle_raw_hat(slot, e.jhat.hat, e.jhat.value);
        return true;
    }
    default:
        return false;
    }
}
