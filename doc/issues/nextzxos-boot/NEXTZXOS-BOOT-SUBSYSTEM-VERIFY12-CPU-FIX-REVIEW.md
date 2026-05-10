# Pass-12 CPU Subsystem — Fix-of-Reviewer Review

- **Subsystem**: CPU / Z80N / IM2
- **Pass**: 12 (fix-of-reviewer cycle)
- **Reviewer worktree**: `.claude/worktrees/task2-verify12-cpu-z80n-im2-fix-reviewer`
- **Branch**: `task2/verify12-cpu-z80n-im2-fix-reviewer` (forked from `task2/verify12-cpu-z80n-im2` @ `bb79be2`)
- **HEAD reviewed**: `bb79be2`
- **Verdict**: **APPROVE**

## Scope

Two NIT fixes promoted from the Pass-12 reviewer:

| ID            | Commit    | Subject                                                                                  |
| ------------- | --------- | ---------------------------------------------------------------------------------------- |
| V12-CPU-NIT-01 | `2cc8453` | Stale comment at `z80_cpu.cpp:594-596` — LDPIRX listed under non-F-writing group         |
| V12-CPU-NIT-02 | `a57282c` | OUTINB extended-M1 1T tail uses raw `tstates += 1` instead of `contend_read_no_mreq(IR,1)` |

## V12-CPU-NIT-01 — LDPIRX comment grouping

### Diff inspection (commit `2cc8453`)

The Q-semantics block at `src/cpu/z80_cpu.cpp:590-612` was rewritten:
- Pre-fix listed LDPIRX in the non-F-writing group inline with SWAPNIB / MIRROR_A / MUL_DE / etc.
- Post-fix splits the list into explicit "F-writing" and "Non-F-writing" sub-groups.
- LDPIRX is now correctly grouped with `LDIX, LDWS, LDDX, LDIRX, LDDRX, LDIRSCALE` in the F-writing block, with an inline note that it joined this group at Pass-10 per VHDL `t80n.vhd:1277-1289` I_BT block-transfer flag composition.

### Cross-check: does LDPIRX actually write F?

`src/cpu/z80n_ext.cpp:874-881` (LDPIRX implementation):

```cpp
uint8_t f = static_cast<uint8_t>(regs.AF & 0xFF);
f &= (FLAG_S | FLAG_Z | FLAG_C);   // preserve S, Z, C
if (alu_q & 0x08)   f |= FLAG_X;   // X = ALU_Q[3]
if (alu_q & 0x02)   f |= FLAG_Y;   // Y = ALU_Q[1]
// H = 0, N = 0 (left clear)
if (regs.IncDecZ)   f |= FLAG_P;   // P = IncDecZ (I_BC/I_BT override)
regs.AF = (regs.AF & 0xFF00) | f;
regs.Q = f;                        // track last F write
```

Confirmed: LDPIRX both updates `regs.AF` low byte and assigns `regs.Q = f`. The Pass-10 fix moved LDPIRX into the F-writing group; the Pass-12 NIT corrects the trailing stale comment.

### VHDL oracle

`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n_mcode.vhd:1962-1991` (LDPIRX MCycle 3) sets `I_BT <= '1'` — this triggers the I_BT flag composition block at `t80n.vhd:1277-1289`, which forces F.X/F.Y/F.H/F.N/F.P. Comment is correct.

### Verdict V12-CPU-NIT-01

**APPROVE.** Comment now accurately reflects post-Pass-10 functional code. No discriminative test needed (comment-only change).

## V12-CPU-NIT-02 — OUTINB extended-M1 contend-no-MREQ

### Diff inspection (commit `a57282c`)

Pre-fix shape (`src/cpu/z80n_ext.cpp` OUTINB case):

```cpp
auto regs = cpu.get_registers();
uint8_t temp = fuse_z80_readbyte(regs.HL);
uint16_t port = regs.BC;
fuse_z80_writeport(port, temp);
regs.HL = (regs.HL + 1) & 0xFFFF;
cpu.set_registers(regs);
tstates += 1;            // <-- raw, bypasses contention gate
return 16;
```

Post-fix shape:

```cpp
auto regs = cpu.get_registers();
uint16_t ir = (static_cast<uint16_t>(regs.I) << 8)
            | static_cast<uint16_t>(regs.R);
contend_read_no_mreq(ir, 1);   // <-- traverses contention gate, BEFORE operand read
uint8_t temp = fuse_z80_readbyte(regs.HL);
uint16_t port = regs.BC;
fuse_z80_writeport(port, temp);
regs.HL = (regs.HL + 1) & 0xFFFF;
cpu.set_registers(regs);
return 16;
```

