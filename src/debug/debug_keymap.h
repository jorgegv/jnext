#pragma once

// GH #1 — redefinable debugger keys.
//
// The MODEL: the action inventory, the config-file grammar, what is a legal
// binding and what happens to a bad one. Deliberately Qt-free and deliberately
// in jnext_debug, which every one of the four ENABLE_QT_UI x ENABLE_DEBUGGER
// build combinations builds:
//
//   * QT_UI=ON, DEBUGGER=OFF must still ROUND-TRIP [debugger_keys] through
//     AppConfig, or pressing OK in a debugger-less build's Preferences would
//     delete a user's bindings (the GH #25 hazard app_config.h documents);
//   * QT_UI=OFF, DEBUGGER=ON must not make jnext_debugger reach into jnext_gui.
//
// Design record: doc/design/GH1-DEBUGGER-KEYMAP-DESIGN.md.

#include <cstdint>
#include <string>
#include <vector>

namespace jnext::dbgkeys {

/// The twelve "virtual" debugger commands a key can be bound to.
///
/// The ORDER is load-bearing twice over: it is the order the Preferences table
/// lists, and it is the tie-break when two actions want the same combination
/// (see resolve_conflicts()).
enum class Action : uint8_t {
    Run = 0,
    Pause,
    StepInto,
    StepOver,
    StepOut,
    StepBack,
    FrameBack,
    RunToCursor,
    RunToEof,
    RunToEosl,
    TraceToggle,
    TraceExport,
};

inline constexpr int ACTION_COUNT = 12;

/// Modifier bitmask. NOT Qt's values — this header must not know about Qt.
enum Mod : uint8_t {
    MOD_NONE  = 0,
    MOD_SHIFT = 1 << 0,
    MOD_CTRL  = 1 << 1,
    MOD_ALT   = 1 << 2,
    MOD_META  = 1 << 3,
};

/// The bounded key vocabulary. A fixed list this project can test and document,
/// rather than a mirror of Qt::Key that would rot the moment Qt adds a value.
/// Anything outside it is rejected BY NAME at parse time.
enum class Key : uint8_t {
    None = 0,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Tab, Return, Backspace, Escape,
    Insert, Delete, Home, End, PageUp, PageDown,
    Up, Down, Left, Right,
};

/// One key combination. `key == Key::None` means UNBOUND — a first-class state,
/// not an error: three actions ship unbound.
struct Combo {
    uint8_t mods = MOD_NONE;
    Key     key  = Key::None;

    bool bound() const { return key != Key::None; }
    bool operator==(const Combo& o) const { return mods == o.mods && key == o.key; }
    bool operator!=(const Combo& o) const { return !(*this == o); }
};

/// Static description of one action.
struct ActionInfo {
    Action      action;
    const char* id;       ///< config-file key. A STABLE PUBLIC NAME — see the design doc.
    const char* label;    ///< English label shown in Preferences
    Combo       def;      ///< compiled-in default
};

/// The inventory, indexed by `static_cast<int>(Action)`. ACTION_COUNT entries.
const ActionInfo* action_table();

/// `nullptr` when `id` is not a known action.
const ActionInfo* find_action(const std::string& id);

inline const ActionInfo& info(Action a) { return action_table()[static_cast<int>(a)]; }

// --- grammar -------------------------------------------------------------

/// Canonical text for a combination: modifiers in the fixed order
/// Ctrl+Alt+Shift+Meta, then the key. Unbound renders as "none".
std::string render_combo(const Combo& c);

/// Parse `text` (case-insensitive, spaces around '+' tolerated).
/// "none" and "" both yield an unbound Combo and return true.
/// On failure returns false and sets `why` to a one-line reason.
bool parse_combo(const std::string& text, Combo& out, std::string& why);

/// Is this a combination a user is ALLOWED to bind? An unbound Combo is legal.
/// On refusal returns false and sets `why`. See the design doc §5 for each rule.
bool validate_combo(const Combo& c, std::string& why);

// --- the map -------------------------------------------------------------

/// One thing that went wrong while reading `[debugger_keys]`. Reported on the
/// log at error level AND listed in the Preferences tab; never swallowed.
struct LoadIssue {
    std::string action_id;   ///< the key as written in the file
    std::string text;        ///< the value as written in the file ("" for a conflict)
    std::string reason;      ///< human-readable, already complete as a sentence
};

/// The effective binding of every action, plus the entries that were kept
/// verbatim because this build did not recognise them.
class Keymap {
public:
    Keymap();   ///< every action at its compiled-in default

    const Combo& combo(Action a) const { return combos_[static_cast<int>(a)]; }
    void set(Action a, const Combo& c) { combos_[static_cast<int>(a)] = c; }

    bool is_default(Action a) const { return combo(a) == info(a).def; }
    void reset(Action a) { set(a, info(a).def); }
    void reset_all();

    /// The action bound to `c`, or `nullptr`. Never matches an unbound `c`.
    const ActionInfo* action_for(const Combo& c) const;

    /// Entries whose action id this build does not know, preserved so that
    /// running an OLDER jnext does not delete a NEWER one's binding.
    const std::vector<std::pair<std::string, std::string>>& unknown_entries() const {
        return unknown_;
    }
    void add_unknown_entry(const std::string& id, const std::string& text) {
        unknown_.emplace_back(id, text);
    }

    bool operator==(const Keymap& o) const;
    bool operator!=(const Keymap& o) const { return !(*this == o); }

private:
    Combo combos_[ACTION_COUNT];
    std::vector<std::pair<std::string, std::string>> unknown_;
};

/// Build the effective keymap from the `[debugger_keys]` entries of a config
/// file. Every rejection is appended to `issues` — nothing is dropped quietly.
///
/// Rules, in order:
///   1. start from the compiled-in defaults;
///   2. an unparseable, illegal or unknown entry is REPORTED and the action
///      keeps its default (an unknown id is additionally preserved);
///   3. accepted overrides are applied;
///   4. conflicts are resolved: an explicit override outranks a default, and
///      among explicit overrides the action earlier in Action order wins. The
///      loser is left UNBOUND and reported.
Keymap build_keymap(const std::vector<std::pair<std::string, std::string>>& entries,
                    std::vector<LoadIssue>& issues);

/// Conflict pass on an already-built map, used by the UI to describe a clash
/// before the user can commit it. Returns the pairs that collide, loser first.
struct Conflict {
    Action loser;
    Action winner;
    Combo  combo;
};
std::vector<Conflict> find_conflicts(const Keymap& map);

} // namespace jnext::dbgkeys
