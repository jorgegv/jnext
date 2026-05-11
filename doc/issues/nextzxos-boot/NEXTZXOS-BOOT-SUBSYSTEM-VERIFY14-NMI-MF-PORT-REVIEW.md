# NextZXOS Boot — Pass-14 NMI/MF/Port/NextREG Audit — Independent Review

**Review branch**: `task2/verify14-nmi-mf-port-reviewer` (forked off
`task2/verify14-nmi-mf-port` at HEAD `e13ce54`).
**Audit head reviewed**: `e13ce54` "doc(task2-verify14-nmi-mf-port):
pass-14 audit report — 3 findings".
**Reviewer head**: `ecdbe4a` (this branch — adds V14-NMP-04 promotion fix
on top of audit head).

**Verdict**: **APPROVE**.

The three class-(a) findings (V14-NMP-01/02/03) are VHDL-correct,
discriminatively tested, and the rewrite of the previously-buggy
MF-MUX-01 row was justified — the original test enshrined the
emulator's buggy `port_1ffd_ & 0x0F` output, not the VHDL truth. The
class-(c) NR 0x2A deferral was rejected per the convergence rule and
fixed in this review (V14-NMP-04). Build clean, ctest 38/38 PASS, FUSE
1356/1356 PASS.

---

## Methodology

* Read the audit report at `…/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY14-NMI-MF-PORT.md`.
* Did NOT read prior pass reports (V01-V13).
* VHDL as oracle: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.
* Discriminative protocol: revert each fix individually → rebuild
  affected test binary → confirm the new test row(s) FAIL → restore
  fix → confirm PASS.
