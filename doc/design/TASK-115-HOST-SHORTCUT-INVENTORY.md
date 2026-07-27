# Task 115 — Host Shortcut Inventory, and the Alt Migration That Followed

> **Status: DECIDED AND IMPLEMENTED.** §§1-7 are the original inventory (the
> evidence). §8 is the owner's decision and what shipped. §9 is the test suite
> that pins it. Every table below has been updated to the post-migration state;
> where the pre-migration value matters it is shown struck through or named
> explicitly, so this file still reads as the record of what changed and why.
>
> **What shipped:** the six plain `Ctrl+<letter>` host shortcuts moved to
> `Alt+<letter>`. Ctrl is the guest's Symbol Shift and is now left entirely to
> the guest. Three menubar mnemonics were re-lettered to clear the collisions
> the move created. Pinned by `test/gui/host_hotkey_test.cpp` (27 rows).
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
host shortcut steals a real Spectrum key. jnext bound six of them; §8 is how
that was resolved.

The codebase already knows this. `src/gui/main_window.cpp:650-655`, on the
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

- `src/gui/main_window.cpp:178` — `setCentralWidget(emulator_widget_)`
- `src/gui/main_window.cpp:186` — `setFocusPolicy(Qt::StrongFocus)` on the window
- `src/gui/emulator_widget.cpp:8` — `EmulatorWidget`'s constructor **never** calls
  `setFocusPolicy`, so it keeps `QWidget`'s default `Qt::NoFocus`.

The viewport is not a focus target. Key events land on `MainWindow` and the
guest is fed from the *window's* handler via `handle_key()`
(`src/gui/main_window.cpp:1593-1607`).

### 2.2 Precedence, highest first

1. **Qt shortcut map** — `QAction::setShortcut`. Default context is
   `Qt::WindowShortcut`; `setShortcutContext` is called **nowhere** in `src/`.
   Qt delivers `QEvent::ShortcutOverride` then `QEvent::Shortcut` *before* the
   key ever arrives as a `KeyPress` at the focus widget.
2. **`QMenuBar` Alt+letter mnemonics** — same layer.
3. **`MainWindow::event()`** — `src/gui/main_window.cpp:1573-1591`. Intercepts
   `Tab`/`Backtab` only.
4. **`MainWindow::keyPressEvent` / `keyReleaseEvent`** —
   `src/gui/main_window.cpp:1351`, `:1524`.
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
unreachable. The source says so at `src/gui/main_window.cpp:1420-1424`:

```
// Note (#45): plain F4 is normally
// consumed by the Machine > Soft Reset QAction shortcut before it
// reaches this handler (same situation as F11/fullscreen_action_),
// so the F4 case below only sees modified presses (Shift/Ctrl+F4).
```

- **F4** — `soft_reset` QAction (`:519`) wins; `case Qt::Key_F4` (`:1433`) is dead for plain F4.
- **F11** — `fullscreen_action_` (`:725`) wins; `case Qt::Key_F11` (`:1405`) is dead.

---

## 3. Inventory — Qt main window

### 3.1 `QAction` shortcuts (12). All swallowed; guest never sees them.

Post-migration state. "Was" is the pre-#115 binding where it changed.

