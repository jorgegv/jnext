# D3 / V24-MEM-01 / V25-MEM-01 — Contention Timing Axis Split — Independent Review

**Reviewer**: Independent reviewer agent (Task 2 D3 verify cycle).
**Subject commit**: `6e68c680` — `fix(task2-d3): V24-MEM-01 — contention timing axis split (machine_timing_ vs MachineType) per VHDL zxnext.vhd:5761-5777,:6694-6703,:4490-4492`.
**Branch**: `task2/d3-v24-mem-01-reviewer` (off dev branch HEAD).
**Date**: 2026-05-11.

---

## Verdict: **APPROVE-WITH-NITS**

The contention timing axis split is **VHDL-faithful and correctly scoped**. All cited VHDL anchors verified independently; sandwich verification discriminates as advertised; save/load schema is backward-compatible; test invariants hold (ctest 38/38, FUSE 1356/1356, contention_test 88/88, regression 33/0/0). The fix is a clean class-(d) architectural improvement — splits `MachineType` (typ_sel) from `MachineTimingMode` (tim_sel) on exactly the surfaces the dev claimed.

Two **NIT-class** observations below for the dev's awareness:
- NIT-1: Emulator::load_state re-sync **collapses** the deferred-commit pair from the Mmu schema slot. Documented but worth flagging.
- NIT-2: Comment block in `contention.h:13-17` lists `port_ff_dat_ula` (VHDL :4513) as a machine_timing_ consumer in jnext, but jnext's actual port 0xFF read handler (emulator.cpp:6829) still gates on `config_.type` (typ_sel). The comment overstates the post-fix state.

Three **adjacent missed audits** identified — all out-of-scope for D3 (which is explicitly contention-only) but worth recording for follow-up.

---

## 1. VHDL faithfulness — confirmed

Read each cited VHDL anchor in `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd` independently against the C++ implementation:

### 1.1 `zxnext.vhd:5761-5777` — one-hot decode of `eff_nr_03_machine_timing`

VHDL:
```vhdl
if eff_nr_03_machine_timing(2) = '1' then
    machine_timing_pentagon <= '1';
elsif eff_nr_03_machine_timing(1 downto 0) = "10" then
    machine_timing_128 <= '1';
elsif eff_nr_03_machine_timing(1 downto 0) = "11" then
    machine_timing_p3 <= '1';
else
    machine_timing_48 <= '1';
end if;
```

C++ `contention.h:58-63`:
```cpp
inline constexpr MachineTimingMode decode_nr_03_machine_timing(uint8_t v) {
    if ((v & 0x04) != 0) return MachineTimingMode::TimingPentagon;
    if ((v & 0x03) == 0x02) return MachineTimingMode::Timing128;
    if ((v & 0x03) == 0x03) return MachineTimingMode::TimingPlus3;
    return MachineTimingMode::Timing48;
}
```

**VHDL-faithful**: Pentagon priority on bit 2; then bits[1:0]=10 → 128, =11 → +3, else 48.

### 1.2 `zxnext.vhd:6694-6703` — video-frame-edge latch

VHDL:
```vhdl
if video_frame_sync = '1' then
    eff_nr_03_machine_timing <= nr_03_machine_timing;
end if;
```

C++ — `emulator.cpp:5645-5646` at top of `run_frame()`:
```cpp
contention_.commit_pending_machine_timing();
mmu_.commit_pending_machine_timing();
```

**VHDL-faithful**: jnext's per-frame seam is the top of `run_frame()`. One commit per logical video frame. Note: `video_frame_sync` in VHDL fires during VSYNC (end of vertical blanking) — placing the commit at frame top is semantically equivalent for the contention surfaces, since both shadow→effective transitions are non-observable in the period between VSYNC and next-frame start.

### 1.3 `zxnext.vhd:5121-5135` — NR 0x03 bits 6:4 immediate write gates

