# Pass-18 CPU + Z80N + IM2 — Fix-Review of Reviewer-Promoted Fixes

**Verdict**: **APPROVE**

**Branch**: `task2/verify18-cpu-z80n-im2-fix-review`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify18-cpu-z80n-im2-fix-review`
**Reviewer (REJECT) commit**: `2e25153`
**Fix-of-reviewer commits**: `19589c7` (V18R-CPU-01), `b45cb7e` (V18R-CPU-02),
`c881f97` (V18R-CPU-NIT-01).
**Fix-review HEAD at submission**: (this commit)

## Scope

The Pass-18 CPU reviewer (commit `2e25153`) REJECTED the audit's
defensible-zero claim and recorded **2 class-(c) findings + 1 NIT**:

1. **V18R-CPU-01** — INT pulse-expired drop arm gated on `!IFF1`,
   contradicting VHDL `zxnext.vhd:2017-2033`.
2. **V18R-CPU-02** — `Im2Controller::raise(Im2Level::DMA)` collides with
   `DevIdx::CTC7` via the dev_[] array, polluting CTC7's int_status on
   every DMA completion despite VHDL hardwiring `ctc_zc_to(7:4)<="0000"`
   and `ctc_int_en(7:4)<="0000"` (`zxnext.vhd:4092-4093`).
3. **V18R-CPU-NIT-01** — LDPIRX does not strobe MEMPTR low to the
   opcode byte 0xB7 per VHDL `t80n_mcode.vhd:1967` +
   `t80n.vhd:1181-1182` (LDZ='1' at MCycle 1 → `TmpAddr(7:0)<=DI_Reg`).

The fix-of-reviewer agent applied each in its own commit. This review
re-derives each VHDL oracle, validates the C++ change against it,
verifies the discriminative tests with a source-only sandwich for the
two class-(c) fixes, and inspects side effects (golden-file changes
in particular).

## Files reviewed

* `src/cpu/z80_cpu.cpp:452-475` — V18R-CPU-01 INT pulse-expired drop arm.
* `src/cpu/im2.cpp:128-176` — V18R-CPU-02 raise/clear early-return for
  DMA/DIVMMC/MULTIFACE + redirect via `to_devidx()` for everything else.
* `src/core/emulator.cpp:4482-4508` — V18R-CPU-02 `dma_.on_interrupt`
  no-op.
* `src/cpu/z80n_ext.cpp:931-948` — V18R-CPU-NIT-01 LDPIRX MEMPTR-lo
  strobe.
* `test/cpu/int_pulse_test.cpp` — rewritten boundary tests + 2 new
  V18R-CPU-01 discriminative tests.
* `test/cpu/cpu_z80n_im2_regressions_test.cpp` — 2 V18R-CPU-02 tests
  + 1 V18R-CPU-NIT-01 test.
* `test/z80n/tests.expected` — `edb7_basic` / `edb7_skip` golden lines
  for MEMPTR end-state (0x0000 → 0x00B7).

---

## V18R-CPU-01 — INT pulse-expired drop unconditional of IFF1

### VHDL oracle re-derivation

`zxnext.vhd:2017-2033` (verbatim):

```vhdl
   process (i_CLK_28)
   begin
      if falling_edge(i_CLK_28) then
         if reset = '1' then
            pulse_int_n <= '1';
         elsif pulse_int_n = '1' then
            if pulse_int_en = '1' then
               pulse_int_n <= '0';
            end if;
         elsif pulse_count_end = '1' then
            pulse_int_n <= '1';
         end if;
      end if;
   end process;

   pulse_count_end <= pulse_count(5) and (machine_timing_48 or machine_timing_p3 or pulse_count(2));
