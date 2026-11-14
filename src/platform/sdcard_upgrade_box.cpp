#include "platform/sdcard_upgrade_box.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#ifdef __linux__
#include <sys/stat.h>
#endif

namespace sdcard {

UpgradeChoice upgrade_choice_for_button(int button_id) {
    switch (button_id) {
    case kUpgradeButtonReplace: return UpgradeChoice::Replace;
    case kUpgradeButtonNever:   return UpgradeChoice::KeepDontAskAgain;
    case kUpgradeButtonKeep:
    default:                    return UpgradeChoice::Keep;   // closed, Escape, anything else
    }
}

bool box_would_hang(bool linux_os, bool zenity_in_path, bool session_bus) {
    return linux_os && zenity_in_path && !session_bus;
}

namespace {
const char* const kHowToUpgrade = "run jnext --sdcard-upgrade yes to upgrade";

bool on_linux() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool zenity_in_path() {
#ifdef __linux__
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        struct stat st{};
        if (!dir.empty() && ::stat((dir + "/zenity").c_str(), &st) == 0 && (st.st_mode & S_IXUSR))
            return true;
    }
#endif
    return false;
}

bool session_bus() {
#ifdef __linux__
    const char* addr = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (addr && *addr) return true;
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    struct stat st{};
    return rt && *rt && ::stat((std::string(rt) + "/bus").c_str(), &st) == 0;
#else
    return true;   // not Linux: box_would_hang() never asks
#endif
}

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
    if (box_would_hang(on_linux(), zenity_in_path(), session_bus())) {
        say_kept(from, to, "zenity has no session bus to show it on");
        return UpgradeChoice::Keep;
    }
    if (!SDL_ShowMessageBox(&box, &pressed)) {
        say_kept(from, to, SDL_GetError());
        return UpgradeChoice::Keep;
    }
    return upgrade_choice_for_button(pressed);
}

}  // namespace sdcard
