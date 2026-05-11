# D3 Follow-up Review — Port-Handler `machine_timing_` Gates (D3F-01/02/03)

**Subject commit:** `d2e4aa67` — `fix(task2-d3-followup): D3F-01/02/03 — port handlers gate via machine_timing_ per VHDL :2589,:2771,:4513`

**Reviewer:** Independent (task2/d3-followup-reviewer-v2 worktree).

**Verdict:** **APPROVE-WITH-NITS** — 3 fixes VHDL-faithful, 3 discriminative regression tests, 2 legacy-test updates legitimate bug-corrections (not enshrinement). Two additional adjacent sites in the same family identified as NITs (follow-up scope).

---

## 1. VHDL faithfulness — per fix

### D3F-01 — port 0x0FFD `port_p3_float` gate

* **VHDL anchor** `zxnext.vhd:2589`:
  ```
  port_p3_float <= '1' when cpu_a(15 downto 12) = "0000" and port_fd = '1'
                       and p3_timing_hw_en = '1'
                       and port_p3_floating_bus_io_en = '1' else '0';
  ```
* **`p3_timing_hw_en` definition** `zxnext.vhd:2457`:
  ```
  p3_timing_hw_en <= machine_timing_p3;
  ```
  → one-hot mirror of `machine_timing_p3`, registered on `cpu_mreq_n` edge (line :2452). Effectively == `machine_timing_p3`.
* **C++ change** (`emulator.cpp:3559`):
  ```cpp
  if (mmu_.machine_timing() != MachineTimingMode::TimingPlus3) return 0x00;
  ```
* **Verdict:** VHDL-faithful. The fix replaces the typ_sel-derived `config_.type` check with the tim_sel-derived `machine_timing()` accessor — exact mirror of `p3_timing_hw_en = '1'`.

### D3F-02 — port 0xBFFD AY-read alias

* **VHDL anchor** `zxnext.vhd:2771`:
  ```
  port_fffd_rd <= iord and (port_fffd or (port_bffd and machine_timing_p3) or port_bff5);
  ```
* **C++ change** (`emulator.cpp:3697`):
  ```cpp
  if (mmu_.machine_timing() != MachineTimingMode::TimingPlus3) return 0xFF;
  ```
* **Verdict:** VHDL-faithful. Same family — BFFD-aliases-FFFD gate consumes `machine_timing_p3` (tim_sel), not `port_p3_dat` (typ_sel).

### D3F-03 — port 0xFF ULA-floating-bus arm

* **VHDL anchor** `zxnext.vhd:4513`:
  ```
  port_ff_dat_ula <= ula_floating_bus when (machine_timing_48 = '1' or machine_timing_128 = '1')
                                       else X"FF";
  ```
* **C++ change** (`emulator.cpp:6841-6845`):
  ```cpp
  const MachineTimingMode tim = mmu_.machine_timing();
  if (tim != MachineTimingMode::Timing48 && tim != MachineTimingMode::Timing128) {
      return 0xFF;
  }
  ```
* **Verdict:** VHDL-faithful. Closes the pre-existing "FOLLOW-UP" note (commit body Section 3 prologue, prior `floating_bus_test.cpp:432-437`) flagged by the D3 reviewer NIT-2 + Section 7.3.

---

## 2. Legacy test update verdicts (CRITICAL — bug-correction vs enshrinement)

### FB-3F (`floating_bus_test.cpp`, Section 3)

* **Pre-fix assertion:** `v == 0x00` (Next-base port 0x0FFD returns 0).
* **Post-fix assertion:** `v == 0x01` (Next-base port 0x0FFD returns `latch | 0x01` = 0x01).
* **VHDL evidence:** `:1099` `nr_03_machine_timing : std_logic_vector(2 downto 0) := "011"` → default tim_sel = `+3 timing`. The emulator's `init` (line 369) seeds `tim_sel = 0x03` for `ZXN_ISSUE2` to match this VHDL default. Therefore on a fresh Next-base emulator, `machine_timing() == TimingPlus3` and port 0x0FFD IS decoded.
* **Verdict:** **LEGITIMATE BUG-CORRECTION.** The OLD assertion (`v == 0x00`) enshrined the pre-fix `config_.type != ZX_PLUS3` gate behavior that VHDL contradicted. The updated assertion (`v == 0x01`) matches VHDL-correct behavior. NOT enshrinement — accommodating fix.
* **Sandwich confirmation:** with source reverted + test kept, FB-3F **FAILS** (`v=0x00 (want 0x01)`) → confirms the test update is consequential and tracks the fix.

### IO-05 (`audio_port_dispatch_test.cpp`)

