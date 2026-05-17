# Task 18 — CSpect vs jnext-bypass Post-Boot Diff

## Method

Both emulators booted from `roms/nextzxos-1gb-fat32fix.img`. CSpect was
launched in the hardened lifecycle (`timeout --kill-after=2s 90s mono
.../CSpect.exe -mmc ...` bracketed by `pkill -9 mono`), allowed ~15 s to
reach the welcome screen, then halted via DZRP `CMD_PAUSE`. The
`task18_cspect_post_boot_diff.py` script dumped the same set of state
the jnext bypass dump exposes.

jnext side: pre-recorded ground truth from the worktree's
`task18_bypass_post_boot_probe` ($t \approx 12$ s after launch).

## Z80 registers — divergence

| Item | jnext bypass | CSpect | Diff |
|------|--------------|--------|------|
| PC   | `$0000`      | `$0C90` | **DIFFERENT execution site** |
| SP   | `$5F31`      | `$FF3F` | Different stack base — CSpect's SP near top of slot 7 |
| AF   | `$4301`      | `$032A` | Different (different code path active) |
| BC   | `$023B`      | `$0869` | Different |
| DE   | `$2E5F`      | `$0002` | Different |
| HL   | `$2E50`      | `$5C3B` | CSpect's HL points at the FLAGS sysvar — actively walking the sysvar table |
| IX   | `$2A7A`      | `$F700` | Different |
| IY   | `$5C3A`      | `$5C3A` | ✓ — canonical Spectrum BASIC sysvars pointer in both |
| IFF/IM | 1/1, IM=1 | -/-, IM=1 | ✓ where comparable |
| I    | `?`          | `$09`   | (jnext bypass doesn't expose I in dump; CSpect's I=$09 → ISR at $09xx) |

**Bottom line: jnext is in NextZXOS's "I'm CLS'd and idle" trap. CSpect
is still actively executing user-visible boot code at $0C90.**

## MMU slots (NR $50..$57) — KEY DIVERGENCE

| Slot | Range          | jnext bypass | CSpect | Note |
|------|----------------|--------------|--------|------|
| 0    | `$0000-$1FFF`  | `$FF`        | `$FF`  | ✓ (NR ROM range) |
| 1    | `$2000-$3FFF`  | `$FF`        | `$FF`  | ✓ |
| 2    | `$4000-$5FFF`  | `$0A`        | `$0A`  | ✓ (display bank 5, page 0) |
| 3    | `$6000-$7FFF`  | `$0B`        | **`$11`** | **✗ — CSpect has bank `$11` here, jnext has `$0B`** |
| 4    | `$8000-$9FFF`  | `$04`        | `$04`  | ✓ |
| 5    | `$A000-$BFFF`  | `$05`        | `$05`  | ✓ |
| 6    | `$C000-$DFFF`  | `$0E`        | **`$00`** | **✗ — CSpect has bank `$00` (= ROM-0!), jnext has `$0E` (workspace)** |
| 7    | `$E000-$FFFF`  | `$0F`        | **`$01`** | **✗ — CSpect has bank `$01` (= ROM-1), jnext has `$0F`** |

This is the single biggest visible divergence. **CSpect has executable
ROM/code mapped at slots 6/7, NOT the NextZXOS workspace.** And slot 3
is also different. CSpect's PC=$0C90 falls in **slot 0** (within the ROM
view, not the workspace), and SP=$FF3F is in slot 7 = bank `$01`.

This implies CSpect is running its **post-CLS welcome-banner drawing
code from a different ROM page** — likely NextZXOS's "BASIC welcome"
routine sitting in a slot the bypass commit doesn't map.

## NextRegs — divergences

