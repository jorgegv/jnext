---
name: dzrp-compare
description: Compare jnext vs CSpect at a specific Z80 PC using DZRP — captures regs, SP, MEM[SP..+15], and slot mapping at the same PC in both emulators and diffs the result. Use when the user says "compare jnext vs CSpect at PC X", "DZRP probe at X", "run a DZRP comparison", or describes a boot-time divergence that needs jnext-vs-CSpect diff at a specific code site.
---

# DZRP compare

The standard G46(b) investigation move: pause both jnext and CSpect at the same Z80 PC and diff captured state.

## Inputs

Ask the user (if not specified):

- **PC** in hex (e.g. `5B48`, `27A3`, `0322`).
- **Duration** in seconds (default 10).
- **Probe scope:** just regs+SP+stack, or also slot mapping (NextREG $50..$57), MMU state, port_7ffd/port_1ffd?

## Steps

1. Start CSpect in DZRP debug mode (background):
   ```bash
   mono ../CSpect3_1_0_0/CSpect.exe -mmc $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img -debug &
   ```
   Wait ~2 seconds for it to come up.

2. Run the DZRP capture script targeting the PC:
   ```bash
   python3 tools/cspect_dzrp/dzrp_check.py --pc 0x<PC> --duration <N>
   ```
   Or pick a topic-specific script from `tools/cspect_dzrp/` if one exists for this PC range.

3. Run jnext at the same PC with an existing or newly-added env-gated probe.
   If no probe exists for this PC, invoke the `probe-add` skill first (or dispatch `boot-trace-detective` for the deeper end-to-end methodology).

4. Capture jnext for the same wall-clock window:
   ```bash
   JNEXT_G46B_<PROBE>=1 ./build/jnext --headless --machine next \
     --sdcard $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img \
     --delayed-automatic-exit <N> \
     2> /tmp/dzrp-jnext-<PC>.log
   ```

5. Diff:
   ```bash
   diff -u /tmp/dzrp-cspect-<PC>.log /tmp/dzrp-jnext-<PC>.log | head -50
   ```

6. Kill CSpect:
   ```bash
   pkill -f CSpect.exe || true
   ```

## Report format

```
## DZRP compare at PC=<PC>
- CSpect hits: N
- jnext hits: M
- First divergence: <regs/SP/MEM/slot delta>
- Likely upstream cause: <hypothesis from the delta>
- Next probe to add: <site one step upstream of the divergence>
```

Per `reference_cspect_dzrp_launch.md` and the G46(b) memory chain, the FIRST divergence in any probe window is usually one step downstream of the bug. Walk back to find the bug.

## Hard rules

- **CSpect is comparison, not oracle.** VHDL is the oracle. When CSpect ≠ VHDL, VHDL wins.
- **No bypass.** Don't add a "skip past this divergence" hack; trace the cause.
- **No band-aids.** Per `feedback_g46b_no_bypass_tbblue`.
- **Always kill CSpect when done** (it eats CPU in the background otherwise).

## When to escalate to the `boot-trace-detective` subagent

If this is the start of a deeper investigation (multiple PCs to compare, hypotheses to test, fix to ship), don't run the compare yourself — dispatch `boot-trace-detective` via the Agent tool. This skill is for one-shot comparisons; the agent is for end-to-end investigation.