* Release-mode build (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`).

---

## Per-finding verification

### V14-NMP-01 — MF+3 readback at LSB 0x3F leaks port_1ffd bit 3 when FDC disabled — APPROVE

**VHDL claims confirmed**:

* `zxnext.vhd:4310-4327` (mf_port_dat case mux) — branch `"0001"`:
  `mf_port_dat <= "0000" & (NOT port_1ffd_mtr_n) & port_1ffd_reg;` ✓
* `zxnext.vhd:879-880` — `port_1ffd_reg` is 3 bits, `port_1ffd_mtr_n`
  is a separate 1-bit FF (NOT a slice of port_1ffd_reg). ✓
* `zxnext.vhd:3744-3761` (port_1ffd_mtr_n process) — priority order
  exactly as the audit cites:
  * line 3747-3749: `if reset='1'` → motor_n='1'
  * line 3751-3753: `elsif nr_81_expbus_fdc='0'` → motor_n='1' (every clock)
  * line 3755-3757: `elsif port_xffd='1' AND cpu_a(13:12)="01" AND
    iowr='1' AND port_fd_conflict_wr='0'` → motor_n=NOT cpu_do(3) ✓
* `zxnext.vhd:1221-1225` — NR 0x81 power-on default has all expbus
  fields at '0'. With `nr_81_expbus_fdc = '0'` by default and no
  jnext code that drives NR 0x81, the priority-2 elsif fires every
  clock and forces motor_n='1' indefinitely. ✓

**Fix correctness**:
The C++ at `src/core/emulator.cpp` MF+3 mux `case 0x1` now composes
bit 3 from the FDC gate:

```cpp
const uint8_t reg_lo3 = static_cast<uint8_t>(mmu_.port_1ffd() & 0x07);
const bool fdc_en = (nr_81_ & 0x08) != 0;
const uint8_t mtr_bit = fdc_en ? static_cast<uint8_t>(mmu_.port_1ffd() & 0x08)
                               : static_cast<uint8_t>(0x00);
return static_cast<uint8_t>(mtr_bit | reg_lo3);
```

This faithfully implements the priority chain: when `nr_81_expbus_fdc=0`
(`(nr_81_ & 0x08) == 0`), the motor latch is '1' (forced by the
priority-2 elsif), so `(NOT motor_n) = 0` — bit 3 is forced 0. When FDC
is enabled, the motor latch tracks `NOT cpu_do(3)`, so `(NOT motor_n)
= cpu_do(3)` — bit 3 = `port_1ffd_ & 0x08`. Bits 2:0 always come from
`port_1ffd_reg` (the low 3 bits of the cached `cpu_do`). ✓

**MF-MUX-01 rewrite justification (CRITICAL CHECK)**:
Inspected `git show 789e8b8 -- test/multiface/multiface_test.cpp` —
the original creation of MF-MUX-01 in commit `789e8b8` (Wave 1 B3 of
the Multiface plan, 2026-05-04). The original comment explicitly
read VHDL :3757 in isolation (`port_1ffd_mtr_n = NOT cpu_do(3)`) and
concluded "(NOT mtr_n) = cpu_do(3)" — completely missing the priority-2
elsif at :3751-3753 that forces `mtr_n='1'` when `nr_81_expbus_fdc='0'`.
The original test asserted `got == 0x0B` after writing 0xAB to port
0x1FFD with FDC disabled — but the VHDL-true expected value is `0x03`
(bit 3 forced 0). The original row therefore enshrined the buggy C++
output `port_1ffd_ & 0x0F`, not a defensible VHDL reading. The Pass-14
rewrite replaces the assertion with the VHDL-faithful `got == 0x03`
and adds MF-MUX-01b for the FDC-enabled case. Rewrite is justified —
this was a coverage-theatre row of exactly the kind documented in
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`.

**Discriminative test (REVERT-CHECK)**:
Reverted the case 0x1 body to `return static_cast<uint8_t>(mmu_.port_1ffd() & 0x0F);`.
Result:
```
FAIL MF-MUX-01: MF+3 read 0x1xxx LSB 0x3F (FDC=0): bit 3 forced 0,
                bits 2:0 = port_1ffd_reg
                [VHDL zxnext.vhd:4312 (mux), :3724 (port_1ffd_reg),
                 :3751 (FDC=0 motor force)]
```
MF-MUX-01b passes coincidentally pre-fix (because the buggy code
also returns 0x0B for FDC=1 — the discriminator is the COMPARISON
between the FDC=0 and FDC=1 rows, as the audit documents). Revert →
FAIL on FDC=0 row only. Restore → both PASS. ✓

### V14-NMP-02 — NR 0x28 read returns cached last-write byte instead of `nr_stored_palette_value` — APPROVE

**VHDL claims confirmed**:

* `zxnext.vhd:1190` — `signal nr_stored_palette_value :
  std_logic_vector(7 downto 0);` ✓
* `zxnext.vhd:5397-5403` — NR 0x44 first-half write (`nr_palette_sub_idx
  = '0'`) latches `nr_stored_palette_value <= nr_wr_dat;`; second-half
  branch only auto-increments `nr_palette_idx`. ✓
* `zxnext.vhd:6003-6004` — read mux: `when X"28" => port_253b_dat <=
  nr_stored_palette_value;` ✓
* `zxnext.vhd:6298-6310` — NR 0x28 write effects: only mutate
  `nr_keymap_sel` and `nr_keymap_addr(8)` from bits 7 and 0 of
  `nr_wr_dat`; do NOT touch `nr_stored_palette_value`. ✓

**Fix correctness**:
* `src/video/palette.h` adds public inline accessor
  `uint8_t nine_bit_first_byte() const { return nine_bit_first_byte_; }`
  exposing the existing PaletteManager shadow (which is latched in
  `write_9bit` at palette.cpp:332 — the same place VHDL latches
  `nr_stored_palette_value`).
* `src/core/emulator.cpp` installs a NR 0x28 read_handler that returns
  `palette_.nine_bit_first_byte()`. Per `NextReg::read` at
  nextreg.cpp:408-414, when a read_handler exists it overrides the
  cached byte — so NR 0x28 reads now bypass the cache entirely and
  surface the live shadow. ✓

The four discriminative test rows (V14-NMP-02-NR28-01..04) cover
exactly the boundary cases:
* Reset → 0x00 (default).
* NR 0x28 ← 0xA5 → must NOT change NR 0x28 read.
* NR 0x44 first half ← 0xC3 → NR 0x28 read becomes 0xC3.
* NR 0x44 second half ← 0x01 → NR 0x28 read remains 0xC3.

**Discriminative test (REVERT-CHECK)**:
Reverted the read_handler installation. Result:
```
FAIL V14-NMP-02-NR28-02: ... [after NR 0x28<-0xA5: got=0xA5 want=0x00]
FAIL V14-NMP-02-NR28-03: ... [after NR 0x44<-0xC3 (first half): got=0xA5 want=0xC3]
FAIL V14-NMP-02-NR28-04: ... [after NR 0x44<-0x01 (second half): got=0xA5 want=0xC3]
```
Three rows fail; -01 passes coincidentally because both code paths
return 0x00 at fresh-emulator state. Restore → all four PASS. ✓

### V14-NMP-03 — NR 0x2B write-only register leaks last-write byte through reads — APPROVE

**VHDL claims confirmed**:

* `zxnext.vhd:4851` — `when X"2B" => nr_2b_we <= '1';` (the only
  write-side decode entry). ✓
* `zxnext.vhd:6306-6307` — `elsif nr_2b_we = '1' then nr_keymap_addr
  <= nr_keymap_addr + 1;` (the only state effect: address
  auto-increment). ✓
* `zxnext.vhd:6321-6324` — keymap_dat side-channel routed via
  `nr_keymap_we` / `nr_joymap_we`, gated by `nr_keymap_sel`. The data
  byte is consumed by the dpram, not stored in a readable register. ✓
* `zxnext.vhd:5878-6289` — read mux case statement has NO entry for
  `X"2B"`. Reads fall through to `when others => port_253b_dat <=
  (others => '0');` at :6286-6287. ✓

**Fix correctness**:
The NR 0x2B write_handler returns 0 (was: returned `v`). Per
`NextReg::write` at nextreg.cpp:451-455, the handler's return value is
stored in `regs_[reg]`. Returning 0 ensures the cache is canonicalised
to 0, matching VHDL's `when others => (others => '0')` fall-through.
Same shape as the existing NR 0x29 fix directly above. ✓

**Discriminative test (REVERT-CHECK)**:
Reverted `return 0;` → `return v;`. Result:
```
FAIL WO-INT-2B: NR 0x2B write-only — read returns 0 (no leak of keymap data)
                [zxnext.vhd:5878-6289 others=>'0', :6306-6307 nr_2b_we]
                [wrote=0xAA got=0xAA want=0x00]
```
Restore → PASS. ✓

---

## Catalogued items

### Class-(c) — NR 0x2A dead-register cache leak — REVIEWER-PROMOTED to V14-NMP-04 (FIXED)

**Audit position**: catalogued for "a future all-dead-NR cache-zero
canonicalisation cleanup pass".

**Reviewer position**: REJECTED. Per the convergence rule ("0 pending
of any class"), and given that this is functionally identical in shape
to V14-NMP-03 (NR 0x2B), the deferral was unjustified. Promoted to
V14-NMP-04 and fixed in this review.

**VHDL oracle**:
* `zxnext.vhd:4848-4851` — write decode case statement:
  ```
  when X"28"  => nr_28_we <= '1';
  when X"29"  => nr_29_we <= '1';
  --          when X"2A"  => nr_2a_we <= '1';
  when X"2B"  => nr_2b_we <= '1';
  ```
  The NR 0x2A line is COMMENTED OUT. ✓
* `zxnext.vhd:6312-6319` — write-side process for `nr_keymap_dat_msb`
  (the only signal `nr_2a_we` would have driven) is also COMMENTED OUT
  in its entirety:
  ```
  -- process (i_CLK_28)
  -- begin
  --    if rising_edge(i_CLK_28) then
  --       if nr_2a_we = '1' then
  --          nr_keymap_dat_msb <= nr_wr_dat(0);
  --       end if;
  --    end if;
  -- end process;
  ```
* `zxnext.vhd:5878-6289` — read mux has NO entry for NR 0x2A. Reads
  must fall through to `when others => (others => '0')` at :6286-6287.
* `zxnext.vhd:6324` — `nr_keymap_dat <= nr_wr_dat;` (the original
  `nr_keymap_dat_msb & nr_wr_dat;` line is commented out at :6323),
  confirming the dpram receives only the 8-bit write byte and the
  9th-bit MSB has been dropped from the design. NR 0x2A has no
  consumer at all.

**Pre-fix C++ behaviour**:
* No write_handler → `NextReg::write` (nextreg.cpp:451-455) stored the
  raw byte in `regs_[0x2A]`.
* No read_handler → `NextReg::read` (nextreg.cpp:408-414) returned
  the cached byte.
* Result: write 0xCC to NR 0x2A, read returns 0xCC.

**Reviewer fix**: install a write_handler returning 0 (same shape as
NR 0x2B fix directly above). No subsystem dispatch since the byte has
no consumer in real hardware.

**Discriminative test row** (`WO-INT-2A` in `WO-Integration` group):
```
nr_write(emu, 0x2A, 0xCC);
const uint8_t got = nr_read(emu, 0x2A);
check("WO-INT-2A", "...", got == 0x00, "wrote=0xCC got=0xCC want=0x00");
```
Pre-reviewer-fix: `FAIL WO-INT-2A: ... [wrote=0xCC got=0xCC want=0x00]`.
Post-reviewer-fix: PASS. ✓

**Why class-(c)→class-(a) promotion is correct, not class-(d)**:
The fix is 3 lines of code in the same emulator.cpp section as the
NR 0x29/NR 0x2B sibling fixes. It does not require any new subsystem,
no architectural change to the NextReg dispatch, no half-cycle timing
work, and no API surface mutation. The audit's framing as "future
all-dead-NR cleanup pass" is incorrect — the cleanup is one register
in this exact pass, identical in all but the register number.

**Commit**: `ecdbe4a` "fix(task2-verify14-nmi-mf-port): V14-NMP-04
NR 0x2A — reviewer-promoted from deferred class-c".

### Class-(d) — NR 0x82-0x85 expbus AND-with-NR 0x86-0x89 mask — APPROVE-AS-DEFERRED

The audit's class-(d) framing is correct. VHDL :2392-2393 composes
`internal_port_enable` as `nr_8x AND nr_8y_bus_port_enable` only when
`expbus_eff_en = '1'`. Since jnext does not model the expansion bus,
`expbus_eff_en` is permanently 0, the AND term is dead, and gating on
`cached(0x82..0x85)` is functionally equivalent at the visible-from-Z80
boundary. Promoting this would require a full expansion-bus simulation
(`o_BUS_*` pins, expbus transactor model, NR 0x80 hot-key fan-in for F5/F6).
That is the same architectural escalation tier as `nr_07_cpu_speed`
bus-idle commit lag, `eff_nr_09_scanlines` per-instruction tick
granularity, and the other class-(d)s already on the convergence
ledger. ✓

### Other class-(d)s mentioned in audit (NR 0x07 cpu_speed, F5/F6 hotkeys, port_1ffd_mtr_n external pin) — APPROVE-AS-NOTED

All three are correctly framed as architectural escalations; reviewer
agrees with the deferral.

---

## Build / test summary

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON  ✓
cmake --build build -j$(nproc)                                ✓
ctest --test-dir build --output-on-failure                    38/38 PASS
./build/test/fuse_z80_test build/test/fuse                    1356/1356 PASS
./build/test/multiface_test                                    49/49 PASS
./build/test/nextreg_integration_test                         241/241 PASS
```

Test-row deltas:
* `multiface_test`: 49 rows (audit's expected post-fix).
* `nextreg_integration_test`: 241 rows (audit's 240 + 1 new WO-INT-2A
  from V14-NMP-04 reviewer-promotion).

---

## Final verdict

**APPROVE** — all 3 audit findings (V14-NMP-01/02/03) are VHDL-correct,
discriminatively tested, and the MF-MUX-01 test rewrite is justified
(the original test enshrined buggy emulator output, not VHDL truth).

The class-(c) NR 0x2A deferral was rejected per the user's "0 pending
of any class" convergence rule and fixed by the reviewer as V14-NMP-04
in commit `ecdbe4a`.

The class-(d) deferrals (NR 0x82-0x85 expbus AND-mask, NR 0x07
cpu_speed bus-idle, F5/F6 hotkeys, port_1ffd_mtr_n external pin) are
correctly framed as architectural escalations and accepted as deferred.

Net result for Pass-14:

| Finding ID  | Class | Status                                       |
|-------------|-------|----------------------------------------------|
| V14-NMP-01  | (a)   | Fixed + tested + disc-verified by reviewer   |
| V14-NMP-02  | (a)   | Fixed + tested + disc-verified by reviewer   |
| V14-NMP-03  | (a)   | Fixed + tested + disc-verified by reviewer   |
| V14-NMP-04  | (a)*  | Reviewer-promoted from class-c, fixed + disc |
| Expbus mask | (d)   | Architectural deferral approved              |

\* Promoted from audit's class-(c) deferral by reviewer.

Total: **4 class-(a) findings landed, all discriminatively tested,
0 catalogued deferrals of class lower than (d)**.