| NR  | Name                           | jnext bypass | CSpect | Diff |
|-----|--------------------------------|--------------|--------|------|
| $03 | Machine type / config          | `$B0`        | **`$33`** | **✗** — wildly different |
| $07 | Turbo / CPU speed              | `$03` (28 MHz) | **`$33`** | **✗** — jnext at 28 MHz, CSpect's `$33` undocumented bits |
| $0A | Periph 2 settings              | (default `$00`?) | `$11`   | ✗ |
| $0E | Bus master                     | (default)    | `$00`  | ✓-ish |
| $12 | Layer 2 active page            | `$09`        | `$09`  | ✓ |
| $13 | Layer 2 shadow page            | `$09`        | `$09`  | ✓ |
| $14 | Global transparency colour     | (default `$00`?) | **`$E3`** | **✗** |
| $40 | Palette index                  | (default)    | `$1F`  | ✗ |
| $4A | Transparency fallback colour   | (default)    | `$00`  | ✓ |
| $4B | Sprite transparency index      | (default)    | `$E3`  | ✗ |
| $4C | Tilemap transparency index     | (default)    | `$0F`  | ✗ |
| $50..$55 | (legacy slots, mirror of slots above) |||
| $56 | Slot 6 bank                    | `$0E`        | **`$00`** | **✗** (matches slot disagreement above) |
| $57 | Slot 7 bank                    | `$0F`        | **`$01`** | **✗** |
| $68 | Display control                | `$00`        | `$00`  | ✓ |
| $69 | Display layer enable           | `$00`        | `$00`  | ✓ |
| $7F | (user reg)                     | (default)    | `$FF`  | (default) |
| $80..$85 | Expansion bus / multiface | (defaults)   | mixed `$00`/`$FF` | likely defaults-mostly |
| $8A | Spare                          | (default)    | `$00`  | ✓ |
| $8C | Alt-ROM activate               | `$00`        | `$00`  | ✓ |
| $8E | +3 paging                      | `$02`        | **`$00`** | **✗ — CSpect has NextZXOS-default `$00`; jnext has `$02` from initial CLS** |
| $8F | Memory mapping mode            | (default)    | `$00`  | ✓ |
| $B8 | DivMMC trigger 1               | (default)    | `$82`  | ✗ (likely the post-firmware divmmc state) |
| $B9 | DivMMC trigger 2               | (default)    | `$00`  | ✓ |
| $BB | (multiface)                    | (default)    | `$F2`  | ✗ |
| $C0 | Interrupt control              | (default `$00`?) | **`$08`** | **✗ — CSpect has bit 3 set = ULA INT NMI return** |
| $D8 | (Joystick / kempston?)         | (default)    | `$01`  | ✗ |

The most operationally significant NR differences are at **$03 / $07 /
$14 / $C0 / $56 / $57 / $8E**.

## Memory dumps — divergences

### Slot 0 ROM view at $0000 (32 bytes)

| | jnext bypass | CSpect |
|---|---|---|
| `$0000` | (jnext: no dump captured at this addr in baseline) | `F3 C3 EF 00 45 44 09 02 C3 3B 10 2A 2E 2A FF 00 EF 10 00 C9 C3 EF 00 00 C3 80 3E 3C 44 49 52 BE` |

CSpect's slot 0 starts with `F3 C3 EF 00` = `DI; JP $00EF` — the
canonical 48.rom / enNextZX entry. Then `45 44 09 02 ...` = ASCII `"ED"`
+ `09 02` — this is the **embedded esxDOS / NextZXOS metadata at
$0004-$000F**. The `$3C 44 49 52` (`<DIR`) at $001D is the start of an
internal jump table the firmware uses. **jnext bypass should be running
the same ROM — but no diff captured. ASSUME ✓ (same ROM).**

### Attribute area $5800-$5AFF (768 bytes)