| # | Key | Was | file:line | Action | Guest collision |
|---|-----|-----|-----------|--------|-----------------|
| 1 | `Alt+O` | `Ctrl+O` | `main_window.cpp:396` | Load NEX dialog | Cleared — SS+O `;` is back |
| 2 | `Ctrl+F5` | — | `main_window.cpp:405` | Start MPEG4 recording | No (F-key, no ZX meaning) |
| 3 | `Ctrl+F6` | — | `main_window.cpp:423` | Stop MPEG4 recording | No |
| 4 | `Alt+S` | `Ctrl+S` | `main_window.cpp:457` | Save Screenshot dialog | Cleared — SS+S `\|` is back |
| 5 | `Ctrl+Shift+S` | — | `main_window.cpp:490` | Save Snapshot | **Yes, still** — CS+SS+S. Out of #115's scope (a three-key chord, not one of the six); see §8.3 |
| 6 | **`Alt+Q`** | **`Ctrl+Q`** | `main_window.cpp:496` | **`QApplication::quit`** | Cleared — SS+Q `<=` is back |
| 7 | `Alt+R` | `Ctrl+R` | `main_window.cpp:515` | Power Reset (cold boot) | Cleared — SS+R `<` is back |
| 8 | `F4` | — | `main_window.cpp:519` | Soft Reset | No |
| 9 | `Alt+T` | `Ctrl+T` | `main_window.cpp:668` | Open Tape File | Cleared — SS+T `>` is back |
| 10 | `F11` | — | `main_window.cpp:725` | Fullscreen (checkable) | No |
| 11 | `Alt+D` | `Ctrl+D` | `main_window.cpp:740` | Toggle debugger (`#ifdef ENABLE_DEBUGGER`) | Cleared — SS+D `STEP` is back |
| 12 | `QKeySequence::Preferences` | — | `main_window.cpp:751` | Preferences dialog | No — see note |

**Quit still bypasses `closeEvent`** (`main_window.cpp:495-503`):

```cpp
QAction* quit = file_menu->addAction(tr("&Quit"));
quit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
connect(quit, &QAction::triggered, qApp, &QApplication::quit);
```

The migration changed the key and nothing else. **Nothing guards it** — no
confirmation, and because it goes straight to `QApplication::quit()` it does
**not** route through `MainWindow::closeEvent` (declared `main_window.h:159`),
so a Quit from here skips the recorder-stop and debugger-teardown that closing
the window performs. The owner's #115 decision was the plain key migration and
explicitly *not* a confirmation dialog, so this is recorded, not fixed. It is
unchanged from the pre-#115 behaviour in every respect except which key
reaches it.

**Note on #12 (`QKeySequence::Preferences`) — the earlier guess was wrong.**
This inventory originally reasoned from Qt's documented standard-key table that
`Preferences` resolves to an empty sequence on Linux and consumes no host key,
and flagged it as unverified. It is now measured: on this host (Qt 6.11.1,
Linux) it resolves to the portable sequence **`Settings`** — the dedicated
`Qt::Key_Settings` multimedia key, not a modifier chord. Either way it claims
no letter and collides with nothing, but the *reason* is not the one given
before. Measured by `host_hotkey_test`'s H115-27 failure detail, which prints
the complete live shortcut set. macOS (Cmd+`,`) is still unverified here.

### 3.2 Top-level menu mnemonics → global Alt+letter (8)

Mechanism: `menuBar()->addMenu(tr("&X"))`. Qt makes a top-level menubar mnemonic
an Alt+letter accelerator whenever the window is active. All swallowed.

| Alt key | Title | Was | file:line |
|---|---|---|---|
| `Alt+F` | `&File` | — | `main_window.cpp:393` |
| `Alt+M` | `&Machine` | — | `main_window.cpp:506` |
| `Alt+I` | `&Input` | — | `main_window.cpp:618` |
| `Alt+A` | `T&ape` | `Alt+T` (`&Tape`) | `main_window.cpp:665` |
| `Alt+B` | `De&bug` | `Alt+D` (`&Debug`) | `main_window.cpp:690` |
| `Alt+V` | `&View` | — | `main_window.cpp:704` |
| `Alt+N` | `Setti&ngs` | `Alt+S` (`&Settings`) | `main_window.cpp:748` |
| `Alt+H` | `&Help` | — | `main_window.cpp:755` |

**Three of these were re-lettered by the migration, and had to be.** Moving the
six shortcuts onto Alt put `Alt+S`, `Alt+T` and `Alt+D` into a namespace already
claimed by `&Settings`, `&Tape` and `&Debug`. A letter claimed twice is an
*ambiguous* Qt shortcut, and `QAction::event()` answers an ambiguous
`QShortcutEvent` with a `qWarning` and nothing else — so **both** bindings stop
working, with no visible error. Measured, not assumed: with `&Tape` and the
`Alt+T` shortcut both live, four consecutive `Alt+T` presses fired the action
**zero** times. The visible menu titles are unchanged ("Tape", "Debug",
"Settings"); only which letter is underlined moved.