```

`pulse_int_n` (the /INT line) returns to '1' **as soon as `pulse_count_end='1'`**
— the gate is purely the count value, with no reference to any CPU-side
signal. IFF1 lives in the CPU and is sampled at the **rising edge of the
last clock of M1** (Z80 standard behaviour replicated by the T80n core),
which simply latches whatever level /INT carries at that moment. Once
the count gate has fired and /INT has gone high, that sample sees a
HIGH level → no INT acceptance, **even if IFF1=1 at that instant**.

The pre-fix C++ gate `if (tstates - int_requested_at_ > pulse_width && !z80.iff1)`
therefore had an extraneous `!iff1` term. The fix removes it.

### C++ implementation (`src/cpu/z80_cpu.cpp:452-475`)

```cpp
if (tstates - int_requested_at_ > int_pulse_tstates) {
    // Pulse expired — /INT line went high in hardware; drop the
    // pending request regardless of IFF1.
    int_pending_ = false;
} else if (z80.iff1) {
    // ... accept arm ...
}
```

**Verdict**: VHDL-faithful. The drop arm is now unconditional on IFF1;
the strict `>` comparator preserves the VHDL `pulse_count(5) AND ...`
edge semantics (pulse becomes high on the cycle where bit 5 sets,
i.e. the first cycle *strictly after* delta = pulse_width). The
comment block correctly cites VHDL :2017-2033.

### Test discriminative — V18R-CPU-01 + boundary rewrite

The two new discriminative tests sit at delta=30 (48K/+3, width=32) and
delta=34 (128K/Pent/Next, width=36). Trace for delta=30:

1. `tstates=0`, `request_interrupt(0xFF)` → `int_pending_=true`,
   `int_requested_at_=0`.
2. `tstates` set manually to 30.
3. First `execute()` (IFF1=0):
   - Pulse-window arm: `30 > 32` ? No → drop arm skipped.
   - IFF1=0 → accept arm skipped.
   - NOP executes → `tstates += 4 = 34`.
4. Second `execute()` (IFF1=1):
   - Pulse-window arm: `34 > 32` ? Yes.
   - Post-fix: drop arm fires unconditionally → `int_pending_=false`
     → NOP runs → PC=2 (discarded=true). ✓ asserted.
   - Pre-fix: drop arm was `> 32 && !iff1` → IFF1=1 short-circuits to
     false → accept arm fires → IM1 dispatch → PC=0x0038
     (discarded=false). Test would FAIL.

This is a genuine pre-fix-vs-post-fix discrimination — the test
fails on the pre-fix source even though IFF1=1 at the second
execute(), exactly the bug shape the reviewer flagged.

### Boundary-rewrite scrutiny

The existing edge tests were renamed `INT-PULSE-48K-edge-28`,
`-past-29`, `-128K-edge-32`, `-past-33` (was edge-32, past-33,
inside-33/edge-36/past-37). Re-derivation:

* The boundary now lies at `delta_initial + 4 (NOP) > pulse_width`
  because the discard decision happens at the **second** execute()
  entry (the harness intentionally pushes the test past one NOP
  so the IFF1=1 branch is the one that decides).
* 48K/+3, width=32: discarded iff `delta+4 > 32` iff `delta >= 29`.
  So delta=28 is the last live, delta=29 is the first discarded.
  Renamed labels match exactly.
* 128K/Pent/Next, width=36: discarded iff `delta+4 > 36` iff `delta >= 33`.
  delta=32 last live, delta=33 first discarded. Labels match.

The rewrite is *not* tautological: it reflects the actual new VHDL
boundary the fix produces. Sandwich (below) confirms — reverting the
fix re-fails the 5 boundary/V18R tests with the **exact** assertion
strings the new test sets.

### Sandwich verification (independent re-run)

* `git checkout 2e25153 -- src/cpu/z80_cpu.cpp` (revert fix only).
* `cmake --build build -j$(nproc)` → OK.
* `./build/test/cpu_int_pulse_test` →
  - `[FAIL] INT-PULSE-48K-past-29`
  - `[FAIL] INT-PULSE-128K-past-33`
  - `[FAIL] INT-PULSE-DELTA-DIVERGENCE-48K-expires`
  - `[FAIL] INT-PULSE-V18R-CPU-01-48K-pulse-expired-iff1-up`
  - `[FAIL] INT-PULSE-V18R-CPU-01-128K-pulse-expired-iff1-up`
  - 6/11 PASS, **5/11 FAIL** — matches the commit's claim verbatim.
* `git checkout c881f97 -- src/cpu/z80_cpu.cpp` → restore.
* Rebuild → 11/11 PASS again.

**Verdict V18R-CPU-01**: VHDL-faithful, test discriminative, boundary
rewrite justified. ✅

---

## V18R-CPU-02 — `raise(Im2Level::DMA)` must not pollute CTC7

### VHDL oracle re-derivation

`zxnext.vhd:4064-4093` defines a single `ctc_mod` entity instance with
`NUM_CTC=4`, producing `ctc_zc_to(3:0)` and `ctc_int_en(3:0)` only.
Bits (7:4) are **hardwired to "0000"**:

```vhdl
4092   ctc_zc_to(7 downto 4) <= "0000";
4093   ctc_int_en(7 downto 4) <= "0000";
```

In the priority chain `im2_int_req <= ... & ctc_zc_to & ... `
(line 1941), bits (7:4) — corresponding to CTC4..CTC7 — can therefore
**never** carry a '1' from peripheral activity. CTC7's `int_status`
must remain 0 throughout system runtime.

`zxnext.vhd:2003-2008`:

```vhdl
   im2_dma_delay <= (im2_dma_int) or (nmi_activated and nr_cc_dma_int_en_0_7) or
                    (im2_dma_delay and dma_delay);
