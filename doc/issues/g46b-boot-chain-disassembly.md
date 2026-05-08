# G46(b) Boot-chain ROM Disassembly Reference

**Generated:** 2026-05-08
**Branch:** `g46b-investigation` HEAD `8b31225`
**Working dir:** `/home/jorgegv/src/spectrum/jnext`
**Raw dasm files:** `doc/issues/dasm/<rom>-bank<N>.asm` (z88dk-dis -mz80n output)

This document is a **static-analysis** complement to the dynamic instrumentation
work in `project_g46b_2026_05_08_eod17_*.md`. The goal is ground truth on what
the supervisor's code path **actually is**, so we can reconcile against what
jnext does dynamically.

---

## TL;DR — Headline findings

1. **NextZXOS does not draw a "welcome menu"** in the way the project memo
   has been framing it. There is **no "NextZXOS"/"Welcome"/"©" string anywhere**
   in any boot-chain ROM. The "welcome menu" is the **TBBlue boot module's**
   `Press SPACEBAR for menu / Press C for extra cores` screen drawn by
   `tbblue-boot.bin` PC=$6B85-$6BA3, which jnext **already renders correctly**
   per EOD-17. NextZXOS's first user-visible UI is the file
   browser / dot-command shell (bank 0 has "EDIT=up EXTEND=more BREAK"
   strings) reached **only after the supervisor's main dispatch loop runs**,
   which is the post-handoff dead zone we currently observe.

2. **`enNextZX.rom` bank 1 ≠ "48K BASIC"** — earlier memos suggested otherwise
   but only **bank 3** is close (6423/16384 bytes differ from 48.rom). Bank 1
   starts `00 C3 00 3F` = `NOP; JP $3F00` and is **the central cross-bank
   dispatcher**: every bank-2 → bank-0 (or vice versa) call goes through bank
   1's $3E80/$3F00 wrappers. PC=$1411 (where supervisor "settles") is in the
   middle of the **canonical 48K-BASIC `INPUT`-statement cursor-flash routine**
   at $140C-$1416 (`LD HL,$5C91; LD A,(HL); RRCA; XOR (HL); AND $55; XOR (HL);
   LD (HL),A; RET` — toggles FLASH bit of `MASK_T` BASIC sysvar). This routine
   is hooked from the IM 1 ISR. **The supervisor is stuck in the "BASIC INPUT
   statement waiting for keypress" hot loop**, NOT making forward progress
   into the welcome menu.

3. **The bank-2 supervisor's welcome-screen entry IS already running** —
   bank-2 PC=$088D `DI; LD SP,$5BFF; LD HL,$5800; LDIR (HL)<-L` (= clear
   attrs to $00 = black on black) **followed by** `XOR A; OUT ($FE),A` (= black
   border) is the **CAUSE of the black screen** observed in EOD-14c after the
   P42 band-aid was removed. The supervisor did the screen clear and is
   waiting for the next phase.

4. **NR $43 = $03 at $0B2B is a config-restore, not a deliberate write.**
   PC=$0B06-$0B2F in bank 0 reads 7 bytes from `(HL)=$D5ED` and restores
   NR $15, $14, $5C8D, $5B62, $5C48, then border via OUT ($FE), then NR $43.
   The `$03` value is whatever was saved in the 7th byte of that block — likely
   reflecting the **TBBlue boot's saved palette state** before the soft-reset
   handoff, which is real-Next-faithful.

5. **NextZXOS uses TWO RAM-resident jump tables** for cross-bank dispatch.
   The bank-flip wrapper at `$5B00` (LDIR'd from bank 0's `$0091` — see
   PC=$00E3 in bank 0: `LD HL,$0091; LD DE,$5B00; LD BC,$0052; LDIR`) is
   the supervisor's main bank-bridge. **`$5B00..$5B52` is canonical RAM code**
   — if it's been zeroed (by a soft-reset that wasn't supposed to clear it,
   or by a bug in our reset sequence), nothing further can run. This is
   worth tracing with a $5B00..$5B52 watcher.

---

## Section 1 — File inventory and provenance

### 1.1 — ROMs extracted from SD image `roms/nextzxos-1gb-fat32fix.img`

Partition starts at byte offset 32256 (sector 63). All extracted via
`mcopy -i img@@32256` to `/tmp/g46b-dasm/extracted/`:

| ROM file | Size | MD5 | SD path | Notes |
|---|---|---|---|---|
| `enNextZX.rom` | 65536 | `8c34d957fb2cbbd0be423cea3b370fd5` | `/MACHINES/NEXT/enNextZX.rom` | NextZXOS supervisor — 4 × 16 KB banks (multi-bank ROM, all banks load at $0000) |
| `enNextMf.rom` | 8192 | `5923a41922b5af965b4a71cc26c871ba` | `/MACHINES/NEXT/enNextMf.rom` | Multiface NextOS firmware (8K, single bank at $0000) |
| `enNxtmmc.rom` | 8192 | `81b6ed2a3dec124eb58ed023c66ea154` | `/MACHINES/NEXT/enNxtmmc.rom` | DivMMC firmware (8K, single bank, runs in MAPRAM region) |
| `48.rom` | 16384 | `4c42a2f075212361c3117015b107ff68` | `/MACHINES/NEXT/48.rom` | 48K BASIC (Sinclair 1982, well-documented elsewhere) |
| `128.rom` | 32768 | `85fede415f4294cc777517d7eada482e` | `/MACHINES/NEXT/128.rom` | 128K BASIC, 2 × 16 KB banks |
| `plus3.rom` | 65536 | `7e00ed3562abfd188d0d4da03e80bc0a` | `/MACHINES/NEXT/plus3.rom` | +3 BASIC, 4 × 16 KB banks |
| `TBBLUE.FW` | 304640 | `81f8de81674cc9551218eec44afe2595` | `/TBBLUE.FW` | TBBlue firmware container — see §1.3 |

### 1.2 — FPGA-embedded ROM

| ROM file | Size | MD5 | Source |
|---|---|---|---|
| `nextboot.rom` | 8192 | `8c4f0c1b77db8de9ed857f2c873250b8` | jnext binary, embedded via `objcopy --input-target=binary` in `src/core/CMakeLists.txt` (silicon-baked IPL on real Next) |

### 1.3 — TBBLUE.FW container layout

`TBBLUE.FW` is a custom container with a 512-byte directory header at offset 0,
followed by 512-byte block-aligned modules. Decoder source:
`/tmp/tbblue-src/src/firmware/firmware/src/main.c`. Each directory entry is 4
bytes: `block_offset_lo, block_offset_hi, block_count_lo, block_count_hi`.

Parsed table:

| Idx | Module | First block | Block count | File offset | Size (bytes) | Notes |
|---|---|---|---|---|---|---|
| 0 | boot | 0 | 54 | $000200 | 27648 | The TBBlue boot loader — runs on cold start, draws TBBlue logo + "Press SPACEBAR for menu" + handles SD mount, ROM loading, hands off to NextZXOS |
| 1 | (alias for boot) | — | — | — | — | Skipped |
| 2 | updater | 54 | 52 | $006E00 | 26624 | Firmware updater (`U` keypress) |
| 3 | (alias for boot) | — | — | — | — | Skipped |
| 4 | editor | 106 | 66 | $00D600 | 33792 | Config editor |
| 5 | cores | 172 | 62 | $015A00 | 31744 | Extra cores menu (`C` keypress) |
| 6 | videotest | 234 | 62 | $01D600 | 31744 | Video mode selection screen (`A`/`D`/`V`/`R` for All/Digital/VGA/RGB) |
| 7 | reset | 296 | 57 | $025200 | 29184 | Anti-brick / safety reset |
| 8 | screens | 353 | 241 | $02C400 | 123392 | **17 pre-rendered Spectrum screens** (6912 bytes each) + tile data |