VHDL:
```vhdl
if nr_wr_dat(7) = '1' and nr_03_user_dt_lock = '0' and nr_wr_dat(3) = '0' then
    case nr_wr_dat(6 downto 4) is
        when "000"  => nr_03_machine_timing <= "001";
        when "001"  => nr_03_machine_timing <= "001";
        when "010"  => nr_03_machine_timing <= "010";
        when "011"  => nr_03_machine_timing <= "011";
        when "100"  => nr_03_machine_timing <= "100";
        when others => nr_03_machine_timing <= "011";
    end case;
end if;
```

C++ — `emulator.cpp:2296-2376` (NR 0x03 write_handler):
```cpp
const bool bit7 = (v & 0x80) != 0;
const bool bit3 = (v & 0x08) != 0;
if (bit7 && !nextreg_.nr_03_user_dt_lock() && !bit3) {
    const uint8_t tim_sel = static_cast<uint8_t>((v >> 4) & 0x07);
    uint8_t new_timing;
    switch (tim_sel) {
        case 0x00: new_timing = 0x01; break;
        case 0x01: new_timing = 0x01; break;
        case 0x02: new_timing = 0x02; break;
        case 0x03: new_timing = 0x03; break;
        case 0x04: new_timing = 0x04; break;
        default:   new_timing = 0x03; break;
    }
    nextreg_.set_nr_03_machine_timing(new_timing);
    // ...
    const MachineTimingMode tim_mode = decode_nr_03_machine_timing(new_timing);
    contention_.set_pending_machine_timing(tim_mode);
    mmu_.set_pending_machine_timing(tim_mode);
}
```

**VHDL-faithful**: gates match (bit7=1, dt_lock=0, bit3=0); case mapping matches (000/001→001, 010→010, 011→011, 100→100, others→011); the `set_pending_machine_timing` push sits inside the gate.

### 1.4 `zxnext.vhd:4481` — `i_contention_en` Pentagon-disable gate

VHDL:
```vhdl
i_contention_en => (not eff_nr_08_contention_disable) and (not machine_timing_pentagon)
                   and (not cpu_speed(1)) and (not cpu_speed(0)),
```

C++ — `contention.cpp:126-128`:
```cpp
if (contention_disable_) return false;
if (cpu_speed_ != 0)     return false;
if (machine_timing_ == MachineTimingMode::TimingPentagon) return false;
```

**VHDL-faithful**: all four AND-terms enforced. Pentagon disables contention via `machine_timing_` (tim_sel), per VHDL.

### 1.5 `zxnext.vhd:4490-4492` — `mem_contend` per-machine page decode

VHDL:
```vhdl
mem_contend <= '0' when mem_active_page(7 downto 4) /= "0000" else
               '1' when machine_timing_48  = '1' and mem_active_page(3 downto 1) = "101" else
               '1' when machine_timing_128 = '1' and mem_active_page(1) = '1' else
               '1' when machine_timing_p3  = '1' and mem_active_page(3) = '1' else
               '0';
```

C++ — `contention.cpp:138-152`:
```cpp
if ((mem_active_page_ & 0xF0) != 0) return false;
const uint8_t low = mem_active_page_ & 0x0F;
switch (machine_timing_) {
    case MachineTimingMode::Timing48:
        return ((low >> 1) & 0x07) == 0x05;
    case MachineTimingMode::Timing128:
        return (low & 0x02) != 0;
    case MachineTimingMode::TimingPlus3:
        return (low & 0x08) != 0;
    case MachineTimingMode::TimingPentagon:
        return false;
}
```

Same decode in `Mmu::mem_contend_for_` (mmu.h:1213-1232) and in `contention_tick` (contention.cpp:283-301).

**VHDL-faithful**: 48 → (low>>1)&7 == 5 (matches "101" on bits 3:1); 128 → low & 2 (matches bit 1); +3 → low & 8 (matches bit 3); Pentagon → already gated by i_contention_en.

### 1.6 `zxnext.vhd:1099, :1377` — power-on defaults "011"

VHDL:
```vhdl
signal nr_03_machine_timing : std_logic_vector(2 downto 0) := "011";
signal eff_nr_03_machine_timing : std_logic_vector(2 downto 0) := "011";
```

