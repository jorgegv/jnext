# Pass-13 Memory subsystem audit — Independent Review

**Branch under review:** `task2/verify13-memory` HEAD `a185c8e`
**Reviewer worktree:** `.claude/worktrees/task2-verify13-memory-reviewer`
**Reviewer branch:** `task2/verify13-memory-reviewer` (forked off `a185c8e`)
**Date:** 2026-05-10
**Verdict:** **APPROVE**

## Mandate

Independent review of Pass-13 audit (1 class-(a) finding, V13-MEM-01).
Verify (1) the VHDL claim, (2) fix correctness, (3) discriminative
tests, (4) full Release-mode test suite passes, (5) any missed cases.

## 1. VHDL claim — VERIFIED

Read `zxnext.vhd:3904-3935` directly. The claim is **exact**:

- `port_123b_layer2_en` is a single FF, declared implicitly via the
  `:3908` reset clause (`port_123b_layer2_en <= '0'`).
- The FF has TWO write paths in the same registered process:
  - Line 3914-3916: when `port_123b_wr = '1'` and `cpu_do(4) = '0'`,
    `port_123b_layer2_en <= cpu_do(1)`.
  - Line 3924-3925: when `nr_69_we = '1'`,
    `port_123b_layer2_en <= nr_wr_dat(7)`.
- Line 3933: `port_123b_dat <= ... & port_123b_layer2_en & ...` (this
  FF surfaces as bit 1 of the port 0x123B read-back byte).

Both writers feed the same FF — the canonical VHDL "single FF, multiple
writers" pattern. Pre-fix jnext violated this by routing the two writers
to two different mirrors (`Layer2::enabled_` for NR, `Mmu::l2_enable_`
for port). The bug is one-directional — NR 0x69 → port 0x123B read —
because the port write handler already updates BOTH mirrors
(`emulator.cpp:2645-2653`).

## 2. Fix correctness — APPROVED

Diff at `a185c8e^..a185c8e`:

### 2.1 `Mmu::set_l2_enable` setter (mmu.h:1054)

```c++
void set_l2_enable(bool en) { l2_enable_ = en; }
bool l2_enable() const { return l2_enable_; }
```

- Trivial inline setter — no side effects, no validation needed
  (mirrors a single bool latch).
- Matches the existing cross-subsystem-mirror pattern of
  `set_l2_active_bank` (NR 0x12 → Mmu) and `set_l2_shadow_bank`
  (NR 0x13 → Mmu) at `mmu.h:1027/1036`.
- The new `l2_enable()` observable is well-scoped (used only by tests
  + this regression guard).

### 2.2 NR 0x69 write handler (emulator.cpp:2160)

```c++
mmu_.set_l2_enable((v & 0x80) != 0);
```

- Inserted between the existing `layer2_.set_enabled(...)` call
  (Layer2 mirror, used by NR 0x69 read) and the `mmu_.set_port_7ffd_bit3`
  call (port_7ffd_reg(3) fan-out).
- Bit-extraction polarity matches VHDL :3925 (`nr_wr_dat(7)`).
- Cross-subsystem-mirror inline comment is accurate and well-cited.

### 2.3 Symmetric path verification (port 0x123B write)

Verified at `emulator.cpp:2639-2653` that the port 0x123B write handler
already updates BOTH mirrors:
- `Mmu` via `mmu_.set_l2_port(val, ...)` (`mmu.cpp:327` sets `l2_enable_`).
- `Layer2` via `layer2_.set_enabled((val & 0x02) != 0)` (line 2652).

So with the V13-MEM-01 fix in place, both writers (NR 0x69 + port 0x123B)
keep both mirrors (`Layer2::enabled_` + `Mmu::l2_enable_`) in sync —
faithful to VHDL's single-FF-two-writers pattern.

### 2.4 Save/load state coverage

Verified `Mmu::l2_enable_` is already serialised at `mmu.cpp:857,922`,
so the fix doesn't change the persisted-state surface.

