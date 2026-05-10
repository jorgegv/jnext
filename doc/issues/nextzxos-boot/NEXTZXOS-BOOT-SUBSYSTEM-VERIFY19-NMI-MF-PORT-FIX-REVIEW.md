# Pass-19 NMI + MF + Port + NextREG — Fix-of-Reviewer Independent Review

**Subject:** Verification of fix-of-reviewer commits `e98cc2b` (doc-only,
NIT-01 + NIT-02) and `015c2cf` (source + tests, NIT-03 + NIT-04) against
the Pass-19 reviewer (`4f007b3`) APPROVE-WITH-NITS verdict.

**Reviewer:** Independent fix-reviewer agent (this document).

**Source under review:** `task2/verify19-nmi-mf-port-fix-review` worktree,
HEAD `015c2cf`.

**Verdict: APPROVE.**

The two source-level NITs (V19R-NMP-NIT-03 NR 0xF0 XADC composed-read stub
and V19R-NMP-NIT-04 NR 0xF8 bit-7 mask) are VHDL-faithful, the
discriminative tests are genuinely discriminative (pre-fix FAIL with
exactly the leaked cache values, post-fix PASS), and no pre-existing test
or invariant regresses. Two adjacent XADC NRs (NR 0xF9 / NR 0xFA) exhibit
the same family of cache-fall-through leak as NR 0xF0 and were not
addressed by the V19R fix; this is recorded as a side-effect observation
for the next pass rather than a reject reason for the current fix-of-
reviewer commit (the original Pass-19 reviewer did not flag them, and the
fix faithfully implements what the reviewer asked for).

---

## Methodology

1. Read `git show 015c2cf` (source + tests) and `git show e98cc2b` (doc-only)
   in full.
2. Cross-referenced every VHDL citation against
   `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
   lines 6273-6278 (read mux), 7420-7436 (Issue 2/3 hard-wires),
   7438-7484 (Issue 4/5 nr_f0_xdev_cmd composer), 7553-7558 (NR 0xF8 write
   path).
3. Confirmed `g_board_issue` modeling in jnext: NR 0x0F is seeded to 0x00
   in `src/port/nextreg.cpp` (Issue 2). The Issue 2 VHDL path therefore
   applies to nr_f0_xdev_cmd (= constant 0) and to nr_f8_xadc_daddr
   (= constant 0).
4. Built Release. Ran the standard sandwich on `src/core/emulator.cpp`
   only (kept tests at HEAD): both V19R-NMP-NIT-03 and V19R-NMP-NIT-04
   FAIL pre-fix with exactly the cache-leak values, PASS post-fix.
5. Ran full Release invariants: `ctest --output-on-failure` 38/38,
   `fuse_z80_test` 1356/1356, `nextreg_integration_test` 259/259.
6. Searched for adjacent XADC NRs (NR 0xF9, NR 0xFA) and the absence of
   read-handlers there to assess whether the fix scope is complete.

---

## VHDL faithfulness — per-NIT verification

### V19R-NMP-NIT-03 — NR 0xF0 XADC composed-read stub

**VHDL read mux** (`zxnext.vhd:6273-6275`):

```vhdl
when X"F0" =>
   port_253b_dat <= nr_f0_xdev_cmd;
```

**VHDL driver — Issue 2/3** (`zxnext.vhd:7420-7436`):

```vhdl
gen_xper_23: if (g_board_issue <= 1) generate
   nr_f0_xdev_cmd <= (others => '0');
   ...
end generate;
```

**VHDL driver — Issue 4/5** (`zxnext.vhd:7438-7484`):
`nr_f0_xdev_cmd` is a clocked register driven from a 4-way mux of
{`xdev_cmd`-select bit, XDNA-DO, XADC-busy/eoc/eos, all-zero}. None of the
mux paths simply latch the previously written byte.

**jnext's modeled board:** Issue 2 (NR 0x0F seed = 0x00 in nextreg.cpp).
VHDL-faithful read = constant 0x00.

**Fix** (`emulator.cpp:2096-2098`):

```cpp
nextreg_.set_read_handler(0xF0, []() -> uint8_t {
    return 0x00;
});
```

VHDL-faithful for the modeled board issue. ✓

**Caveat (documented in commit msg, not a finding):** if jnext is later
retargeted to Issue 4/5, NR 0xF0 read must compose `i_XADC_BUSY |
nr_f0_xadc_eoc | nr_f0_xadc_eos` from a modeled XADC. There is currently
no `g_board_issue` analog gating the stub — the lambda hard-wires 0x00.
Acceptable today (jnext is Issue 2). A `TODO` pointing to the Issue-4/5
composer would be defensible but is not required.

### V19R-NMP-NIT-04 — NR 0xF8 bit-7 mask

**VHDL read mux** (`zxnext.vhd:6277-6278`):

```vhdl
when X"F8" =>
   port_253b_dat <= '0' & nr_f8_xadc_daddr;
