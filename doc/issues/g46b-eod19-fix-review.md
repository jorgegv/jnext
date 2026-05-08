# Review of fix 144af1f — NR $03 machine_type commit + soft-reset preservation

**Reviewer**: independent agent
**Date**: 2026-05-08
**Verdict**: APPROVE-WITH-NITS

## TL;DR

The fix is correct in its core intent and faithfully mirrors the central
VHDL semantics: `nr_03_machine_type` (FPGA-side latch) is now propagated
to `Mmu::machine_type_` (the C++ side that drives `current_sram_rom()`),
and the soft-reset preservation in `Emulator::init()` matches VHDL line
:1103's "no reset clause" property. Five real findings:

1. **Pentagon mapping is FUNCTIONALLY correct but VHDL-semantically
   ambiguous.** typ_sel=$04 maps in VHDL (`zxnext.vhd:5751`) to
   `machine_type_128`, NOT `machine_type_p3`. The fix maps it to
   `MachineType::ZXN_ISSUE2`, which shares the same
   `current_sram_rom()` branch as `ZX128K` (1-bit `port_7ffd(4)`). So
   the ROM-routing behavior is right, but a future reader will wonder
   why Pentagon ≠ 128K. (Nit; comment would resolve.)

2. **Stale slot cache after machine_type change.** `set_machine_type()`
   (mmu.h:746) is a bare assignment with no `apply_legacy_rom_slots_()`
   trigger. VHDL `sram_rom` is a combinational signal that updates
   instantly when `machine_type_*` changes; the C++ cached
   `slots_[0/1]` only updates on a subsequent paging-port write. In
   the actual NextZXOS path the supervisor writes $7FFD/$1FFD shortly
   after, so this is benign there, but it is a 1-write VHDL lag worth
   closing for strict faithfulness. (Latent.)

3. **Hard-reset divergence between NextReg and Mmu fields.** On hard
   reset, `init()` pushes `cfg.type` into Mmu (e.g. ZX48K), but
   `nextreg_.nr_03_machine_type_` is preserved (defaults 0x03 = +3).
   This means after `--machine 48k` hard boot, an NR $03 read returns
   `bits[2:0]=011` (+3) while the MMU actually behaves as 48K. This
   pre-existed but the fix makes it more visible. (Should be
   addressed.)

4. **Latent gap in `current_sram_rom()` ZX128K branch — pre-existing,
   exposed by fix.** VHDL :2998-3001 applies altrom-lock override to
   the non-48K, non-+3 branch (= 128K AND ZXN). C++ ZX128K branch
   (mmu.h:768-769) does NOT honor altrom locks, while ZXN_ISSUE2 does.
   Pre-fix this branch was unreachable at runtime; post-fix the
   supervisor can reach it via typ_sel=$02. NextZXOS uses typ_sel=$03
   so G46(b) is unaffected, but a regression seam is introduced.
   (Suggest addressing in a follow-up.)

5. **Test coverage gap on the NEW behavior.** Cat11b tests
   (mmu_test.cpp:1746-1808) verify `current_sram_rom()` per machine
   type via direct `set_machine_type()`. CFG-09-INT
   (nextreg_integration_test.cpp:1672-1717) verifies NR $03 *reads
   back* preserved across soft reset. **No test verifies the
   integrated path "NR $03 write commits to Mmu" or "Mmu::machine_type
   survives soft reset"** — exactly the property the fix introduces.

Numbered changes recommended below; verdict APPROVE-WITH-NITS because
the fix is correct on the supervised happy path and unblocks G46(b)
forward progress, and none of the findings are show-stoppers.

## VHDL faithfulness audit

### `zxnext.vhd:5124-5151` — NR $03 write FSM

Cross-reference with the C++ handler at `emulator.cpp:1631-1716`.