All TBBLUE.FW modules are **linked at base address `$6000`** (per
`/tmp/tbblue-src/src/firmware/app/Makefile` `--code-loc 0x6010` with a 16-byte
crt0 stub at $6000). Disassembled with `-o 0x6000`.

### 1.4 — Bank shape of `enNextZX.rom`

| Bank | First 16 bytes | Role | Bytes diff from 48.rom |
|---|---|---|---|
| 0 | `f3 c3 ef 00 45 44 09 02 c3 3b 10 2a 2e 2a ff 00` (`DI; JP $00EF; …`) | First-stage boot, RST handlers, NEXTREG init at $00EF, DivMMC IM 1 ISR at $0038 | 16197 |
| 1 | `00 c3 00 3f ff ff ff ff c3 d4 0d …` (`NOP; JP $3F00; …`) | **Cross-bank dispatcher** — bank-2 → bank-0 calls go via $3E80; bank-0 → bank-2 via $3F00. Also Layer-2 / palette init ($1500). PC=$1411 = 48K-style cursor-flash routine. | 16166 |
| 2 | `00 18 fd 00 …` (`NOP; JR $-3` — infinite-loop trap) | **Supervisor MAIN** — only entered via bank-1 $3F00 wrappers. Has welcome-screen-init at $088D, ROM-name table, file-load logic. **Landing at $0000 in this bank = infinite loop** (the EOD-9 trap). | 16006 |
| 3 | `f3 af 01 3b 24 c3 e8 3b 2a 5d 5c 22 5f 5c …` (`DI; XOR A; LD BC,$243B; JP $3BE8` then 48K-BASIC entry at $0008) | **Soft-reset trampoline + 48K BASIC fallback**. The $3BE8 site is the deliberate "clean-reboot" path (NEXTREG $02,$01 ← supervisor-driven soft reset, decoded EOD-15). Below $3BE8 it's mostly canonical 48K BASIC code. | 6423 |

---

## Section 2 — Cross-bank calling convention (CRITICAL)

NextZXOS uses **same-PC-different-code wrappers** at $3E80 and $3F00. The same
PC means different things depending on which bank is currently mapped (MMU3
controlled by NEXTREG $8E):

| Wrapper | Source bank | Target NEXTREG | Effect |
|---|---|---|---|
| $3E80 | bank 0 | `NEXTREG $8E,$01` | switch to bank 1 |
| $3E80 | bank 1 | `NEXTREG $8E,$00` | switch to bank 0 |
| $3F00 | bank 1 | `NEXTREG $8E,$02` | switch to bank 2 |
| $3F00 | bank 2 | `NEXTREG $8E,$01` | switch to bank 1 |
| $3E80 | bank 2 | (NOPs — not a wrapper) | — |
| $3F00 | bank 0 | (different code — not a wrapper) | — |
| $3E80/$3F00 | bank 3 | (data — not wrappers) | — |

**Implication:** **Bank 0 and bank 2 cannot directly call each other** — every
such call is routed through bank 1. Bank 1 is the central switchboard.

The wrapper logic (decoded from bank 1 `$3E80`):

```
$3E80: ld   ($5b54),bc          ; save BC
$3E84: ex   (sp),hl              ; HL <-> top of stack (= return addr to inline target words)
$3E85: ld   c,(hl); inc hl       ; pull target lo
$3E87: ld   b,(hl); inc hl       ; pull target hi
$3E89: ex   (sp),hl              ; restore HL, push adjusted return
$3E8A: push $3E93                 ; push trampoline addr (to NEXTREG site below)
$3E8E: push bc                    ; push real target
$3E8F: ld   bc,($5b54)            ; restore BC
       ret                        ; "return" to real target — but inside bank 0 still
$3E93: nextreg $8e,$00            ; final bank flip THEN
$3E97: ret                        ; ret to caller (now in caller's bank)
```

**Idiom:** `CALL $3E80; DW <target>` calls `<target>` in the OTHER bank.

### 2.1 — Bank-2 calls into bank 1 (decoded from disassembly)

| Caller PC (bank 2) | Inline target (bank 1) | Purpose |
|---|---|---|
| $0888 | $1500 | Layer-2 / palette / sprite init (welcome-screen entry phase 1) |
| $0ADA | $37A6 | (TBD — printer routine?) |
| $0AE9 | $0D34 | (TBD) |
| $0FAE | $0AB4 | (TBD) |
| $1044 | $1439 | (TBD) |
| $27F9 | $3A20 | (TBD) |
| $2815 | $3AA8 | (TBD) |

These are **the only** bank-2-to-bank-1 transitions in the entire ROM. Listing
the bank-1 entry points reachable from bank 2 narrows the welcome-menu draw
dispatch to a small set.

---

## Section 3 — Boot chain ordered by execution

This section reconstructs the **dynamic execution order** the system goes
through, anchored to disassembly addresses.

### 3.1 — Cold start (FPGA boots)

1. **`nextboot.rom`** (silicon, FPGA-embedded). 8 KB at slot 0 + slot 1
   under a special FPGA boot mode. Per `scan-nextboot.txt`:
   - Writes NR $10 = $80 at PC=$0278
   - 32 NR $41 (palette) writes at PC=$18A2-$18FF — splash screen palette
   - Writes NR $43 = $10 (ULAnext+pal-1) at PC=$19B6, $20 at PC=$19C7
   - Writes NR $40 = $00 then $80
   - Border $00 at PC=$0154 and $19D8
   - SD I/O via $E7 (8 PCs) and $EB (11 PCs)
   - Reads keyboard via IN ($FE) at $01A2, $023E, $02D2

2. After loading TBBLUE.FW from SD, jumps to TBBLUE.FW boot module at $6000.

### 3.2 — TBBLUE.FW boot module ($6000-$CBFF) — **JNEXT REACHES HERE FULLY**

#### 3.2.1 — Entry sequence

```
$6000: ld    sp,$ffff
$6003: di
$6004: call  $ca6d            ; (init/setup)
$6007: call  $6afb            ; (probably mount SD, draw splash)
$600a: jp    $6010
$6010: halt; jr $6010         ; idle if init fails
```

#### 3.2.2 — Welcome-screen text print at PC=$6B85

```
$6B85: ld    de,$0b05         ; cursor row=0x0B col=0x05
$6B88: push  de
$6B89: call  $71B4             ; set cursor
$6B8C: ld    hl,$6d33          ; addr of "Press SPACEBAR for menu\0"
$6B8F: ex    (sp),hl
$6B90: call  $7358             ; print routine
$6B93: pop   af
$6B94: ld    de,$0d05         ; cursor row=0x0D col=0x05
$6B97: push  de
$6B98: call  $71b4             ; set cursor
$6B9B: ld    hl,$6d4c          ; addr of "Press C for extra cores\0"
$6B9E: ex    (sp),hl
$6B9F: call  $7358             ; print routine
```

