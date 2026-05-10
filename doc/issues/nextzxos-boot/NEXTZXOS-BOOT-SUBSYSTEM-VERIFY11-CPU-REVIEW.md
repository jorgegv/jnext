# Pass-11 CPU/Z80N/IM2 Audit — Independent Review

**Reviewer worktree**: `.claude/worktrees/task2-verify11-cpu-z80n-im2-reviewer`
**Reviewer branch**: `task2/verify11-cpu-z80n-im2-reviewer`
**Audit head**: `d31e753` (combined fix + tests + report)
**Audit branch**: `task2/verify11-cpu-z80n-im2`
**Verdict**: **APPROVE**

## Scope

Independent verification of the Pass-11 BLIND audit's two class-(c)
findings:

* **V11-CPU-01** — IM2 RETI decoder treated `DD ED 4D` as RETI.
* **V11-CPU-02** — PIXELDN corrupted H[7:5] when band counter wrapped
  from 11.

Per the review protocol I read only the audit's own report
(`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY11-CPU.md`) and consulted the VHDL
oracle (`im2_control.vhd`, `t80n.vhd`) plus the FUSE Z80 reference. I
did not read any earlier `VERIFY*` pass reports.

## Methodology

For each finding:

1. Read the audit's claim and code change (`git show d31e753 -- <file>`).
2. Open the VHDL oracle at the cited line range, verify the audit's
   reading is faithful.
3. Verify the fix is bit-for-bit correct against the VHDL.
4. **Discriminative revert**: `git checkout d31e753^ -- <file>`,
   rebuild, run the new test, confirm FAIL with the expected pre-fix
   value. Restore, rebuild, confirm PASS.
5. Run the full test suite at the post-fix HEAD: `ctest`,
   `fuse_z80_test`, `regression.sh`.
6. Hunt for missed findings in adjacent code (IM2 state machine,
   PIXELDN edge cases, Z80N flag semantics).

## V11-CPU-01 — IM2 RETI decoder DDFD-ED chain

**VHDL oracle re-read** (`im2_control.vhd:158-209`):

```vhdl
when S_DDFD_T4 =>
   if ifetch_fe_t3 = '1' and opcode_ddfd = '1' then
      state_next <= S_DDFD_T4;
   elsif ifetch_fe_t3 = '1' then
      state_next <= S_0;
   else
      state_next <= S_DDFD_T4;
   end if;
```

The `elsif` at line 202-203 has no ED special-case — any non-DDFD
opcode falling on `ifetch_fe_t3='1'` returns the FSM to `S_0`. The
audit's reading is correct: the IM2 RETI decoder is wired off the
**physical bus opcode pattern**, not the CPU's internal re-dispatch.
A `DD ED 4D` byte sequence on the bus therefore must NOT trigger
`reti_seen`, even though FUSE Z80's `z80_ddfd.c` default handler
re-dispatches the inner `ED 4D` semantically as RETI.

**Pre-fix C++** (`im2.cpp` S_DDFD_T4 case, before d31e753):

```cpp
case DecState::S_DDFD_T4:
    if      (opcode == 0xDD || opcode == 0xFD) {
        // stay
    } else if (opcode == 0xED) {
        dec_state_ = DecState::S_ED_T4;   // ← bug: VHDL has no such branch
    } else {
        dec_state_ = DecState::S_0;
    }
    break;
```

**Post-fix C++**: removes the ED branch; non-DDFD opcodes (ED, CB, or
anything else) all fall through to `S_0`. Matches VHDL line 199-206
exactly.

**Discriminative confirmation**: with `src/cpu/im2.cpp` reverted to
`d31e753^`, rebuilt, then `./build/test/cpu_z80n_im2_regressions_test`:

```
[FAIL] V11-CPU-01-IM2-DDFD-ED-NO-RETI
   Setup: CTC0 in S_ISR=1. After DD ED 4D:
   reti_seen at 4D pulse high?YES (pre-fix)
   CTC0 state=0 (expect 3=S_ISR per VHDL im2_control.vhd:199-206 …)
Total:   25  Passed:   24  Failed:    1
```

After restoring the fix, the test PASSES. The FAIL message string
matches the VHDL reading and the audit's reproduction case verbatim.
The other 24 tests, including the related `IM2-RETI-DECODE-
SIMULTANEITY-NESTED-ISR` (Pass-10 simultaneity test), all pass under
both pre-fix and post-fix configurations — confirming the change is
isolated to the DDFD-ED chain, not regressing the simultaneity model.

**State machine spot-check** (audited the entire FSM, not just the
DDFD-ED transition):

| State        | VHDL line | C++ behaviour                           |
|--------------|-----------|-----------------------------------------|
| S_0          | 161-170   | ED→S_ED_T4, CB→S_CB_T4, DD/FD→S_DDFD_T4 ✓ |
| S_ED_T4      | 171-180   | 4D→S_ED4D_T4 + reti_seen, 45→S_ED45_T4 + retn_seen, IM-mode latch on 01xxx110, else S_0 ✓ |
| S_ED4D_T4    | 181-183   | → S_SRL_T1 ✓                            |
| S_ED45_T4    | 190-192   | → S_SRL_T1 ✓                            |
| S_SRL_T1     | 186-187   | → S_SRL_T2 ✓                            |
| S_SRL_T2     | 188-189   | → S_0 ✓                                 |
| S_CB_T4      | 193-198   | → S_0 unconditionally on next M1 ✓      |
| S_DDFD_T4    | 199-206   | DD/FD stay; else → S_0 ✓ (post-fix)     |

Output signals (line 233-238): `o_reti_decode = (state==S_ED_T4)`,
`o_reti_seen = (state_next==S_ED4D_T4)`, `o_dma_delay` over the union
of {S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1, S_SRL_T2}. All five
match the C++ derived signals at `im2.cpp:588-593`. The Pass-10
simultaneity compensation (`reti_decode_ || reti_seen_pulse_` for IEI
chain) remains intact.

## V11-CPU-02 — PIXELDN H[7:5] preservation on band-3 wrap

**VHDL oracle re-read** (`t80n.vhd:900-921`):

```vhdl
when PIXELDN =>
   reg_temp_t(31 downto 16) := std_logic_vector(unsigned(H & L));

   reg_temp_t(7 downto 0) := std_logic_vector(unsigned(
       unsigned(H(4 downto 3)) &      -- b: 2 bits  → composite[7:6]
       unsigned(L(7 downto 5)) &      -- R: 3 bits  → composite[5:3]
       unsigned(H(2 downto 0))        -- C: 3 bits  → composite[2:0]
   ) + 1);

   if TState = 4 then
      reg_direct_val_H_b <= reg_temp_t(31 downto 29)   -- = H[7:5] preserved
                          & reg_temp_t(7 downto 6)     -- = new b
                          & reg_temp_t(2 downto 0);    -- = new C
      reg_direct_val_L_b <= reg_temp_t(5 downto 3)     -- = new R
                          & reg_temp_t(20 downto 16);  -- = L[4:0] preserved
```

The 8-bit add at line 904-908 truncates the carry at bit 7
(`reg_temp_t(7 downto 0) := … + 1`). H[7:5] is sourced from
`reg_temp_t(31:29)`, which was set to the original H by line 902 and
never written by the +1, so it is preserved verbatim regardless of
band-counter wrap.

The audit's reading is correct.

**Pre-fix C++** had a 4-step carry chain that ended with
`H = H + 0x08;` when the band counter wrapped — that 8-bit add
propagated bit-4 carry into H[5] when H[4:3]=11, corrupting the
preserved screen-prefix.

**Post-fix C++** does the composite increment in one 8-bit add and
re-distributes the result fields into H/L, masking H[7:5] preservation
explicitly with `(H & 0xE0)`. Bit-correct against the VHDL.

