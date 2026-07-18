#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <SDL2/SDL.h>
#include "input/joy_source.h"

class Joystick;

/// Host-side dispatcher: translates SDL_CONTROLLERBUTTONDOWN/UP and
/// SDL_CONTROLLERAXISMOTION events into Joystick raw 12-bit per-connector
/// vectors. Closes plan rows JOY-WIRE-02/03/04 (gap G42).
///
/// This class plays the role of the host-side adapter for the two physical
/// pad headers on real Next hardware: the FPGA exposes `i_JOY_LEFT[10:0]` /
/// `i_JOY_RIGHT[10:0]` as inputs that an external host (the on-board MD6
/// shift-register / Kempston pin driver — or, in our case, the SDL/Qt host
/// event loop) must drive. There is no VHDL oracle for the dispatch itself
/// — the oracle is the shape that lands in the Joystick raw bit vector
/// (zxnext.vhd:3441-3442) which then composes via `Joystick::read_port_1f` /
/// `read_port_37` per zxnext.vhd:3470-3506. Mirror of the MouseDispatcher
/// (G43) shape.
///
/// **Two-pad routing policy** (JOY-WIRE-04): SDL controller index 0 →
/// `Joystick::set_joy_left()` (left connector / joy0); SDL controller
/// index 1 → `Joystick::set_joy_right()` (right connector / joy1).
/// Indices 2+ are ignored. This matches the canonical convention used by
/// FUSE / CSpect and by every commercial Next title that names the
/// physical pad headers "joy 1" / "joy 2".
///
/// **Bit layout** (per zxnext.vhd:3441-3442 — same as Joystick.h):
///
///   bit 11 = MODE   bit 10 = X     bit  9 = Z   bit  8 = Y
///   bit  7 = START  bit  6 = A
///   bit  5 = C(F2)  bit  4 = B(F1)
///   bit  3 = U      bit  2 = D     bit  1 = L   bit  0 = R
///
/// **Button mapping** (SDL → Kempston/MD6 logical bits):
///
///   SDL_CONTROLLER_BUTTON_A             → bit 4  (B / Fire 1)
///   SDL_CONTROLLER_BUTTON_B             → bit 5  (C / Fire 2)
///   SDL_CONTROLLER_BUTTON_X             → bit 6  (A — MD3 lower-row)
///   SDL_CONTROLLER_BUTTON_Y             → bit 11 (MODE — MD6 latch)
///   SDL_CONTROLLER_BUTTON_START         → bit 7  (START)
///   SDL_CONTROLLER_BUTTON_DPAD_UP       → bit 3  (U)
///   SDL_CONTROLLER_BUTTON_DPAD_DOWN     → bit 2  (D)
///   SDL_CONTROLLER_BUTTON_DPAD_LEFT     → bit 1  (L)
///   SDL_CONTROLLER_BUTTON_DPAD_RIGHT    → bit 0  (R)
///
/// **Axis mapping** (SDL_CONTROLLERAXISMOTION):
///
///   left-stick X / RIGHT_X axes  → digital L (bit 1) / R (bit 0)
///   left-stick Y / RIGHT_Y axes  → digital U (bit 3) / D (bit 2)
///
/// Threshold: SDL axes are signed 16-bit (-32768..+32767). We apply a
/// symmetric digital threshold at ~50% of full range (16384), with a
/// hysteresis-free polarity gate. Axes within the deadzone clear the
/// corresponding D-pad bits without disturbing the others.
class JoystickDispatcher {
public:
    /// Number of routed SDL controllers. Slots beyond this index are
    /// silently ignored. The Next hardware has exactly two physical pad
    /// headers — anything more would have to be host-side multiplexed
    /// outside the FPGA spec.
    static constexpr int NUM_CONNECTORS = 2;

    /// Axis threshold (digital U/D/L/R fires when |axis| >= AXIS_THRESHOLD).
    /// 16384 = 50% of int16 range, matches the convention used by SDL's
    /// own gamecontrollerdb and FUSE's joystick driver.
    static constexpr int16_t AXIS_THRESHOLD = 16384;

    /// Construct bound to a Joystick instance. The reference must outlive
    /// the dispatcher.
    explicit JoystickDispatcher(Joystick& joy) : joy_(joy) {}

    /// Logical cursor-key direction/fire bits (Task 79). Mapped internally
    /// to the Kempston/MD6 12-bit layout (U/D/L/R + Fire 1).
    enum class CursorBit : uint8_t { Up, Down, Left, Right, Fire };

    /// Reset internal per-connector bit vectors to zero. Does NOT reset
    /// the bound Joystick — callers manage that lifecycle. The per-connector
    /// input SOURCE (set_source) is a host configuration, not machine state,
    /// so it is preserved across reset() (mirrors the Joystick NR 0x05
    /// mode-preservation posture in joystick.cpp).
    void reset();

