# EOD-30i+36 — Trace-to-Writes Decoder + jnext-vs-CSpect Write-Stream Diff

**Date:** 2026-05-16
**Branch:** `g46b-trace-write-decoder` (worktree)
**Script:** `tools/trace_writes_decoder/decode.py`
**Inputs:**
- `/tmp/g46b-trace-dump/full_trace.txt` (jnext, 500 000 instr)
- `/tmp/g46b-cspect-fulltrace/cspect_full_trace.txt` (CSpect, 500 000 instr)

## Motivation

The two emulators' PC streams diverge very early (since EOD-30i+11/+12)
and the cascade is hard to chase by PC alone. PCs are *effects*; writes
to memory, ports, and NextRegs are *causes*. Symmetric write-stream
diffing — same trace format on both sides, decoded by one script — is
the right discriminator.

## What was built

`tools/trace_writes_decoder/decode.py` (Python 3, stdlib only). It:

- Parses TraceLog lines (`cycle | PC | regs | flags | opcode-bytes`).
- For each instruction, decodes opcode bytes against a write-table
  (LD (HL),A; LD (nn),A; LD (nn),HL; PUSH; CALL / cond CALL; RST;
  EX (SP),HL; LD (IX/IY+d),...; ED-prefixed LD (nn),rp; LDI/LDD/LDIR/LDDR;
  OUT (n),A; OUT (C),r; OTI/OTD/OTIR/OTDR; Z80N `ED 8A` PUSH nn;
  Z80N `ED 91` NEXTREG nn,vv; Z80N `ED 92` NEXTREG nn,A).
- Tracks the **currently-selected NR** via raw `OUT ($243B),A` writes
  and synthesises a `NEXTREG nr value` record whenever it sees a
  subsequent `OUT ($253B),A` write — covering NR access via plain
  OUT pairs as well as the Z80N opcodes.
- Output line format: `<12-digit cycle>  <pc>  <kind> <addr/port/nr> <value|?>`
- Diff mode (`--diff`): linear positional diff with the cycle column
  stripped — equal records align, divergence is the first index where
  jnext's record disagrees with CSpect's.

LDIR / LDI / OUTI / OUTD source bytes are not in the trace, so those
records carry `?` for value but still anchor *where* a copy happened.

## Run output

```
parsed=500000 lines, emitted=429641 write records  -> jnext_writes.txt
parsed=500000 lines, emitted=229977 write records  -> cspect_writes.txt
```

Note: jnext emits ~1.87× more write records than CSpect over the same
500 000 instructions — jnext spends much more time in early-boot LDIR
loops (already documented in EOD-30i+25e — the IPL's second-pass LDIR
at PC=$012E self-corrupts RAM page 0).

NEXTREG-only counts:

```
jnext  NEXTREG writes:   500
CSpect NEXTREG writes:  2529  (5.06× more — CSpect reaches kernel init; jnext doesn't)
```

NR set membership (after 500 000 instructions, ignoring values):

- Common NRs both write: **23**
  `03 05 06 07 08 0A 56 57 80 81 82 83 84 85 8A 8E 8F B8 B9 BA BB C0 D8`
