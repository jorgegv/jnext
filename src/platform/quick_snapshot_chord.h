#pragma once

// GH #321 — the host chords for Quick Save / Quick Load snapshots, in ONE
// place, as a pure predicate (no SDL include) so it can be unit-tested.
//
// Alt+F5 = Quick Save, Alt+F9 = Quick Load, Alt held with NO other modifier
// (the same exact-modifier policy as Router::script_chord_held). Bare F5/F9
// are not claimed: they keep the EmuFnKeys strobe and the Multiface NMI.

namespace jnext {

enum class QuickChord { None, Save, Load };

// USB HID scancodes, the values SDL_SCANCODE_F5 / SDL_SCANCODE_F9 take
// (static_assert'ed in sdl_app.cpp).
constexpr int kQuickSaveScancode = 62;   // F5
constexpr int kQuickLoadScancode = 66;   // F9

inline QuickChord quick_snapshot_chord(int scancode, bool alt, bool ctrl,
                                       bool shift, bool gui)
{
    if (!alt || ctrl || shift || gui) return QuickChord::None;
    if (scancode == kQuickSaveScancode) return QuickChord::Save;
    if (scancode == kQuickLoadScancode) return QuickChord::Load;
    return QuickChord::None;
}

}  // namespace jnext
