# Pass-7 Blind Re-Audit — NMI / Multiface / Port Subsystems

## Verdict

**Convergence NOT yet reached.** Three additional class-(a) reset-preservation
bugs found and fixed (NR $05, NR $09, NR $08-low / NR $81), all matching the
**same shape** Pass-6 identified for NR $06: a VHDL signal whose reset block
re-asserts only some bits while jnext's reset path unconditionally re-applies
the whole byte. The recurring pattern strongly suggests at least one more pass
is warranted — probably broader, sweeping every NR cached register against the
VHDL reset block.

| Pass | Class-(a) bugs found |
| --- | --- |
| 1 | many (initial pass) |
| 2 | several |
| 3 | several (cross-subsystem NR routing) |
| 4 | several |
| 5 | 4 (NR $86-$89 / NR $80 fold / NR $7F / NR $8C fold) |
| 6 | 1 (NR $06 reset preservation) |
| **7** | **3 (NR $05, NR $09, NR $08-low + NR $81 — same shape as Pass-6)** |

The Pass-6 finding was not an isolated bug. It was the first instance of a
*systemic* shape: jnext's `regs_.fill(0)` plus per-register re-application
clobbers VHDL signals that survive reset. Pass-7 caught three more instances
in the NMI/MF/Port scope; a future pass should sweep the rest of the NR
surface (NR $0A, $11, $14, $4A, $4B, $4C, $43, $C0, …) for the same shape.

## Methodology

1. Scope: `nmi_source.{cpp,h}`, `multiface.{cpp,h}`, `nextreg.{cpp,h}`,
   `port_dispatch.{cpp,h}`, plus the NR-touching surface of `emulator.cpp`.
2. **Per-prompt directive: focus on the same-shape-as-NR-$06 search.** For
   every NR register flagged in earlier-pass reports as "class (b) — same
   shape", re-evaluate against the VHDL reset block. Promote any genuine
   match to class (a) and fix.
3. Walk the multiface FSM (multiface.vhd:120-196) signal-by-signal against
   the C++ implementation. (Convergence: no new findings — code matches
   VHDL line-by-line, including the priority cascades, the
   `port_io_dly`-gating disjunctions, and the `invisible_eff = invisible
   AND NOT mode_48` derived signal.)
4. Walk the NMI FSM (zxnext.vhd:2089-2170) state-by-state, including the
   gate conditions on `nr_03_config_mode`, the
   `nmi_mf > nmi_divmmc > nmi_expbus` priority chain, the FETCH→HOLD M1
   trigger, the HOLD→END `nmi_hold` derivation (mf branch / divmmc branch /
   expbus FALL-THROUGH-TO-COMBINATIONAL-PRODUCER), and the END→IDLE
   `cpu_wr_n='1'` advance approximation. (Convergence: no new findings —
   the C++ collapsed-into-`recompute_()` model is already documented as a
   coarse but conservative approximation.)
5. NR readback completeness audit (spot-checked NR $00-$10 / $80-$89 /
   $C0-$D9). All readbacks match the VHDL `port_253b_dat <= ...` formula
   except for one minor finding catalogued as class (b) below.
6. NR write/read cross-subsystem sourcing audit. (Convergence: no new
   findings.)
7. Walked `port_dispatch.cpp` line-by-line. (Convergence: no new findings —
   the most-specific-mask-wins dispatch is a faithful approximation of VHDL
   one-hot decode, and the IO observer hook is correctly invoked
   unconditionally before handler dispatch for both reads and writes.)

## Findings

### Class (a) — fixed in this pass

#### F1. NR $05 reset clobbers `nr_05_5060` and `nr_05_scandouble_en`

**VHDL oracle**
- Signal declarations: `zxnext.vhd:1105-1106, 1302-1303` — only **initial
  values**, no reset clauses (`nr_05_joy0 := "001"`, `nr_05_joy1 := "000"`,
  `nr_05_5060 := '0'`, `nr_05_scandouble_en := '1'`).