    /// Task 79 — select the host input source for connector `slot` (0 = Joy 1,
    /// 1 = Joy 2). Changing the source clears that connector's current bit
    /// vector (so a held SDL direction or cursor key does not linger under
    /// the new source) and re-emits. Out-of-range slots are ignored.
    ///
    /// The gate is enforced on BOTH input paths: SDL controller events
    /// (handle_button / handle_axis, reached via handle_sdl_event) are dropped
    /// for a slot whose source is CursorKeys; cursor-key events
    /// (set_cursor_bit) are dropped for a slot whose source is Sdl. Because at
    /// most one source is ever live per slot, the shared `bits_[slot]` shadow
    /// never has two writers.
    void set_source(int slot, JoySource src);

    /// Current source for connector `slot`. Out-of-range slots return Sdl.
    JoySource source(int slot) const {
        if (slot < 0 || slot >= NUM_CONNECTORS) return JoySource::Sdl;
        return source_[static_cast<size_t>(slot)];
    }

    /// Task 79 — cursor-key input path. Sets/clears the direction/fire bit for
    /// connector `slot`. Ignored unless that slot's source is CursorKeys (so a
    /// stray call while the connector is SDL-sourced is a harmless no-op). The
    /// updated 12-bit vector is re-emitted to the bound Joystick on any change.
    void set_cursor_bit(int slot, CursorBit b, bool pressed);

    /// Task 60c — re-seed this dispatcher's shadow (`bits_` + `axis_state_`)
    /// from the bound Joystick's CURRENT canonical vectors. Call after a
    /// rewind / save-load restore has overwritten the Joystick state:
    /// without it, the next controller event's `emit_to_joystick()` would
    /// push this dispatcher's STALE `bits_` back into the Joystick,
    /// silently stomping the restored value. Does NOT emit to the Joystick
    /// (it reads FROM it) and does NOT touch the device_map_.
    void resync();

    // ── Transport-agnostic API ──────────────────────────────────────────

    /// SDL_CONTROLLERBUTTONDOWN/UP → set/clear the corresponding 12-bit
    /// vector bit for the indicated connector. Out-of-range connector
    /// indices (>= NUM_CONNECTORS) are silently ignored — this is the
    /// JOY-WIRE-04 routing policy. Unmapped SDL buttons (e.g. LEFT/RIGHT
    /// shoulders, GUIDE) are also ignored.
    ///
    /// `sdl_button` is one of the SDL_GameControllerButton enum values
    /// (SDL_CONTROLLER_BUTTON_A etc.). `pressed` selects down vs. up. The
    /// updated 12-bit vector is re-emitted to the bound Joystick on every
    /// change via `set_joy_left()` / `set_joy_right()`.
    void handle_button(int controller_idx, uint8_t sdl_button, bool pressed);

    /// SDL_CONTROLLERAXISMOTION → apply digital threshold to derive the
    /// U/D/L/R bits for the indicated connector. `sdl_axis` is one of the
    /// SDL_GameControllerAxis enum values; `value` is the signed 16-bit
    /// axis position. The updated 12-bit vector is re-emitted on every
    /// transition that changes a D-pad bit.
    ///
    /// Trigger axes (LEFT_TRIGGER, RIGHT_TRIGGER) are unmapped — they have
    /// no Kempston/MD6 analogue. STICK X/Y axes for the right stick are
    /// folded into the same D-pad bits as the left stick (OR-merge), so
    /// either stick can drive the digital direction.
    void handle_axis(int controller_idx, uint8_t sdl_axis, int16_t value);

    // ── Raw SDL_Joystick path (Task 83) ─────────────────────────────────
    //
    // Devices SDL has no game-controller mapping for (older USB sticks, many
    // generic pads, the Logitech WingMan Precision, …) never produce
    // SDL_CONTROLLER* events at all — SDL_IsGameController() is false and only
    // the raw SDL_JOY* event family is emitted. Before Task 83 those devices
    // were dropped outright, which is issue #13: the pad is plugged in, and
    // jnext neither uses nor mentions it.
    //
    // A raw joystick has no semantic button names — just indices — so the
    // mapping is positional and deliberately conservative:
    //
    //   button 0 → bit 4  (B / Fire 1)      button 3 → bit 11 (MODE)
    //   button 1 → bit 5  (C / Fire 2)      button 4 → bit 7  (START)
    //   button 2 → bit 6  (A / MD3)         button 5+ → unmapped
    //
    //   axis 0   → digital L/R              axis 1   → digital U/D
    //   axis 2+  → unmapped (throttles, twist, extra sticks)
    //
    //   hat 0    → U/D/R/L, the usual D-pad on sticks that have one
    //
    // Axis and hat both fold into the SAME direction bits as the controller
    // path, so a stick with both a hat and an X/Y axis pair works either way.

