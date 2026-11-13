#include "platform/sdcard_upgrade_box.h"

#include "core/log.h"

#include <SDL3/SDL.h>

namespace sdcard {

UpgradeChoice upgrade_choice_for_button(int button_id) {
    switch (button_id) {
    case kUpgradeButtonReplace: return UpgradeChoice::Replace;
    case kUpgradeButtonNever:   return UpgradeChoice::KeepDontAskAgain;
    case kUpgradeButtonKeep:
    default:                    return UpgradeChoice::Keep;   // closed, Escape, anything else
    }
}

UpgradeChoice sdl_offer_upgrade(const std::string& from, const std::string& to,
                                const std::string& message) {
    const std::string title = distribution_title(to) + " is available";
    SDL_MessageBoxButtonData buttons[3] = {
        { 0, kUpgradeButtonReplace, "Download and replace" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
          kUpgradeButtonKeep, "Keep my card" },
        { 0, kUpgradeButtonNever, "Keep, do not ask again" },
    };
    SDL_MessageBoxData box{};
    box.flags      = SDL_MESSAGEBOX_WARNING;
    box.title      = title.c_str();
    box.message    = message.c_str();
    box.numbuttons = 3;
    box.buttons    = buttons;
    int pressed = kUpgradeButtonKeep;
    if (!SDL_ShowMessageBox(&box, &pressed)) {
        Log::emulator()->warn("sdcard: the upgrade offer from {} to {} could not be shown ({}); "
                              "keeping the card", from, to, SDL_GetError());
        return UpgradeChoice::Keep;
    }
    return upgrade_choice_for_button(pressed);
}

}  // namespace sdcard
