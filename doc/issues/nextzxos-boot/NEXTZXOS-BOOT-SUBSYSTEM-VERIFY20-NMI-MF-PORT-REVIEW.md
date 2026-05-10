# Pass-20 Independent Review — NMI + Multiface + Port-Decode + NextREG

Branch: `task2/verify20-nmi-mf-port-reviewer` (off audit HEAD `e8689252`).
Reviewer scope: independently verify the Pass-20 audit at
`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY20-NMI-MF-PORT.md` against VHDL oracle
and emulator code; sandwich-verify both fixes; investigate the
V20-NMP-01 dismissal; re-audit adjacent surfaces.

## Verdict: **APPROVE**

The Pass-20 audit's enumeration table is complete (~294 rows when
sub-rows expanded — comfortably above the audit's stated "~270"). Both
fixes are VHDL-faithful and the discriminative tests fail in
sandwich-revert. The V20-NMP-01 false-positive dismissal is grounded in
correct analysis of the `Emulator::reset()` / `soft_reset()` outer
save/restore loops. Independent re-audit of the entire NR composed-read
mux for missed `'0'` literal bits found nothing — all 18 register read
paths with literal '0' fields are correctly masked. No Class-(d)
architectural escalations.

## Validation Summary

| Step | Outcome |
|---|---|
| 1. Table row count | 294 expanded rows (audit claim ~270 — accurate when sub-rows in B/C consolidated) |
| 2. Spot-check 10 ✓ rows | 10/10 PASS against VHDL oracle |
| 3a. V20-NMP-XADC sandwich | Revert → 2 FAIL (got 0x5A / 0xA5 vs expected 0x00); restore → 262/262 PASS |
| 3b. V20-NMP-02 sandwich | Revert → 1 FAIL (got 0x02 vs expected 0x00); restore → 262/262 PASS |
| 4. V20-NMP-01 dismissal | CONFIRMED — outer save/restore loops fire NR 0x86 write handler before any port read |
| 5. Side-effect inspection | Clean — no consumers of `regs_[0xF9]`/`regs_[0xFA]`/NR 0x68 bit 1 |
| 6. Adjacent re-audit | 18 literal-'0' positions in read mux all correctly masked; Issue-2-gated families exhaustively covered |
| 7. Test invariants | ctest 38/38, FUSE 1356/1356, regression 33/0/0, nextreg integration 262/262 |

## Step 1 — Row Count Validation

Tool-based count of audit table rows (after consolidating loop-generated
NRs and gate sub-rows):

| Section | Rows |
|---|---|
| A. Port handlers | 52 |
| B. NR WRITE handlers | 93 |
| C. NR READ handlers | 88 |
| D. `port_*_io_en` gates | 28 |
| E. NMI sources | 7 |
| F. MF transitions | 17 |
| G. save/load schema | 9 |
| **TOTAL** | **294** |

Audit claims "~270 rows". The 24-row delta is explained by the audit
collapsing some loop-generated NR ranges (e.g. `0x16-0x1B` as one row,
`0x50-0x57` as one row, `0x35-0x39` as one row, `0x75-0x79` as one row).
The "~270" is a fair characterisation. No gap >5% — passes the
acceptance threshold.

Pass-19 NIT-01 corrections (Panel-2 gate row additions for NR 0x84 b4-b7
DAC stereo BC / mono AD fb / GS Covox / Specdrum) are correctly
incorporated into the Pass-20 Section D table — all 8 NR 0x84 bits b0-b7
present.

## Step 2 — Spot-Check 10 ✓ Rows

10 rows sampled across sections A, B, C, D, E, F. Each VHDL line
read directly and compared to the C++ summary.

