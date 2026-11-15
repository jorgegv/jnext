// The GPA fixture helpers, shared by input_integration_test and
// input_integration_native_test (single source): attach a virtual joystick shaped as
// a gamepad (raw = false) or as a raw stick (raw = true), and read a device's GUID.
#pragma once

#include <SDL3/SDL.h>

#include <string>

inline SDL_JoystickID gpa_attach(const char* name, Uint16 vendor, Uint16 product, bool raw) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.vendor_id  = vendor;
    desc.product_id = product;
    desc.name       = name;
    if (raw) {
        desc.type     = SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.naxes    = 2;
        desc.nbuttons = 2;
    } else {
        desc.type     = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes    = 6;
        desc.nbuttons = 11;
        desc.nhats    = 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                           (1u << SDL_GAMEPAD_BUTTON_WEST)  | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                           (1u << SDL_GAMEPAD_BUTTON_DPAD_UP)   | (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                           (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    }
    return SDL_AttachVirtualJoystick(&desc);
}

inline std::string gpa_guid(SDL_JoystickID iid) {
    char buf[33] = {};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(iid), buf, sizeof(buf));
    return buf;
}
