#pragma once

// GH #403 - which modifier key carries the HOST shortcuts, per platform.
//
// Everywhere but macOS it is Alt (GH #115): Ctrl is the guest's Symbol Shift,
// so a Ctrl+letter host shortcut would eat a Symbol Shift sequence. On macOS
// the convention - and what Mac users expect - is Command. Command is not a
// guest key there, and the physical Control key is Symbol Shift, so the two
// namespaces stay apart exactly as Alt/Ctrl do elsewhere.
//
// In SDL terms the host modifier is LGUI/RGUI (SDL never swaps Control and
// Command, unlike Qt: see gui/qt_ctrl_is_command.h). Every consumer takes the
// choice as a PARAMETER whose default is the Alt form, so a suite exercises
// the macOS branch on any OS and the existing Alt rows mean the same on a Mac
// build; the frontends pass kHostModifierIsGui.

namespace host_modifier {

#if defined(__APPLE__)
inline constexpr bool kIsGui = true;
#else
inline constexpr bool kIsGui = false;
#endif

/// Bit n = scancode 224+n: LCtrl LShift LAlt LGui RCtrl RShift RAlt RGui
/// (the layout of jnext::QuickChordTracker::held_modifiers()).
inline constexpr unsigned kCtrlBits  = 0x11u;
inline constexpr unsigned kShiftBits = 0x22u;
inline constexpr unsigned kAltBits   = 0x44u;
inline constexpr unsigned kGuiBits   = 0x88u;

/// The held-bits of the host modifier / of the one that is NOT the host's
/// (the other of Alt and GUI), for either choice.
inline constexpr unsigned host_bits(bool host_is_gui)  { return host_is_gui ? kGuiBits : kAltBits; }
inline constexpr unsigned other_bits(bool host_is_gui) { return host_is_gui ? kAltBits : kGuiBits; }

}  // namespace host_modifier
