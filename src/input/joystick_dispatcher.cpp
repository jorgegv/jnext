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
        bits_[static_cast<size_t>(i)] = 0;
        axis_state_[static_cast<size_t>(i)] = AxisState{};
        emit_to_joystick(i);
    }
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

void JoystickDispatcher::handle_button(int controller_idx, uint8_t sdl_button, bool pressed)
{
    if (controller_idx < 0 || controller_idx >= NUM_CONNECTORS) {
        // JOY-WIRE-04: indices 2+ are silently ignored. Real hardware has
        // exactly two physical pad headers.
        return;
    }
    const uint16_t bit = sdl_button_to_jbit(sdl_button);
    if (bit == 0) {
        // Unmapped buttons (shoulders, guide, triggers-as-button, …)
        // are silently dropped. The Kempston/MD6 protocol exposes only
        // the 9 buttons enumerated above.
        return;
    }
    auto& v = bits_[static_cast<size_t>(controller_idx)];
    const uint16_t prev = v;
    if (pressed) {
        v = static_cast<uint16_t>(v | bit);
    } else {
        v = static_cast<uint16_t>(v & ~bit);
    }
    if (v != prev) {
        emit_to_joystick(controller_idx);
    }
}

void JoystickDispatcher::handle_axis(int controller_idx, uint8_t sdl_axis, int16_t value)
{
    if (controller_idx < 0 || controller_idx >= NUM_CONNECTORS) {
        return;
    }
    auto& st = axis_state_[static_cast<size_t>(controller_idx)];
    auto& v  = bits_[static_cast<size_t>(controller_idx)];

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

    const uint16_t prev = v;

    // Apply symmetric digital threshold. A single axis update only
    // touches the L/R or U/D pair — never the opposite pair.
    //
    // Negative deflection past -threshold pulls L (or U) high; positive
    // past +threshold pulls R (or D) high. Note the asymmetry to handle
    // the int16 range (-32768..+32767): we use `< -AXIS_THRESHOLD` and
    // `> +AXIS_THRESHOLD` so a value of 0 always sits cleanly in the
    // deadzone.
    if (is_x) {
        const bool left_now  = (value < -AXIS_THRESHOLD);
        const bool right_now = (value > +AXIS_THRESHOLD);
        if (left_now != st.left_active) {
            st.left_active = left_now;
            v = static_cast<uint16_t>(left_now ? (v | JBIT_L) : (v & ~JBIT_L));
        }
        if (right_now != st.right_active) {
            st.right_active = right_now;
            v = static_cast<uint16_t>(right_now ? (v | JBIT_R) : (v & ~JBIT_R));
        }
    } else if (is_y) {
        const bool up_now    = (value < -AXIS_THRESHOLD);
        const bool down_now  = (value > +AXIS_THRESHOLD);
        if (up_now != st.up_active) {
            st.up_active = up_now;
            v = static_cast<uint16_t>(up_now ? (v | JBIT_U) : (v & ~JBIT_U));
        }
        if (down_now != st.down_active) {
            st.down_active = down_now;
            v = static_cast<uint16_t>(down_now ? (v | JBIT_D) : (v & ~JBIT_D));
        }
    }

    if (v != prev) {
        emit_to_joystick(controller_idx);
    }
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
    default:
        return false;
    }
}
