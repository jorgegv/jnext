# GH #1 — Redefinable debugger keys

> Status: implemented. This document is the design record for the mechanism,
> written before the code and corrected against the running product.

## 1. What the issue actually asks for

The reporter ([#1](https://github.com/jorgegv/jnext/issues/1)) asked for jnext's
debugger keys to be changed to the blueMSX / Spectaculator convention:

| Reporter's request | What jnext v1.0.30 actually ships |
|---|---|
| F10 — Step Over        | **F7** |
| F11 — Step Into        | **F6** |
| Shift+F1 — Step Out    | **F8** |
| Ctrl/Shift+F10 — Run to Cursor | **Enter** in the disassembly panel; no window key |
| F5 — Continue          | **F5** ✔ |

So exactly one of the five already matched. That was measured by reading
`src/debugger/debugger_window.cpp`, not by trusting any document — the earlier
briefing for this work asserted "F10 Step Over, F11 Step Into, Shift+F11 Step
Out" was already what jnext shipped, and it is not.

The owner's comment supersedes the body: instead of moving the defaults, make
the keys **redefinable**, persist **only redefinitions**, and offer the
customisation under Settings.

**No default binding is changed by this work.** A user who wants the reporter's
layout configures it; a user who wants today's layout does nothing.

## 2. The action inventory

Twelve "virtual" actions, taken from the debugger window's own Debug menu and
bottom toolbar. The id is the config-file key and is a **stable public name**:
renaming one silently orphans every existing user binding, so a rename must
come with a migration, not just a new spelling.

| id | Menu / toolbar label | Default |
|---|---|---|
| `run`           | Run / Continue             | `F5` |
| `pause`         | Pause / Break              | `F9` |
| `step_into`     | Single Step                | `F6` |
| `step_over`     | Step Over                  | `F7` |
| `step_out`      | Step Out                   | `F8` |
| `step_back`     | Step Back                  | `Shift+F7` |
| `frame_back`    | Frame Back                 | `Shift+F6` |
| `run_to_cursor` | Run to Cursor              | *(unbound)* |
| `run_to_eof`    | Run to End of Frame        | *(unbound)* |
| `run_to_eosl`   | Run to End of Scan Line    | *(unbound)* |
| `trace_toggle`  | Enable / disable the trace | `F2` |
| `trace_export`  | Export Trace…              | `F3` |

Three actions are **unbound by default and stay that way**. `run_to_eof` and
`run_to_eosl` are toolbar + menu items that have never had a key.
`run_to_cursor` is new as a *window* action — the disassembly panel's Enter key
and its "Run to Here" context entry are unchanged — and exists precisely because
the reporter asked for a bindable run-to-cursor. Binding a default to any of the
three would be changing a default, which this work does not do.

**Deliberately excluded**, and why:

* **Toggle Breakpoint** — reachable from the gutter and the context menu. The
  reporter did not ask for it, and the project's scope rule says not to add
  surface that was not asked for. It is one table row away if it is ever wanted.
* **Ctrl+C / Ctrl+A** in the disassembly panel (Copy / Select All, GH #21) —
  standard platform editing chords, scoped to the focused widget rather than the
  window. They are *reserved* (see §5) rather than bindable.
* **Panel navigation** — the memory panel's arrows / PageUp / Home / hex digits,
  the disassembly panel's arrows / Enter. These are content navigation inside a
  focused widget, not window commands.
* **Menu-bar mnemonics** (Alt+D, Alt+M, Alt+B, Alt+W, Alt+N) — Qt derives them
  from the menu titles; they are not `QKeySequence`s and cannot be rebound
  without renaming the menus.
* **Main-window hotkeys** (Alt+O, Alt+S, F11 fullscreen, …) — a different
  window and a different issue.

## 3. The grammar

Config section `[debugger_keys]`, one key per redefined action:

```ini
[debugger_keys]
step_over=F10
step_into=F11
step_out=Shift+F11
run_to_cursor=Ctrl+F10
```

```
combination := "none" | [modifier "+"]... key
modifier    := "Ctrl" | "Alt" | "Shift" | "Meta"
key         := F1..F12
             | A..Z | 0..9
             | Space Tab Return Backspace Escape Insert Delete
             | Home End PageUp PageDown Up Down Left Right
```

* Parsing is **case-insensitive** and tolerates spaces around the `+`.
* Rendering is canonical: modifiers always in the order `Ctrl+Alt+Shift+Meta`,
  key in the spelling above. `render(parse(x)) == render(parse(render(parse(x))))`
  for every accepted `x`.
* `none` (and an empty value) means **unbound** — a first-class state, not an
  error.
* The vocabulary is **bounded on purpose.** It is a fixed list this project can
  test and document, rather than a mirror of Qt's ~1000-value `Qt::Key` enum
  that would rot the moment Qt adds one. Anything outside it is rejected by
  name.

## 4. Unparseable, unknown, conflicting — what happens

Nothing is ever silently ignored.

* **Unparseable value** (`step_over=Ctrl+`, `run=F13`): rejected, the action
  keeps its **default**, and an error naming the action, the text and the reason
  is logged on the `gui` channel at startup *and* listed at the top of the
  Preferences ▸ Debugger Keys tab.
* **Illegal binding** (see §5): same treatment.
* **Unknown action id** (`step_ovr=F7`, or an id from a future jnext): rejected
  and reported the same way, and **preserved verbatim** so that opening
  Preferences in an older jnext does not delete a newer jnext's binding. The
  cost is that a genuine typo is preserved too — which is why it is reported
  every single startup rather than kept quietly.
* **Conflict** (two actions on one combination): resolved deterministically —
  an explicit override outranks a default, and among explicit overrides the one
  earlier in the §2 table wins. The loser is left **unbound** (not silently
  re-defaulted, which could conflict again) and the pair is reported.

  This is not hypothetical politeness. Qt classifies two identical sequences as
  AMBIGUOUS and dispatches them **round-robin**, so both bindings half-work and
  neither can be used on purpose. That defect shipped in this debugger five
  times and is what GH #124 and `debugger_accel_test` exist for.

  The Preferences tab therefore **refuses a conflict outright**: the offending
  row is marked, the conflict is named, and OK/Apply are disabled until it is
  cleared. Load-time resolution only ever runs against a hand-edited file.

## 5. Which combinations are refused as bindings

| Rule | Reason |
|---|---|
| A binding must name exactly one non-modifier key | `Ctrl` alone is not a shortcut |
| No Ctrl/Alt/Meta ⇒ the key must be **F1..F12** | A bare letter, digit, arrow, `Home`, `Return`… would be consumed by Qt's shortcut map *before* the focused panel sees it, breaking the memory panel's hex typing, the disassembly address box and every panel's navigation. `Shift+F6` is fine — the key is still an F-key |
| `Alt+<letter>` (alone or with Shift) refused | The debugger's menu bar owns the window-wide Alt+letter namespace. A collision there is the GH #124 round-robin defect |
| `Ctrl+C`, `Ctrl+A` refused | Reserved by the disassembly panel for Copy / Select All (GH #21) |

Two things are explicitly **allowed**, and both need saying because the project's
main-window rules say the opposite:

* **Ctrl is free here.** In the emulator window Ctrl is the guest's SYMBOL SHIFT,
  which is why host hotkeys there live on Alt. The debugger is a separate
  `QMainWindow` with no key handler feeding `Keyboard::set_key()`, so nothing
  typed in it reaches the guest.
* **F11 is free here.** F11 is the *emulator window's* fullscreen toggle. A
  separate top-level window has its own shortcut map, so F11 pressed in the
  debugger never reaches the emulator window. This is what makes the reporter's
  "F11 = Step Into" possible at all.

## 6. The emulator window forwards five of them

`MainWindow::keyPressEvent` forwards `run`, `pause`, `step_into`, `step_over`
and `step_out` to the debugger whenever the debugger is enabled, so the step
keys work without leaving the emulator window (GH #223). That forwarding is
driven from the **same keymap**, otherwise a rebind would half-apply: the new
key would work in the debugger and the old one would keep working — and keep
being swallowed — in the emulator window.

Two consequences, documented rather than prevented:

* The forwarding now matches **modifiers exactly**. It previously switched on
  the key alone, so `Shift+F7` in the emulator window triggered Step Over. It no
  longer does, which is what lets `Shift+F7` mean Step Back.

  The consequence is that a *modified* F-key now falls through to the emulator
  window's own handling, exactly as it does when the debugger is off:
  `Shift+F9` fires the Multiface NMI rather than pausing, and `Shift+F5` /
  `Shift+F6` / `Shift+F7` / `Shift+F8` drive the `EmuFnKeys` FSM. (`Shift+F4`
  already did nothing — that case tests the modifiers itself.) This is a
  deliberate consequence of exact matching rather than a separate decision:
  the debugger claims the combinations it is bound to and nothing else.
* Binding one of those five to a combination the emulator window uses itself
  (F1 hard reset, F4 soft reset, F10 DivMMC, F11 fullscreen, F2 scale) shadows
  it while the debugger is enabled. This already happened before this work —
  `pause` on F9 shadows the Multiface NMI hotkey — and is left as the user's
  choice, reversible from Preferences.

The other seven actions are **not** forwarded, exactly as today. `trace_toggle`
defaults to F2, which is the emulator window's scale cycler; forwarding it would
break that.

## 7. Persistence — only redefinitions

`AppConfig::save()` removes the `[debugger_keys]` group and then writes only the
actions whose effective combination differs from the compiled-in default (plus
the preserved unknowns of §4). A machine that never redefines anything has **no
`[debugger_keys]` section at all**, so:

* a fresh config file does not gain twelve lines of restated defaults, and
* a default this project changes later reaches every user who never overrode it.

An action left unbound by conflict resolution is written back as `none`, because
that is what the Preferences table is showing the user. The invalid text that
caused it is not preserved; it was reported loudly at load.

## 8. Reset

* **Per action** — a `Reset` button on each row of the Preferences table, which
  restores that action's compiled-in default (including "unbound" for the three
  that default to it).
* **Globally** — `Reset All to Defaults` under the table.

Both edit the dialog's in-memory copy; nothing is written until OK or Apply, and
Cancel discards.

## 9. Where the code lives

| Piece | File | Library |
|---|---|---|
| Action table, grammar, validation, conflict resolution | `src/debug/debug_keymap.{h,cpp}` | `jnext_debug` — pure C++, **no Qt** |
| Persistence | `src/gui/app_config.{h,cpp}` | `jnext_gui` |
| Customisation UI | `src/gui/preferences_dialog.{h,cpp}`, `src/gui/shortcut_capture_button.{h,cpp}` | `jnext_gui` |
| Applying it to the real actions | `src/debugger/debugger_window.{h,cpp}` | `jnext_debugger` |
| Emulator-window forwarding | `src/gui/main_window.cpp` | `jnext_gui` |

The model is Qt-free and lives in the one library every configuration builds,
because the four supported build combinations are
`ENABLE_QT_UI` × `ENABLE_DEBUGGER` and two of them have only one side of this:

* `QT_UI=ON, DEBUGGER=OFF` — the config layer must still **round-trip**
  `[debugger_keys]`, or opening Preferences in a debugger-less build would wipe
  a user's bindings. That is the GH #25 hazard `app_config.h` already documents,
  and it is the reason `PreferencesDialog` carries the keymap through `collect()`
  even when its tab is not compiled in.
* `QT_UI=OFF, DEBUGGER=ON` — `jnext_debugger` must not reach into `jnext_gui`.

`src/debug/` satisfies both: `jnext_debug` is always built and depends on
neither.
