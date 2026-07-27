# Task 115 — Host Shortcut Inventory and Reserved-Key Proposal

> **Status: INVENTORY AND PROPOSAL ONLY. No GUI behaviour was changed by this
> document.** The reserved-shortcut list is a product decision for the project
> owner. This document supplies the evidence needed to make it.
>
> Scope: issue [#115](https://github.com/jorgegv/jnext/issues/115) part 2
> ("host shortcuts leak through — Ctrl+Q quits the emulator"). Part 1 (mapping
> fidelity) was audited and fixed separately in the same branch; see
> §7 for the one place the two parts touch.

---

## 1. The complaint, restated precisely

> Some combinations act on the application instead of reaching the guest —
> **Ctrl+Q QUITS THE EMULATOR**. A running program cannot see those keys, and
> an accidental Ctrl+Q loses the session.

On a ZX Spectrum, `Ctrl` is a shift key. After the part-1 fix it is **Symbol
Shift** (`keymaps.vhd:84`); before it, it was Caps Shift. Either way **every
`Ctrl+<letter>` chord is a legitimate guest keystroke**, so every `Ctrl+<letter>`
host shortcut steals a real Spectrum key. jnext currently binds six of them.

The codebase already knows this. `src/gui/main_window.cpp:612-616`, on the
mouse-capture menu item:

```
// Deliberately NO keyboard shortcut. Every plain Ctrl+<key> is a real ZX
// sequence — Ctrl maps to Symbol Shift (issue #115), so Ctrl+M IS SS+M —
// and a host shortcut would swallow it before the guest ever saw it.
// Capture is by clicking the viewport (or this menu item); Ctrl+Alt
// releases.
```

That reasoning was applied to exactly one menu item and to no other. The
inventory below is what happens when it is applied to all of them.

---

## 2. How key events actually flow (the load-bearing part)

This determines what any fix can and cannot do.

### 2.1 Focus

- `src/gui/main_window.cpp:177` — `setCentralWidget(emulator_widget_)`
- `src/gui/main_window.cpp:185` — `setFocusPolicy(Qt::StrongFocus)` on the window
- `src/gui/emulator_widget.cpp:8` — `EmulatorWidget`'s constructor **never** calls
  `setFocusPolicy`, so it keeps `QWidget`'s default `Qt::NoFocus`.

The viewport is not a focus target. Key events land on `MainWindow` and the
guest is fed from the *window's* handler via `handle_key()`
(`src/gui/main_window.cpp:1518-1535`).

### 2.2 Precedence, highest first

1. **Qt shortcut map** — `QAction::setShortcut`. Default context is
   `Qt::WindowShortcut`; `setShortcutContext` is called **nowhere** in `src/`.
   Qt delivers `QEvent::ShortcutOverride` then `QEvent::Shortcut` *before* the
   key ever arrives as a `KeyPress` at the focus widget.
2. **`QMenuBar` Alt+letter mnemonics** — same layer.
3. **`MainWindow::event()`** — `src/gui/main_window.cpp:1498-1517`. Intercepts
   `Tab`/`Backtab` only.
4. **`MainWindow::keyPressEvent` / `keyReleaseEvent`** —
   `src/gui/main_window.cpp:1276`, `:1449`.
5. **`handle_key()` → `qt_key_to_sdl()` → `key_callback_` → guest.**

### 2.3 There is no fall-through mechanism at all

Verified absent across `src/` (zero hits each):

| Mechanism | Present? |
|---|---|
| `QEvent::ShortcutOverride` handler | **No** |
| `QShortcut` objects | **No** |
| `grabKeyboard()` / `releaseKeyboard()` | **No** |
| `QApplication::notify()` subclass | **No** |
| Application-wide event filter | **No** |
| Any passthrough / "send next key to guest" toggle | **No** |

The only `installEventFilter` in the tree is
`src/debugger/debugger_manager.cpp:38`, and its filter (`:65-71`) handles only
`QEvent::Move` / `QEvent::Resize` for debugger-window sticky positioning. It
never sees a key event.

**Therefore every key carrying a `QAction` shortcut is unconditionally
swallowed, and nothing in the codebase can currently override that.**

### 2.4 The precedence already creates dead code

Because shortcuts outrank `keyPressEvent`, some `keyPressEvent` cases are
unreachable. The source says so at `src/gui/main_window.cpp:1346-1349`:

```
// Note (#45): plain F4 is normally
// consumed by the Machine > Soft Reset QAction shortcut before it
// reaches this handler (same situation as F11/fullscreen_action_),
// so the F4 case below only sees modified presses (Shift/Ctrl+F4).
```

- **F4** — `soft_reset` QAction (`:481`) wins; `case Qt::Key_F4` (`:1358`) is dead for plain F4.
- **F11** — `fullscreen_action_` (`:681`) wins; `case Qt::Key_F11` (`:1330`) is dead.

---

## 3. Inventory — Qt main window

### 3.1 `QAction` shortcuts (12). All swallowed; guest never sees them.

| # | Key | file:line | Action | Guest collision |
|---|-----|-----------|--------|-----------------|
| 1 | `Ctrl+O` | `main_window.cpp:368` | Load NEX dialog | **Yes** — SS+O is `;` |
| 2 | `Ctrl+F5` | `main_window.cpp:377` | Start MPEG4 recording | No (F5 reserved anyway) |
| 3 | `Ctrl+F6` | `main_window.cpp:395` | Stop MPEG4 recording | No |
| 4 | `Ctrl+S` | `main_window.cpp:429` | Save Screenshot dialog | **Yes** — SS+S is `\|` |
| 5 | `Ctrl+Shift+S` | `main_window.cpp:459` | Save Snapshot | **Yes** — CS+SS+S |
| 6 | **`Ctrl+Q`** | `main_window.cpp:465` | **`QApplication::quit`** | **Yes** — SS+Q is `<=` |
| 7 | `Ctrl+R` | `main_window.cpp:477` | Power Reset (cold boot) | **Yes** — SS+R is `<` |
| 8 | `F4` | `main_window.cpp:481` | Soft Reset | No |
| 9 | `Ctrl+T` | `main_window.cpp:627` | Open Tape File | **Yes** — SS+T is `>` |
| 10 | `F11` | `main_window.cpp:681` | Fullscreen (checkable) | No |
| 11 | `Ctrl+D` | `main_window.cpp:696` | Toggle debugger (`#ifdef ENABLE_DEBUGGER`) | **Yes** — SS+D is `STEP` |
| 12 | `QKeySequence::Preferences` | `main_window.cpp:704` | Preferences dialog | See note |

**Ctrl+Q, verbatim** (`main_window.cpp:464-466`):

```cpp
QAction* quit = file_menu->addAction(tr("&Quit"));
quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
connect(quit, &QAction::triggered, qApp, &QApplication::quit);
```

Defined once. **Nothing guards it** — no confirmation, and because it goes
straight to `QApplication::quit()` it does **not** route through
`MainWindow::closeEvent` (declared `main_window.h:159`). One slip and the
session is gone.

**Note on #12 (`QKeySequence::Preferences`).** This is a `StandardKey`, resolved
by `QPlatformTheme::keyBindings()`. In Qt's standard-key table `Preferences` is
bound only under the macOS keyboard scheme (Cmd+`,`); on Linux/X11 and Windows
it resolves to an empty sequence and consumes no host key. **This is inferred
from Qt's documented standard-key semantics, not from jnext source, and was not
empirically confirmed on this host** — worth a 30-second check before acting on
it. If it *is* live on macOS it collides with the guest's `,` key (SS+N).

### 3.2 Top-level menu mnemonics → global Alt+letter (8)

Mechanism: `menuBar()->addMenu(tr("&X"))`. Qt makes a top-level menubar mnemonic
an Alt+letter accelerator whenever the window is active. All swallowed.

| Alt key | Title | file:line |
|---|---|---|
| `Alt+F` | `&File` | `main_window.cpp:365` |
| `Alt+M` | `&Machine` | `main_window.cpp:469` |
| `Alt+I` | `&Input` | `main_window.cpp:580` |
| `Alt+T` | `&Tape` | `main_window.cpp:624` |
| `Alt+D` | `&Debug` | `main_window.cpp:646` |
| `Alt+V` | `&View` | `main_window.cpp:660` |
| `Alt+S` | `&Settings` | `main_window.cpp:701` |
| `Alt+H` | `&Help` | `main_window.cpp:708` |

**Collision risk with the guest's Alt chords.** `src/input/keyboard.cpp:165-171`
binds `Alt+E` → EDIT, `Alt+G` → GRAPH, `Alt+C` → CAPS LOCK, and `:163`
`Alt+`` ` `` → INV VIDEO. E, G and C happen not to be used by any top-level menu
— **that is luck, not design**. Adding a menu titled `&Edit`, `&Graphics` or
`&Config` would silently kill three guest keys. `keyboard.cpp:165-168` already
records that `Alt+D` was unusable for exactly this reason.

### 3.3 `keyPressEvent` interceptions (`main_window.cpp:1276-1447`)

All swallowed unless noted.

**Debugger-active only** (`#ifdef ENABLE_DEBUGGER`, guarded by
`debugger_mgr_->is_enabled()`):

| Key | file:line | Effect |
|---|---|---|
| `F5` | `:1301` | Run |
| `F6` | `:1306` | Step Into |
| `F7` | `:1311` | Step Over |
| `F8` | `:1316` | Step Out |
| `F9` | `:1321` | Pause |

**Unconditional:**

| Key | file:line | Effect | Live? |
|---|---|---|---|
| `F11` | `:1330` | toggle fullscreen | Dead — QAction `:681` wins |
| `F2` | `:1337` | cycle scale | **Live** |
| `F1` | `:1350` | hard reset | Live (only if no Shift/Ctrl) |
| `F4` | `:1358` | soft reset | Dead for plain F4 |
| `F9` | `:1366` | Multiface NMI (debugger off) | Live |
| `F10` | `:1377` | DivMMC NMI | Live |
| `F3` | `:1405` | EmuFnKeys 3 | Live |
| `F5` | `:1412` | EmuFnKeys 5 (expansion bus on) | Live |
| `F6` | `:1419` | EmuFnKeys 6 (expansion bus off) | Live |
| `F7` | `:1426` | EmuFnKeys 7 (scanline weight) | Live |
| `F8` | `:1433` | EmuFnKeys 8 (CPU speed) | Live |

**The one deliberate fall-through** — `main_window.cpp:1285-1296`:

```cpp
if (mouse_captured_ &&
    ((key == Qt::Key_Alt     && (modifiers & Qt::ControlModifier)) ||
     (key == Qt::Key_Control && (modifiers & Qt::AltModifier)))) {
    set_mouse_captured(false);
    // Deliberately NOT consumed: fall through to normal key handling.
```

Ctrl+Alt releases the pointer **and** still reaches the guest, because
swallowing the Alt key-down would desync `Keyboard::alt_held_`.

`Esc` deliberately has no case (`:1334-1336`) — Task 77 made it the ZX BREAK key.

### 3.4 `keyReleaseEvent` (`main_window.cpp:1449-1496`)

Consumes releases of F11/F2 (`:1453-1456`), F1/F4/F9/F10 (`:1459-1462`), and
F3/F5/F6/F7/F8 (`:1468-1481`, with a debugger-enabled exception at `:1485-1489`),
so hotkey releases never bleed into the ZX matrix. Auto-repeat is filtered at
`:1451` and again in `handle_key` at `:1520`.

### 3.5 `MainWindow::event()` — the Tab override (`main_window.cpp:1498-1517`)

```cpp
if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
    handle_key(ke, t == QEvent::KeyPress);
    return true;
}
```

The only place that pre-empts a Qt built-in. Without it, `QWidget::event()`
would consume Tab for focus traversal before `keyPressEvent`, leaving ZX EXTEND
MODE unreachable. Qt Tab traversal is therefore disabled outright in the main
window — acceptable because the focus chain is the viewport alone.

**This is the sole precedent for overriding a Qt built-in, and it is a
hard-coded per-key special case, not a general mechanism.**

### 3.6 Net F-key state in the Qt frontend

F1–F11 are **all** swallowed. **F12 is the only function key that reaches the
guest** (`main_window.cpp:138` → `SDL_SCANCODE_F12`), where it is a no-op
because `keyboard.cpp` binds no F-keys.

---

## 4. Inventory — Qt debugger window

### 4.1 `QAction` shortcuts (9)

Active only while the debugger window is the active window — which is why the
duplicate F5–F9 handling exists in `MainWindow::keyPressEvent` (§3.3).

| Key | file:line | Effect |
|---|---|---|
| `F5` | `debugger_window.cpp:401` | Run / Continue |
| `F9` | `debugger_window.cpp:405` | Pause / Break |
| `F6` | `debugger_window.cpp:411` | Single Step |
| `F7` | `debugger_window.cpp:415` | Step Over |
| `F8` | `debugger_window.cpp:419` | Step Out |
| `Shift+F6` | `debugger_window.cpp:423` | Frame Back |
| `Shift+F7` | `debugger_window.cpp:434` | Step Back |
| `F2` | `debugger_window.cpp:452` | Enable Trace (checkable) |
| `F3` | `debugger_window.cpp:469` | Export Trace… |

`Shift+F6` / `Shift+F7` are bound **only** here; in the main window they fall
through the modifier guards and reach the guest.

### 4.2 Debugger menu mnemonics (5)

| Alt key | Title | file:line |
|---|---|---|
| `Alt+D` | `&Debug` | `debugger_window.cpp:398` |
| `Alt+M` | `&Map` | `debugger_window.cpp:528` |
| `Alt+B` | `&Breakpoints` | `debugger_window.cpp:537` |
| `Alt+W` | `&Watches` | `debugger_window.cpp:564` |
| `Alt+W` | `&Window` | `debugger_window.cpp:573` |

> **Pre-existing defect, unrelated to #115 but found here:** `Alt+W` is bound
> **twice**. Qt resolves an ambiguous mnemonic by cycling focus between the
> candidates instead of activating either. Worth its own issue.

### 4.3 Debugger panel `keyPressEvent` overrides

Inside the debugger window only — these can never intercept a guest key.

- **`MemoryPanel`** (`memory_panel.cpp:423-524`, focus set at `:26`/`:410`):
  `PageUp` `:427`, `PageDown` `:434`, `Home` `:443`, `End` `:449`,
  `Left` `:461`, `Right` `:467`, `Up` `:473`, `Down` `:479`,
  `0`–`9`/`A`–`F` hex edit `:490-491`, `Esc` deselect `:516`; default falls to
  `QWidget::keyPressEvent` `:523`.
- **`DisasmPanel`** (`disasm_panel.cpp:429-528`, focus at `:72`):
  `Up` `:432`, `Down` `:450`, `Return`/`Enter` `:468-469`, `PageDown` `:476`,
  `PageUp` `:495`, `Home` `:509`, `End` `:517`; default falls through `:525-526`.

No other debugger panel overrides any key event.

### 4.4 Toolbars

Neither the main-window toolbar (`main_window.cpp:718-760`) nor the debugger
toolbar (`debugger_manager.cpp:242-245`, `debugger_window.cpp:275-296`) sets any
shortcut. Several button labels advertise F-keys ("F7: Step Over", NMI tooltip
"Multiface NMI (F9)") but the bindings live on the QActions above.

---

## 5. Inventory — SDL frontend (`src/platform/sdl_app.cpp:87-143`)

A flat `if`-chain on `SDL_KEYDOWN`/`SDL_KEYUP` from `sdl_input.cpp:12`. No
menus, no mnemonics, no shortcut map, no focus traversal.

**Intercepted on key-down:** `F11` (`:105`, fullscreen), `F2` (`:111`, scale),
`F1` (`:119`), `F4` (`:120`), `F9` (`:121`), `F10` (`:122`),
`F3`/`F5`/`F6`/`F7`/`F8` (`:125-129`, EmuFnKeys).
**Key-up consumed** for F1–F11 (`:131-140`).
**Ctrl+Alt** (`:95-103`) releases the pointer and is deliberately *not*
consumed. `Esc` explicitly not intercepted (`:109-110`).
Everything else is forwarded raw at `:142`.

> **Frontend divergence — significant.** `Ctrl+Q`, `Ctrl+O`, `Ctrl+S`, `Ctrl+R`,
> `Ctrl+T`, `Ctrl+D`, `Ctrl+Shift+S`, `Tab` and every `Alt+letter`
> **reach the guest under SDL but are swallowed under Qt.** The two frontends do
> not agree on what the guest can see. Any reserved list must be defined once
> and enforced in both.

**Headless** (`src/platform/headless_app.cpp`) has no interactive key handling at
all — only scripted injection at `:486`.

---

## 6. What the hardware itself reserves

This is the part worth leaning on, because it converts a taste question into a
spec question. The real Next reserves function keys and passes everything else
to the machine — `keymaps.vhd:72-79` and `:96-104`:

```
--            F1 = hard reset
--            F2 = toggle scandoubler, hdmi reset
--            F3 = toggle 50Hz / 60Hz display
--            F4 = soft reset
--            F5 = (temporary) expansion bus on
--            F6 = (temporary) expansion bus off
--            F7 = change scanline weight
--            F8 = change cpu speed
...
--    F9  = reserved (multiface nmi), left windows key
--    F10 = reserved (divmmc nmi), right windows key
--    F11 = expansion bus on
--    F12 = expansion bus off
--    PAUSE/BREAK = reserved (reset ps2 module)
```

Every one of these is a **Case-2 "Function Key"** entry in the keymap
(`keymaps.vhd:65-71`, bits 7 and 6 both set), which `ps2_keyb.vhd:202-209`
routes to `o_ps2_func_keys_n` — a *separate output* from the key matrix. They
never touch the membrane.

**Conclusions the hardware hands us:**

1. **F1–F12 reserved is correct and hardware-faithful.** jnext's existing F-key
   interception matches real hardware, and jnext's F1/F4/F9/F10 assignments
   match the hardware's meanings exactly.
2. **Pause/Break is reserved on hardware** and is currently unbound in jnext.
3. **The Windows keys are hardware aliases for F9/F10**, and are unbound in jnext.
4. **Nothing else is reserved.** The hardware has no equivalent of `Ctrl+Q`,
   `Ctrl+O`, `Ctrl+S`, `Ctrl+R`, `Ctrl+T`, `Ctrl+D` or `Alt+letter` — every one of
   those chords reaches the machine on a real Next.

---

## 7. Interaction with the part-1 mapping fix

The part-1 fix (same branch) corrects `Shift`/`Ctrl`, which had been inverted
against `keymaps.vhd:83-84`:

- `Shift` → **Caps Shift** (row 0 col 0) — was Symbol Shift
- `Ctrl` → **Symbol Shift** (row 7 col 1) — was Caps Shift

This does not change *which* keys are swallowed, but it changes *what the user
loses*: the six `Ctrl+<letter>` shortcuts now steal **Symbol Shift** chords —
i.e. the punctuation and keyword layer (`SS+O` = `;`, `SS+R` = `<`, `SS+T` = `>`,
`SS+Q` = `<=`, `SS+S` = `|`, `SS+D` = `STEP`). Under NextBASIC that is a more
painful loss than the Caps Shift chords they used to steal, so the case for
un-reserving them is now stronger, not weaker.

Two comments that justified their design by "Ctrl is Caps Shift" were updated
in the same branch to say Symbol Shift — `sdl_app.cpp:92-94` (mouse release) and
`main_window.cpp:612-616` (the no-shortcut rule quoted in §1). The reasoning in
both is unaffected; only the name of the shift key changes.

---

## 8. Proposed reserved list

Design rule: **reserve only what the hardware reserves, plus the minimum the
host OS forces on us. Everything else falls through to the guest.**

### 8.1 Recommended RESERVED

| Key | Rationale |
|---|---|
| `F1` – `F12` | Hardware reserves all twelve (`keymaps.vhd:72-79,96-102`); they are routed off-matrix via `o_ps2_func_keys_n` (`ps2_keyb.vhd:202-209`). jnext's existing assignments already match hardware meaning. Zero guest cost — no F-key exists on a Spectrum. |
| `Ctrl+Alt` | Pointer release. Already implemented as a fall-through (the guest still sees both keys), so it costs the guest nothing. Established VM convention. |
| `Alt+F4` (Windows) / platform close | The window manager owns it; jnext cannot reliably keep it anyway. |
| `Pause` / `Break` | Hardware reserves it (`keymaps.vhd:104`). Currently unbound in jnext — reserving it is free and matches hardware. |

That is the whole list. It costs the guest **nothing**: no reserved key above
has any ZX matrix meaning.

### 8.2 Recommended UN-RESERVED (returned to the guest)

| Key | Currently | Guest meaning it steals | Replacement |
|---|---|---|---|
| **`Ctrl+Q`** | Quit | `SS+Q` = `<=` | Menu File → Quit; window close button; `Alt+F4` |
| `Ctrl+O` | Load NEX | `SS+O` = `;` | Menu / toolbar Load button |
| `Ctrl+S` | Screenshot | `SS+S` = `\|` | Menu / toolbar Screenshot button |
| `Ctrl+Shift+S` | Save snapshot | `CS+SS+S` | Menu |
| `Ctrl+R` | Power reset | `SS+R` = `<` | Menu / toolbar Reset button; F1 already does hard reset |
| `Ctrl+T` | Open tape | `SS+T` = `>` | Tape menu |
| `Ctrl+D` | Toggle debugger | `SS+D` = `STEP` | View menu / Debug toolbar button (already exists) |
| `Ctrl+F5`, `Ctrl+F6` | Start/stop recording | none directly, but see note | Menu — and F5/F6 are reserved anyway |
| `Alt+F/M/I/T/D/V/S/H` | Menu mnemonics | `Alt` chords the guest uses (`Alt+E/G/C` today — `keyboard.cpp:169-171` — more if part 1's hardware bindings are adopted) | See §8.4 |

Every replacement already exists. **No functionality is lost — only its
keyboard route.**

### 8.3 Keys that must keep reaching the guest (already do — protect them)

`Esc` (BREAK), `Tab` (EXTEND MODE, via the `event()` override), arrows, `Space`,
`Enter`, `Backspace`, all letters and digits, and the punctuation keys. A
regression here is worse than the bug being fixed; §9 proposes the test.

### 8.4 The `Alt` question

`Alt+letter` is the awkward case: Qt makes menubar mnemonics into global
accelerators, and the guest wants `Alt` chords. Three options, in the order I
would recommend them:

1. **Keep the mnemonics but require the menubar to be activated first.**
   Qt does this natively if the top-level menus carry **no** `&` mnemonic:
   pressing and releasing bare `Alt` focuses the menubar, after which plain
   letters navigate it. The guest gets every `Alt+letter` chord back, and the
   menus stay fully keyboard-reachable. Cost: one extra keystroke to open a menu.
2. **Keep mnemonics, and formally reserve the eight letters F/M/I/T/D/V/S/H**,
   documenting them as unavailable to the guest. Cheapest to implement (zero
   code) but permanently blocks eight `Alt` chords and leaves the "adding a menu
   silently breaks a guest key" trap in place.
3. **Suppress mnemonics only while the guest has focus.** Most faithful,
   most code, and the mode is invisible to the user — I would not start here.

### 8.5 Mechanism

Given §2.3 (no override mechanism exists), the smallest change that implements
any of the above:

1. **Preferred: stop creating the shortcut.** For the eight `Ctrl+*` QActions,
   delete the `setShortcut(...)` call. The menu item, toolbar button and
   `triggered` wiring all stay; only the accelerator goes. This is a one-line
   deletion per action, needs no new machinery, and is trivially reviewable.
   It also automatically fixes the Qt/SDL divergence in §5, because the SDL
   frontend never had these shortcuts in the first place.
2. **If shortcuts must be retained as a user option**, add a single
   `QEvent::ShortcutOverride` handler on `MainWindow`. Qt sends
   `ShortcutOverride` before dispatching a shortcut; accepting the event
   cancels the shortcut and delivers a normal `KeyPress` instead. That is the
   one Qt-sanctioned hook for "the focused widget wants this key more than the
   shortcut does", and it is the natural home for a
   `Settings → Preferences → Input → "Send all keys to the emulator"` toggle
   (`AppConfig` already exists, `src/gui/app_config.*`). One handler covers
   every current and future QAction.
3. **Do not** use `grabKeyboard()`. It grabs at the window-system level, breaks
   the window manager's own bindings, and is far more than is needed.

Whichever is chosen, the reserved list must be **one shared table** consulted by
both `MainWindow` and `sdl_app.cpp`, or the frontends will drift again (§5).

---

## 9. Suggested tests (not written — part 2 is proposal-only)

If the owner adopts §8, these are the rows that would make it stick:

1. **A no-`Ctrl`-shortcut assertion.** Walk every `QAction` on the main window
   and assert none has a shortcut whose sequence contains
   `Qt::ControlModifier` with a letter key. This is the row that would have
   caught Ctrl+Q, and it keeps catching the next one.
2. **A reserved-list agreement test** between the Qt and SDL frontends: both
   must intercept exactly the §8.1 set and nothing else.
3. **A guest-reachability test** for §8.3 (`Esc`, `Tab`, arrows, `Space`,
   `Enter`, letters, digits) so un-reserving cannot regress into over-swallowing.

There is precedent for all three: `debugger_quit_gate_test` and `esc_break_test`
already unit-test GUI key policy without a display.

---

## 10. Open items and things not verified

- **`QKeySequence::Preferences`** (§3.1 #12) — platform binding inferred from
  Qt's documented standard-key table, **not** confirmed empirically. Check on
  macOS before relying on it.
- **Bare `Alt` menubar activation** — `Alt` reaches the guest as a modifier
  (`keyboard.cpp:267-270`), but whether `QMenuBar` *also* activates on
  Alt-press-release is Qt-internal and platform-conditional. Not confirmed on
  Linux/X11. Option §8.4.1 depends on this behaviour, so confirm it first.
- **`Alt+W` double-binding** in the debugger (§4.2) is a real pre-existing
  defect found during this inventory. It is out of scope for #115 and should get
  its own issue.
- **No hardware confirmation.** Everything here derives from the FPGA VHDL and
  from reading jnext's source. No physical ZX Spectrum Next was available.