## 3. Discriminative tests — VERIFIED

### 3.1 Discriminative revert procedure

Reverted the `mmu_.set_l2_enable((v & 0x80) != 0);` line in the NR 0x69
handler (emulator.cpp:2160). Rebuilt `mmu_integration_test`. Result:

```
FAIL V13-MEM-01-B: NR 0x69 bit 7 = 1 fans out into port 0x123B bit 1 = 1
[zxnext.vhd:3924-3925 nr_69_we drives port_123b_layer2_en]
[expected bit 1 = 1, got 0x00 (Mmu::l2_enable_ stale after NR 0x69 fan-out)]
Total: 16 Passed: 15 Failed: 1
V13-MEM-01-L2EN  4/5
```

V13-MEM-01-B is the **discriminative bug-surface row** — it fails
without the fix and passes with it. Restored the fix → 16/16 PASS.

### 3.2 Per-row analysis

| Row | Purpose | Discriminative? |
|-----|---------|-----------------|
| V13-MEM-01-A | Baseline (clear NR 0x69 + port 0x123B) | Vacuous (initial state already 0) — sanity guard, not discriminative |
| V13-MEM-01-B | **Bug surface** — NR $69,$80 → IN A,(123B) bit 1 = 1 | **Yes** — fails without fix |
| V13-MEM-01-C | Parallel guard — NR 0x69 read still works (Layer2 mirror) | Regression guard, not discriminative for V13-MEM-01 |
| V13-MEM-01-D | Sweep — NR $69,$00 clears port 0x123B bit 1 | Vacuous without fix (Mmu mirror already 0); discriminative only if a prior port-write set it true. Passes with or without fix when run after B without fix. **Caveat**: with the fix, B set `l2_enable_=true`, so D's clear correctly drives it to false; without the fix, B never sets `l2_enable_=true`, so D's clear is a no-op. Both pass — D guards "fix is not a one-shot raise" only when fix is in place. Acceptable as a forward-direction guard. |
| V13-MEM-01-E | Independence guard — NR 0x69 only touches bit 1 | Discriminative for "NR $69 also writes other port_123b bits" regression — ensures fix is narrow |

Row B is the strong discriminator. Rows A/C/D/E are well-targeted
secondary guards (cold-state baseline, regression guard for the
already-working Layer2 path, sweep verification, and scope/independence
guard). I confirm the test set is **adequately discriminative** — at
least one row (B) FAILS reproducibly without the fix and PASSES with it,
which is the hard-rule criterion.