String table at $6D33 in tbblue-boot.bin:

| Offset (in bin) | PC | String |
|---|---|---|
| $0D33 | $6D33 | `Press SPACEBAR for menu` |
| $0D4C | $6D4C | `Press C for extra cores` |
| $0D65 | $6D65 | `<none>` |

#### 3.2.3 — TBBlue → NextZXOS soft-reset handoff at PC=$6D14

```
$6D14: ld    a,$02
$6D16: ld    bc,$243b         ; NEXTREG select
$6D19: out   (c),a            ; -> NR $02 (Reset register)
$6D1B: ld    a,($ce25)         ; check flag
$6D1E: or    a
$6D1F: jr    z,$6D2A           ; if zero, normal soft-reset
$6D21: ld    a,$81             ; if non-zero, soft-reset to default core
$6D23: ld    bc,$253b
$6D26: out   (c),a            ; NR $02 = $81
$6D28: jr    $6D31
$6D2A: ld    a,$01             ; normal path: soft-reset preserving config
$6D2C: ld    bc,$253b
$6D2F: out   (c),a            ; NR $02 = $01
$6D31: jr    $6D31             ; spin (will be wiped by reset)
```

**This is THE TBBlue → NextZXOS handoff.** A NEXTREG $02 ← $01 triggers a
"soft reset to selected core" — the FPGA reload soft-resets the Z80 to
$0000 with rom_bank now pointing at NextZXOS bank 0. **Any per-bank state
that should NOT survive this reset must be cleared by the reset path.**

#### 3.2.4 — Other key TBBlue boot writes

| PC | Action | Purpose |
|---|---|---|
| $6324 | NR $04 = $01 | RAM bank-7 select (legacy) |
| $66A0 | NR $04 = $01 | (same — repeated) |
| $6A8D | NR $28 = $00 | Stack-trace bank |
| $6A9B | NR $29 = $00 | Stack-trace |
| $6B07 | NR $07 = $03 | Turbo to 28 MHz |
| $6B15 | NR $06 = $A0 | Peripheral / NMI button enable |
| $6D80 | NR $88 = $DB | ULA-next palette enable / config |
| $6D8E | NR $80 = $80 | (timer) |
| $6DA0 | NR $80 = $00 | (timer reset) |
| $6DAE | NR $88 = $FF | (palette commit) |
| $6DBC | NR $03 = $B0 | Machine type set (0xB0 = 128K issue 2 + Next?) |
| $ABEE | NR $03 = $00 | (machine type clear later) |
| $AFB2 | NR $02 = $80 | **HARD RESET trigger** (anti-brick path) |

### 3.3 — NextZXOS bank 0 boot (entry at $00EF after soft-reset)

Bank 0 reset vector and IM 1 ISR:

```
$0000: di; jp $00EF                 ; cold-reset entry
$0008: jp $103B                      ; RST $08 — paging trampoline
$0018: jp $3E80                      ; RST $18 — call to bank 1 (cross-bank)
$0020: jp $3E00                      ; RST $20 — wrapper (likely bank 1, fast call)
$0028: cross-bank wrapper            ; RST $28 — uses ED 8A push-immediate
$0030: jp $1024                      ; RST $30 — bank-0 internal call
$0038: push af; push hl; ld h,$00; ld a,$80; jp $0046  ; IM 1 ISR head
$0046: out  ($e3),a                  ; DivMMC enable bit (auto-paging)
$0066: retn                          ; NMI return point
```

#### 3.3.1 — `$00EF` — REAL boot entry (initial NEXTREG init)

```
$00EF: nextreg $07,$03                 ; turbo = 28 MHz (matches TBBlue)
$00F3: nextreg $03,$B0                 ; machine type = 128K (Next-mode)
$00F7: nextreg $C0,$08                 ; INT mask config
$00FB: ld a,$ff
$00FD: nextreg $82,a                   ; expansion bus en/dis (FF = enable all)
$0100: nextreg $83,a
$0103: nextreg $84,a
$0106: nextreg $85,a
$0109: xor a
$010A: nextreg $80,a                   ; expansion bus = 0
$010D: nextreg $81,a
$0110: nextreg $8A,a                   ; ?
$0113: nextreg $8F,a                   ; ?
$0116: ld bc,$243b
$0119: ld d,$06
$011B: out (c),d                       ; select NR $06
$011D: inc b                           ; ($253B)
$011E: in a,(c)                        ; read NR $06 current value
$0120: and $44                         ; mask scandoubler+5060
$0122: out (c),a                       ; preserve those bits
$0124: ld hl,$5800; ld de,$5801
$012A: ld (hl),l                       ; (HL)=$00
$012B: ld bc,$02FF
$012E: ldir                            ; clear ULA attrs to $00 (black/black)
$0130: ld bc,$7000
$0133: ld hl,$4000
$0136: ld a,c
$0137: exx
$0138: add a (= 0)
$0139: nextreg $56,a                   ; MMU slot 6 = page 0
$013C: inc a
$013D: nextreg $57,a                   ; MMU slot 7 = page 1
$013F: ... continues with RAM init
```

This sequence **already clears ULA attrs to $00** at PC=$012E. So the
"black screen" we see post-handoff is the boot ROM's intentional starting
state — a cleared screen with paper=black, ink=black. The supervisor would
**later** repaint with the welcome menu.

#### 3.3.2 — `$00CF` — bank-switch helpers (rom_bank setters)

```
$00CB: push $0a9e            ; trampoline addr — return into bank 0 $0A9E
$00CF: nextreg $8e,$01       ; switch to bank 1
$00D3: ret                   ; jumps via stacked target into bank 1
$00D4: nextreg $8e,$02       ; switch to bank 2
$00D8: ret
$00D9: nextreg $8e,$03       ; switch to bank 3
$00DD: ret
$00DE: nextreg $8e,$00       ; switch to bank 0
$00E2: ret
```

**These have no callers anywhere in enNextZX.rom!** (Per memory, EOD-5
research). They're either unused legacy code or used by external dot
commands.

#### 3.3.3 — `$00E3` — install bank-flip wrapper into RAM

```
$00E3: ld   hl,$0091
$00E6: ld   de,$5B00
$00E9: ld   bc,$0052
$00EC: ldir                  ; copies 82 bytes 0091..00E2 to $5B00..$5B51
$00EE: ret
```

**The `$0091` source code is the real bank-flip wrapper.** It uses port
$7FFD/$1FFD and is copied to RAM `$5B00` so bank 0 can be paged out while
the wrapper still runs.

```
$0091: push af; push bc; ld bc,$7ffd
$0096: ld a,($5b5c)          ; saved $7FFD shadow
       xor $10                ; toggle bit 4 (rom_lo)
       di
       ld ($5b5c),a
       out (c),a              ; OUT $7FFD,A
       ...
```

This is the **`$5B00` wrapper** decoded extensively in EOD-6. **If memory at
$5B00..$5B52 is corrupted (e.g. by an over-eager soft reset), no further
bank-cross calls work.** Worth a $5B00 watcher.

#### 3.3.4 — `$0B06` — register-restore routine (the NR $43 site)