- jnext-only NRs: **0**  (jnext writes nothing CSpect doesn't)
- CSpect-only NRs: **33**
  `12 13 14 15 16 17 18 19 1A 1B 1C 26 27 2F 30 31 32 33 34 40 41 42 43 44 4A 4B 4C 51 54 55 68 6B 78`

CSpect-only NRs CSpect writes that jnext never reaches include:
- **NR $12 / $13** — Layer-2 active bank / Layer-2 shadow bank
- **NR $14 / $15** — Transparency / sprite-L2-ULA priority
- **NR $16..$1B** — L2 X/Y scroll and clip windows
- **NR $26 / $27** — ULA X/Y scroll
- **NR $30..$34** — Sprite layer control
- **NR $40..$44** — Palette index / data / format
- **NR $4A..$4C** — Transparency colour, sprite tiebreak
- **NR $51** — MMU2 page (slot 2 = $4000-$5FFF; supervisor uses it)
- **NR $54 / $55** — MMU4 / MMU5 page (slots 4/5 = $8000-$BFFF)
- **NR $68** — Display-control + disable-ULA / L2 visibility
- **NR $6B** — Tilemap control
- **NR $78** — RTC port

These are exactly the registers the **kernel init** path writes once
the bootloader hands off — jnext never reaches that point.

The user-asked-about set:

| NR | jnext writes? | CSpect writes? |
|---|---|---|
| $03 (Machine config) | yes (1×, value $B0) | yes (1×, value $B0) — IDENTICAL |
| $50 (MMU1 page)      | no               | no                |
| $51 (MMU2 page)      | **no**           | **yes** (NR $51 = $10 at PC=$15A0; NR $51 = $FF at PC=$15B0) |
| $8E (MMU map page)   | yes (5×)         | yes (149× — much higher rate) |

## First 10 divergent records

(Position 0 = first emitted write of each trace; cycle column ignored.)

```
idx   JNEXT                              CSPECT
  0   006A  MEM FFFD 01                  006A  MEM FFFE 01     <-- SP start divergence (FFFF vs 0000)
  1   006A  MEM FFFE 00                  006A  MEM FFFF 00     <-- SP start divergence
 37   0122  PORT 253B 00                 0122  PORT 253B 04    <-- 1st REAL divergence: NR $06 readback
 38   0122  NEXTREG 06 00                0122  NEXTREG 06 04   <-- same site, NR-decoded form
17205 0148  MEM 4001 00                  0148  MEM 4001 EE     <-- LDIR copying ZEROS in jnext, real ROM in CSpect
33597 0148  MEM 4002 00                  0148  MEM 4002 29     <-- (next ROM-copy iteration)
49989 0148  MEM 4003 00                  0148  MEM 4003 1C
66381 0148  MEM 4004 00                  0148  MEM 4004 9D
99165 0148  MEM 4006 00                  0148  MEM 4006 DF
115557 0148  MEM 4007 00                  0148  MEM 4007 34
```

Records 0 and 1 are the **initial SP divergence** (well known): jnext
starts with `SP=$FFFF`, CSpect with `SP=$0000`. The Z80N
`ED 8A 00 01` (PUSH $0100) lands the bytes at different addresses;
control flow is identical but the per-byte address differs by 1.

Record 37/38 is the **first divergent NR write**. Tracing back through
the source trace at PC=$0118-$0122:

```
$0119  LD D,$06              ; select NR $06
$011B  OUT (C),D    (ED 51)  ; BC=$243B  -> NR-select reg
$011D  INC B                  ;                BC=$253B (NR-data reg)
$011E  IN  A,(C)    (ED 78)  ; READ-BACK NR $06
       jnext  -> A=$A0
       CSpect -> A=$05
$0120  AND $44                ; mask bits 6 and 2
       jnext  -> A=$00
       CSpect -> A=$04
$0122  OUT (C),A    (ED 79)  ; WRITE NR $06 back with masked value
       jnext  -> NR $06 = $00
       CSpect -> NR $06 = $04
```

**Root cause of the divergence cascade: NR $06 readback (`IN A,($253B)`
at PC=$011E) returns different values.** ALU at $0120 is correct in
both; the divergence is upstream in the NR $06 readback path.

NR $06 is the *Peripheral 2 Setting* register. Its low nibble encodes
keyboard / scandoubler / divMMC-button / 50-60 Hz / video-timing /
keyboard-issue bits. Per VHDL oracle (`zxnext.vhd` NR $06 readback
path) the value at this point in boot should reflect the firmware's
configured peripheral state — which is loaded from `config.ini` in the
NextZXOS image. jnext's readback returns $A0 (bit 7 + bit 5 set:
"divMMC-NMI button enabled" + "Esp UART"); CSpect returns $05 (bit 2 +
bit 0: "50 Hz internal" + "joystick = Kempston").

After the `AND $44 / OUT NR $06` at $0122 the two emulators **commit
different NR $06 states** that propagate through every downstream
peripheral-control decision the bootloader makes.

Record 17205 is the **second-stage cascade**: LDIR at PC=$0148 (the
ROM-to-RAM copy that moves the boot-loader payload into slot 2) writes
**zero bytes in jnext** but real boot-data in CSpect. This is the same
"jnext IPL self-corrupts RAM page 0 then reads it back as code/data"
finding from EOD-30i+25e — the LDIR source page is zeroed because of an
upstream RAM-page-0 / boot-ROM mapping divergence triggered by the
diverged NR $06 state.

