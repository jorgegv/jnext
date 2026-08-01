# 3.7 Input

`src/input/` holds a dozen small classes in two clearly separated groups. Some
are transcriptions of FPGA modules and answer to the VHDL. The rest have no VHDL
counterpart at all: they stand in for a physical adapter (the PS/2 driver, the
pad cable) or compensate for an artefact of emulating a frame as an indivisible
unit. **Knowing which group a class is in tells you what its correctness
criterion is** — the VHDL, or a documented host policy.

`src/input` links SDL2 even in the Qt build, because SDL scancodes are the
project's canonical host-key type: `MainWindow::qt_key_to_sdl()` converts Qt keys
so both frontends feed `Keyboard::set_key()` identically.

## The keyboard matrix

`keyboard.{h,cpp}` models the 8 × 5 membrane. `matrix_[row]` is active-low, row
selection comes from the upper byte of the port address (bit *N* clear selects
row *N*), and `read_rows()` returns the 5-bit AND of every selected row — bits
7:5 of the port `0xFE` byte are composed by the caller in `Emulator`, not here.
Three layers apply before the row-select AND, in this order:

1. **Shift hysteresis** — `membrane.vhd:178` holds the Caps-Shift and
   Symbol-Shift column state one extra scan, delaying a release. `tick_scan()`
   advances the two-entry history from the frame loop.
2. **Extended-key fold** (below).
3. **`MembraneStick` fold** (below), matching
   `keyb_col <= keyb_col_i_q AND membrane_stick_col AND ps2_kbd_col`.

## Which compounds the hardware synthesises

This distinction has been got wrong once, so it is worth stating precisely.

**The sixteen extended keys are folded by the hardware itself.** Cursors, DELETE,
EXTEND MODE, CAPS LOCK, GRAPH, TRUE VIDEO, INV VIDEO, BREAK, EDIT and the four
symbol-shifted punctuation keys (`;` `"` `,` `.`) each set a bit in a raw 16-bit
register, and `membrane.vhd:236-240` ANDs that register into the matrix —
**both** halves of the compound. The digit or letter column *and* the Caps-Shift
or Symbol-Shift that goes with it are produced by the membrane, not by the host.

jnext models that path rather than short-cutting it. `set_key()` routes these
host keys to `set_extended_key(id)`, which only touches `ex_matrix_`;
`read_rows()` does the fold, deriving the shift half from the same per-index
split the VHDL uses (every index drives Caps Shift except the four punctuation
ones; EXTEND drives both, which is why it is CS+SS). That buys the two behaviours
a direct compound cannot express: NR `0xB0` / `0xB1` read the raw register, and
NR `0x68` bit 4 cancels the **fold** while leaving the register reporting.
Asserting the compound directly — which jnext used to do — makes both impossible.

**Three compounds are synthesised host-side**, in `s_compound[]`: `/` → SS+V,
`-` → SS+J, `=` → SS+L. On hardware these come from the PS/2 keymap's `SYMBOL`
bit (`keymaps.vhd`, `ps2_keyb.vhd:197`), and jnext does not model the PS/2
protocol at all — SDL key events replace it. Both matrix bits are asserted
directly, with no raw register behind them and nothing to read back.

## Joysticks

`joystick.{h,cpp}` is the machine side: it decodes NR `0x05` into a per-connector
mode (Sinclair 1/2, Cursor, Kempston 1/2, MD3 left/right, I/O mode) and composes
the port `0x1F` and `0x37` reads. Each connector holds a **12-bit raw vector**
whose layout is `zxnext.vhd:3441-3442`.

The constraint that shapes everything above it: **only six of those twelve bits
ever reach a port, two of them conditionally.** U/D/L/R and C/B are readable in
Kempston or MD mode; START and A are driven in MD mode only and hard-tied to 0 in
Kempston; MODE/X/Z/Y reach no port under any mode and are readable only through
NR `0xB2`. So a host pad's four face buttons map onto B, C, A and START — the
reachable bits — and the MD6 latch bits are bound only to spare controls.

`membrane_stick.{h,cpp}` (`membrane_stick.vhd`) is the joystick-to-keyboard
adapter for the Sinclair and Cursor modes, in its own module upstream of the port
`0xFE` read rather than inside `Keyboard`, exactly as the VHDL decomposes it. Its
user-defined keymap is a 64 × 6-bit RAM reprogrammable through NR `0x28` /
`0x29` / `0x2B`, initialised from the FPGA's own `keyjoy_64_6.coe`.