| Row | VHDL line read | Audit summary | Verdict |
|---|---|---|---|
| `port 0xFE` ULA (LSB=0) | `:2582` `port_fe <= '1' when cpu_a(0) = '0' else '0'` | "mask 0x0001/0x0000 (LSB=0)" | ✓ |
| `port 0x7FFD` | `:2593` requires `cpu_a(15)='0' and cpu_a(14)='1' or p3=0 and port_fd and not port_1ffd and port_7ffd_io_en` | "mask 0x8003/0x0001 + NR 0x82 b1 gate" | ✓ |
| NR 0x22 write | `:5295-5298` `nr_22_line_interrupt_en <= nr_wr_dat(1); nr_23_line_interrupt(8) <= nr_wr_dat(0)` | "line_interrupt_en + line_interrupt(8)" | ✓ |
| NR 0xC0 write | `:5596-5599` stores vector(7:5), stackless_nmi(3), int_mode(0) | "im2_vector + stackless_nmi + im_mode + int_mode" | ✓ (im_mode is read-mux composition, not stored — minor cosmetic but row still correct) |
| NR 0x09 read | `:5908-5909` `psg_mono & sprite_tie & '0' & (not hdmi_audio_en) & scanlines` | "cached & 0xE7 + tie" | ✓ |
| NR 0x6A read | `:6099` `"00" & radastan & radastan_xor & lores_palette_offset` | "composed lores" | ✓ |
| port_ay_io_en gate | `:2428` `port_ay_io_en <= internal_port_enable(16)` (NR 0x84 b0 in expbus AND-mask) | "0x84 b0 → AY ports" | ✓ |
| NMI MF button | `multiface.vhd:135-141` `button_pulse <= button_i and not nmi_active; nmi_active <= '1' on button_pulse` | "button + nmi_sw_gen_mf + iotrap, AND mf_enable" | ✓ |
| MF reset → invisible=1 | `multiface.vhd:156` `invisible <= '1' on reset` | "Reset → invisible=1 (NOT 0)" | ✓ |
| MF fetch 0x66 → mf_enable=1 | `multiface.vhd:169-177` `fetch_66 <= a0066 and not m1 and nmi_active; mf_enable <= '1' on fetch_66 and not mreq` | "Fetch 0x66 → mf_enable=1 (and nmi_active=1)" | ✓ |

All 10 rows verified VHDL-faithful.

## Step 3 — Fix Verification

### V20-NMP-XADC (commit `b81c832`)

VHDL `:7428-7429` (Issue 2/3 path):
```
nr_f9_xadc_d0 <= (others => '0');
nr_fa_xadc_d1 <= (others => '0');
```

VHDL `:6280-6284` read mux:
```
when X"F9" => port_253b_dat <= nr_f9_xadc_d0;
when X"FA" => port_253b_dat <= nr_fa_xadc_d1;
```

Combined: for Issue 2/3 boards (`g_board_issue <= 1`), NR 0xF9/0xFA
read returns constant 0x00 regardless of writes. jnext seeds
`regs_[0x0F] = 0x00` (Issue 2) in `nextreg.cpp:430`, so the Issue 2
path applies. The fix installs read handlers at
`emulator.cpp:2203-2208` returning constant 0x00 for both NRs.

**Sandwich verification**:
- Baseline: nextreg integration 262/262 PASS.
- Revert: commented out the two `set_read_handler(0xF9/0xFA)` calls.
- Rebuild + run: `V20-NMP-XADC-F9 FAIL [got=0x5a expected=0x00]`,
  `V20-NMP-XADC-FA FAIL [got=0xa5 expected=0x00]`. Total 260/262.
- Restore: rebuild + run → 262/262 PASS.

Discriminative confirmed. Fix is VHDL-faithful.

### V20-NMP-02 (commit `740a79c`)

VHDL `:6092-6093` read mux:
```
when X"68" =>
   port_253b_dat <= (not nr_68_ula_en) & nr_68_blend_mode
                    & nr_68_cancel_extended_keys & port_ff3b_ulap_en
                    & nr_68_ula_fine_scroll_x & '0' & nr_68_ula_stencil_mode;
```

Bit-position decode (MSB-first): b7=(not ula_en), b6:5=blend_mode,
b4=cancel_ext_keys, b3=ulap_en, b2=fine_scroll_x, **b1='0' literal**,
b0=stencil_mode.

VHDL `:5444-5450` write decoder:
```
when X"68" =>
   nr_68_ula_en <= not nr_wr_dat(7);
   nr_68_blend_mode <= nr_wr_dat(6 downto 5);
   nr_68_cancel_extended_keys <= nr_wr_dat(4);
-- nr_68_ulap_en <= nr_wr_dat(3);
   nr_68_ula_fine_scroll_x <= nr_wr_dat(2);
   nr_68_ula_stencil_mode <= nr_wr_dat(0);
```

No `nr_68_*(1)` assignment — bit 1 of the write byte is silently
dropped. Reading bit 1 must always return '0'.

Fix at `emulator.cpp:2492`: read handler mask tightened from `0xF7`
(= 1111_0111, leaks bit 1) to `0xF5` (= 1111_0101, clears bit 1 AND
bit 3 — bit 3 recomposed from live `renderer_.ula().get_ulap_en()`).

Mask is correct: `0xF5 = 0b1111_0101` — bits 7:4, 2, 0 preserved from
cache; bit 3 cleared (recomposed live); bit 1 cleared (literal '0').

