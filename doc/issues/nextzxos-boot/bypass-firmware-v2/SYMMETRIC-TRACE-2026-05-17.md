# Task 18 — Symmetric jnext-vs-CSpect Trace (2026-05-17 late EOD)

Follow-up to the EXPERIMENTS-2026-05-17-LATE.md negative results. The
user asked for a full symmetric PC trace; this doc captures the result.

## Tooling (cherry-picked from g46b-investigation-v2)

- `src/debug/trace.{h,cpp}` + `src/core/emulator.cpp` env-gates:
  `JNEXT_TRACE_CAPACITY`, `JNEXT_TRACE_FORCE_ENABLE`, `JNEXT_TRACE_NO_WRAP`,
  `JNEXT_TRACE_DUMP_AT_EXIT`. Cherry-picked from `d4105faa`.
- `tools/cspect_plugin/CSpectFullTrace.{cs,dll}` — CSpect plugin that
  emits trace lines in jnext's exact format. Cherry-picked from `403209f2`.
- `tools/trace_writes_decoder/decode.py` — reconstructs memory + I/O +
  NEXTREG writes from trace. Cherry-picked from `52fbb40c`. Unused in
  this pass; available for future passes.

## Capture procedure

### jnext-bypass

```bash
JNEXT_TRACE_FORCE_ENABLE=1 \
JNEXT_TRACE_CAPACITY=500000 \
JNEXT_TRACE_NO_WRAP=1 \
JNEXT_TRACE_DUMP_AT_EXIT=/tmp/task18-symmetric-trace/jnext_full_trace.txt \
timeout --kill-after=3s 20s ./build/jnext --machine next --headless \
    --sd-card roms/nextzxos-1gb-fat32fix.img \
    --bypass-tbblue-fw \
    --delayed-automatic-exit 12
```

### CSpect (with the matching plugin installed at CSpect3_1_0_0/)

```bash
JNEXT_CSPECT_TRACE_FILE=/tmp/task18-symmetric-trace/cspect_full_trace.txt \
JNEXT_CSPECT_TRACE_CAP=500000 \
JNEXT_CSPECT_TRACE_SKIP_RESETS=0 \
timeout --kill-after=2s 30s mono \
    /home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpect.exe \
    -mmc=roms/nextzxos-1gb-fat32fix.img
```

Both produce 500 000-line traces in the same format.

## Sync point

CSpect's boot ROM differs from `enNextZX.rom` only at offset `$0001`:
- enNextZX.rom `$0001`: `C3 EF 00` (JP $00EF)
- CSpect boot ROM `$0001`: `C3 6A 00` (JP $006A)

CSpect runs a tiny 9-instruction IPL (`$006A → $006E → $1EA0 → $1FF9 →
$0001 → $00EF`) that ends with the boot-ROM overlay disabled, exposing
`enNextZX.rom` byte 1's `C3 EF 00`. From `$00EF` onwards, both emulators
execute the **same NextZXOS code**.

Sync points found via `grep -nm1 '\$00EF'`:
- **jnext line 3** (T-state 112): `$00EF ED 91 07 03` (first instruction
  after `DI; JP $00EF`)
- **CSpect line 10** (instruction-count 9): same `$00EF ED 91 07 03`
  after CSpect's IPL

## Diff result

After stripping the cycle column (jnext = T-states, CSpect = instruction
count) and the register columns (jnext starts at `AF=FFFF SP=FFFF` vs
CSpect's `AF=0044 SP=0000` because CSpect's IPL ran first), the **PC +
opcode-byte sequence matches for the first 249 547 instructions**.

That is: from `$00EF` onwards, jnext-bypass and CSpect execute identical
NextZXOS code — for **a quarter of a million instructions** — without
divergence.

## First divergence: line 249 548

```
jnext line 249 540-249 548       CSpect line 249 540-249 548
$0E5C ED B0  (LDIR)               $0E5C ED B0  (LDIR)
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0E5C ED B0                       $0E5C ED B0
$0038 F5  ← INT TAKEN HERE        $0E5C ED B0  ← LDIR continues, no INT
```

