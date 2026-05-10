# Pass-11 Blind Audit — NMI + Multiface + Port + NextREG Subsystem

**Branch**: `task2/verify11-nmi-mf-port`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify11-nmi-mf-port`
**Base**: integration HEAD `d385d5e`
**Auditor**: Pass-11 (blind — no prior-pass reports read)

## Methodology

VHDL was the oracle — only line ranges from
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` were
cited. Files in `doc/issues/nextzxos-boot/` were not opened. The audit
walked every NR write/read handler in the in-scope set
(0xC0/C2/C3/C4/C5/C6/C8/C9/CA/CC/CD/CE, 0x02, 0x06, 0x80/81, 0x0A,
0x05, 0x0B, 0x07, 0x10, 0x83, 0xD8/D9/DA, 0x00/01/0E/0F), the central
`NmiSource` arbiter, the `Multiface` four-flip-flop FSM,
`PortDispatch`, and the post-tick fan-out from peripherals into
NmiSource. Each VHDL signal's reset clause and gate vector was checked
against the C++ implementation; anomalous storage-vs-readback paths
were prioritised since prior-pass focus had largely been on the FSM
itself.

## Summary

| Class | Count |
|-------|-------|
| (a) Definite VHDL divergence — must fix | 0 |
| (b) Behavioural ambiguity / missing test | 0 |
| (c) Read-back / cache leak — observable, non-fatal | 2 |
| (d) Architectural — out-of-scope | 0 |
| **Total** | **2** |

Both findings were fixed in this branch with discriminative regression
tests appended to `test/nextreg/nextreg_integration_test.cpp`. All
fixes cite VHDL line numbers verbatim. No prior-pass reports were
consulted at any point.

## Findings

### V11-NMP-01 — NR 0x81 readback leaks `nr_81_expbus_speed` bits 1:0

**Class**: (c)
**VHDL**: `zxnext.vhd:5491-5496` (write decoder), `zxnext.vhd:6126` (read mux).

**Root cause**. The NR 0x81 write decoder at line 5491-5496 stores
each user bit verbatim — except `nr_81_expbus_speed`, which the VHDL
**hardwires to "00"** by the line:

```
nr_81_expbus_speed <= "00";   -- nr_wr_dat(1 downto 0);
```

The originally-intended user data source `nr_wr_dat(1 downto 0)` is
explicitly commented out. The read-mux at line 6126 surfaces
`nr_81_expbus_speed` in bits 1:0 of the NR 0x81 read:

```
port_253b_dat <= i_BUS_ROMCS_n & nr_81_expbus_ula_override
              & nr_81_expbus_nmi_debounce_disable & nr_81_expbus_clken
              & nr_81_expbus_fdc & '0' & nr_81_expbus_speed;
```

So bits 1:0 of any NR 0x81 read are **always** zero on real hardware,
regardless of what was written. The pre-fix C++ stored the full raw
write byte in `nr_81_` and used the read mask `0x80 | (nr_81_ & 0x7B)`
where `0x7B = 0b01111011` preserves bits 1:0 from the last write —
diverging from VHDL by leaking the dropped bits.

**Fix**. `src/core/emulator.cpp` NR 0x81 read handler — change mask
from `0x7B` to `0x78`:

```cpp
nextreg_.set_read_handler(0x81, [this]() -> uint8_t {
    return static_cast<uint8_t>(0x80 | (nr_81_ & 0x78));
});
```

`0x78 = 0b01111000` keeps only bits 6, 5, 4, 3 (the four
`nr_81_expbus_*` storage signals). Bit 7 stays forced to '1' for
`i_BUS_ROMCS_n` idle (no expbus device wired in jnext, G45). Bit 2 was
already masked out (VHDL constant '0' on the read mux). Bits 1:0
become forced to '0' to match VHDL's hardwired `nr_81_expbus_speed`.

**Discriminative tests** (`test/nextreg/nextreg_integration_test.cpp`):

- `V11-NMP-01-LO2`: write 0x03 (bits 1:0 only) → read 0x80. Pre-fix
  this was 0x83.
- `V11-NMP-01-MIXED`: write 0x41 (bits 6 + 0) → read 0xC0. Bit 6
  preserved, bit 0 dropped per :5496.
- Pre-existing `G56-CR-81-MASK` / `G56-CR-81-RT` / `TC-NR81-PRESERVE-SOFT`
  expectations updated from the old leaky mask to the new
  VHDL-faithful values:
  - write 0xFF → read 0xF8 (was 0xFB)
  - write 0x6B → read 0xE8 (was 0xEB)
  - soft-reset preservation test write 0x33 → read 0xB0 (was 0xB3)