**Collision risk with the guest's Alt chords.** `src/input/keyboard.cpp:163,172-174`
binds `Alt+E` → EDIT, `Alt+G` → GRAPH, `Alt+C` → CAPS LOCK, and `:163`
`Alt+`` ` `` → INV VIDEO. E, G and C are not used by any top-level menu or any
shortcut — that used to be luck, and is now **checked**: `host_hotkey_test`
row H115-27 fails if any host binding claims one. Adding a menu titled `&Edit`,
`&Graphics` or `&Config`, or a shortcut on those letters, now fails a test
instead of silently killing a guest key. The re-lettering above also had to
avoid E/G/C, which is why `Settings` went to `N` rather than the more natural
`&Settings`→`S&ettings`.

### 3.3 `keyPressEvent` interceptions (`main_window.cpp:1351-1522`)

All swallowed unless noted.

**Debugger-active only** (`#ifdef ENABLE_DEBUGGER`, guarded by
`debugger_mgr_->is_enabled()`):

| Key | file:line | Effect |
|---|---|---|
| `F5` | `:1376` | Run |
| `F6` | `:1381` | Step Into |
| `F7` | `:1386` | Step Over |
| `F8` | `:1391` | Step Out |
| `F9` | `:1396` | Pause |

**Unconditional:**

| Key | file:line | Effect | Live? |
|---|---|---|---|
| `F11` | `:1405` | toggle fullscreen | Dead — QAction `:725` wins |
| `F2` | `:1412` | cycle scale | **Live** |
| `F1` | `:1425` | hard reset | Live (only if no Shift/Ctrl) |
| `F4` | `:1433` | soft reset | Dead for plain F4 |
| `F9` | `:1441` | Multiface NMI (debugger off) | Live |
| `F10` | `:1452` | DivMMC NMI | Live |
| `F3` | `:1480` | EmuFnKeys 3 | Live |
| `F5` | `:1487` | EmuFnKeys 5 (expansion bus on) | Live |
| `F6` | `:1494` | EmuFnKeys 6 (expansion bus off) | Live |
| `F7` | `:1501` | EmuFnKeys 7 (scanline weight) | Live |
| `F8` | `:1508` | EmuFnKeys 8 (CPU speed) | Live |

**The one deliberate fall-through** — `main_window.cpp:1360-1371`:

```cpp
if (mouse_captured_ &&
    ((key == Qt::Key_Alt     && (modifiers & Qt::ControlModifier)) ||
     (key == Qt::Key_Control && (modifiers & Qt::AltModifier)))) {
    set_mouse_captured(false);
    // Deliberately NOT consumed: fall through to normal key handling.
```

Ctrl+Alt releases the pointer **and** still reaches the guest, because
swallowing the Alt key-down would desync `Keyboard::alt_held_`.

`Esc` deliberately has no case (`:1409-1411`) — Task 77 made it the ZX BREAK key.

### 3.4 `keyReleaseEvent` (`main_window.cpp:1524-1571`)

Consumes releases of F11/F2 (`:1528-1531`), F1/F4/F9/F10 (`:1534-1537`), and
F3/F5/F6/F7/F8 (`:1543-1556`, with a debugger-enabled exception at `:1560-1564`),
so hotkey releases never bleed into the ZX matrix. Auto-repeat is filtered at
`:1526` and again in `handle_key` at `:1595`.

### 3.5 `MainWindow::event()` — the Tab override (`main_window.cpp:1573-1592`)

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
guest** (`main_window.cpp:139` → `SDL_SCANCODE_F12`), where it is a no-op
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