- Dedicated processes for the 5060 and scandouble bits at
  `zxnext.vhd:5832-5854` have **no `if reset='1'` branch** — only writes
  via `nr_05_we` and the F2/F3 hotkey toggles change them.
- No `nr_05_*` assignment appears in the reset block at zxnext.vhd:4930+.

**C++ behaviour pre-fix**
- `nextreg.cpp:122` — `regs_[0x05] = 0x41;` (unconditional, on every reset).
- The NR $05 read handler at `emulator.cpp:996-1010` pulls bits 2 (5060)
  and 0 (scandouble) directly from `cached(0x05)`. The Joystick subsystem
  owns joy0/joy1.
- Result: software that toggles the 5060 / scandouble bits via NR $05
  write or F2/F3 hotkeys, then issues NR $02 ← 0x01 (soft reset), silently
  loses those bits.

**Fix** (`src/port/nextreg.cpp`)
- Capture `saved_05 = regs_[0x05]` before `regs_.fill(0)`. On first boot
  (saved_05 == 0 from `regs_{}` value-init), substitute the power-on
  default 0x41. On subsequent resets, the saved byte is preserved verbatim.

#### F2. NR $09 reset clobbers `nr_09_psg_mono`, `nr_09_hdmi_audio_en`, `nr_09_scanlines`

**VHDL oracle**
- Signal declarations: `zxnext.vhd:1121-1123, 1304` — initial values only
  for `nr_09_psg_mono = "000"`, `nr_09_hdmi_audio_en = '1'`,
  `nr_09_scanlines = "00"`, plus `nr_09_sprite_tie = '0'`.
- The reset block at `zxnext.vhd:4937` re-asserts **only**
  `nr_09_sprite_tie <= '0'`. The other three signals survive reset.
- The dedicated `nr_09_scanlines` process at `zxnext.vhd:5856-5865` has
  no `if reset='1'` branch.

**C++ behaviour pre-fix**
- `nextreg.cpp:97` — `regs_.fill(0);` clears the NR $09 cache. There was
  no explicit re-application of NR $09.
- The NR $09 read handler at `emulator.cpp:911-918` pulls bits 7:5 / 2 /
  1:0 from `cached(0x09)`; bit 4 from `sprites_.mirror_tie()`.
- Result: software that writes NR $09 (psg_mono / hdmi_audio_en /
  scanlines), then issues NR $02 ← 0x01, silently loses those bits.

**Fix** (`src/port/nextreg.cpp` + `src/core/emulator.cpp`)
- NextReg: capture `saved_09 = regs_[0x09]` before `regs_.fill(0)`,
  re-apply `(saved_09 & 0xEF)` afterwards (forces bit 4 = 0 to mirror the
  VHDL reset of `nr_09_sprite_tie`; preserves bits 7:5 / 3 / 2 / 1:0).
- Emulator: in init() after subsystem resets, fan out the preserved
  cached(0x09) byte to `sprites_.set_mirror_tie(false)` and
  `turbosound_.set_mono_mode(...)` so the live subsystem state matches the
  preserved cache.

#### F3. `nr_08_stored_low_` reset clobbers preserved NR $08 bits 5/4/3/2/1/0

**VHDL oracle**
- Signal declarations: `zxnext.vhd:1115-1120` — initial values only for
  `nr_08_psg_stereo_mode = '0'`, `nr_08_internal_speaker_en = '1'`,
  `nr_08_dac_en = '0'`, `nr_08_port_ff_rd_en = '0'`,
  `nr_08_psg_turbosound_en = '0'`, `nr_08_keyboard_issue2 = '0'`. Plus
  `nr_08_contention_disable = '0'` (initial-value).
- The reset block at `zxnext.vhd:4935` re-asserts **only**
  `nr_08_contention_disable <= '0'`. The other six signals (which form
  exactly the bits stored in `nr_08_stored_low_`) survive reset.

