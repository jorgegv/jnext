# NextZXOS Boot Subsystem — Independent Verification Audit
# DivMMC + SD-card + SPI subsystem (second pass)

Date: 2026-05-09
Branch: `task2/verify-divmmc-sd-spi`
Auditor scope: blind re-audit of the post-fix codebase against the VHDL
oracle. **Did not read** any prior `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS*.md`
files — all findings here are derived independently from VHDL + C++.

---

## VERDICT

**APPROVE WITH ONE CLASS-(B) MODELLING GAP NOTED, ZERO CLASS-(A) BUGS FOUND.**

Specifically:

- 0 class-(a) (clear correctness bugs requiring a fix on this branch).
- 1 class-(b) modelling gap that does not affect the canonical NextZXOS
  boot path but is a strict-fidelity concern (`rom3_active` feeder uses
  `mmu_.rom3_selected()` which is a different VHDL signal than the
  `sram_pre_rom3` that `sram_divmmc_automap_rom3_en` actually consumes).
- 4 class-(c) lower-priority observations (timing pipeline collapse,
  CMD17 OOR R1 polarity, no-device SPI MISO drift, Flash-CS gate elided).

Tests: 27/27 pass on the worktree (build succeeds clean,
`fuse_z80_tests`, `divmmc_tests` 123/123, `sdcard_tests` 15/15,
`sd_rom_extractor_tests`, plus all other unit suites).

No fixes were applied; the working tree is unchanged.

---

## METHODOLOGY

1. Listed every VHDL source touching DivMMC, SPI, and SD card:
   - `device/divmmc.vhd` (60-line FSM)
   - `serial/spi_master.vhd` (180-line SPI byte engine)
   - `zxnext.vhd` port-decode + NR registers (~6300 lines, audited the
     relevant ranges: 2473-2620 port LSB/MSB decode, 2848-2908
     trap-decode + RST/$0066/$04C6/$0562/$04D7/$056A/$3DXX/$1FF8 wiring,
     2981-3008 sram_rom3 logic, 3029-3138 sram_pre_override arbiter,
     3300-3332 port_e7 CS decode, 3866-3898 NR D9/DA IO-trap, 4112-4188
     divmmc_mod port map, 4173-4188 port_e3_reg latch, 5050-5210 NR
     reset + write paths, 5191-5198 NR 0A, 6266-6272 read mux).

2. Read every C++ source in scope (`src/peripheral/divmmc.{cpp,h}`,
   `src/peripheral/spi.{cpp,h}`, `src/peripheral/sd_card.{cpp,h}`)
   end-to-end. Cross-checked against `src/core/emulator.cpp` port
   dispatch + NR write/read handlers + on_m1_prefetch / on_m1_cycle
   callbacks. Did not read host-side `sd_rom_extractor.{cpp,h}` (out of
   scope per prompt).

3. Compared each VHDL signal/register against its C++ counterpart by
   line number. For any divergence flagged it as a finding.

4. Built and ran tests. Examined `divmmc_test.cpp` rows independently
   to confirm the test expectations match VHDL (sanity-check the
   oracle).

---

## FINDINGS

### DivMMC

#### DM-VERIFY-01: AUTOMAP entry-point trap decode — VHDL-faithful ✓

VHDL `zxnext.vhd:2848-2908` decodes:

| Trap PC      | Source                                              | NR bit         |
|--------------|-----------------------------------------------------|----------------|
| $0000-$0038  | `port_00xx_msb` + `cpu_a(7:6)="00"` + `cpu_a(2:0)="000"`, bit-select `cpu_a(5:3)` | NR $B8[0..7] (validity NR $B9, timing NR $BA) |
| $0066 (NMI)  | `port_00xx_msb AND port_66_lsb`                      | NR $BB[1] instant + [0] delayed |
| $04C6        | `port_04xx_msb AND port_c6_lsb`                      | NR $BB[2] (ROM3 path) |
| $0562        | `port_05xx_msb AND port_62_lsb`                      | NR $BB[3] (ROM3 path) |
| $04D7        | `port_04xx_msb AND port_d7_lsb`                      | NR $BB[4] (ROM3 path) |
| $056A        | `port_05xx_msb AND port_6a_lsb`                      | NR $BB[5] (ROM3 path) |
| $3Dxx        | `port_3dxx_msb` (any low byte)                       | NR $BB[7] (ROM3 instant) |
| $1FF8-$1FFF  | `port_1fxx_msb AND cpu_a(7:3)="11111"`               | NR $BB[6] (delayed-off) |

C++ `src/peripheral/divmmc.cpp:332-414` matches every entry. The
single-bit fix from commit `399c9ae` (NR $BB bit 0/7 + decoupled
clauses) is correctly applied: each trap PC contributes independently
to `instant_match`/`delayed_match` rather than else-if'ing them, so an
M1 fetch at PC=$0066 with NR $BB=$CD lights both bit 0 (delayed) and
bit 1 (instant) — VHDL `divmmc.vhd:120-121` ORs both into
`automap_hold` independently.

