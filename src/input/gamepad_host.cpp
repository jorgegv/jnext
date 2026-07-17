#include "input/gamepad_host.h"
#include "input/joystick.h"

GamepadHost::~GamepadHost()
{
    for (auto*& c : controllers_) {
        if (c) {
            SDL_GameControllerClose(c);
            c = nullptr;
        }
    }
}

void GamepadHost::handle_event(const SDL_Event& e)
{
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        // e.cdevice.which is the device-index here (NOT the instance-id).
        // Open it, resolve the instance-id and map it to the first free
        // connector slot whose source is Sdl. Slots the user assigned to
        // CursorKeys are skipped — a physical pad must not occupy a
        // keys-driven connector (its events would be gated away anyway).
        const int dev_idx = e.cdevice.which;
        if (!SDL_IsGameController(dev_idx)) return;
        int slot = -1;
        for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
            if (controllers_[s] == nullptr &&
                dispatcher_.source(s) == JoySource::Sdl) {
                slot = s;
                break;
            }
        }
        if (slot < 0) return;   // no free SDL-sourced slot
        controllers_[slot] = SDL_GameControllerOpen(dev_idx);
        if (controllers_[slot]) {
            SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[slot]);
            if (js) {
                const SDL_JoystickID iid = SDL_JoystickInstanceID(js);
                dispatcher_.map_instance_to_slot(iid, slot);
            }
        }
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        // e.cdevice.which is the instance-id here.
        const SDL_JoystickID iid = e.cdevice.which;
        for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
            if (controllers_[s]) {
                SDL_Joystick* js = SDL_GameControllerGetJoystick(controllers_[s]);
                if (js && SDL_JoystickInstanceID(js) == iid) {
                    dispatcher_.map_instance_to_slot(iid, -1);
                    SDL_GameControllerClose(controllers_[s]);
                    controllers_[s] = nullptr;
                    break;
                }
            }
        }
    } else {
        dispatcher_.handle_sdl_event(e);
    }
}
