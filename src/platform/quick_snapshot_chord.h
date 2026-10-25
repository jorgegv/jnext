#pragma once

// GH #321 — the host chords for Quick Save / Quick Load snapshots, in ONE
// place, as a pure predicate (no SDL include) so it can be unit-tested.
//
// Alt+Shift+F5 = Quick Save, Alt+Shift+F9 = Quick Load: Alt and Shift held, and
// NO other modifier (GH #403: on macOS the host modifier is Command/GUI instead
// of Alt, see platform/host_modifier.h)
// (the same exact-modifier policy as Router::script_chord_held). Bare F5/F9
// are not claimed: they keep the EmuFnKeys strobe and the Multiface NMI.

#include "platform/host_modifier.h"

namespace jnext {

// Repeat: the key-autorepeat of a chord already acted on, swallowed (it must
// neither act again nor fall through to the F-key block).
enum class QuickChord { None, Save, Load, Repeat };

// GH #386 - a quick-snapshot request as the frontends carry it from the key
// Router's queue to the tick that services it (host_key_wiring.h).
enum class QuickAction { Save, Load };

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
// `host_is_gui` (GH #403): the host modifier is GUI (macOS), so GUI plays Alt's
// part and Alt plays GUI's.
inline QuickChord quick_snapshot_chord_held(int scancode, unsigned held, bool host_is_gui = false)
{
    using namespace host_modifier;
    return quick_snapshot_chord(scancode, (held & host_bits(host_is_gui)) != 0,
                                (held & kCtrlBits) != 0, (held & kShiftBits) != 0,
                                (held & other_bits(host_is_gui)) != 0);
}

// The press/release bookkeeping behind the chords, kept pure so it can be
// tested without a window: held modifier keys (from the event stream, not from
// SDL_GetModState(), because a whole chord can arrive in one poll batch) and
// which quick key is already down (so autorepeat acts once per press).
class QuickChordTracker {
public:
    // GH #403: the host modifier is GUI (macOS) rather than Alt.
    void set_host_is_gui(bool gui) { host_is_gui_ = gui; }
    bool host_is_gui() const { return host_is_gui_; }
    // GH #403: in GUI mode Alt (Option) is no host modifier at all; is it held?
    bool option_held_in_gui_mode() const
    {
        return host_is_gui_ && (mods_ & host_modifier::kAltBits) != 0;
    }
    // Is the host modifier (Alt, or GUI on macOS), left or right, held?
    bool host_modifier_held() const { return (mods_ & host_modifier::host_bits(host_is_gui_)) != 0; }
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
        const QuickChord c = quick_snapshot_chord_held(scancode, mods_, host_is_gui_);
        if (c == QuickChord::None) return c;
        if (down) return QuickChord::Repeat;
        down = true;
        return c;
    }
    unsigned held_modifiers() const { return mods_; }
private:
    unsigned mods_ = 0;
    bool host_is_gui_ = false;
    bool f5_ = false, f9_ = false;
};

}  // namespace jnext