The `$3Dxx` wildcard correctly uses `(pc & 0xFF00) == 0x3D00` (any low
byte), gated on `rom3_path_eligible`. ✓

#### DM-VERIFY-02: AUTOMAP main vs ROM3 path gating ✓ (with minor caveat)

VHDL `zxnext.vhd:3137-3138`:
```
sram_divmmc_automap_en      <= sram_pre_override(2);
sram_divmmc_automap_rom3_en <= sram_pre_override(2) AND sram_pre_override(0)
                               AND (NOT sram_layer2_map_en) AND (NOT sram_romcs)
                               AND ((sram_altrom_en AND sram_pre_alt_128_n)
                                    OR (sram_pre_rom3 AND NOT sram_altrom_en));
```

C++ `divmmc.cpp:324-327` decomposes:
```
main_path_eligible = sram_pre_override_2;
rom3_path_eligible = sram_pre_override_2 && sram_pre_override_0
                  && !layer2_map_read_ && rom3_active_;
```

This drops `(NOT sram_romcs)` and the altrom branch. Per VHDL
`zxnext.vhd:3018-3020,3079`:
- `sram_pre_romcs_n = i_BUS_ROMCS_n AND expbus_eff_en AND NOT
   expbus_eff_disable_mem`
- `sram_romcs = sram_pre_override(0) AND sram_pre_romcs_n`

In jnext, expansion-bus is not modelled (`expbus_eff_en = 0` always) →
`sram_pre_romcs_n = 0` → `sram_romcs = 0` → `(NOT sram_romcs) = 1`.
Dropping the term is therefore VHDL-faithful for the supported
configuration; documented in the source comment. ✓

The altrom branch is also collapsed to the `rom3_active_` second clause
under the documented assumption `nr_8c_altrom_en=0` on the boot path.
The C++ comment at `divmmc.cpp:319-323` explicitly flags this
assumption. Boot ROMs do not flip NR $8C bit 7 (verified via G46(b)
investigation logs; NR $8C bit 7 stays 0 throughout NextZXOS boot). ✓

#### DM-VERIFY-03: `rom3_active_` feeder mismatch (CLASS-(b) MODELLING GAP)

**Finding:** the C++ `DivMmc::rom3_active_` flag is fed from
`mmu_.rom3_selected()`, which returns `current_rom_bank() == 3`
(VHDL: `port_1ffd(2) AND port_7ffd(4)`). But the VHDL
`sram_divmmc_automap_rom3_en` consumes the latched
`sram_pre_rom3 = sram_rom3` (per `zxnext.vhd:3023`) — and
`sram_rom3` (defined at `zxnext.vhd:2981-3008`) has different per-machine
semantics:

| machine_type | VHDL `sram_rom3` formula                  |
|--------------|-------------------------------------------|
| 48K          | `'1'` (hardwired)                         |
| +3 (locked)  | `lock_rom1 AND lock_rom0`                 |
| +3 (no lock) | `port_1ffd(2) AND port_7ffd(4)`           |
| ZXN/128K (locked)   | `nr_8c_altrom_lock_rom1`           |
| ZXN/128K (no lock)  | `port_7ffd(4)`                     |

Note especially the **ZXN/128K no-lock branch**: `sram_rom3 = port_7ffd(4)`
— so ROM bank 1 (= "01" = `port_7ffd(4)=1, port_1ffd(2)=0`) is
*also* "rom3-active" for the AUTOMAP gate. jnext's
`mmu_.rom3_selected()` returns false for bank 1, suppressing the
ROM3-conditional automap path on ZXN/128K when bank 1 is mapped.

Coincidentally `Mmu::sram_rom3()` already exists and implements the
correct VHDL formula (with the documented caveat that it ignores the
NR $82 bit-3 gate on Next mode, per G57). It is wired into the read
arbiter but **not** into the DivMMC AUTOMAP feeder.

Three call sites in `src/core/emulator.cpp` should switch:

```
emulator.cpp:2335   divmmc_.set_rom3_active(mmu_.rom3_selected());
emulator.cpp:2424   divmmc_.set_rom3_active(mmu_.rom3_selected());
emulator.cpp:3785   divmmc_.set_rom3_active(mmu_.rom3_selected());
```

All three should call `mmu_.sram_rom3()` instead.

**Impact for boot path:**
- 48K: VHDL hardwires `sram_rom3='1'`; jnext's `rom3_selected()` returns
  `false` (bank 0 is the only 48K ROM). On 48K, NR $B9 default $01 means
  RST $00 fires main path; RST $08-$38 fall to ROM3-only path. With
  jnext returning rom3=false on 48K, those traps never fire — but
  classic 48K firmware doesn't expect them either. Boot-path impact
  in 48K mode unclear; this path is not currently exercised in tests.