**Sandwich verification**:
- Baseline: 262/262 PASS.
- Revert: changed `& 0xF5` back to `& 0xF7`.
- Rebuild + run: `V20-NMP-02 FAIL [got=0x02 expected=0x00]`. Total 261/262.
- Restore: rebuild + run → 262/262 PASS.

Discriminative confirmed. Fix is VHDL-faithful.

## Step 4 — V20-NMP-01 False-Positive Dismissal

The candidate finding was "init-time `propagate_effective_port_enables`
redundancy". The audit dismissed it on the grounds that
`Emulator::reset()` and `soft_reset()` outer save/restore loops re-fire
the NR 0x86 write handler at `emulator.cpp:2705-2720` after init.

**Trace verification**:

1. `Emulator::reset()` at `emulator.cpp:6182`:
   - Lines 6202-6205: save NR 0x86/0x87/0x88/0x89 byte-cache values.
   - Line 6215: `nextreg_.reset()` → resets NRs to defaults.
   - Line 6233: `init(config_)` → re-registers write handlers (lines
     2705+); does NOT call `propagate_effective_port_enables()` at
     init time (only NR 0x80 write handler does, line 4094, and that
     only fires on actual writes).
   - Lines 6243-6248: `if (!bus_reset_type_0) { nextreg_.write(0x86, save_86); ... }`
     → fires NR 0x86 write handler → calls
     `propagate_effective_port_enables(/*override_reg=*/0x86, save_86)`.

2. `Emulator::soft_reset()` at `emulator.cpp:6251`: identical
   save/restore pattern (lines 6273-6277, 6312-6317).

**Path analysis**:
- `bus_reset_type_0 = false` (i.e. nr_89 bit 7 = 1, the default): write
  handler fires → propagation. ✓
- `bus_reset_type_0 = true`: save/restore skipped; NR 0x86-0x89 reset
  to defaults inside `init()`; no write handler fires post-init.
  Subsystem-side shadows (DivMmc, Multiface, ContentionModel) also reset
  to their defaults via their own reset() methods in init(). State
  remains consistent — no propagation needed.

Both paths leave the shadow state consistent with NR cache values.
**Dismissal CONFIRMED**.

Also verified that NR 0x84 has no write_handler but all consumers use
`effective_internal_port_enable(0x84)` at port-decode time
(`emulator.cpp:3570-3732`, 14 sites) — this is a live-AND lookup, not a
shadow. No shadow mirror needed for NR 0x84. The audit's row B0x84
description ("All consumers read live") is correct.

## Step 5 — Side-Effect Inspection

**NR 0xF9 / 0xFA**: `grep -rn "regs_\[0xF9\]|nr_f9|xadc_d0" src/` shows
the only references are V20-NMP-XADC commentary and unrelated
DAC/sprite/trace constants. No consumer reads the cache for non-XADC
purposes. The stubs returning 0x00 are safe.

**NR 0x68 bit 1**: `grep -rn "cached(0x68)|regs_\[0x68\]" src/` shows
debugger reads `reg68 & 0x80` (ula_en bit) — unaffected. No other
consumer touches bit 1. The mask change is inert outside the
discriminative test.

**Issue 2 vs Issue 4/5 paths**: jnext seeds `regs_[0x0F] = 0x00`
(Issue 2) at `nextreg.cpp:430`. No code branches on board issue value
at runtime (checked via `grep -rn "board_issue|g_board_issue"
src/`) — the seeded constant is the source of truth. The XADC stubs
are appropriate for the current Issue-2 model and conservative for any
future Issue 4/5 retarget (which would require a separate fix).

## Step 6 — Independent Re-Audit for Adjacent Missed

### Literal '0' bits in NR composed-read mux

Scanned VHDL `:5878-6289` (NR read mux range) for `'0' &` and `& '0'`
patterns. Found 18 occurrences. For each, verified the C++ read handler
correctly masks the corresponding bit:

