#pragma once
#include <SDL2/SDL.h>
#include "input/joystick_dispatcher.h"
#include "input/joy_source.h"

class Joystick;

/// Host-side owner of the two Next pad headers' SDL_GameController lifecycle
/// plus the JoystickDispatcher that translates their events into the Joystick
/// raw 12-bit vectors (Task 79).
///
/// Extracted so BOTH the SDL-only frontend (SdlApp) and the Qt GUI (QtApp)
/// get identical autodetect + routing behaviour — before Task 79 only the
/// SDL-only frontend had any gamepad support at all. A device is auto-assigned
/// to the first free connector slot whose input source is `Sdl`; slots the
/// user has assigned to `CursorKeys` are skipped so a physical pad never lands
/// on a keys-driven connector (its events would be gated away anyway).
///
/// This class touches only SDL + Joystick — no Qt, no core-emulator loop — so
/// either frontend can own one and just pump SDL events through handle_event().
class GamepadHost {
public:
    explicit GamepadHost(Joystick& joy) : dispatcher_(joy) {}
    ~GamepadHost();

    GamepadHost(const GamepadHost&)            = delete;
    GamepadHost& operator=(const GamepadHost&) = delete;

    JoystickDispatcher&       dispatcher()       { return dispatcher_; }
    const JoystickDispatcher& dispatcher() const { return dispatcher_; }

    /// Feed one SDL event. Consumes SDL_CONTROLLERDEVICEADDED /
    /// SDL_CONTROLLERDEVICEREMOVED (open/close + slot mapping) and forwards
    /// button/axis controller events to the dispatcher. Non-controller events
    /// are ignored.
    void handle_event(const SDL_Event& e);

    /// Re-seed the dispatcher's shadow after a state restore (rewind / load).
    void resync() { dispatcher_.resync(); }

    /// Select the host input source for connector `slot` (0 = Joy 1, 1 = Joy 2).
    void set_source(int slot, JoySource src) { dispatcher_.set_source(slot, src); }

private:
    JoystickDispatcher dispatcher_;
    // Open SDL_GameController* per connector slot; nullptr = free.
    SDL_GameController* controllers_[JoystickDispatcher::NUM_CONNECTORS] = { nullptr, nullptr };
};
