---
name: vhdl
description: Look up authoritative ZX Next FPGA hardware behavior in the VHDL source at /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/. Use when the user asks "what does the VHDL say about X", "look up VHDL for X", "what's the spec for register/port/NEXTREG X", or any time hardware-spec authority is needed before writing/fixing emulator code or tests.
---

# VHDL spec lookup

Authoritative hardware spec lookup. The VHDL at
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` is the
single oracle for jnext (not CSpect, not Fuse, not ZEsarUX, not the wiki).

## Inputs

Argument or implicit from user message:

- A **subsystem name** (mmu, divmmc, ula, copper, sprite, ctc, dma, multiface, ay, nextreg, layer2, tilemap), OR
- A **free-text pattern** (e.g. `"NextREG 8E"`, `"7FFD bit 4"`, `"automap RAM bank"`).

## Steps

1. If a known subsystem name, prefer reading the subsystem's file (`device/`, `video/` or `audio/`; MMU and NextREG are in `zxnext.vhd`) in full. Otherwise grep across all `.vhd` files for the pattern.

2. For grep results, read **±30 lines** around each hit to capture the full process body.

3. For each finding, output:

```
### <file:line-range>
```vhdl
<verbatim VHDL excerpt — full process or case-when block>
```
**Interpretation:** <≤5 bullets, no hedging>
```

4. Note any coverage gaps (things the VHDL does NOT specify).

## Hard rules

Per `feedback_vhdl_faithful_only`, `feedback_test_from_vhdl`, `feedback_unobservable_audit_rule`:

- **VHDL is the single oracle.** No paraphrase from CSpect / Fuse / ZEsarUX / wiki / forum.
- **Cite, don't summarize.** Verbatim excerpt + line range + path is mandatory.
- **No invention.** If the VHDL doesn't say, say so.

## Reading traps that have shipped defects

- Before citing a line, check which `generate` block encloses it. jnext targets **Issue 2** (`g_board_issue <= 2`). A line inside e.g. `gen_fdc_5: if (g_board_issue >= 3) generate` (zxnext.vhd ~1695) is on a branch the emulated hardware does not take.
- Before claiming a signal is NOT affected by X, read every process that assigns it: latch processes as well as the output assignments. Missing from one path is not missing from all of them.
- **Signals update at process end.** After `s <= not s;`, a later `if s = '1'` in the same process reads the value on entry, not the toggled one.
- **A slice literal lands on the slice's declared bits.** `o_cpu_d(7 downto 3) <= "01000"` sets bit 6. Map each character to its declared index before naming a bit.

## When to escalate to the `vhdl-oracle` subagent

If the lookup will require more than ~3 reads or grep passes, or if the answer will feed into a non-trivial fix, dispatch `vhdl-oracle` via the Agent tool. The agent has stricter output discipline (always cites, refuses to extrapolate) and protects the main context from spurious VHDL bytes. This skill is for fast one-shot lookups.
