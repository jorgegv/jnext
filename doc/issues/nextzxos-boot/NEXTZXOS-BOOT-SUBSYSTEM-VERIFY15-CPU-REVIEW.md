# Pass-15 CPU Subsystem Audit — Independent Review

**Reviewer branch**: `task2/verify15-cpu-z80n-im2-reviewer` (off audit
HEAD `c461458`)
**Review HEAD (post-fix)**: see commit `1c30aa5` (V15-CPU-NIT-03 fix)
**Tests at HEAD**: ctest 38/38 PASS, FUSE Z80 1356/1356 PASS

## Verdict

**APPROVE-WITH-NITS** with one NIT promoted-and-fixed by reviewer.

* **NIT-1** (`DD ED <Z80N>` not dispatched as Z80N): re-classified as
  **class-(d) ARCHITECTURAL** — VHDL discriminative but no real-software
  triggers; fix would require non-trivial FUSE/jnext interface changes
  to propagate DD/FD prefix state into Z80N decode.
* **NIT-2** (`on_m1_prefetch` fires only on first M1 byte): re-classified
  as **class-(d) ARCHITECTURAL** — VHDL discriminative but practical
  attack surface is tiny, and a proper fix requires plumbing per-byte
  prefetch ordering through `fuse_z80_execute_one()` and the z80n_ext
  contend_read pair. See "Per-NIT resolution" below for the depth analysis.
* **NIT-3** (ULA+ port contention not propagated from CPU side):
  **REJECTED as missed finding, FIXED BY REVIEWER**. Audit's deflection
  to "memory subsystem boundary" is incorrect — per
  zxnext.vhd:4496, `port_bf3b`/`port_ff3b` are CPU contention OR-terms,
  exactly mirroring the `port_7ffd_active` pattern that Verify9-memory
  already wired through. Same fix shape applied here. Discriminative
  regression test added.

CPU subsystem **may converge** despite the promoted NIT-3, because
NIT-3 was actually a missed finding the audit dismissed — but the
reviewer fixed it inline (with a regression test pair) on the
reviewer branch. Convergence is contingent on the manager merging
this reviewer branch back into integration. NIT-1 and NIT-2 are
acceptable as class-(d) only because their real-world manifestation
is bounded to "supervisor never executes a multi-byte instruction
that puts an inner-byte M1 fetch onto an RST entry vector" and
"no real software writes `DD ED <Z80N opcode>`", neither of which
is a class-(c) "intentional gap" — they are honest architectural
classifications backed by VHDL behaviour analysis.

## Per-angle table