C++ — `contention.h:379-380` and `mmu.h:1358-1359`:
```cpp
MachineTimingMode machine_timing_         = MachineTimingMode::TimingPlus3;
MachineTimingMode pending_machine_timing_ = MachineTimingMode::TimingPlus3;
```

**VHDL-faithful**: both shadow and effective default to TimingPlus3 ("011").

---

## 2. Sandwich verification (independent)

### Sub-sandwich 1: revert `is_contended_access` switch to `type_`

Edited `contention.cpp:138-153` to switch on `type_` (MachineType) instead of `machine_timing_`. Rebuilt + ran `contention_test`.

**Result**: 88 → 83 pass, **5 FAIL**:
- `CT-GATE-08` (default-ctor — pre-fix ZXN_ISSUE2 fallthrough returns false; post-fix needs TimingPlus3 decode active)
- `D3-CONTENTION-01` (tim_sel=128/typ_sel=48 — pre-fix uses 48-decode → false instead of 128's true)
- `D3-CONTENTION-02` (tim_sel=+3/typ_sel=128 — pre-fix uses 128-decode → false instead of +3's true)
- `D3-CONTENTION-03` (deferred-commit — bare-class path; pre-fix uses 48-decode for all states)
- `D3-CONTENTION-04` (Emulator end-to-end — same as above for 128/+3 axis split)

**D3-CONTENTION-06 (Pentagon)** still passes because the Pentagon gate is in the `i_contention_en` path (untouched by this sub-sandwich). Discriminative power confirmed for the per-machine bank decode.

Restored fix → 88/88 pass.

### Sub-sandwich 2: remove `commit_pending_machine_timing()` from `run_frame()`

Commented out the two `commit_pending_machine_timing()` calls in `emulator.cpp:5645-5646`. Rebuilt + ran `contention_test`.

**Result**: 88 → 86 pass, **2 FAIL**:
- `D3-CONTENTION-04` (Emulator end-to-end — needs run_frame() to promote pending to effective; without commit, contention sees old effective).
- `FIX-CONTEND-NR03-INT-01` (also drives `run_frame` and expects the post-commit value).

**D3-CONTENTION-03** still passes because the bare-class test invokes `cm.commit_pending_machine_timing()` directly (not via `run_frame`). Discriminative power confirmed for the run_frame seam.

Restored fix → 88/88 pass.

### Note on dev's claim accuracy

Dev claimed sub-sandwich 1 fails "D3-CONTENTION-01/02/06" and sub-sandwich 2 fails "D3-CONTENTION-03/04". My empirical results: sub-sandwich 1 fails 01/02/03/04 (not 06 — Pentagon term is in a separate path); sub-sandwich 2 fails only 04 (plus pre-existing INT-01) — 03 passes because it doesn't use the run_frame seam. Both sandwiches still independently discriminate; the dev's row-naming is just slightly off. The fix's two seams are clearly demonstrated.

---

## 3. Save/load schema compatibility

### 3.1 Old-save round-trip (snapshots predating D3)

`Mmu::load_state` reads `machine_timing_` + `pending_machine_timing_` guarded by `!r.eof()`. Old saves fall through with constructor defaults (TimingPlus3). `Emulator::load_state` then re-derives from `nextreg_.nr_03_machine_timing()` cached byte (which DOES persist via NextReg's own save_state). **Compatible** — old saves round-trip with the canonical NR 0x03 cached value.

Empirical: no existing save files in repo to test against, but code path is verified correct by inspection.

### 3.2 New-save round-trip (snapshots with `pending != effective`)

NIT-1: `Emulator::load_state` (emulator.cpp:7391-7394) unconditionally calls `set_machine_timing(tim_mode)` (immediate-commit) on both ContentionModel and Mmu after the Mmu schema slot has been read. This **collapses** any deferred-commit state restored by the Mmu schema slot:

```cpp
const uint8_t nr_03_tim = nextreg_.nr_03_machine_timing();
const MachineTimingMode tim_mode = decode_nr_03_machine_timing(nr_03_tim);
contention_.set_machine_timing(tim_mode);  // forces pending == effective
mmu_.set_machine_timing(tim_mode);          // forces pending == effective
```

The dev's own comment notes this: "Both shadow and effective fields are re-seeded to the tim_sel-derived MachineTimingMode (mirroring the VHDL power-on state where both ... are equal until the first gated NR 0x03 bits-6:4 write hits between save points)."

**Observable consequence**: if a snapshot is taken between an NR 0x03 bits-6:4 write and the next video-frame edge (where `pending != effective`), the post-load state forces both to the shadow (= pending). The next `run_frame()` then re-commits (idempotent), so contention sees the new value one frame earlier than it would have on a non-snapshotted run.

**Severity**: NIT. The mmu schema slot is effectively cosmetic given the unconditional clobber. The contract is documented but worth flagging — either drop the mmu schema slot (and rely entirely on the NextReg re-derivation), or skip the Emulator::load_state re-sync when the mmu slot was successfully read. Current state is correct-by-collapse but inconsistent in intent.

---

## 4. Side-effect inspection

### 4.1 Default ContentionModel construction

`machine_timing_` defaults to `TimingPlus3` (matches VHDL :1099/:1377). Without `build()`, a bare-class test fixture will exercise the +3 bank-decode (`(low & 0x08) != 0`). This is exactly what CT-GATE-08 now verifies post-fix.

Bare-class consumers checked: `test/contention/contention_test.cpp` constructs many `ContentionModel cm;` instances without `build()` — for those, the new TimingPlus3 default is the operative bank-decode rule. Existing tests don't crash; CT-GATE-08 is the only row that probes the default-ctor's contention behaviour.

### 4.2 Emulator::init NR 0x03 seeding

Verified at emulator.cpp:364-371:
- ZX48K → tim_sel = 0x01 (Timing48) ✓
- ZX128K → tim_sel = 0x02 (Timing128) ✓
- ZX_PLUS3 → tim_sel = 0x03 (TimingPlus3) ✓
- ZXN_ISSUE2 → tim_sel = 0x03 (TimingPlus3 — Next defaults to +3 timing per VHDL :1099) ✓

The push happens under `if (!preserve_memory)` (hard reset only) — soft reset preserves the existing NR 0x03 tim_sel from before reset, matching VHDL `nr_03_machine_timing`'s lack of a reset clause.

Then `Emulator::init` at lines 388-391 reads `nextreg_.nr_03_machine_timing()` (canonical source) and pushes into both ContentionModel and Mmu via `set_machine_timing()` — both hard and soft reset paths re-sync from the canonical state.

### 4.3 NR 0x03 readback (CFG-01 in nextreg_integration_test)

NR 0x03 bits 6:4 are composed from `nr_03_machine_timing_`. Pre-fix this was always 0x03 (VHDL default, no init push). Post-fix for ZXN_ISSUE2 (the test's emulator config) it's still 0x03 (Next → +3 timing). CFG-01 still passes — empirically verified via `ctest --test-dir build` 38/38.

For ZX48K config: NR 0x03 bits 6:4 = 0x01 post-fix (was 0x03 pre-fix). Any test that hardcoded the pre-fix 0x03 readback would break. Search of `test/` finds no such hardcoded expectation. All tests pass.

---

## 5. Three pre-existing test updates

### 5.1 CT-GATE-08 (LEGITIMATE bug-correction)

Pre-fix the row asserted that a default-constructed `ContentionModel` (no `build()`, `type_=ZXN_ISSUE2`) returns false at page=0x0A via the ZXN_ISSUE2 fallthrough. Post-fix the default ctor's `machine_timing_=TimingPlus3` is now the operative axis, and the +3 bank-decode (`bit 3 of low nibble`) is the correct test point: page=0x02 (bit 3=0) → false; page=0x0A (bit 3=1) → true.

The pre-fix assertion was enshrining a fallthrough that doesn't reflect VHDL (VHDL :1099 default is "011" = +3, NOT a fallthrough-false). **VHDL-faithful correction.**

### 5.2 FIX-CONTEND-NR03-01 PASS-B (LEGITIMATE API update)

Pre-fix the test called `cm2.rebuild_for_type(MachineType::ZX_PLUS3)` to flip both contention surfaces. Post-fix `rebuild_for_type` only flips typ_sel; the test now adds `cm2.set_machine_timing(MachineTimingMode::TimingPlus3)` to also flip tim_sel. This matches the production NR 0x03 dispatcher's co-commit of both axes.

**Legitimate API-shape update**, not enshrinement of new behaviour. The contract is unchanged — both axes must be pushed for a fully consistent state.

### 5.3 FIX-CONTEND-NR03-INT-01 (LEGITIMATE re-pivot)

Pre-fix tested ZXN_ISSUE2 → +3 transition asserting contention flipped from false (ZXN fallthrough) to true (+3 banks>=4). The "false at ZXN" pre-state was a typ_sel-axis quirk; post-fix ZXN_ISSUE2 already runs with TimingPlus3 (+3 timing, VHDL :1099 default), so the pre-state would already be true and the transition becomes a no-op.

The new test starts from ZX48K (a discriminative pre-state where page=0x08 is uncontended under Timing48) and drives NR 0x03=0xB3 (tim_sel=+3 + typ_sel=+3 — both axes flip together) → page=0x08 contends post-+3. The pivot is preserved; the rationale is updated to reflect the VHDL-faithful axis split.

**Legitimate re-pivot following VHDL truth**, not enshrinement.

---

## 6. Pentagon timing-axis check

D3-CONTENTION-06 tests that `tim_sel=Pentagon` disables contention regardless of typ_sel. Verified the new gate at `contention.cpp:128`:
```cpp
if (machine_timing_ == MachineTimingMode::TimingPentagon) return false;
```
Directly mirrors VHDL :4481 (`(not machine_timing_pentagon)` AND-term in `i_contention_en`).

Pre-fix this term was hardcoded to '0' (the standalone Pentagon MachineType was retired). Post-fix a user-supplied NR 0x03 with bits 6:4=100 (Pentagon tim_sel) properly silences contention via the new path.

**No boot-path regression**: `regression.sh` shows 33/0/0 — NextZXOS never writes tim_sel=Pentagon; the new gate is dormant on the canonical boot path.

---

## 7. Adjacent missed audits (out-of-scope for D3 but recorded)

The dev's commit body explicitly scopes D3 to the **contention** surfaces. Three other port-decode surfaces that VHDL keys on `machine_timing_*` remain on `config_.type` in jnext:

### 7.1 emulator.cpp:3554 — port 0x0FFD floating-bus gate

VHDL :2589: `port_p3_float <= '1' when ... and p3_timing_hw_en = '1' and ...`.

jnext:
```cpp
if (config_.type != MachineType::ZX_PLUS3) return 0x00;
```

Should gate on `machine_timing_ == TimingPlus3`. Pre-existing inconsistency; not in D3 scope.

### 7.2 emulator.cpp:3688 — port 0xBFFD AY-read alias

VHDL :2771: `port_fffd_rd <= iord and (port_fffd or (port_bffd and machine_timing_p3) or port_bff5);`

jnext:
```cpp
if (config_.type != MachineType::ZX_PLUS3) return 0xFF;
```

Should gate on `machine_timing_ == TimingPlus3`. Pre-existing inconsistency.

### 7.3 emulator.cpp:6829 — port 0xFF `port_ff_dat_ula` gate

VHDL :4513: `port_ff_dat_ula <= ula_floating_bus when (machine_timing_48 = '1' or machine_timing_128 = '1') else X"FF";`

jnext:
```cpp
if (config_.type != MachineType::ZX48K && config_.type != MachineType::ZX128K) {
    return 0xFF;
}
```

The dev's `contention.h` comment block (lines 13-17) lists this as a `machine_timing_` consumer, implying jnext routes it through the new axis — but it doesn't. The existing code at line 6824-6828 acknowledges this as a documented gap: "for Next the runtime `nr_03_machine_timing` could in principle re-enable 48K/128K timing, but ... leaves the runtime re-classification to a follow-up."

### Why these are out-of-scope

D3's class-(d) escalation was explicitly **contention-only**: "the contention surfaces (`ContentionModel::is_contended_access` / `contention_tick` / `port_contend` + `Mmu::mem_contend_for_`)". The dev did not claim to fix the port-decode surfaces — fixing those would require a separate plan with their own discriminative tests.

### Recommendation

Either:
- File three follow-up class-(c)/(d) findings for the port_p3_float / port_bffd_rd / port_ff_dat_ula gates so they're tracked, OR
- Update `contention.h:13-17` to clarify which surfaces are **currently** wired in jnext (just :2594 / :4490-4492 / :4481) vs surfaces VHDL keys on `machine_timing_*` but jnext still keys on `config_.type` (:4513 / :2589 / :2771). Otherwise the comment falsely suggests jnext is fully axis-correct.

The comment inaccuracy is the only concrete NIT for this review.

---

## 8. Test invariants — all hold

Empirically verified on the reviewer branch:
- `ctest --test-dir build`: **38/38 PASS** (all subsystems clean).
- `./build/test/contention_test`: **88/88 PASS** (was 82; +6 new D3 rows).
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 PASS** (Z80 opcode invariant).
- `bash test/00regression/regression.sh`: **33/0/0 PASS** (screenshot + functional regressions).

---

## 9. Summary

| Aspect | Verdict |
|---|---|
| VHDL faithfulness on 6 anchors | **PASS** |
| Sandwich verification (2 sub-sandwiches) | **PASS** (both discriminate) |
| Save/load schema | **PASS** (with NIT-1 on collapse semantics) |
| Default ctor side-effect | **PASS** |
| Init seeding side-effect | **PASS** |
| NR 0x03 readback side-effect | **PASS** |
| CT-GATE-08 update | **LEGITIMATE bug-correction** |
| FIX-CONTEND-NR03-01 PASS-B update | **LEGITIMATE API update** |
| FIX-CONTEND-NR03-INT-01 update | **LEGITIMATE re-pivot** |
| Pentagon timing-axis active | **PASS** (no regression) |
| Adjacent missed audits | **3 out-of-scope** (port_p3_float, port_bffd_rd, port_ff_dat_ula — pre-existing typ_sel gates) |
| Test invariants | ctest 38/38, FUSE 1356/1356, contention 88/88, regression 33/0/0 |

**Verdict: APPROVE-WITH-NITS.**

NITs:
- **NIT-1**: `Emulator::load_state` (emulator.cpp:7391-7394) unconditionally collapses the deferred-commit pair restored from the Mmu schema slot. Either drop the schema slot or skip the re-sync on successful read. Functional impact: minimal (one-frame snapshot round-trip imperfection).
- **NIT-2**: `contention.h:13-17` comment block lists `:4513` (port_ff_dat_ula) as a machine_timing_ consumer in jnext, but jnext's port 0xFF handler at emulator.cpp:6829 still gates on `config_.type` (typ_sel). The comment overstates the post-fix state. Suggest splitting the list into "wired via machine_timing_ in jnext" vs "VHDL keys on machine_timing_, jnext still on typ_sel (follow-up)".

Follow-ups (out-of-scope, suggested):
- Class-(c)/(d) follow-up to wire port_p3_float (:2589), port_bffd_rd (:2771), port_ff_dat_ula (:4513) onto the new tim_sel axis. Required for full VHDL faithfulness on the post-boot user-NR-0x03-tim_sel-flip scenario.
- IM2/CPU pulse_count_end (:2033) is currently immediate (not video-frame-deferred). Pre-existing minor inconsistency; the dev explicitly noted it in the NR 0x03 handler comment ("Our `is_48_or_p3` therefore stays immediate; the new contention/MMU axis is deferred"). Worth a separate audit pass.

The D3 fix itself is sound, well-scoped, and VHDL-faithful on the surfaces it claims. No regressions. **Approved with the two NITs above.**

---

**Reviewer final HEAD**: (will be set at commit time).