| VHDL line | VHDL action | C++ mirror | Verdict |
|---|---|---|---|
| 5117-5119 | NR 0x02 → bus_reset signal | (separate handler at line 4630+) | OK (out of scope) |
| 5122 | `bootrom_en <= '0'` on any NR 0x03 write | emulator.cpp:1632-1635 | OK |
| 5124 | gate timing on `bit7=1 AND user_dt_lock=0 AND bit3=0` (PREVIOUS dt_lock) | emulator.cpp:1641-1666 | OK |
| 5126-5132 | `nr_03_machine_timing` case from bits[6:4] | emulator.cpp:1644-1654 | OK |
| 5135 | `nr_03_user_dt_lock <= xor bit3` (UNCONDITIONAL XOR) | emulator.cpp:1669 | OK |
| 5137 | "if PREVIOUS nr_03_config_mode = '1'" | emulator.cpp:1683 reads `nextreg_.nr_03_config_mode()` BEFORE `apply_nr_03_config_mode_transition(v)` (line 1709) | OK — ordering is correct |
| 5138-5145 | `nr_03_machine_type` case from bits[2:0]; "111" / "000" / others (5/6/7) → null | emulator.cpp:1684-1697 | OK — explicit cases for $01..$04, default = no change. 0x07 / 0x05 / 0x06 / 0x00 fall through correctly. |
| 5147-5151 | config_mode FSM: 111 → set, 001..110 → clear, 000 → no change | NextReg::apply_nr_03_config_mode_transition() | OK |

**Critical ordering check (VHDL :5137 reads PREVIOUS config_mode):**
emulator.cpp:1683 evaluates `nextreg_.nr_03_config_mode()` BEFORE the
FSM call at line 1709 — correct. Comment at line 1707-1708 explicitly
documents this dependency. ✓

### `zxnext.vhd:5741-5755` — machine_type → machine_type_{48,128,p3}

```
case nr_03_machine_type is
   when "000" | "001" => machine_type_48 <= '1';
   when "010" | "100" => machine_type_128 <= '1';
   when others        => machine_type_p3 <= '1';   -- "011" + anything else
end case;
```

The C++ fix maps:
- typ_sel=$01 → `MachineType::ZX48K` ✓ matches VHDL "001" → machine_type_48
- typ_sel=$02 → `MachineType::ZX128K` ✓ matches VHDL "010" → machine_type_128
- typ_sel=$03 → `MachineType::ZX_PLUS3` ✓ matches VHDL "011" → machine_type_p3 (default)
- typ_sel=$04 → `MachineType::ZXN_ISSUE2` — VHDL "100" → machine_type_128

**Finding (1) clarification**: typ_sel=$04 in VHDL is functionally
identical to typ_sel=$02 (both fire `machine_type_128`). The C++ fix
maps it to `ZXN_ISSUE2` instead. Looking at `Mmu::current_sram_rom()`
mmu.h:768-782 and `sram_rom3()` mmu.h:818-822, ZX128K and ZXN_ISSUE2
share the same branch (1-bit `port_7ffd(4)` selector). So the ROM-
routing behavior is identical. The semantic difference would only
matter if some other code path branched on `machine_type_` AND
distinguished 128K from ZXN. Search confirms no such branch exists
today (`Mmu::altrom_sram_page_` mmu.h:925-950 also bundles 128K +
ZXN_ISSUE2 in one case label). So functionally correct, but a
follow-on developer might map typ_sel=$04 to `ZX128K` to mirror VHDL
literally. **Recommend adding a comment** documenting why $04 maps to
ZXN_ISSUE2 and not ZX128K.

### `zxnext.vhd:2981-3008` — sram_rom selection

Audited mmu.h:764-783 against VHDL:

| VHDL | C++ | Verdict |
|---|---|---|
| :2983-2986 (48K → "00", altrom modulates alt_128_n only) | mmu.h:766-767 returns 0 unconditionally | OK — see existing comment at mmu.h:716-721 noting altrom-locks-on-48K is a documented gap. Not introduced by this fix. |
| :2988-2991 (+3 with altrom lock → lock_rom1 & lock_rom0) | mmu.h:771-774 | OK |
| :2993-2995 (+3 no lock → port_1ffd_rom = (port_1ffd(2), port_7ffd(4))) | mmu.h:775 returns `current_rom_bank()` = `(port_1ffd(2)<<1) | port_7ffd(4)` | OK |
| :2998-3001 (else / 128K / ZXN with altrom lock → '0' & lock_rom1) | mmu.h:776-779 (ZXN_ISSUE2 only — ZX128K branch at line 768-769 does NOT honor lock!) | **MISMATCH (pre-existing)** — see Finding (4) |
| :3003-3005 (else no lock → '0' & port_1ffd_rom(0) = '0' & port_7ffd(4)) | mmu.h:781 (ZXN), mmu.h:769 (128K) | OK |