> **Frontend divergence — mostly closed by the migration.** The six chords the
> issue was about (`Ctrl+Q/O/S/R/T/D`) reached the guest under SDL and were
> swallowed under Qt. **They now reach the guest under both**, because the fix
> removed the Qt bindings rather than adding SDL ones — the frontends converged
> on SDL's pre-existing (correct) behaviour, with no SDL change at all.
>
> **What still differs, and why it is not a defect to fix here:** `Ctrl+Shift+S`
> (out of #115's scope, §8.3) and every `Alt+letter` are still Qt-only. That is
> structural, not drift: the SDL frontend has no menu bar, no mnemonics and no
> shortcut map (`sdl_app.cpp:87-143` is a flat `if`-chain on F-keys plus
> Ctrl+Alt), so there is no menu for an accelerator to reach. A shared reserved
> table would have nothing to enforce on the SDL side beyond the F-keys both
> already intercept identically.

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
`main_window.cpp:650-655` (the no-shortcut rule quoted in §1). The reasoning in
both is unaffected; only the name of the shift key changes.

---

## 8. The decision, and what shipped

Design rule the owner adopted: **reserve only what the hardware reserves, plus
the minimum the host OS forces on us. Everything else falls through to the
guest — and where jnext genuinely needs a host chord, it takes it from `Alt`,
never from `Ctrl`.**

### 8.1 RESERVED (unchanged by the migration)

| Key | Rationale |
|---|---|
| `F1` – `F12` | Hardware reserves all twelve (`keymaps.vhd:72-79,96-102`); they are routed off-matrix via `o_ps2_func_keys_n` (`ps2_keyb.vhd:202-209`). jnext's existing assignments already match hardware meaning. Zero guest cost — no F-key exists on a Spectrum. |
| `Ctrl+Alt` | Pointer release. Implemented as a fall-through (the guest still sees both keys), so it costs the guest nothing. Established VM convention. |
| `Alt+F4` (Windows) / platform close | The window manager owns it; jnext cannot reliably keep it anyway. |
| `Pause` / `Break` | Hardware reserves it (`keymaps.vhd:104`). Still unbound in jnext — reserving it is free and matches hardware. |

F-keys and Pause/Break were **deliberately not migrated**: the hardware itself
routes them off the key matrix, so intercepting them is hardware-faithful and
costs the guest nothing.

### 8.2 MIGRATED — the six plain `Ctrl+<letter>` chords → `Alt+<letter>`

| Was | Now | Action | Guest sequence handed back |
|---|---|---|---|
| `Ctrl+Q` | `Alt+Q` | Quit | `SS+Q` = `<=` |
| `Ctrl+O` | `Alt+O` | Load NEX | `SS+O` = `;` |
| `Ctrl+S` | `Alt+S` | Save Screenshot | `SS+S` = `\|` |
| `Ctrl+R` | `Alt+R` | Power Reset | `SS+R` = `<` |
| `Ctrl+T` | `Alt+T` | Open Tape File | `SS+T` = `>` |
| `Ctrl+D` | `Alt+D` | Toggle debugger | `SS+D` = `STEP` |

**No functionality moved or was lost** — every action keeps its menu entry, its
toolbar button where it had one, and its handler. Only the accelerator changed.

Three menubar mnemonics moved to make room (§3.2): `&Tape`→`T&ape` (Alt+A),
`&Debug`→`De&bug` (Alt+B), `&Settings`→`Setti&ngs` (Alt+N). The visible menu
titles are identical; only the underlined letter differs.

### 8.3 NOT migrated, and why

- **`Ctrl+Shift+S`** (Save Snapshot) — outside the owner's stated scope, which
  was the six *plain* `Ctrl+<letter>` chords. It does still swallow the guest's
  `CS+SS+S`. Known residual, deliberately left; the guard row H115-13 is scoped
  to plain chords so it does not silently flip to green over this.
- **`Ctrl+F5` / `Ctrl+F6`** (recording) — F-keys have no ZX matrix meaning, so
  these steal nothing from the guest.
- **The nine debugger accelerators** (§4.1) — checked, and none is a
  `Ctrl+<letter>` chord: they are F5–F9 and Shift+F6/F7. `Qt::CTRL` appears
  nowhere in `src/debugger/`. Nothing to migrate.

### 8.4 The `Alt` question — resolved as "Alt is host-only"

`Alt+letter` is one namespace shared by menubar mnemonics and QAction
shortcuts, and the guest wanted it too. The owner's decision: **Alt belongs to
the host.** jnext claims `Alt+Q/O/S/R/T/D` (shortcuts) and `Alt+F/M/I/A/B/V/N/H`
(mnemonics); the guest keeps only `Alt+E/G/C` (EDIT / GRAPH / CAPS LOCK) and
``Alt+` `` (INV VIDEO), and those three are now test-protected (H115-27).

**Deliberate divergence from hardware, recorded here so no one later files it
as a bug:** real Next hardware maps **Left Alt = EXTEND MODE** and
**Right Alt = GRAPH** (`keymaps.vhd:85-94`). jnext will not do that — it
follows the FUSE/ZEsarUX convention that puts EXTEND MODE on `Tab`
(`keyboard.cpp:154`, and the `MainWindow::event()` override at
`main_window.cpp:1573-1591` that makes `Tab` reachable at all). Reserving Alt
for the host is the price paid for giving Ctrl back to the guest, and Ctrl is
worth far more: Symbol Shift is used constantly in NextBASIC, whereas EXTEND
MODE already has a working, conventional home.

The two options this section previously preferred were both rejected:
bare-`Alt` menubar activation (§8.4.1 as written) would have required dropping
every mnemonic and depended on unverified Qt behaviour; focus-conditional
mnemonic suppression (§8.4.3) is an invisible mode.

### 8.5 Mechanism used

Option 1 of the original proposal, with one change: rather than **deleting**
the `setShortcut(...)` calls, each was **re-pointed** from `Qt::CTRL` to
`Qt::ALT`, because the owner wanted the shortcuts kept, not removed. That is
still a one-line edit per action with no new machinery, and it converged the
Qt frontend onto the SDL frontend's behaviour for those six chords (§5)
without touching `sdl_app.cpp` at all.

The `QEvent::ShortcutOverride` handler (option 2) was **not** built: it is only
needed if shortcuts must be retained on Ctrl behind a user toggle, and the
migration removes that need. It remains the right design if a
"send all keys to the emulator" preference is ever wanted.

`grabKeyboard()` (option 3) was not used and should not be.

The "one shared reserved table consulted by both frontends" idea was **not**
implemented, and §5 explains why it would have nothing to enforce: SDL has no
menus, so the only shared reserved set is the F-keys, which both frontends
already intercept identically.

### 8.6 Keys that must keep reaching the guest (protected)

`Esc` (BREAK), `Tab` (EXTEND MODE, via the `event()` override), arrows,
`Space`, `Enter`, `Backspace`, all letters and digits, the punctuation keys —
**and now every `Ctrl+<letter>` chord**. A regression here is worse than the
bug that was fixed, which is what §9 is for.

---

## 9. The tests that pin this — `test/gui/host_hotkey_test.cpp` (29 rows)

Registered in `test/CMakeLists.txt` and declared in `test/unit-tests.conf`
(`?host_hotkey_test 29`). Needs a real `MainWindow`, no display — offscreen QPA
is forced in `main()`, the same idiom as `esc_break_test`.

| Rows | What they prove |
|---|---|
| H115-01..06 | Each `Ctrl+<letter>` chord arrives in the **emulated key matrix** as Symbol Shift + letter. Real `QKeyEvent`s → `MainWindow` → real `Keyboard` → `read_rows()`, the same call port `0xFE` makes. The property, not the menu label. |
| H115-07..12 | Releasing the chord clears both matrix bits — no stuck key. |
| H115-13 | No `QAction` under the main window binds a plain `Ctrl+<letter>`. The row that would have caught `Ctrl+Q`, and keeps catching the next one. |
| H115-14..19 | Each migrated action carries exactly its `Alt+<letter>`. |
| H115-20..25 | Each `Alt+<letter>` really activates its action through the live shortcut map, and types nothing into the guest. |
| H115-26 | Menubar mnemonics and QAction shortcuts are disjoint — no ambiguous Alt binding. |
| H115-27 | The guest's `Alt+E/G/C` are claimed by no host binding. |
| H115-28 | No `QAction`'s text, tooltip or status tip names a modifier chord the product does not bind. Two tiers: an action with its own shortcut must name *that*; an action without one (a toolbar twin of a menu action) must name a chord *something* binds. |
| H115-29 | Every modifier chord advertised in `FEATURES.md` is one the product really binds. |

**Why these rows are not decoration.** `QApplication::sendEvent()` to a widget
passes through `QApplication::notify()`, which consults the global
`QShortcutMap` *before* dispatching the `KeyPress` — so a synthetic `Ctrl+O`
here is swallowed by a live `Ctrl+O` QAction exactly as a real one would be.
Verified by mutation, seven cycles:

| Mutation | Rows that fail |
|---|---|
| All six shortcuts back on `Ctrl` | 19: H115-01..06, 13, 14..19, 20..25. The detail line reads `sym_row7=0x1D letter_row2=0x1F` — Symbol Shift down, letter swallowed. Exactly the reported bug. |
| `T&ape` back to `&Tape` (restore the mnemonic collision) | 2: H115-24 (`fired=0` — the shortcut silently stops working) and H115-26 |
| `&Machine` → `Machin&e` (a future menu steals the guest's Alt+E) | 1: H115-27 |
| Letter key-releases consumed in `keyReleaseEvent` | 6: H115-07..12, each with its own letter's bit stuck low |
| Toolbar bug-button tooltip back to `Ctrl+D` | 1: H115-28 — `Debug says Ctrl+D but nothing binds it` |
| `FEATURES.md` back to `Ctrl+R` / `Ctrl+S` | 1: H115-29 — `unbound=Ctrl+R,Ctrl+S` |
| Every chord deleted from `FEATURES.md` | 1: H115-29 — the vacuity guard (`found=`), so an unreadable file or a broken pattern fails instead of passing empty |

Every row is covered by at least one mutation.

Two of the three tests this section originally suggested were built (the
no-`Ctrl`-shortcut assertion, and guest-reachability for the six chords). The
"reserved-list agreement test between the Qt and SDL frontends" was **not**:
per §5 the frontends' only shared reserved set is the F-keys, and a test
asserting agreement on a set of size zero beyond that proves nothing. Broad
guest-reachability for `Esc`/`Tab` is already covered by `esc_break_test`.

### 9.1 Why H115-28/29 exist — the seam the first cut missed

The first cut of this migration re-pointed every `QAction` and every mnemonic
correctly, passed all 27 rows, `docs-check`, `cli-check` and the full 5857-row
unit run — and **still shipped two defects**, because the enumeration stopped at
`QKeySequence` and mnemonic *objects* and never swept the tree for literal
`"Ctrl+<letter>"` *text*:

- `debugger_manager.cpp:246` — the toolbar bug-button tooltip still read
  `Toggle Debugger (Ctrl+D)`. The product was telling the user to press the
  exact chord the fix hands back to the guest, where it types `SS+D` = `STEP`.
- `FEATURES.md:64,66` — still advertised `Ctrl+R` and `Ctrl+S`.

Neither was catchable by any existing gate: nothing tests tooltip text, and
`docs-check` cannot see `FEATURES.md` at all, so both were green.

**The generalisable rule: a chord named in prose is a promise about a binding,
and a promise is checkable whenever the binding is a live object.** H115-28/29
make it checkable.

**What they do NOT cover, stated so a green row is not over-read:**

- **H115-28's tier 2 proves only that a named chord is bound to *something*, not
  that it is the *right* something.** See §10 — this is the guard's own
  disclosed limit, and it is inherent, not an implementation slip.
- `doc/man/jnext.1.md` and `src/doc/user-guide/**` are deliberately **not**
  scanned. Both now carry a paragraph naming `Ctrl+O/D/R/T/Q/S` *on purpose* —
  to tell the reader those chords reach the guest — so scanning them would need
  the checker to skip that passage, and a checker exclusion is exactly how a
  real defect hides (the project's standing rule: exceptions are declared in
  the data, never as a scan exclusion). Those files remain a human-review
  responsibility.
- `README.md` is not scanned: it contains no chord at all today, so the branch
  would be vacuous.
- Bare F-keys are not matched — `Multiface NMI (F9)` names a `keyPressEvent`
  key, not a `QAction` shortcut. `Ctrl+Alt` (mouse release) does not match
  either, there being no key after the second modifier. That is why the group
  needs **no** exception table today.

---

## 9.2 Full-tree sweep for literal chord text, and its disposition

`git grep -nE 'Ctrl[+-][QOSRTD]'` over the whole tree, every hit classified.
This is the sweep the first cut should have done.

**Live prose describing current behaviour — FIXED:**

| Hit | Was | Now |
|---|---|---|
| `src/debugger/debugger_manager.cpp:251` | tooltip `Toggle Debugger (Ctrl+D)` | `Alt+D` |
| `FEATURES.md:64` | `Power Reset (Ctrl+R/F1, cold boot)` | `Alt+R/F1` |
| `FEATURES.md:66` | `PNG screenshot (Ctrl+S, toolbar, …)` | `Alt+S` |
| `src/core/emulator.cpp:7358` | comment `GUI "Save Screenshot" (Ctrl+S)` | `Alt+S` |

**Correct as-is — chords that did not move:** `FEATURES.md:51`,
`doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md:1109`, `test/mmu/mmu_test.cpp:4148`
(all `Ctrl+Shift+S`); `doc/man/jnext.1.md:439` (`Ctrl+F5`/`Ctrl+F6`).

**Historical record — deliberately LEFT naming the old chords:**

- `.prompts/*.md` — dated daily task logs. Rewriting a log to match today's
  code destroys its only value. **6** matches of `Ctrl[+-][QOSRTD]` across
  **4** files: `2026-03-22` ×1, `2026-04-07` ×1, `2026-05-03` ×1,
  `2026-05-04` ×3. Of those, only **2** name a chord that actually moved
  (`Ctrl+D` in `2026-03-22:97`, `Ctrl+S` in `2026-04-07:51`); the other 4 are
  the `Ctrl+S` *prefix* of `Ctrl+Shift+S`, which did not move. `2026-07-12:552`
  says `Ctrl+d` (lower case, so no match) and `2026-07-19` names only `Ctrl+Alt`
  and `Ctrl+M` — neither is one of the six.

  > An earlier revision of this section said "7 hits across 2026-03-22 …
  > 2026-07-19". Both halves were wrong: the count, and a range endpoint that
  > contains no matching hit at all. Corrected on a recount. Noted rather than
  > silently patched because a wrong number in the record of a sweep is the
  > same defect class as the stale tooltip this whole section exists to
  > document.
- `doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md` — declared frozen by
  CLAUDE.md.
- `doc/design/EMULATOR-DESIGN-PLAN.md:816` — a completed roadmap checkbox,
  annotated in place as `Alt+O (Ctrl+O until issue #115)` rather than silently
  rewritten.
- `doc/design/WINDOWS-COMPAT-PLAN.md:249` — cites `QKeySequence(Qt::CTRL |
  Qt::Key_X)` as the **Qt5-clean API shape**, not as a user-facing chord. The
  claim is still true (`Qt::ALT | Qt::Key_X` is the same int-promotion form,
  and `Qt::CTRL` uses remain for `Ctrl+F5`/`Ctrl+F6`/`Ctrl+Shift+S`).
- This document, and `test/gui/host_hotkey_test.cpp` — both name the old
  chords because describing the change is their job.

**Not chords at all** (matched only a loose grep): `Ctrl-C` in shell scripts and
harness comments, `ctrl` local variables, NextREG names like `Line IRQ Ctrl`,
and `tools/debug/cspect-g46b-probe11.dbg:49` (`Ctrl+F5` is a *CSpect* key).

**Noted, NOT fixed — out of scope, flagged for its own issue:**
`doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md:637` lists `Ctrl+S` among
"hardcoded debugger keys". The debugger has no `Ctrl+S` binding and never did,
so that line was wrong before #115 and is unrelated to it — and the file is
frozen.

---

## 10. Open items and things not verified

- **`QKeySequence::Preferences`** (§3.1 #12) — the earlier inference was wrong
  and is corrected there: measured on this host it resolves to `Settings`
  (a multimedia key), not an empty sequence. **macOS is still unverified** —
  if it is Cmd+`,` there, it does not collide with anything jnext binds, but
  nobody has run it.
- **The `Alt+Q` → `QApplication::quit()` bypass of `closeEvent` remains**
  (§3.1). Out of scope by the owner's explicit decision (no confirmation
  dialog); recorded, not fixed. Worth its own issue.
- **H115-28 has a tier-2 blind spot, and a green row must not be read as
  "every chord in every tooltip is correct".** For an action that carries its
  own shortcut the row is exact (the chord must BE that shortcut). For an
  action with **no** shortcut of its own — a toolbar twin of a menu action,
  like the debug bug-button — the row can only ask "is this chord bound to
  *something*". So a tooltip naming a **real but wrong** chord passes silently.
  Demonstrated, not theorised: setting that tooltip to
  `Toggle Debugger (Alt+S)` — a live chord, but Save Screenshot's — still
  reports **29/29 green**.

  This is inherent rather than an implementation slip: Qt offers no structural
  way to declare "this toolbar action mirrors that menu action", so nothing is
  available to compare against. It is also strictly narrower than the defects
  the row was built for, which were chords bound to **nothing** — the actual
  historical failure mode, twice over (`Ctrl+D` in the tooltip, `Ctrl+R`/`Ctrl+S`
  in `FEATURES.md`).

  Worth recording why it went undisclosed for a round: the mutation table in §9
  exercised only the reverted-to-**unbound** case, which is the case the row
  catches. A mutation that only ever reproduces the bug you already know about
  cannot find the limits of your own guard. Closing the gap properly needs an
  explicit twin declaration (e.g. the toolbar action carrying the menu action's
  pointer, or `setShortcut` on both), which is a change to the product's own
  structure and out of scope for #115.

- **`Alt+W` double-binding** in the debugger (§4.2) is a real pre-existing
  defect found during the original inventory. Still unfixed, still out of scope
  for #115, still wants its own issue.
- **Stray key-release delivery — inert by construction, not merely by
  measurement.** Qt consults the shortcut map only for `KeyPress`, so the
  `KeyRelease` matching an `Alt+<letter>` shortcut *is* delivered to the guest.
  It cannot desync anything: on an unpaired release `Keyboard::set_key` takes
  `use_alt = alt_variant_[sc]` (`keyboard.cpp:288-292`), which was never set
  because the press never arrived — and none of `Q/O/S/R/T/D` has an
  `s_alt_compound` or `s_alt_extkey` entry in the first place, so `use_alt`
  would resolve false on the press edge too. The release therefore takes the
  plain `s_map` path and clears a matrix bit that is already clear. The same
  was true of the `Ctrl` chords before the migration. Noted so the next reader
  does not mistake it for new.
- **No hardware confirmation.** Everything here derives from the FPGA VHDL and
  from reading and running jnext. No physical ZX Spectrum Next was available.
- **Not exercised on a real X11/Wayland desktop by an automated test.** The
  suite runs under the offscreen QPA platform. Window-manager-level Alt
  handling (some desktops use Alt-drag to move windows) is a plausible source
  of platform-specific surprises and wants a manual check on each platform.