```
$0AFE: ld hl,$d694             ; alt entry (different saved-state block)
$0B01: jr $0B06
$0B03: ld hl,$d5ed             ; saved-state block in slot 6/7
$0B06: ld a,(hl); push af      ; byte 0 -> AF (final NR $43 value, restored at $0B2B)
$0B08: inc hl; ld a,(hl); nextreg $15,a    ; byte 1 -> NR $15 (sprite priority)
$0B0D: inc hl; ld a,(hl); nextreg $14,a    ; byte 2 -> NR $14 (palette transparent)
$0B12: inc hl; ld a,(hl); ld ($5c8d),a     ; byte 3 -> ATTR_P (BASIC sysvar)
$0B17: inc hl; ld a,(hl); ld ($5b62),a     ; byte 4 -> P_FLAG-like (BASIC sysvar)
$0B1C: inc hl; ld a,(hl); ld ($5c48),a     ; byte 5 -> BORDCR (BASIC sysvar)
$0B21: rra; rra; rra
$0B24: out ($fe),a              ; byte 5 lo 3 bits -> border
$0B26: inc hl; call $0a45      ; byte 6 -> additional config
$0B2A: pop af                  ; recover saved byte 0
$0B2B: nextreg $43,a           ; **THE NR $43 WRITE EOD-17 NOTED**
$0B2E: scf
$0B2F: ret
```

**This routine restores NR $43 to whatever value was in `($D5ED)` — that's a
saved palette-mode state, set up earlier by the supervisor itself.** This
**is** real-Next-faithful behaviour. The `$03` value at frame 280 is just
"palette mode 3" = ULAnext + alt-pal + something. It isn't a "deliberate
write to drive NextZXOS state" — it's a configuration restore.

### 3.4 — NextZXOS bank 2 supervisor (entry only via bank 1 $3F00)

Bank 2 has NO standalone entry point. PC=$0000-$0007 is a **deliberate trap**:

```
$0000: nop; jr $0000          ; infinite loop — landing here = bug
$0008: jp $3F18                ; RST $08 -> $3F18 (bank-internal)
```

#### 3.4.1 — Welcome-screen draw entry — bank-2 PC=$088D

After cross-bank call from bank 1 (which itself was called via
`CALL $3F00; DW $1500` from bank 2 PC=$0888):

```
$088D: di
$088E: ld   sp,$5BFF              ; reset SP to top of slot-2-shadow
$0891: ld   hl,$5800              ; ULA attr area
$0894: ld   de,$5801
$0897: ld   bc,$02FF
$089A: ld   (hl),l                ; (HL)=$00 (black/black)
$089B: ldir                       ; **fills $5800..$5AFF with $00**
$089D: xor  a
$089E: out  ($fe),a               ; **black border**
$08A0: ld   hl,$0819              ; small init table
$08A3: ld   de,$5D45
$08A6: ld   bc,$0015
$08A9: ldir                       ; copy 21-byte block to $5D45 (sysvar area)
$08AB: ld   a,$04
$08AD: ld   ($5D79),a             ; set state
$08B0: ld   a,($5C92)             ; check sysvar bit
$08B3: bit  6,a
$08B5: jr   z,$0913                ; branch on "previous-call-state" bit
$08B7: ld   hl,$5D08
$08BA: ld   a,$1B
$08BC: call $0AD2                  ; -> internal $0AD2 -> CALL $3F00; DW $0D34 -> bank 1
$08BF: ld   sp,$5D07              ; SP becomes a parameter pointer
$08C2: pop  af; ld ($5D2D),a      ; pop+stash a series of values
$08C6: pop  hl; ld ($5D36),hl
... continues popping into $5D??
```

So **the supervisor IS ENTERING the welcome-screen draw**, but it
immediately performs a **screen clear to black** and then attempts to draw
text via cross-bank calls. The black screen we observe is `$089B-$089E`
running successfully.

The drawing path then **branches on `($5C92) bit 6`** at PC=$08B5:
- **bit 6 set** ($5C92 has 0x40+ value) → fall through to $08B7 path
  (calls $0AD2 with HL=$5D08, then complex SP-magic to read 12+ words)
- **bit 6 clear** → jump to $0913 (alternate path)

This `$5C92` is BASIC sysvar `LASTK` — the last key pressed code stored by
keyboard scan. **If it isn't being polled correctly, the supervisor won't
see user input and stays on the welcome screen.**

#### 3.4.2 — `$0AB9-$0AC8` — slot 6/7 setup helper

```
$0AB9: ld   b,$00
$0ABB: push de
$0ABC: nextreg $56,$0E          ; MMU slot 6 = SRAM page 14 (= upper half of bank 7)
$0AC0: nextreg $57,$0F          ; MMU slot 7 = SRAM page 15 (= ditto)
$0AC4: call $0112                ; routine in $0100-$01FF
$0AC7: pop  hl
$0AC8: ret  c
```

Pages 14/15 = bytes `$1C000-$1FFFF` of SRAM = the **last 16 KB of the first
256 KB**. This is where the supervisor's per-bank scratch typically lives.

#### 3.4.3 — `$0D00` — full-screen LDIR draw (suspicious)

```
$0CFD: ...
$0D00: ldir   ; HL=$8000, DE=$4000, BC=$7FFD = ~32 KB
```

This is a **giant 32K LDIR from $8000 → $4000**. That's a full ULA-screen
+ part-of-RAM copy. Likely used to blit a pre-rendered bitmap. Possibly
for the file-browser background.

#### 3.4.4 — `$0D32` — small-region LDIR

```
$0D2C: ...
$0D32: ldir   ; HL=$F040, DE=$4000, BC=$0009 = 9 bytes only
```

Only 9 bytes — possibly setting up a single character / icon at top-left of
ULA. Not the welcome-menu fill.

#### 3.4.5 — `$1B0D, $1B37, $1B43, $1B50, $1B5A, $1B65` — slot-7 RAM scrolls/copies

Multiple LDIRs targeting `$DA37`, `$DBA1`, `$E301`, `$E36E`, `$E3CB`,
`$E3F8`, $E843, etc. — all in slot 7 (bank 7). These are very likely
**font / glyph blits for the file browser**, not for a "welcome menu".

### 3.5 — NextZXOS bank 1 — central dispatcher

Bank 1 is mostly the dispatcher + Layer-2 management + 48K-BASIC-style
keyboard handling. Most relevant routines:

#### 3.5.1 — `$1500` — Layer-2 / palette / sprite init (called from bank-2 $0888)

```
$1500: call $15B6              ; palette load
$1503: call $158F              ; Layer-2 init
$1506: call $3E80; dw ?         ; cross-bank to bank 0
$1509-...                       ; NR $11/$12/$32/$26/$16/$2F config
$152A-$1531: NR $31=0, NR $68=0, NR $6B=0
$1534: call $1466               ; bank-1 internal sub
$1537: ld   bc,$123b             ; sprite enable port
$153A: xor  a; out (c),a         ; sprites disabled
$153D: ld   ($5b7b),a            ; ?
$1540: ld   d,$e3; ld e,a         ; (sprite-related)
$1543: call $14b6
$1546: jr   $155f
```

#### 3.5.2 — `$15B6` — palette restore loop

```
$15B6: xor  a
$15B7: push af
$15B8: nextreg $43,a            ; select palette 0 (ULAnext default)
$15BB: xor  a; ld h,a
$15BD: nextreg $40,a            ; palette index = 0
$15C0: ld   de,$1626             ; palette-data table addr
$15C3: ld   a,(de); inc de
$15C5: cp   $aa                  ; sentinel
$15C7: jr   z,$15C0               ; loop back if found
$15C9: nextreg $41,a              ; write palette colour
$15CC: dec  h
$15CD: jr   nz,$15C3
$15CF: pop  af
$15D0: xor  $40                   ; switch to second palette set
$15D2: ...
```

