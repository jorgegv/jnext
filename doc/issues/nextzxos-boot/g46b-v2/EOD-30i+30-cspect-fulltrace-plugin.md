# G46(b) EOD-30i+30: CSpect full-trace plugin — first divergence found

**Date:** 2026-05-16

## Objective

Build a CSpect-side full-instruction tracer that produces output in the
exact same format as jnext's `TraceLog::export_to_file()`, then diff
both 500K-instruction traces to locate the first divergent instruction.

## Plugin code path

`tools/cspect_plugin/CSpectFullTrace.cs` — new standalone plugin, lines 1-275.

The built DLL is installed as
`/home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpectFullTrace.dll`.

## Hook mechanism used: `Memory_EXE` at every address

The CSpect plugin API provides `Memory_EXE` hooks that fire on every M1
cycle (instruction fetch) for a specific registered address. There is no
global "fire on every instruction" hook and no T-state counter in the
plugin API.

The plugin registers `Memory_EXE` for all 65536 addresses (0x0000..
0xFFFF) in `Init()`. This fires `Read()` for every instruction fetch
regardless of PC. The existing `JnextG46bTrace.cs` already uses 8192
such hooks (for $0000-$1FFF) without performance issues; 65536 hooks
proved equally stable.

No other hook types are registered.

## Cycle field limitation

jnext's "cycle" field is T-states (master clock cycles). CSpect's plugin
API does not expose T-states or any cycle counter. The plugin substitutes
a monotonic instruction count (0, 1, 2, …) as the cycle field. All other
fields (PC, registers, flags, opcode bytes) use the real values from
`GetRegs()` and `Peek()`.

This means the cycle column DIFFERS numerically between the two traces.
The diff script strips the cycle column before comparison.

## Output strategy: progressive flush to disk

The plugin writes directly to a `StreamWriter` and flushes every 10,000
instructions. This ensures data survives even if CSpect is killed with
SIGKILL (the `--kill-after` hard-kill from `timeout`). The old approach
(buffering 500K strings in a `string[]` and writing all at once in
`Quit()`) would lose all data on SIGKILL.

## CSpect run invocation

```bash
pkill -9 mono 2>/dev/null; sleep 1

# Remove JnextG46bTrace.dll temporarily to avoid double-fire on $0000-$1FFF:
mv /home/jorgegv/src/spectrum/CSpect3_1_0_0/JnextG46bTrace.dll \
   /home/jorgegv/src/spectrum/CSpect3_1_0_0/JnextG46bTrace.dll.bak

mkdir -p /tmp/g46b-cspect-fulltrace

JNEXT_CSPECT_TRACE_FILE=/tmp/g46b-cspect-fulltrace/cspect_full_trace.txt \
JNEXT_CSPECT_TRACE_CAP=500000 \
mono /home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpect.exe \
    -w2 -zxnext -nextrom \
    -mmc=/home/jorgegv/src/spectrum/jnext/roms/nextzxos-1gb-fat32fix.img \
    -tv -basickeys -brk -exit -nocrop \
    >/tmp/g46b-cspect-fulltrace/cspect.stdout 2>&1 &

# Monitor until 500K lines captured, then SIGTERM:
until [ "$(wc -l < /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt 2>/dev/null)" -ge 499990 ]; do
    sleep 5
done
kill -TERM $(pgrep mono); sleep 5; pkill -9 mono 2>/dev/null

# Restore:
mv /home/jorgegv/src/spectrum/CSpect3_1_0_0/JnextG46bTrace.dll.bak \
   /home/jorgegv/src/spectrum/CSpect3_1_0_0/JnextG46bTrace.dll
```

## Trace file statistics

```
wc -l /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt
500000 /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt

wc -l /tmp/g46b-trace-dump/full_trace.txt
500000 /tmp/g46b-trace-dump/full_trace.txt

ls -la /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt
-rw-r--r-- 1 jorgegv jorgegv 66729208 May 16 22:12 /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt
```

CSpect captured 500K instructions in under 5 seconds of wall-clock time.

## First 5 lines of CSpect trace (format spot-check)