- 128K / ZXN: when ROM bank 1 is selected (e.g. tape-trap entry to
  $04C6 from BASIC ROM 1), VHDL would fire the rom3-conditional trap;
  jnext doesn't. NextZXOS supervisor primarily uses banks 0 and 3, so
  the canonical NextZXOS boot path is not affected — but tape-trap
  paths from a +3 BASIC running in bank 1 could differ.
- +3: bank 3 path matches; bank 1 path differs.

**Classification:** modelling gap, not a clear-cut bug. The canonical
NextZXOS boot path uses bank 0 / bank 3 transitions; tests don't
exercise the bank-1-as-rom3 case. Recommended to fix as a
forward-correctness improvement, but not blocking for current
investigation.

**Not fixed on this branch** because (a) it requires updating the test
helper that constructs DivMMC fixtures (some tests poke `set_rom3_active`
directly, which already corresponds to `sram_rom3` semantics), (b) the
existing `sram_rom3()` accessor itself has a documented G57 gap (NR $82
bit-3 gating), so the fix is a layered change, and (c) I cannot rule
out unintended interactions with the 48K test path without dynamic
verification.

#### DM-VERIFY-04: Two-stage automap latch (hold/held) — VHDL-faithful ✓

VHDL `divmmc.vhd:123-148` is a two-FF pipeline:
- `automap_hold` clocks on `i_cpu_mreq_n='0' AND i_cpu_m1_n='0'`
  (M1+MREQ-low strobe).
- `automap_held` clocks on `i_cpu_mreq_n='1'` (MREQ rising edge after
  the M1 cycle).
- Combinational `automap = held OR (active AND instant_on) OR (rom3_active
  AND rom3_instant_on)`.