    /// SDL_JOYBUTTONDOWN/UP → set/clear the logical bit for `raw_button`
    /// (a positional index, not an SDL_GameControllerButton). Buttons beyond
    /// the mapped range are ignored.
    void handle_raw_button(int connector_idx, uint8_t raw_button, bool pressed);

    /// SDL_JOYAXISMOTION → digital threshold on raw axis index `raw_axis`
    /// (0 = X, 1 = Y). Other axes are ignored.
    void handle_raw_axis(int connector_idx, uint8_t raw_axis, int16_t value);

    /// SDL_JOYHATMOTION → set the four direction bits from an SDL_HAT_* mask.
    /// A centred hat (SDL_HAT_CENTERED) clears all four.
    void handle_raw_hat(int connector_idx, uint8_t hat_value);

    // ── Production wiring ───────────────────────────────────────────────

    /// Convenience entry point for the SDL event loop: dispatches any of
    /// SDL_CONTROLLERBUTTONDOWN / SDL_CONTROLLERBUTTONUP /
    /// SDL_CONTROLLERAXISMOTION into the transport-agnostic API. Other
    /// event types are ignored (returns false). Returns true if the
    /// event was a controller event the dispatcher consumed.
    ///
    /// SDL's `cbutton.which` / `caxis.which` field is an
    /// `SDL_JoystickID` (the device-instance id assigned at open time);
    /// we rely on the host (SdlApp) to translate that id into a
    /// connector slot 0..NUM_CONNECTORS-1 BEFORE calling the
    /// transport-agnostic API. `handle_sdl_event` performs the
    /// translation via the host-installed `which_to_index_` table.
    bool handle_sdl_event(const SDL_Event& e);

    /// Register a host-resolved (instance-id → connector slot) mapping.
    /// SdlApp calls this on SDL_CONTROLLERDEVICEADDED with the
    /// `cdevice.which` joystick instance-id and the desired slot. Calling
    /// with `slot < 0` removes the mapping. Slots >= NUM_CONNECTORS are
    /// rejected (no-op).
    void map_instance_to_slot(int32_t sdl_instance_id, int slot);

    // ── Test introspection ──────────────────────────────────────────────

    /// Current 12-bit vector for connector `idx` (0 or 1). Mirrors what
    /// was last forwarded to Joystick::set_joy_left() / set_joy_right().
    /// Out-of-range indices return 0.
    uint16_t bits12(int idx) const {
        if (idx < 0 || idx >= NUM_CONNECTORS) return 0;
        return bits_[static_cast<size_t>(idx)];
    }

private:
    Joystick& joy_;
    std::array<uint16_t, NUM_CONNECTORS> bits_{};

    // Task 79 — per-connector host input source. Default Sdl for both keeps
    // the historical behaviour (pads auto-map to slots). Not serialised /
    // not touched by reset() — it is host configuration owned by the
    // Emulator's set_joystick_source() coordinator.
    std::array<JoySource, NUM_CONNECTORS> source_{ {JoySource::Sdl, JoySource::Sdl} };

    // Shared helper: OR/AND `jbit` into bits_[idx] and emit on change.
    // Used by both the SDL button path and the cursor-key path.
    void apply_bit(int idx, uint16_t jbit, bool pressed);

    // Sticky per-connector axis-state cache. We need it because SDL emits
    // axis events as absolute positions (not deltas), and a single axis
    // crossing the threshold from + to - produces two transitions that
    // must update opposite bits. Keep one byte per connector tracking
    // {Lact, Ract, Uact, Dact} so handle_axis can resolve U/D/L/R from a
    // partial axis update.
    struct AxisState {
        bool left_active   = false;
        bool right_active  = false;
        bool up_active     = false;
        bool down_active   = false;
    };
    std::array<AxisState, NUM_CONNECTORS> axis_state_{};

    // SDL instance-id → connector slot translation. SDL emits events
    // tagged with the joystick instance-id, NOT the controller index, so
    // we maintain a host-installed dispatch map. -1 means "not mapped".
    // Keep this small and fixed-size — a plain linear scan is faster than
    // a std::unordered_map for ≤4 entries on every controller event.
    struct DeviceSlot {
        int32_t instance_id = -1;
        int     slot        = -1;
    };
    static constexpr int MAX_DEVICES = 4;  // SDL allows many; we map up to 4
    std::array<DeviceSlot, MAX_DEVICES> device_map_{};

    // Helper: forward `bits_[idx]` to the bound Joystick on the
    // corresponding lane (idx==0 → left, idx==1 → right).
    void emit_to_joystick(int idx);

    // Helper: translate SDL_JoystickID → connector slot via device_map_.
    // Returns -1 if not mapped.
    int  resolve_instance_to_slot(int32_t sdl_instance_id) const;
};