```
000000000000  $0000  AF=0000 BC=0000 DE=0000 HL=0000  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=0000 SP=0000  [--------]  F3
000000000001  $0001  AF=0000 BC=0000 DE=0000 HL=0000  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=0000 SP=0000  [--------]  C3 6A 00
000000000002  $006A  AF=0000 BC=0000 DE=0000 HL=0000  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=0000 SP=0000  [--------]  ED 8A 00 01
000000000003  $006E  AF=0000 BC=0000 DE=0000 HL=0000  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=0000 SP=FFFE  [--------]  C3 A0 1E
000000000004  $1EA0  AF=0000 BC=0000 DE=0000 HL=0000  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=0000 SP=FFFE  [--------]  AF
```

Format matches jnext exactly: 12-digit cycle, $PC, AF= BC= DE= HL=
AF'= BC'= DE'= HL'= IX= IY= SP=, [flags], opcode-bytes.

## Diff methodology

```bash
# Strip cycle column (field 1 in awk):
awk '{$1=""; print}' /tmp/g46b-trace-dump/full_trace.txt > /tmp/g46b-cspect-fulltrace/jnext_no_cycle.txt
awk '{$1=""; print}' /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt > /tmp/g46b-cspect-fulltrace/cspect_no_cycle.txt

# PC-only diff to find first divergent control flow:
awk '{print $2}' /tmp/g46b-trace-dump/full_trace.txt > /tmp/g46b-cspect-fulltrace/jnext_pcs.txt
awk '{print $2}' /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt > /tmp/g46b-cspect-fulltrace/cspect_pcs.txt
diff jnext_pcs.txt cspect_pcs.txt | head -5
```

## Findings: first divergence

### PC stream alignment

The PC streams match for **206,513 instructions**. Both emulators execute
the exact same PC sequence from $0000 through the IPL's NextReg init
loop, the attribute-table LDIR ($012E), the SD card I/O loops, and the
supervisor dispatcher — all in identical order for 206,513 steps.

The first PC-level divergence is at trace line 206,514:

```
jnext  206514: $1F01  opcode=23 (INC HL)
cspect 206514: $1F01  opcode=ED 91 8E 7A (NEXTREG $8E,$7A)
```

Both emulators returned from the supervisor's `RET` at $3E17 to address
$1F01. But the **memory contents at $1F01 differ**: jnext has `23` (INC
HL from the patched IPL code), CSpect has `ED 91 8E 7A` (a NEXTREG
instruction from the NextZXOS loader). This is the primary symptom: jnext
maps wrong physical memory into slot 0 at this point.

### Register divergence: first non-trivial

The PC stream match of 206,513 lines hides ongoing register divergence.
Key fields that differ throughout:

| Field | jnext value | CSpect value | First diverge at |
|-------|-------------|--------------|-----------------|
| SP    | $FFFF       | $0000        | line 1 (power-on) |
| AF'   | $FFFF       | $0000        | line 1 (power-on) |
| IX    | $0000       | $0063        | line 193,304     |
| AF' (non-trivial) | $0043 | $0843 | line 193,411 |

The SP and AF' power-on-state differences (FFFF vs 0000) are expected:
jnext initializes registers to $FFFF at cold boot, CSpect to $0000.

### Root cause: `IN A,(C)` at PC=$011E reads different NR $06 value

The first divergence that matters causally is at **trace line 28**, PC=$011E:

```
jnext  line 28: $011E AF=0020 ... ED 78   → A = $A0 after IN A,(C)
cspect line 28: $011E AF=0020 ... ED 78   → A = $05 after IN A,(C)
```

The instruction `ED 78` = `IN A,(C)` reads from port BC=$253B (NextReg
data port) with the NextReg selector set to NR $06 (selected by the
immediately preceding `OUT ($243B), D` with D=$06 at PC=$011B).

- **jnext**: NR $06 reads $A0
- **CSpect**: NR $06 reads $05

NR $06 = "Peripheral 1 Setting" register (or in some Next firmware
contexts, Turbo Mode). The CSpect value $05 is the expected post-boot
NextReg $06 state on ZX Spectrum Next hardware. The jnext value $A0 is
wrong.

