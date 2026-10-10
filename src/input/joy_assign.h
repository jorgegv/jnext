#pragma once
#include <array>
#include <string>
#include <vector>
#include "input/joy_source.h"

// Which physical controller drives which Next joystick connector (GH #311).
// Pure policy: no SDL, no Qt. GamepadHost feeds it the present devices and
// opens what it answers; the menu and Preferences build their lists from
// joy_choices(), so the two cannot disagree.

/// Smallest ordinal >= 1 that no id in `present_ids` holding the same GUID
/// already uses. Unplugging pad #1 leaves pad #2 as "#2"; replugging gives #1
/// back. `guid` is the 32-hex GUID (no "#N").
int next_joy_ordinal(const std::vector<std::string>& present_ids, const std::string& guid);

/// Device index (into `present_ids`, arrival order) for each connector, or -1.
///   R1  a connector whose source is not Sdl gets -1 (a dormant id reserves nothing);
///   R2  an Sdl connector whose `assigned` id is present gets it, pre-empting
///       whoever holds it;
///   R3  other Sdl connectors keep `current` if that device is still present
///       and not taken by R2 (sticky: a pad is never moved under the player);
///   R4  the rest take the first untaken present device, Joy 1 then Joy 2.
/// An assigned id that is absent falls through to R3/R4: that is the fallback.
std::array<int, 2> resolve_joy_assignment(const std::vector<std::string>& present_ids,
                                          const JoySource src[2],
                                          const std::string assigned[2],
                                          const int current[2]);

/// One entry of the per-connector choice list.
struct JoyChoice {
    enum class Kind { Auto, Device, Missing, Keys, None };
    Kind        kind = Kind::Auto;
    std::string id;       // Device / Missing
    std::string label;    // raw text, not Qt-escaped
    bool        checked = false;
};

/// Automatic, one entry per present device (name, plus " #N" for ordinal >= 2),
/// "<name> (not connected)" when `assigned` names an absent device, Cursor
/// Keys + Space, None. Exactly one entry is checked.
std::vector<JoyChoice> joy_choices(int connector, const std::vector<JoyDeviceInfo>& devices,
                                   JoySource src, const JoyDeviceRef& assigned);
