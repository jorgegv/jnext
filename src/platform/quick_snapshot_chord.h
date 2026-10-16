#pragma once

// GH #321 — the host chords for Quick Save / Quick Load snapshots, in ONE
// place, as a pure predicate (no SDL include) so it can be unit-tested.
//
// Alt+Shift+F5 = Quick Save, Alt+Shift+F9 = Quick Load: Alt and Shift held, and
// NO other modifier
// (the same exact-modifier policy as Router::script_chord_held). Bare F5/F9
// are not claimed: they keep the EmuFnKeys strobe and the Multiface NMI.

namespace jnext {

// Repeat: the key-autorepeat of a chord already acted on, swallowed (it must
// neither act again nor fall through to the F-key block).
enum class QuickChord { None, Save, Load, Repeat };

// USB HID scancodes, the values SDL_SCANCODE_F5 / SDL_SCANCODE_F9 take
// (static_assert'ed in sdl_app.cpp).
constexpr int kQuickSaveScancode = 62;   // F5
constexpr int kQuickLoadScancode = 66;   // F9

inline QuickChord quick_snapshot_chord(int scancode, bool alt, bool ctrl,
                                       bool shift, bool gui)
{
    if (!alt || !shift || ctrl || gui) return QuickChord::None;
    if (scancode == kQuickSaveScancode) return QuickChord::Save;
    if (scancode == kQuickLoadScancode) return QuickChord::Load;
    return QuickChord::None;
}

// The same, from a bitmask of held modifier KEYS (bit n = scancode 224+n:
// LCtrl LShift LAlt LGui RCtrl RShift RAlt RGui), left or right counting alike.
inline QuickChord quick_snapshot_chord_held(int scancode, unsigned held)
{
    return quick_snapshot_chord(scancode, (held & 0x44u) != 0, (held & 0x11u) != 0,
                                (held & 0x22u) != 0, (held & 0x88u) != 0);
}

// The press/release bookkeeping behind the chords, kept pure so it can be
// tested without a window: held modifier keys (from the event stream, not from
// SDL_GetModState(), because a whole chord can arrive in one poll batch) and
// which quick key is already down (so autorepeat acts once per press).
class QuickChordTracker {
public:
    // Feed EVERY key event the pointer-release gate does not consume (GH #365), press and release. Returns what a PRESS means.
    QuickChord on_key(int scancode, bool pressed)
    {
        if (scancode >= 224 && scancode <= 231) {
            const unsigned bit = 1u << (scancode - 224);
            if (pressed) mods_ |= bit; else mods_ &= ~bit;
            return QuickChord::None;
        }
        if (scancode != kQuickSaveScancode && scancode != kQuickLoadScancode)
            return QuickChord::None;
        bool& down = (scancode == kQuickSaveScancode) ? f5_ : f9_;
        if (!pressed) { down = false; return QuickChord::None; }
        const QuickChord c = quick_snapshot_chord_held(scancode, mods_);
        if (c == QuickChord::None) return c;
        if (down) return QuickChord::Repeat;
        down = true;
        return c;
    }
    unsigned held_modifiers() const { return mods_; }
private:
    unsigned mods_ = 0;
    bool f5_ = false, f9_ = false;
};

}  // namespace jnext
