---
name: cspect-debug
description: Comprehensive CSpect debugging via DZRP for jnext-vs-CSpect differential investigations. Covers hardened launch lifecycle (timeout-wrapped mono + bracketing pkill), BP-spray PC-stream capture, DZRP API quirks, and when to escalate to a custom plugin. Use whenever invoking CSpect from a script, doing BP-spray differential debugging, or any G46(b) workflow that touches CSpect — including "launch cspect", "run cspect", "BP-spray", "find divergent PC", "diff jnext vs CSpect PC stream", "cspect dzrp".
---

# CSpect debugging — unified workflow

The comprehensive guide for every CSpect-via-DZRP investigation step in the jnext repo. Bundles the hardened launch lifecycle (mandatory) with the BP-spray differential-debugging technique (decisive at EOD-30).

## When to invoke

- Any script that needs to launch CSpect.
- Any jnext-vs-CSpect differential debugging step where you need CSpect's PC stream over a window.
- Any time you need to capture register state, memory dumps, or NextReg state from CSpect at a specific PC.

## Background memories

- [[technique_cspect_lifecycle_hardening]] — full rationale for the launch pattern
- [[technique_dzrp_bp_spray]] — full rationale for the BP-spray technique
- [[reference_cspect_dzrp_launch]] — older launch reference (parts superseded by the hardened lifecycle below; the `setsid nohup` detach pattern works but doesn't guarantee timeout-death — prefer the new pattern)

---

# Part 1: Hardened CSpect launch lifecycle (MANDATORY)

CSpect on Linux runs under mono. Several failure modes hang the Claude session: DZRP deadlocks, zombie mono holding port 11000, opaque background-task lifecycles. User-mandated 2026-05-14: "Ensure CSpect is killed after a while when you launch it."

## Canonical template

Copy verbatim into any script that needs CSpect:

```bash
# 1. Defensive pkill — clears any zombie holding port 11000
pkill -9 mono 2>/dev/null; sleep 1

# 2. Launch with HARD timeout — guarantees mono dies after N seconds
#    Adjust 60 → expected script runtime + 10s margin
(timeout --kill-after=2s 60 mono /home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpect.exe \
   -w3 -zxnext -nextrom -debug -mmc=$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img \
   >/tmp/cspect_<topic>.log 2>&1 &)

# 3. Wait for DZRP listener with 20s cap
t0=$(date +%s)
until nc -z 127.0.0.1 11000 2>/dev/null; do
  sleep 0.2
  if [ $(($(date +%s) - t0)) -gt 20 ]; then
    echo "DZRP listener never came up"
    pkill -9 mono
    exit 1
  fi
done
echo "DZRP up after $(($(date +%s) - t0))s"
sleep 1   # let CSpect settle before first DZRP request

# 4. Run the actual DZRP Python script
python3 tools/cspect_dzrp/<your_script>.py 2>&1 | tee /tmp/<topic>.log

# 5. Defensive cleanup
pkill -9 mono 2>/dev/null; sleep 1

# 6. Verify mono is dead
pgrep mono >/dev/null && echo "STILL ALIVE — investigate" || echo "mono dead"
```

## Three required layers

1. **Bracketing pkill**: `pkill -9 mono` before AND after the script body. Before kills zombies; after guarantees cleanup even if the script crashed.

2. **`timeout --kill-after=2s N mono` wrapper**: outer `timeout` SIGKILLs mono after N seconds no matter what the script does. Pick N = script runtime + 10s buffer.

3. **Listener-up gate with 20s cap**: after launch, poll port 11000. If never listens within 20s, kill mono and abort.

## Pitfalls — DO NOT

- **DO NOT** use Bash tool's `run_in_background: true` for mono launch. Its lifecycle is opaque and can auto-kill mid-script. Use `(... &)` subshell + outer `timeout` for explicit control.
- **DO NOT** rely on Python's `try/finally` for mono cleanup. If Python hangs in `wait_for_pause()`, `finally` never runs.
- **DO NOT** skip the "before" pkill. A zombie from a previous hang attaches to your DZRP connect with stale CPU state, giving bogus results.
- **DO NOT** assume CSpect is dead when its log says "shutdown". The mono process can linger — check `pgrep mono`.

## CSpect CLI flags

For G46(b) NextZXOS boot work:

```bash
mono /home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpect.exe \
   -w3 -zxnext -nextrom -debug \
   -mmc=$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img
```

| Flag | Meaning |
|------|---------|
| `-w3` | 3× window scale (any GUI mode acceptable; CSpect needs a display) |
| `-zxnext` | boot in ZX Spectrum Next mode (28 MHz turbo capable) |
| `-nextrom` | use bundled Next ROMs |
| `-debug` | halt CPU at PC=$0000, wait for DZRP commands to resume |
| `-mmc=...` | mount the canonical NextZXOS SD image |

Note: older `reference_cspect_dzrp_launch.md` recommended `-remote=127.0.0.1:11000` instead of `-debug`. Modern CSpect's DezogPlugin starts the DZRP listener automatically with `-debug` and the `-debug` halt-at-$0000 behaviour is exactly what BP-spray sync needs. Prefer `-debug`.

---

# Part 2: BP-spray differential debugging

The decisive technique when point-sampling isn't enough. Captures CSpect's full PC stream over a window by installing BPs at every PC in that window, then diffs against jnext's PCTRACE to identify the first divergent PC.

## When to use

- jnext's PCTRACE shows it visits N PCs in some window; you need to know which CSpect ALSO visits vs SKIPS.
- The PC where CSpect's path diverges from jnext's is the upstream branch point — find it, and the bug is one step before.
- DZRP single-step is unreliable (no native support; `step_over_byte` breaks on JP/CALL/RET); BP-spray sidesteps this.

## Prerequisites

- jnext has a PCTRACE-style probe armed at the right sync point (`JNEXT_G46B_PCTRACE` in `src/cpu/z80_cpu.cpp` is the canonical pattern).
- jnext run captures PCTRACE to a log file.
- Unique PCs extracted to `/tmp/jnext_<topic>_pcs.txt`:
  ```bash
  grep "PCTRACE" /tmp/<run>.log | grep -oP "pc=[0-9a-f]+" | sed 's/pc=//' | sort -u > /tmp/jnext_<topic>_pcs.txt
  ```

## Steps

### 1. Pick a template — don't write from scratch

Copy the closest existing script in `tools/cspect_dzrp/`:
- `g46b_v2_bp_spray_v3.py` — 2-phase, sync at first $0038 INT
- `g46b_v2_bp_spray_v4.py` — 2-phase, sync at $14C0 hit N (proven at EOD-30)

### 2. Configure the sync point (Phase 1)

In Phase 1, BP at a known sync PC and count to the required hit number. This lets CSpect free-run at native speed to the same state jnext is in. Verify against jnext's known register/SP signature at that hit.

### 3. Build the BP set (Phase 2)

Load jnext PCs from `/tmp/jnext_<topic>_pcs.txt`. **CRITICAL**: exclude tight loops via the `TIGHT_LOOPS` set:

```python
TIGHT_LOOPS = {
    0x0E5C, 0x097B, 0x3D96,                     # LDIR + HALTs
    0x012E, 0x01A0, 0x01C0, 0x0140, 0x0160,     # RAM-clear/init loops
    0x1FFC, 0x1F6E,                              # DivMMC auto-unmap, INIR
}
```

Each BP in a tight loop converts CSpect from ~5M instr/s to ~25 BP/s — a 200,000× slowdown. Without exclusion you'll never reach the divergence.

### 4. Launch CSpect (hardened lifecycle from Part 1) + run script

### 5. Diff to find divergence

```python
jnext_pcs = set of jnext-visited PCs
cspect_pcs = set of CSpect-visited PCs
jnext_only = jnext_pcs - cspect_pcs   # jnext took branches CSpect didn't
cspect_only = cspect_pcs - jnext_pcs  # usually empty unless you added anchors
```

The FIRST PC in jnext's PCTRACE order that's in `jnext_only` is the divergence point. The PC just before it is the branch point — examine that instruction.

## Performance notes

- DZRP `add_breakpoint(addr)` round-trip ~10-30 μs. 500+ BPs install in <1 sec.
- Total BPs scale fine up to ~5000 (tested 4416 at EOD-30 v1).
- Each BP-fire is a Python round-trip. Plan budget: ~25-50 BP-fires/sec sustained.

## Report format

```
## BP-spray at <sync-point>
- BPs installed: <N>
- CSpect hits: <M> in <T>s
- Common PCs (jnext ∩ CSpect): <C>
- jnext-only PCs: <K>
- First divergent PC in jnext order: $<addr>
- Branch instruction just before: $<addr> = <opcode> <decoded>
- Likely cause: <port read, NextReg readback, etc.>
```

## When to escalate / widen / narrow

- If diff shows **no jnext-only PCs** (= 100% overlap): divergence is OUTSIDE your window — widen the trace OR pick a different sync point.
- If diff shows **>50% jnext-only PCs**: sync point is wrong — both emulators are doing entirely different things. Pick an earlier sync point.
- If you need **full per-instruction tracing** (not just window): CSpect plugin approach is feasible per Task 16 verdict, but ~1-2 days of C# work. BP-spray covers most needs.

---

# Part 3: DZRP API quirks

The Python client lives at `tools/cspect_dzrp/cspect_dzrp.py`. Key things to know:

## What works

- `c.add_breakpoint(addr)` — install address BP (returns BP ID).
- `c.remove_breakpoint(bp_id)` — uninstall.
- `c.cont()` — resume CPU (async; returns immediately).
- `c.wait_for_pause(timeout=N)` — block until next BP fires or timeout.
- `c.pause()` — force pause.
- `c.get_registers()` — returns `Registers` with AF/BC/DE/HL/IX/IY/AF'/BC'/DE'/HL'/SP/PC.
- `c.read_mem(addr, count)` — read N bytes bank-aware (= CPU's view).
- `c.get_tbblue_reg(N)` — read NextReg $N. Note: only NRs with read_handlers return meaningful values; others may return $00 or $FF.

## What doesn't exist (= BP-spray works around)

- **No native single-step.** `step_over_byte(n)` exists but only handles fixed-length instructions. For JP/CALL/RET it's unreliable. BP-spray bypasses this entirely.
- **No M1-fetch hook.** Can't trace every instruction without BPs.
- **No memory write watchpoints.** Can't trigger on `mem[X] = Y`.
- **No port-IO trace.** Can't see what ports the CPU reads/writes (only NextReg state via `get_tbblue_reg`).

## Caveat: register I, R, IFF1, IFF2, IM not exposed

CSpect's DZRP `get_registers()` does NOT return I, R, IFF1, IFF2, or IM. jnext's PCTRACE probes DO log these. When comparing, treat these as "jnext only" — but a real divergence may exist that you can't directly verify on the CSpect side.

---

# Part 4: Validated session-history references

The technique was proven decisive at EOD-30 (2026-05-14):

| Step | Tool | Finding |
|------|------|---------|
| PCTRACE in jnext | `JNEXT_G46B_PCTRACE=1` | jnext's path through 2000 PCs after $0038 INT |
| PC14C0 probe | `JNEXT_G46B_PC14C0_TRACE=1` | jnext byte-identical to CSpect at $14C0 hits 1-5; diverges after |
| BP-spray v4 | `g46b_v2_bp_spray_v4.py` | CSpect visits 255 of jnext's 564 PCs; 308 jnext-only; **jnext re-enters bank-flip dispatcher at $3CFC** repeatedly while CSpect doesn't |

The BP-spray REVEALED the divergence shape that point-sampling could not. The hardened lifecycle made the 25s Phase 2 run reliable across multiple iterations.

## Script lineage in `tools/cspect_dzrp/`

- `g46b_v2_bp_spray.py` (v1) — blanket coverage; drowned by RAM-clear at $012E.
- `g46b_v2_bp_spray_v2.py` (v2) — 380 BPs with TIGHT_LOOPS; drowned by RAM-init pass.
- `g46b_v2_bp_spray_v3.py` (v3) — canonical 2-phase, sync at $0038. Use as template for "first-INT divergence" workflows.
- `g46b_v2_bp_spray_v4.py` (v4) — 2-phase, sync at $14C0 hit N. Use as template for "mid-stream divergence" workflows.

---

# Hard rules

- **Always** use the hardened lifecycle pattern. Never invoke mono without `timeout` wrapper + bracketing pkill.
- **Always** exclude tight loops from BP-spray. Even one inner-loop BP destroys throughput.
- **Don't write from scratch** — fork an existing `g46b_v2_*` script and adapt the sync trigger.
- **CSpect is comparison, not oracle.** VHDL is the oracle. When CSpect ≠ VHDL, VHDL wins.
- **No bypass.** Don't add a "skip past this divergence" hack; trace the cause (per `feedback_g46b_no_bypass_tbblue`).
