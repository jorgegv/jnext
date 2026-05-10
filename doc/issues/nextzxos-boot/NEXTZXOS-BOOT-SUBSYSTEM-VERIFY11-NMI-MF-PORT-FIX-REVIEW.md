# Pass-11 Fix-of-Reviewer Independent Review — V11-NMP-03 NR $06 cache leak

**Reviewer**: independent (not the audit author, not the fix author, not the prior reviewer)
**Branch reviewed**: `task2/verify11-nmi-mf-port` HEAD `12f83f2`
**Reviewer worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify11-nmi-mf-port-fix-reviewer`
**Reviewer branch**: `task2/verify11-nmi-mf-port-fix-reviewer` (forked off `task2/verify11-nmi-mf-port` @ `12f83f2`)
**Base**: integration HEAD `d385d5e`

## Verdict

**APPROVE**

The V11-NMP-03 fix is VHDL-faithful, structurally correct, and discriminatively
tested. The fix mirrors the V11-NMP-02 pattern exactly (canonicalise the stored
byte for config-mode-gated bits when `config_mode` is closed). The discriminative
test is well-targeted: it forces the cache-leak path through the init/reset
fan-out at `emulator.cpp:3272-3276` and observes the post-reset NR 0x06 readback.
A pre-revert run exposed exactly the predicted FAIL (`after_reset=0xA0`),
confirming the test discriminates the bug.

Tests in the post-fix state: ctest 38/38, FUSE 1356/1356, nextreg_integration 233/233,
zero FAILs across the board.

No additional cache-leak siblings of the V11-NMP-02 / V11-NMP-03 family were
found by my fan-out audit (NRs 0x03, 0x10, 0x11 — see "Sibling audit" below).

## VHDL claim verification

`zxnext.vhd:5161-5170` (NR 0x06 write decoder):

```
when X"06" =>
   nr_06_hotkey_cpu_speed_en   <= nr_wr_dat(7);    -- :5162  unconditional
   nr_06_internal_speaker_beep <= nr_wr_dat(6);    -- :5163  unconditional
   nr_06_hotkey_5060_en        <= nr_wr_dat(5);    -- :5164  unconditional
   nr_06_button_drive_nmi_en   <= nr_wr_dat(4);    -- :5165  unconditional
   nr_06_button_m1_nmi_en      <= nr_wr_dat(3);    -- :5166  unconditional
   if nr_03_config_mode = '1' then                  -- :5167  GATE
      nr_06_ps2_mode <= nr_wr_dat(2);              -- :5168
   end if;                                          -- :5169
   nr_06_psg_mode <= nr_wr_dat(1 downto 0);        -- :5170  unconditional
```

Confirmed:

- **Bit 2** (`nr_06_ps2_mode`) is the **only** field gated by `nr_03_config_mode='1'`,
  per `:5167-5169`.
- **Bits 7, 6, 5, 4, 3, 1, 0** are **unconditionally** writable per `:5162-5166` and
  `:5170`.
- The audit/fix's claim is precise and correct.

## Fix correctness

Read of `src/core/emulator.cpp:3289-3370` (post-fix) and the diff in commit
`816dce9`:

The fix introduces a single canonicalisation at the tail of the NR 0x06 write
handler. The relevant excerpt (lines 3325-3326, 3365-3369):

```cpp
const bool cfg = nextreg_.nr_03_config_mode();
if (cfg) {
    nr_06_ps2_mode_ = ((v >> 2) & 1) != 0;        // shadow gated (was already correct)
}
[…]
if (cfg) {
    return v;                                      // cache passes through when gate open
}
const uint8_t prev = nextreg_.cached(0x06);
return static_cast<uint8_t>((v & 0xFB) | (prev & 0x04));   // gate closed: keep prev bit 2
```

Verification:

- **`0xFB == 0b11111011`** = `~bit2`. Correct: bits 7,6,5,4,3,1,0 come from `v`,
  bit 2 is masked out from `v`.
- **`prev & 0x04`** = bit 2 of the prior cached value. ORed in to preserve the
  VHDL-faithful "latch holds when gate closed" semantic for bit 2.
- The branch is gated on `cfg == false`, so when config_mode is open the byte
  passes through verbatim — matching VHDL's behaviour when the gate is open.
- The shared `cfg` local (introduced at line 3325) is used consistently for both
  the live shadow update (lines 3326-3328) and the cache canonicalisation
  (lines 3365-3369), avoiding any race or repeated-read concern.

**Pattern conformance with V11-NMP-02**: the V11-NMP-02 fix at lines 1027-1031
uses the same shape:

```cpp
if (cfg) { return v; }
const uint8_t prev = nextreg_.cached(0x0A);
return static_cast<uint8_t>((v & 0x1F) | (prev & 0xE0));   // bits 7:5 gated → keep prev
```

V11-NMP-03 mirrors this exactly with the appropriate mask for NR 0x06's
single-bit gate. The fix is correct and minimal.

## Discriminative test verification

The new test is in `test/nextreg_integration_test.cpp` immediately after the
NR 0x0A V11-NMP-02 test, inside the G56 cluster B suite (correct location —
this is the same audit cluster that covers V11-NMP-02). It performs:

1. (a) Enter config_mode (NR 0x03 = 0x07), commit NR 0x06 = 0x04 — bit 2 set.
2. (b) Exit config_mode (NR 0x03 = 0x01).
3. (c) Out-of-config_mode write NR 0x06 = 0x00 — VHDL latch should retain bit 2 = 1;
   pre-fix the C++ cache absorbs `v=0x00` and clears bit 2 in the stored byte.
4. (d) Hard reset (NR 0x02 = 0x02). The init/reset fan-out at `emulator.cpp:3272-3276`
   re-seeds `nr_06_ps2_mode_` from `cached(0x06) & 0x04`.
5. Assert: `after_reset & 0x04 == 0x04` (bit 2 preserved).

The asserted post-reset value is **0xA4** (bits 7,5 from the NR-reset clause +
the preserved bit 2 = ps2_mode=1).

**Pre-revert FAIL verification** (run by reviewer):

```
$ git checkout 2aa35ac -- src/core/emulator.cpp   # revert the fix only
$ cmake --build build -j$(nproc)
$ ./build/test/nextreg_integration_test
FAIL V11-NMP-03: NR 0x06 bit 2 (ps2_mode) survives an out-of-config_mode
  attempted clear AND a subsequent hard reset
  [after_commit=0x04 after_reject=0x04 after_reset=0xA0 (want bit 2 set in each)]
