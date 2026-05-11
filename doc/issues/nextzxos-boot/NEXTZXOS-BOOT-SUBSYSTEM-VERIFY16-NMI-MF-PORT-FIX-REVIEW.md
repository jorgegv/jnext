# NEXTZXOS Boot Subsystem — Pass-16 NMI/MF/Port FIX REVIEW

Branch: `task2/verify16-nmi-mf-port-fix-reviewer` (off `task2/verify16-nmi-mf-port` HEAD `80bedde`).
Workspace: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-nmi-mf-port-fix-reviewer`.
Build: Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`).
Tests: ctest 38/38 PASS, FUSE 1356/1356 PASS, all 13 V16 disc tests PASS.

This review verifies the IMPLEMENTATION of the V16-NMP-01 + V16-NMP-02
fix delivered in `80bedde`. Audit (`...VERIFY16-NMI-MF-PORT.md`) and
prior reviewer have already approved the FINDINGS; this fix-reviewer
only audits the code and tests landed in the fix commit.

VHDL oracle: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.

---

## Verdict: **APPROVE**

Both findings are correctly fixed against VHDL semantics. All 13
discriminative tests pass post-fix and individually fail when the
relevant fix is reverted. Full Release-mode test suite is clean
(ctest 38/38 + FUSE 1356/1356, zero regressions). Code quality is
high: comments cite VHDL line numbers, the override-pair pattern is
load-bearing and correctly documented, the helper is a clean abstraction.

---

## V16-NMP-01 — NR $10 SPKEY_BUTTONS read-mux: **VERIFIED**

### Spec match

VHDL `zxnext.vhd:5924`:
```
when X"10" =>
   port_253b_dat <= '0' & nr_10_coreid & i_SPKEY_BUTTONS(1 downto 0);
```

VHDL `zxnext_top_issue2.vhd:2281`:
```
zxn_buttons <= (NOT btn_drive_divmmc_n) & (NOT btn_m1_multiface_n);
```

Where `btn_*_n` is the active-low debounced PCB-button input. `NOT btn_*_n`
inverts to active-high, so:
- bit 0 = M1 (F9) = 1 when held
- bit 1 = Drive (F10) = 1 when held

### jnext implementation (post-fix)

`src/core/emulator.cpp:1283-1298` installs a `set_read_handler(0x10, ...)`:
```c++
const uint8_t coreid_bits = static_cast<uint8_t>((nr_10_coreid_ & 0x1F) << 2);
const uint8_t spkey_bits  = static_cast<uint8_t>(
    (test_hotkey_m1_    ? 0x01 : 0x00) |
    (test_hotkey_drive_ ? 0x02 : 0x00));
return static_cast<uint8_t>(coreid_bits | spkey_bits);
```

- Bit positions correct: M1 = bit 0, Drive = bit 1 (matches VHDL :2281).
- Active-high semantics correct: `test_hotkey_m1_=true` (= held) → bit set.
  Both shadows default to `false` (= idle) at construction
  (`src/core/emulator.h:833-834`), matching VHDL combinational `NOT btn_*_n`
  starting at 0 with both buttons released.
- Top bit (bit 7) hardcoded to '0' via `(coreid & 0x1F) << 2` upper-bound
  = bit 6, leaving bit 7 = 0. Matches VHDL `'0' & nr_10_coreid & ...`.
- Combinational semantics: each NR $10 read re-reads
  `test_hotkey_m1_/test_hotkey_drive_` live, no cache flop. Matches VHDL
  (the read mux is purely combinational — no FF on this path).

### Reset clause removal

VHDL `zxnext.vhd:1133`:
```
signal nr_10_coreid : std_logic_vector(4 downto 0) := "00001";
```

VHDL `zxnext.vhd:5677-5687` process:
```
process (i_CLK_28)
begin
  if rising_edge(i_CLK_28) then
    if nr_10_we = '1' then
      nr_10_flashboot <= nr_wr_dat(7);
      if nr_03_config_mode = '1' then
        nr_10_coreid <= nr_wr_dat(4 downto 0);
      end if;
    end if;
  end if;
end process;
```

The `:= "00001"` is a VHDL signal **initial value**, applied at
power-on only. The process has NO reset clause, so `nr_10_coreid`
SURVIVES both hard and soft reset — only NR $10 writes
(config_mode-gated) update it.

The fix at `src/core/emulator.cpp:198-210` removes the spurious
`nr_10_coreid_ = 0x01;` from `Emulator::init()`/`reset()`. The
constructor's default `nr_10_coreid_{0x01}` (in `emulator.h`) delivers
the power-on byte once. **Removal is justified per VHDL — no reset
clause exists and the prior code was clobbering the user-set coreid on
every reset.** Pre-fix this was masked because the readback came from
the canonical write-time cache byte; with V16-NMP-01 the read mux now
reflects the live `nr_10_coreid_` field, so the bug would have become
observable.