### FUSE oracle

`third_party/fuse-z80/z80_ed.c:330-348` (OUTI):

```c
case 0xa3:		/* OUTI */
  {
    libspectrum_byte outitemp, outitemp2;

    contend_read_no_mreq( IR, 1 );      // <-- BEFORE readbyte(HL)
    outitemp = readbyte( HL );
    B--;
    z80.memptr.w = BC + 1;
    writeport(BC,outitemp);
    ...
```

The fix mirrors the FUSE OUTI pattern exactly:
- IR (refresh pair) used as address.
- `contend_read_no_mreq(IR, 1)` emitted BEFORE `readbyte(HL)`.
- Total cycles unchanged: 16T (= 8 (M1 ED+inner) + 3 (mem read HL) + 4 (port write IORQ) + 1 (extended M1)).

### VHDL oracle

`t80n_mcode.vhd:2516-2530`:

```vhdl
when "10100011" | "10101011" | "10110011" | "10111011"
   -- OUTI, OUTD, OTIR, OTDR

   | x"90" => -- OUTINB

   ...

   case to_integer(unsigned(MCycle)) is
   when 1 =>
      TStates <= "101";          -- 5T extended M1
      Set_Addr_To <= aXY;        -- aXY at MCycle 1 with no XY-state = RegBusC
      Set_BusB_To <= "1010";
      Set_BusA_To <= "0000";
   ...
```

OUTINB shares the same `when`-clause as OUTI/OUTD/OTIR/OTDR — they all emit `TStates <= "101"` (=5T) at MCycle 1. The standard 4T M1 + 1T extension. Per `zxula.vhd:582-600` the contention gate fires on `(hc_adj × vc × contention_en)` regardless of MREQ, so the extended T must traverse the gate.

`t80n.vhd:566-576` shows `aXY` at MCycle 1 with `XY_State="00"` and `NextIs_XY_Fetch='0'` resolves to `A <= TmpAddr`, where `TmpAddr` carries the M1 refresh pair (IR). The fix's choice of `IR` for the contention address is VHDL-faithful.

### Discriminative test verification

New test in `test/cpu/cpu_z80n_im2_regressions_test.cpp:1535-1618`:
`V12-CPU-NIT-02-Z80N-OUTINB-EXTENDED-M1-CONTEND-NO-MREQ`

Two ZX48K-contended fixtures, identical except for `I` (and therefore IR routing):
- Fixture A: `I=0x40` → IR=0x40NN ∈ slot 2 (page 0x0A — ZX48K-contended).
- Fixture B: `I=0x80` → IR=0x80NN ∈ slot 4 (page 0x10 — non-contended).

With fix: A's IR T-state traverses the contention gate on a contended bank → emits per-cycle stretch. B's traverses the gate on a non-contended bank → no stretch. `delta_A > delta_B`.

Pre-revert verification protocol:

1. Reverted the fix in the worktree (replaced `contend_read_no_mreq(ir, 1)` with `tstates += 1` at the case tail).
2. Rebuilt `cpu_z80n_im2_regressions_test`.
3. Ran the test — output:

   ```
   [FAIL] V12-CPU-NIT-02-Z80N-OUTINB-EXTENDED-M1-CONTEND-NO-MREQ  OUTINB total: A (IR=0x40NN, slot 2 contended)=18, B (IR=0x80NN, slot 4 non-contended)=18, delta=0 (expect > 0 …)
   ```

   **Confirmed: delta=0 pre-revert.** The test discriminates the fix.

4. Restored the fix → test PASSES.

### Verdict V12-CPU-NIT-02

**APPROVE.** Fix is VHDL- and FUSE-faithful, structurally aligned with the unified VHDL when-clause for OUTI/OUTD/OTIR/OTDR/OUTINB. Discriminative test passes post-fix and fails pre-revert with delta=0 — the test correctly isolates the IR no-MREQ stretch contribution.

## Hunt: other extended-M1 contention gaps in Z80N opcodes

Audit method: scanned `t80n_mcode.vhd` for `TStates <= "101"` and cross-referenced each match against `src/cpu/z80n_ext.cpp` to determine whether the corresponding T-state traverses the contention gate.

