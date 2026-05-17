# G46(b)-v2 EOD-25c — supervisor loop decoded; suspected wait-for-input

Branch: `g46b-investigation-v2` HEAD `279a839`. Findings build on
EOD-25 and EOD-25b.

## NRREAD probe results (10 s headless)

400 NextReg reads via the supervisor's `$0D6B` read-helper. 10 NRs
cycled in lockstep (40 reads per NR per cycle), values nearly all
static:

| NR | val | meaning (per NextReg wiki) |
|---|---|---|
| `$05` | `$5B` | Peripheral 1: bit 7 = 50/60Hz, bits 6-5 = scanlines, etc. |
| `$06` | `$00` | Peripheral 2: AY enable / drive lock / etc. |
| `$07` | `$33`/`$00` | CPU speed (toggle: 28 MHz ↔ 3.5 MHz) |
| `$08` | `$FE` | Peripheral 3: timex video / +3 always-paged / etc. |
| `$0A` | `$11` | Anti-brick / button state |
| `$14` | `$E3` | Transparency colour fallback (legacy) |
| `$15` | `$00` | Sprite + Layer 2 system control |
| `$43` | `$00` | Palette control |
| `$4A` | `$00` | Transparency colour fallback (8-bit) |

Static values mean **the wait condition is NOT one of these read NRs**.
The supervisor reads them every iteration but their values don't change.

## RST08 probe results (10 s headless)

399 RST $08 entries from 6 distinct caller sites. **Each caller is in
a different block of enNextZX.rom** (the supervisor swaps slot 0/1
between blocks 0/2/3 during the loop):

| caller_byte | fn | block | calls | % |
|---|---|---|---|---|
| `$046D` | `$0A` | **block 3** | 160 | 40% |
| `$0069` | `$01` | block 2 | 120 | 30% |
| `$1B1B` | `$00` | block 2 | 40 | 10% |
| `$0302` | `$F5` | block 0 | 40 | 10% |
| `$192C` | `$C0` | block 2 | 38 | 9.5% |
| `$192D` | `$10` | block 2 | 1 | — |

**The fn codes (`$00`-`$0F`, `$10`, `$C0`, `$F5`) are NextZXOS
supervisor-internal RST $08 hooks**, NOT standard ESXDOS. Standard
ESXDOS function codes start at `$80+`:

```
M_GETSETDRV  = $89   F_OPEN  = $9A   F_READ  = $9D
F_CLOSE      = $9B   F_WRITE = $9E   F_SEEK  = $9F
```

NextZXOS's own supervisor uses low-numbered fn codes for internal
inter-bank dispatch + sysvar manipulation. Block 2 at `$0068+` has
three back-to-back RST $08 + fn=$01 wrappers each followed by 2-byte
inline params + RET — classic NextZXOS API trampoline pattern.

## Structural observation

The 1:1 ratio of NRREADs to RST08 entries (400:399) means **the
supervisor's outer loop iteration consists of**:

1. Read ~10 NextRegs via `$0D6B` helper (status check)
2. Call ~10 internal RST $08 hooks (bank-flip + work)
3. LDDR 4054 bytes (`$0168`) — bank shuffle in slot 6 (RAM bank 0)
4. HALT (`$3D96`, `$097B`) waiting for /INT or /NMI nudge
5. Toggle CPU speed (28 MHz `$00EF` ↔ 3.5 MHz `$099C`)
6. Repeat

40 outer iterations in 10 seconds = **4 Hz outer loop**, matching the
NR $07 toggle cadence.

## Why is the supervisor stuck?

Several layers of evidence converge on a **wait-for-async-event** pattern:

1. **CPU state**: `IFF1=0 IM=1 halted=0` consistently. HALT exits on
   /INT or /NMI pin assertion but the ISR doesn't run (IFF1 prevents
   service).
2. **Read NR values static**: NRs `$05` `$06` `$08` `$0A` `$14`-`$4A`
   never change — supervisor cycles past them every iteration with
   the same values, so they aren't the wait condition.
3. **No keyboard activity**: only 352 keyboard reads (port `$FE`) in
   15 s vs 9416 SPI reads + 5698 NextReg reads — definitively NOT
   a "wait for key press" loop at the firmware-keyboard-scan level.
4. **Real SD I/O**: DivMMC SPI ($EB) returns varied bytes ($FF/$00/
   $01/$80/$AA/$5B etc.) — supervisor is doing real reads but doesn't
   find what it expects.
5. **CPU speed bouncing**: two distinct supervisor routines
   (`$00EF` Z80N, `$099C` legacy) toggle NR $07 in lockstep —
   typical "speed down for SD I/O, speed up for compute" pattern.

## Refined hypothesis

The supervisor is in a **periodic boot-time task** — probably one of:

**(a) NextZXOS "Press SPACE for boot menu" wait loop with autoboot timer**

The `FRAMES` sysvar at `$5C78-$5C7B` is incremented by the IRQ
handler. With `IFF1=0` throughout, the IRQ never runs, so `FRAMES`
never advances. If NextZXOS's wait code reads `FRAMES` to detect
"N seconds elapsed without keypress → autoboot", that timeout never
fires. Supervisor waits forever.

This would mean the supervisor INTENTIONALLY disabled IRQs (DI at
boot), expecting the autoboot timer to read frames via the /INT pin
nudge of HALT or via NMI counter — which our emulation may not
provide.

**(b) Async SD/USB/Multiface event-wait**

The supervisor is polling for a Multiface NMI button or some other
external trigger that headless mode can't produce.

**(c) Boot-menu rendering with redraw-each-iteration**

The LDDR (4054 bytes in slot 6 = RAM bank 0) could be a screen
redraw or sprite-table refresh. Boot menu screens often redraw every
frame; if running headless = nobody pressing keys = boot menu draws
forever.

## Recommended next investigation step

**Add NMI cadence + /INT pulse probe** — instrument both pin
assertions to see whether HALT is being nudged out (which it must be,
since `halted=0` in the snapshot). If NMI/INT cadence is wrong,
that's likely a Task-2 timing edge case.

In parallel: **disassemble block 3 at `$0410-$0470`** (the routine
calling `RST $08; DB $0A` 160×/window) to see what state the supervisor
is in — that's the heaviest outer-loop activity and would name the
specific wait state.

## Alternative path — bypass the wait

If hypothesis (a) holds, we can test by **forcing autoboot**: edit
`/MACHINES/NEXT/config.ini` on the SD image to skip the menu wait
(`AUTOBOOT=YES`, no menu key timeout). If the wait disappears →
hypothesis (a) confirmed. If it doesn't → hypothesis (b) or (c).

This is a 1-mcopy test, no code changes.
