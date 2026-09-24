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
        if (js && SDL_GetJoystickID(js) == iid) return true;
    }
    return false;
}

void GamepadHost::close_slot(int s)
{
    if (controllers_[s]) {
        SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[s]);
        if (js) dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), -1);
        SDL_CloseGamepad(controllers_[s]);
        controllers_[s] = nullptr;
    }
    if (raw_joysticks_[s]) {
        dispatcher_.map_instance_to_slot(SDL_GetJoystickID(raw_joysticks_[s]), -1);
        SDL_CloseJoystick(raw_joysticks_[s]);
        raw_joysticks_[s] = nullptr;
    }
}

void GamepadHost::open_device(SDL_JoystickID iid)
{
    // 0 is SDL3's invalid instance id (SDL_JoystickID is Uint32; SDL2 used a
    // signed id with -1). Nothing can be opened from it, and letting it reach
    // the dedupe scan below would compare against closed slots' zeroes.
    if (iid == 0) return;

    const char* name  = SDL_GetJoystickNameForID(iid);
    const char* label = name ? name : "(unnamed device)";

    // Already open? The event carries the instance id directly in SDL3, so
    // this is a plain identity check — SDL2 needed a speculative open-then-ask
    // dance to find out what id the device WOULD have.
    for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
        if (raw_joysticks_[s] && SDL_GetJoystickID(raw_joysticks_[s]) == iid) return;
        if (controllers_[s]) {
            SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[s]);
            if (js && SDL_GetJoystickID(js) == iid) return;
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

    if (SDL_IsGamepad(iid)) {
        controllers_[slot] = SDL_OpenGamepad(iid);
        if (!controllers_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_OpenGamepad failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[slot]);
        if (js) dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' (gamepad, SDL mapping)",
                           slot + 1, label);
    } else {
        // No SDL gamepad mapping — open it raw. Before Task 83 this
        // device was dropped on the floor with no message at all (issue #13).
        raw_joysticks_[slot] = SDL_OpenJoystick(iid);
        if (!raw_joysticks_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_OpenJoystick failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = raw_joysticks_[slot];
        dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' (raw joystick, no SDL "
                           "gamepad mapping: {} axes, {} buttons, {} hats)",
                           slot + 1, label,
                           SDL_GetNumJoystickAxes(js), SDL_GetNumJoystickButtons(js),
                           SDL_GetNumJoystickHats(js));
    }
}

void GamepadHost::enumerate_existing_devices()
{
    // SDL3 returns a malloc'd, 0-terminated array of INSTANCE IDS; SDL2 gave
    // a count to index-loop over. The array must be released with SDL_free
    // even when the count is zero (SDL still allocates the terminator).
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    Log::input()->info("Joystick scan: {} device(s) present", count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        open_device(ids[i]);
    }
    SDL_free(ids);
}

void GamepadHost::handle_event(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        // Deliberately a no-op: SDL emits BOTH this and SDL_EVENT_JOYSTICK_ADDED
        // for a gamepad-mapped device. Routing every arrival through the JOY
        // case keeps open/dedupe logic in one place and means a device with no
        // gamepad mapping takes the identical path.
        break;

    case SDL_EVENT_JOYSTICK_ADDED:
        // e.jdevice.which is the INSTANCE ID in SDL3. (Under SDL2 this one
        // event was the odd one out and carried a device INDEX instead, which
        // is why the old open_device took an index.)
        open_device(e.jdevice.which);
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED: {
        // e.*device.which is the instance-id here. Close whichever slot owns
        // it; the paired event then finds nothing and no-ops.
        const SDL_JoystickID iid = e.jdevice.which;
        for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
            bool match = false;
            if (raw_joysticks_[s] && SDL_GetJoystickID(raw_joysticks_[s]) == iid) {
                match = true;
            } else if (controllers_[s]) {
                SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[s]);
                if (js && SDL_GetJoystickID(js) == iid) match = true;
            }
            if (match) {
                Log::input()->info("Joystick {} disconnected", s + 1);
                close_slot(s);
                break;
            }
        }
        break;
    }

    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_HAT_MOTION: {
        // SDL emits the JOY* family for gamepads TOO, on top of the
        // GAMEPAD* events. Applying both would register every press twice
        // (and through two different button tables), so only devices we opened
        // raw are allowed down this path.
        SDL_JoystickID iid = 0;
        switch (e.type) {
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:   iid = e.jbutton.which; break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: iid = e.jaxis.which;   break;
        default:                             iid = e.jhat.which;    break;
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
