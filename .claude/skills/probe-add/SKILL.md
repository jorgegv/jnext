---
name: probe-add
description: Add an env-gated diagnostic probe at a specific code site in jnext (the JNEXT_G46B_* pattern — zero cost when env unset, logs to stderr when on). Use when the user says "add a probe at X", "instrument X", "add a G46B probe", or describes wanting to log emulator state at a specific PC/port/NEXTREG/memory event.
---

# Add a diagnostic probe

Add a new env-gated diagnostic probe in jnext following the project's established `JNEXT_G46B_*` pattern.

## Inputs

Ask the user (if not specified):

- **Probe name** in UPPER_SNAKE_CASE (e.g. `RST08_TRACE2`, `NR8E_TRACE`, `PORTSPY`, `PCMAP`).
- **Site description** — what to instrument (e.g. "every NEXTREG $8E,$XX write", "RST $08 caller PC", "OUT (7FFD) writes", "memory reads in $4000-$57FF").
- **What to capture** — PC, regs, SP, stack[0..3], slot mapping, port state, memory bytes, etc.

## Steps

### 1. Locate the call site

Use grep to find the exact function. Common sites:

- `src/cpu/z80_cpu.cpp` — instruction-level probes (RST, CALL, RET, PC trace)
- `src/memory/mmu.cpp` — bank-swap / NEXTREG / port write probes
- `src/peripheral/sd_card.cpp` — SD protocol probes
- `src/peripheral/divmmc.cpp` — DivMMC automap probes
- `src/port/port_dispatch.cpp` — port-write probes

### 2. Wrap in env-var gate

Pattern (matching existing G46B probes):

```cpp
// Top of file, with other env-var probe statics:
static const bool g46b_<NAME> = []() {
  const char* e = std::getenv("JNEXT_G46B_<NAME>");
  return e && std::atoi(e) != 0;
}();

// At the call site:
if (g46b_<NAME>) [[unlikely]] {
  // Log to stderr, following src/peripheral/divmmc.cpp:476-482:
  std::fprintf(stderr, "G46B <NAME> pc=%04x sp=%04x ...\n", pc, sp);
}
```

### 3. Optional atexit summary

If cumulative state matters:

```cpp
static int g46b_<name>_count = 0;
// increment in the probe
struct Probe<Name>Summary { ~Probe<Name>Summary() {
  if (g46b_<name>) fprintf(stderr, "[g46b/<name>] total hits: %d\n", g46b_<name>_count);
}} probe_<name>_summary;
```

### 4. Verify zero-cost when off

Single-bool branch + `[[unlikely]]`. No new allocations, no string formatting unless probe fires.

### 5. Build and smoke-test

```bash
LANG=C cmake --build build -j$(nproc) > /tmp/probe-build.log 2>&1; echo "status=$?"; tail -5 /tmp/probe-build.log

# Clone the SD master once per investigation; never boot on the master (jnext writes to it). Delete the clone when done.
mkdir -p ~/tmp/g46b-<name> && { [ -f ~/tmp/g46b-<name>/sd.img ] || cp --reflink=auto ~/.jnext/sdcard/cspect-next-1gb-fixed.img ~/tmp/g46b-<name>/sd.img; }

# Probe off (default):
./build/jnext --headless --machine next --sdcard $HOME/tmp/g46b-<name>/sd.img --delayed-automatic-exit 3

# Probe on:
JNEXT_G46B_<NAME>=1 ./build/jnext --headless --machine next --sdcard $HOME/tmp/g46b-<name>/sd.img --delayed-automatic-exit 3 2>/tmp/probe-<name>.log
wc -l /tmp/probe-<name>.log
head -10 /tmp/probe-<name>.log
```

### 6. Commit

```
diag(g46b-<branch-tag>): EOD-NN<x> — <probe-name> probe added
```

One probe per commit; don't bundle with fixes.

### 7. Suggest a matching DZRP capture script

Under `tools/cspect_dzrp/` so the user can compare jnext output vs CSpect at the same site. Don't write it yourself unless asked; just suggest.

## Hard rules

- **Env-gated.** Probes must be off by default. No behavior change when env unset.
- **No file artifacts outside repo.** Logs go to `/tmp/g46b-*/`; probe code in `src/`. Per `feedback_no_files_outside_repo`.
- **No band-aids.** A probe is a diagnostic; do NOT add a "probe" that actually changes emulator behavior. If you find yourself adding `if (g46b_<NAME>) { state.foo = bar; }`, stop — that's a band-aid, not a probe.
- **Single commit.** Probe addition is self-contained.

## When to escalate to the `boot-trace-detective` subagent

If the probe is the first step of a multi-step investigation, dispatch `boot-trace-detective` instead — it owns the full probe-add + DZRP-compare + hypothesis-test workflow. This skill is for adding one probe.