This rebuilds the palette from a table at $1626 in bank 1. **The palette
table starts at bank-1 $1626 and ends at the first $AA byte.**

#### 3.5.3 — `$158F` — Layer-2 init

```
$158F: ld   de,$fc00             ; Layer-2 base addr
$1592: call $14b6                ; setup
$1595: nextreg $34,a              ; sprite-pattern slot
$1598: ld   b,$80
$159A: nextreg $78,$00            ; Layer-2 X offset = 0 (loop body)
$159E: djnz $159A                 ; ... 128 times — possibly clearing sprite slots
$15A0: nextreg $51,$10            ; map ROM bank 0x10 to slot 5 (Layer-2)
$15A4: ld   de,$24df
$15A7: ld   h,d; ld l,e; inc de
$15AA: ld   (hl),b               ; (HL) = $80
$15AB: ld   bc,$0820
$15AE: ldir                      ; clear $24DF..$2CFE with $80 = 0x820 = 2080 bytes
$15B0: nextreg $51,$ff            ; restore ROM bank
$15B4: jr   $155b
```

So $158F **maps SRAM page 0x10 (= 256 KB offset) to slot 5 ($A000-$BFFF)
temporarily, fills 2080 bytes with $80, then unmaps**. That looks like
**clearing a 2080-byte region of Layer-2 / sprite memory**. NR $51=$FF
restores some MMU state.

### 3.6 — NextZXOS bank 3 — soft-reset trampoline + 48K BASIC

Bank 3 PC=$0000-$0007 is the **reset-cause-detector**:

```
$0000: di
$0001: xor a
$0002: ld   bc,$243b              ; NEXTREG select port
$0005: jp   $3be8                  ; trampoline to read NR $02
$3be8: ld   a,$02
$3bea: out  (c),a                 ; select NR $02 (Reset register, read-mode)
$3bec: inc  b                     ; ($253B)
$3bed: in   a,(c)                 ; read NR $02
$3bef: and  $80                   ; mask bit 7 = (?soft reset cause)
```

Below $3BE8 the rest of bank 3 is largely **48K BASIC + NextZXOS hooks**.
Per byte-diff: only 6423/16384 bytes differ from `48.rom`.

After the bootstrap, bank 3 is essentially **48K BASIC** (per byte-diff
analysis: only 6423/16384 bytes differ from `48.rom`). The deviations are:

- **NEXTREG hooks** in some BASIC routines (so they can drive Next hardware)
- **DOT-command intercept** in CHAN-OPEN
- A few **driver patches** for keyboard/sound/file-system routing

The supervisor calls into bank 3 via $00DD `nextreg $8e,$03; ret` — but
that helper has no callers, suggesting bank 3 is reached only via the
soft-reset path (NEXTREG $02 ← $01 then natural reset to $0000 with
rom_bank set to bank 3).

---

## Section 4 — Port-usage map (per ROM)

Methodology: ports identified via `OUT (n),A`, `OUT (C),r` (with BC tracked),
NEXTREG opcodes (ED 91/92), block I/O patterns. See per-ROM scan files in
`/tmp/g46b-dasm/scans/` for raw output. Sample PCs limited to 5 per port.

### 4.1 — `enNextZX-bank0`

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $243B | 32 | $011B, $011E, $0122, $09FF, $0A02 | NEXTREG select |
| $FE | 13 | $0179, $0183, $0188, $02DE, $0AE7 | Border / keyboard |
| $FF | 5 | $097D, $0B88, $0B8E, $0BE2, $1C4D | Timex video / sound chip |
| $123B | 2 | $0992, $099C | Sprite-control register |
| $E3 | 1 | $0046 | DivMMC auto-paging |
| $7FFD | 1 | $009F | 128K paging |
| $1FFD | 1 | $00AC | +3 paging |
| $29 | 1 | $2981 | Kempston joystick? |
| $30 | 1 | $3CE0 | (uncommon) |

### 4.2 — `enNextZX-bank1`

(Most port activity is in bank 0; bank 1 is the dispatcher.)

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $243B | many | (palette/Layer-2/sprite NR setups) | Inside $1500/$15B6/$158F |
| $123B | 1 | $1537 | Sprite enable |

### 4.3 — `enNextZX-bank2` (supervisor MAIN)

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $FE | many | (welcome-screen-related: $089E, …) | Border + keyboard |

### 4.4 — `enNxtmmc.rom` (DivMMC firmware)

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $E3 | 27 | $0045, $004C, $005E, $0098, $009D | DivMMC auto-paging (most-used) |
| $EB | 11 | $1F20, $1F40, $1F72, $1F76, $1F7A | SPI data port |
| $E7 | 2 | $1F1E, $1F7E | SPI control port |
| $243B | 2 | $007C, $007F | NEXTREG (rom_bank flips) |
| $FE | 2 | $1A0E, $1A15 | Border (error indication?) |

NEXTREG writes:
- NR $56 / NR $57 (MMU8 slot 6/7): $008A / $008E (one each)
- NR $8E (rom_bank): $0400 → $02, $0452 → $02, $0539 → $03

### 4.5 — `enNextMf.rom` (Multiface)

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $E3 | 18 | $00DF, $018F, $0541, $1385, $13AA | DivMMC paging (MF reuses) |
| $123B | 13 | $00DB, $018D, $01A0, $01A3, $01A7 | Sprite control |
| $243B | 10 | $00BB, $050B, $05A8, $05AD, $179D | NEXTREG select |
| $3F | 7 | $0534, $1391, $13EC, $1904, $1984 | (custom?) |
| $BF | 6 | $1373, $13A1, $13AE, $18EA, $197D | (custom?) |
| $FFFD | 5 | $0160, $0162, $016E, $051F, $0532 | AY register select |
| $7FFD | 4 | $04C1, $13B5, $186D, $18F7 | 128K paging |
| $1FFD | 3 | $04B9, $187A, $18F0 | +3 paging |
| $DFFD | 2 | $00FD, $04A9 | Next paging (DFFD) |
| $EFF7 | 2 | $010A, $04B1 | Issue-3-disable port |
| $FE3F | 2 | $010F, $011F | (Multiface combined port) |

### 4.6 — `nextboot.rom` (FPGA-embedded IPL)

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $253B | 39 | $0278, $1569, $157A, $1890, … | NEXTREG data write |
| $243B | 13 | $015B, $016F, $017A, $0185, $0271 | NEXTREG select |
| $EB | 11 | $0467, $0476, $0522, $0525, $0578 | SPI data |
| $E7 | 8 | $0112, $03CE, $0461, $046D, $048F | SPI control |
| $FE | 6 | $012B, $0154, $01A2, $023E, $02D2 | Border + keyboard |
| $3B | 4 | $015F, $0173, $017E, $0189 | Direct $3B (NEXTREG read?) |

### 4.7 — `tbblue-boot` (TBBLUE.FW boot module)

Already detailed in §3.2. Top ports:

