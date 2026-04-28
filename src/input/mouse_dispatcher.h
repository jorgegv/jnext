#pragma once
#include <cstdint>
#include <SDL2/SDL.h>

class KempstonMouse;

/// Host-side dispatcher: translates SDL_MOUSEMOTION / SDL_MOUSEBUTTON{DOWN,UP}
/// / SDL_MOUSEWHEEL events into KempstonMouse counter / button / wheel
/// updates. Closes plan rows MOUSE-13/14/15 (gap G43).
///
/// This class plays the role of the PS/2 host adapter on real Next hardware:
/// the FPGA exposes i_MOUSE_X / i_MOUSE_Y / i_MOUSE_BUTTON / i_MOUSE_WHEEL as
/// inputs that an external host (PS/2 driver or, in our case, the SDL/Qt
/// host event loop) must drive. There is no VHDL oracle for the dispatch
/// itself — the oracle is the shape that lands in the KempstonMouse
/// register file (see plan rows MOUSE-01..MOUSE-11, all VHDL-cited).
///
/// Two surfaces are exposed:
///
///   1. A transport-agnostic API (handle_motion / handle_button /
///      handle_wheel) — no SDL_Event construction needed by callers,
///      makes the unit tests SDL-event-loop-free.
///   2. A convenience handle_sdl_event(const SDL_Event&) helper for the
///      production SdlInput path, which forwards into the transport-
///      agnostic API.
///
/// Button mapping (SDL → Kempston): SDL_BUTTON_LEFT=1, _MIDDLE=2, _RIGHT=3.
/// KempstonMouse::set_buttons() takes a 3-bit mask with bit 0 = R, bit 1 = L,
/// bit 2 = M (mouse.h:33). The dispatcher tracks pressed buttons internally
/// as an active-high mask and re-emits set_buttons() on every change.
///
/// Wheel mapping (SDL → Kempston): SDL_MOUSEWHEEL.y is a signed step count
/// (positive = wheel rolled away from user). The Kempston wheel is a 4-bit
/// unsigned counter (zxnext.vhd:3560 high nibble) — we accumulate signed
/// steps modulo-16 (matching the VHDL roll-over at MOUSE-10 = G).
class MouseDispatcher {
public:
    /// Construct bound to a KempstonMouse instance. The reference must
    /// outlive the dispatcher.
    explicit MouseDispatcher(KempstonMouse& mouse) : mouse_(mouse) {}

    /// Reset internal button mask + wheel counter. Does NOT reset the
    /// bound KempstonMouse — callers manage that lifecycle.
    void reset();

    // ── Transport-agnostic API ──────────────────────────────────────────

    /// SDL_MOUSEMOTION → inject relative delta into KempstonMouse counters.
    /// dx>0 = right, dy>0 = down (matches SDL's screen-space convention,
    /// which also matches the Kempston Y-counter polarity — both grow
    /// downward).
    void handle_motion(int dx, int dy);

    /// SDL_MOUSEBUTTON{DOWN,UP} → update the 3-bit button mask. `sdl_button`
    /// is one of SDL_BUTTON_LEFT (1), SDL_BUTTON_MIDDLE (2),
    /// SDL_BUTTON_RIGHT (3); other values are ignored. `pressed` selects
    /// down vs. up. The aggregated mask is re-emitted to KempstonMouse on
    /// every change.
    void handle_button(uint8_t sdl_button, bool pressed);

    /// SDL_MOUSEWHEEL → accumulate signed Y-axis step into the 4-bit
    /// unsigned wheel counter modulo-16. SDL's `y` field is the signed
    /// step count (positive = away from user). X-axis wheel is ignored
    /// (no Kempston field).
    void handle_wheel(int sdl_wheel_y);

    // ── Production wiring ───────────────────────────────────────────────

    /// Convenience entry point for the SDL event loop: dispatches any of
    /// SDL_MOUSEMOTION / SDL_MOUSEBUTTONDOWN / SDL_MOUSEBUTTONUP /
    /// SDL_MOUSEWHEEL into the transport-agnostic API. Other event types
    /// are ignored (returns false). Returns true if the event was a mouse
    /// event the dispatcher consumed.
    bool handle_sdl_event(const SDL_Event& e);

    // ── Test introspection ──────────────────────────────────────────────

    /// Current aggregated active-high button mask (bit 0=R, 1=L, 2=M).
    /// Mirrors what was last forwarded to KempstonMouse::set_buttons().
    uint8_t button_mask() const { return button_mask_; }

    /// Current accumulated 4-bit wheel counter (mirrors what was last
    /// forwarded to KempstonMouse::set_wheel()).
    uint8_t wheel_nibble() const { return wheel_nibble_; }

private:
    KempstonMouse& mouse_;
    uint8_t        button_mask_  = 0;  // bit 0=R, 1=L, 2=M (matches mouse.h:33)
    uint8_t        wheel_nibble_ = 0;  // 4-bit unsigned, zxnext.vhd:3560 high nibble
};
