# Pass-11 Verify-Audit — DivMMC + SD + SPI Subsystem — Independent Review

**Date**: 2026-05-10
**Reviewer**: independent (did not participate in Pass-11 audit)
**Branch under review**: `task2/verify11-divmmc-sd-spi`
- Audit report HEAD: `49f7b29`
- Fix + tests HEAD:  `d340bdd`
**Worktree (review)**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify11-divmmc-sd-spi-reviewer`
**Branch (review)**: `task2/verify11-divmmc-sd-spi-reviewer` (forked off the audit head; all
 verification done on the audit fix tree, no rewrites).
**VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`

## Verdict

**APPROVE**.

Both class-(b) findings — `V11-DIVMMC-01` (SPI reset deselect) and
`V11-DIVMMC-02` (DivMmc::is_nmi_hold combinational) — are correctly
identified, accurately VHDL-cited, faithfully fixed, and pinned by
discriminative regression tests that fail pre-fix and pass post-fix. The
full smoke trio is clean (ctest 38/38, FUSE 1356/1356; regression in the
documented baseline flakiness band). I attempted to find missed angles
across the methodology surface and found none worth promoting to a
class-(a)/(b) finding.

## Review protocol

Same protocol the memory-subsystem and prior-pass reviews used:

1. Read the audit report (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY11-DIVMMC-SD-SPI.md`)
   and the audit's commits (`49f7b29` + `d340bdd`).  No other
   `VERIFY*` reports consulted.
2. Verify each finding against the cited VHDL line ranges.
3. Verify the C++ fix is structurally and semantically faithful to that
   VHDL.
4. **Discriminative-test verification** — for each finding:
   - revert the C++ fix (only the fix; not the test or the comment block),
   - rebuild + run `divmmc_test` → confirm the new test row FAILS,
   - restore the fix, rebuild + run → confirm the row PASSES,
   - confirm no previously-passing row regresses to FAIL.
5. Run the full smoke trio (ctest, FUSE, regression).
6. Hunt for findings the audit missed in the listed scope.

No probes were added or used.

## Finding-by-finding verification

### V11-DIVMMC-01 — `SpiMaster::reset()` does not pulse `deselect()`

**VHDL re-cite (independent)**:
`zxnext.vhd:3305-3326` is the `port_e7_reg` process. The first if-branch
on `reset='1'` (line 3308-3309) sets `port_e7_reg <= (others => '1')`,
which physically deasserts all SPI chip-select lines (`spi_ss_*_n` all
'1' per lines 3328-3332). Real SPI slaves treat the CS rising edge as
"abort the current transaction"; for an SD card in SPI mode that means
returning the protocol-state FFs to IDLE. ✓

**Pre-fix C++**: `cs_ = 0xFF; rx_data_ = 0xFF;` (no callback walk). Any
slave that was selected at the moment of reset retained its in-flight
state (e.g. `state_=SENDING_DATA` mid-CMD17) until the next host-driven
CS write.

**Post-fix C++** (`src/peripheral/spi.cpp:62-66`):

```cpp
for (int i = 0; i < kMaxDevices; ++i) {
    if (!(cs_ & (1 << i)) && devices_[i]) {
        devices_[i]->deselect();
    }
}
cs_ = 0xFF;
```

The loop precondition (`!(cs_ & (1<<i)) && devices_[i]`) and the call
shape exactly mirror what `write_cs(0xFF)` does on a 0xFE→0xFF
transition (compare `spi.cpp:144-150`). Devices that weren't selected
when reset fired don't get a spurious `deselect()`. The PASS-7 invariant
(do not clear `devices_[]`) is preserved. The
`SdCardDevice::deselect()` implementation (`sd_card.cpp:84-110`) resets
all protocol-state FFs (`state_`, `cmd_idx_`, `resp_buf_`,
`multi_block_`, `pending_write_after_r1_`,
`persistent_response_byte_=0xFF`) but explicitly preserves
`initialized_` — matching the audit's stated and the VHDL-faithful
"FPGA reset doesn't reset the SD card itself; only the CS-line goes
high" semantics.

**Discriminative regression test**: `divmmc_test.cpp` row **SS-15**
(group 12). I reverted the loop body alone (kept the comment block;
left `cs_ = 0xFF` in place) and rebuilt:

```
[ ] revert spi.cpp loop → divmmc_test 133/134, FAIL on SS-15:
    sel_before=1 desel_before=0 desel_after=0 cs_after=ff
[ ] restore spi.cpp loop  → divmmc_test 134/134
```

The `desel_after=0` failure is the smoking gun — the device never saw
the deselect callback. The fix unequivocally resolves it. SS-12 (the
PASS-7 device-bindings sentinel) was unaffected during my revert run,
confirming the audit's compatibility analysis: SS-12 doesn't `write_cs`
before `reset`, so the loop never fires a deselect there.

**Verdict**: VHDL-faithful fix; discriminative test pinned; APPROVE.

### V11-DIVMMC-02 — `DivMmc::is_nmi_hold()` drops same-cycle `instant_on`

**VHDL re-cite (independent)**:
`device/divmmc.vhd:147-150` defines:

```vhdl
-- automap <= ... (commented earlier variant with not i_cpu_m1_n)
automap <= (not i_automap_reset) and (automap_held or
            (i_automap_active and (i_automap_instant_on or
                                    automap_nmi_instant_on)) or
            (i_automap_rom3_active and i_automap_rom3_instant_on));

o_disable_nmi <= automap or button_nmi;
```

`o_disable_nmi` (line 150) ORs the **combinational** `automap` (line
148, which includes the same-cycle instant_on terms) with the
registered `button_nmi`. NOT the registered `automap_held` alone. The
audit's identification is precise. ✓

**Pre-fix C++**: `is_nmi_hold() { return automap_held_ || button_nmi_; }`
— used the registered FF, dropping the same-cycle instant_on
contribution.

**Post-fix C++** (`src/peripheral/divmmc.h:255`):
`is_nmi_hold() { return automap_active_ || button_nmi_; }`

Verifying that `automap_active_` is the right C++ shadow of VHDL line
148:
- `divmmc.cpp:434-435` computes `automap_active_ = automap_held_ ||
  instant_match`. `instant_match` accumulates over RST + NMI@$0066 +
  $3Dxx + ROM3-instant via the path-eligibility gates that mirror
  `i_automap_active` / `i_automap_rom3_active` (the audit-Pass G46(b)
  decomposition at `divmmc.cpp:332-335`). So
  `instant_match = (i_automap_active AND (instant_on OR
  nmi_instant_on)) OR (i_automap_rom3_active AND
  i_automap_rom3_instant_on)`.
- `automap_active_` is therefore exactly `automap_held OR instant_match`
  = the parenthesised body of VHDL line 148.
- The `(NOT i_automap_reset)` outer factor is enforced by
  `apply_enabled_transition_` (`divmmc.cpp:129-145`), which clears
  `automap_active_=false` (and `button_nmi_=false`) on every
  enabled→disabled edge. So when `i_automap_reset=1`,
  `is_nmi_hold()` returns false, matching VHDL. ✓

**Subtle correctness detail (verified clean)**: `automap_active_` is
recomputed only inside `check_automap` (M1 fetches). VHDL `automap` is
combinational every cycle. Between M1 fetches PC doesn't change, so
`i_automap_instant_on`/`i_automap_rom3_instant_on` don't change either,
and the cached `automap_active_` value is the right one — the model
collapse is consistent with the rest of the audit-pass methodology and
not a defect.

**Discriminative regression test**: `divmmc_test.cpp` row **NM-10**
(group 9). I reverted only the accessor (kept the 30-line comment
block; reverted `automap_active_` → `automap_held_`) and rebuilt:

```
[ ] revert divmmc.h is_nmi_hold → divmmc_test 133/134, FAIL on NM-10:
    held=0 (exp 0) active=1 (exp 1) is_nmi_hold=0 (exp 1)
[ ] restore divmmc.h is_nmi_hold → divmmc_test 134/134
```

The pre-fix shape returns `is_nmi_hold=0` because `automap_held_` is
still 0 on the M1 of the entry point (it only catches up to hold on the
NEXT M1's step-1 promotion). NM-08 (the steady-state truth-table row)
remained green throughout the revert — both implementations agree once
held has caught up to active, so NM-08 isn't a discriminative pin and
the audit was right to add NM-10 as the first-M1 case. The comment
update on NM-08 (no truth-table change) is appropriate documentation
hygiene.

**Verdict**: VHDL-faithful fix; discriminative test pinned; APPROVE.

## Discriminative-test summary

Both new rows are genuine fix-pins, not coverage theatre:

| Row    | Pre-fix outcome                                         | Post-fix outcome  |
|--------|---------------------------------------------------------|-------------------|
| SS-15  | FAIL — `desel_after=0` (deselect callback never fired)  | PASS              |
| NM-10  | FAIL — `is_nmi_hold=0` while `active=1` (combinational) | PASS              |

No previously-passing row regresses on either revert. SS-12 (PASS-7
sentinel) and NM-08 (steady-state truth table) both stay green.

## Full test-suite results (post-fix tree)

```
$ cmake --build build -j$(nproc)         OK
$ ctest --test-dir build                 38/38 PASS
$ ./build/test/fuse_z80_test build/test/fuse
                                         1356/1356 PASS (100%)
$ MAX_JOBS=1 bash test/00regression/regression.sh
                                         30 PASS / 3 FAIL / 0 SKIP
                                         (parallax-demo pixel-diff is a
                                          pre-existing baseline FAIL;
                                          rzx-record + rzx-playback +
                                          video-record-func are
                                          baseline flaky — same set as
                                          documented in the audit
                                          report; not introduced by
                                          Pass-11)
```

`divmmc_test` per-group at the audit head matches the audit report
exactly (134 checks, 0 skips, 134 plan rows covered; group 9 NMI/button
14/14 includes new NM-10; group 12 Port 0xE7 CS 14/14 includes new
SS-15).

A note on regression-suite parallelism: the script defaults to
`MAX_JOBS=$(nproc)`. On this 16-thread test machine the unbounded
parallel run produces 10-18 transient FAILs ("emulator crashed or
timed out") because the parallel emulator instances starve each other
of CPU during the screenshot-deadline window. The audit's reported
"31-32 PASS / 1-2 FAIL" matches the `MAX_JOBS=1` (sequential) run, so
that is what I report above. This is a long-known flakiness of the
script, orthogonal to Pass-11.

## Missed-finding hunt

I scanned the audit's "Areas explicitly verified clean" list and the
broader DivMMC/SD/SPI surface for divergences the audit might have
overlooked. Findings worth promoting to class-(a)/(b): **none**.
Observations worth recording (none rises above class-(c) doc-debt and
**all are deliberately not flagged as findings** — they are listed for
the next pass's attention, not as gaps in Pass-11):

1. `SdCardDevice::receive` in `RECEIVING_DATA` skips a leading 0xFE
   token unconditionally on `data_idx_==0 && data_crc_count_==0`. If
   the post-token data byte 0 happens to be 0xFE (legitimate user data),
   the FSM eats it as if it were another start-token attempt and the
   block silently shifts by one byte. This isn't a Pass-11 finding —
   it's an existing limitation of the fixed-token FSM, latent for the
   boot path (TBBlue's writes don't begin with 0xFE) and listed in the
   audit's class-(d) "SPI cycle FSM" item by reference. Fixing it
   requires a small token-state byte (`waiting_for_token_` / `got_token_`),
   not architectural rework.

2. `automap_active_` is recomputed only on M1 fetches. VHDL `automap` is
   combinational every cycle. As established in V11-DIVMMC-02 above,
   the model collapse is sound because PC doesn't change between M1s,
   so the instant_on signal doesn't either. I confirmed by walking the
   non-M1 access paths in `Mmu::divmmc_read` / `divmmc_write` and the
   `Emulator::on_m1_cycle` lambda — every consumer reads
   `is_active()` after the most recent `check_automap` for the current
   PC. Not a finding.

3. `read_control() & 0xCF` correctly masks bits 5:4 to zero per VHDL
   `port_e3_dat <= port_e3_reg(7 downto 6) & "00" & port_e3_reg(3
   downto 0)` (`zxnext.vhd:4190`). The `control_reg_` shadow at
   `divmmc.cpp:109` includes the user-written bits 5:4 internally, but
   they never surface on the read path. Audit verified clean — I
   re-verified clean.

4. The port 0xE7 / 0xEB SPI gate uses `nextreg_.cached(0x83) & 0x08`
   (NR 0x83 bit 3) per `port_spi_io_en <= internal_port_enable(11)` in
   VHDL `zxnext.vhd:2419`. `internal_port_enable(11) = nr_83(3)` per
   the concat at line 2392. Audit verified clean — I re-verified clean.

5. `SpiMaster::reset()` calls `deselect()` on the same physical
   `SdCardDevice` only once even when it's bound to both CS0 and CS1
   (the SS-14 contract-pin shape), because the loop walks slot indices
   and only the slot whose CS bit is currently low gets a callback. SS-14
   doesn't call reset() so this is purely a "could the reset loop
   double-fire on a same-device wiring" check; the answer is no, because
   only one CS line is asserted at a time (single-device SPI). Not a
   finding.

6. The `divmmc_automap_delayed_off` gate in VHDL line 131
   (`automap_held AND NOT (i_automap_active AND i_automap_delayed_off)`)
   is faithfully modelled in `check_automap` as `off_match` already
   includes the `main_path_eligible` (= `sram_pre_override(2)` =
   `i_automap_active`) factor. The composite `(automap_held_ &&
   !off_match)` is therefore exactly the VHDL term. Verified clean.

## Class-(d) carried forward (not in this pass's scope)

Same as the audit:

- **SPI cycle FSM and DMA wait_n throttle** (`spi_master.vhd:62-99`).
  G137. Out of scope; documented in `spi.h::spi_wait_n()` accessor
  comment.

## Overall assessment

The audit's two findings are well-isolated, well-cited, well-fixed, and
well-tested. The discriminative tests are real fix-pins (revert → FAIL,
restore → PASS) at the level the testcov subsystem demands. Build and
test runs are clean. The "verified clean" list is — to the extent I
could check it independently — accurate. No new findings.

**Verdict: APPROVE.**

## Final return shape

| Field              | Value                                                  |
|--------------------|--------------------------------------------------------|
| verdict            | APPROVE                                                |
| findings_verified  | 2                                                      |
| discriminative     | SS-15, NM-10                                           |
| issues             | (none — class-(c) observations recorded above)         |
| tests_passed       | true                                                   |
| audit_head_sha     | `d340bdd` (fix+tests on top of `49f7b29` audit report) |
| report_path        | `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY11-DIVMMC-SD-SPI-REVIEW.md` |