| Port | Hits | Sample PCs | Notes |
|---|---|---|---|
| $243B | 53 | $6075, $6128, $6302, $631D, $665C | NEXTREG select (heavy use) |
| $253B | 38 | $6088, $6324, $6663, $66A0, $685C | NEXTREG data |
| $FE | 17 | $601E, $653D, $6541, $6BC1, $6BC8 | Border + keyboard |
| $E7 | 9 | $74CE, $74D5, $74E1, $74EE, $74F9 | SPI control |
| $3B | 8 | $6306, $7740, $774E, $AC01, $AC29 | NEXTREG read |
| $EB | 3 | $74D1, $7876, $788A | SPI data |
| $103B | 1 | $6121 | (NEXTREG mirror?) |
| $FB | 1 | $6D92 | (probably AY/timer) |

NEXTREG writes (key ones):
- NR $02: PC=$6D26 ($81), $6D2F ($01), $AFB2 ($80) — soft/hard reset triggers
- NR $03: PC=$6DBC ($B0), $ABEE ($00) — machine type
- NR $07: PC=$6B07 ($03) — turbo
- NR $04: PC=$6324, $6663, $66A0, $ACA0, $B7A2 — RAM bank 7
- NR $06: PC=$6B15 ($A0)
- NR $28-$29: PC=$6A8D ($00), $6A9B ($00) — stack-trace bank
- NR $80: PC=$6D8E ($80), $6DA0 ($00) — timer
- NR $88: PC=$6D80 ($DB), $6DAE ($FF) — palette enable

---

## Section 5 — MMU / bank-mapping plan

Aggregated NEXTREG writes for $50-$57 (MMU8 slots), $8C (alt-ROM enable),
$8E (legacy rom_bank), $03 (machine type), $07 (turbo). Format: `(PC, value)`.

### 5.1 — `enNextZX-bank0`

| NR | Writes |
|---|---|
| $03 | $00F3=$B0 |
| $07 | $00EF=$03, $3DBD=A |
| $51 | $0B70=$10, $0BF6=$FF |
| $56 | $0139=A, $0196=A, $26D5=A, $2796=A |
| $57 | $013D=A, $019A=A, $26DD=A, $279A=A, $2C04=$10, … (15 more) |
| $8C | $007B=$80 (alt-rom toggle) |
| $8E | $00CF=$01, $00D4=$02, $00D9=$03, $00DE=$00, $1024=$08 (and 3 more — possibly bank 8/9 of expansion ROM) |

### 5.2 — `tbblue-boot`

| NR | Writes |
|---|---|
| $02 | $6D26=$81, $6D2F=$01, $AFB2=$80 |
| $03 | $6DBC=$B0, $ABEE=$00 |
| $04 | $6324=$01, $6663=$00, $66A0=$01, $ACA0=$06, $B7A2=$02, … |
| $06 | $6B15=$A0 |
| $07 | $6B07=$03 |
| $28 | $6A8D=$00, $AF55=$80 |
| $29 | $6A9B=$00 |
| $80 | $6D8E=$80, $6DA0=$00 |
| $88 | $6D80=$DB, $6DAE=$FF |

### 5.3 — `enNxtmmc.rom`

| NR | Writes |
|---|---|
| $56 | $008A=A |
| $57 | $008E=A |
| $8E | $0400=$02, $0452=$02, $0539=$03 |

### 5.4 — `enNextMf.rom` (Multiface)

| NR | Writes |
|---|---|
| $02 | $1796=$08 (Multiface NMI inject?) |
| $07 | $027A=$03 |
| $1C | $0134=$0F, $0475=$0F |
| $40 | $1829=A, $184B=A |
| $43 | $1823=A, $1847=$44 |
| $44 | $04FA=A, $1850=A, $1857=A |
| $54 | $1625=A |
| $55 | $137F=$10, $1908=$10, $1A71=$05 |
| $62 | $00C4=A |
| $80 | $00C7=A |
| $8C | $1699=A, $169F=$00 |
| $8E | $189D=$01, $18A2=$02, $18A7=$03, $18AC=$00 |
| $8F | $0191=$00, $04A0=A |

### 5.5 — `nextboot.rom`

| NR | Writes |
|---|---|
| $10 | $0278=$80 |
| $40 | $1569=$00, $1890=$00, $1923=$80 |
| $41 | $18A2-$18FF = 32 palette colour writes |
| $43 | $19B6=$10, $19C7=$20 |

---

## Section 6 — Welcome-menu draw candidates

Searched for: LDIR/LDDR with destination in $4000-$5B00 (ULA pixels) /
$5800-$5B00 (attrs); `LD ($nnnn),A` with addr in same ranges. Full output
in `/tmp/g46b-dasm/scans/<rom>.draws.txt`.

### 6.1 — `enNextZX-bank2` (supervisor) — most relevant

| Site | Type | Source → Dest, Count | Purpose |
|---|---|---|---|
| **PC=$089B** | LDIR | $5800 → $5801, $02FF | **Clear all attrs to $00 (black/black)** — explains observed black screen post-handoff |
| PC=$0D00 | LDIR | $8000 → $4000, $7FFD | Full 32K image blit — file browser background? |
| PC=$0D32 | LDIR | $F040 → $4000, $0009 | 9-byte top-left icon |
| PC=$0D91 | LDIR | $0D5C → $C000, $0014 | 20 bytes to slot 6 |
| PC=$1B0D | LDIR | $5B8A → $DA37, $0075 | 117 bytes from print buffer to slot 7 |
| PC=$1B37 | LDIR | $DBA0 → $DBA1, $07F9 | self-fill (clear) 2041 bytes in slot 7 |
| PC=$1B43 | LDIR | $E3F7 → $E3F8, $1119 | Self-fill 4377 bytes |
| PC=$1B50 | LDIR | $E300 → $E301, $1119 | Self-fill 4377 bytes |
| PC=$1B5A | LDIR | $E36D → $E36E, $1119 | Self-fill 4377 bytes |
| PC=$1B65 | LDIR | $E3CA → $E3CB, $1119 | Self-fill 4377 bytes |
| PC=$1BEA | LDIR | $DA37 → $5B8A, $0075 | 117 bytes from slot 7 to print buffer (REVERSE of $1B0D) |

Direct attr/sysvar writes:

| PC | Address | Notes |
|---|---|---|
| $05D7 | $5B52 | (printer-buf addr) |
| $0F93 | $5C61 | sysvar |
| $0F96 | $5C63 | sysvar |
| $0F99 | $5C65 | sysvar |
| $084F | $5C92 | LASTK sysvar set |
| $0860 | $5C93 | sysvar |
| $0D1C | $4000 | ULA pixel direct write |

### 6.2 — `enNextZX-bank0`

Bank 0 has the early ULA attr-clear at PC=$012B-$012E. Other LDIRs are
mostly NEXTREG-saved-state restores, not screen draws.

### 6.3 — `tbblue-boot` — confirms welcome menu is here

The TBBlue boot module has many LDIRs targeting $4000-$5800 (ULA pixels) for
splash drawing, plus `OUT ($FE)` at $601E etc. for border. **It also has the
"Press SPACEBAR" string at $6D33** referenced from PC=$6B8C as documented in
§3.2.2.

### 6.4 — Cross-bank font / glyph blits in bank 2

