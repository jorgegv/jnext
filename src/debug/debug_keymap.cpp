#include "debug/debug_keymap.h"

#include <algorithm>
#include <cctype>

namespace jnext::dbgkeys {

namespace {

constexpr Combo bare(Key k)        { return Combo{MOD_NONE, k}; }
constexpr Combo with(uint8_t m, Key k) { return Combo{m, k}; }
constexpr Combo unbound()          { return Combo{MOD_NONE, Key::None}; }

// The inventory. Order = Preferences listing order = conflict tie-break order.
//
// The defaults are MEASURED from debugger_window.cpp's create_menus(), not
// copied from any document: the design plan and an earlier briefing both had
// them wrong (they claimed F10/F11/Shift+F11 for the step family).
const ActionInfo kActions[ACTION_COUNT] = {
    { Action::Run,         "run",           "Run / Continue",          bare(Key::F5)              },
    { Action::Pause,       "pause",         "Pause / Break",           bare(Key::F9)              },
    { Action::StepInto,    "step_into",     "Single Step",             bare(Key::F6)              },
    { Action::StepOver,    "step_over",     "Step Over",               bare(Key::F7)              },
    { Action::StepOut,     "step_out",      "Step Out",                bare(Key::F8)              },
    { Action::StepBack,    "step_back",     "Step Back",               with(MOD_SHIFT, Key::F7)   },
    { Action::FrameBack,   "frame_back",    "Frame Back",              with(MOD_SHIFT, Key::F6)   },
    { Action::RunToCursor, "run_to_cursor", "Run to Cursor",           unbound()                  },
    { Action::RunToEof,    "run_to_eof",    "Run to End of Frame",     unbound()                  },
    { Action::RunToEosl,   "run_to_eosl",   "Run to End of Scan Line", unbound()                  },
    { Action::TraceToggle, "trace_toggle",  "Enable / Disable Trace",  bare(Key::F2)              },
    { Action::TraceExport, "trace_export",  "Export Trace...",         bare(Key::F3)              },
};

struct KeyName { Key key; const char* name; };

// The whole vocabulary, in the spelling render_combo() emits and parse_combo()
// accepts case-insensitively. Anything not in here is rejected by name.
const KeyName kKeyNames[] = {
    { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" },
    { Key::F5, "F5" }, { Key::F6, "F6" }, { Key::F7, "F7" }, { Key::F8, "F8" },
    { Key::F9, "F9" }, { Key::F10, "F10" }, { Key::F11, "F11" }, { Key::F12, "F12" },
    { Key::A, "A" }, { Key::B, "B" }, { Key::C, "C" }, { Key::D, "D" },
    { Key::E, "E" }, { Key::F, "F" }, { Key::G, "G" }, { Key::H, "H" },
    { Key::I, "I" }, { Key::J, "J" }, { Key::K, "K" }, { Key::L, "L" },
    { Key::M, "M" }, { Key::N, "N" }, { Key::O, "O" }, { Key::P, "P" },
    { Key::Q, "Q" }, { Key::R, "R" }, { Key::S, "S" }, { Key::T, "T" },
    { Key::U, "U" }, { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" },
    { Key::Y, "Y" }, { Key::Z, "Z" },
    { Key::Num0, "0" }, { Key::Num1, "1" }, { Key::Num2, "2" }, { Key::Num3, "3" },
    { Key::Num4, "4" }, { Key::Num5, "5" }, { Key::Num6, "6" }, { Key::Num7, "7" },
    { Key::Num8, "8" }, { Key::Num9, "9" },
    { Key::Space, "Space" }, { Key::Tab, "Tab" }, { Key::Return, "Return" },
    { Key::Backspace, "Backspace" }, { Key::Escape, "Escape" },
    { Key::Insert, "Insert" }, { Key::Delete, "Delete" },
    { Key::Home, "Home" }, { Key::End, "End" },
    { Key::PageUp, "PageUp" }, { Key::PageDown, "PageDown" },
    { Key::Up, "Up" }, { Key::Down, "Down" }, { Key::Left, "Left" }, { Key::Right, "Right" },
};

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

const char* key_name(Key k) {
    for (const KeyName& kn : kKeyNames)
        if (kn.key == k) return kn.name;
    return nullptr;
}

bool key_from_name(const std::string& lowered, Key& out) {
    for (const KeyName& kn : kKeyNames) {
        if (lower(kn.name) == lowered) { out = kn.key; return true; }
    }
    return false;
}

bool is_function_key(Key k) { return k >= Key::F1 && k <= Key::F12; }
bool is_letter(Key k)       { return k >= Key::A && k <= Key::Z; }

} // namespace

const ActionInfo* action_table() { return kActions; }

const ActionInfo* find_action(const std::string& id) {
    for (const ActionInfo& a : kActions)
        if (id == a.id) return &a;
    return nullptr;
}

// --- grammar -------------------------------------------------------------

std::string render_combo(const Combo& c) {
    if (!c.bound()) return "none";
    std::string out;
    // Fixed order, so a round-trip is stable regardless of how the user typed it.
    if (c.mods & MOD_CTRL)  out += "Ctrl+";
    if (c.mods & MOD_ALT)   out += "Alt+";
    if (c.mods & MOD_SHIFT) out += "Shift+";
    if (c.mods & MOD_META)  out += "Meta+";
    const char* kn = key_name(c.key);
    out += kn ? kn : "?";
    return out;
}

bool parse_combo(const std::string& text, Combo& out, std::string& why) {
    const std::string t = trim(text);
    if (t.empty() || lower(t) == "none") { out = Combo{}; return true; }

    Combo c;
    size_t pos = 0;
    // Split on '+'. The last token is the key; everything before it a modifier.
    std::vector<std::string> tokens;
    while (true) {
        const size_t plus = t.find('+', pos);
        if (plus == std::string::npos) { tokens.push_back(trim(t.substr(pos))); break; }
        tokens.push_back(trim(t.substr(pos, plus - pos)));
        pos = plus + 1;
    }

    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string m = lower(tokens[i]);
        if      (m == "ctrl"  || m == "control") c.mods |= MOD_CTRL;
        else if (m == "alt")                     c.mods |= MOD_ALT;
        else if (m == "shift")                   c.mods |= MOD_SHIFT;
        else if (m == "meta"  || m == "super" || m == "win") c.mods |= MOD_META;
        else {
            why = "'" + tokens[i] + "' is not a modifier (Ctrl, Alt, Shift, Meta)";
            return false;
        }
    }

    const std::string last = tokens.back();
    if (last.empty()) {
        why = "no key after the last '+'";
        return false;
    }
    Key k;
    if (!key_from_name(lower(last), k)) {
        why = "'" + last + "' is not a key jnext can bind";
        return false;
    }
    c.key = k;
    out = c;
    return true;
}

bool validate_combo(const Combo& c, std::string& why) {
    if (!c.bound()) return true;   // deliberately unbound is always legal

    const bool has_hard_mod = (c.mods & (MOD_CTRL | MOD_ALT | MOD_META)) != 0;

    // A window-wide shortcut is consumed by Qt's shortcut map BEFORE the
    // focused panel sees the key, so a bare letter/digit/arrow/Home/Return
    // would break the memory panel's hex typing, the disassembly address box
    // and the panels' navigation. Function keys are safe: no panel types one.
    //
    // SCOPE, because the sentence above is easy to read as more than it is:
    // this protects the BARE forms only. A navigation key WITH Ctrl/Alt/Meta
    // is not restricted, so `Ctrl+Up` is bindable and would shadow a panel
    // that gave Ctrl+Up its own meaning. Today none does — both panels handle
    // the arrows, Home/End and PageUp/PageDown identically with or without
    // Ctrl, and the only modified chords either one claims are Ctrl+C/Ctrl+A,
    // reserved below. An ACCEPTED BOUND, not an oversight: restricting the
    // modified forms too would cost most of the usable namespace to defend
    // behaviour that does not exist. A panel that later gives a modified
    // navigation key its own meaning must reserve it here, the way GH #21 did.
    if (!has_hard_mod && !is_function_key(c.key)) {
        why = "needs Ctrl, Alt or Meta — only F1-F12 may be bound on their own, "
              "because a bare key is taken from the panel that has focus";
        return false;
    }

    // Alt+<letter> is the debugger menu bar's own namespace (Alt+D Debug,
    // Alt+M Map, Alt+B Breakpoints, Alt+W Watches, Alt+N Window). Two claimants
    // there make an AMBIGUOUS Qt shortcut, which fires them round-robin — the
    // GH #124 defect this project has already shipped five times.
    const uint8_t hard = c.mods & (MOD_CTRL | MOD_ALT | MOD_META);
    if (hard == MOD_ALT && is_letter(c.key)) {
        why = "Alt+letter belongs to the debugger's menu bar";
        return false;
    }

    // GH #21: the disassembly panel's Copy / Select All.
    if (c.mods == MOD_CTRL && (c.key == Key::C || c.key == Key::A)) {
        why = "reserved by the disassembly panel for Copy / Select All";
        return false;
    }

    return true;
}

// --- Keymap ---------------------------------------------------------------

Keymap::Keymap() {
    for (int i = 0; i < ACTION_COUNT; ++i) combos_[i] = kActions[i].def;
}

void Keymap::reset_all() {
    for (int i = 0; i < ACTION_COUNT; ++i) combos_[i] = kActions[i].def;
}

const ActionInfo* Keymap::action_for(const Combo& c) const {
    if (!c.bound()) return nullptr;
    for (int i = 0; i < ACTION_COUNT; ++i)
        if (combos_[i] == c) return &kActions[i];
    return nullptr;
}

bool Keymap::operator==(const Keymap& o) const {
    for (int i = 0; i < ACTION_COUNT; ++i)
        if (combos_[i] != o.combos_[i]) return false;
    return unknown_ == o.unknown_;
}

std::vector<Conflict> find_conflicts(const Keymap& map) {
    std::vector<Conflict> out;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const Combo& ci = map.combo(static_cast<Action>(i));
        if (!ci.bound()) continue;
        for (int j = 0; j < i; ++j) {
            if (map.combo(static_cast<Action>(j)) == ci) {
                out.push_back(Conflict{static_cast<Action>(i), static_cast<Action>(j), ci});
                break;   // one report per loser
            }
        }
    }
    return out;
}

Keymap build_keymap(const std::vector<std::pair<std::string, std::string>>& entries,
                    std::vector<LoadIssue>& issues) {
    Keymap map;
    bool explicit_set[ACTION_COUNT] = {};

    for (const auto& e : entries) {
        const ActionInfo* a = find_action(e.first);
        if (!a) {
            // Preserved, not dropped: an id this build does not know may be a
            // NEWER jnext's binding, and silently deleting it would make an
            // older jnext destructive. A genuine typo is preserved too, which
            // is exactly why it is reported on every startup.
            issues.push_back(LoadIssue{e.first, e.second,
                "unknown debugger action; the entry is kept but does nothing"});
            map.add_unknown_entry(e.first, e.second);
            continue;
        }

        Combo c;
        std::string why;
        if (!parse_combo(e.second, c, why)) {
            issues.push_back(LoadIssue{e.first, e.second,
                why + "; keeping the default " + render_combo(a->def)});
            continue;
        }
        if (!validate_combo(c, why)) {
            issues.push_back(LoadIssue{e.first, e.second,
                why + "; keeping the default " + render_combo(a->def)});
            continue;
        }

        if (explicit_set[static_cast<int>(a->action)]) {
            // QSettings cannot produce this (one value per key), but a caller
            // feeding entries from elsewhere can.
            issues.push_back(LoadIssue{e.first, e.second,
                "duplicate entry for this action; the first one is kept"});
            continue;
        }
        map.set(a->action, c);
        explicit_set[static_cast<int>(a->action)] = true;
    }

    // Conflict resolution. Qt dispatches two identical sequences ROUND-ROBIN
    // (GH #124), so a clash is not a cosmetic problem: both bindings half-work.
    // An explicit override outranks a default because the file is the user's
    // expressed intent and a default is not; among explicit overrides the
    // earlier action in Action order wins. The loser is left UNBOUND rather
    // than re-defaulted, because a re-default can collide all over again.
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const Action ai = static_cast<Action>(i);
        const Combo ci = map.combo(ai);
        if (!ci.bound()) continue;
        for (int j = 0; j < i; ++j) {
            const Action aj = static_cast<Action>(j);
            if (map.combo(aj) != ci) continue;

            // Both explicit, or both default: the earlier action keeps it.
            // One explicit and one default: the explicit one keeps it.
            Action loser  = ai;
            Action winner = aj;
            if (explicit_set[i] && !explicit_set[j]) { loser = aj; winner = ai; }

            map.set(loser, Combo{});
            issues.push_back(LoadIssue{
                std::string(info(loser).id), render_combo(ci),
                "already bound to '" + std::string(info(winner).id) +
                    "'; leaving '" + std::string(info(loser).id) + "' unbound"});
            break;
        }
    }

    return map;
}

} // namespace jnext::dbgkeys
