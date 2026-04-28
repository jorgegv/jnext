#include "input/mouse_dispatcher.h"
#include "input/mouse.h"

// =============================================================================
// MouseDispatcher — host-side adapter from SDL events to KempstonMouse.
//
// Closes plan rows MOUSE-13/14/15 (gap G43) — see mouse_dispatcher.h header
// comment for the full design rationale.
//
// VHDL surface this dispatcher feeds into (already covered by MOUSE-01..11):
//   zxnext.vhd:3546   port_fbdf_dat <= i_MOUSE_X
//   zxnext.vhd:3553   port_ffdf_dat <= i_MOUSE_Y
//   zxnext.vhd:3560   port_fadf_dat <= wheel(3:0) & '1'
//                                       & (not btn(2)) & (not btn(0)) & (not btn(1))
//
// VHDL has no oracle for the SDL-event-to-register translation itself —
// that is host-adapter work, equivalent to the PS/2 driver on real Next
// hardware which the FPGA does not specify.
// =============================================================================

void MouseDispatcher::reset()
{
    button_mask_  = 0;
    wheel_nibble_ = 0;
    // Re-emit cleared state to the mouse so it observes the dispatcher reset.
    mouse_.set_buttons(0);
    mouse_.set_wheel(0);
}

void MouseDispatcher::handle_motion(int dx, int dy)
{
    // Forward verbatim. KempstonMouse::inject_delta does the modulo-256
    // wrap-around per zxnext.vhd:3546 / 3553 (the X/Y registers are raw
    // 8-bit fields).
    mouse_.inject_delta(dx, dy);
}

void MouseDispatcher::handle_button(uint8_t sdl_button, bool pressed)
{
    // SDL_BUTTON_LEFT=1, SDL_BUTTON_MIDDLE=2, SDL_BUTTON_RIGHT=3.
    // KempstonMouse mask convention (mouse.h:33):
    //   bit 0 = R   (port 0xFADF bit 0 — VHDL i_MOUSE_BUTTON(1))
    //   bit 1 = L   (port 0xFADF bit 1 — VHDL i_MOUSE_BUTTON(0))
    //   bit 2 = M   (port 0xFADF bit 2 — VHDL i_MOUSE_BUTTON(2))
    uint8_t bit = 0;
    switch (sdl_button) {
    case SDL_BUTTON_LEFT:   bit = 0x02; break;  // L → bit 1
    case SDL_BUTTON_MIDDLE: bit = 0x04; break;  // M → bit 2
    case SDL_BUTTON_RIGHT:  bit = 0x01; break;  // R → bit 0
    default:
        // SDL_BUTTON_X1/X2 and unknowns are ignored — the Kempston mouse
        // protocol exposes only L/M/R.
        return;
    }
    if (pressed) {
        button_mask_ = static_cast<uint8_t>(button_mask_ | bit);
    } else {
        button_mask_ = static_cast<uint8_t>(button_mask_ & ~bit);
    }
    mouse_.set_buttons(button_mask_);
}

void MouseDispatcher::handle_wheel(int sdl_wheel_y)
{
    // 4-bit unsigned modulo-16 accumulator. Matches the VHDL roll-over at
    // zxnext.vhd:3560 / :104 (plan row MOUSE-10 = G — VHDL exposes only
    // the raw 4-bit field, signed/unsigned is host-adapter semantics).
    //
    // We treat SDL_MOUSEWHEEL.y as a signed step count: positive (wheel
    // rolled away from user) increments the counter, negative decrements,
    // both with 4-bit wrap. This matches the documented Kempston "wheel
    // ticks" behaviour observed by NextZXOS mouse drivers.
    int acc = static_cast<int>(wheel_nibble_) + sdl_wheel_y;
    // C++ `%` on negatives is implementation-defined for sign; use a
    // bitmask after normalising into 0..15 (works for any signed input).
    acc &= 0x0F;
    wheel_nibble_ = static_cast<uint8_t>(acc);
    mouse_.set_wheel(wheel_nibble_);
}

bool MouseDispatcher::handle_sdl_event(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_MOUSEMOTION:
        handle_motion(e.motion.xrel, e.motion.yrel);
        return true;
    case SDL_MOUSEBUTTONDOWN:
        handle_button(e.button.button, true);
        return true;
    case SDL_MOUSEBUTTONUP:
        handle_button(e.button.button, false);
        return true;
    case SDL_MOUSEWHEEL: {
        // SDL2 mousewheel events have a `direction` field — when set to
        // SDL_MOUSEWHEEL_FLIPPED, the y/x values are negated relative to
        // the natural direction. Normalise so `+y` always means "away
        // from user" regardless of OS preference.
        int y = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            y = -y;
        }
        handle_wheel(y);
        return true;
    }
    default:
        return false;
    }
}