The cluster of $1B37/$1B43/$1B50/$1B5A/$1B65 LDIRs in bank 2, all 4377 bytes
each at `$E300` / `$E36D` / `$E3CA` / `$E3F7`, are extremely suspicious for
**bitmap-font initialisation in slot 7** (`$E000-$FFFF` window). 4377 bytes ≈
547 chars × 8 rows or 274 chars × 16 rows — looks like a font set being
generated/copied. **If bank 7 RAM is corrupted at this point, fonts won't be
available and any subsequent printing would silently fail.**

This connects with EOD-17's note that "bank 5 is cleared at frame 280
(handoff)" — if bank 7 is also being cleared by the soft reset, but the
supervisor expects the font to survive, **that's the welcome-menu blocker.**

---

## Section 7 — Per-ROM ASCII strings (offsets and addresses)

### 7.1 — `enNextZX-bank0` (file browser)

| Offset | PC | String |
|---|---|---|
| $0E6B | $0E6B | "Change to destination and press P to paste" |
| $0FBA | $0FBA | "Press any key" |
| $137E | $137E | "ENTER=select EDIT=up EXTEND=more BREAK" |
| $13A9 | $13A9 | "New name: " |
| $13C1 | $13C1 | "Paste as: " |

### 7.2 — `enNextZX-bank1`

| Offset | PC | String |
|---|---|---|
| $? | $? | `C:/DOT/FM` |
| $2C51 | $2C51 | "Remove/insert SD and press Y" |

### 7.3 — `enNextZX-bank2` (supervisor)

| Offset | PC | String |
|---|---|---|
| $? | $? | `zx80.rom`, `zx81.rom`, `128.rom` (rom-name table) |
| $? | $? | `enAltZX.rom`, `128-2.rom`, `enNxtmmc.rom` |
| $? | $? | `Mount=???-?.???` |
| $2057 | $2057 | `"c:/nextzxos/autoexec.1st"` |
| $2073 | $2073 | `"c:/nextzxos/autoexec.bas"` |
| $2956 | $2956 | `C:/NEXTZXOS/METADATA/` |
| $? | $? | `Core 3.01.10 needed` |
| $? | $? | `Error reading file:` |
| $? | $? | `Version mismatch:` |
| $? | $? | `0209 enNextZX.rom` (version stamp) |

### 7.4 — `enNextZX-bank3` (48K BASIC + patches)

| Offset | PC | String |
|---|---|---|
| $? | $? | `RANDOMIZ` (48K BASIC token table) |
| $? | $? | `BHY65TGVNJU74RFCMKI83EDX` (keyboard map) |
| $1525 | $1525 | "Tape loading erro..." (48K BASIC error) |

### 7.5 — `tbblue-boot.bin` (PC base $6000)

| Offset | PC | String |
|---|---|---|
| $0339 | $6339 | "/machines/next/" |
| $? | $? | "esxmmc.bin" |
| $? | $? | "Loading ESXMMC:" |
| $? | $? | "mf1.rom", "mf128.rom", "mf3.rom", "mf128v1.rom", "mf128v12.rom" |
| $? | $? | "Loading Multiface ROM:" |
| $? | $? | "Loading ROM:" |
| $? | $? | "Anti-Brick" |
| $? | $? | "No TBBLUE.TBU update file found!" |
| $? | $? | "Please update your core!" |
| $? | $? | "You need at least core  v" |
| $? | $? | "%lu.%02lu.%02lu" |
| $? | $? | "You currently have core v" |
| $? | $? | "Press U to enter the updater now" |
| $? | $? | " if you have copied the latest" |
| $? | $? | "  TBBLUE.TBU to your SD card" |
| $? | $? | "For video mode selection press:" |
| $? | $? | "A=All, D=Digital, V=VGA, R=RGB" |
| $? | $? | "Firmware v" |
| $? | $? | "Core v" |
| $? | $? | "Loading keymap:" |
| $? | $? | "keymap.bin" |
| **$0D33** | **$6D33** | **"Press SPACEBAR for menu"** |
| $0D4C | $6D4C | "Press C for extra cores" |
| $0D65 | $6D65 | "<none>" |
| $0D6C | $6D6C | "1.44.db" (firmware version) |

### 7.6 — `enNxtmmc.rom`, `enNextMf.rom`

| File | String |
|---|---|
| `enNextMf.rom` | `c:/nextzxos/enMf0.sy...` (MF system file) |

---

## Section 8 — TBBlue source cross-reference

Source tree at `/tmp/tbblue-src/src/firmware/` (cloned from
`https://gitlab.com/thesmog358/tbblue.git`).

| TBBLUE.FW module | Source dir | Linker | Code base |
|---|---|---|---|
| boot.bin | `app/src/boot.c + …` | sdcc | 0x6010 (crt0 stub at 0x6000) |
| editor.bin | `app/src/editor.c + …` | sdcc | 0x6010 |
| updater.bin | `app/src/updater.c + …` | sdcc | 0x6010 |
| cores.bin | `app/src/cores.c + …` | sdcc | 0x6010 |
| videotest.bin | `app/src/videotest.c + …` | sdcc | 0x6010 |
| reset.bin | `app/src/reset.c + …` | sdcc | 0x6010 |

Top-level relevant sources:

- `src/firmware/app/src/boot.c` — main TBBlue boot logic (the `Press SPACEBAR`
  flow, SD mount, ROM loading, NextZXOS handoff).
- `src/firmware/app/src/spi.s` — SPI assembly for SD I/O via $E7/$EB.
- `src/firmware/app/src/switch.s` — bank-switching helpers.
- `src/firmware/app/src/vdplow.s` — low-level video helpers.
- `src/firmware/app/src/videomagic.c` — video mode setup + magic.
- `src/firmware/app/src/modules.c` — module dispatch (`boot` → `editor` etc).
- `src/firmware/loader/src/mmc.s` — MMC/SD init (used by FPGA `nextboot.rom`).

The **TBBlue boot module's `main()`** loads the keymap, draws the splash from
`screens.bin`, and waits for SPACEBAR / C / U keypress. With timeout 0
(default), it auto-advances after a few seconds, calling
`load_module(MODULE_RESET)` (or NextZXOS-load via the `boot.c` final path)
which writes `NEXTREG $02 ← $01` to soft-reset into NextZXOS.

---

## Section 9 — Per-routine summaries (highest-value)

### 9.1 — bank-0 `$00EF` — NextZXOS reset entry (32 NEXTREG inits) — DESCRIBED in §3.3.1

### 9.2 — bank-0 `$0B06` — saved-state restore — DESCRIBED in §3.3.4

### 9.3 — bank-0 `$3E80` cross-bank wrapper — DESCRIBED in §2

### 9.4 — bank-1 `$1500` — Layer-2 / palette init — DESCRIBED in §3.5.1

### 9.5 — bank-1 `$15B6` — palette restore — DESCRIBED in §3.5.2

### 9.6 — bank-1 `$158F` — Layer-2 init + slot-5-Layer2-RAM clear — DESCRIBED in §3.5.3

### 9.7 — bank-2 `$088D` — welcome-screen entry / black-clear — DESCRIBED in §3.4.1

### 9.8 — bank-2 `$0AB9` — slot 6/7 mapper helper — DESCRIBED in §3.4.2

### 9.9 — `$5B00` (RAM-resident bank-flip wrapper)

Source resides in bank-0 ROM at $0091, copied to RAM $5B00 by `$00E3`. 82
bytes total. The wrapper toggles bit 4 of `$7FFD` (= $5B5C shadow) to swap
the lower 16K ROM. Used by every BASIC-interrupt-driven cross-bank call.

