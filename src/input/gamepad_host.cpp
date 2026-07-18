#include "input/gamepad_host.h"
#include "input/joystick.h"
#include "core/log.h"

GamepadHost::~GamepadHost()
{
    for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
        close_slot(s);
    }
}

int GamepadHost::first_free_slot() const
{
    for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
        // A slot is free only if BOTH handles are null — it holds either a
        // controller or a raw joystick, never both. Slots the user assigned
        // to CursorKeys are skipped: a physical pad must not occupy a
        // keys-driven connector (its events would be gated away anyway).
        if (controllers_[s] == nullptr && raw_joysticks_[s] == nullptr &&
            dispatcher_.source(s) == JoySource::Sdl) {
            return s;
        }
    }
    return -1;
}

bool GamepadHost::is_raw_instance(SDL_JoystickID iid) const
{
    for (auto* js : raw_joysticks_) {
        if (js && SDL_JoystickInstanceID(js) == iid) return true;
    }
    return false;
}

void GamepadHost::close_slot(int s)
{
    if (controllers_[s]) {
        SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[s]);
        if (js) dispatcher_.map_instance_to_slot(SDL_JoystickInstanceID(js), -1);
        SDL_GameControllerClose(controllers_[s]);
        controllers_[s] = nullptr;
    }
    if (raw_joysticks_[s]) {
        dispatcher_.map_instance_to_slot(SDL_JoystickInstanceID(raw_joysticks_[s]), -1);
        SDL_JoystickClose(raw_joysticks_[s]);
        raw_joysticks_[s] = nullptr;
    }
}

void GamepadHost::open_device(int dev_idx)
{
    const char* name  = SDL_JoystickNameForIndex(dev_idx);
    const char* label = name ? name : "(unnamed device)";

    // Already open? SDL_JoystickGetDeviceInstanceID gives the instance id the
    // device WOULD have; if we already hold it, this is a duplicate ADDED or a
    // re-enumeration pass over a device we opened earlier.
    const SDL_JoystickID iid = SDL_JoystickGetDeviceInstanceID(dev_idx);
    if (iid >= 0) {
        for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
            if (raw_joysticks_[s] && SDL_JoystickInstanceID(raw_joysticks_[s]) == iid) return;
            if (controllers_[s]) {
                SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[s]);
                if (js && SDL_JoystickInstanceID(js) == iid) return;
            }
        }
    }

    const int slot = first_free_slot();
    if (slot < 0) {
        // Both Next pad headers are taken (or assigned to cursor keys). Say so
        // — a silently ignored pad is exactly the complaint in issue #13.
        Log::input()->info("Joystick '{}' detected but not connected: "
                           "both Next joystick connectors are already in use",
                           label);
        return;
    }

    if (SDL_IsGameController(dev_idx)) {
        controllers_[slot] = SDL_GameControllerOpen(dev_idx);
        if (!controllers_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_GameControllerOpen failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[slot]);
        if (js) dispatcher_.map_instance_to_slot(SDL_JoystickInstanceID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' (gamepad, SDL mapping)",
                           slot + 1, label);
    } else {
        // No SDL game-controller mapping — open it raw. Before Task 83 this
        // device was dropped on the floor with no message at all (issue #13).
        raw_joysticks_[slot] = SDL_JoystickOpen(dev_idx);
        if (!raw_joysticks_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_JoystickOpen failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = raw_joysticks_[slot];
        dispatcher_.map_instance_to_slot(SDL_JoystickInstanceID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' (raw joystick, no SDL "
                           "gamepad mapping: {} axes, {} buttons, {} hats)",
                           slot + 1, label,
                           SDL_JoystickNumAxes(js), SDL_JoystickNumButtons(js),
                           SDL_JoystickNumHats(js));
    }
}

void GamepadHost::enumerate_existing_devices()
{
    const int n = SDL_NumJoysticks();
    Log::input()->info("Joystick scan: {} device(s) present", n);
    for (int i = 0; i < n; ++i) {
        open_device(i);
    }
}

void GamepadHost::handle_event(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_CONTROLLERDEVICEADDED:
        // Deliberately a no-op: SDL emits BOTH this and SDL_JOYDEVICEADDED for
        // a controller-mapped device. Routing every arrival through the JOY
        // case keeps open/dedupe logic in one place and means a device with no
        // controller mapping takes the identical path.
        break;

    case SDL_JOYDEVICEADDED:
        // e.jdevice.which is the device-index here (NOT the instance-id).
        open_device(e.jdevice.which);
        break;

    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_JOYDEVICEREMOVED: {
        // e.*device.which is the instance-id here. Close whichever slot owns
        // it; the paired event then finds nothing and no-ops.
        const SDL_JoystickID iid = e.jdevice.which;
        for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
            bool match = false;
            if (raw_joysticks_[s] && SDL_JoystickInstanceID(raw_joysticks_[s]) == iid) {
                match = true;
            } else if (controllers_[s]) {
                SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[s]);
                if (js && SDL_JoystickInstanceID(js) == iid) match = true;
            }
            if (match) {
                Log::input()->info("Joystick {} disconnected", s + 1);
                close_slot(s);
                break;
            }
        }
        break;
    }

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYAXISMOTION:
    case SDL_JOYHATMOTION: {
        // SDL emits the JOY* family for game controllers TOO, on top of the
        // CONTROLLER* events. Applying both would register every press twice
        // (and through two different button tables), so only devices we opened
        // raw are allowed down this path.
        SDL_JoystickID iid = -1;
        switch (e.type) {
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:   iid = e.jbutton.which; break;
        case SDL_JOYAXISMOTION: iid = e.jaxis.which;   break;
        default:                iid = e.jhat.which;    break;
        }
        if (!is_raw_instance(iid)) break;
        dispatcher_.handle_sdl_event(e);
        break;
    }

    default:
        dispatcher_.handle_sdl_event(e);
        break;
    }
}