```

DMA is a *victim* of INT (via `im2_dma_delay`'s OR-of-priority-chain
gating its own latch), **not a priority-chain source**. The Im2
fabric's `im2_int_req` priority vector at :1941 has no DMA slot.
The legacy `Im2Level::DMA` aliasing in the C++ scaffold therefore
maps to nothing in VHDL.

### C++ implementation review

**Enum collision audit** (`src/cpu/im2.h:14-43`):

```text
Im2Level::DMA       = 10  vs  DevIdx::CTC7     = 10     ← collision
Im2Level::DIVMMC    = 11  vs  DevIdx::ULA      = 11     ← collision
Im2Level::ULA_EXTRA = 12  vs  DevIdx::UART0_TX = 12     ← collision (not no-op)
Im2Level::MULTIFACE = 13  vs  DevIdx::UART1_TX = 13     ← collision
```

`Im2Level::ULA_EXTRA` is **not** in the early-return set because it
legitimately routes to `DevIdx::ULA` via `to_devidx()` (the second
ULA pulse path, VHDL :1946 `im2_int_unq`). That's correct.

The pre-fix `dev_[static_cast<int>(level)].int_req = true` raised the
wrong device's int_req for all 4 collisions. The fix's two-layer
defence:

1. **`im2.cpp:128-176`**: `raise()`/`clear()` switch-early-return for
   `DMA/DIVMMC/MULTIFACE` (no daisy-chain slot per VHDL); other
   `Im2Level` entries route through `to_devidx()` to the proper
   `DevIdx` slot (also fixes pre-existing FRAME_IRQ/CTC_/UART_ raw-int
   collisions that were latent because nothing called them via the
   legacy API — but they're still robustly fixed against future
   accidents).

2. **`emulator.cpp:4504-4508`**: `dma_.on_interrupt` lambda is now a
   no-op. Belt-and-suspenders: cuts the bug at the call site even if
   the im2.cpp fix were reverted in isolation.

Both layers cite VHDL line numbers in their inline comments.

### Test discriminative

`test_v18r_cpu_02_dma_raise_no_pollute_ctc7` constructs an isolated
`Im2Controller`, calls `raise(Im2Level::DMA)` + `tick(1)`, and asserts
`int_status(CTC7) == 0` and `int_status_mask_c9() & 0x80 == 0`.

* Post-fix: raise() early-returns for DMA → CTC7's dev_[10].int_req
  stays 0 → step_devices() Phase-1 edge-detect does not fire →
  int_status stays 0 → bit 7 of NR 0xC9 stays 0.
* Pre-fix: dev_[10].int_req=true → tick()'s Phase-1 latches
  int_status=true → bit 7 of NR 0xC9 becomes 1 → assertion FAILS.

`test_v18r_cpu_02_dma_raise_no_pollute_ula` is the
**discrimination shape lock-in** check: it confirms the fix is
"no-op" rather than "redirect to ULA" — preventing the next agent
from "fixing" the collision by moving the raise to ULA via
`to_devidx()` (which would silently fire a spurious framebuffer
INT). Already passes pre-fix because ULA's int_req isn't touched by
the buggy raw-int write.

### Sandwich verification

* `git checkout 2e25153 -- src/cpu/im2.cpp src/core/emulator.cpp`
  (revert both fix files only; tests preserved).
* Rebuild.
* `./build/test/cpu_z80n_im2_regressions_test 2>&1 | grep V18R-CPU-02`:
  - `[FAIL] V18R-CPU-02-DMA-RAISE-NO-POLLUTE-CTC7  CTC7 int_status pre=0 post=1; NR 0xC9 pre=0x00 post=0x80`
  - `[PASS] V18R-CPU-02-DMA-RAISE-NO-POLLUTE-ULA`
  - 42/43 PASS, 1/43 FAIL — exactly matches the commit's claim. The
    bug shape is precisely the DMA→CTC7 collision the reviewer
    flagged.
* Restore both files. Rebuild. 43/43 PASS.

### Side-effect inspection — `dma_.on_interrupt` no-op

Search for callers and consumers of the legacy `Im2Level::DMA` path:

* `grep -r "Im2Level::DMA\|raise.*DMA" src/`: the only call site that
  raised DMA via the legacy API was `dma_.on_interrupt` (now no-op).
* The **modern** DMA INT path lives in
  `Im2Controller::set_dma_int_en_mask` + `dma_int_pending()` +
  `dma_delay()` + `step_dma_delay()` (all citing VHDL :1957 and
  :2001-2010). Driven from NR 0xCC/CD/CE writes in `emulator.cpp`,
  routed to the CPU via `run_frame()`'s per-frame DMA hook. That
  path is **untouched** by this fix.

Conclusion: no legitimate DMA INT signal was lost. The removed call
was a pre-existing vestigial save/load-schema artefact that was
actively buggy due to the dev_[] index collision.

**Verdict V18R-CPU-02**: VHDL-faithful, test discriminative, two-layer
fix structurally correct, no legitimate INT path lost. ✅

---

## V18R-CPU-NIT-01 — LDPIRX MEMPTR-low strobe to opcode byte 0xB7

### VHDL oracle re-derivation

`t80n_mcode.vhd:1962-1969`:

```vhdl
            Z80N_command_o <= LDPIRX;
            MCycles <= "100";
            case to_integer(unsigned(MCycle)) is
            when 1 =>
            LDZ <= '1';
               Set_Addr_To <= aXY;
               IncDec_16 <= "1100"; -- decrement BC
