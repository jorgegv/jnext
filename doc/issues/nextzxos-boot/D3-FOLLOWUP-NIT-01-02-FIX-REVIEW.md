# Review — D3 follow-up NITs fix-of-reviewer (commit `c8ba39ff`)

**Scope:** Independent VHDL-faithfulness review of the NIT-only follow-up
to D3F-01/02/03 (reviewer HEAD `11d5f537`). Two new findings:

- **D3F-NIT-01** — port `0x7FFD` A14 gate axis (`emulator.cpp:3211-3212`)
- **D3F-NIT-02** — slot-3 contention pattern selector in the `0x7FFD`
  write handler (`emulator.cpp:3252-3261`)

Both fixes are in the same family as `V24-MEM-01`, `D3F-01`, `D3F-02`,
`D3F-03`: re-key a port-decode/contention site that was bound to the
typ_sel axis (`config_.type` / `MachineType`) onto the tim_sel axis
(`mmu_.machine_timing()` / `MachineTimingMode`) where the VHDL keys on
`machine_timing_*`.

## Verdict — **APPROVE**

Both fixes are VHDL-faithful. The accompanying regression tests are
discriminative and sandwich-verified. Side-effect inspection (Pentagon
timing, 48K-timing edge case, A14-gated cousins) is clean. The independent
grep audit confirms no remaining live `config_.type` site in the
port-decode / contention surfaces this NIT family covers.

---

## 1. VHDL faithfulness

### D3F-NIT-01 — port `0x7FFD` A14 gate

**VHDL `zxnext.vhd:2593`:**

```
port_7ffd <= '1' when cpu_a(15) = '0'
                  and (cpu_a(14) = '1' or p3_timing_hw_en = '0')
                  and port_fd = '1' and port_1ffd = '0' and port_7ffd_io_en = '1'
                 else '0';
```

**VHDL `zxnext.vhd:2457`:** `p3_timing_hw_en <= machine_timing_p3;` — one-hot
mirror of the tim_sel axis (latched on every `cpu_mreq_n='0'`).

Decoding the OR clause:

- Under **+3 timing** (`p3_timing_hw_en = '1'`): the right disjunct is
  `'1' or '0' = '0'`, so the gate reduces to `cpu_a(14) = '1'` —
  i.e. **A14 must be 1**.
- Under **non-+3 timing** (`p3_timing_hw_en = '0'`): the right disjunct
  is `'0' or '1' = '1'`, so A14 is don't-care — i.e. **A14 not required**.

**Post-fix C++ (`emulator.cpp:3211-3212`):**

```cpp
if (mmu_.machine_timing() == MachineTimingMode::TimingPlus3
    && (port & 0x4000) == 0) return;   // reject A14=0 under +3 timing
```

Gate polarity correct. The C++ inversely rejects the disallowed case
(`+3 timing AND A14=0`), exactly mirroring the VHDL's positive assertion
that under +3 timing A14 must be 1.

**Pre-fix code** keyed on `config_.type == ZX_PLUS3`. When NextZXOS writes
`NR 0x03 = 0xBn` with `tim_sel != typ_sel` (e.g. typ_sel = 48K but
tim_sel = +3), the pre-fix gate was wrongly disabled, accepting a 0x7FFD
write at A14=0 (port 0x2001 / 0x0001) that the FPGA would have ignored.
**Axis is now correctly the tim_sel axis.**

### D3F-NIT-02 — slot-3 contention pattern

**VHDL `zxnext.vhd:4489-4493`:**

```
mem_contend <= '0' when mem_active_page(7 downto 4) /= "0000" else
               '1' when machine_timing_48  = '1' and mem_active_page(3 downto 1) = "101" else
               '1' when machine_timing_128 = '1' and mem_active_page(1) = '1' else
               '1' when machine_timing_p3  = '1' and mem_active_page(3) = '1' else
               '0';
```

Per-mode patterns are **selected by `machine_timing_*`** — the tim_sel
axis, not the typ_sel axis.

**Post-fix C++ (`emulator.cpp:3253-3261`):**

```cpp
const MachineTimingMode tim = mmu_.machine_timing();
if (tim == MachineTimingMode::TimingPlus3)
    slot3_contended = (bank >= 4);
else if (tim == MachineTimingMode::Timing128)
    slot3_contended = (bank & 1) != 0;
else
    slot3_contended = false;   // 48K / Pentagon: bank-switched slot 3 not contended
```

The three-way switch faithfully encodes the VHDL's three tim_sel arms.
The `else → false` branch covers 48K and Pentagon timings; see
*Section 3 — Side-effect inspection* below for the rationale that this
is correct in context.