**C++ behaviour pre-fix**
- `emulator.cpp:172` — `nr_08_stored_low_ = 0x10;` (unconditional, on
  every reset).
- The NR $08 read handler at `emulator.cpp:3308-3314` pulls bits 5..0
  from `nr_08_stored_low_`, bit 7 from `mmu_.paging_locked()`, bit 6
  from `mmu_.contention_disabled()`.
- Result: software that enables the DAC, internal speaker, TurboSound,
  port-FF read-enable, or keyboard issue-2 mode via NR $08, then issues
  NR $02 ← 0x01, silently loses the gate.

**Fix** (`src/core/emulator.cpp`)
- Gate the `nr_08_stored_low_ = 0x10` re-init on `!preserve_memory`
  (i.e. only on hard reset / first boot). Restore `dac_enabled_` from
  `nr_08_stored_low_ & 0x08` so the DAC stays enabled across soft reset.
- After subsystem resets in init(), fan out the preserved byte to
  `turbosound_.set_stereo_mode(b5)`, `turbosound_.set_enabled(b1)`, and
  `mixer_.set_exc_i(beep_spkr_excl())` so live subsystem state matches
  the preserved shadow.

#### F4. `nr_81_` reset clobbers preserved NR $81 bits

**VHDL oracle**
- Signal declarations: `zxnext.vhd:1221-1225` — initial values only for
  `nr_81_expbus_ula_override = '0'`, `nr_81_expbus_nmi_debounce_disable
  = '0'`, `nr_81_expbus_clken = '0'`, `nr_81_expbus_fdc = '0'`,
  `nr_81_expbus_speed = "00"`.
- **None of `nr_81_*` appear in any reset block** anywhere in
  zxnext.vhd. All five fields survive both hard and soft reset.

**C++ behaviour pre-fix**
- `emulator.cpp:3228` — `nr_81_ = 0;` (unconditional, on every reset).
- Result: software that sets the ExpBus NMI-debounce-disable bit (5)
  via NR $81, then issues NR $02 ← 0x01, silently loses the bit. The
  `nmi_source_.set_expbus_debounce_disable()` propagation in the NR $81
  write handler also stops reflecting the live VHDL state.

**Fix** (`src/core/emulator.cpp`)
- Gate the `nr_81_ = 0` re-init on `!preserve_memory`. After init,
  unconditionally re-propagate `nr_81_ & 0x20` to
  `nmi_source_.set_expbus_debounce_disable(...)` so the ExpBus NMI gate
  reflects the preserved value.

### Class (b) — flagged for next-pass attention (NOT fixed here)

#### B1. NR $0A — Mouse subsystem reset clobbers `mouse_button_reverse` / `mouse_dpi`

`src/input/mouse.cpp:34-46` `KempstonMouse::reset()` unconditionally sets
`button_reverse_ = false; dpi_ = 0x01;`. Per VHDL `zxnext.vhd:1127-1128`,
`nr_0a_mouse_button_reverse` and `nr_0a_mouse_dpi` are initial-only
signals with no reset clause; both survive reset. The NR $0A read handler
in emulator.cpp:963-972 pulls these from `mouse_.button_reverse()` and
`mouse_.dpi()`, so the bits are correctly surfaced as zeros after reset
even though VHDL would preserve them.

**Out of pass-7 scope** (`src/input/mouse.cpp` is not in the listed file
set). Attach to a future input-subsystem audit. Same shape as Pass-6 NR
$06.

#### B2. Joystick subsystem reset clobbers `joy0_mode_` / `joy1_mode_`

`src/input/joystick.cpp:11-25` `Joystick::reset()` resets `joy0_mode_ =
Kempston1; joy1_mode_ = Sinclair2;`. Per VHDL `zxnext.vhd:1105-1106`,
`nr_05_joy0` and `nr_05_joy1` are initial-only signals with no reset
clause. Software that selects a different joystick mode and issues a
soft reset silently reverts. Same shape, **out of scope**.