| Range | jnext bypass | CSpect |
|-------|--------------|--------|
| `$5800-$59FF` | all `$38` (paper 7, ink 0, no BRIGHT) — standard CLS | all `$38` ✓ |
| `$5A00-$5A5F` | all `$38` | **all `$7A`** (paper 7, ink 2 = RED, BRIGHT 1) — **CSpect's "(c) 1986/2014" copyright bar** |
| `$5A60-$5A9F` | all `$38` | all `$38` ✓ |
| `$5AA0-$5AAF` | all `$38` | **all `$78`** (paper 7, ink 0, BRIGHT 1) — CSpect's NextZXOS "READY"-style line |
| `$5AB0-$5ABF` | all `$38` | `78 78 78 78 78 78 78 78 78 78 50 56 66 65 45 45` — **mixed attrs (per-cell colours), likely a status field with several colour cells** |
| `$5AC0-$5AFF` | all `$38` | all `$38` ✓ |

**The bottom 3 rows of the attribute area in CSpect carry the
NextZXOS-on-+3 welcome banner colours.** jnext has those rows blank.

### Pixel area $4000-$40FF (256 bytes — first text rows)

| | jnext bypass | CSpect |
|---|---|---|
| `$4000-$4007` | all `$00` | all `$00` (top blank) |
| `$4008-$4016` | all `$00` | `03 FF FF FF FF FF FF FF FF FF FF FF FF FF FF F0` — **CSpect has drawn pixel content (a long horizontal bar)** |
| `$4017+`     | all `$00` | all `$00` |

This confirms CSpect has rendered actual content into the framebuffer.
jnext has not.

### BASIC sysvars at $5C00 (128 bytes)

| Offset | Sysvar | jnext bypass | CSpect | Notes |
|--------|--------|--------------|--------|-------|
| `$5C00`-`$5C07` | KSTATE | `FF 00 00 00 FF 00 00 00` | `FF 00 00 00 FF 00 00 00` | ✓ |
| `$5C08`-`$5C0F` | RAMTOP-ish | `00 23 02 00 00 00 00 00` | `00 23 02 00 00 00 00 00` | ✓ first 6 bytes; bytes 14-15 diverge (`00 00` vs `12 00`) — extra init word |
| `$5C0E`-`$5C1F` | (FRAMES + counters) | `01 00 06 00 0B 00 01 00 01 00 06 00 10 00 00 00` | `12 00 01 00 06 00 0B 00 01 00 01 00 06 00 10 00 15 00` | shifted by 2 — **CSpect has an extra leading word at $5C0E** = `$0012` and a trailing word at $5C1E = `$0015` |
| `$5C20`-`$5C36` | zeros | all `$00` | all `$00` | ✓ |
| `$5C37`-`$5C3F` | end of frames-block | `3C 40 00 FF 1C 01 53 FF 00` | **`3C 40 00 FF DC 01 49 FF 00`** | $5C3B (**FLAGS**) `$1C` vs **`$DC`**, $5C3D (**ERR_NR**) `$01` vs `$01` (same), $5C3E (**RAMTOP-related**) `$53` vs **`$49`** |
| `$5C40`-`$5C47` | (zeros mostly) | `00 00 00 00 00 00 00 00` | **`00 00 7C 01 80 78 00 01`** | **CSpect has more init at $5C42-$5C47** ($5C42 = `$017C`, $5C44 = `$7880`, $5C46 = `$0100`) |
| `$5C48` | BORDCR | `$38` | `$38` | ✓ |
| `$5C49`-`$5C5F` | (PROG / VARS / etc. pointers) | mostly zero / a few `CB 5C` | **fully populated: `00 88 86 D7 5E B6 5C B6 5C FA 5D D9 5E B5 62 5E 89 CE 86 D8 5E 97`** | **CSpect has full BASIC environment set up** (PROG, VARS, E_LINE, WORKSP, STKBOT, STKEND, MEM, all live) |
| `$5C60`-`$5C7F` | sparse | sparse, also differs heavily | | |
| `$5C7B` | LAST_K | `$58` | `$EF` (NextZXOS scan-code) | ✗ |

**The KEY sysvars table divergence**: at `$5C3B` (FLAGS) jnext has
`$1C` (K-cursor, no L-mode, 128-cursor mode) while CSpect has **`$DC`**
(K-cursor, L-mode active, screen scroll enabled, full editor mode). And
the entire PROG/VARS/E_LINE/WORKSP pointer chain at `$5C49-$5C5B` is
**populated in CSpect, zeros in jnext.**