---

## 2. Sandwich verification

### Step 1 — Post-fix (baseline)

```
$ ./build/test/port_test
Total: 105  Passed: 104  Failed:   0  Skipped:   1
```

Both new tests pass:

- `D3F-NIT-01-PORT-7FFD-A14` — PASS
- `D3F-NIT-02-SLOT3-CONTENTION` — PASS

### Step 2 — Revert source only (tests kept)

`git apply -R` of only the `src/core/emulator.cpp` portion of the diff
restores the pre-fix `config_.type`-keyed gate and pattern selector.
Rebuild and rerun:

```
$ ./build/test/port_test
Total: 105  Passed: 102  Failed:   2  Skipped:   1

FAIL D3F-NIT-01-PORT-7FFD-A14:
  tim_after=2 (want 2=TimingPlus3) typ_after=1 (want 1=ZX48K)
  pre=0x00 post=0x05 (want equal)

FAIL D3F-NIT-02-SLOT3-CONTENTION:
  tim_after=2 (want 2=TimingPlus3) typ_after=1 (want 1=ZX48K)
  slot3_after=0 (want 1)
```

Pre-fix diagnostics match the dev's claims exactly:

- NIT-01: gate keyed on `config_.type==ZX_PLUS3` was false for ZX48K,
  so the write was accepted (post latch = 0x05).
- NIT-02: else branch took the 128K odd-banks pattern; bank=4 & 1 = 0,
  so slot 3 was not marked contended.

### Step 3 — Restore source

Re-apply the diff, rebuild, re-run port_test → `104/0/1`. Sandwich complete.

### Test invariants

```
ctest --test-dir build              → 38/38 PASS
./build/test/fuse_z80_test ...      → 1356/1356 PASS
./build/test/port_test              → 105 total / 104 PASS / 1 SKIP / 0 FAIL
```

Regression suite was not re-run in-worktree (the script is independent of
the patched surfaces; D3F-NIT-01/02 only fire under a user-written
`NR 0x03 = 0xBn` post-init with `tim_sel != typ_sel`, which the canonical
NextZXOS boot path never does).

---

## 3. Side-effect inspection

### 3.1 Pentagon timing — slot-3 contention

The fix's `else` branch returns `false` for `TimingPentagon`. This is the
right behavior:

- **VHDL `zxnext.vhd:4481`** disables ALL contention for Pentagon timing
  via `i_contention_en <= ... and (not machine_timing_pentagon) and ...`.
- **Canonical `Mmu::mem_contend_for_` (`mmu.h:1220-1235`)** returns false
  for `TimingPentagon` in its switch.

So even if a 0x7FFD write under Pentagon timing landed in this code
path, contention would be disabled at the ULA level regardless of the
per-slot flag. The legacy `slot_contended_[3]` mirror — the only thing
this handler updates — does not affect the canonical mem_contend latch
path (`mmu.h:712-720`: the latch hot path now consults
`mem_contend_for_(addr)`, not `slot_contended_[]`). **No regression.**

### 3.2 48K timing — slot-3 contention

Pre-fix: 48K-with-`config_.type==ZX48K` took the else branch (128K odd
pattern). Post-fix: 48K timing takes the new `else → false`.

VHDL `:4490` says under 48K timing, contention requires
`mem_active_page(3 downto 1) = "101"` — i.e. **bank 5 only** (low nibble
0b101x). The fix doesn't model this for the per-slot mirror.

However, this is a **non-issue** because:

1. The canonical `Mmu::mem_contend_for_(addr)` (`mmu.h:1225-1226`)
   correctly handles 48K bank-5-only contention per-page.
2. The legacy `slot_contended_[]` mirror is documented (`mmu.h:712-720`)
   as **legacy plumbing retained for save-state compatibility** — the
   floating-bus latch and read/write hot paths consult
   `mem_contend_for_(addr)`, not the slot mirror.
3. On real 48K hardware, port 0x7FFD bank-switching is a no-op (no 128K
   paging exists); the bank field is ignored. Any test that exercises
   0x7FFD slot-3 contention under 48K timing is contrived.

The dev's commit message inline comment ("48K / Pentagon: bank-switched
slot 3 not contended") accurately reflects the intent.

### 3.3 A14-gated cousins

The NIT-01 change only touches the 0x7FFD handler's A14 gate. Adjacent
A14:13 / A15:14 gates in the `port_xffd` family
(`port_1ffd` @ 0x1FFD, `port_2ffd` @ 0x2FFD, `port_3ffd` @ 0x3FFD,
`port_p3_float` @ 0x0FFD, `port_dffd` @ 0xDFFD) are decoded in separate
handlers with their own mask/dispatcher gating — unaffected by this
change. Manual inspection of `src/core/emulator.cpp:3273-3380` and the
PortDispatcher mask registrations confirms no cross-coupling.