Total:  233  Passed:  232  Failed:    1  Skipped:    0
```

`after_reset=0xA0` is exactly what the audit predicted (only the NR-reset bits 7,5
are set; bit 2 was lost). After restoring the fix:

```
$ git checkout 12f83f2 -- src/core/emulator.cpp
$ cmake --build build -j$(nproc)
$ ./build/test/nextreg_integration_test
Total:  233  Passed:  233  Failed:    0  Skipped:    0
```

The test is **discriminative** — it cleanly fails on the pre-fix code and passes
on the post-fix code, on the exact bug pathway described.

## Full test suite (post-fix)

All ran against the post-fix HEAD `12f83f2` in this worktree:

| Suite                              | Result                                       |
|------------------------------------|----------------------------------------------|
| `ctest --test-dir build`           | **38/38 PASS**, 0 FAIL                       |
| `./build/test/fuse_z80_test`       | **1356/1356 PASS**, 0 FAIL, 0 SKIP           |
| `./build/test/nextreg_integration` | **233/233 PASS**, 0 FAIL (V11-NMP-03 PASS)   |

Zero failures. Zero regressions versus the prior reviewer's reported counts
(38/38, 1356/1356, 232/232 pre-fix → 233/233 post-fix is the expected +1
for the new test).

## Sibling audit — other config-mode-gated NRs

I scanned `zxnext.vhd` for `if nr_03_config_mode = '1' then` write-handler gates
and audited each for the same cache-leak pattern (raw `v` returned to cache
while a sub-field is gated):

| NR    | VHDL line | Gated field(s)                           | C++ handler line | Cache-leak risk?                                              |
|-------|-----------|------------------------------------------|------------------|---------------------------------------------------------------|
| 0x03  | :5137-5145| `nr_03_machine_type[2:0]`                | emulator.cpp:1915| **No** — handler returns recomputed `(timing<<4)\|(dtlock<<3)\|mtype` (`emulator.cpp:2050-2054`); only sources from gated shadows. |
| 0x06  | :5167-5169| `nr_06_ps2_mode[2]`                      | emulator.cpp:3289| **Fixed by V11-NMP-03.**                                      |
| 0x0A  | :5192-5195| `nr_0a_mf_type[7:6]`, `nr_0a_sd_swap[5]` | emulator.cpp:998 | **Fixed by V11-NMP-02.**                                      |
| 0x10  | :5682,    | `nr_10_coreid[4:0]`                      | emulator.cpp:1173| **No** — handler returns recomputed `((nr_10_coreid_ & 0x1F) << 2)` (`emulator.cpp:1178`); shadow is the gated one, byte rebuilt every write. |
|       | :5700-5702|                                          |                  |                                                               |
| 0x11  | :5209-5230| `nr_11_video_timing[2:0]`                | emulator.cpp:758 | **No** — when `cfg` is closed, returns `cached(0x11) & 0x07` (`emulator.cpp:761-762`), short-circuits the path — write `v` is never stored. |

**Conclusion**: no further siblings exist in the V11-NMP-02/03 family. The
audit's two cache-leak fixes (V11-NMP-02 NR 0x0A bits 7:5 + V11-NMP-03 NR 0x06
bit 2) cover all the affected registers in the C++ today. The other gated NRs
are protected by different (also correct) C++ patterns (recompute-from-shadow
or short-circuit-when-gate-closed), so the bug class is now closed.

This sibling audit is an **observation**, not a finding. No fix is recommended.

## Other sanity spot-checks

I also re-read the prior reviewer's report (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY11-NMI-MF-PORT-REVIEW.md`)
to ensure the original V11-NMP-01 / V11-NMP-02 fixes were not destabilised by
the V11-NMP-03 follow-up:

- The V11-NMP-03 patch touches only the NR 0x06 write handler at lines
  3322-3370 and adds one new test in `nextreg_integration_test.cpp`. It does
  not modify the V11-NMP-01 or V11-NMP-02 code paths.
- All G56-CR-81 / G56-Cluster-B tests still pass (= V11-NMP-01 / V11-NMP-02
  guarded paths intact).
- The doc commit `12f83f2` correctly bumps the class-(c) finding count
  from 2 to 3 in the audit report and adds a V11-NMP-03 section. Doc is
  consistent with code.

## Final verdict

**APPROVE**

- VHDL claim correct (line 5167-5169 is the gate; bit 2 is the only gated bit;
  surrounding bits unconditional).
- Fix mirrors V11-NMP-02 pattern with the right mask (`0xFB`).
- Discriminative test confirmed FAIL on pre-fix revert (`after_reset=0xA0`)
  and PASS on post-fix restore (`after_reset=0xA4`).
- All test suites green: ctest 38/38, FUSE 1356/1356, nextreg 233/233.
- Sibling audit found no further cache-leak cases in the same family.

## Commit

This review report committed as
`review(task2-verify11-nmi-mf-port-fix): V11-NMP-03 fix-reviewer — APPROVE`.
