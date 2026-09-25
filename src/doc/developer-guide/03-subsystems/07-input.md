# 3.7 Input

The input subsystem is everything between a host event — a key going down, a
gamepad button, a mouse movement — and the byte a Z80 `IN` instruction
eventually reads back. Input state is never pushed at the guest: host events
accumulate in a small set of objects, and the guest samples them whenever its
own code gets round to reading port `0xFE`, `0x1F`, `0x37`, one of the Kempston
mouse ports, or one of the NextREGs that expose raw key and pad state. The only
host keys that reach the CPU of their own accord are the emulator function keys
handled by `EmuFnKeys` below, and they do it through reset and NMI rather than
through the matrix.

The classes fall on either side of that port boundary, and ownership is what
tells them apart rather than the directory they live in. The **machine-side**
classes — `Keyboard`, `Joystick`, `KempstonMouse`, `MembraneStick`, `IoMode`,
`Md6ConnectorX2`, `EmuFnKeys` — are members of `Emulator`, read by the port
handlers `Emulator::init()` registers. The **host-side** adapters —
`GamepadHost`, `JoystickDispatcher`, `MouseDispatcher` — are owned by the
frontend instead (`SdlApp`, and the Qt `MainWindow` / `QtApp`), and exist only
to turn SDL events into the state those members hold. Both groups live under
`src/input/`.

A second, more consequential split runs through the same directory. Some
classes are transcriptions of FPGA modules and answer to the VHDL. The rest
have no VHDL counterpart at all, because they stand in for a physical adapter
the FPGA merely assumes is present — the PS/2 driver, the pad cable — or
compensate for an artefact of emulating a frame as an indivisible unit.
**Knowing which group a class is in tells you what its correctness criterion
is**: the VHDL, or a documented host policy. Nothing else does.

`src/input` links SDL3 even in the Qt build, because SDL scancodes are the
project's canonical host-key type. `MainWindow::qt_key_to_sdl()` converts Qt
keys into them, so both frontends feed `Keyboard::set_key()` identically and
neither can drift from the other.

## The keyboard matrix

`keyboard.{h,cpp}` models the 8 × 5 membrane. `matrix_[row]` is active-low, row
selection comes from the upper byte of the port address (bit *N* clear selects
row *N*), and `read_rows()` returns the 5-bit AND of every selected row. Bits
7:5 of the port `0xFE` byte are composed by the caller in `Emulator`, not here.

Three layers apply before that row-select AND, in this order:

1. **Shift hysteresis.** `membrane.vhd:178` holds the Caps-Shift and
   Symbol-Shift column state for one extra scan, which delays a release.
   `tick_scan()` advances the two-entry history once per **membrane scan** —
   4608 master cycles, which the VHDL annotates as "complete scan every 2.5
   scanlines" (`zxnext_top_issue2.vhd:1179`) — driven from
   `Emulator::on_scanline()` by a cycle accumulator. It used to run once per
   video frame, which stretched a 165 µs hold to 20 ms and left a released
   Symbol Shift contaminating the *next* frame's keystroke: a tap of Symbol
   Shift followed by a tap of `P` typed `"` instead of `PRINT` (issue #268).
   The rate is part of the spec, not an implementation detail.
2. **The extended-key fold**, described below.
3. **The `MembraneStick` fold**, also below, matching
   `keyb_col <= keyb_col_i_q AND membrane_stick_col AND ps2_kbd_col`.

## Which compounds the hardware synthesises

Several ZX keys are compounds — CAPS LOCK is Caps Shift plus `2`, and so on —
and the question of *who* presses the shift half has been got wrong here once,
so it is worth stating precisely.

**The sixteen extended keys are folded by the hardware itself.** Cursors,
DELETE, EXTEND MODE, CAPS LOCK, GRAPH, TRUE VIDEO, INV VIDEO, BREAK, EDIT and
the four symbol-shifted punctuation keys (`;` `"` `,` `.`) each set a bit in a
raw 16-bit register, and `membrane.vhd:236-240` ANDs that register into the
matrix — **both** halves of the compound. The digit or letter column *and* the
Caps-Shift or Symbol-Shift that goes with it are produced by the membrane, not
by whoever is pressing keys.

jnext models that path rather than short-cutting it. `set_key()` routes these
host keys to `set_extended_key(id)`, which touches only `ex_matrix_`, and
`read_rows()` does the fold, deriving the shift half from the same per-index
split the VHDL uses: every index drives Caps Shift except the four punctuation
ones, and EXTEND drives both, which is why it comes out as CS+SS. Going the
long way round buys two behaviours a directly asserted compound cannot express.
NR `0xB0` / `0xB1` read the raw register, so a program can see which extended
key is held; and NR `0x68` bit 4 cancels the **fold** while leaving that
register reporting normally. Asserting the compound directly — which jnext used
to do — makes both impossible.

**Three compounds are synthesised host-side instead**, in `s_compound[]`:
`/` → SS+V, `-` → SS+J, `=` → SS+L. On hardware these come from the PS/2
keymap's `SYMBOL` bit (`keymaps.vhd`, `ps2_keyb.vhd:197`), and jnext does not
model the PS/2 protocol at all — SDL key events replace it. Both matrix bits
are therefore asserted directly, with no raw register behind them and nothing
to read back.

## Joysticks

`joystick.{h,cpp}` is the machine side. It decodes NR `0x05` into a
per-connector mode (Sinclair 1/2, Cursor, Kempston 1/2, MD3 left/right, I/O
mode) and composes the port `0x1F` and `0x37` reads. Each connector holds a
**12-bit raw vector** whose layout is `zxnext.vhd:3441-3442`.

The constraint that shapes everything above it is that **only six of those
twelve bits ever reach a port, and two of them conditionally.** U/D/L/R and C/B
are readable in Kempston or MD mode; START and A are driven in MD mode only and
hard-tied to 0 in Kempston; MODE/X/Z/Y reach no port under any mode and are
visible only through NR `0xB2`. So a host pad's four face buttons are mapped
onto B, C, A and START — the bits a program can actually read — and the MD6
latch bits are bound only to spare controls.

`membrane_stick.{h,cpp}` (`membrane_stick.vhd`) is the joystick-to-keyboard
adapter used by the Sinclair and Cursor modes. It is a module of its own,
upstream of the port `0xFE` read rather than something inside `Keyboard`,
exactly as the VHDL decomposes it. Its user-defined keymap is a 64 × 6-bit RAM
reprogrammable through NR `0x28` / `0x29` / `0x2B`, initialised from the FPGA's
own `keyjoy_64_6.coe`.

`md6_connector_x2.{h,cpp}` is a faithful transcription of the MD6 scan FSM, and
it is **not on any live read path**: the NR `0xB2` handler reads
`Joystick::nr_b2_byte()` instead. Nothing feeds it, because detecting a
six-button pad needs a physical select-pulse handshake that a host gamepad
cannot produce. It is a reference model with its own test rows, and fixing a
bug in it changes no emulator behaviour.

`iomode.{h,cpp}` implements the NR `0x0B` pin-7 multiplexer, which can be
static, CTC-toggled, or driven by a UART TX line.

## Mouse

`mouse.{h,cpp}` is a Kempston mouse: 8-bit X and Y counters read raw at ports
`0xFBDF` / `0xFFDF`, with active-low buttons and a 4-bit wheel at `0xFADF`. The
ports decode on **twelve** address lines only — A15:A12 are don't-care — so
`IN A,(0x2ADF)` reaches the mouse exactly as it would on hardware.

Two details sit deliberately outside this class. The NR `0x83` bit 5 gate is
enforced one layer up, in the emulator's port handler. And NR `0x0A`
button-reverse and DPI are stored but **not applied**, because neither has an
in-core VHDL consumer — both are concerns of the host adapter rather than of
the machine.

## The host-side layer

Nothing below has a VHDL oracle. Each is judged instead by the shape it lands
in the machine-side class, plus a stated policy.

- **`joystick_dispatcher` / `gamepad_host` / `mouse_dispatcher`** stand in for
  the physical adapters the FPGA expects to be driving its input pins.
  `GamepadHost` owns the SDL controller lifecycle and auto-assigns a pad to the
  first free connector; the two dispatchers translate SDL button, axis, motion
  and wheel events into the machine-side vectors and counters. Both frontends
  use the same classes, so behaviour cannot drift between them.

- **`pointer_capture.h`** (in `src/platform/`) decides what the Qt frontend
  does with each motion event while the pointer is captured — forward the
  delta, re-centre, or ignore the echo of its own warp. SDL does not use it,
  because its relative mode does the equivalent natively.

- **`host_key_latch.h`** (also `src/platform/`) holds two policies, and both
  exist because of the frame boundary. Host key events are delivered *between*
  `run_frame()` calls, so the matrix is sampled once per emulated frame.

  `Latch` is the **minimum-hold** half (issue #120): a press plus its release
  arriving in the same gap would be invisible to the guest, so the latch defers
  such a release until a frame has actually run.

  `Router` is the **serialiser** (issue #268), and it is the half that decides
  when a *press* may land. Holding a press was never enough on its own: the
  next key pressed while the first one's release was still deferred used to be
  asserted on top of it, so the frame sampled a two-key chord the host never
  had. `KEY-SCAN` rejects a two-key chord, so both characters vanished — and a
  tap of Symbol Shift followed by a tap of `P` decoded as Symbol-Shift + `P`,
  producing the *wrong* character rather than none. The router therefore queues
  host events and applies a press only when the queue is empty, no release is
  deferred, and no non-modifier press is still waiting for a frame. The SDL
  modifier block 224..231 — the two ZX shifts and host Alt — never arms that
  last condition, which is what keeps a genuine shift-plus-key chord in a
  single frame; what separates "shift held as a modifier" from "shift tapped on
  its own" is the host's own key-up event and nothing else. Autorepeat presses
  are dropped rather than queued, because SDL forwards them and Qt does not.

  Real hardware has neither problem — it scans at membrane rate and merges
  nothing — so neither policy is a fidelity claim about the Next; both are
  compensation for jnext delivering host input once per frame. One residue is
  deliberately left: two taps of the *same* key inside one gap still reach the
  guest as one continuous press, since separating them needs a released frame
  between the presses, which halves the drain rate for a case no human hand can
  produce.

  `Router::release_all()` is the third policy, and it answers a different
  question: **what if the key-up never arrives at all?** A Qt popup and a focus
  change both take the keyboard away mid-keystroke, and the key-up then goes to
  whatever took it. Opening the File menu with Alt+F swallows the `F` key-up
  *and* the `Alt` key-up, which leaves host Alt asserted in `Keyboard` for the
  rest of the session — after which E, G and C resolve to EDIT, GRAPH and
  CAPS LOCK and those three letters silently stop typing while every other
  letter still works. With an ordinary letter stranded instead, the ROM's own
  auto-repeat fills the BASIC line. `MainWindow::focusOutEvent` therefore
  reports the loss, the frontend releases everything the sink holds, and the
  invariant is that jnext never holds a guest key down whose release it cannot
  observe. The Router tracks what the *sink* holds separately from what the
  *host* holds for this: releasing a key that was never pressed would clear
  matrix bits a compound key shares — `/`, `-` and `=` each write two bits
  directly and all three share the Symbol Shift cell.
  The price is stated rather than hidden — a press not yet shown to a frame is
  dropped instead of held, which is correct, because the user has moved to a
  menu (GH #268).

- **`phantom_typist.{h,cpp}`** types `LOAD ""` for you. Loading a tape on a
  real Spectrum starts with the user typing it and pressing ENTER — or, on a
  128K or +3, accepting the boot menu's default entry — and jnext does that
  itself when a `.tap` is loaded, so a cold machine goes from reset to loading
  with no interaction. The hard part is *when*: BASIC will not accept a
  keystroke for a good fraction of a second after reset, and how long depends
  on the machine. This class is a port of FUSE's answer. It watches port `0xFE`
  reads, accumulating which of the eight half-rows have been selected, and
  treats a full sweep of all eight within one frame as proof that the ROM's
  own scan loop is running. It then waits eight more frames — the first full
  sweep happens while the copyright message is still printing, before the BASIC
  editor is reading — and only then queues the per-machine sequence: `J`,
  Symbol-Shift + `P` twice and ENTER on a 48K, a bare ENTER on the 128K and +3
  where the default menu entry loads a tape itself. It replaced a fixed frame
  delay that was fragile across the three boots. See
  [3.8 Media and loaders](08-media-and-loaders.md) for which formats arm it.

- **`emu_fnkeys.{h,cpp}`** is the odd one out: it *is* a transcription
  (`emu_fnkeys.vhd`), but nothing drives it from a real membrane. The frontends
  call `simulate_mf_fkey_press()` for F3/F5/F6/F7/F8, and
  `Emulator::on_hotkey_*()` for F1/F4/F9/F10 — the four that act on the machine
  itself rather than on the matrix: hard reset, soft reset, Multiface NMI and
  DivMMC NMI.

## Alt is the host namespace, Ctrl is the guest's

Every plain Ctrl+*key* is a real ZX sequence, because **Ctrl is Symbol Shift**.
jnext therefore puts its host shortcuts on Alt, and gives up binding the Alt
keys as guest shift keys to pay for it. That is a deliberate divergence — real
hardware maps Left Alt to EXTEND MODE and Right Alt to GRAPH
(`keymaps.vhd:85-94`) — and jnext follows the FUSE / ZEsarUX convention
instead, with EXTEND on Tab and BREAK on Esc. `Keyboard` treats Alt as a
modifier that is never a ZX key, and latches at press time which table a
scancode resolved through, because the user may well release Alt before the
key.

One consequence matters to anyone touching the GUI. Alt+*letter* is a single
namespace shared by the Qt `QAction` shortcuts **and** the menubar mnemonics,
and Alt+E, Alt+G, Alt+C and Alt+`` ` `` are reserved for the guest's EDIT,
GRAPH, CAPS LOCK and INV VIDEO. Adding a menu on one of those would silently
kill a guest key, so `host_hotkey_test` pins the two sets disjoint — see
[Testing](../04-testing/index.md).

Qt has its own claim on Alt, and jnext declines it. A bare Alt tap normally
moves keyboard focus to the menu bar, where the next letter that is a top-level
mnemonic is swallowed and opens that popup instead of typing. In an ordinary
application that is a convenience; here Alt is part of typing, so the menu bar
is given a `QProxyStyle` reporting `SH_MenuBar_AltKeyNavigation` as off
(`src/debug/menu_bar_alt_nav_qt.h`, installed by both the emulator window and
the debugger window). Alt+*letter* mnemonics are unaffected — Qt opens those
through the shortcut map, not through this hint — and `handle_key()`
additionally feeds the guest nothing at all while a Qt popup owns the keyboard
(GH #268).

**This is a settled decision, taken 2026-09-25, not a provisional workaround.**
The reasoning: Alt is simultaneously a guest modifier (EDIT / GRAPH / CAPS
LOCK) and this project's entire host-hotkey namespace, so pressing it is an
ordinary part of typing here in a way it is not in other applications; every
menu remains reachable by Alt+*letter* and navigable with the arrow keys once
open; and the only thing given up is the tap-Alt-then-arrow convenience, which
in this application was silently eating keystrokes rather than helping. For an
emulator with an overloaded Alt it is a net accessibility improvement, and the
user guide says so to anyone who used the old route.