| VHDL line | Opcode(s)                         | MCycle | jnext handling                                                                 | Status |
| --------- | --------------------------------- | ------ | ------------------------------------------------------------------------------ | ------ |
| 1654-1666 | LD A,I / LD A,R / LD I,A / LD R,A | 1      | FUSE-handled (standard Z80, not in z80n_ext.cpp). 1356/1356 PASS.              | OK     |
| 1306      | RET cc                            | 1      | FUSE-handled. 1356/1356 PASS.                                                  | OK     |
| 1322      | RST p                             | 1      | FUSE-handled. 1356/1356 PASS.                                                  | OK     |
| 1979      | LDPIRX                            | 3      | `fuse_z80_writebyte` (real write) or 3× `contend_write_no_mreq` (suppressed). | OK     |
| 1989      | LDPIRX                            | 4      | `for (i=0..internal_idle) contend_write_no_mreq(de_pre_inc, 1)`               | OK     |
| 2118      | LDIX/LDDX/LDIRX/LDDRX             | 3      | `fuse_z80_writebyte` or 3× `contend_write_no_mreq`                            | OK     |
| 2137      | LDIX/LDDX/LDIRX/LDDRX             | 4      | per-T `contend_write_no_mreq(de_pre_inc, 1)` loop                             | OK     |
| 2215      | LDIRSCALE                         | 3      | `fuse_z80_writebyte` or 3× `contend_write_no_mreq`                            | OK     |
| 2225      | LDIRSCALE                         | 4      | per-T `contend_write_no_mreq` loop                                            | OK     |
| 2246      | LDDX/LDDRX                        | 3      | `fuse_z80_writebyte` or 3× `contend_write_no_mreq`                            | OK     |
| 2254      | LDDX/LDDRX                        | 4      | per-T `contend_write_no_mreq` loop                                            | OK     |
| 2528-2530 | OUTI/OUTD/OTIR/OTDR/**OUTINB**    | 1      | OUTI/OUTD/OTIR/OTDR FUSE-handled (`contend_read_no_mreq(IR,1)`); **OUTINB now matches via `a57282c`** | OK (this fix) |

**No other Z80N extended-M1 contention gap.** All `TStates <= "101"` markers at MCycles touching jnext-managed Z80N implementations already route through `contend_read_no_mreq` / `contend_write_no_mreq` at the appropriate position.

### Other `tstates +=` residuals (out of scope)

For completeness, z80n_ext.cpp still contains a few `tstates += N` residuals at:
- `ADD_HL_NN/ADD_DE_NN/ADD_BC_NN`: `+= 2` (case tail) — these are NOT TStates="101" extensions; they account for the residual after FUSE-handled M1+M2+M3 cycles totaling ~14T to reach the 16T spec.
- `PUSH_NN`: `+= 3` — same shape, residual.
- `NEXTREG_NN/NEXTREG_A`: `+= 6` — residual covering MCycles 4-5 (NextReg fabric strobes, no MREQ/IORQ on external pins).

Per VHDL `t80n_mcode.vhd:1850-1882` (ADD_*_NN), `1924-1949` (PUSH_NN), and `1668-1709` (NEXTREG_NN/A) none of these MCycles set `TStates <= "101"`. They're standard 4T or 3T cycles with the extra accounting reflecting bookkeeping of internal-state cycles rather than missed extended-M1 cycles. Migrating them to per-T `contend_*_no_mreq` would be a separate architectural refinement (potential future class-(c)), but is **not** an extended-M1 gap and is **out of scope for V12-CPU-NIT-02**.

## Test results (HEAD `bb79be2`, fix restored)

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
... configure done

$ cmake --build build -j$(nproc)
[100%] Built target jnext

$ ctest --test-dir build --output-on-failure
... 38/38 PASS
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

ctest 38/38 PASS, FUSE 1356/1356 PASS, no regressions.

## Summary

| Item                                            | Result                          |
| ----------------------------------------------- | ------------------------------- |
| V12-CPU-NIT-01 comment correctness              | APPROVE                         |
| V12-CPU-NIT-02 fix VHDL/FUSE fidelity           | APPROVE                         |
| V12-CPU-NIT-02 discriminative test pre-revert   | FAIL with delta=0 (as expected) |
| V12-CPU-NIT-02 discriminative test post-restore | PASS                            |
| ctest                                           | 38/38 PASS                      |
| FUSE Z80                                        | 1356/1356 PASS                  |
| Other extended-M1 gaps                          | None found                      |

**Final verdict: APPROVE.**