If `$5B00..$5B52` is wiped (e.g. by a soft reset that shouldn't have
preserved RAM but did, or by a misbehaving routine writing zeros), **all
RAM-driven cross-bank dispatch fails silently**. This is worth a watcher
checkpoint after the handoff.

### 9.10 — bank-2 fonts at $1B37/$1B43/$1B50/$1B5A/$1B65 (4×4377 bytes to $E300+)

Probably a **font generation/blit sequence** running on slot-7 RAM. 4377
bytes ≈ 547 × 8 = 4376 (close to a 547-char × 8-row font). If bank 7 RAM is
zeroed by handoff and these LDIRs are skipped, all subsequent text rendering
will produce blanks — consistent with "screen renders but no text appears".

### 9.11 — bank-2 PC=$0888 → bank-1 PC=$1500 — the welcome init kickoff

This is the cross-bank invocation that **sets up Layer-2 palette + clears
sprite/Layer-2 RAM** before the supervisor enters its black-clear at $088D.

---

## Section 10 — What this means for G46(b)

### 10.1 — Where the dynamic state matches the static analysis

- **NR $43 = $03 at PC=$0B2B** — **EXPECTED** behaviour: this is a saved-state
  restore, not a deliberate "set palette mode 3". The `$03` is whatever
  byte was saved at offset 0 of `($D5ED)` before the latest soft reset.
- **PC ≈ $1411 in rom_bank=$03 (= bank 1)** — the BASIC FLASH/cursor-blink
  routine. **Supervisor is in IM 1 ISR loop, not making forward progress.**
- **Black screen post-handoff** — caused by bank-2 PC=$089B (`LDIR` clears
  attrs to $00) + PC=$089E (`OUT ($FE),$00`). Code IS running.
- **Frame 280 bank-5 cleared** — consistent with NR $50-$57 MMU map being
  re-set up post-soft-reset, paging out previous bank 5 contents.

### 10.2 — What NextZXOS "should" do next (according to disassembly)

After the black-clear at PC=$089B, the supervisor should:

1. Read sysvar `$5C92` (LASTK) at PC=$08B0 to check key state.
2. Branch on bit 6:
   - **Set:** call `$0AD2` → cross-bank to bank 1 `$0D34` to do (?TBD)
   - **Clear:** jump to `$0913` for alternate path
3. After return, do SP-magic to read 12 saved values into `$5D??` BASIC sysvars.
4. Eventually call `$0AF0` (which LDIRs $0C22 → $5D80, 0x84 bytes — config
   block).
5. Eventually invoke the **file-browser** or **dot-shell** print path —
   strings at bank-0 `$0E6B`/`$0FBA`/`$137E` etc. would render to
   $4000-$57FF.

### 10.3 — Hypotheses to test against jnext

1. **`$5B00..$5B52` wrapper integrity** — was the LDIR at PC=$00E3-$00EE
   actually executed in jnext, and is `$5B00..$5B52` intact when supervisor
   reaches its main loop? Add a diagnostic that snapshots `$5B00..$5B52` at
   frame 280 and again at frame 600.
2. **Bank-7 font integrity** — were the LDIRs at bank-2 $1B37/$1B43/$1B50/
   $1B5A/$1B65 (slot-7 RAM clears + font blits) actually performed? If bank
   7 is empty when font printing happens, all glyphs render as blanks.
3. **Sysvar `$5C92` (LASTK) reads** — which path is bank-2 PC=$08B5
   taking? If LASTK never gets set (because keyboard scan path is broken),
   supervisor is stuck on bit-6-clear branch waiting forever.
4. **Cross-bank wrapper $5B00 dependency** — the IM 1 ISR at bank-1 $0038
   and $0046 does `OUT ($E3),A` which depends on DivMMC auto-paging. If
   that auto-paging hasn't been correctly configured, every IM 1 interrupt
   could be paging in unexpected RAM and the supervisor's flow is poisoned.

### 10.4 — Suggested diagnostics

- **`JNEXT_G46B_5B00_WATCH=1`**: log every write to $5B00..$5B52 with PC.
  Detect zero-fill or unexpected overwrite.
- **`JNEXT_G46B_BANK7_DUMP=1`**: dump SRAM page 14/15 at frame 600 — should
  contain the font blit results from bank-2 $1B37+.
- **`JNEXT_G46B_LASTK_TRACE=1`**: trace every read from `$5C92` and every
  write to it. Compare frequency in jnext vs CSpect.
- **`JNEXT_G46B_3F00_TRACE=1`**: log every CALL $3F00 / CALL $3E80 cross-bank
  call with the inline target. Build a histogram. Compare against the 7
  bank-2→bank-1 sites listed in §2.1.

---

## Appendix A — File index

| Asset | Location |
|---|---|
| Raw disassemblies | `/home/jorgegv/src/spectrum/jnext/doc/issues/dasm/<rom>-bank<N>.asm` |
| Extracted ROMs | `/tmp/g46b-dasm/extracted/` |
| Bank-split binaries | `/tmp/g46b-dasm/banks/` |
| TBBLUE.FW modules | `/tmp/g46b-dasm/tbblue/` |
| Port scans | `/tmp/g46b-dasm/scans/<rom>.txt` |
| Draw scans | `/tmp/g46b-dasm/scans/<rom>.draws.txt` |
| TBBlue source | `/tmp/tbblue-src/` (shallow clone of `https://gitlab.com/thesmog358/tbblue.git`) |
| Helper scripts | `/tmp/g46b-dasm/scan_ports.py`, `/tmp/g46b-dasm/scan_draws.py` |

## Appendix B — Tools used

- `z88dk-dis -mz80n` (Z80 + Z80N opcodes — from `/home/jorgegv/src/spectrum/z88dk/`)
- `mtools` (mcopy, mdir) for SD-image FAT32 access
- Custom Python scripts for port / NEXTREG / LDIR pattern extraction

## Appendix C — Honest uncertainty notes

- **Bank-2 routine targets at $0AD2, $0FAE etc. — partial decode.** Some
  internal calls are not chased to their effects; deeper analysis would
  require manual trace through.
- **Bank-2 $0D00 $7FFD-byte LDIR** — flagged as suspicious but not fully
  decoded. Its source $8000 implies it's reading from RAM (slot 4) at
  the time, so depends on what's there.
- **The "welcome-menu draw"** in bank 2 hasn't been chased to a final-text-
  print routine that calls into bank 0 or bank 1's print system. The flow
  past PC=$088D-$09xx is complex SP-manipulation that probably needs live
  tracing to follow.
- **TBBlue source mapping** — the `boot.c` source isn't yet matched 1:1 with
  the disassembled tbblue-boot.asm; many strings could be mapped but the
  printf-style format strings in `boot.c` would help nail down each call
  site precisely.
- **NEXTREG $43 value semantics**: the supervisor's saved-state byte 0 (which
  becomes NR $43 at PC=$0B2B) is set by code earlier in boot. We have NOT
  decoded **where in boot that block is initialised** (i.e., what value is
  written to `($D5ED)` byte 0 originally). Likely it's filled from CSpect
  saved-state or from a default table.

---

*Document generated as static-analysis ground truth for G46(b) investigation,
to be paired with dynamic instrumentation (port-spy, slot-context, NR43 trace,
ULA-write watcher, dump-at-frame) in commit `8b31225`.*
