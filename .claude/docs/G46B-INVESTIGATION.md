# G46(b) investigation playbook

When jnext boots NextZXOS / TBBlue / a NEX differently from real hardware
(approximated by CSpect), this is the playbook. Sourced from 25+ G46(b) EOD
entries (April–May 2026).

## Symptom signature

- Boot stalls (supervisor never advances past a specific PC)
- "Slide" cascade (CPU executes through cleared screen RAM as NOPs)
- Wrong screen (TBBlue logo never appears, or appears garbled)
- Infinite loop in a specific bank
- Stack corruption (SP drifting between expected RST $08 frames)

If the symptom is any of the above, this playbook applies. Otherwise, treat
as a generic emulator bug.

## The methodology in 5 steps

### Step 1 — Reproduce + characterise

Run jnext:
```bash
./build/jnext --headless --machine next \
  --sd-card roms/nextzxos-1gb-fat32fix.img \
  --delayed-screenshot /tmp/jnext.png --delayed-screenshot-time 10 \
  --delayed-automatic-exit 12
```

Run CSpect:
```bash
mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img -debug
```

Compare screenshots. Write a one-paragraph description of the divergence:
when (boot stage), what (visible difference), where (which PC range / bank
state).

### Step 2 — Add env-gated probes in jnext

Follow the `JNEXT_G46B_*` pattern (`/probe-add` command does the scaffolding).
Common probe types:
- **PC-trace probes** at suspect RSTs, CALLs, RETs
- **NEXTREG-write probes** (`NR07_TRACE`, `NR8E_TRACE`, etc.) logging value +
  caller PC
- **Port-write probes** (`PORTSPY`) for 7FFD / 1FFD / DFFD
- **Stack probes** (`RST08_GAP`) logging PUSH/POP between fixed RSTs
- **Memory-mapping probes** (`PCMAP`) showing slot 0..7 mapping at trap PC
- **Snapshot probes** capturing screen RAM / supervisor state at a specific
  moment

All probes are **env-gated** (zero cost when env unset) and **non-mutating**
(read state only).

### Step 3 — DZRP comparison against CSpect

For each probe, write a matching DZRP script under `tools/cspect_dzrp/`:
- See `reference_cspect_dzrp_launch.md` for the launch incantation.
- Use `tools/cspect_dzrp/cspect_dzrp.py` as the protocol library.
- Set BP at the same PC the jnext probe fires at.
- Capture regs, SP, MEM[SP..SP+15], slot mapping (NextREG $50..$57), MMU
  state, port_7ffd, port_1ffd.

Run jnext and CSpect for the same wall-clock window. Diff. The first delta
is one step downstream of the bug.

### Step 4 — Walk back to find the FIRST divergence

The investigation principle: **the first PC / port / NEXTREG / stack op that
differs between jnext and CSpect IS the bug or one step downstream of it.**

Walk back:
- Stall observed at PC=X → why?
- Supervisor read value V at PC=Y → who wrote V?
- Port write happened at PC=Z → what state caused that path?
- … keep walking until the divergence root cause is identified.

### Step 5 — Hypothesise, test, fix

Once a delta is identified, form 2-3 hypotheses about the cause. For each:
- Predict observable consequence if hypothesis were true.
- Design a probe (env-gated, in jnext) that would falsify it.
- Run. Diff vs CSpect. Conclude.

Then apply the fix:
- Must trace to a specific VHDL line the emulator was getting wrong.
- Must ship with a discriminative regression test.
- Must NOT be a band-aid (hiding the symptom without explaining the cause).

## Hard rules

- **VHDL > CSpect.** CSpect is a *comparison target* but not the oracle. When
  CSpect and VHDL disagree (rare), VHDL wins. (`feedback_vhdl_faithful_only`)
- **No band-aids / no bypass.** Per `feedback_g46b_no_bypass_tbblue`, don't
  bypass TBBlue boot via manual key injection or ROM-side hacks. The boot
  must complete via the normal supervisor path.
- **No keypress injection.** Per `feedback_g46b_no_keypress`, don't simulate
  spacebar / enter to advance past the menu. Trace the real boot.
- **Env-gated probes only.** No conditional behavior changes. Probes log
  state, they don't alter it.
- **No file artifacts outside the repo.** Probe logs in `/tmp/g46b-<topic>/`.
  DZRP scripts in `tools/cspect_dzrp/`. (`feedback_no_files_outside_repo`)

## Concrete G46(b) findings (history)

Notable lessons from the 25+ EOD chain:

- **EOD-23** — Slide cascade traced to bank 3 slot 1 having font glyph data
  at $3F00 instead of a wrapper (CSpect has bank 1/2 there).
- **EOD-24** — `NEXTREG $8E,$03` at RAM $5B48 sets both 7FFD bit 4 AND
  1FFD bit 2 atomically per VHDL :3662-3734 → sram_rom=3 in one step.
- **V20-DIVMMC-01** — DIVMMC app-cmd bit handling regression: shipped as
  class-c but actually class-a (single-layer enumeration missed
  supervisor consumer). Reverted in commit `a84dca1f`. Lesson recorded in
  `feedback_audit_enumerate_all_protocol_consumers_per_boot_stage.md`.

## Tools available

- `tools/cspect_dzrp/cspect_dzrp.py` — DZRP protocol library
- `tools/cspect_dzrp/dzrp_check.py` — generic capture utility
- `tools/cspect_dzrp/g46b_*.py` — topic-specific scripts (15+ exist)
- The `boot-trace-detective` agent — full workflow handler
- The `/dzrp-compare` command — quick comparison at a specific PC
- The `/probe-add` command — scaffolding for new env-gated probes

## When to escalate

If after 2-3 sessions of investigation the root cause is still elusive,
escalate to the user with:

- Probes added (paths + env-var names)
- DZRP captures (script + output diffs)
- Hypotheses tested and ruled out
- Current best hypothesis + what would falsify it

The user may authorize a class-d architectural change (e.g. memory half-cycle
modeling, NMI Stackless model, IM2 controller bridge).