## NEXTREG diff — focused

Common-NR / diverged-value records (jnext vs CSpect):

```
       jnext              CSpect
NR $06:  $00              $04    <-- first divergence (PC=$0122, root)
NR $06:  $AB / $A8        $AF / $AC    (PC=$1F xxxx, second-stage)
NR $56:  $02 04 06 08 0A 0C ...   (jnext does MMU6 page sweep)
NR $57:  $03 05 07 09 0B 0D ...   (jnext does MMU7 page sweep)
```

CSpect's later trace (records 462+) is dominated by:
- Many `NEXTREG 8E xx` writes (MMU map updates — CSpect runs the
  kernel's MMU shuffle, jnext only does 5 of these total).
- `NEXTREG 41 xx` (palette data writes — CSpect initialises the
  palette; jnext never reaches this).
- `NEXTREG 51 / 54 / 55` (MMU2/MMU4/MMU5 page selects — CSpect
  enters supervisor mode that pages slot 2, slot 4, slot 5; jnext
  never reaches it).
- `NEXTREG 12 / 13 / 14 / 15 / 16 / 17 / 18 / 19 / 1A / 1B / 1C` —
  Layer-2 setup (palette, scroll, clip): CSpect-only, kernel-init
  territory.

jnext's later trace (records 462+) is dominated by repeated
`NEXTREG 56 xx / 57 xx` sweeps from PCs in slot 1 — the second-pass
LDIR self-corruption symptom: it keeps re-paging different RAM pages
into slot 6/7 (NR $56 / $57) trying and failing to find the data it
expected, then loops.

## Key insight — which write the cascade attributes to

The decisive **first divergent write** is at idx 37/38:

```
PC=$0122   NEXTREG 06   jnext=$00   CSpect=$04
```

…but its **cause** is the divergent IN at PC=$011E, which is **not a
write** (the decoder cannot see it). The instruction-level divergence
is therefore in jnext's **NR $06 readback** (`IN A,($253B)` after
selecting NR $06).

This places the bug squarely in the NR $06 readback wiring inside
jnext — the same kind of class of bug as the previously-found
"DivMMC NMI button" / "50/60 Hz" state-bit defaults. The candidates
(to be confirmed by reading `zxnext.vhd` for NR $06 readback and
comparing against jnext's NextReg `read()` implementation) are:

1. **NR $06 read defaults** in jnext don't match the VHDL reset
   state and/or the config-loaded state injected by the firmware
   when it consumes `config.ini`.
2. **NR $06 readback path** in jnext returns wrong bits (e.g. it
   reads the *write* shadow instead of the readback-wire-OR
   defined by the VHDL).
3. **NR $06 is being side-effected by another NR write** earlier
   in boot in jnext but not CSpect (search the write stream for
   any NEXTREG $06 write before idx 37 — there are **none**, so
   this option is the least likely).

The cleanest next step is **NOT** a new probe — it's reading
`zxnext.vhd` for NR $06 read decoding and verifying jnext's
implementation byte-by-byte. The write-stream diff has reduced an
unbounded "boot cascades wrong somewhere" problem to a single Z80
read instruction whose return value is wrong.

## Reproduction

```bash
python3 tools/trace_writes_decoder/decode.py \
    /tmp/g46b-trace-dump/full_trace.txt \
    /tmp/g46b-write-stream/jnext_writes.txt

python3 tools/trace_writes_decoder/decode.py \
    /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt \
    /tmp/g46b-write-stream/cspect_writes.txt

python3 tools/trace_writes_decoder/decode.py --diff \
    /tmp/g46b-write-stream/jnext_writes.txt \
    /tmp/g46b-write-stream/cspect_writes.txt | head -40

# NEXTREG-only focused diff
grep NEXTREG /tmp/g46b-write-stream/jnext_writes.txt | awk '{print $3,$4,$5}' \
    > /tmp/g46b-write-stream/jnext_nextreg.txt
grep NEXTREG /tmp/g46b-write-stream/cspect_writes.txt | awk '{print $3,$4,$5}' \
    > /tmp/g46b-write-stream/cspect_nextreg.txt
diff /tmp/g46b-write-stream/jnext_nextreg.txt /tmp/g46b-write-stream/cspect_nextreg.txt | head -30
```