| Angle | Audit claim                                  | Reviewer assessment | VHDL/FUSE oracle citation |
|-------|----------------------------------------------|---------------------|---------------------------|
| A.1   | IncDec_16(2:0)="100" latch coverage          | CORRECT             | t80n.vhd:1361-1367 |
| A.2   | Z80N I_BT flag composition (LDPIRX joined)   | CORRECT             | t80n.vhd:1277-1289 |
| A.3   | DJNZ shadow polarity (V13-CPU-01 stable)     | CORRECT             | t80n.vhd:1358-1359 |
| B     | Z80N opcode coverage (30+ walk)              | CORRECT — 30 cases verified in z80n_ext.cpp:142-944 | t80n.vhd:700-1140 |
| C.1   | OUTINB 1T extended-M1 (V12-CPU-NIT-02)       | CORRECT             | t80n_mcode.vhd:2528-2530 |
| C.2   | Z80N M1 contention via contend_read pair     | CORRECT             | z80_cpu.cpp:580-581; FUSE opcodes_base.c:1075 |
| D.1   | EI grace before Z80N opcode (Pass-8)         | CORRECT — gate at z80_cpu.cpp:479-481 | FUSE z80.c interrupts_enabled_at |
| D.2   | EI grace timing (FUSE-equivalent)            | CORRECT             | (same) |
| E.1   | NMI from HALT (G88, +1 saved_pc)             | CORRECT — z80_cpu.cpp:425-427 | t80n.vhd:557-559 |
| E.2   | INT from HALT (FUSE handles internally)      | CORRECT             | FUSE z80.c |
| E.3   | HALT-loop INT acceptance fidelity (P)        | CORRECT             | im2_control.vhd:158-209 |
| F     | DD/FD/CB inner-byte M1 delivery (Pass-9)     | CORRECT — z80_cpu.cpp:729-764 walk verified | im2_control.vhd:158-209 |
| G.1   | compute_vector composition                   | CORRECT             | zxnext.vhd:1999 |
| G.2   | ack_vector S_REQ→S_ACK on IEI                | CORRECT             | im2_device.vhd:111-116 |
| G.3   | IEI snapshot (no cascade)                    | CORRECT             | (synchronous-update VHDL semantic) |
| G.4   | RETI propagation (Pass-10)                   | CORRECT             | im2_control.vhd:233-234 |
| H     | Q register hygiene per Z80N opcode           | CORRECT             | FUSE Q convention |
| I.1   | R-reg increment +2 for Z80N (ED+ext)         | CORRECT — z80_cpu.cpp:586 | (Z80 architecture) |
| I.2   | Non-Z80N ED R-incrementing (delegated FUSE)  | CORRECT             | opcodes_base.c:1075 |
| I.3   | DD/FD/CB chain R++ via FUSE goto end_opcode  | CORRECT             | (FUSE re-dispatch path) |
| J     | LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR INT  | CORRECT             | FUSE PC-=2 rewind |
| K     | PIXELDN bit-extraction (V11-CPU-01)          | CORRECT — discriminative HL=$5FE0 case verified | t80n.vhd:900-921 |
| L     | PIXELAD bit-composition                      | CORRECT             | t80n.vhd:939-947 |
| M     | Contention table per-machine                 | CORRECT             | zxula.vhd:582-600 |
| N     | Turbo (NR $07) contention emission gate      | CORRECT             | zxnext.vhd:4481 |
| O     | CPU clock change mid-instruction             | CORRECT             | (per-call shadow consult) |
| P     | HALT-loop INT acceptance + IM2 FSM           | CORRECT             | im2_control.vhd FSM |
| **NEW Reviewer angles below** | | | |
| Q     | NR 0x85 bit 0 / port_ulap_io_en propagation  | **MISSED** (audit deflected) — CPU-side seam drops the gate; fixed by V15-CPU-NIT-03 | zxnext.vhd:2439, 2685-2686, 4496 |
| R     | Multi-byte M1 inner-byte coverage on `on_m1_prefetch` | CONFIRMED PARTIAL — audit's NIT-2 stands as class-(d); inner-byte M1 fetches at RST/MF entry vectors are missed (rare, bounded) | divmmc.vhd:128, multiface.vhd:169-186 |
| S     | DD/FD prefix into Z80N (NIT-1)               | CONFIRMED ACADEMIC — class-(d) accepted; Z80N opcodes use Alternate (EXX), not XY_State, so DD's effect is null on the dispatched op | t80n.vhd:516-519, 700-1140 |

## Per-NIT resolution

### NIT-1: `DD ED <Z80N opcode>` not dispatched as Z80N

**Resolution**: class-(d) ARCHITECTURAL.

**Rationale**: The audit's analysis is correct. Per VHDL t80n.vhd:516-519,
DD prefix sets `XY_State` only (01=IX, 10=IY); it does NOT alter `ISet`.
The following ED then sets `ISet="10"`, dispatching the inner byte
through Z80N case statements at t80n.vhd:700-1140. Inside the Z80N
case block, register selection uses `Alternate & dHL/dDE/dBC` (lines
720-725, 768-775) — the EXX state, NOT `XY_State`. Therefore DD's
XY_State has no observable effect on any dispatched Z80N opcode.

The C++ side (z80_ddfd.c default branch in FUSE) backtracks PC/R and
re-dispatches the ED+ext bytes through the main switch — which lacks
Z80N opcodes. So `DD ED <Z80N>` is treated as NOPD plus a re-dispatch
of `ED <Z80N>` minus the Z80N intercept (because the inner-ED path
went through FUSE first).