`md6_connector_x2.{h,cpp}` is a faithful transcription of the MD6 scan FSM and is
**not on any live read path** — the NR `0xB2` handler reads
`Joystick::nr_b2_byte()` instead. Nothing feeds it, because the 6-button detect
needs a physical select-pulse handshake a host gamepad cannot produce. It is a
reference model with its own test rows; fixing a bug in it changes no emulator
behaviour.

`iomode.{h,cpp}` implements the NR `0x0B` pin-7 multiplexer — static,
CTC-toggled, or driven by a UART TX line.

## Mouse

`mouse.{h,cpp}` is a Kempston mouse: 8-bit X and Y counters read raw at ports
`0xFBDF` / `0xFFDF`, active-low buttons and a 4-bit wheel at `0xFADF`. The ports
decode on **twelve** address lines only (A15:A12 are don't-care), so
`IN A,(0x2ADF)` reaches the mouse exactly as on hardware. The NR `0x83` bit 5
gate is enforced one layer up, in the emulator's port handler. NR `0x0A`
button-reverse and DPI are stored but **not applied** — neither has an in-core
VHDL consumer; both are host-adapter concerns.

## The host-side layer

Everything below has no VHDL oracle; its correctness criterion is the shape that
lands in the machine-side class, plus a stated policy.

- **`joystick_dispatcher` / `gamepad_host` / `mouse_dispatcher`** stand in for the
  physical adapters the FPGA expects to be driving its input pins. `GamepadHost`
  owns the SDL controller lifecycle and auto-assigns a pad to the first free
  connector; the two dispatchers translate SDL button, axis, motion and wheel
  events into the machine-side vectors and counters. Both frontends use the same
  classes, so behaviour cannot drift between them.
- **`pointer_capture.h`** (in `src/platform/`) decides what the Qt frontend does
  with each motion event while the pointer is captured — forward the delta,
  re-centre, or ignore the warp's own echo. SDL does not use it; its relative
  mode does the equivalent natively.
- **`host_key_latch.h`** (also `src/platform/`) is a minimum-hold latch. Host key
  events are delivered *between* `run_frame()` calls, so the matrix is sampled
  once per emulated frame and a press plus release arriving in the same gap is
  invisible to the guest; the latch defers such a release until a frame has run.
  Its header is explicit that this is **not** hardware equivalence — real hardware
  scans at pixel-clock rate and merges nothing, so two rapid taps collapsing into
  one is a documented residual limitation, not a fidelity claim.
- **`phantom_typist.{h,cpp}`** is a port of FUSE's design: it watches port `0xFE`
  reads until all eight half-rows have been polled — the ROM's full-matrix scan,
  which proves BASIC is ready — and only then queues the per-machine `LOAD ""`
  sequence. It replaces a fixed frame delay that was fragile across 48K, 128K
  and +3 boots.
- **`emu_fnkeys.{h,cpp}`** is the odd one out: it *is* a transcription
  (`emu_fnkeys.vhd`), but nothing drives it from a real membrane. The frontends
  call `simulate_mf_fkey_press()` for F3/F5/F6/F7/F8 and
  `Emulator::on_hotkey_*()` for F1/F4/F9/F10.

## Alt is the host namespace, Ctrl is the guest's

Every plain Ctrl+*key* is a real ZX sequence, because **Ctrl is Symbol Shift**.
So jnext puts host shortcuts on Alt instead and gives up binding the Alt keys as
guest shift keys — a deliberate divergence, since real hardware maps Left Alt to
EXTEND MODE and Right Alt to GRAPH (`keymaps.vhd:85-94`). jnext follows the FUSE
/ ZEsarUX convention instead: EXTEND on Tab, BREAK on Esc. `Keyboard` treats Alt
as a modifier that is never a ZX key, and latches at press time which table a
scancode resolved through, because the user may release Alt before the key.

One consequence for anyone touching the GUI: Alt+*letter* is a single namespace
shared by the Qt `QAction` shortcuts **and** the menubar mnemonics, and Alt+E,
Alt+G, Alt+C and Alt+`` ` `` are reserved for the guest's EDIT, GRAPH, CAPS LOCK
and INV VIDEO. Adding a menu on one of those silently kills a guest key.
`host_hotkey_test` pins the sets disjoint — see [Testing](../04-testing/index.md).