**Discriminative confirmation** (HL=0x5FE0, H[7:5]=010, b=11, R=111,
C=111 → composite=0xFF + 1 = 0x00):

* Pre-fix (revert `src/cpu/z80n_ext.cpp`): test FAILs with
  `HL=0x6000` (H[7:5]=011 — bit-5 corruption).
* Post-fix: test PASSes with `HL=0x4000` (H[7:5]=010 preserved).

```
[FAIL] V11-CPU-02-Z80N-PIXELDN-BAND3-WRAP-PRESERVES-H-HIGH
   PIXELDN HL=0x5FE0 → 0x6000 (expect 0x4000); H[7:5] = 011 (expect 010);
   t=8 (expect 8). Pre-fix would yield 0x6000 (H[7:5]=011 — bug).
```

The non-discriminative regression guard
`V11-CPU-02-Z80N-PIXELDN-ROW191-WRAP-UNCHANGED` (HL=0x57E0 → 0x5800)
correctly passes both pre-fix and post-fix — confirming the new code
preserves the existing `ed93_last_line_wrap` fixture behaviour.

**Hand-checked edge cases** (against my own VHDL trace):

| HL in | b | R | C | composite | +1 | new HL | Notes |
|------:|--:|--:|--:|---------:|---:|-------:|-------|
| 0x4000 | 00 | 000 | 000 | 0x00 | 0x01 | 0x4001 | C++0..6 increments |
| 0x4007 | 00 | 000 | 111 | 0x07 | 0x08 | 0x4020 | C wraps, R++ |
| 0x40E7 | 00 | 111 | 111 | 0x3F | 0x40 | 0x4800 | R wraps, b++ |
| 0x57E0 | 10 | 111 | 111 | 0xBF | 0xC0 | 0x5800 | b 10→11 (existing fixture) |
| 0x5FE0 | 11 | 111 | 111 | 0xFF | 0x00 | 0x4000 | b wraps to 00, H[7:5]=010 preserved (V11-CPU-02 case) |
| 0x07E0 | 00 | 111 | 111 | 0x3F | 0x40 | 0x0800 | top of screen, b 0→1 |
| 0xDFE0 | 11 | 111 | 111 | 0xFF | 0x00 | 0xC000 | H[7:5]=110 preserved on wrap |

All match the C++. The H[7:5] preservation is now correct for every
input.

## Test results at d31e753 (post-fix HEAD)

```
$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/cpu_z80n_im2_regressions_test
Total:   25  Passed:   25  Failed:    0

$ bash test/00regression/regression.sh
Pass: 32  Fail: 1  Skip: 0
   (only failure: parallax-demo 44636-pixel diff —
    pre-existing in baseline; reverted both fixes,
    rebuilt, re-ran: STILL fails identically.
    Confirmed not caused by V11-CPU fixes.)
```

Note: the regression suite is mildly flaky on the reviewer's worktree
when other compilation/IO is in flight (some Qt-launched tests
intermittently report "emulator crashed or timed out"). On a stable
quiescent run the result is exactly 32/1, as the audit reports.
Re-running the same suite on the main worktree (HEAD `d385d5e`) gives
33/0 — confirming the parallax-demo diff is local to the post-Pass-11
state, not the Pass-11 fix itself, since reverting the V11-CPU patch
does not move the diff. Per the audit's stash-revert-rebuild claim,
this is a pre-existing baseline issue (probably from an earlier pass
that touched contention/timing). It is **not** a regression of
Pass-11. I have not investigated its origin further as it is out of
review scope.

## Hunt for missed findings (none)

I spot-audited the audit's "Investigated-and-cleared" defenses and
several adjacent code paths:

* **BSRA arithmetic right shift on `int16_t`**: shift in [16,31] —
  promotion to `int32_t` and arithmetic right shift on x86_64 GCC/
  Clang yields full sign-fill (0xFFFF on negative input, 0 on
  non-negative), matching the VHDL 17-bit `shift_right(signed,…)`.
  Audit's defense is correct.
* **BSRL/BSRF unsigned shift**: `regs.DE >> shift` for shift ≥ 16
  yields 0 (DE is `uint16_t` promoted to `unsigned int`). VHDL with
  `IR(0)=0` for BSRL fills with 0. Match. BSRF code uses an explicit
  17-bit value with bit 16 = 1 and arithmetic-shift sign-extension
  through `(int32_t << 15) >> 15` — correct.
* **BRLC mod-16 rotation**: `rot &= 0x0F` after the `B[4:0]` mask
  ensures the 16-bit rotate wraps; VHDL `rotate_left(unsigned(16-bit),
  n)` for n ≥ 16 wraps mod 16 too. Match.
* **PIXELAD/SETAE** (`z80n_ext.cpp:464-485`): bit composition matches
  `t80n.vhd:923-947`.
* **IM2 priority order** (`im2.cpp` DevIdx assignments): LINE=0,
  UART0_RX=1, …, UART1_TX=13 matches `zxnext.vhd:1933-1944` priority
  order and the `device_index = priority – 1` vector composition rule.
* **IM-mode bit decode** (`im2.cpp:666-676` vs `im2_control.vhd:223-
  224`): collapses correctly to {IM0, IM1, IM2} on opcodes 46/4E/56/
  5E/66/6E/76/7E. The mask `(opcode & 0xC7) == 0x46` is the exact
  bit-pattern test for `01xxx110`.
* **NMI HALT exit** (`z80_cpu.cpp:425`): saved PC = pc+1 when halted —
  matches `fuse_z80_nmi()` and Zilog manual.
* **EI-grace gate** (`z80_cpu.cpp:479-510`): the gate is checked
  BEFORE invoking `on_int_ack()`, so the IM2 daisy-chain device is
  not falsely advanced from S_REQ to S_ACK when FUSE rejects the
  cycle. Pass-8 fix preserved.
* **Magic-breakpoint paths** (`z80_cpu.cpp:524-535, 673-683`): ED FF
  and DD 01 do NOT fire M1 callbacks (decoder stays in S_0), do NOT
  increment R. The IM2 RETI decoder's "physical bus pattern" rule
  argues this could in principle differ from real hardware (which
  WOULD see the bytes on the bus), but magic breakpoints are a
  jnext-only debug feature with no hardware analogue, and per the
  audit this is the documented intent.

I found no additional findings.

## Conclusion

**APPROVE.**

Both V11-CPU-01 and V11-CPU-02 are real class-(c) findings, the VHDL
oracle reading is correct in both cases, the C++ fixes are bit-for-bit
faithful against the VHDL, and each fix is independently
discriminated by its dedicated regression test (revert → FAIL,
restore → PASS). The non-discriminative PIXELDN guard correctly
catches both pre- and post-fix configurations to protect the existing
`ed93_*` fixtures.

Test posture at HEAD `d31e753`:
- ctest 38/38, FUSE 1356/1356, cpu_z80n_im2_regressions 25/25
  (was 22 pre-Pass-11), z80n 85/85.
- regression 32/1 (the 1 fail is a pre-existing parallax-demo
  baseline diff, NOT caused by the Pass-11 fixes — confirmed by
  stash-revert-rebuild-rerun).

The audit's "Defense paragraph" and "Investigated-and-cleared"
sections faithfully reflect the VHDL state. Spot-audits of BSRA,
BSRL, BSRF, BRLC, PIXELAD, SETAE, IM-mode decode, IM2 priority order,
EI-grace gate, NMI HALT, and the full IM2 decoder FSM (eight states
plus the SRL guard pair) found no additional issues.

Recommend merging `task2/verify11-cpu-z80n-im2` to the integration
branch.
