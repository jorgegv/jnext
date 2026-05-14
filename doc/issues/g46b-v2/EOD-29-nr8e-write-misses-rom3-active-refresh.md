# G46(b)-v2 EOD-29 — NR $8E write handler omits divmmc_.set_rom3_active refresh (root cause + fix)

**Date:** 2026-05-14 (afternoon)
**Branch:** `g46b-investigation-v2`
**Builds on:** [EOD-28](EOD-28-regstate-identical-but-execution-diverges.md)
**Trigger:** EOD-28 Hypothesis B (jnext's M1 fetch returns different bytes than DZRP `read_mem` reports) needed mechanical verification.

## TL;DR — root cause found and fixed

The NR $8E write handler ([src/core/emulator.cpp:2889](../../../src/core/emulator.cpp#L2889))
calls `mmu_.write_nr_8e(v)` (which updates `port_7ffd` and `port_1ffd`
internally per VHDL `:3662-3734`) but **never refreshes
`divmmc_.set_rom3_active(mmu_.sram_rom3())`** afterward — unlike every
other handler that touches `port_7ffd` / `port_1ffd` / `NR $8C`
(emulator.cpp:2443, 2877, 3282, 3399, 5225, 7394).

Consequence at PC=$3CFC (the supervisor's `NEXTREG $8E,$03` boot-time
bank flip): the write switches slot 0/1 to `enNextZX.rom` block 3 but
leaves `divmmc_.rom3_active_` stale (false). The next M1 fetch at
PC=$3D00 evaluates the DivMMC `$3Dxx` wildcard entry-point gate
([divmmc.cpp:419](../../../src/peripheral/divmmc.cpp#L419)), which
requires `rom3_path_eligible = sram_pre_override_2 && sram_pre_override_0 && !layer2_map_read_ && rom3_active_`.
With `rom3_active_=0`, the gate stays cold in jnext — but CSpect's VHDL
combinational `sram_rom3` updates same-cycle with the port writes, so
its $3Dxx automap **fires immediately** at PC=$3D00, overlaying slot
0/1 with DivMMC RAM whose content at offset $3D00 starts with $C9 RET.

jnext fetches the underlying block-3 font data ($00 $00 ... NOPs) and
executes through it, eventually reaching the `$5B00` bank-flip wrapper
trampoline via stale font interpretation. CSpect's overlay RETs
immediately, pops $0448 from the stack, and continues legitimate
supervisor code.

The fix is one line: add `divmmc_.set_rom3_active(mmu_.sram_rom3());`
after `mmu_.write_nr_8e(v);` in the NR $8E write handler. Pattern
matches the OUT ($1FFD),A handler at line 3399.

## How EOD-28 Hypothesis B was verified

### Step 1: PC-checkpoint diff with `g46b_v2_path_checkpoint.py`

Install BPs at jnext's first-30 unique PCs from `JNEXT_G46B_PCTRACE=1`
(post-`$3CFC` first-hit) plus `$5B00` and `$0448`. Run CSpect from
`-debug` paused at `$0000`. Capture order of BP hits.

**jnext path** (per `JNEXT_G46B_PCTRACE=1`):
```
$3CFC → $3D00 → $3D01 → $3D02 → ... → $3D09 (DJNZ) →
$3D1B → $3D1C → ... → $3D54 → $3D75 → ... (40+ PCs in $3Dxx font data)
```

**CSpect path** (per `g46b_v2_path_checkpoint.py`):
```
$3CFC → $3D00 → $0448 → $3CFC → $3D00 → $0448 → ... (tight 3-step cycle)
```

Critically, CSpect SKIPS the entire `$3D01..$3D75` range — even with
fine-grain BPs at every byte from `$3D01..$3D09`. SP rises by 2 between
the `$3D00` hit and the `$0448` hit, evidence of a RET fired *somewhere*
between them.

### Step 2: identify the overlay path that returns RET

In jnext at PC=$3D00, `mem_.read($3D00)` returns `$00` (NOP) — block-3
font data via the post-flip slot[0,1]=06,07 mapping. In CSpect, the
M1 fetch at $3D00 must return `$C9` (RET) for SP to rise by 2 and PC
to land at $0448.

The only mechanism that can change the M1 fetch byte at $3D00 without
changing `mem_.read` is a memory **overlay** that fires per VHDL
combinational logic. Three overlays exist in jnext: AltROM, Multiface,
and DivMMC automap. AltROM is gated by NR $8C bit 7 (= $00 in both
emulators, so off). Multiface is gated by its activation FSM (off
during this boot phase). **DivMMC automap** has a `$3Dxx` wildcard
entry-point (NR $BB bit 7) that fires when:
- bit 7 of NR $BB is set (DEFAULT NR $BB = $CD has bit 7)
- PC is in $3D00..$3DFF
- `rom3_path_eligible` is true

### Step 3: instrument the DivMMC gate with `JNEXT_G46B_AUTOMAP_3DXX_TRACE=1`

New env-gated probe in `divmmc.cpp` at the `$3Dxx` wildcard:

```
G46B-v2 AUTOMAP_3DXX pc=3d00 hit=1 ep1=f2 bit7=1 pre_ovr2=1 pre_ovr0=1
  layer2_map=0 rom3_active=0 main_eligible=1 rom3_eligible=0
  instant_match=0 ...
```

**`rom3_active=0`** in jnext at PC=$3D00. With every other gate input
set correctly, `rom3_path_eligible=0` → wildcard doesn't fire → no
overlay → jnext fetches the underlying block-3 font data.

### Step 4: trace the rom3_active update path

`divmmc_.set_rom3_active(mmu_.sram_rom3())` is the only update site.
`mmu_.sram_rom3()` is a combinational accessor that returns true when
the +3 mode's `a13 && a14` (= `port_1ffd(2) && port_7ffd(4)`) condition
holds — exactly what `NEXTREG $8E,$03` sets at $3CFC. So
`sram_rom3()` returns true post-write.

But `set_rom3_active` isn't called after NR $8E writes. Checked all 7
existing call sites (emulator.cpp:2443, 2877, 3282, 3399, 5225, 7394,
7406) — all on port_7ffd / port_1ffd / NR $8C / NR $50/$51 paths, none
on the NR $8E path. Confirmed by reading
`Mmu::write_nr_8e()` (mmu.cpp:695): it touches port_7ffd, port_1ffd,
port_dffd, then rebuilds slots via `apply_paging_update_()` /
`apply_legacy_rom_slots_()` — no DivMMC refresh.

## The fix

```cpp
nextreg_.set_write_handler(0x8E, [this](uint8_t v) -> uint8_t {
    mmu_.write_nr_8e(v);
    // G46(b)-v2 / EOD-29 fix: NR $8E writes update port_7ffd /
    // port_1ffd internally; push the new sram_rom3 state to DivMmc
    // so its cached gate matches VHDL's combinational drive.
    divmmc_.set_rom3_active(mmu_.sram_rom3());
    return v;
});
```

## Post-fix verification

### AUTOMAP_3DXX probe

```
G46B-v2 AUTOMAP_3DXX pc=3d00 hit=1 ... rom3_active=1 rom3_eligible=1
  instant_match=1 ...
```

`rom3_active=1`, `instant_match=1` — overlay fires correctly.

### PCTRACE post-fix

```
pc=3cfc bytes=ed918e03 AF=0144 BC=0060 ... SP=5bef slot[0,1]=04,05
pc=3d00 bytes=00000000 AF=0144 BC=0060 ... SP=5bef slot[0,1]=06,07
pc=0448 bytes=d3e3cd5d AF=0144 BC=0060 ... SP=5bf1 slot[0,1]=06,07  ← matches CSpect!
pc=044a bytes=cd5d04f5 AF=0144 BC=0060 ... SP=5bf1 slot[0,1]=06,07
pc=045d bytes=e52a565b AF=0144 BC=0060 ... SP=5bef slot[0,1]=06,07
... (legitimate supervisor code: OUT ($E3),A; CALL $045D; PUSH HL; LD HL,(NN); ...)
pc=04dd bytes=edb037c9 AF=0142 BC=0060 ... SP=5bef slot[0,1]=06,07  ← LDIR loop
... (BC=0060 down to BC=0001 across ~96 iterations)
```

jnext now follows CSpect's path exactly through the dispatcher chain.

### Wrapper-entry hits

Before fix: 8+ hits at $5B00 in 6 s of boot.
After fix: **0 hits** at $5B00 in 10 s of boot.

### Test triplet (post-fix)

```
ctest 36/36 • FUSE 1356/1356 • regression 33/0/0
```

No regression. NR $8E test cases (if any) presumably already verified
the write semantics; the missing DivMMC refresh was a latent gap
exposed only by the supervisor's boot-time bank-flip.

## What this fix does NOT solve

jnext's headless screenshot at 15 s post-boot is still blank gray. The
$5B00 wrapper-loop bug is resolved, but the supervisor enters a
different stall downstream — `JNEXT_G46B_RAMCLEAR_TRACE=1` shows ~700
soft-reset hits/sec at `pc=$0000`. Either:

- (a) the supervisor is now legitimately cycling through boot phases
  rapidly, with each phase ending in a controlled reset (this is
  plausible — NextZXOS does soft-reset between supervisor stages); or
- (b) a SECOND VHDL-faithfulness gap exists downstream of the fix —
  some other state that VHDL drives combinationally but jnext caches
  with a missed refresh.

Next-session priority is to characterise the new stall point. If it
involves another DivMMC / AltROM / Multiface gate, the methodology
established here (PC-checkpoint diff against CSpect + gate-input
trace probe) finds it the same way.

## Methodology — what worked

1. **REGDIFF probe (PC=$3CFC, PC=$3D00)** — ruled out CPU register
   divergence at the dispatcher entry point.
2. **PCTRACE probe (post-`$3CFC` linear capture, 120 PCs)** — gave
   jnext's exact execution path through the font-data region.
3. **CSpect DZRP path-checkpoint script** — installed BPs at jnext's
   PCs, captured CSpect's hit order. Immediate discrepancy
   (CSpect skips 40+ PCs jnext visited) named the divergence window
   with no further single-stepping needed.
4. **AUTOMAP_3DXX gate probe in DivMMC** — pinpointed the missing
   `rom3_active_` update.
5. **Cross-reference set_rom3_active call sites** — found all 7
   existing refresh paths, confirmed NR $8E was the only omission.

Each step took <30 minutes. The PC-checkpoint approach was decisive:
**install BPs at jnext's visited PCs, then watch which ones CSpect
ignores.** That asymmetry — jnext visits PCs CSpect skips — is the
canonical signature of a missing memory-overlay activation in jnext.

## Files changed in this commit

- `src/core/emulator.cpp` — fix at NR $8E write handler (1-line +
  comment block).
- `src/peripheral/divmmc.cpp` — new `JNEXT_G46B_AUTOMAP_3DXX_TRACE=1`
  probe at the wildcard gate (env-gated, zero cost when unset).
- `src/cpu/z80_cpu.cpp` — new `JNEXT_G46B_PCTRACE=1` probe at exec_step
  (linear PC capture post-$3CFC, 120 hits).
- `tools/cspect_dzrp/g46b_v2_path_checkpoint.py` — CSpect-side BP
  checkpoint script for PC-path diff.
- `test/nextreg/nextreg_integration_test.cpp` — new test case
  **N8E-DIVMMC-ROM3-REFRESH** in the N8E-RAM-Gate group. Verifies:
  (a) initial `divmmc.rom3_active() == false` after RESET_HARD;
  (b) `divmmc.rom3_active() == true` after `NR $8E = $03` (= same
  bit pattern the NextZXOS supervisor writes at $3CFC). Confirmed
  discriminative: fails without the fix (`port_7ffd=0x10 sram_rom3=1`
  but `rom3_active=0`), passes with the fix.
- `doc/issues/g46b-v2/EOD-29-nr8e-write-misses-rom3-active-refresh.md`
  (this doc).
