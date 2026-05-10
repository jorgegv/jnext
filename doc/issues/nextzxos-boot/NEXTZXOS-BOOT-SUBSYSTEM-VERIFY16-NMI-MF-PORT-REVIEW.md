# Pass-16 NMI + Multiface + Port + NextREG — Independent Review

Branch under review: `task2/verify16-nmi-mf-port` HEAD `391c276`
Reviewer branch:    `task2/verify16-nmi-mf-port-reviewer`
Reviewer workspace: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-nmi-mf-port-reviewer`
Build mode:         Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`)
Audit's findings:   2 × class-(b) (V16-NMP-01 + V16-NMP-02)
Audit's commits:    1 (doc-only — `391c276 doc(task2-verify16-nmi-mf-port): pass-16 audit report — 2 findings`)

VERDICT: **APPROVE**

---

## Audit-scope check

The audit prompt for Pass-16 was a *blind audit* (the auditor did not
read prior verify-pass reports and was instructed not to apply fixes —
report-only). The audit honoured both rules: HEAD `391c276` adds a
single new file (the audit report) and zero source-file changes.
Therefore the reviewer mission's instructions to *revert each fix
individually and confirm FAIL* are not applicable to Pass-16 — there
are no fixes to revert. The discriminative-test check is recorded
below as `N/A` for this reason. The verdict is rendered against (a)
the VHDL-claim accuracy, (b) finding-class correctness, (c) test
hygiene of the integration base, and (d) coverage completeness inside
the declared scope.

## VHDL claim verification

### V16-NMP-01 — NR 0x10 readback bits 1:0

VHDL `zxnext.vhd:5924`:

```
when X"10" =>
   port_253b_dat <= '0' & nr_10_coreid & i_SPKEY_BUTTONS(1 downto 0);
```

Entity port comment `zxnext.vhd:70`:

```
i_SPKEY_BUTTONS : in std_logic_vector(1 downto 0); -- raw state of nmi
                                                   -- buttons m1(0) & drive(1)
```

Top-level wiring `zxnext_top_issue2.vhd:2281`:

```
zxn_buttons <= (not btn_drive_divmmc_n) & (not btn_m1_multiface_n);
```

Confirmed: bit 0 = M1 (F9), bit 1 = Drive (F10), active-high
combinational, sampled on every NR 0x10 read. The audit's
interpretation matches the VHDL byte-for-byte.