The pre-existing tests had **enshrined the bug**. Their updates are
mandatory: they were testing the divergent behaviour, not the spec.

---

### V11-NMP-02 — NR 0x0A cache poisoning when bits 7:5 written outside `nr_03_config_mode`

**Class**: (c)
**VHDL**: `zxnext.vhd:5191-5198` (write decoder).

**Root cause**. The NR 0x0A write decoder gates its three high-bit
latches behind `nr_03_config_mode = '1'`:

```
when X"0A" =>
   if nr_03_config_mode = '1' then
      nr_0a_mf_type <= nr_wr_dat(7 downto 6);
      nr_0a_sd_swap <= nr_wr_dat(5);
   end if;
   nr_0a_divmmc_automap_en <= nr_wr_dat(4);
   nr_0a_mouse_button_reverse <= nr_wr_dat(3);
   nr_0a_mouse_dpi <= nr_wr_dat(1 downto 0);
```

Bits 7:6 (`nr_0a_mf_type`, the Multiface mode selector) and bit 5
(`nr_0a_sd_swap`) **only** commit while config_mode is set. Outside
config_mode the underlying latches retain their previous values.

Pre-fix the C++ NR 0x0A write_handler did the right thing for the
**subsystems** (it gated the `multiface_.set_mode()` /
`spi_.set_sd_swap()` calls inside the `if (config_mode)` branch), but
the cached byte stored in `regs_[0x0A]` was unconditionally the raw
`v`. The cache therefore leaked rejected bits 7:5 from
out-of-config_mode writes.

**Observable consequence**. The init / reset fan-out at
`emulator.cpp:985-996` reads `nextreg_.cached(0x0A)` and forwards bits
7:6 to `multiface_.set_mode()`. After `Emulator::reset()` the cached
byte survives (NextReg::reset preserves regs_[0x0A] per
`nextreg.cpp:252` — VHDL :1124-1128 declare these as initial-only
signals with no reset clause). A "cache-poisoning" write (out-of-
config_mode NR 0x0A write with bits 7:6 cleared) would therefore
commit a wrong Multiface mode on the next reset, even though the VHDL
latch was untouched.

**Fix**. `src/core/emulator.cpp` NR 0x0A write_handler — canonicalise
the stored byte. Inside config_mode, return the raw `v` (full latch
update). Outside config_mode, OR in the previous cached value's bits
7:5 to mirror the VHDL latches' "no change" behaviour:

```cpp
if (cfg) {
    return v;
}
const uint8_t prev = nextreg_.cached(0x0A);
return static_cast<uint8_t>((v & 0x1F) | (prev & 0xE0));
```

Bits 4:0 always come from the new write (those signals have no
config_mode gate per VHDL :5196-5198).

**Discriminative test** (`test/nextreg/nextreg_integration_test.cpp`):

- `V11-NMP-02`: in config_mode, write 0xC0 (mf_type = 11 / mode_48).
  Exit config_mode. Write 0x00 (would clear all bits if buggy). Hard
  reset via NR 0x02 = 0x02. Read NR 0x0A — bits 7:6 must still be 11
  (preserved per VHDL). Pre-fix this read was 0x00 (cache leaked the
  rejected write); post-fix it is 0xC0.

The discriminative chain exercises both the in-write canonicalisation
AND the post-reset fan-out, since both paths converge on
`cached(0x0A)`.

---

## Defence — what was checked and found correct

The audit walked every register in the in-scope NR set and every FF in
NmiSource and Multiface. The following were verified VHDL-faithful and
required no change:

- `NmiSource::nmi_assert_{mf,divmmc,expbus}` producer logic vs
  VHDL :2089-2091 (including the Pass-9 `expbus_eff_en` /
  `expbus_eff_disable_mem` gates).
- Priority arbiter latch update vs VHDL :2095-2116
  (`nmi_assert_mf AND NOT port_e3_reg(7) AND NOT divmmc_nmi_hold` for
   the MF arm; `mf_is_active` block for DivMMC).
- FSM transitions vs VHDL :2120-2162, including the END→IDLE advance
  collapse the existing comment justifies as spec-faithful at
  per-instruction tick granularity.
- `nmi_generate_n` formula vs VHDL :2168 (HOLD does NOT assert /NMI;
  the existing post-Pass-N comment correctly cites this).