## 4. Full Release-mode test suite — PASS

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`
Compile: `cmake --build build -j$(nproc)` — clean.

| Suite | Result |
|-------|--------|
| `ctest --test-dir build` | 38/38 PASS, 0 FAIL |
| `fuse_z80_test` | 1356/1356 PASS, 0 FAIL |
| `mmu_integration_test` | 16/16 PASS, 0 FAIL — V13-MEM-01-L2EN 5/5 |

Zero FAILs in any suite.

## 5. Hunt for missed cases

### 5.1 Other Layer2 NR fields with cross-subsystem mirrors

Audited all `port_123b_layer2_*` fields in VHDL (zxnext.vhd:907-912):

- `port_123b_layer2_en` — TWO writers (port + NR 0x69). **V13-MEM-01 covers this.**
- `port_123b_layer2_map_wr_en` — port 0x123B only (cpu_do(0)). No NR fan-out at :3924.
- `port_123b_layer2_map_rd_en` — port 0x123B only (cpu_do(2)). No NR fan-out.
- `port_123b_layer2_map_shadow` — port 0x123B only (cpu_do(3)). No NR fan-out.
- `port_123b_layer2_map_segment` — port 0x123B only (cpu_do(7:6)). No NR fan-out.
- `port_123b_layer2_offset` — port 0x123B with bit 4=1 only. No NR fan-out.

Conclusion: **`port_123b_layer2_en` is the ONLY port_123b field with
multi-writer fan-out**. Pass-13 V13-MEM-01 closes the only gap in this
family.

### 5.2 Other "single FF, multiple NR writers" patterns

Audited `grep "elsif nr_..._we = '1' then"` across zxnext.vhd:

| FF | Writers | Status |
|----|---------|--------|
| `port_ff_reg(5:0)` | port 0xFF + NR 0x69 (b5:0) | Centralised in `port_ff_reg_` field — OK |
| `port_ff_reg(6)` | port 0xFF + NR 0x22 b2 + NR 0xC4 b0 | Pass-12 V12-NMP-01/02 closed |
| `port_7ffd_reg(3)` | port 0x7FFD + NR 0x69 b6 + NR 0x8E b6 (special) | `mmu_.set_port_7ffd_bit3` keeps single store; OK |
| `port_7ffd_reg(5)` | port 0x7FFD + NR 0x08 b7 | Verify3-memory class-(a) covered |
| `port_7ffd_reg(2:0)` | port 0x7FFD + NR 0x8E b6:4 | NR 0x8E unified in `write_nr_8e()`; OK |
| `port_7ffd_reg(4)` | port 0x7FFD + NR 0x8E b0 | Same as above |
| `port_123b_layer2_en` | port 0x123B + NR 0x69 b7 | **V13-MEM-01 closes** |
| `nr_d9_iotrap_write` | port 0x3FFD + NR 0xD9 | Centralised in `nr_d9_iotrap_write_` field — OK |
| `nr_da_iotrap_cause` | NMI accept + NR 0x02 b4 reset | NMI domain — Pass-12 V12-NMP-01 family |
| `nr_c2/c3_retn_address_*` | Z80N NMIACK + NR 0xC2/C3 | Stackless NMI domain — class-(d) escalation per pass-10 |
| `nr_80_expbus(7)` | NR 0x80 + hotkey expbus enable/disable | Out of memory subsystem scope (keyboard/expbus) |
| `nr_8c_alt_rom_lo/hi` | NR 0x8C + reset lo→hi copy | Modelled at `mmu.cpp:104-107` |

No additional missed cross-subsystem-mirror cases were found in the
memory subsystem domain. The V13-MEM-01 fix correctly closes the only
remaining `port_123b_*` family gap.

### 5.3 NR 0x69 NR side completeness

NR 0x69 fans into THREE FFs per VHDL:
- `port_ff_reg(5:0) <= nr_wr_dat(5:0)` (line 3617-3618) — **emulator.cpp:2174 covers**
- `port_7ffd_reg(3) <= nr_wr_dat(6)` (line 3658-3660) — **emulator.cpp:2168 covers**
- `port_123b_layer2_en <= nr_wr_dat(7)` (line 3924-3925) — **V13-MEM-01 covers (this fix)**

All three NR 0x69 fan-outs are now correctly mirrored in jnext.

## 6. Verdict

**APPROVE**

- VHDL claim is exact and verified.
- Fix is minimal, correct, and matches the existing cross-subsystem-mirror
  pattern used by `set_l2_active_bank` / `set_l2_shadow_bank`.
- Discriminative test V13-MEM-01-B reproducibly fails without the fix
  and passes with it; the surrounding rows are reasonable secondary
  guards.
- Full Release-mode test suite is clean (ctest 38/38, FUSE 1356/1356,
  mmu_integration_test 16/16).
- No missed cases found in the `port_123b_*` family or the broader
  memory-domain "single FF, multiple writers" pattern. NR 0x69 is now
  fully covered across all three of its VHDL fan-outs.

## Final return

```json
{
  "verdict": "APPROVE",
  "findings_verified": 1,
  "discriminative": ["V13-MEM-01-B"],
  "issues": [],
  "tests_passed": true,
  "head_sha": "a185c8e"
}
```