## Diagnosis — single concrete missing step

**CSpect's NextZXOS has run a chunk of post-CLS BASIC-environment init
that jnext-bypass has not.** Two evidence chains converge:

1. **Slot 6/7 mapping**: CSpect's slots 6/7 = banks `$00`/`$01` (= ROM
   pages, with executable code in CSpect's PC=$0C90 site living
   somewhere in slot 0). jnext-bypass has the **NextZXOS workspace**
   (banks `$0E`/`$0F`) mapped at slots 6/7. That mapping in jnext is
   what comes out of the `enNxtmmc.rom` divmmc init + the bypass NR $03
   write. **The bypass commit short-circuits NextZXOS into "I'm at
   workspace mode" before it has run the "set up BASIC editor
   environment" code.**

2. **Sysvars at `$5C42-$5C5F`**: CSpect has the PROG / VARS / E_LINE /
   WORKSP / STKBOT / STKEND / MEM pointer chain set up. jnext-bypass
   has those bytes still at zero. **NextZXOS's `NEW`/`MAIN-4` style
   sysvar-initialization routine has not run in jnext-bypass.**

The single most likely missing step: **NextZXOS's "MAIN-4 / NEW"
sysvars-init routine** is in the ROM bank that the bypass commit
selected away when it wrote NR $8E = $02. CSpect has NR $8E = `$00`
(the NextZXOS default for +3 paging), which keeps the **NextZXOS init
ROM** mapped, not the **BASIC-2 alt ROM**.

### Concrete recommendation for the next emulator.cpp edit

Change the bypass NR $8E commit from `$02` to **`$00`** (and double-check
NR $03 too — jnext's `$B0` vs CSpect's `$33` is a 7-bit difference that
likely reflects the bypass committing "config-mode complete" prematurely):

```
// In emulator.cpp bypass-firmware-fw apply_post_firmware_state():
//   OLD: nextreg_->write($8E, $02);   // map BASIC-2 alt ROM
//   NEW: nextreg_->write($8E, $00);   // leave NextZXOS init ROM mapped;
//                                     // let it run its own MAIN-4 init
//                                     // which populates $5C42-$5C5F and
//                                     // draws the welcome banner.
```

Secondary follow-ups (after the $8E fix, in priority order):

1. **NR $03 = $33** (CSpect) vs **$B0** (jnext). The bypass commit
   writes $B3 (per the doc); the difference between `$33` and `$B3`
   is bit 7 (machine-config locked). **CSpect leaves bit 7 clear so
   NextZXOS can finish its own config-mode dance.** Try writing
   `$33` instead of `$B3` in the bypass.

2. **NR $07 = $33** (CSpect) vs **$03** (jnext, 28 MHz). The high
   nibble `$3x` is documented; the low `$x3` byte is the speed
   selector. Try `$33` to match.

3. **NR $14 = $E3** in CSpect, default `$00` in jnext. This is the
   global transparency colour and is purely cosmetic — set after
   $8E fix is confirmed.

4. **Slot 6/7 bank reassignment**: after the NR $8E = $00 fix, the
   correct slot 6/7 banks should self-consistent. If the bypass still
   maps slots 6/7 to `$0E`/`$0F` manually, that write must be
   removed or moved before the NR $8E commit so the OS sees the
   default $00/$01 (ROM) mapping at boot-time and can re-page as it
   wishes.

## Caveats

- DZRP capture pauses CSpect at a non-deterministic point in its idle
  loop — the PC=$0C90 is wherever the OS was when we halted. A
  re-run might catch it at a different PC; that's fine, the **state**
  (sysvars, attributes, slots) is steady-state.
- We did not single-step CSpect to identify exactly which code wrote
  `$5C42-$5C5F`. That's a follow-up for after the $8E fix lands.