```

`t80n.vhd:1175-1186`:

```vhdl
         if TState = 3 then
            ...
            if LDZ = '1' then
               TmpAddr(7 downto 0) <= DI_Reg;
            end if;
            if LDW = '1' then
               TmpAddr(15 downto 8) <= DI_Reg;
            end if;
```

`TmpAddr` is the WZ/MEMPTR. `DI_Reg <= DI` (t80n.vhd:1638) — the
just-fetched data byte. At MCycle 1 / TState 3 of an ED-prefixed
instruction, the t80n decoder is **past** the ED-prefix fetch, in
the second M1 cycle of the instruction; `DI_Reg` carries the second
opcode byte = **0xB7** (LDPIRX). LDW is never asserted in the LDPIRX
mcode, so WZ-hi is preserved.

### C++ implementation (`src/cpu/z80n_ext.cpp:931-948`)

```cpp
regs.AF = (regs.AF & 0xFF00) | f;
regs.Q = f;
// ... V18R-CPU-NIT-01 fix:
regs.MEMPTR = static_cast<uint16_t>((regs.MEMPTR & 0xFF00) | 0x00B7u);
int t = 16;
// ...
```

The write is unconditional on the (LDPIRX-execution) path — including
the match-skip path, because per VHDL the LDZ strobe at MCycle 1 fires
regardless of whether the pattern byte matches A (the pattern compare
happens at MCycle 2). The fix is correct.

### Test discriminative

`test_v18r_cpu_nit_01_ldpirx_memptr_lo_strobe`:

* Pre-sets `MEMPTR = 0xAB34` (distinctive hi=0xAB, lo=0x34).
* HL points at non-A pattern byte 0x55.
* Asserts `MEMPTR == 0xABB7` after execute().

Post-fix: MEMPTR becomes (0xAB00 | 0xB7) = 0xABB7. ✓
Pre-fix: MEMPTR untouched at 0xAB34. ✗ (would FAIL).

### Side-effect inspection — golden file change

The change to `test/z80n/tests.expected` lines 468 + 476 modifies
the end-of-test MEMPTR field for `edb7_basic` and `edb7_skip`:

```text
- aa20 0000 a00a 9000 1122 3344 5566 7788 aaaa bbbb fff0 8002 0000
+ aa20 0000 a00a 9000 1122 3344 5566 7788 aaaa bbbb fff0 8002 00b7
```

The corresponding `tests.in` lines (462, 470) confirm input MEMPTR is
`0000`. Per the fix, post-execution MEMPTR = `0x0000 & 0xFF00 | 0x00B7`
= `0x00B7`. The golden update is the **correct** consequence of the
fix, not a separate behaviour change. Field positioning verified
against the test harness:

```cpp
// test/z80n/runner.cpp:84
>> tc.IX >> tc.IY >> tc.SP >> tc.PC >> tc.MEMPTR;
```

The last field on the regs line is MEMPTR. No other golden lines were
touched. No other opcode goldens reference MEMPTR-after-LDPIRX, so
nothing else can have broken.

### Out-of-scope completeness note (not a defect)

A wider grep for `LDZ <= '1'` in `t80n_mcode.vhd` shows 25 LDZ
assertions across many ED-prefixed instructions. Several of those
(NEXTREGW, MMU, PIXELAD, PIXELDN, etc.) might also strobe MEMPTR low
at their MCycle 1 per the same `TmpAddr(7:0) <= DI_Reg` path. The
Pass-18 reviewer specifically scoped V18R-CPU-NIT-01 to **LDPIRX
only**, and the fix obeys that scope. Other ED-prefix instructions
remain a known incomplete-coverage area for a future audit pass; not
a regression of this fix.

**Verdict V18R-CPU-NIT-01**: VHDL-faithful, test discriminative, golden
update justified and scoped. ✅

---

## Test invariants verified post-fix

Release build, post-restore (HEAD = `c881f97`):

| Test                              | Result          |
|-----------------------------------|-----------------|
| `ctest --output-on-failure`       | 38/38 PASS      |
| `fuse_z80_test`                   | 1356/1356 PASS  |
| `cpu_z80n_im2_regressions_test`   | 43/43 PASS      |
| `cpu_int_pulse_test`              | 11/11 PASS      |
| `z80n_test`                       | 85/85 PASS      |

No regressions in the wider test suite. The golden change in
`test/z80n/tests.expected` is the only data-asset modification and is
fully justified by the VHDL oracle for LDPIRX's MCycle-1 LDZ strobe.

## Final verdict

**APPROVE.** All three fixes are VHDL-faithful:

* V18R-CPU-01 drop arm matches `zxnext.vhd:2017-2033`'s
  `pulse_int_n`-on-`pulse_count_end` semantics independent of IFF1.
* V18R-CPU-02 two-layer no-op aligns with
  `zxnext.vhd:4092-4093` (CTC4..CTC7 hardwired to 0) and :2003-2008
  (DMA is a victim, not a chain source); preserves the modern DMA
  INT path via `set_dma_int_en_mask` / `dma_int_pending` / `dma_delay`.
* V18R-CPU-NIT-01 LDPIRX MEMPTR-lo write matches `t80n_mcode.vhd:1967`
  + `t80n.vhd:1181-1182` (LDZ strobe of opcode byte 0xB7).

Discriminative tests verified by independent sandwich for the two
class-(c) fixes; the NIT's golden change inspected and justified.
No side effects in adjacent code paths. No goldens regressed beyond
the intentional LDPIRX MEMPTR-lo update.

No missed findings detected by this fix-review.
