#include "gui/host_chords.h"

#include "debug/debug_keymap_qt.h"

#include <QAction>
#include <QKeySequence>
#include <QWidget>

using namespace jnext::dbgkeys;

std::vector<HostChord> harvest_host_chords(const QWidget* w) {
    std::vector<HostChord> out;
    if (!w) return out;

    for (QAction* a : w->findChildren<QAction*>()) {
        // Deliberately NOT gated on isEnabled() — see the header.
        // shortcuts(), not shortcut(): an action may carry alternates, and
        // reading only the primary is how debugger_accel_test's DACC-03
        // mutation slipped past an earlier harvest.
        for (const QKeySequence& seq : a->shortcuts()) {
            if (seq.isEmpty()) continue;
            Combo c;
            std::string why;
            // Qt's portable text is the same vocabulary parse_combo() speaks
            // for everything a user could bind. Anything it cannot express is
            // by definition not a collision, so it is skipped rather than
            // guessed at.
            if (!parse_combo(seq.toString().toStdString(), c, why)) continue;
            if (!validate_combo(c, why)) continue;   // the user could not bind it either
            if (find_host_chord(out, c)) continue;   // first claimant wins the label
            out.push_back(HostChord{c, a->text().remove(QLatin1Char('&'))});
        }
    }
    return out;
}

const HostChord* find_host_chord(const std::vector<HostChord>& chords, const Combo& c) {
    if (!c.bound()) return nullptr;
    for (const HostChord& h : chords)
        if (h.combo == c) return &h;
    return nullptr;
}