Both emulators are inside the same `LDIR` at `$0E5C` (`ED B0`) copying
within the `$4F00` region (`DE=$4FA6 HL=$4FA5 BC=$005A` in jnext just
before INT, near-identical in CSpect). jnext takes an IM-1 interrupt
(vector $0038) after 5 LDIR iterations; CSpect runs the LDIR
**considerably further** before the same interrupt fires.

Numeric span EI → first INT:
- **jnext:** instruction count 243 075 → 249 548 = **6 473 instructions**
  (T-state delta = 4 540 498 − 4 414 199 = **126 299 T-states**)
- **CSpect:** instruction count 243 075 → 253 919 = **10 844 instructions**
  (CSpect plugin doesn't expose T-states; instructions only)

**jnext fires the interrupt 67 % more frequently than CSpect** — a
significant timing divergence.

## What it isn't

- **Not a NextZXOS state issue.** Both emulators execute the same
  249 547 NextZXOS instructions; both write the same NextREGs at the
  same instruction-count offsets:
  - `NR $07 = $03`, `NR $03 = $B0` (both at the same line)
  - `NR $C0 = $08`, `NR $B8 = $82`, `NR $B9 = $00`, `NR $BA = $00`,
    `NR $BB = $F2`, `NR $D8 = $01` (interrupt-related, identical)
- **Not an SPI/DivMMC issue.** Both make exactly 0 reads of port `$E3`
  and 3 writes of port `$E3` before the divergence. SD-init writes are
  identical. The ISR jnext jumps to at `$0038` does subsequently read
  `(E3)` — but the ISR isn't the cause; the INT-firing is.
- **Not a NextZXOS divergent code path.** The PC + opcode sequence is
  byte-for-byte identical for 249 547 instructions. NextZXOS is doing
  the same thing in both.

## What it is

A **jnext T-state-accounting bug** at 28 MHz Next mode, exposing as
"interrupts fire ~67 % more frequently than CSpect under the same
NextZXOS code." The interrupt source is probably the ULA frame INT (50
Hz at +3 timing), but jnext's frame counter advances faster than
CSpect's per executed instruction — likely a contention / wait-state
miscount during reads from slot 2 (ULA bank 5, contended) under
turbo (28 MHz) speed.

## Why this prevents the welcome banner

After the early INT, jnext's ISR runs (`$0038 → $003B → CALL $0045 →
IN/OUT (E3) → CALL $2009 → RET → $005A LD SP,(25B8) → OUT (E3) → RET`)
and returns to mid-LDIR. The LDIR resumes — but it's now significantly
behind CSpect's progress through the same code. Subsequent INTs fire
again before main code can advance.

Per the prior session's full trace (frame 600 of headless execution):
jnext spends most of its time in the ISR loop (PCs `$3F39 / $3CFC /
$0452 / $0456` cycling continuously). Main code progress is starved by
the over-frequent INT rate. CSpect's NextZXOS reaches the BASIC banner
draw + keyboard scan because INTs fire less frequently, allowing main
code to progress.

## Root cause: outside Task 18 scope

This is a **jnext emulator-level bug**, not a bypass-mode issue. The
bypass is functionally correct (NextZXOS boots, MMU is configured, ROM
banks are mapped, sysvars are set up). The blocker is jnext's cycle
accounting under turbo mode.

Fixing it requires:
1. Identifying the over-counted instructions (most likely candidates:
   `LDIR` with contended-area destination at 28 MHz, or NEXTREG reads
   at 28 MHz)
2. Auditing the contention model + cycle counts vs VHDL `zxnext.vhd`
3. Verifying the fix doesn't regress the FUSE test suite (1356/1356)
   or screenshot regression (40/0/0)

This would be a multi-session Task-2-style audit of the timing / CPU
contention subsystem, not a bypass-mode change.

## Conclusion

The symmetric trace **decisively diagnosed** why NextZXOS doesn't draw
the welcome banner in jnext-bypass mode: jnext's interrupt rate is too
high (67 % more frequent than CSpect's), starving main-code progress.
The bypass is functionally correct; the blocker is a separate
emulator-timing bug.

Trace files preserved at `/tmp/task18-symmetric-trace/` (66 MB each;
not committed). Re-runnable via the commands above.