```

Bit 7 is hard-wired to '0' on the read mux **regardless of board issue**.

**VHDL write path — Issue 2/3** (`zxnext.vhd:7426`):
`nr_f8_xadc_daddr <= (others => '0')` — writes have no effect; the
register is always 0. Issue-2 VHDL-faithful read = 0x00 always.

**VHDL write path — Issue 4/5** (`zxnext.vhd:7550-7558`):

```vhdl
if nr_f8_we = '1' then
   nr_f8_xadc_dwe   <= nr_wr_dat(7);
   nr_f8_xadc_daddr <= nr_wr_dat(6 downto 0);
end if;
```

Bit 7 is split off into a separate latch (`nr_f8_xadc_dwe`); only bits
6:0 land in `nr_f8_xadc_daddr`. Read mux still surfaces only the daddr
field with bit 7 forced to 0.

**Fix** (`emulator.cpp:2122-2124`):

```cpp
nextreg_.set_read_handler(0xF8, [this]() -> uint8_t {
    return static_cast<uint8_t>(nextreg_.cached(0xF8) & 0x7F);
});
```

This is **Issue-4/5 faithful** (bit 7 always cleared, bits 6:0 reflect
last write). It is **not strictly Issue-2 faithful** (Issue 2 returns
0x00 always; the stub leaks bits 6:0 of the cached write byte). The
commit message acknowledges this trade-off explicitly as a "conservative-
but-Issue-4/5-aware" choice. The discriminative test (`write 0xC5 → read
0x45`) accepts the looser semantics.

**Reviewer judgement:** Acceptable. Two reasons:

1. The XADC block is not modeled and never exercised by NextZXOS boot or
   any user-relevant software. The leaked bits 6:0 are inert.
2. The conservative variant survives a future retarget to Issue 4/5
   without code change, whereas a strict `return 0x00` would silently
   break Issue 4/5 daddr readback semantics.

A strict-Issue-2 alternative (`return 0x00`) would be equally defensible.
The current choice is documented and self-consistent. ✓

---

## Discriminative-test sandwich

Build: Release, `cmake -B build -DCMAKE_BUILD_TYPE=Release` then
`cmake --build build -j$(nproc)`.

| Stage | nextreg total | V19R group | Notes |
|-------|---------------|------------|-------|
| Baseline (HEAD `015c2cf`) | 259 / 259 | 2 / 2 | All tests PASS. |
| Source reverted to `4f007b3` (tests kept at HEAD) | 257 / 259 | 0 / 2 | V19R-NMP-NIT-03 FAIL `got=0xAA expected=0x00`; V19R-NMP-NIT-04 FAIL `got=0xC5 expected=0x45`. Both leak the exact cached write byte — confirms the cache-fall-through bug they target. |
| Source restored to `015c2cf` | 259 / 259 | 2 / 2 | All tests PASS again. |

Both tests are genuinely discriminative: they exercise a write of a
non-trivial byte (0xAA / 0xC5 with bit 7 set), then verify the read
returns the VHDL-faithful value (0x00 / 0x45) and not the leaked cache.
The pre-fix failure values are the cached write bytes verbatim, ruling
out any other cause.

---

## Test invariants (Release)

| Suite | Result |
|-------|--------|
| `ctest --output-on-failure` | 38 / 38 PASS |
| `./build/test/fuse_z80_test build/test/fuse` | 1356 / 1356 PASS |
| `./build/test/nextreg_integration_test` | 259 / 259 PASS (group `V19R-NMP-XADC-Read-Stubs` 2/2) |

All invariants hold.

---

## Side-effect inspection

### Pre-existing NR 0xF0 / NR 0xF8 tests

`grep -n '0xF0\|0xF8' test/nextreg/nextreg_integration_test.cpp` shows
no pre-existing test that writes a value to NR 0xF0 or NR 0xF8 and
expects to read it back unmodified. The hits at lines 3669-3673 and
4315/4373 are unrelated bit-mask assertions on other registers
(`& 0xF8`, `& 0xF0` slot masks). Therefore the new read-handler stubs
cannot break any pre-existing assertion. ✓

### Adjacent XADC NRs — observation, not reject reason

The XADC family in the read mux (`zxnext.vhd:6273-6284`) covers four
addresses: NR 0xF0, NR 0xF8, **NR 0xF9** (`nr_f9_xadc_d0`), and
**NR 0xFA** (`nr_fa_xadc_d1`). For Issue 2/3
(`zxnext.vhd:7428-7429`):

```vhdl
nr_f9_xadc_d0 <= (others => '0');
nr_fa_xadc_d1 <= (others => '0');
```

Both are hard-wired to all-zero on Issue 2 (= jnext). For Issue 4/5
(`:7571-7591`) they are written by software OR latched from `i_XADC_DO`,
so the read can be either the previously written byte or the live ADC
sample.

jnext currently has **no read handler for NR 0xF9 or NR 0xFA**
(verified by `grep "set_read_handler" src/core/emulator.cpp`). Writes
fall through to the bare `regs_[reg]` cache and reads return the cached
write byte verbatim. Per VHDL Issue 2, both should return 0x00 always
— this is the **same family of bug** as V19R-NMP-NIT-03 (NR 0xF0).

This is **not a reject reason** for the current fix:

- The Pass-19 reviewer (`4f007b3`) did not flag NR 0xF9 / NR 0xFA. The
  fix-of-reviewer commit (`015c2cf`) faithfully implements what the
  reviewer asked for and nothing more, which is the correct scope for a
  fix-of-reviewer commit.
- Both registers belong to the unmodeled XADC block. Like NIT-03 they
  are inert (Class-(c)) divergences — no software exercises them on the
  NextZXOS boot path or any user-relevant code.

**Recommendation for the next pass:** add NR 0xF9 / NR 0xFA stubs
returning 0x00 (Issue-2 strict) or `regs_[reg]` (Issue-4/5 conservative)
to close the family. Suggested follow-up identifier: **V20-NMP-XADC-04**
(or the Pass-20 audit's first XADC finding). I do not require it for
APPROVE because closing the family was outside the scope of the original
Pass-19 review.

### NR 0xFB — confirmed absent from VHDL XADC read mux

`zxnext.vhd:6280-6287` ends the XADC entries at NR 0xFA; NR 0xFB falls
into the `when others => port_253b_dat <= (others => '0')` default. So
NR 0xFB is correctly handled by the bare cache **only if** the cache is
also zero. jnext does not seed NR 0xFB and does not register a write
handler for it; the cache is therefore zero at reset and reads return 0
until software writes — at which point the leak begins. This is the
generic "fall-through-after-write" issue that V19R-NMP-NIT-02 (doc-only
recount) explicitly says is acceptable when the VHDL `when others`
default applies AND the register is not in the write decoder. NR 0xFB is
**absent from the VHDL write decoder** at `zxnext.vhd:4860-5870`
(verified by no `nr_register = X"FB"` match in the assignment block), so
writes to it are no-ops in VHDL — meaning jnext's cache-after-write IS
a (mild) divergence, but it falls in the same Class-(c) inert family
already documented. Same recommendation: close in the next pass.

### `g_board_issue` model in jnext

Confirmed by inspection of `src/port/nextreg.cpp` (NR 0x0F seeded to
0x00) and the absence of any `g_board_issue` configuration knob.
jnext = Issue 2 always today. The NIT-03 stub is therefore correct
unconditionally. The NIT-04 stub is not strictly Issue-2 faithful (per
above) but is conservative.

### Doc-only commit `e98cc2b` (NIT-01 + NIT-02)

Spot-checked. The four added Panel-2 rows (idx 20-23 = NR 0x84 b4-b7)
correspond to existing gate sites in `emulator.cpp:3474, 3552, 3582,
3615` — verified the call sites apply the correct `(nr84 & 0xXX)` mask.
The Panel-3 recount from "103 distinct (74 R + 97 W) / 153 fall-through"
to "126 distinct (82 R + 115 W) / 130 fall-through" credibly accounts
for the loop-generated handlers (NR 0x35-0x39 W loop, NR 0x50-0x57 R+W
loop, NR 0x75-0x79 W loop). The deltas (23 distinct, 8 R, 18 W, 23 fall-
through) are consistent with the loop bodies. I did not run an
exhaustive recount; the NIT-02 substantive claim ("the 130 fall-through
NRs are VHDL-correct modulo the 2 inert XADC NIT-03/04 cases, since
fixed") is unchanged and is the load-bearing assertion.

Per workflow rule `feedback_task2_skip_review_comment_only.md`, doc-only
commits skip the fix-reviewer cycle. Confirming I am NOT re-reviewing
`e98cc2b` substantively — it is included only for narrative context.

---

## Summary

**APPROVE.** All four NITs from the Pass-19 reviewer (`4f007b3`) are
addressed:

- NIT-01 (Panel 2 missing rows): doc-only, applied at `e98cc2b`.
- NIT-02 (Panel 3 enumeration): doc-only, applied at `e98cc2b`.
- NIT-03 (NR 0xF0 stub): source + test at `015c2cf`. VHDL-faithful for
  Issue 2 (jnext's modeled board). Discriminative test sandwich passes.
- NIT-04 (NR 0xF8 bit-7 mask): source + test at `015c2cf`. Issue-4/5
  faithful, conservative for Issue 2. Discriminative test sandwich
  passes.

**No regressions:** ctest 38/38, FUSE 1356/1356, nextreg 259/259.

**Observation for the next pass:** NR 0xF9 and NR 0xFA exhibit the same
cache-fall-through leak family as NR 0xF0 (NIT-03) and were not
addressed by the V19R fix. They should be closed in a subsequent pass.
This is **not** a reject reason: the fix-of-reviewer commit faithfully
implements the Pass-19 reviewer's asks, and the original review missed
the adjacent rows. Treating it as Pass-20 follow-up is cleaner than
expanding the scope of a fix-of-reviewer commit retroactively.

---

## Final HEAD

After this review commit: see commit log for `task2/verify19-nmi-mf-port-fix-review`.

The fix commit (`015c2cf`) is **NOT modified** by this review.