#### B3. NR $11 / NR $14 / NR $4A / NR $4B / NR $4C / NR $43 same-shape candidates

Visual scan of zxnext.vhd reset block at lines 4930-5060 vs cached NR
storage suggests several more registers that are partially-reset in VHDL
but fully-cleared by `regs_.fill(0)`. They were NOT walked in detail
because their read handlers may bypass cached() or the registers may
not be exercised by the boot path. Flag for a future broad sweep.

#### B4. NR $0E / NR $0F / NR $00 / NR $01 / NR $10 — read-only registers writable by software

NextReg::write falls through to the raw stored byte for any register
without a write_handler. Software writes to NR $0E (sub_version),
NR $0F (board_issue), NR $00 (machine_id), etc. corrupt the cache. The
read handler returns the constant only if explicitly registered;
otherwise reads return the corrupted byte. This is an architectural
issue separate from reset preservation. Flag for triage.

### Class (c) — investigated, no bug

- **NMI FSM walk** (zxnext.vhd:2089-2170 vs nmi_source.cpp): clean. The
  `recompute_()` ordering correctly captures the IDLE-state strobe before
  advancing to FETCH (matching VHDL combinational `nmi_mf_button` /
  `nmi_divmmc_button`).
- **Multiface FSM walk** (multiface.vhd:120-196 vs multiface.cpp): clean.
  Pre-edge state snapshots, port_io_dly gating, the `mf_enable` priority
  cascade (reset > fetch_66+mreq > disable_rd|retn_seen > enable_rd), and
  the `invisible_eff = invisible AND NOT mode_48` derivation all match.
- **NR $02 readback / accept-cause / reset-type FSM** (nmi_source.cpp +
  emulator.cpp NR $02 handlers): clean post-Pass-3.
- **`mf_port_en()`** (multiface.cpp:206-214): theoretical staleness
  between two `clock_edge_` calls (the VHDL combinational signal would
  fall back to '0' as soon as port_mf_enable_rd_i drops, which the C++
  approximation doesn't model). No real consumer of `mf_port_en()` —
  the NR $0A read handler and the per-LSB MF readback handlers gate
  directly on `multiface_.invisible_eff()` and `mf_type()`, not on
  `mf_port_en()`. NOT a bug in practice.

## Test results

```
$ LANG=C cmake -B build -DENABLE_QT_UI=ON     # configure: success
$ LANG=C cmake --build build -j$(nproc)        # build: success
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   1.20 sec
```

Regression suite: 32/33 pass (one pre-existing parallax-demo failure that
also reproduces on the parent commit `e2cf262` — not introduced by this
pass).

## Honest convergence verdict

**NOT CONVERGED.** Pass-6 found 1 class-(a); Pass-7 found 3 class-(a) of
the **same shape** — all systematic NR-reset-preservation bugs. Two more
out-of-scope same-shape candidates were spotted in `mouse.cpp` /
`joystick.cpp` and a third group of in-scope candidates (NR $11, $14, $4A,
$4B, $4C, $43) was flagged but not exhaustively walked. The pattern is
not yet exhausted in the broader NextReg surface.

**Recommendation for Pass-8**:
1. Sweep every NR register with a stored cache against the VHDL reset
   block at zxnext.vhd:4930-5060. For each register, classify reset
   coverage as full / partial / none and reconcile with `regs_.fill(0)` +
   per-register re-init.
2. Extend the audit to `mouse.cpp` and `joystick.cpp` (Class B1/B2 above).
3. Audit the NR write fallback path: read-only registers should reject
   software writes (or at least cache the constant verbatim) rather than
   accepting raw last-write semantics.

Pass-7 made meaningful progress (3 fixes, all systemic), but the
descent is **not yet complete**. A Pass-8 with a broader sweep lens is
warranted.