This is the **first causally upstream divergence**: the IPL reads NR $06
and gets the wrong value in jnext. All subsequent register differences
(IX, AF') propagate from this single bad read. The A register at $011E
becomes $A0 in jnext vs $05 in CSpect; the IPL uses this value to drive
its initialization loop (DE=$06 sets the loop count); IX is loaded from
A at PC=$01AF (`DD 6F` = LD IXL, A) 193,303 instructions later.

### Why NR $06 returns $A0 in jnext

NR $06 in the jnext 5-flag boot context has value $A0 rather than the
hardware default $05. This may reflect:

1. **`JNEXT_PATCH_ROM_BANK0_WITH_IPL=1`** patches the ROM that gets
   loaded; the NextReg init sequence in the patched ROM may write NR $06
   to a wrong value before the `IN A,(C)` scan.
2. The jnext `JNEXT_USE_CSPECT_BOOTROM_16KB=1` flag loads a 16KB boot
   ROM that writes different NextReg values before the scan.
3. jnext's cold-boot NextReg reset state for NR $06 itself differs from
   the hardware default ($05).

The first two hypotheses relate to the 5-flag boot experiment; hypothesis
3 would indicate a VHDL compliance issue in jnext's NR $06 cold-reset
logic.

### The PC-stream divergence (line 206,514) is a consequence

The 206,513-instruction PC match does NOT mean the two emulators are
producing equivalent computation: their register state diverges at line 28
(NR $06 read), and this divergence grows. The reason the PC stream
continues to match for so long is that the IPL code is deterministic on
PC for a wide range of A values — the loop counter varies (IX changes)
but the main control flow path is the same until the SD card returns
different data or a supervisor NR write triggers a different mapping.

The PC-stream divergence at line 206,514 (memory at $1F01 differs)
confirms that by that point the slot-0 physical page mapping has diverged
— jnext never switched slot 0 away from the patched boot ROM, while
CSpect had already paged in the NextZXOS loader code.

## Side-by-side: first divergent lines (PC stream)

```
jnext  line 206513: 000004090090  $3E17  AF=6F3A BC=4070 DE=0100 HL=DCBA  AF'=0043 BC'=3FFE DE'=FFFD HL'=FFFE  IX=0000 IY=0000 SP=5BF9  [--5H3-N-]  C9
cspect line 206513: 000000206512  $3E17  AF=6F3A BC=4070 DE=0100 HL=DCBA  AF'=0843 BC'=3FFE DE'=FFFD HL'=FFFE  IX=0008 IY=0000 SP=5BF9  [--5H3-N-]  C9
                                                                                    ^^^^                                  ^^^^
                                                                              AF' differs                           IX differs
jnext  line 206514: 000004090100  $1F01  AF=6F3A BC=4070 DE=0100 HL=DCBA  AF'=0043 BC'=3FFE DE'=FFFD HL'=FFFE  IX=0000 IY=0000 SP=5BFB  [--5H3-N-]  23
cspect line 206514: 000000206513  $1F01  AF=6F3A BC=4070 DE=0100 HL=DCBA  AF'=0843 BC'=3FFE DE'=FFFD HL'=FFFE  IX=0008 IY=0000 SP=5BFB  [--5H3-N-]  ED 91 8E 7A
                                                                                                                                                       ^^^^^^^^^^^^
                                                                                                                                              opcode DIFFERENT ($1F01 contents differ)
```

The differences at line 206,513 (AF' and IX) are pre-existing from line 193K.
The difference at line 206,514 (opcode bytes = memory contents at $1F01) is
the first **memory-mapping** divergence in the trace.

## Next-session priority

1. **Verify hypothesis 3 (NR $06 cold-reset)**: check jnext's NR $06
   reset value in `src/peripheral/nextreg.cpp` or wherever NR $06 is
   initialized. Compare against VHDL `zxnext.vhd` reset value for NR $06.
   If jnext resets NR $06 to $A0 instead of $05, that's a standalone VHDL
   compliance bug independent of the 5-flag experiment.

2. **Verify without 5-flag**: re-run jnext with ONLY `JNEXT_TRACE_*` flags
   (no boot-rom patches) and capture NR $06 value at PC=$011E using a
   targeted probe. If NR $06 is still wrong without the patched ROM, it's
   a standalone emulator bug.

3. **Audit `$1F01` memory mapping**: at trace line 206,513 both emulators
   execute `NEXTREG $8E, $02` at $3E13 (mapping physical ROM page $02 into
   slot 0). In jnext, this clearly doesn't work as intended — after the
   write, $1F01 still has old boot ROM code. Audit jnext's NR $8E handler
   vs VHDL `zxnext.vhd` to see why the slot-0 mapping doesn't update.
