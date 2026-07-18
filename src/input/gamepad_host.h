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
    /// SDL_CONTROLLERDEVICEREMOVED and SDL_JOYDEVICEADDED /
    /// SDL_JOYDEVICEREMOVED (open/close + slot mapping) and forwards
    /// button/axis/hat events to the dispatcher. Other events are ignored.
    void handle_event(const SDL_Event& e);

    /// Enumerate every joystick SDL currently knows about and open the ones
    /// that fit a free connector. Call once after SDL init and again whenever
    /// this host is recreated (cold boot): SDL only emits DEVICEADDED for
    /// devices that arrive AFTER the subsystem is up, so a host constructed
    /// while a pad is already plugged in would otherwise never see it.
    ///
    /// Idempotent — devices already mapped to a slot are skipped, so calling
    /// it twice does not double-open anything.
    void enumerate_existing_devices();

    /// Re-seed the dispatcher's shadow after a state restore (rewind / load).
    void resync() { dispatcher_.resync(); }

    /// Select the host input source for connector `slot` (0 = Joy 1, 1 = Joy 2).
    void set_source(int slot, JoySource src) { dispatcher_.set_source(slot, src); }

private:
    JoystickDispatcher dispatcher_;
    // Open SDL_GameController* per connector slot; nullptr = free.
    SDL_GameController* controllers_[JoystickDispatcher::NUM_CONNECTORS] = { nullptr, nullptr };

    // Task 83 — devices SDL has no game-controller mapping for are opened
    // through the raw SDL_Joystick API instead. A slot holds EITHER a
    // controller or a raw joystick, never both.
    SDL_Joystick* raw_joysticks_[JoystickDispatcher::NUM_CONNECTORS] = { nullptr, nullptr };

    // Open the device at `dev_idx` into a free slot, as a game controller if
    // SDL has a mapping for it and as a raw joystick otherwise. No-op when
    // the device is already open or no slot is free. Logs the outcome.
    void open_device(int dev_idx);

    // First free connector slot whose source is Sdl, or -1 if none.
    int first_free_slot() const;

    // True if `iid` is one of our OPEN RAW joysticks. Used to drop the JOY*
    // event copies SDL also emits for game controllers, which would
    // otherwise apply every press twice.
    bool is_raw_instance(SDL_JoystickID iid) const;

    // Release whichever handle slot `s` holds and unmap it.
    void close_slot(int s);
};
