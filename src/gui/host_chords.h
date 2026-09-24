#pragma once

// GH #1 review follow-up — which key combinations the HOST WINDOWS already
// claim as QAction shortcuts.
//
// The set is HARVESTED FROM THE REAL WIDGETS rather than hand-listed. A
// hand-written list is a claim of exhaustiveness that goes stale the next time
// somebody adds a menu item; walking the window is exhaustive by construction,
// and the suite additionally pins the harvest's CONTENTS so a new claimant is
// a loud test failure rather than a silent shadow.

#include <QString>
#include <vector>

#include "debug/debug_keymap.h"

class QWidget;

/// One combination a host window already answers to.
struct HostChord {
    jnext::dbgkeys::Combo combo;
    QString               label;   ///< the action's user-visible text, '&' stripped
};

/// Every QAction shortcut under `w` that is also a combination the debugger
/// keymap grammar could express and `validate_combo()` would accept — i.e.
/// exactly the ones a user could collide with. Duplicates are collapsed, first
/// label wins.
///
/// DISABLED ACTIONS ARE INCLUDED, and that is not an oversight. Qt does not
/// dispatch to a disabled action, so it shadows nothing *at this instant* — but
/// the question here is what the window BINDS, not what it happens to dispatch
/// while the dialog is open. "Stop MPEG4 Recording" is disabled until a
/// recording starts; skipping it meant a user could bind Ctrl+F6 in silence and
/// then lose it the first time they recorded anything. Found by measurement:
/// the enumeration row came back with three entries instead of four.
std::vector<HostChord> harvest_host_chords(const QWidget* w);

/// The entry whose combo is `c`, or nullptr.
const HostChord* find_host_chord(const std::vector<HostChord>& chords,
                                 const jnext::dbgkeys::Combo& c);
