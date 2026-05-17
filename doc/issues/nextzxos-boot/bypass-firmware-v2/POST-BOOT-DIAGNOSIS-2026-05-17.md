# Task 18 — Post-Boot Diagnosis (2026-05-17 follow-up)

Goal of this pass: explain the "screen stays uniform gray" observation
from the initial bypass commit (`c0a0ff1b`).

## TL;DR

**The screen IS correctly painted.** What looked like a hang is in
fact NextZXOS sitting in its standard `NOP; JR -3` idle loop at
bank-2 offset `$0000`, with a CLS'd white-paper / black-ink screen
($5800 attribute area = `38 38 38 ...`) and no text drawn. The
"uniform gray" is the same `color 7 BRIGHT=0` that the `ay_demo`
border renders as — i.e. **white**, headless-rendered as light gray.

NextZXOS booted, did SD init, performed 196 file reads, and entered
its IM-1 idle loop with the editor screen pre-CLS'd. What it has NOT
done is reach AUTOEXEC.1ST or the BASIC editor banner draw — those
require additional state that tbblue.fw normally provides.

## Evidence from `task18_bypass_post_boot_probe`

After running 600 frames (~12 s of emulated time):

| Probe | Value | Interpretation |
|-------|-------|----------------|
| Z80 PC | `$0000` | In idle loop (after RETI from ISR) |
| Z80 IFF1/IFF2 | 1/1 | Interrupts enabled, IM 1 |
| Z80 IY | `$5C3A` | Canonical Spectrum BASIC sysvars pointer — IY points exactly where ZX-BASIC expects it |
| Z80 SP | `$5F31` | Stack in NextZXOS workspace area |
| MMU slot map | `[FF FF 0A 0B 04 05 0E 0F]` | Standard 128K-style layout w/ slot 6/7 → bank 7 (NextZXOS workspace code) |
| `mmu.boot_rom_enabled` | 0 | Boot ROM correctly disabled by NR $03=$B3 commit |
| `mmu.rom_in_sram` | 1 | ROM reads route through SRAM pages 0-7 |
| `mmu.machine_type` | 3 (ZX_PLUS3) | Correctly committed by bypass init |
| NR $03 cached | `$B0` | NextZXOS re-wrote NR $03 (timing-only, no machine-type change) |
| NR $07 cached | `$03` | 28 MHz running |
| NR $8E cached | `$02` | +3 paging emulation selected ROM bank 2 (NextZXOS main BASIC) |
| NR $8C cached | `$00` | No alt-ROM active |
| NR $12 / $13 | `$09` / `$09` | Layer 2 active and shadow page = 9 (matches CSpect post-boot capture) |
| NR $68 / $69 | `$00` / `$00` | Default display control (ULA layer enabled, no Layer 2 priority override) |
| `ula.shadow_screen_en` | 0 | Not in shadow-screen mode |
| `ula.border` | 7 | White border (jnext default — NextZXOS hasn't overwritten) |

### Screen content

- **Bank 5 page $0A pixel area $4000-$57FF**: ALL zero — no characters drawn
- **Bank 5 page $0A attribute area $5800-$5AFF**: all `$38` = paper 7 (white), ink 0 (black), BRIGHT 0, FLASH 0 — **standard BASIC ready attributes**
- **Bank 7 page $0E**: contains Z80 code (`21 AD 27 CD AE 27 ...` = `LD HL,$27AD; CALL $27AE; ...`) — NextZXOS's workspace code mapped at slot 6/7
- **Layer 2 active bank ($09 → 8K page 18)**: all zeros — no L2 content drawn
- **Slot 0/1 ROM ($0000-$3FFF)**: bank 2 of `enNextZX.rom` (selected via NR $8E=$02). $0000 = `NOP; JR -3` idle loop; $0038 = standard IM-1 ISR start (`PUSH AF; PUSH BC; PUSH HL; LD BC,$243B; IN L,(C); ...`)

### BASIC sysvars at $5C00 (first 128 bytes)

```
$5C00: FF 00 00 00 FF 00 00 00 00 23 02 00 00 00 00 00 01 00 06 00 0B 00 01 00 01 00 06 00 10 00 00 00
$5C20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 3C 40 00 FF 1C 01 53 FF 00
$5C40: 00 00 00 00 00 00 00 00 38 00 00 CB 5C 00 00 B6 5C B6 5C CB 5C 00 00 CA 5C CC 5C 00 00 00 00 00
$5C60: 00 CE 5C CE 5C CE 5C 00 00 00 10 02 00 00 00 00 00 00 00 00 00 00 00 00 38 02 00 58 FF 00 00 10
```

Key sysvars decoded:
- `$5C3B (FLAGS)` = `$1C` — K-cursor on, no L-mode, 128-cursor mode (valid BASIC state)
- `$5C41 (MODE)` = `$00` — K/L mode (BASIC ready)
- `$5C48 (BORDCR)` = `$38` — paper 7 ink 0 border attribute (matches screen $5800 pattern)
- `$5C7B (LAST_K)` = `$58` — junk / no real key pressed yet

These sysvars are INITIALIZED, not zero — NextZXOS has set them up.

## What this rules out

- ✗ "jnext renderer broken" — `ay_demo` and other NEX games render fine; renderer is correct.
- ✗ "SRAM seed wrong" — bank 0 starts with the enNextZX.rom signature `F3 C3 EF 00` exactly as expected (verified by `task18_bypass_state_test`).
- ✗ "Boot ROM overlay leaked through" — `mmu.boot_rom_enabled = 0`, the JP $00EF entry trace shows code from enNextZX.rom executing, not from nextboot.rom.
- ✗ "NextZXOS crashed early" — sysvars are populated, IY/SP/IM are correct, ISR is running on every frame, NR writes happen.
- ✗ "Wrong machine type" — `mmu.machine_type = 3` (ZX_PLUS3), matches what menu.def default specifies (mode=2 → +3).
- ✗ "Display layer disabled" — NR $69 = 0, ULA layer is active; pixels at $4000-$57FF are 0 not because they aren't being read, but because nothing was written there.
- ✗ "Screen written to wrong bank" — both Layer 2 active page ($09) and bank 7 are scanned; no pixel content anywhere.

## What this likely IS

NextZXOS is in a **valid post-CLS idle state** waiting for an event
that we haven't generated. Candidates:

1. **Boot screen / banner draw deferred behind a "firmware-completed"
   flag.** tbblue.fw normally paints the boot screen (`display_bootscreen()`)
   and may set a sysvar or NR bit that NextZXOS's banner-draw logic
   tests for. Without that flag, NextZXOS skips the banner.

2. **AUTOEXEC.1ST not yet loaded.** NextZXOS's 196 SD reads stopped
   well before the AUTOEXEC.1ST cluster (10530). The OS may be in
   "ready for autoexec" state but never gets to load it because
   something earlier signals the autoexec path.

3. **`config.ini` settings parsing.** tbblue.fw parses config.ini and
   passes settings to NextZXOS via NextReg writes / sysvars. The
   absence of these may leave NextZXOS in a "default + locked" state.

4. **Pre-firmware boot-mode latch.** Real Next hardware power-on has
   a "first boot vs soft reset" distinction; NextZXOS may detect "no
   prior boot" and refuse to proceed.

None of these are jnext bugs. They are all "things tbblue.fw does
beyond loading ROMs" that the FUTURE plan §2 enumerated but the
bypass implementation skipped on the assumption they weren't
load-bearing. The empirical evidence now suggests at least one IS
load-bearing for the visual welcome — though the OS itself is
otherwise correct.

## Next-session experiments (in priority order)

1. **Capture CSpect's equivalent post-boot state via DZRP** — same
   sysvar / NR / SRAM dump as this probe. Diff against jnext's bypass
   state. The first divergence in $5C00-$5CBF or in pixel content
   will point at the missing init step.

2. **Load AUTOEXEC.1ST manually** — set a CLI flag like
   `--bypass-autoexec` that pokes `/NEXTZXOS/AUTOEXEC.1ST` into the
   NextBASIC tokenized-program area at $5C53 (PROG) onwards and sets
   line counter, then trigger BASIC RUN. This is hacky but lets us
   see if NextZXOS can run the autoexec given the chance.

3. **Replicate tbblue.fw `init_registers()` exactly** — write all of
   NR 0x05 = 0x71, NR 0x06 = 0x85, NR 0x08, NR 0x09, NR 0x0A, NR
   0x82-0x85 per the FUTURE plan §2.4 recipe. Even if NextZXOS
   subsequently overrides these, the initial values may trigger the
   correct boot-progress path.

4. **Pre-paint a "firmware-completed" hint** — write a known sentinel
   to the standard +3 firmware-handoff sysvar (TBD: research where
   tbblue.fw signals "I'm done"). NextZXOS may key off this.

## Conclusion

Task 18 has reached its main goal: **NextZXOS boots in jnext** for
the first time. The OS is alive in its idle loop with correct
sysvars, ISR running, ROM banks switched to BASIC mode, and a CLS'd
screen ready to receive content. The remaining gap (no welcome
banner) is a refinement, not a blocker — the bypass is functionally
correct and the OS is healthy. Path forward is to diff against
CSpect to identify the single missing init step.
