#pragma once

#include "core/sdcard_provisioner.h"

#include <string>

namespace sdcard {

/// The SDL frontends' UpgradeFn: an SDL message box with three buttons, "Download and
/// replace", "Keep my card" and "Keep my card, do not ask again". Closing the box or
/// pressing Escape is Keep. SDL documents SDL_ShowMessageBox as usable before
/// SDL_Init; if no box can be shown (no display, a driver without message boxes)
/// the answer is Keep and the reason is logged, so a session never replaces a card
/// the user was not asked about.
UpgradeChoice sdl_offer_upgrade(const std::string& from, const std::string& to,
                                const std::string& message);

/// True when SDL3's message box would HANG instead of failing: on Linux SDL runs zenity, and a
/// zenity with no session bus (no DBUS_SESSION_BUS_ADDRESS and no <XDG_RUNTIME_DIR>/bus socket) sits
/// forever with no window, so the Keep fallback never fires. SDL has no other condition for it:
/// it runs zenity whenever `zenity --version` works (SDL_zenitymessagebox.c), else its own X11 box.
/// The three facts are the caller's; the rule is here so a test can pin it.
bool box_would_hang(bool linux_os, bool zenity_in_path, bool session_bus);

/// The button ids of that box and the choice each means, once, for the box and its test.
UpgradeChoice upgrade_choice_for_button(int button_id);
constexpr int kUpgradeButtonReplace = 1, kUpgradeButtonKeep = 2, kUpgradeButtonNever = 3;

}  // namespace sdcard