- `nmi_*_button` strobes vs VHDL :2169-2170.
- NR 0x02 software-NMI strobe semantics vs VHDL :3829-3864 (combinational
  pulse always vs accept-cause-gated readback latches; auto-clear at
  S_NMI_END is correctly NOT applied — the latches survive END).
- NR 0x02 readback bit assembly vs VHDL :5891 (bit 4 = OR of
  `nr_da_iotrap_cause(1:0)`; bit 7 = `nr_02_bus_reset` latched on
  every NR 0x02 write per :5119).
- NR 0x02 reset_type FSM advance vs VHDL :1732-1739 (verified by
  enumeration: 100→010→001→001 saturating).
- `nr_da_iotrap_cause` clear conditions vs VHDL :3866-3883.
- NR 0xC0 write bit decode (vector / stackless / im_mode) and read
  mux composition vs VHDL :5597-5599 + :6230. The read correctly
  composes the live `z80_im_mode` from the IM2 controller, the
  stored `stackless_nmi` flag, and the user-written `im_mode_pulse_0_im2_1`
  bit. Bit 4 reads as constant '0' per VHDL.
- NR 0xC2/0xC3 NMI return-address shadow vs VHDL :2057-2068 + :6233-6236
  (reset-clear path covered by `regs_.fill(0)`; software write/read
  round-trip via cache; NMI-cycle capture via
  `cpu_.on_nmi_servicing → set_nmi_return_address`).
- NR 0xC4 read composition vs VHDL :6239 (line int, ULA int, expbus int).
- NR 0xC5/0xC6/0xCC/0xCD/0xCE write masks AND read masks (all match
  the VHDL `& "00000" & x` and `'0' & a & '0' & b` shapes).
- NR 0xC8/0xC9/0xCA write→clear and read→status mappings vs the bit
  layout inferred from VHDL :1952-1955 + :6247-6254.
- NR 0xD8/0xD9/0xDA reset clause and write/read masks vs VHDL :5640
  + :6266 + :3866-3898 (including the trap-event gate
  `nmi_accept_cause` for the NR 0xD9 capture path at port 0x3FFD WR).
- NR 0x06 read composition with the `nr_06_ps2_mode` config_mode
  gate vs VHDL :5167-5169 + :5900.
- NR 0x05 read composition with the Pentagon mode 5060-zero override
  per VHDL :5832-5841 (Pass-10 fix held).
- NR 0x07 read composition (cpu_speed | nr_07_cpu_speed) vs VHDL :5903.
- NR 0x80 / NR 0x81 expansion-bus enable / disable_mem fan-out into
  NmiSource (Pass-9 fix held) vs VHDL :5800-5813 + :2089.
- Multiface entity end-to-end vs `device/multiface.vhd:103-197`:
  `reset = reset_i OR NOT enable_i` modelled by held-reset short-
  circuit; mode decode "00"/"11"/"others"; `port_io_dly` edge-detector;
  `nmi_active` priority cascade; `invisible` priority cascade with
  `mode_p3` polarity flip; `mf_enable` four-way priority; `fetch_66`
  combinational with one-cycle bypass via `mf_enable_eff`; `mf_port_en`
  combinational gate.
- Multiface +3 / MF128 readback mux at port LSBs 0x3F/0xBF/0x9F vs
  VHDL :4310-4322 (case mux on cpu_a(15:12) for MF+3, single-bit
  shadow surface for MF128 var A/B, mode_48 path bypassed via
  `invisible_eff` gate).
- Port-dispatch IO observer model for the parallel Multiface decode
  vs VHDL :2610-2616 + :2615 (`port_multiface_io_en` AND gate).
- Most-specific-mask-wins precedence in `PortDispatch` vs VHDL one-hot
  decode (verified by inspection of the read fall-through logic).
- Floating-bus default for unmatched reads.

## Tests passing (post-fix)

- ctest: 38/38 PASS.
- FUSE Z80 opcode suite: 1356/1356 PASS.
- Regression suite: pre-existing worktree-infrastructure failures
  (parallax-demo, video-record-func, rzx-record/playback) reproduce
  on the **pre-change baseline** of this worktree (verified by
  `git stash`-ing the changes and re-running). The failures are
  caused by the worktree not having the `build/gui-release/jnext`
  release-build that the regression script prefers — main has it,
  worktree does not, so worktree falls back to `build/jnext` (debug)
  which has timing differences large enough to drift parallax-demo's
  scrolling phase by one frame at the screenshot capture point. None
  of the failures involve NMI / Multiface / NR-port code, and none
  are present after my fix that weren't present before.

## Commit

To be filled after `git commit` runs.
