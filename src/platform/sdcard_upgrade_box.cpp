#include "platform/sdcard_upgrade_box.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace sdcard {

UpgradeChoice upgrade_choice_for_button(int button_id) {
    switch (button_id) {
    case kUpgradeButtonReplace: return UpgradeChoice::Replace;
    case kUpgradeButtonNever:   return UpgradeChoice::KeepDontAskAgain;
    case kUpgradeButtonKeep:
    default:                    return UpgradeChoice::Keep;   // closed, Escape, anything else
    }
}

namespace {
const char* const kHowToUpgrade = "run jnext --sdcard-upgrade yes to upgrade";

void say_kept(const std::string& from, const std::string& to, const std::string& why) {
    const std::string new_title = distribution_title(to);
    Log::emulator()->warn("sdcard: the upgrade offer from {} to {} could not be shown ({}); "
                          "keeping the card; {}", distribution_title(from), new_title, why, kHowToUpgrade);
    std::fprintf(stderr, "warning: %s is available but the offer could not be shown (%s); "
                 "keeping the card; %s\n", new_title.c_str(), why.c_str(), kHowToUpgrade);
}
}  // namespace

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
    // On Linux SDL shows this through zenity when it is installed, and waits for it with no timeout:
    // a zenity started on a display other than the desktop session's (an Xvfb next to a real
    // session bus, say) can wait without ever drawing a window. A real desktop is not affected.
    if (!SDL_ShowMessageBox(&box, &pressed)) {
        say_kept(from, to, SDL_GetError());
        return UpgradeChoice::Keep;
    }
    return upgrade_choice_for_button(pressed);
}

}  // namespace sdcard
