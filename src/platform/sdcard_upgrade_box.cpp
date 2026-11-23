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
                                const std::string& message, bool files_carry_over) {
    const std::string title = distribution_title(to) + " is available";
    SDL_MessageBoxButtonData buttons[3] = {
        { 0, kUpgradeButtonReplace, files_carry_over ? kUpgradeLabelReplace : kUpgradeLabelFresh },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
          kUpgradeButtonKeep, kUpgradeLabelKeep },
        { 0, kUpgradeButtonNever, kUpgradeLabelNever },
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

std::string report_box_text(const std::string& report, const std::string& report_path) {
    std::string out;
    size_t pos = 0;
    int lines = 0;
    while (pos < report.size() && lines < 20) {
        const size_t nl = report.find('\n', pos);
        out += report.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos + 1);
        if (nl == std::string::npos) { out += "\n"; pos = report.size(); break; }
        pos = nl + 1;
        ++lines;
    }
    if (pos < report.size()) out += "...\nthe full list is in " + report_path + "\n";
    return out;
}

void sdl_show_report(const std::string& report) {
    const std::string path = default_sdcard_dir() + "/previous/upgrade-report.txt";
    const std::string text = report_box_text(report, path);
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SD card upgraded", text.c_str(), nullptr))
        Log::emulator()->warn("sdcard: the upgrade report box could not be shown ({}); the report is in {}",
                              SDL_GetError(), path);
}

void sdl_show_notice(const std::string& message) {
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SD card", message.c_str(), nullptr))
        Log::emulator()->warn("sdcard: the notice box could not be shown ({}): {}", SDL_GetError(), message);
}

}  // namespace sdcard