**Finding (4) detail.** VHDL `:2998-3001` does NOT distinguish 128K
from ZXN — the entire `else` branch covers both. The C++ split:

```cpp
case MachineType::ZX128K:
    return static_cast<uint8_t>((port_7ffd_ >> 4) & 1);   // no altrom lock check
case MachineType::ZXN_ISSUE2:
default:
    if (nr_8c_altrom_lock_rom1() || nr_8c_altrom_lock_rom0())
        return static_cast<uint8_t>(nr_8c_altrom_lock_rom1() ? 1 : 0);
    return static_cast<uint8_t>(current_rom_bank() & 1);
```

For `ZX128K + lock_rom0=1, lock_rom1=0`: VHDL returns `'0'&'0'=0`,
C++ returns `(port_7ffd(4))` — which can be 0 or 1 depending on
$7FFD. **Diverges.** Pre-fix this was unreachable at runtime
(`Mmu::machine_type_` was frozen to `cfg.type` at boot). Post-fix it
is reachable if a supervisor writes NR $03 with typ_sel=$02. NextZXOS
uses typ_sel=$03, so G46(b) is unaffected. **Recommend** harmonizing
the ZX128K and ZXN_ISSUE2 branches in a follow-up commit.

### `zxnext.vhd:1103` — power-on default and reset semantics

VHDL declaration: `signal nr_03_machine_type : std_logic_vector(2 downto 0) := "011";`

VHDL reset block at :4930 (audited :4930-5111): `nr_03_machine_type` is
NOT in the reset block. Confirmed — only initialiser at :1103 sets it.
Therefore VHDL preserves it across BOTH hard and soft reset (VHDL `reset`
signal is `reset_hard or reset_soft` per CLAUDE.md / mmu.cpp:74 audit).

The C++ NextReg::reset() at nextreg.cpp:117 explicitly does NOT reset
`nr_03_machine_type_` (member initialiser handles power-on, comment
at lines 105-117 cites G63). ✓ Faithful.

The fix's gate at emulator.cpp:260-262 preserves `Mmu::machine_type_`
across soft reset: ✓ Faithful (matches the NextReg latch property).

**However, see Finding (3)**: there's an inconsistency on HARD reset.
On hard reset, the fix pushes `cfg.type` into Mmu (line 261). But
NextReg's `nr_03_machine_type_` is preserved across hard reset too
(VHDL :1103 + no reset clause + nextreg.cpp:117 NOT-reset). After
hard reset, the two are out of sync if `cfg.type ≠ ZX_PLUS3` (the
default). For e.g. `--machine 48k` hard boot:
- `Mmu::machine_type_` = ZX48K
- `NextReg::nr_03_machine_type_` = 0x03 (= ZX_PLUS3 per VHDL :5751)
- NR $03 read returns `bits[2:0]=011`
- `current_sram_rom()` returns 0 (48K branch)

This is a NEW divergence introduced by the fix's mismatched reset
gates: NextReg preserves; Mmu re-pushes from CLI. Pre-fix, both were
mismatched but `Mmu::machine_type_` was the C++ source of truth and
NextReg's field was unused for routing. Post-fix, both fields drive
behavior — and they disagree on hard reset. The NextReg field is the
read-back surface; Mmu's field is the routing surface. Result: a
third-party debugger reading NR $03 sees +3 even on `--machine 48k`.

Recommendation: on hard reset, ALSO push `cfg.type` →
`nextreg_.set_nr_03_machine_type(typ_sel_for(cfg.type))`. This mirrors
the FPGA-side latch's "init via signal initialiser" with a runtime
push in the C++ harness, and keeps the two fields in sync.

## Side-effect analysis

### Methods consuming `Mmu::machine_type_`

I audited every consumer of `machine_type_` in mmu.h/cpp:

| Consumer | Behavior on type change | Verdict |
|---|---|---|
| `current_sram_rom()` (mmu.h:764) | Computes per-type; reads live `port_*` state | OK — pure function, recomputed on every call |
| `sram_rom3()` (mmu.h:806) | Pure function | OK |
| `altrom_sram_page_()` (mmu.h:925) | Pure function | OK |
| `apply_legacy_rom_slots_()` (mmu.cpp:330) | Uses `current_sram_rom()` to compute `map_rom_physical(slot, sram_rom*2)` | Will pick up new type **on next call**, but `set_machine_type` does NOT auto-trigger this. See Finding (2). |
| `map_plus3_bank()` (mmu.cpp:530) | Calls `apply_legacy_rom_slots_()` | OK |
| `to_sram_page()` (mmu.h:898) | Gated on `rom_in_sram_`, NOT `machine_type_` | OK — unaffected |
| Save/load state (mmu.cpp:591, 649) | Round-trips `machine_type_` | OK |

### Methods NOT in sync with `machine_type_`

- **`ContentionModel::type_` (contention.cpp:4)** is set at
  `build()`-time only, called from `Emulator::init()`. After NR $03
  commits a new type at runtime, `ContentionModel::type_` lags. **HOWEVER**
  the VHDL contention path actually uses `machine_timing_48/128/p3`
  (zxnext.vhd:4490-4492), derived from `nr_03_machine_timing`, NOT
  `nr_03_machine_type`. So strictly speaking the contention model
  should track timing, not type. The C++ field `type_` is misnamed; it
  serves both roles today. The fix doesn't worsen this — pre-existing
  gap.

- **`Im2Controller::machine_48_or_p3_` (im2.cpp:572)** is updated by
  the NR $03 handler at emulator.cpp:1665 from the **timing** bits
  (`new_timing == 0x01 || == 0x03`), independent of type. This is
  VHDL-faithful (zxnext.vhd:2033 `pulse_count_end` gates on
  `machine_timing_*`). Fix does not interact.

### Slot rebuild on type change (Finding 2 detail)

VHDL `sram_rom` is a combinational process (zxnext.vhd:2981 — sensitive
to `machine_type_48, machine_type_p3, ...`). Output `sram_rom` is fed
into `sram_pre_A21_A13` at :3052, latched into `sram_A21_A13` on the
falling edge of CLK_28. So on the VERY NEXT mreq after a NR $03 commit,
the new ROM bank is selected.

In C++, slot 0/1 ROM page is cached in `slots_[0]` and `slots_[1]` via
`map_rom_physical()` calls inside `apply_legacy_rom_slots_()`. The cache
is rebuilt only when `apply_legacy_paging_()` (or one of its callers) is
invoked — i.e. on $7FFD/$1FFD/$DFFD/$EFF7 writes, NR 0x8E/0x8F writes,
or soft reset.

`Mmu::set_machine_type(t)` at mmu.h:746 is a 1-line assignment with no
side-effect. **Therefore between an NR $03 commit and the next paging
write, the C++ ROM mapping lags VHDL.**

Mitigation in practice: A supervisor entering +3 mode would always
follow up with $7FFD/$1FFD writes to set up the desired layout, so
the cached slots_[0/1] would be refreshed within a few instructions.
The fix's commit message confirms this happens in NextZXOS: "Post-fix
supervisor reaches bank-2 init".

Recommendation (not blocking): in `Mmu::set_machine_type()`, call
`apply_legacy_rom_slots_()` if `t != machine_type_` (i.e. only on
actual transitions, to avoid recursion or excess work). This makes
the C++ behavior strictly VHDL-faithful and removes the potential
1-write lag.

## Regression risk

**Hard-reset semantics for non-Next CLI machines.** Tested mentally
across `--machine 48k`, `128k`, `plus3`, `next`:

| CLI | cfg.type | Mmu::machine_type_ post-init | NextReg::nr_03_machine_type_ post-init | Diverges? |
|---|---|---|---|---|
| 48k | ZX48K | ZX48K (line 261) | 0x03 (preserved) | YES |
| 128k | ZX128K | ZX128K | 0x03 | YES |
| plus3 | ZX_PLUS3 | ZX_PLUS3 | 0x03 | NO (matches) |
| next | ZXN_ISSUE2 | ZXN_ISSUE2 | 0x03 (= +3 timing/type per VHDL :1103, also "matches" the +3 ROM-routing branch since ZXN doesn't equal +3 in C++) | YES (semantically) |

This pre-existed the fix. The fix didn't introduce the divergence,
but it didn't fix it either. **Recommend** that the hard-reset block
also push `nextreg_.set_nr_03_machine_type(typ_for(cfg.type))` to
align the two surfaces.

**No regression** in the existing tests: the 16-row Cat11b test
(mmu_test.cpp:1746-1808) calls `set_machine_type()` directly and
doesn't depend on the new path. CFG-09-INT (nextreg_integration_test
:1672-1717) tests the NextReg latch survival, also unaffected.

## Soft-reset preservation

VHDL: NextReg's `nr_03_machine_type` survives both hard and soft
reset (no reset clause inside zxnext.vhd at lines 4930-5111 — verified).

C++ post-fix:
- `NextReg::nr_03_machine_type_` survives both — nextreg.cpp:117 ✓
- `Mmu::machine_type_` survives soft reset — emulator.cpp:260-262 ✓
- `Mmu::reset(hard|soft)` does NOT touch `machine_type_` — mmu.cpp:68-163 ✓ (verified)

**Verified completeness**: the gate at `init()` line 260 is the ONLY
hard-reset push; `Mmu::reset()` doesn't undo it; no other code path
touches `machine_type_` outside `set_machine_type()` and the load_state
path. ✓

The comment at emulator.cpp:254-259 is accurate and references the
correct VHDL semantics. ✓

## Code quality

### Log message

```cpp
Log::emulator()->info(
    "machine_type committed via NR 0x03: {} -> {} (typ_sel={:#04x})",
    static_cast<int>(mmu_.machine_type()),
    static_cast<int>(new_mt), typ_sel);
```

**Nit (minor)**: prints raw enum integer values (0..3). The enum
declaration is `enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, ZX_PLUS3 }`,
so the user sees `0 -> 3` instead of `ZXN_ISSUE2 -> ZX_PLUS3`. Use
the existing `machine_type_str()` helper at emulator.cpp:3611. Sample:
```cpp
"machine_type committed via NR 0x03: {} -> {} (typ_sel={:#04x})",
machine_type_str(mmu_.machine_type()), machine_type_str(new_mt), typ_sel
```

**Frequency**: gated on actual transition (`new_mt != mmu_.machine_type()`).
Boot has at most one or two transitions; not noisy. Info-level is fine.

### Comment quality

Comment block at emulator.cpp:1672-1682 is excellent — cites VHDL
lines, explains the supervisor's expected behavior, names the side
effect, and identifies the cross-bank chain that broke pre-fix.
Comment block at emulator.cpp:254-259 is also good. ✓

### Code structure

The expansion of the existing switch/case to commit both
`nextreg_.set_nr_03_machine_type(...)` AND track `new_mt` for Mmu is
clean. The post-switch guard `if (commit && new_mt != mmu_.machine_type())`
is defensive against unexpected enum default values. ✓

**Minor nit**: the four `case` arms duplicate the pattern
`nextreg_.set_nr_03_machine_type(0x0X); new_mt = ...; commit = true; break;`.
Could be tightened to a small lookup table, but current form is
parallel to the pre-fix shape and easy to read. Not worth changing.

### Dead code

`MachineType new_mt = mmu_.machine_type();` initializer at line 1685
is overwritten in every commit case. The `default` arm does NOT touch
new_mt, but `commit` stays false so `new_mt` is dead in that path.
Would be slightly cleaner to declare without initializer and only
assign inside commit cases, but the C++ idiom of "always-defined
variable" is reasonable. Pre-existing pattern in this file. ✓

## Test coverage gaps

**Gap A (highest priority)**: No integration test for
"NR $03 write commits to Mmu::machine_type_". Cat11b tests the
sram_rom calculation in isolation; CFG-09-INT tests NextReg-side
preservation. The integration is **untested**.

Suggested test (place in
`test/nextreg/nextreg_integration_test.cpp`, after CFG-09-INT):