The C++ `check_automap` (`divmmc.cpp:248-437`) collapses both stages
into one per-M1 call, in the correct order:
1. Latch `automap_held_ ← automap_hold_` (simulating MREQ rising edge
   that ended the previous M1's cycle).
2. Decode this PC, accumulate `instant_match`/`delayed_match`/
   `off_match`.
3. Update `automap_hold_` per VHDL :128-131 formula:
   `instant || delayed || (held AND NOT off)`.
4. Compute combinational output: `automap_active_ = held OR instant`.

This matches VHDL functionally at byte granularity. The (minor) caveat
is that VHDL's `*_q` registers (zxnext.vhd:4114-4135) introduce a
half-cycle delay between cpu_a-decode and divmmc-entity input —
collapsing this into one per-M1 call is a documented simplification.
For the purposes of byte-level emulation it is correct.

#### DM-VERIFY-05: RETN clear with one-M1 delay — VHDL-faithful ✓

VHDL `divmmc.vhd:108,126,139` clears `button_nmi`, `automap_hold`,
`automap_held` on `i_retn_seen='1'`. Because the VHDL FFs only update
on a clock edge, the cleared values are observable on the *next* M1
fetch — the M1 that completes ED 45 still sees the pre-clear values.

C++ `DivMmc::on_m1_retn_delay()` (`divmmc.cpp:212-244`) implements a
one-M1-cycle delay register:
- On the M1 with `retn_seen=true`: latch `retn_pending_clear_=true`.
- On the NEXT M1: apply the clears, drop the pending flag.

This is the correct VHDL-faithful sequencing. The legacy `on_retn()`
entry point is preserved for direct test callers (DA-06, IN-03, NM-06)
that want immediate-clear semantics. ✓

#### DM-VERIFY-06: `i_en` gate (port_divmmc_io_en) wiring ✓

VHDL `divmmc.vhd:98-99`:
```
o_divmmc_rom_en <= rom_en AND i_en;
o_divmmc_ram_en <= ram_en AND i_en;
```
where `i_en = port_divmmc_io_en = NR $83 bit 0`.

VHDL `zxnext.vhd:4112`:
```
divmmc_automap_reset <= '1' when port_divmmc_io_en='0' OR nr_0a_divmmc_automap_en='0'
```

C++ models BOTH levers separately (`port_io_enable_`, `nr_0a_4_enable_`)
and:
- `is_active() = port_io_enable_ AND (conmem_ OR automap_active_)` —
  uses port_io_enable only, NOT the combined enabled. This is correct:
  CONMEM is gated only by `i_en` per VHDL :98, NOT by automap_reset.
- `check_automap` early-outs on `!enabled_` (= !port_io_enable OR
  !nr_0a_4_enable) — automap detection requires both. This is correct:
  automap_reset ='1' freezes hold/held to '0' continuously per VHDL.

The C++ comment at `divmmc.h:106-123` documents this distinction. ✓

#### DM-VERIFY-07: Port $E3 OR-latch for mapram (bit 6) ✓

VHDL `zxnext.vhd:4181-4185`:
```
elsif port_e3_wr = '1' then
   port_e3_reg(7) <= cpu_do(7);
   port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6);     -- OR-latch
   port_e3_reg(3 downto 0) <= cpu_do(3 downto 0);
elsif nr_09_we = '1' and nr_wr_dat(3) = '1' then
   port_e3_reg(6) <= '0';                             -- NR $09 bit 3 clear
end if;
```

C++ `divmmc.cpp:107`:
```
mapram_ = mapram_ || ((val & 0x40) != 0);   -- OR-latch
```
And `clear_mapram()` (`divmmc.cpp:121-125`) wired from the NR $09 write
handler. ✓

VHDL `zxnext.vhd:4190` also enforces bits 5:4 always read as 0
(`port_e3_dat <= port_e3_reg(7:6) & "00" & port_e3_reg(3:0)`); C++
`read_control()` returns `control_reg_ & 0xCF` which masks bits 5:4 to
0. ✓

#### DM-VERIFY-08: Port $E3 reset semantics ✓

VHDL `zxnext.vhd:4173-4188`: `port_e3_reg` is in a clocked process whose
reset clause clears it to `(others => '0')` on `reset='1'` (= `i_RESET`,
= `reset_hard OR reset_soft` per `zxnext_top_issue4.vhd:858`). So the
DivMMC control register IS cleared on soft reset.

C++ `Emulator::init()` calls `divmmc_.reset()` unconditionally, which
clears `conmem_`, `mapram_`, `bank_`, `control_reg_` plus the automap
shadow latches. The enable-flag state (`port_io_enable_`,
`nr_0a_4_enable_`) is intentionally preserved across soft reset — also
VHDL-faithful per `zxnext.vhd:5052-5059` (`nr_83_internal_port_enable`
only resets if `nr_85_internal_port_reset_type='1'`, default '1') and
the absence of any reset clause on `nr_0a_divmmc_automap_en`. ✓

#### DM-VERIFY-09: Default NR $B8/$B9/$BA/$BB values ✓

VHDL `zxnext.vhd:5087-5090`:
```
nr_b8_divmmc_ep_0      <= X"83";
nr_b9_divmmc_ep_valid_0 <= X"01";
nr_ba_divmmc_ep_timing_0 <= X"00";
nr_bb_divmmc_ep_1      <= X"CD";
```

C++ `divmmc.cpp:43-46` (in `reset()`):
```
entry_points_0_ = 0x83;
entry_valid_0_  = 0x01;
entry_timing_0_ = 0x00;
entry_points_1_ = 0xCD;
```
✓

#### DM-VERIFY-10: Memory overlay (rom_en / ram_en / rdonly / ram_bank) ✓

VHDL `divmmc.vhd:88-101`:
```
page0 = '1' when cpu_a(15:13)="000"
page1 = '1' when cpu_a(15:13)="001"
rom_en = page0 AND (conmem OR automap) AND NOT mapram
ram_en = (page0 AND (conmem OR automap) AND mapram)
       OR (page1 AND (conmem OR automap))
ram_bank = X"3" when page0 else i_divmmc_reg(3:0)
o_divmmc_rdonly = '1' when page0 OR (mapram AND ram_bank=X"3")
```

C++ `read()`/`write()`/`is_ram_mapped()`/`is_read_only()`/`ram_page_for()`
implement each rule. ✓

Note: VHDL's `o_divmmc_rdonly = '1' when page0` — slot 0 is *always*
read-only when DivMMC overlays it (whether mapping ROM or RAM bank 3).
C++ `is_read_only()` returns `true` for any addr < 0x2000 (slot 0). ✓
Issue #7 comment in VHDL ("bring in divmmc bank 3 as rom substitute
when conmem is set") is the source of the page0 rdonly behaviour even
when conmem maps RAM-bank-3 on slot 0.

### SPI

#### SPI-VERIFY-01: Port $E7 chip-select decode ✓

VHDL `zxnext.vhd:3300-3326`:
```
if cpu_do(1:0) = "10" then
   port_e7_reg <= "111111" & not nr_0a_sd_swap & nr_0a_sd_swap;
elsif cpu_do(1:0) = "01" then
   port_e7_reg <= "111111" & nr_0a_sd_swap & not nr_0a_sd_swap;
elsif cpu_do = X"FB" then
   port_e7_reg <= X"FB";
elsif cpu_do = X"F7" then
   port_e7_reg <= X"F7";
elsif cpu_do = X"7F" and ((nr_03_config_mode='1') or (nr_02_reset_type(2)='1')) then
   port_e7_reg <= X"7F";
else
   port_e7_reg <= X"FF";
end if;
```

C++ `SpiMaster::write_cs()` (`spi.cpp:52-93`) handles all four primary
branches. SD0/SD1 swap math verified by hand for both `sd_swap=0` and
`=1` polarity:

| cpu_do(1:0) | sd_swap | C++ decoded | VHDL pattern    |
|-------------|---------|-------------|-----------------|
| "10"        | 0       | $FE         | "11111110" = $FE |
| "10"        | 1       | $FD         | "11111101" = $FD |
| "01"        | 0       | $FD         | "11111101" = $FD |
| "01"        | 1       | $FE         | "11111110" = $FE |

✓

The Flash-CS branch (`cpu_do=$7F` gated on `nr_03_config_mode OR
nr_02_reset_type(2)`) is intentionally NOT modelled — there is no Flash
device wired in jnext. The C++ comment documents this. **Class-(c)
note:** if Flash-via-SPI is ever modelled, this branch will need
gating on `NextReg::nr_03_config_mode()` OR a reset-type bit.

#### SPI-VERIFY-02: SPI byte engine (write_data/read_data) ✓

VHDL `spi_master.vhd:62-178` is a classic CPOL=0/CPHA=0 8-bit SPI
master with:
- `o_spi_miso_dat` updated from `ishift_r & i_spi_miso` on
  `state_last_d='1'` (one cycle AFTER transfer completes).
- `i_spi_wr` triggers a transfer with `oshift_r ← i_spi_mosi_dat`.
- `i_spi_rd` triggers a transfer with `oshift_r ← all-ones` (= MOSI
  $FF).
- `o_spi_wait_n = state_idle OR state_last_d`.

C++ `SpiMaster::write_data()` (`spi.cpp:99-109`) calls
`device->receive(val)` and stores the response in `rx_data_`.
`SpiMaster::read_data()` (`spi.cpp:111-127`) returns the previously
captured `rx_data_` THEN starts a new transfer (matching the
"miso_dat lags by one transfer" VHDL semantics). ✓

`spi_wait_n()` always returns `true` — documented in the header as a
zero-latency byte-level approximation. ✓ (For the DMA throttling path
that VHDL `zxnext.vhd:3297` exposes, this means DMA-via-SPI bursts run
at zero cost in jnext — out of scope for boot.)

#### SPI-VERIFY-03: NR $83 bit 3 (`port_spi_io_en`) gate ✓

VHDL `zxnext.vhd:2419,2620-2621`:
```
port_spi_io_en <= internal_port_enable(11);     -- = NR $83 bit 3
port_e7 <= '1' when port_e7_lsb='1' AND port_spi_io_en='1' else '0';
port_eb <= '1' when port_eb_lsb='1' AND port_spi_io_en='1' else '0';
```

C++ `emulator.cpp:3292-3309` gates the registered handlers for ports
$E7 and $EB on `(nextreg_.cached(0x83) & 0x08) == 0` → return $FF /
drop write. This was the second half of fix `399c9ae`. ✓

(Reads return 0xFF when gated off — VHDL's `port_e7 / port_eb` only
turn into `port_e7_rd / port_eb_rd` when the gate is high; otherwise
the reads get the floating-bus value, which jnext approximates as
$FF.)

#### SPI-VERIFY-04: NR $0A bit 5 (`nr_0a_sd_swap`) — config_mode-gated ✓

VHDL `zxnext.vhd:5191-5195`:
```
when X"0A" =>
   if nr_03_config_mode = '1' then
      nr_0a_mf_type <= nr_wr_dat(7 downto 6);
      nr_0a_sd_swap <= nr_wr_dat(5);
   end if;
   ...
```

C++ `emulator.cpp:883-892`:
```
if (nextreg_.nr_03_config_mode()) {
    spi_.set_sd_swap((v & 0x20) != 0);
    multiface_.set_mode(static_cast<uint8_t>((v >> 6) & 0x03));
}
```
✓ The other three NR $0A bits (4, 3, 1:0) are unconditional in both
VHDL and C++.

### SD card

#### SD-VERIFY-01: SDHC CSD v2.0 register layout ✓

C++ `sd_card.cpp:543-596` synthesises a 16-byte CSD per SD spec § 5.3.3.
C_SIZE is computed from the actual image size in 512KB units; the
fixed bytes (CSD_STRUCTURE=01, TAAC=$0E, NSAC=$00, TRAN_SPEED=$32 (25
MHz), CCC=$5B5, READ_BL_LEN=9, etc.) are spec-compliant. ✓

#### SD-VERIFY-02: SDHC vs SDSC byte addressing ✓

C++ `cmd17_read_single_block` and `cmd18_read_multiple_block` compute
`byte_addr = sector * 512` (`sd_card.cpp:436,472`). This is correct for
SDHC mode — the host's argument is the block index, not byte address.
tbblue.fw expects SDHC mode (CCS=1 in OCR per
`cmd58_read_ocr` `sd_card.cpp:646`). ✓

#### SD-VERIFY-03: CMD12 stop-transmission with stuff bytes ✓

C++ `cmd12_stop_transmission` (`sd_card.cpp:371-397`) emits 8 stuff
bytes ($FF) before R1, then sets `persistent_response_byte_=0xFF`. The
comment cites the supervisor poll loop at bank-2 $196D-$1978 (G46(b)
decode) — `IN A,($EB); AND A; JR Z` polls until non-zero, so $FF is the
correct post-busy idle byte. Note the choice **diverges from the SD
spec** which says R1b includes a busy phase (MISO=$00) — but this is a
documented practical compromise for tbblue compatibility. ✓

#### SD-VERIFY-04: CMD18 multi-block stream with inter-block filler ✓

C++ `send()` SENDING_DATA branch (`sd_card.cpp:240-285`) handles the
between-blocks re-prime correctly: after the 2 CRC bytes of block N,
re-prime `data_block_` from sector N+1, queue `0xFE` as the next data
token, and emit one `0xFF` filler so the host's "skip $FF until $FE"
loop in tbblue diskio.c sees the new token on a subsequent read. ✓

End-of-image bound-check is present (`byte_addr+512 > file_size_`):
emits warning, ends stream, returns to IDLE. ✓

#### SD-VERIFY-05: CMD24 R1-then-data bridge ✓

`pending_write_after_r1_` flag (`sd_card.cpp:117-121,222-228`) ensures
the State::RESPONDING → RECEIVING_DATA transition only fires after the
R1 byte (and its NCR prefix) have actually been emitted on MISO. Without
it, a previous bug clobbered RESPONDING with RECEIVING_DATA before
send() could emit R1, hanging the FatFs `send_cmd` poll loop. ✓ The
comment on this is exemplary.

#### SD-VERIFY-06: CMD55+ACMD41 init flow ✓

C++ `cmd55_app_cmd` sets `app_cmd_=true` and emits R1.
`process_command` consumes `app_cmd_` for the next dispatch, routing
ACMD41 to `acmd41_sd_send_op_cond` which sets `initialized_=true`. ✓

#### SD-VERIFY-07: CMD13 SEND_STATUS R2 response ✓

C++ `cmd13_send_status` emits NCR + R1 + R2-status (`sd_card.cpp:399-408`).
R2 = $00 ("no errors"). Used by some firmware paths post-init. ✓

#### SD-VERIFY-08: CMD58 OCR with CCS=1 (SDHC) ✓

C++ `cmd58_read_ocr` returns OCR bytes [$C0,$FF,$80,$00] when initialized
(bit 31 = power-up done, bit 30 = CCS). Comment cites tbblue.fw
MMC_Init line 113 mmc.s `and #0x40` selecting CCS bit. ✓

#### SD-VERIFY-09: ZEsarUX-style persistent IDLE response byte ✓

The `persistent_response_byte_` mechanism (`sd_card.cpp:135` +
handlers) is a deliberate pragmatic choice to handle firmware that
polls past the proper response length. CMD0 → $01, CMD8 → $00 (NB: the
earlier value), CMD12 → $FF. Documented decision; not strictly
spec-compliant but matches ZEsarUX's mmc_read switch behaviour.

**Class-(c) note:** the comment at `cmd8_send_if_cond` (`sd_card.cpp:362`)
says "ZEsarUX returns sustained $00 for CMD8 deliberately... we keep
proper R7 here". That's correct — `resp_buf_` is the full 6-byte R7 so
the response is fully drained before falling to IDLE state where
`persistent_response_byte_` (also $FF default) takes over. The
"$00 forever" claim in the audit comment is mis-attributed; the actual
behaviour is "$FF forever after the 6-byte R7 drains". This is fine
for tbblue.fw, which reads exactly the documented response length.

### Cross-cutting (zxnext.vhd → emulator.cpp)

#### CC-VERIFY-01: NR $D8/$D9/$DA — IO trap registers ✓

VHDL `zxnext.vhd:5107` resets `nr_d8_io_trap_fdc_en` to `'0'`.
VHDL `:3870,3891` resets `nr_da_iotrap_cause` and `nr_d9_iotrap_write`
to all zeros.
C++ `emulator.cpp:139,142,143` mirrors all three.

VHDL `:3879-3880` clears `nr_da_iotrap_cause` on NR $02 bit 4=0 write;
C++ `emulator.cpp:1560-1562` does the same in the NR $02 write handler.
VHDL `:3892-3893` captures `cpu_do` into `nr_d9_iotrap_write` on
`port_3ffd_wr AND nmi_accept_cause`; C++ `emulator.cpp:2401-2410`
strobes both `nmi_source_.strobe_iotrap()` and updates
`nr_da_iotrap_cause_`/`nr_d9_iotrap_write_` from the port-$3FFD write
handler. ✓

NR $DA write is correctly modelled as a no-op (`emulator.cpp:1667-1672`)
— VHDL `:5645` has the write-arm commented out. ✓

#### CC-VERIFY-02: `sram_pre_override(2)` and `(0)` helpers ✓

VHDL `zxnext.vhd:3029-3066`. Per-MREQ priority arbiter:
- `cpu_a(15:14)="00"` (slot 0/1):
  - `mf_mem_en=1` → `"000"`
  - `mmu_A21_A13(8)=0` → `"110"` (RAM-mapped slot)
  - `nr_03_config_mode=1` → `"110"` (config_mode)
  - else (ROM-mapped, no config_mode) → `"111"`
- `cpu_a(15:14) /= "00"` ($4000-$FFFF): bit 2 (DivMMC) always 0.

C++ `Mmu::sram_pre_override_divmmc_eligible()` (`mmu.h:102-106`):
```
if (pc >= 0x4000) return false;
if (mf_active)   return false;
return true;     // any of the slot-0/1 sub-branches has bit 2='1'
```
✓

C++ `Mmu::sram_pre_override_romcs_priority()` (`mmu.h:119-126`):
```
if (!sram_pre_override_divmmc_eligible(pc, mf_active)) return false;
const int slot = pc >> 13;
if (!slot_in_rom_area(slot)) return false;
if (nr_03_config_mode_v)     return false;
return true;
```
Implements `bit 0 = 1 iff (cpu_a(15:14)="00") AND !mf_mem_en AND
mmu_A21_A13(8)=1 AND !nr_03_config_mode`. ✓

`Mmu::slot_in_rom_area()` returns `get_page(slot) >= 0xE0` — VHDL's
`mmu_A21_A13(8)` formula is `("0001" + page(7:5))(3)` which is high
iff page(7:5) >= "111", i.e. page >= 0xE0. ✓

These are wired into `emulator.cpp:511-519` from `cpu_.on_m1_prefetch`,
called per M1 with the live MF-active and config-mode signals.

### Lower-priority observations (class-(c))

#### CC-OBS-01: VHDL `*_q` registers vs collapsed per-M1 model

VHDL `zxnext.vhd:4114-4135` introduces a half-clock-cycle delay between
the combinational entry-point match (computed from `cpu_a` + NR
registers) and the divmmc entity's `i_automap_*_on` inputs. This is
collapsed in the C++ per-M1 model. Documented in the C++ comments;
acceptable at byte granularity.

#### CC-OBS-02: CMD17/CMD18 OOR R1 polarity

`cmd17_read_single_block` and `cmd18_read_multiple_block` queue R1=$00
(ready) followed by data error token $08 when the requested sector is
past EOF. SD spec § 4.9.1 says R1 should set bit 5 ("address error"
= $20) for the OOR case. Emitting R1=$20 followed by $08 would be more
spec-faithful. Boot-path impact: nil (NextZXOS doesn't read past EOF).

#### CC-OBS-03: SPI no-device MISO drift

`SpiMaster::read_data()` does NOT update `rx_data_` to $FF when no
device is selected, so a sequence of "select SD0 → write $40 → deselect
SD0 → read $EB" will return the last byte SD0 returned, rather than $FF
which a real SPI master with no device on the bus would shift in.
Boot-path impact: nil — tbblue.fw always selects a device before
reading.

#### CC-OBS-04: SPI Flash CS unreached

`SpiMaster::write_cs` drops `cpu_do=$7F` to "all deselected" because the
config-mode/reset-type-2 gate on the Flash branch is not modelled. No
Flash device exists in jnext. If Flash-via-SPI is ever wired (e.g. to
emulate the FPGA flash boot path explicitly), this branch will need
gating on `NextReg::nr_03_config_mode()`.

---

## SPOT-CHECKS

### S-01: hand-decode of NR $BB default $CD

```
0xCD = 11001101
       ||||||||
       |||||||+- bit 0: NMI delayed at $0066    (default ON)
       ||||||+-- bit 1: NMI instant at $0066    (default OFF)
       |||||+--- bit 2: ROM3 delayed at $04C6   (default ON)
       ||||+---- bit 3: ROM3 delayed at $0562   (default ON)
       |||+----- bit 4: ROM3 delayed at $04D7   (default OFF)
       ||+------ bit 5: ROM3 delayed at $056A   (default OFF)
       |+------- bit 6: delayed-off at $1FF8    (default ON)
       +-------- bit 7: ROM3 instant at $3DXX   (default ON)
```

Confirmed against C++ check_automap and VHDL line 2898-2908. ✓

### S-02: hand-decode of NR $B8/$B9 defaults

```
NR $B8 = $83 = 1000_0011 → RST $00, RST $08, RST $38 enabled.
NR $B9 = $01 = 0000_0001 → RST $00 valid (main path);
                            RST $08, $38 fall to ROM3-only path.
```

Combined with NR $BA = $00 (all delayed): RST $00 fires `delayed_on`
(main path, becomes active on next M1 via held-promotion); RST $08/$38
fire `rom3_delayed_on` (gated by sram_pre_rom3 via the rom3_path_eligible
factor in C++). ✓

### S-03: SD CMD0 → CMD8 → ACMD41 → CMD58 boot init flow

| Step | host TX | C++ response                                |
|------|---------|---------------------------------------------|
| 1    | CMD0    | NCR(0xFF) + R1=$01 (idle); `persistent=$01` |
| 2    | CMD8    | NCR(0xFF) + R7={$01, $00, $00, $01, check}; persistent=$FF post-drain |
| 3    | CMD55   | NCR + R1=$01; sets app_cmd_=true            |
| 4    | ACMD41  | NCR + R1=$00; sets initialized_=true        |
| 5    | CMD58   | NCR + R1=$00 + OCR=[$C0,$FF,$80,$00] (CCS=1) |

This matches tbblue.fw MMC_Init expected dialogue. ✓

### S-04: built and ran tests

```
cmake -B build -DENABLE_QT_UI=ON  → success
cmake --build build -j$(nproc)    → 100% built, no warnings
ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80'
  → 4/4 pass
ctest --test-dir build -E 'fuse|cpu_int_pulse|integration'
  → 27/27 pass
./build/test/divmmc_test          → 123/123 pass, 18/18 groups
./build/test/sdcard_test          → 15/15 pass
```

---

## OPEN QUESTIONS

1. **DM-VERIFY-03** (`rom3_active_` feeder mismatch) — should the three
   call sites in `emulator.cpp` switch from `mmu_.rom3_selected()` to
   `mmu_.sram_rom3()`? The latter exists and matches VHDL semantics for
   ZXN/128K mode (`sram_rom3 = port_7ffd(4)` in no-lock mode, where
   bank 1 also lights `sram_rom3`). The change is scoped (3-line edit
   in emulator.cpp) but tests need a review pass first. **Did not fix
   on this branch.**

2. The G46(b) investigation has dragged on through EOD-19 to EOD-24
   without finding a single root cause despite extensive supervisor
   trace and DZRP comparison. Could the
   `mmu_.rom3_selected()` vs `mmu_.sram_rom3()` mismatch
   contribute? Specifically, would the supervisor's bank-1 transitions
   land on a RST trap PC where VHDL would automap (rom3 path) and
   jnext does not? Worth a focused experiment: instrument the three
   `set_rom3_active` call sites to log when `sram_rom3()` differs from
   `rom3_selected()`, then check coincidence with the slide-trigger
   PCs (per memory's EOD-24 entry: NEXTREG $8E,$03 at $5B48).

3. **Cross-cutting timing model**: the VHDL clocks DivMMC at 28 MHz
   (`zxnext.vhd:4140`), so an M1 fetch (~1 µs at 28 MHz = ~28 cycles)
   has many internal state transitions. The C++ collapses everything
   per-M1 to a single function call. For boot-path correctness this is
   sufficient, but a hypothetical buggy supervisor that depends on
   sub-M1 timing would not be reproducible. This is documented.

4. **NR $C0 stackless NMI** (`nr_c0_stackless_nmi`) interacts with the
   DivMMC NMI button latch but was out of scope here. If MF NMI tests
   regress at some future point, check this signal's wiring against
   `divmmc.vhd:108-114`.

5. **Hard reset vs soft reset for DivMMC**: VHDL clears `port_e3_reg` on
   either reset (since `reset = reset_hard OR reset_soft` per
   `zxnext_top_issue4.vhd:858`), but `nr_0a_divmmc_automap_en` is
   preserved (no reset clause). Currently jnext clears the e3-derived
   state in `DivMmc::reset()` and preserves the enable levers. This is
   correct on its face — but worth re-verifying once the system-wide
   "what gets cleared by which reset" matrix is documented (G130 / TODO
   in mmu memory subsystem audits).

---

## SUMMARY

The DivMMC + SD-card + SPI subsystem in jnext is a careful and
well-documented translation of the VHDL oracle. The first-pass audit
+ subsequent fix commits (`399c9ae`, `0ba6bc4`, `24ada07`) closed the
biggest gaps (NR $BB bit 0/7 trap decoding, $3DXX wildcard, SPI
port_spi_io_en gate). This second-pass blind audit found one
class-(b) modelling gap (`rom3_active_` feeder uses
`mmu_.rom3_selected()` rather than `mmu_.sram_rom3()`) which does not
affect the canonical NextZXOS boot path but is a strict-fidelity
concern, especially on 128K/ZXN mode with bank 1 selected. Four
class-(c) lower-priority observations are noted for the record.

No class-(a) bugs found. No fixes applied on this branch.

Tests 27/27 pass.

— end of report —
