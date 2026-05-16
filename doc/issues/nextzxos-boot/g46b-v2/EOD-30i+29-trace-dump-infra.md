# G46(b) EOD-30i+29: JNEXT_TRACE_* dump infrastructure

**Date:** 2026-05-16

## Objective

Add trace-dump infrastructure to capture the full Z80 instruction stream
from boot start, enabling post-mortem grep/awk analysis to walk backward
from the corruption moment at step 318,550 (RAM0_0046_WTRAP).

## Code path: changes made

### `src/debug/trace.h` + `src/debug/trace.cpp`

- Added `TraceLog::resize(size_t new_capacity)` method: reassigns the
  underlying `std::vector` to the new capacity, resets head/count. Defined
  at `trace.cpp:211-218`.
- Added `TraceLog::set_no_wrap(bool v)` and `bool no_wrap_` field: when
  set, `record()` stops writing once buffer is full (preserves earliest
  entries instead of overwriting them). Modified `record()` at
  `trace.cpp:197-205`.

### `src/core/emulator.cpp`

- Added `#include <cstdlib>` (for `std::strtoull` and `std::getenv`).
- Added `Emulator::~Emulator()` destructor at approx line 43: reads
  `JNEXT_TRACE_DUMP_AT_EXIT` env var; if set and trace is non-empty,
  calls `trace_log_.export_to_file(path)` and prints
  `[trace] exported N entries to <path>` to stderr.
- Added four env-var gates at end of `Emulator::init()` (after rewind
  buffer init block, before `return true`):
  - `JNEXT_TRACE_CAPACITY=N` — resize trace buffer (clamped 1024..16M).
  - `JNEXT_TRACE_FORCE_ENABLE=1` — force-enable recording.
  - `JNEXT_TRACE_NO_WRAP=1` — stop recording when full (capture early boot).
  - `JNEXT_TRACE_DUMP_AT_EXIT` documented in comment block (handled in dtor).

### `src/core/emulator.h`

- Added `~Emulator()` destructor declaration.

All four env-var gates have zero cost when unset (single `getenv` call per
gate, executed once at init/dtor time only).

## Run command

```bash
mkdir -p /tmp/g46b-trace-dump
JNEXT_USE_CSPECT_BOOTROM_16KB=1 \
JNEXT_USE_CSPECT_BOOTROM=1 \
JNEXT_DIVMMC_BEATS_BOOTROM=1 \
JNEXT_FORCE_AUTOMAP_AT_BOOT=1 \
JNEXT_PATCH_ROM_BANK0_WITH_IPL=1 \
JNEXT_TRACE_FORCE_ENABLE=1 \
JNEXT_TRACE_CAPACITY=500000 \
JNEXT_TRACE_NO_WRAP=1 \
JNEXT_TRACE_DUMP_AT_EXIT=/tmp/g46b-trace-dump/full_trace.txt \
JNEXT_G46B_RAM0_0046_WTRAP=1 \
timeout --kill-after=5s 15s ./build/jnext --headless --machine next \
    --sd-card roms/nextzxos-1gb-fat32fix.img \
    --delayed-automatic-exit 10 \
    2>/tmp/g46b-trace-dump/run.log
```

Result:

```
wc -l /tmp/g46b-trace-dump/full_trace.txt
500000 /tmp/g46b-trace-dump/full_trace.txt
```

Trace starts at cycle 0 ($0000 DI), ends at cycle 9,322,010 (deep in LDDR
loop at $0168). The corruption at step 318,550 is fully covered — the
buffer freezes at 500,000 entries thanks to `JNEXT_TRACE_NO_WRAP=1`.

## Spot-check results

### First and second visits to PC=$012E

```bash
grep -n "012E" /tmp/g46b-trace-dump/full_trace.txt | head -3
```

```
34:000000001184  $012E  AF=0054 BC=02FF DE=5801 HL=5800  ...  ED B0
35:000000001205  $012E  AF=0054 BC=02FE DE=5802 HL=5801  ...  ED B0
36:000000001226  $012E  AF=0044 BC=02FD DE=5803 HL=5802  ...  ED B0
```

Legitimate first pass: lines 34-800, BC counts down from $02FF to $0000
(768 iterations, writing attribute table $5800-$5AFF).

Spurious second pass: line 276,674. BC=$0000 on entry, immediately wraps
to $FFFF — full 64K overwrite of IPL code.

### $3E17 (supervisor RET site)

```bash
awk '$2 == "$3E17"' /tmp/g46b-trace-dump/full_trace.txt
```

Two visits: cycle 4,090,090 (first supervisor helper call) and
4,637,263 (the fatal one that RET→$012D→$012E).

### 20 lines before second $012E (lines 276,654-276,673)

Sequence confirms ring-buffer evidence from prior probes:
```
$0251 LDIR completes (BC=$0000) at line 276,641
$0253 → $024A...$0259 → $025A DF (RST $18)
→ $0018 → $3E80...$3E97 (supervisor NR write, RET)
→ $3485 → $3486 (RST $20 wrapper)
→ $0020 → $3E00...$3E17 (second supervisor NR write, RET)
→ $012D (mid-instruction land) → $012E LDIR BC=$0000
```

## New observation from the trace

**The $0251 LDIR is the proximate caller, not $012E.** Prior probes only
captured the 16-PC ring ending at $012D→$012E, which showed $3485 and
$3486. The trace reveals that the code reaching $3485 was not returning
from the $012E LDIR itself — it was returning from a DIFFERENT LDIR at
PC=$0251 (the IPL's NextReg-area memory initialization loop). This LDIR
runs 351 iterations (BC=$015F at entry), writing to DE=$5B53...$5CB1
(system variables area). When it completes (BC=$0000), it falls through
to $024A...$025A, where `RST $18` (`$DF`) dispatches into the supervisor
wrapper at $0018→$3E80. The supervisor then fires TWO `RST $20` dispatches
in sequence (one at $3E87/$3E8A returning via $3E93/$3E97, then one at
$3E05/$3E0A returning via $3E13/$3E17). The second $3E17 RET lands at
$012D (mid-instruction of `LD (BC),A` = opcode $02 at $012D), which
falls into the LDIR at $012E with BC=$0000 from the just-completed $0251
sequence.

This identifies the exact trigger: the `RST $18` call at $025A is fired
WHILE BC=$0000 (the register was never reloaded after $0251 completed).
The supervisor then overwrites SP/stack state in a way that on return the
PC is $012D rather than $025B, and BC is still $0000. The upstream fix
question is: why does the supervisor RET to $012D instead of $025B?

## Next-session priority

1. Inspect the supervisor stack frame at $3E17 RET: what value is on the
   stack at SP=$5BF3 (the return address)? The trace shows SP=$5BF3 at
   $3E0F (LD BC,(5B54)) and SP=$5BF3 at $3E17 C9. The pushed return addr
   should be $025B (the byte after RST $20 at $025A); the fact that RET
   lands at $012D means the stack has been corrupted and holds $012D.
2. Walk the trace backward from line 276,665 ($3E0F ED 4B 54 5B =
   `LD BC,($5B54)`) to find WHEN the value $012D was written to the stack
   cell that $3E17 RET pops. This is a memory write to address SP-2 in
   the $5BF3 frame.
3. The $5B54 sysvar slot holds the return address — this is likely
   NextZXOS saving/restoring PC via sysvars. Identify which sysvar $5B54
   maps to and who writes it with $012D.