```cpp
// CFG-10-INT — G46(b) fix 144af1f: NR 0x03 machine-type commit
// propagates to Mmu so current_sram_rom()/sram_rom3() track the
// supervisor-committed type.
//
// Sequence:
//   1. Boot --machine next (ZXN_ISSUE2). Verify mmu.machine_type() = ZXN_ISSUE2.
//   2. Re-enter config_mode (NR 0x03 = 0x07).
//   3. Commit +3 (NR 0x03 = 0x03). Per VHDL :5137-5145 + :5751-5754.
//   4. Verify mmu.machine_type() = ZX_PLUS3.
//   5. Verify NR 0x03 read returns bits[2:0]=011.
//   6. Soft reset (NR 0x02 = 0x01).
//   7. Verify mmu.machine_type() STILL = ZX_PLUS3 (preservation).
```

**Gap B**: No test for
"Mmu::machine_type_ matches NextReg::nr_03_machine_type_ post-hard-reset
for all CLI types". This would catch Finding (3) (the hard-reset
divergence). Would be a 4-row table test exercising
`--machine {48k, 128k, plus3, next}` + asserting the two fields are
equivalent.

**Gap C**: No test exercises altrom-lock + machine_type ZX128K
post-runtime-commit (Finding 4). Cat11b only covers no-lock cases.
A 4-row extension to Cat11b would lock down the
ZX128K-with-altrom-lock VHDL behavior (`:2998-3001`).

## Recommended changes (if any)

**Should-fix (block on next iteration if practical):**

1. **Hard-reset alignment (Finding 3)**: in `Emulator::init()` at the
   `if (!preserve_memory)` block, after `mmu_.set_machine_type(cfg.type)`,
   also push the matching typ_sel to NextReg. Pseudocode:
   ```cpp
   if (!preserve_memory) {
       mmu_.set_machine_type(cfg.type);
       const uint8_t typ_sel = match cfg.type {
           ZX48K     => 0x01;
           ZX128K    => 0x02;
           ZX_PLUS3  => 0x03;
           ZXN_ISSUE2=> 0x04;
       };
       nextreg_.set_nr_03_machine_type(typ_sel);
   }
   ```
   This keeps Mmu and NextReg in sync from boot. Note: NR 0x03 read-back
   on `--machine next` would then return bits[2:0]=100 (= machine_type_128
   per VHDL :5751) — semantically Pentagon, which is what jnext models
   ZXN_ISSUE2 as. If the project prefers ZXN to read back as +3 (0x03),
   document the choice.

**Nice-to-have (follow-up commits):**

2. **Slot cache rebuild (Finding 2)**: extend `Mmu::set_machine_type()`
   to call `apply_legacy_rom_slots_()` on actual transition. One line.
3. **128K altrom-lock harmonization (Finding 4)**: align the ZX128K
   branch of `current_sram_rom()` with the ZXN_ISSUE2 branch (and VHDL
   :2998-3001) — both should honor altrom locks. One-line change in
   mmu.h:768-769.
4. **Pentagon→128K mapping comment (Finding 1)**: add a line of
   comment at the typ_sel=$04 case explaining why we map to ZXN_ISSUE2
   not ZX128K (because ZXN_ISSUE2 in the C++ enum models the Next /
   Pentagon-class 1-bit ROM routing; both are equivalent under
   `current_sram_rom()`).

**Test additions:**

5. **CFG-10-INT** as proposed under Gap A.
6. **Cat11c (or extended Cat11b)** for ZX128K + altrom locks (Gap C).
7. **CFG-11-INT (or similar)** for hard-reset NextReg/Mmu sync (Gap B).

**Cosmetic:**

8. Use `machine_type_str()` in the new info log message instead of
   raw int casts.

---

**Confirmed REVIEW path**: `/home/jorgegv/src/spectrum/jnext/doc/issues/g46b-eod19-fix-review.md`

**Could not verify**: I did not run jnext or any tests (per task
"Don'ts"). The author's claim "Tests: 33 regression PASS / 0 FAIL" is
not independently confirmed in this review. The behavioral claim
"Post-fix supervisor reaches bank-2 init, font blits to bank-7 RAM"
also relies on runtime verification I did not perform.