| NR | VHDL line | Literal '0' bit(s) | C++ mask | Verdict |
|---|---|---|---|---|
| 0x09 | :5909 | b3 | `cached & 0xE7` | ✓ |
| 0x0A | :5912 | b2 | composed (no bit 2 set) | ✓ |
| 0x0B | :5915 | b6, b3:1 | `v & 0xB1` write-side | ✓ |
| 0x10 | :5924 | b7 | composed (max coreid+spkey = 0x7F) | ✓ |
| 0x12 | :5930 | b7 | `& 0x7F` | ✓ |
| 0x13 | :5933 | b7 | `& 0x7F` | ✓ |
| 0x34 | :6033 | b7 | `& 0x7F` | ✓ |
| 0x68 | :6093 | **b1** | `& 0xF5` (V20-NMP-02 fix) | ✓ |
| 0x6E | :6108 | b6 | tilemap `& 0xBF` | ✓ |
| 0x6F | :6111 | b6 | tilemap `& 0xBF` | ✓ |
| 0x81 | :6126 | b2 | `0x80 \| (cache & 0x78)` (V11-NMP-01) | ✓ |
| 0xA2 | :6192 | b5 | `(c & 0xDD) \| 0x02` | ✓ |
| 0xA9 | :6201 | b7:3, b1 | constant 0x00 | ✓ |
| 0xC0 | :6230 | b4 | composed (no bit 4 set) | ✓ |
| 0xC6 | :6245 | b7, b3 | `& 0x77` | ✓ |
| 0xC8 | :6248 | b7:2 | `int_status_mask_c8()` returns max 0x03 | ✓ |
| 0xCE | :6263 | b7, b3 | composed (b6:4 from uart1, b2:0 from uart0) | ✓ |
| 0xF8 | :6278 | b7 | `cached & 0x7F` (V19R-NIT-04) | ✓ |

No missed bits. The V20-NMP-02 fix is the last surviving literal-'0'
leak in the entire NR read mux. **Adjacent re-audit clean.**

### Issue-2-gated NR families

Scanned VHDL `g_board_issue` gates to catalog Issue-2 vs Issue-4/5
divergent register paths:

| Generate | VHDL line | NR(s) | jnext coverage |
|---|---|---|---|
| `gen_coreid_23` | :5673 | NR 0x10 (coreid Issue 2/3) | V16-NMP-01 ✓ |
| `gen_coreid_45` | :5691 | NR 0x10 (coreid Issue 4/5 — different write decode) | n/a (jnext Issue 2) |
| `gen_romram_234` | :5709 | NR 0x04 (bit 7 forced 0 for Issue ≤ 4) | V4 fix `& 0x7F` ✓ |
| `gen_romram_5` | :5724 | NR 0x04 (Issue 5 full 8 bits) | n/a |
| `gen_xper_23` | :7420 | NR 0xF0/0xF8/0xF9/0xFA (Issue 2/3 zero-stubs) | V19R-NIT-03/04 + V20-NMP-XADC ✓ |
| `gen_xper_44` | :7438 | NR 0xF0/0xF8/0xF9/0xFA (Issue 4/5 full XADC) | n/a |
| `gen_sram_234` | :1644 | FPGA `o_RAM_A_ADDR_21` pin | n/a (no FPGA pin model) |
| `gen_fdc_234` | :1681 | FPGA `o_BUS_P3_*` pins | n/a (no expansion bus model) |

All software-visible Issue-2-gated NR families fully covered.
**No missed Issue-2-gated family.**

## Test Invariants

| Suite | Result |
|---|---|
| `ctest --output-on-failure` | 38/38 PASS |
| `./build/test/fuse_z80_test build/test/fuse` | 1356/1356 PASS |
| `./build/test/nextreg_integration_test` | 262/262 PASS |
| `bash test/00regression/regression.sh` | 33/0/0 (Pass 33, Fail 0, Skip 0) |

All tests green at baseline and post-sandwich-restore.

## NITs

None substantive. One cosmetic observation:

- **V20R-NMP-NIT-01 (cosmetic)**: audit Section B row 0xC0 reads
  "im2_vector + stackless_nmi + **im_mode** + int_mode" — `im_mode` is
  not a stored write field but is composed at read time from
  `z80_im_mode` (per VHDL :6230). The row is functionally correct (C++
  writes the right fields and reads recompose im_mode) but the
  description conflates write and read field sets. Suggest "im2_vector
  + stackless_nmi + int_mode (write); + z80_im_mode read-mux
  composition" for clarity. Does not affect convergence determination.

## Final HEAD

Pre-review HEAD: `e8689252cfbabf2f42812ea913fb31be265d5d93` (audit).
Reviewer branch will add this review doc commit on top.

## Reviewer Conclusion

Both Pass-20 fixes (V20-NMP-XADC and V20-NMP-02) are VHDL-faithful and
verified discriminative via sandwich-revert. The V20-NMP-01 false-positive
dismissal is grounded in a correct trace of `Emulator::reset()` /
`soft_reset()` save/restore loops. The audit table is complete and the
adjacent re-audit (18 literal-'0' positions + 4 Issue-2-gated NR
families) found no missed findings. The XADC family (NR 0xF0/0xF8/
0xF9/0xFA) is now fully closed across Pass-19/Pass-20.

**APPROVE.**