jnext side (`src/core/emulator.cpp:1255-1261`) installs only a
`set_write_handler(0x10, ...)` that returns
`(nr_10_coreid_ & 0x1F) << 2`. Bits 1:0 of regs_[0x10] are therefore
permanently zero, irrespective of held-key state. No
`set_read_handler(0x10, ...)` is registered. The audit's claim
("hardcoded 0") is correct. The acknowledging code-comment at
`emulator.cpp:1242-1247` ("jnext does not model SPKEY_BUTTONS — they
read as 0") confirms the divergence is known but not closed.

Class-(b) is the right classification: real NextZXOS firmware does
not poll NR 0x10 bits 1:0 (the M1/Drive buttons surface via the NMI
strobe / hotkey path), so the divergence is software-observable but
non-boot-critical.

### V16-NMP-02 — `internal_port_enable` formula

VHDL `zxnext.vhd:2392-2393`:

```
internal_port_enable <=
   (nr_85 & nr_84 & nr_83 & nr_82) when expbus_eff_en = '0' else
   ((nr_89 AND nr_85) & (nr_88 AND nr_84) & (nr_87 AND nr_83) & (nr_86 AND nr_82));
```

Confirmed: when `expbus_eff_en = 1` (NR 0x80 bit 7), every per-bit
internal-port-enable is the AND of the corresponding internal and
bus-port enable bits. When `expbus_eff_en = 0`, the bus-port group
(NR 0x86-0x89) is dropped from the formula entirely.

jnext side: every consumer that gates on the internal-port-enable
mask reads only NR 0x82-0x85 directly. Examples:

- `src/core/emulator.cpp:759-762` NR 0x82 write hook calls
  `contention_.set_port_7ffd_io_en((v & 0x02) != 0)` — no AND with
  `nr_86 | expbus_eff_en` mask.
- `src/core/emulator.cpp:2441-2448` NR 0x83 write hook calls
  `divmmc_.set_port_io_enable(...)` and `multiface_.set_enabled(...)`
  from raw NR 0x83 bits.
- `src/core/emulator.cpp:2465-2468` NR 0x85 write hook propagates
  bit 0 to `contention_.set_port_ulap_io_en` from raw NR 0x85.
- `nextreg_.cached(0x82..0x85)` is consulted in port-decode paths;
  `cached(0x86..0x89)` is read only by reset-restore code at
  `emulator.cpp:5735-5855` (PASS-5 fix preserving the bus-port group
  across NR 0x02 soft reset) — never as a port-decode AND mask.

The audit's claim is correct. Class-(b) is conservative: jnext does
not emulate any expansion-bus device, so the only path that toggles
`expbus_eff_en` is software writing NR 0x80 bit 7 explicitly, which
NextZXOS firmware never does in the boot path. The divergence is
real but boot-irrelevant.

## "Other expbus_eff_en cases" sweep

Cross-checked every `expbus_eff_en` / `expbus_en` site in
`zxnext.vhd`:

| VHDL line  | Use site                                | jnext status |
| ---------- | --------------------------------------- | ------------ |
| 1701       | `o_BUS_EN <= expbus_eff_en`             | Out signal — no expbus device wired. Correctly inert. |
| 1837       | `expbus_disable_int` gate              | Modelled in `nmi_source.cpp` int-enable path. |
| 1839       | `z80_wait_n` AND with `i_BUS_WAIT_n`    | No expbus → `i_BUS_WAIT_n = 1`. Correctly inert. |
| 1874       | CPU DI mux fallback to bus              | No expbus → fallback unreachable. Correctly inert. |
| 2089       | `nmi_assert_expbus` gate               | Modelled in `nmi_source.cpp:240-256`. |
| 2197       | `expbus_en <= nr_80_expbus(7)`         | Modelled in `emulator.cpp:3746-3750`. |
| 2224       | `bus_iorq_ula <= expbus_eff_en AND ...`| Out signal to bus — no consumer. Correctly inert. |
| **2392**   | **`internal_port_enable` formula**      | **DIVERGENCE — V16-NMP-02.** |
| 3018       | `sram_pre_romcs_n` composition         | `i_BUS_ROMCS_n` not modelled → no effective gate change in jnext. |
| 3453, 3458 | `port_fe_bus` propagation              | No expbus → branch never taken. Correctly inert. |
| 5804-5816  | `expbus_eff_en` bus-idle commit         | Modelled in `emulator.cpp:3746-3750` as immediate commit (acceptable since no expbus device). |

V16-NMP-02 is the only `expbus_eff_en` consumer that materially
affects internally-modelled state and is mismatched. No further
expbus-related findings missed.

## "Other input-driven NR readbacks" sweep

VHDL read-mux entries that consume `i_*` input signals (not cached
NR fields) within the audit scope (NMI/MF/Port-owned NRs):

| NR    | VHDL line | Input signal           | jnext status |
| ----- | --------- | ---------------------- | ------------ |
| 0x10  | 5924      | `i_SPKEY_BUTTONS(1:0)` | **DIVERGENCE — V16-NMP-01.** |
| 0x81  | 6126      | `i_BUS_ROMCS_n`        | OK — read handler at `emulator.cpp:3724-3726` forces bit 7 = '1' (idle, no expbus). V11-NMP-01 prior fix already correct. |
| 0xA9  | 6201      | `i_ESP_GPIO_20`        | Out of scope (ESP, not NMI/MF/Port). Hardcoded to 0 at `emulator.cpp:2420`. |
| 0xB0  | 6208      | `i_KBD_EXTENDED_KEYS`  | Out of scope (keyboard). Has read handler at `emulator.cpp:1340`. |
| 0xB1  | 6212      | `i_KBD_EXTENDED_KEYS`  | Out of scope (keyboard). Has read handler at `emulator.cpp:1341`. |
| 0xB2  | 6215      | `i_JOY_*`              | Out of scope (joystick). Has read handler at `emulator.cpp:1342`. |

V16-NMP-01 is the only in-scope input-driven readback that returns a
stale/cached value. No further findings missed.

## Test hygiene of the integration base

```
ctest --test-dir build --output-on-failure
38/38 PASS, 0 FAIL, 0 SKIP

./build/test/fuse_z80_test build/test/fuse
1356/1356 PASS, 0 FAIL, 0 SKIP

bash test/00regression/regression.sh
32 PASS / 1 FAIL / 0 SKIP    [parallax-demo screenshot regression — pre-existing,
                              not caused by Pass-16 (which added zero source changes)]
```

The `parallax-demo` regression is unrelated to Pass-16 (the audit
modified zero source files; the regression carries forward from prior
integration commits). Per the project's canonical CI gate (ctest +
FUSE), the integration base is green.

## Discriminative revert check

**N/A** — Pass-16 did not apply any fixes. Reviewer mission step 3
("REVERT fix individually → rebuild → confirm FAIL → restore →
confirm PASS") presupposes a fix-bearing audit. This pass was
report-only per its prompt.

## Audit's "areas scrutinized that did NOT yield findings" — spot check

Spot-checked items 6 (Multiface FSM) and 9 (NMI return-address shadow
latch) against the VHDL multiface entity and `zxnext.vhd:2050-2070 +
6232-6235`. Both audit summaries match the VHDL faithfully. No
sleeper findings surfaced in the scrutinized-but-clean list.

## Issues found by reviewer

None. The audit is well-scoped, internally consistent with the
VHDL, and the two findings are correctly classified as class-(b).

## Verdict

**APPROVE.** Pass-16 is a clean, defensible blind audit:

- 2 × class-(b) findings, both VHDL-correct.
- 25 scrutinized-but-clean items spot-checked and consistent.
- Build green, ctest 38/38, FUSE 1356/1356.
- No source changes proposed (per blind-audit prompt).

Pass-17 should triage V16-NMP-01 / V16-NMP-02 against the
"honestly converged" bar and decide whether to apply fixes (with
discriminative tests) or accept-and-document under the class-(b)
exception. Recommended fix shapes are already documented in the
audit report.

---

## Final return JSON

```json
{
  "verdict": "APPROVE",
  "findings_verified": 2,
  "discriminative": "N/A — audit applied zero fixes (report-only per blind-audit prompt)",
  "issues": [],
  "tests_passed": true,
  "head_sha": "391c276"
}
```