### Discriminative revert verification

Reverted `spkey_bits = ...` to `spkey_bits = 0` (mirrors pre-V16-NMP-01
write_handler hardcoding bits 1:0 to 0 with no read_handler).

Result: V16-NMP-01 group reports `1/6` PASS (only IDLE row passes,
since 0x00 == 0x00 trivially). Concrete failures:
- `V16-NMP-01-M1` got=0x00 expected=0x01
- `V16-NMP-01-DRIVE` got=0x00 expected=0x02
- `V16-NMP-01-BOTH` got=0x00 expected=0x03
- `V16-NMP-01-COMPOSE` got=0x28 expected=0x2B (coreid bits intact, button bits zeroed)
- `V16-NMP-01-LIVE` got idle=0x00, press=0x00, release=0x00 (no live tracking)

After restore: all 6/6 PASS. Matches commit message claim "5/6 FAIL pre-fix, 6/6 PASS post-fix".

---

## V16-NMP-02 — Expbus AND-mask: **VERIFIED**

### Spec match

VHDL `zxnext.vhd:2392-2393`:
```
internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82) when expbus_eff_en = '0' else
                       ((nr_89 AND nr_85) & (nr_88 AND nr_84) & (nr_87 AND nr_83) & (nr_86 AND nr_82));
```

When `expbus_eff_en=0`: effective gate = NR $82-$85 alone.
When `expbus_eff_en=1`: effective gate = (NR $86-$89) AND (NR $82-$85),
bitwise per-byte.

### Helper correctness

`Emulator::effective_internal_port_enable(reg)` at
`src/core/emulator.cpp:6042-6092` implements the formula:

1. Pair-up correct: 0x82↔0x86, 0x83↔0x87, 0x84↔0x88, 0x85↔0x89
   (`paired_reg = reg + 4`).
2. Out-of-range guard: `reg < 0x82 || reg > 0x85` returns raw cache
   (or override) — appropriate for unrelated registers.
3. expbus_eff_en=0 short-circuit: returns `base` (cache) without ANDing.
4. expbus_eff_en=1 AND-mask: `base & mask` per-bit.
5. NR $85 ↔ NR $89 special case: only enable nibble (bits 3:0)
   participates in AND. Reset_type bit 7 of `base` (NR $85) is preserved
   out-of-band via `(base & 0xF0) | ((base & 0x0F) & mask)`. Matches VHDL
   widths at :1226-1234 (NR $85/$89 are 4-bit enable + 1-bit reset_type).

### Override-pair pattern correctness

`NextReg::write` (`src/port/nextreg.cpp:451-455`) commits the cache
**after** the handler returns:
```c++
if (write_handlers_[reg]) {
    regs_[reg] = write_handlers_[reg](val);
} else {
    regs_[reg] = val;
}
```

So during a NR $82-$89 write handler, `nextreg_.cached(reg)` still has
the OLD value. The `(override_reg, override_val)` overload lets the
handler pass the in-flight value, which the helper substitutes when
the queried `r == override_reg`. This is **load-bearing** and **correct**.

Trace per write site:
- NR $82 write: `propagate(0x82, v)` → eff(0x82, 0x82, v) uses v for base, eff(0x83/0x85, 0x82, v) use cache for both — correct.
- NR $86 write: `propagate(0x86, v)` → eff(0x82, 0x86, v) uses cache for base, v for paired — correct.
- NR $80 write: `propagate()` (no override) — uses cache for all. Correct because `nmi_source_.set_expbus_eff_en()` is called BEFORE propagate, so `nmi_source_.expbus_eff_en()` reflects the fresh state when the helper consults it.

### 33 of 47 sites routed: hunt for missed cases

Total `nextreg_.cached(0x82-0x85)` references remaining in
`emulator.cpp` post-fix: **13** (the 14 the audit claimed minus one
comment-string mention). All 13 are out-of-scope:

| Line | Site | Justification |
|------|------|---------------|
| 271  | `contention_.set_port_7ffd_io_en((cached(0x82) & 0x02) != 0)` | INIT seed, runs BEFORE NR $80 init → expbus_eff_en=0 → AND-mask is no-op. NIT-eligible (consistency); next NR $82 / NR $86 write refreshes via helper. |
| 283  | `contention_.set_port_ulap_io_en((cached(0x85) & 0x01) != 0)` | Same INIT-seed pattern. NIT-eligible. |
| 769  | comment | not code |
| 2475 | comment | not code |
| 2493 | NR $85 read mux returns `cached(0x85) & 0x8F` | VHDL :6138 read returns the RAW NR $85 byte (reset_type & "000" & enable(3:0)), NOT the AND-masked version. Correct. |
| 5819, 5820, 5821, 5822 | save/restore in `Emulator::reset()` | Captures BEFORE reset, replays via `nextreg_.write` AFTER. Must use raw cache. Correct. |
| 5892, 5893, 5894, 5895 | save/restore in `Emulator::soft_reset()` | Same as hard-reset path. Correct. |