---

## 4. Independent grep audit

Dev claimed: "grep clean — no remaining `config_.type` in port-decode or
contention paths where VHDL keys on `machine_timing_*`."

Independent re-grep of `src/core/emulator.cpp`:

```
$ grep -nE "config_\.type|config\.type" src/core/emulator.cpp
3574:  // `config_.type` would silently mis-decode when NR 0x03 is              [COMMENT]
3712:  // `config_.type` and silently mis-decoded when NR 0x03 was              [COMMENT]
6855:  // version keyed on `config_.type`, which silently mis-decoded when NR   [COMMENT]
7138:  //   contention_   — rebuilt from config.type by build()                 [COMMENT]
```

**All remaining hits are comments** describing the pre-fix mis-decode
history. No live code site uses `config_.type` for port-decode or
contention gating. Confirmed clean.

Independent re-grep of `mmu_.machine_timing()` consumers in
`src/core/emulator.cpp`:

```
3211: 0x7FFD A14 gate                  (NIT-01 — this fix)
3254: 0x7FFD slot-3 contention pattern (NIT-02 — this fix)
3576: port 0x0FFD float read           (D3F-01)
3714: port 0xBFFD AY read alias        (D3F-02)
6860: port 0xFE EAR/keyboard ULA path  (D3F-03)
```

Five tim_sel-gated sites; all five correctly bound to the VHDL
`machine_timing_*` axis. **The NIT family closes the gap surfaced by the
D3F-01/02/03 reviewer (HEAD `11d5f537`).**

### Adjacent missed = none

Sites that legitimately stay on `config_.type` / `MachineType` (typ_sel
axis) — these are correct per VHDL:

- **`Emulator::init` machine-config dispatch** — `rebuild_for_type` keys
  on the user-selected MachineType at construction time (typ_sel
  initialization).
- **`is_48_or_p3_at_reset`** — composite typ_sel-at-reset signal used by
  IM2 / CPU FUSE-callback wiring (NR 0x03 commit path
  `emulator.cpp:389-401`); follows VHDL `nr_03_machine_type` decode.
- **NR 0x03 dispatcher / commit** (`emulator.cpp:2340-2362`,
  `5671-5672`) — explicitly drives both axes (typ_sel + tim_sel) from
  the latched NR 0x03 bytes. Correct two-axis split.

---

## 5. Final verdict

- **VHDL faithfulness:** PASS (gate polarity, pattern selector arms,
  enum-mode coverage all match the VHDL one-to-one).
- **Discriminative regression tests:** PASS (sandwich-verified, both
  tests FAIL pre-fix with exact dev-claimed diagnostics, PASS post-fix).
- **Side-effects:** clean (Pentagon-OK by canonical contention disable;
  48K-edge tolerated by per-page `mem_contend_for_` canonical path;
  A14-gated cousins isolated).
- **Adjacent grep audit:** clean (4 comment refs to `config_.type`, no
  live code; 5 `machine_timing()` consumers, all correct).

**VERDICT: APPROVE.**

---

## Reviewer notes

This is the final chain in the D3 follow-up cascade. The whole sequence
landed:

```
6e68c680  fix V24-MEM-01           (contention canonical accessor)
b9ea1c1e  review D3 main           APPROVE-WITH-NITS (3 new NITs)
d2e4aa67  fix D3F-01/02/03         (port handlers — 0FFD float, BFFD AY, FE ULA)
11d5f537  review D3-followup       APPROVE-WITH-NITS (2 new NITs)
c8ba39ff  fix D3F-NIT-01/02        (port 7FFD A14 gate + slot-3 contention pattern)
HEAD      review D3-followup-nits  APPROVE                                   ← here
```

Trend across the cascade:

- Each fix pass collapses one structural class.
- Each independent review found a small fanout of overlooked adjacent
  sites — exactly the pattern the project memory's
  `feedback_task2_audit_thorough_per_pass.md` warns about.
- The NIT-only follow-up to a NIT review is the final closure point;
  one more grep audit confirms nothing else remains.

**No further follow-up review expected for this family.** The next
upstream pass should audit the next port-decode VHDL surface for the
same typ_sel / tim_sel discrimination (e.g. NR 0x82-`internal_port_enable`
fanout, port_bf3b/port_ff3b, port_1ffd_active gating).
