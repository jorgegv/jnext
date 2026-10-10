#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "input/joystick_dispatcher.h"
#include "input/joy_assign.h"
#include "input/joy_source.h"

class Joystick;

/// Host-side owner of the two Next pad headers' SDL_Gamepad lifecycle
/// plus the JoystickDispatcher that translates their events into the Joystick
/// raw 12-bit vectors (Task 79).
///
/// GH #311: which present device drives which connector is a POLICY decision
/// (joy_assign.h: explicit assignment, then sticky, then first-free fill), not a
/// side effect of arrival order. The host keeps the list of present devices and
/// re-resolves the bindings whenever a device arrives or leaves, a connector's
/// source changes, or an assignment is made.
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

    /// Feed one SDL event. Consumes SDL_EVENT_GAMEPAD_ADDED /
    /// SDL_EVENT_GAMEPAD_REMOVED and SDL_EVENT_JOYSTICK_ADDED /
    /// SDL_EVENT_JOYSTICK_REMOVED (open/close + slot mapping) and forwards
    /// button/axis/hat events to the dispatcher. Other events are ignored.
    void handle_event(const SDL_Event& e);

    /// Enumerate every joystick SDL currently knows about and open the ones
    /// that fit a free connector. Call once after SDL init and again whenever
    /// this host is recreated (cold boot): SDL only emits DEVICEADDED for
    /// devices that arrive AFTER the subsystem is up, so a host constructed
    /// while a pad is already plugged in would otherwise never see it.
    ///
    /// Idempotent — devices already known are skipped, so calling it twice does
    /// not double-open anything.
    void enumerate_existing_devices();

    /// Controllers currently present, in arrival order, with the connector
    /// (0/1/-1) each is bound to and its stable id ("<guid>" or "<guid>#N").
    std::vector<JoyDeviceInfo> devices() const;

    /// Fired after enumerate_existing_devices() and after every device arrival
    /// or removal, once the bindings have been re-resolved. The Qt frontend
    /// rebuilds the Input menu from it.
    std::function<void()> on_devices_changed;

    /// Assign controller `ref` (empty id = Automatic) to connector `slot`
    /// (0 = Joy 1, 1 = Joy 2) and re-resolve the bindings live. An id that is
    /// not present falls back to the first free controller, with a log line.
    void set_device(int slot, const JoyDeviceRef& ref);

    /// Test hook (JNEXT_TEST_VIRTUAL_JOYSTICKS): attach SDL virtual joysticks
    /// described by the environment variable, format
    ///   name:vvvv:pppp[:raw];name:vvvv:pppp[:raw];...
    /// (vendor/product in hex; `raw` = no gamepad mapping). Inert, returns 0,
    /// when the variable is unset. When it is set, the host also admits ONLY
    /// virtual devices (see set_virtual_only), so a real controller on the
    /// machine running a test cannot change what it observes. Call once, after
    /// SDL_Init and before the first enumerate_existing_devices().
    static int attach_test_devices_from_env();

    /// Process-wide: admit only SDL virtual devices. Test seam — SDL3 has no
    /// hint that disables its physical joystick backends (the HIDAPI hints do
    /// not cover Linux evdev), so hermetic tests filter instead.
    static void set_virtual_only(bool on) { virtual_only_ = on; }

    /// Re-seed the dispatcher's shadow after a state restore (rewind / load).
    void resync() { dispatcher_.resync(); }

    /// Select the host input source for connector `slot` (0 = Joy 1, 1 = Joy 2).
    /// Re-resolves the bindings, so a connector newly set to Sdl adopts a
    /// controller that is already plugged in, and one leaving Sdl lets go of its.
    void set_source(int slot, JoySource src);

private:
    struct Known {
        SDL_JoystickID iid;
        std::string    id;
        std::string    name;
    };
    std::vector<Known> known_;                 // arrival order
    JoyDeviceRef       assigned_[JoystickDispatcher::NUM_CONNECTORS];
    bool               fallback_logged_[JoystickDispatcher::NUM_CONNECTORS] = { false, false };
    bool               seen_devices_ = false;  // enumerate or an ADDED has run
    static inline bool virtual_only_ = false;

    // iid -> controller id, PROCESS-WIDE. A cold boot rebuilds the host (and
    // so known_) while SDL, its instance ids and the plugged pads carry on; the
    // ids must survive that, or two identical pads renumbered in arrival order
    // would trade `<guid>#N` and an assignment would move to the other physical
    // pad. Entries are dropped when SDL no longer lists the device.
    static inline std::map<SDL_JoystickID, std::string> id_registry_;
    static void purge_id_registry();

    // Record `iid` (no-op if known or filtered out); returns its index or -1.
    int add_known(SDL_JoystickID iid);
    // Re-resolve which device drives each connector and open/close to match.
    void reconcile();
    // Open known device `iid` into `slot` (gamepad if SDL has a mapping, raw otherwise).
    void open_into_slot(SDL_JoystickID iid, int slot);
    // Instance id held by `slot`, or 0.
    SDL_JoystickID held_instance(int slot) const;
    void log_unbound(const std::vector<SDL_JoystickID>& arrived) const;

    JoystickDispatcher dispatcher_;
    // Open SDL_Gamepad* per connector slot; nullptr = free.
    SDL_Gamepad* controllers_[JoystickDispatcher::NUM_CONNECTORS] = { nullptr, nullptr };

    // Task 83 — devices SDL has no game-controller mapping for are opened
    // through the raw SDL_Joystick API instead. A slot holds EITHER a
    // controller or a raw joystick, never both.
    SDL_Joystick* raw_joysticks_[JoystickDispatcher::NUM_CONNECTORS] = { nullptr, nullptr };

    // True if `iid` is one of our OPEN RAW joysticks. Used to drop the JOY*
    // event copies SDL also emits for game controllers, which would
    // otherwise apply every press twice.
    bool is_raw_instance(SDL_JoystickID iid) const;

    // Release whichever handle slot `s` holds and unmap it.
    void close_slot(int s);
};