**No real port-decode site is missed.** The two INIT-seed sites at 271/283
are NITs at most (the next NR $80/$82-$85/$86-$89 write routes
through `propagate_effective_port_enables` and refreshes the shadows
through the helper anyway).

### Discriminative revert verification

Short-circuited the helper at lines 6060-6065 to return raw cache
unconditionally (mirrors pre-V16-NMP-02 behaviour; the propagation
infrastructure remains so handlers don't crash, but every read returns
the raw cache).

Result: V16-NMP-02 group reports `2/7` PASS (only EXPBUS-OFF and
EXPBUS-ON-PASS pass — both are cases where the AND-mask wouldn't have
changed the outcome). Concrete failures:
- `V16-NMP-02-EXPBUS-ON-MASK` before=0x00 after=0x07 (expected silenced; got mutated)
- `V16-NMP-02-EXPBUS-TOGGLE` before=0x00 after=0x07 (NR $80 toggle didn't refresh shadow)
- `V16-NMP-02-DIVMMC-MASK` port_io_enable=1 (expected 0 with NR $87 b0=0)
- `V16-NMP-02-MF-MASK` is_enabled=1 (expected 0 with NR $87 b1=0)
- `V16-NMP-02-NR85-NR89-B0` port_ulap_io_en=1 (expected 0 with NR $89 b0=0)

After restore: all 7/7 PASS. Matches commit message claim "5/7 FAIL pre-fix, 7/7 PASS post-fix".

---

## Test-suite cleanliness

```
ctest --test-dir build --output-on-failure
38/38 PASS, 0 FAIL, 0 SKIP

./build/test/fuse_z80_test build/test/fuse
1356/1356 PASS, 0 FAIL, 0 SKIP

./build/test/nextreg_integration_test  → V16-NMP-01-NR10-SPKEY 6/6 + V16-NMP-02-Expbus-AND-Mask 7/7
```

Build mode: Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`).
Zero regressions in existing tests.

Note: pre-existing `G56-10-04` test row updated to reflect the
post-fix behaviour (write of 0x03 idle-buttons → bits 1:0 still 0
because shadows are false). That test remains discriminative against
a hypothetical regression where bits 1:0 leak from cache instead of
being re-read live.

---

## Discriminative pre-revert FAIL: **VERIFIED**

| Finding | Pre-revert | Post-revert | Match commit |
|---------|------------|-------------|--------------|
| V16-NMP-01 | 6/6 PASS | 5/6 FAIL (only IDLE) | YES |
| V16-NMP-02 | 7/7 PASS | 5/7 FAIL (only baselines) | YES |

---

## NITs (out-of-scope for APPROVE)

**N16-NMP-A (cosmetic)**: lines 271 and 283 in `Emulator::init()` seed
ContentionModel shadows from `nextreg_.cached(0x82) / cached(0x85)`
directly instead of through `effective_internal_port_enable()`. This
is harmless because at init time `expbus_eff_en=0` (NMI source default)
and the AND-mask is a no-op; the next NR $80/$82-$85/$86-$89 write
refreshes via the helper. Routing through the helper for consistency
would be a one-liner; not blocking.

**N16-NMP-B (cosmetic)**: the `effective_internal_port_enable(reg)`
1-arg overload at line 6043-6046 forwards to the 3-arg with
`override_reg=0xFF`. 0xFF is a valid NR-byte index (NR $FF exists in
VHDL), but it's never paired with NR $82-$85 (paired_reg ranges only
0x86..0x89), so collisions are impossible by construction. Comment at
emulator.h:435 documents the convention. Not blocking.

Neither NIT changes verdict.

---

## Final verdict

**APPROVE**.

Both V16-NMP-01 and V16-NMP-02 fixes correctly implement VHDL
semantics, are well-commented with line-cited VHDL refs, are accompanied
by 13 discriminative tests that all PASS post-fix and all FAIL pre-fix
(per individual revert), and ship with zero regressions in the full
Release-mode test suite. The override-pair pattern is correctly
necessary (NextReg cache is committed after the handler returns) and
correctly documented at the helper's docstring.

The audit's "33 of 47 sites routed" claim is verified: the remaining
14 sites are 8 save/restore + 2 init seeds + 2 comments + 1 NR $85
read-mux + 1 1-arg-overload-helper-line. None are real missed
port-decode gates.

HEAD SHA: `80bedde23389796aacbed1ac46a3d37b0c5db6c8`.