The fix would require: capturing DD/FD prefix state in jnext's
top-level execute() before delegating to FUSE, and re-routing the
inner ED to Z80N decode if applicable. This is non-trivial and
well-defined as architectural; no real-software writes this sequence
(supervisor and NextZXOS code don't), and FUSE compliance (1356/1356)
must not regress. Class-(d) is the honest classification.

### NIT-2: `on_m1_prefetch` fires only for first M1 byte

**Resolution**: class-(d) ARCHITECTURAL.

**Rationale**: Per VHDL divmmc.vhd:128 and multiface.vhd:169, the
automap/MF FSMs sample on **every** M1 cycle (`i_cpu_mreq_n='0' AND
i_cpu_m1_n='0'`), including inner-byte M1s of multi-byte instructions
(DD/FD/ED/CB prefix bytes are themselves M1 cycles per Z80
architecture). jnext's `on_m1_prefetch` (at z80_cpu.cpp:520) fires
only for the first byte of an instruction — so inner-byte M1 cycles
are NOT propagated to:
* `divmmc_.check_automap()` — entry-point detection
* `nmi_source_.observe_m1_fetch()` — NMI source pipeline
* `multiface_.on_m1()` — MF FSM (cpu_a_0066_i)

Practical attack surface (manually walked):
* DivMMC RST entry detection requires inner-byte M1 PC to land on an
  RST vector (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38). Real
  ROM code aligns instructions; the only way to land an inner M1 on
  an RST vector is to put a prefix byte one byte before. NextZXOS,
  TBBlue.fw and supervisor don't do this.
* Multiface entry requires inner-byte M1 PC = 0x0066 with MF armed.
  Same alignment argument. The MF NMI hardware pre-aligns: pressing
  the MF button drops a forced JP $0066 into PC via the FPGA, so the
  $0066 first-byte M1 fetch always observes alignment.

Fix complexity (architectural):
* The `on_m1_cycle` walk in z80_cpu.cpp:729-764 already mirrors the
  per-byte M1 sequence for IM2 RETI/RETN detection. A symmetric
  walk would need to be inserted before each inner-byte's actual
  memory read, and ORDERING vs `mem_.read()` matters: DivMMC's
  memory overlay must activate BEFORE the same byte's read.
* For the Z80N path (z80_cpu.cpp:524+), the inner ED-byte read
  happens at line 525 (`mem_.read(pc + 1)`) — the existing
  `on_m1_prefetch(pc)` at line 520 fires only for the first byte.
  An inner-byte-prefetch hook would need to slot between the first
  read and the inner read.
* For the non-Z80N path (FUSE delegate), `fuse_z80_execute_one()`
  internally reads inner bytes via `contend_read(PC, 4)` macros. A
  per-byte prefetch hook would require either intercepting the
  contend_read macro (the CORETEST mechanism already does this
  for contention) or adding callback firing inside the FUSE
  source — a significant refactor.

Both rationale items support class-(d): the bug is real per VHDL,
but the practical impact is bounded to pathological code that no
supervisor/NextZXOS path exercises, and the proper fix spans the
CPU↔FUSE boundary. Honest architectural classification.

### NIT-3: ULA+ port contention not propagated from CPU side

**Resolution**: REJECTED as missed finding, **FIXED BY REVIEWER**.

**Rationale for rejection**: Audit's claim that this is a "memory
subsystem boundary issue" is incorrect. Per VHDL zxnext.vhd:4496:

```vhdl
port_contend <= (not cpu_a(0)) or port_7ffd_active or port_bf3b or port_ff3b;
```

`port_contend` is the input gate to `o_cpu_contend` (zxula.vhd:595)
and to `o_cpu_wait_n` for +3 (zxula.vhd:600) — i.e. it's the CPU
contention output. The four OR-terms (even-port, port_7ffd_active,
port_bf3b, port_ff3b) are all part of the CPU contention bus. The
`port_7ffd_active` term has been correctly wired through to a
ContentionModel shadow (`port_7ffd_io_en_`) by the Verify9-memory
class-(a) fix. The `port_bf3b`/`port_ff3b` terms (gated by
`port_ulap_io_en` per VHDL :2439, derived from NR 0x85 bit 0) are
the EXACT same pattern, just for a different gate input. Treating
them differently from `port_7ffd_active` (one as CPU-subsystem
wiring, the other as "memory-subsystem boundary") is inconsistent.

**Fix applied (commit `1c30aa5`)**:
* `ContentionModel` grew `port_ulap_io_en_` shadow + setter (mirrors
  `port_7ffd_io_en_` exactly).
* `contention_tick()` OR-folds the parameter and the shadow so
  CPU-side callers (`fuse_z80_readport`/`writeport` in z80_cpu.cpp)
  no longer drop the gate when they call without the parameter.
* Emulator's `init()` seeds the shadow from `nextreg_.cached(0x85)
  & 0x01` (NR 0x85 bit 0).
* Emulator installed an NR 0x85 write handler that calls
  `set_port_ulap_io_en(...)` to keep the shadow live.
* 5 new regression rows in `test/contention/contention_test.cpp`
  group `CT-CAT28-V15`. Rows 01-02 are discriminative: pre-fix
  `stretch=0`, post-fix `stretch>0` for IORQ@$BF3B/$FF3B with
  NR 0x85 bit 0 = 1 inside the active raster window.

**Discrimination verified**: temporarily reverted the OR-fold and
ran tests:
```
FAIL V15-CPU-NIT-03-01: ... [stretch=0]
FAIL V15-CPU-NIT-03-02: ... [stretch=0]
```
Restored fix and re-ran: 5/5 pass. ctest 38/38 PASS, FUSE 1356/1356.

## Spot-check details (5 concrete VHDL+C++ pairs)

### Spot 1 — IncDecZ latch on INC/DEC BC (Angle A.1)

* **VHDL** (t80n.vhd:1361-1367):
  ```vhdl
  if (TState = 2 or (TState = 3 and MCycle = "001")) and IncDec_16(2 downto 0) = "100" then
     if ID16 = 0 then IncDecZ <= '0'; else IncDecZ <= '1'; end if;
  end if;
  ```
* **C++** (z80_cpu.cpp:777-849, V14-CPU-01 walk + DD/FD prefix coverage at z80_cpu.cpp:780+).
* **Verdict**: matches. The audit's claim that V14-CPU-01 + V14-CPU-NIT-01 closed this family is correct.

### Spot 2 — IM2 vector composition (Angle G.1)

* **VHDL** (zxnext.vhd:1999):
  ```vhdl
  im2_vector <= nr_c0_im2_vector & im2_vec & '0';
  ```
  3+4+1 = 8 bits: `[nr_c0[2:0], im2_vec[3:0], 0]`.
* **C++** (im2.cpp:1043):
  ```cpp
  return ((vector_base_msb3_ & 0x07) << 5) | (idx << 1);
  ```
  Bits [7:5] = `vector_base_msb3_`, bits [4:1] = `idx`, bit [0] = 0.
* **Verdict**: matches.

### Spot 3 — IM2 RETI/RETN decode FSM (Angle G.4)

* **VHDL** (im2_control.vhd:84, 158-206, 234-236):
  * State alphabet: `{S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4, S_CB_T4, S_SRL_T1, S_SRL_T2, S_DDFD_T4}`.
  * S_0 → S_ED_T4 on `ifetch_fe_t3='1' AND opcode_ed='1'`.
  * S_ED_T4 → S_ED4D_T4 on opcode_4d (= ED 4D inner byte = RETI).
  * S_ED_T4 → S_ED45_T4 on opcode_45 (= ED 45 inner byte = RETN).
  * `o_reti_seen <= '1' when state_next = S_ED4D_T4 else '0';`
  * `o_retn_seen <= '1' when state_next = S_ED45_T4 else '0';`
* **C++** (z80_cpu.cpp:543-545 — Z80N path, :665-666 — non-Z80N ED path):
  Both fire `on_m1_cycle` for the ED prefix byte AND the inner byte, advancing the FSM through both transitions.
* **Verdict**: matches per Pass-9 / G87 fix.

### Spot 4 — PIXELDN truncation (Angle K)

* **VHDL** (t80n.vhd:900-921): composes `H[4:3] & L[7:5] & H[2:0]` (8-bit), adds 1 with **8-bit truncation** (carry from bit 7 lost), re-extracts.
* **C++** (z80n_ext.cpp:486-510): exact 8-bit composite + 1, truncated to uint8_t, re-extracts.
* **Discriminative case (audit-cited)**: HL=$5FE0 (b=11, R=111, C=111). composite=$FF, +1 wraps to $00; new HL=$4000 (H[7:5]=010 preserved). Audit verified.
* **Verdict**: matches V11-CPU-01 fix.

### Spot 5 — Z80N M1 contention via contend_read pair (Angle C.2)

* **VHDL** (t80n.vhd, Z80N M1 cycles): each prefix byte is its own M1 cycle, gated through the (hc, vc) contention window.
* **FUSE oracle** (opcodes_base.c:1075): main-switch `case 0xed` does `contend_read(PC, 4)` — i.e. emits 4T M1 contention for the ED byte, then dispatches to `case 0xed` inner switch which does another `contend_read(PC, 4)` for the inner byte.
* **C++** (z80_cpu.cpp:580-581):
  ```cpp
  contend_read(pc, 4);
  contend_read(static_cast<uint16_t>((pc + 1) & 0xFFFF), 4);
  ```
* **Verdict**: matches Pass-5 fix exactly.

## Missed-findings hunt (beyond 16 angles)

Walked the following angles independently. Each is reported below as
**clean** or **flagged**. All flagged items are surfaced as findings.

* **Reviewer angle Q — NR 0x85 / port_ulap_io_en**: **FLAGGED**, fixed
  inline as V15-CPU-NIT-03 (see above).
* **Reviewer angle R — `on_m1_prefetch` per-byte coverage**: **FLAGGED**,
  classified as class-(d) (see NIT-2 resolution).
* **Reviewer angle S — DD prefix into Z80N**: clean (per audit's NIT-1
  analysis; agreed class-(d)).
* **Reviewer angle T — LOOP opcode**: clean. Audit said "Not implemented
  in FPGA". Confirmed: `grep "LOOP" t80n.vhd t80n_pack.vhd` returns no
  hits. C++ treats as 8T NOP — matches `not implemented` semantic.
* **Reviewer angle U — Z80N opcode table coverage**: clean. Counted 30
  cases in z80n_ext.cpp:142-944, matches audit's "30+" claim.
* **Reviewer angle V — R register sync via FUSE r7 high-bit**: clean.
  z80_cpu.cpp:334 packs `(z80.r7 & 0x80) | (z80.r & 0x7f)` — preserves
  the bit-7 latched-by-LD-R semantic.
* **Reviewer angle W — DMA-inhibit during IntAck**: out of audit scope
  (DMA subsystem). Not a CPU-subsystem finding.

## Test status (review HEAD)

```
ctest --test-dir build:                    38/38 PASS
./build/test/fuse_z80_test build/test/fuse: 1356/1356 PASS
```

5 new V15-CPU-NIT-03 regression tests under `CT-CAT28-V15`. Two
discriminative; pre-fix shows `FAIL ... [stretch=0]`, post-fix
all pass.

## Outstanding work

* Manager merges `task2/verify15-cpu-z80n-im2-reviewer` into integration
  with the audit branch, capturing both the audit report and this review
  + V15-CPU-NIT-03 fix.
* NIT-1 and NIT-2 remain class-(d) ARCHITECTURAL, requiring user
  authorization for an architectural-effort task before implementation.
  These do NOT block CPU subsystem convergence, since the practical
  attack surface for both is bounded to code paths supervisor and
  NextZXOS demonstrably do not exercise.