* **Pre-fix negative case:** Next-base (`ZXN_ISSUE2`), asserting `nx_read == 0xFF`. With pre-fix gate `config_.type != ZX_PLUS3`, Next returned 0xFF → assertion passed.
* **Post-fix negative case:** 128K (`ZX128K`), asserting `k128_read == 0xFF`. With post-fix gate `machine_timing() != TimingPlus3`, Next-base would now alias (tim_sel defaults to +3) and return 0x5A → the OLD assertion `nx_read == 0xFF` would FAIL. So negative case switched to 128K, whose tim_sel default is `Timing128`, correctly NOT aliasing.
* **VHDL evidence:** `:2771 port_fffd_rd <= iord and (port_fffd or (port_bffd and machine_timing_p3) or port_bff5)` — gate is `machine_timing_p3`, and Next-base's default tim_sel = +3 per :1099 → Next-base SHOULD alias by default. 128K is the correct isolated negative.
* **Verdict:** **LEGITIMATE BUG-CORRECTION.** The negative case had to migrate because Next-base no longer satisfies the negative precondition post-fix. The discriminator (positive-aliases vs negative-doesn't-alias) is preserved. NOT enshrinement.

---

## 3. Sandwich (independent)

### Pre-fix-revert state (HEAD)
* `ctest` 38/38 PASS
* FUSE 1356/1356 PASS
* `floating_bus_test` 34/34 PASS (Section 7 D3F: 3/3)
* `audio_port_dispatch_test` 21/21 PASS
* Regression `33/0/0`

### Source-only revert (kept tests)
Reverted only the 3 source-code changes in `emulator.cpp` (kept commit's test changes):
* FB-D3F-01 **FAIL**: `v=0x00 (want 0xA5); tim_after=2 (=TimingPlus3)` — confirms 0x0FFD handler reverted to `config_.type` rejects 48K + tim_sel=+3.
* FB-D3F-02 **FAIL**: `v=0xFF (want 0x5A); tim_after=2; typ_after=2 (ZX128K)` — confirms BFFD handler reverted blocks 128K + tim_sel=+3.
* FB-D3F-03 **FAIL**: `v=0x42 (want 0xFF); tim_after=2; typ_after=1 (ZX48K)` — confirms 0xFF handler reverted incorrectly arms ULA-bus on 48K + tim_sel=+3.
* FB-3F **FAIL**: `v=0x00 (want 0x01)` — confirms legacy test update is consequential.

All 4 failures are exactly what is expected. Discriminators verified.

### Post-restore state
Restored source → all tests pass again (34/34 floating-bus, 21/21 audio-port-dispatch, 38/38 ctest). Restoration confirmed clean.

---

## 4. Side-effect inspection

### `mmu_.machine_timing()` accessor returns effective (committed) value

Confirmed (`mmu.h:875`):
```cpp
MachineTimingMode machine_timing() const { return machine_timing_; }
```
where `machine_timing_` is the committed field (set by `commit_pending_machine_timing()` on video-frame edge, mmu.h:872-874).

### Init seam ordering

`Emulator::init` seeds `mmu_.set_machine_timing(init_tim_mode)` at line 391 — BEFORE the port handler registrations at line 3551+. Port-handler closures capture `this`, so they read the live `mmu_.machine_timing()` at call time, not at registration. No race.

The MMU default-constructed value is `MachineTimingMode::TimingPlus3` (mmu.h:1359) matching VHDL `:1099` default `"011"`. If a port handler were ever called BEFORE `init`, the safe fallback is `+3` — which matches the VHDL boot default. No silent mis-decode.

### Test suite regression scan

Ran the full ctest matrix (38/38 PASS) and FUSE (1356/1356 PASS) post-fix. Only the 2 expected legacy tests required update (FB-3F + IO-05). No silent regressions detected — `grep -rn "0x0FFD\|BFFD"` of `test/` and `grep` of `MachineType::ZXN_ISSUE2.*0xFF` (port 0xFF) reveal no other tests asserting on the old gate behavior.

---

## 5. Adjacent re-audit — `config_.type` in port-handler context (NITs)

Per the prompt's Step 6 directive: grep `src/core/emulator.cpp` for `config_.type` and verify any port-gating usages.

**Result:** 2 remaining `config_.type` checks in port-handler context (lines 3205 and 3240), both in the `0x7FFD` write handler:

### NIT-1 — `emulator.cpp:3205` — port 0x7FFD A14-gate on +3 timing

```cpp
// VHDL 2593: on +3 timing, require A14=1
if (config_.type == MachineType::ZX_PLUS3 && (port & 0x4000) == 0) return;
```
* **VHDL anchor** `zxnext.vhd:2593`:
  ```
  port_7ffd <= '1' when cpu_a(15) = '0' and (cpu_a(14) = '1' or p3_timing_hw_en = '0')
                    and port_fd = '1' and port_1ffd = '0' and port_7ffd_io_en = '1' else '0';
  ```
* The gate `p3_timing_hw_en = '0'` is the tim_sel axis, not typ_sel — same family as D3F-01.
* **Severity:** Class-(c). Same silent-mis-decode footprint as D3F-01 for users writing NR 0x03 with `tim_sel != typ_sel`.
* **Suggested fix:** `if (mmu_.machine_timing() == MachineTimingMode::TimingPlus3 && (port & 0x4000) == 0) return;`

### NIT-2 — `emulator.cpp:3240` — slot-3 contention pattern selection

```cpp
// Update 0xC000 contention based on machine type (VHDL zxnext.vhd:4489-4493):
//   128K: odd banks (1,3,5,7) are contended
//   +3:   banks >= 4 (4,5,6,7) are contended
uint8_t bank = v & 0x07;
bool slot3_contended;
if (config_.type == MachineType::ZX_PLUS3)
    slot3_contended = (bank >= 4);
else
    slot3_contended = (bank & 1) != 0;
```
* **VHDL anchor** `zxnext.vhd:4489-4493`:
  ```
  mem_contend <= '0' when ... else
                 '1' when machine_timing_48  = '1' and ... else
                 '1' when machine_timing_128 = '1' and mem_active_page(1) = '1' else  -- odd
                 '1' when machine_timing_p3  = '1' and mem_active_page(3) = '1' else  -- >=4
                 '0';
  ```
* The pattern selector keys on `machine_timing_*`, NOT machine_type — same family as D3F-01/02/03 and V24-MEM-01. V24-MEM-01 fixed the canonical `mem_contend` consumer in `Mmu::mem_contend_for_` but did NOT touch this secondary `set_contended_slot(3, ...)` update in the 0x7FFD write handler.
* **Severity:** Class-(c). When a user writes NR 0x03 with `tim_sel=+3 + typ_sel=128K`, the secondary contention update applies the 128K odd-bank pattern instead of the +3 ≥4 pattern.
* **Note:** This is a coverage gap relative to V24-MEM-01's scope, not a regression from the D3F-01/02/03 fix. Recording here per Step 6 (adjacent re-audit) since it lives in `emulator.cpp` and is in the same family.
* **Suggested fix:** route through `mmu_.machine_timing()`:
  ```cpp
  if (mmu_.machine_timing() == MachineTimingMode::TimingPlus3)
      slot3_contended = (bank >= 4);
  else if (mmu_.machine_timing() == MachineTimingMode::Timing128)
      slot3_contended = (bank & 1) != 0;
  else
      slot3_contended = false;  // 48K/Pentagon don't contend bank-switched slot 3
  ```
  (or simpler: keep else-branch as `(bank & 1) != 0` for 128 if Pentagon-collision is not relevant.)

### Why these are NITs and not REJECT findings

* The prompt explicitly scopes D3F-01/02/03 to the 3 sites the D3 reviewer flagged (`port_p3_float`, `port_bffd_rd`, `port_ff_dat_ula`). The dev did NOT claim to fix additional sites.
* The two NIT sites surface only on the post-init `tim_sel != typ_sel` divergence path, and the standard NextZXOS boot writes `tim_sel == typ_sel` (mirrored by the dev's V24-MEM-01 init seed at lines 364-371). Real-world impact is low until a user-NR-0x03-write splits the axes.
* These are clean follow-ups for a separate commit with their own discriminative tests.

---

## 6. Final state

* Working tree: clean (sandwich revert was rolled back via `git checkout HEAD -- src/core/emulator.cpp`).
* `ctest`: 38/38 PASS.
* FUSE: 1356/1356 PASS.
* `floating_bus_test`: 34/34 PASS (Section 7 D3F: 3/3 new rows).
* `audio_port_dispatch_test`: 21/21 PASS.
* Regression script: 33/0/0 PASS.
* HEAD: `d2e4aa67`.

## 7. Verdict

**APPROVE-WITH-NITS.**

* 3 fixes VHDL-faithful (D3F-01/02/03 at :2589, :2771, :4513).
* 3 new discriminative regression tests (FB-D3F-01/02/03), all sandwich-verified.
* 2 legacy-test updates (FB-3F, IO-05) are legitimate bug-corrections accommodating VHDL-faithful behavior — not enshrinement.
* No missed test regressions in the full suite.
* 2 NITs: lines 3205 and 3240 still gate port-decode/contention on `config_.type` where VHDL keys on `machine_timing_*`. Same family; follow-up scope.

No REJECT-class issues. Approve with the 2 NITs recorded for a separate follow-up commit.
