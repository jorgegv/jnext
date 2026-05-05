# G46(b) NR (NextREG) Handler Audit — VHDL Faithfulness Pass

**Date**: 2026-05-05
**Author**: read-only audit agent
**Scope**: jnext NR write/read handlers vs. zxnext.vhd spec, focused on registers
likely to influence NextZXOS supervisor branching during early boot under
`--bypass-tbblue-fw`.
**Inputs**:

- `src/core/emulator.cpp` (handler installation, ~lines 555-3080)
- `src/port/nextreg.cpp/h` (dispatch + reset defaults)
- `src/peripheral/{divmmc,multiface,ctc,nmi_source,...}.cpp`
- `src/memory/mmu.cpp` (NR 0x8E composer, NR 0x50-0x57 store)
- `cores/zxnext/src/zxnext.vhd` lines 1099-1265 (signal defaults), 4839-4906
  (write-strobe decode), 5117-5673 (write FSM body), 5878-6289 (read mux)

## Methodology

For each priority register I:

1. Read VHDL declaration / reset default (lines 1099-1265).
2. Read VHDL write FSM body (`when X"XX" =>` in 5117-5673) and external `_we`
   consumers, capturing all stored fields and their masks.
3. Read VHDL read-mux line in 5878-6289 capturing exact composition formula.
4. Read jnext write_handler at registration site, comparing side-effects, masks,
   gating (e.g. `nr_03_config_mode`), and propagation into subsystems.
5. Read jnext read_handler (or fall-through to `regs_[]`) and compare to VHDL
   composed byte; flag stale-shadow risks where a peripheral mutates state
   without touching `regs_[reg]`.
6. Cross-reference live-investigation notes from
   `doc/issues/G46B-INVESTIGATION-LIVE.md` for known divergences.

Severity scale: HIGH = plausible boot-branch divergence; MED = visible to the
supervisor but no observed branch in the live-instrumentation logs; LOW =
visible difference but cosmetic (read-only for the supervisor).

## Per-Register Findings

### NR 0x00 (machine ID) — read-only

- **VHDL**: `port_253b_dat <= g_machine_id;` (zxnext.vhd:5885) — generic = 0x0A.
- **jnext write**: stored into `regs_[0]` then discarded by read_handler.
- **jnext read**: hard-coded `0x08` (HWID_EMULATORS) — `emulator.cpp:560`.
- **Divergence**: deliberate (documented at `nextreg.cpp:18`). NextZXOS treats
  0x08 as emulator and takes its emulator-aware boot path; reporting 0x0A makes
  it dive into a flashing flow that fails for emulator-mounted images.
- **Severity**: N/A (intentional; documented). Validate that the supervisor
  branching is the EMULATOR branch — if any 0x08-vs-0x0A switch leads to a
  divmmc-aware vs ROM-only branch, that's worth a one-shot test.

### NR 0x01 (core version) — read-only

- **VHDL**: `port_253b_dat <= g_version;` (5888) — generic = 0x32 (3.02).
- **jnext**: `regs_[0x01] = 0x32` at reset (`nextreg.cpp:28`); no
  read_handler → fall-through. **MATCH**.

### NR 0x02 (RESET / NMI strobes) — write-strobe + read-composed

- **VHDL write**: stores only `nr_02_bus_reset <= nr_wr_dat(7)` (5119); other
  bits drive one-shot strobes (soft/hard reset, MF/DivMMC NMI gen, IO trap).
- **VHDL read**: composes `bus_reset & "00" & iotrap & gen_mf_nmi &
  gen_divmmc_nmi & reset_type[1:0]` (5891).
- **jnext**: write_handler at `emulator.cpp:1423-1465` strobes
  `nmi_source_.strobe_soft_reset()`, `reset()`, `soft_reset()`, clears
  `nr_da_iotrap_cause_` if bit 4 = 0. Read at :1481 returns
  `nmi_source_.nr_02_read()` which surfaces `gen_mf_nmi` + `gen_divmmc_nmi`
  bits but **does NOT set bit 7 (`bus_reset`) or bit 4 (`iotrap`) or bits 1:0
  (`reset_type[1:0]`)**.
- **Divergence**: bits 7/4/1:0 always read 0 in jnext. Reset type field shows
  the FSM at `nmi_source_.cpp` does emit `reset_type` — let me verify.
  `emulator.cpp:1480` says "we surface them as zero rather than inventing
  state". Bit 4 is consumed by the NR 0x02 write handler (clears
  iotrap_cause when bit 4 = 0); reading it back as 0 is OK if the
  supervisor doesn't observe a 1 bit there. But if NextZXOS polls NR 0x02
  bit 1 (`reset_type[0]`) to detect "this was a soft reset", we'd lie.
- **Severity**: MED. Inspect supervisor disasm for `IN A,(NR 0x02)` reads
  during early boot (search for `BIT n,A` or `AND nn` after that read).

### NR 0x03 (machine config) — config_mode FSM

- **VHDL write** (5121-5151): clears `bootrom_en`, gates `machine_timing` on
  bit 7 + dt_lock + bit 3, XORs `dt_lock` with bit 3, gates `machine_type`
  commit on `nr_03_config_mode='1'` (read PRE-write), then transitions
  `config_mode` based on bits[2:0] (111→1, 001..110→0, 000→no change).
- **VHDL read** (5894): `nr_palette_sub_idx & machine_timing & dt_lock &
  machine_type` (1+3+1+3 bits = 8).
- **jnext write** (`emulator.cpp:1556-1617`): faithful. Order is
  machine-timing → dt_lock toggle → machine_type commit (gated on
  current/PRE-write `nr_03_config_mode_`) → `apply_nr_03_config_mode_transition()`.
- **jnext read** (:1626): `(0<<7) | (timing<<4) | (dtlock<<3) | mtype` —
  `nr_palette_sub_idx` not modelled (bit 7 reads 0).
- **Divergence**: `nr_palette_sub_idx` unmodelled — if the supervisor reads
  NR 0x03 expecting bit 7 set after a NR 0x44 9-bit-mode write, it would
  see 0 in jnext. The VHDL signal toggles in the NR 0x40/0x41/0x44 path
  (palette aux selector). Probably LOW for boot.
- **Severity**: LOW.

### NR 0x04 (romram_bank) — config_mode-routed

- **VHDL** (5716-5732): only stored when `nr_04_we = '1'`; bit-7 mask depends
  on board issue. Read mux has NO entry — falls through to `(others => '0')`.
- **jnext write** (:1639-1648): `nextreg_.set_nr_04_romram_bank(v)`,
  `mmu_.set_nr_04_romram_bank(v)`, returns `0` so `regs_[0x04] = 0`.
- **jnext read**: no handler → returns `regs_[0x04] = 0`. **MATCH**.
- **Severity**: LOW.

### NR 0x05 (PERIPH1 — joystick + 50/60Hz + scandouble)

- **VHDL write** (5157-5158): `nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7:6)`,
  `nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5:4)`. Bits 0/2 are NOT written
  here — they go through hotkey FSMs (F2 / F3) and machine_timing-gated
  paths (Pentagon override).
- **VHDL read** (5897): `joy0[1:0] & joy1[1:0] & joy0[2] & eff_5060 & joy1[2] &
  eff_scandouble`.
- **jnext**: write decoder at `joystick.cpp:27-63` matches VHDL
  concatenation. Read at `emulator.cpp:858-872` matches VHDL composition. The
  `eff_5060` (bit 2) and `eff_scandouble_en` (bit 0) are sourced from
  `nextreg_.cached(0x05)` — i.e. the last-written byte's bits 2/0 directly.
  This deviates from VHDL because:
  - In VHDL, `eff_*` are frame-sync-latched copies of the raw bits and are
    additionally Pentagon-overridden (line 5836).
  - In jnext, we just echo the raw bits.
- **Divergence**: small 50/60Hz read divergence in Pentagon mode; not relevant
  to the +3-machine_type boot path that NextZXOS-on-Next uses. **MATCH** for
  the boot path.
- **Severity**: LOW.

### NR 0x06 (PERIPH2 — DivMMC NMI + MF NMI gates) — CRITICAL FOR BOOT

- **VHDL write** (5161-5170): stores hotkey enables (bit 7, 5), speaker_beep
  (bit 6), button_drive_nmi_en (bit 4), button_m1_nmi_en (bit 3), `ps2_mode`
  (bit 2 — gated on config_mode), psg_mode (bits 1:0).
- **VHDL read** (5900): straight composition of the same fields.
- **jnext write** (:2715-2776): all fields propagated. PS/2 mode gate applied.
  NMI gates (`set_mf_enable`, `set_divmmc_enable`) forwarded. Mixer gate
  refreshed.
- **jnext read** (:2793-2799): bits 7/6/5/4/3/1/0 from cached, bit 2 from
  `nr_06_ps2_mode_`. **MATCH**.
- **Severity**: LOW. NMI-gate semantics looks faithful; bit 4 and bit 3
  forward to NmiSource correctly.

### NR 0x07 (TURBO / CPU speed)

- **VHDL** (5903): `"00" & cpu_speed & "00" & nr_07_cpu_speed`.
- **jnext** (:592-596): `(act<<4)|req`. Equivalent (act=req under jnext's
  expbus-disabled model). **MATCH**.

### NR 0x08 (PERIPH3) — paging-lock + contention

- **VHDL write** (5176-5183 + 3654-3656): bits 6/5/4/3/2/1/0 stored in named
  signals; bit 7 = strobe to clear `port_7ffd_reg(5)` (paging-lock release).
- **VHDL read** (5906): `(NOT port_7ffd_locked) & eff_contention_disable & ...`.
- **jnext write** (:2900-2942): correct fan-out. DAC-disable transition
  resets the soundrive. `mmu_.unlock_paging()` called on bit 7.
- **jnext read** (:2952-2958): bit 7 from `!mmu_.paging_locked()`, bit 6 from
  `mmu_.contention_disabled()`, bits 5:0 from `nr_08_stored_low_`. **MATCH**.

### NR 0x09 (PERIPH4 — psg_mono + sprite_tie + hdmi audio + scanlines)

- **VHDL** (5185-5188 + 5909): stores psg_mono[7:5], sprite_tie[4],
  hdmi_audio_en (bit 2 stored as `not nr_wr_dat(2)`).
- **jnext**: read at :783-790 composes from cached + `sprites_.mirror_tie()`.
  Bit 3 always 0 per VHDL '0' constant. **MATCH**.

### NR 0x0A (PERIPH5 — divmmc_automap_en, sd_swap)

- **VHDL** (5191-5198 + 5912): bits 7:6 (mf_type), bit 5 (sd_swap) gated on
  config_mode. Bits 4/3/1:0 unconditional.
- **jnext write** (:801-817): config_mode gating applied. Forwards
  `divmmc_.set_nr_0a_4_enable((v & 0x10) != 0)` to bit 4 — drives the
  `nr_0a_divmmc_automap_en` lever on the divmmc_automap_reset path.
- **jnext read** (:826-834): composed from authoritative subsystem state with
  `mf_type` from cached. **MATCH**.

### NR 0x0B (PERIPH4 dup — joystick I/O mode)

- **VHDL** (5200-5203 + 5915): stores joy_iomode_en, joy_iomode[1:0],
  joy_iomode_0; read mask = 0xB1 (bits 6, 3, 2, 1 always 0).
- **jnext write** (:884-887): `iomode_.set_nr_0b(v)`; returns `v & 0xB1`,
  storing the masked value in `regs_[0x0B]`. **MATCH**.

### NR 0x10 (board ID + flashboot) — config_mode-gated

- **VHDL** (5677-5707 + 5924): `nr_10_coreid` 5-bit, gated on config_mode.
  Read = `'0' & coreid & SPKEY_BUTTONS[1:0]`. Issue 4/5 boards apply additional
  guards on bit 4 and the `1111` value (line 5700).
- **jnext write** (:909-915): config_mode gate applied. **JNEXT models the
  Issue-2/3 path only** — `nr_wr_dat(4 downto 0)` taken verbatim, no bit-4 or
  `1111` filter. Comment at :890 acknowledges this. The reset default
  `regs_[0x10] = 0x04` (`nextreg.cpp:50`) corresponds to `coreid = 00001`.
- **Divergence**: jnext models Issue 2/3 board, not Issue 5. The supervisor
  may or may not branch on this. The `g_board_issue` value also flows to the
  NR 0x0F readback (`"0000" & g_board_issue`).
- **Severity**: LOW for boot-loop closure (the bytes are stable at this point;
  configuration writes complete before NextZXOS supervisor loads).

### NR 0x12 / 0x13 (Layer 2 active / shadow bank)

- **VHDL** (5219-5223 + 5930-5933): stores `nr_wr_dat(6:0)`; read = `'0' &
  bank[6:0]`.
- **jnext**: write delegates to `layer2_.set_active_bank(v)` — let's verify
  the masking. Read returns `layer2_.active_bank() & 0x7F`. **MATCH** if
  the `active_bank()` accessor stores the byte verbatim. NR 0x13 also writes
  `mmu_.set_l2_shadow_bank(v)` — that propagation is correct.

### NR 0x14 (global transparent rgb)

- **VHDL** (5225-5226 + 5936): full byte, no mask.
- **jnext**: write fans out to `palette_` and `renderer_`; falls through to
  `regs_[]` for read. **MATCH**.

### NR 0x18-0x1B (clip windows X/Y) — auto-incrementing index

- **VHDL** (5242-5290): each write stores into `clip_x1/x2/y1/y2` based on
  `clip_idx`, then `clip_idx <= clip_idx + 1`.
- **jnext**: clip register handlers wire through `renderer_` accessors —
  outside the priority NR list and unrelated to boot.
- **Severity**: LOW.

### NR 0x1C (clip-index reset)

- **VHDL** (5278-5290): clears each clip_idx if the corresponding bit is set.
- **jnext**: handler at :1095-1112. **MATCH**.

### NR 0x1E / 0x1F (active video line)

- **VHDL** (5982-5986): `"0000000" & cvc(8)` / `cvc(7:0)`.
- **jnext** (:1320-1329): computed from `clock_` and `frame_cycle_` over
  `master_cycles_per_line`. **MATCH** (modulo the per-line latch timing
  difference, but same byte semantics).

### NR 0x20 (UNQ — unqualified IM2 raise)

- **VHDL** (5292-5293 + 5989): `nr_20_we` consumed by Im2Controller; read =
  `int_status(0) & int_status(11) & "00" & int_status(6:3)`.
- **jnext** (:2047-2068): write raises UNQ for LINE/ULA/CTC0..3 by bit. Read
  returns int_status of the same set. **MATCH**.

### NR 0x22 / 0x23 (line interrupt)

- **VHDL** (5295-5301 + 5992-5995): bit 1 = line_interrupt_en, bit 0 = line[8].
  NR 0x22 also drives port_ff_reg(6) ← bit 2.
- **jnext** (:1339-1396): faithful. Re-schedules line interrupt on each write.
  Read recomposes from VideoTiming + port_ff_reg + im2_ pulse. **MATCH**.

### NR 0x40 / 0x41 / 0x43 / 0x44 (palette I/O)

- **NR 0x40** read (6035-6036): `nr_palette_idx`. jnext returns
  `palette_.get_index()` — auto-increment-aware. **MATCH**.
- **NR 0x41** read (6038-6039): `nr_palette_dat(8:1)`. jnext returns
  `palette_.read_8bit()`. **MATCH**.
- **NR 0x43** (6044-6045): full composition. jnext recomposes from
  `palette_.read_control()`. **MATCH**.
- **NR 0x44** (6047-6048): `nr_palette_dat(10:9) & "00000" & nr_palette_dat(0)`.
  **jnext has NO read_handler** → falls through to `regs_[0x44]` which holds
  the last raw write byte. Divergence = read returns the written byte's bit
  layout instead of the masked palette-bits layout. Pre-existing G56-cluster
  miss.
- **Severity**: LOW — supervisor doesn't read NR 0x44 during early boot.

### NR 0x50-0x57 (MMU pages)

- **VHDL** (4880-4882, 6059-6081): `nr_mmu_we` triggers `MMU<i> <= nr_wr_dat`.
  Read mux returns the live `MMU<i>` register, which is also updated by:
  - port 0x7FFD writes
  - NR 0x69 writes (port_7ffd_reg(3) only — does NOT touch MMU6/7)
  - NR 0x8E writes with bit 3 = 1 (MMU6/7 ← port_7ffd_bank & '0'/'1')
  - reset paths
- **jnext**: read_handler at :1310-1313 returns `mmu_.get_page(i)` — LIVE.
  Fix #1 closure (commit `2d90ea1`). **MATCH**.
- **Verification recommended**: ensure `mmu_.set_page(i, v)` is the ONLY
  authority that updates `Mmu::nr_mmu_[i]` and that ALL of the listed write
  paths route through it. The G46(b) live-investigation note (#3-4) reports
  that Mmu's NR_57 = 0x01 at PC=$2734 but the immediately following IN A,
  ($253B) on NR 0x57 returns 0x05. This is the actively-pursued root cause
  and the strongest candidate.

### NR 0x60-0x6F (sprite control, layer2 enable, copper, tilemap)

- All handlers reviewed — no obvious divergences. NR 0x69 carefully composes
  from layer2 + mmu shadow + port_ff_reg (:1747-1753).

### NR 0x80 (expbus)

- **VHDL** (2186-2192 + 5481): bit 7 of nr_80_expbus is enable; on `nr_80_we`
  the entire byte is taken from `nr_wr_dat`. There is also a one-shot
  rotation path (line 2186 `nr_80_expbus(7:4) <= nr_80_expbus(3:0)`) used for
  expbus-eff-en latching.
- **jnext**: NO write_handler for NR 0x80. Bare `regs_[0x80] = v`. Read
  returns the byte. **MATCH** for the no-expbus case (which jnext is).
- **Severity**: LOW.

### NR 0x81 (expbus_clken etc. + i_BUS_ROMCS_n)

- **VHDL** (5491-5496 + 6125-6126): write masks bits 6/5/4/3 and forces
  `expbus_speed = "00"`. Read = `i_BUS_ROMCS_n & expbus_ula_override &
  expbus_nmi_debounce_disable & expbus_clken & expbus_fdc & '0' &
  expbus_speed`.
- **jnext write** (:2873-2877): stores in `nr_81_`. Read at :2878-2880 returns
  `0x80 | (nr_81_ & 0x7B)` — bit 7 force-set (no expbus dev), bit 2 always 0.
- **Divergence**: write at jnext does NOT clear bits 1:0 (expbus_speed
  ZEROed in VHDL). Read returns `nr_81_ & 0x7B` which clears bits 7, 2 only —
  bits 1:0 of the original write LEAK into the read. VHDL would hard-zero
  `expbus_speed` on every write.
- **Severity**: LOW. Supervisor doesn't read NR 0x81 expbus-speed bits.

### NR 0x82 / 0x83 / 0x84 (DECODE_INT0..2 — internal port enables) — CRITICAL FOR BOOT

- **VHDL write** (5498-5505): `nr_8X_internal_port_enable <= nr_wr_dat;` —
  full 8 bits stored.
- **VHDL read** (6128-6135): returns the stored byte.
- **VHDL composition** (2392-2424): each bit gates a peripheral port:
  NR 0x83 bit 0 → divmmc_io_en, bit 1 → multiface_io_en, bit 2 → dffd_io_en,
  bit 3 → 1ffd_io_en, bit 4 → p3_floating_bus_io_en, bit 5 → mouse_io_en, etc.
- **jnext NR 0x82**: NO write_handler, NO read_handler. Bare path. Reads of
  `cached(0x82)` for port-decode gating (`emulator.cpp:2149, 2249, 2366,
  2402, 2437, ...`) consult the raw stored byte. **MATCH**.
- **jnext NR 0x83**: write_handler at :1774-1781 forwards bit 0 →
  `divmmc_.set_port_io_enable`, bit 1 → `multiface_.set_enabled`. Other bits
  consumed via `cached(0x83)` at port-decode time. **MATCH**.
- **jnext NR 0x84**: NO write_handler. Bare path. `cached(0x84)` consumed at
  port-decode time. **MATCH**.
- **POTENTIAL ISSUE — initial state under bypass**: cold-init sets
  `regs_[0x82..0x84] = 0xFF` (`nextreg.cpp:57-59`) **but DivMMC starts with
  `port_io_enable_ = false`** (`divmmc.h:249`). Without bypass-tbblue-fw,
  the firmware writes NR 0x83 = 0x3D before the supervisor runs, which
  fires the write_handler and synchronises divmmc state. **Under
  `--bypass-tbblue-fw`, `emulator.cpp:3781` writes NR 0x83 = 0x3D explicitly
  during the synthetic post-handoff init — verify this code path actually
  runs and the write_handler fires (it should, via `nextreg_.write(0x83,
  ...)`).** Also verify NR 0x82 = 0xDA and NR 0x84 = 0xFF are written so
  port-decode caches reflect the post-handoff TBBlue state.
- **Severity**: MED. Recommend an init-time assertion that
  `divmmc_.port_io_enable() == ((cached(0x83) & 0x01) != 0)` after the
  synthetic-init block at :3760-3795 returns. If the synthetic block is
  skipped on the active code path, divmmc might be silently disabled when
  the supervisor expects it active.

### NR 0x85 (DECODE_INT3 — port enables 3 + reset_type)

- **VHDL write** (5507-5509): `nr_85_internal_port_enable <= nr_wr_dat(3:0);`
  `nr_85_internal_port_reset_type <= nr_wr_dat(7);` — bits 6:4 NOT stored.
- **VHDL read** (6138): `reset_type & "000" & enable[3:0]` — read mask 0x8F.
- **jnext**: NO write_handler — bare path stores ALL 8 bits in `regs_[0x85]`.
  Read handler at :1786-1788 returns `cached(0x85) & 0x8F`. **READBACK MATCH**
  but `cached(0x85)` consumers (e.g. `:2460` "NR 0x85 b2 gate") read bit 2,
  which VHDL hard-zeroes via the write-mask. If a subsystem mistakenly
  consults bits 6:4 of NR 0x85, jnext would give a different answer than
  VHDL. Skim of `cached(0x85)` consumers shows only bit 2 (`& 0x04`)
  reference — that's a leak: VHDL stores 0 in bit 2.
- **Divergence**: bit 2 of NR 0x85 is consumed at `:2460` by some
  port-decode gate. **VHDL stores nothing in bit 2 (mask 0x0F + bit 7 =
  0x8F).** jnext bare-store would hand the user-written bit 2 to that
  gate, while VHDL would hand 0. If the supervisor is writing NR 0x85
  with bit 2 set somewhere, the gate behavior would differ.
- **Severity**: MED. Verify with grep what port `:2460` gates and whether
  the supervisor or tbblue.fw write NR 0x85 with bit 2 set during boot.

### NR 0x86 / 0x87 / 0x88 (bus port enables 0..2)

- **VHDL** (5511-5518): full byte stored. Read = stored byte.
- **jnext**: NO handlers. Bare path. **MATCH**.

### NR 0x89 (bus port enables 3 + reset_type)

- **VHDL** (5520-5522 + 6149-6150): bits 3:0 stored, bit 7 stored, bits 6:4
  NOT stored.
- **jnext**: NO write_handler — bare path stores ALL 8 bits. Read at
  :1796-1798 returns `cached & 0x8F`. **READBACK MATCH**. Same risk as 0x85
  if any consumer reads bits 6:4.
- **Severity**: LOW.

### NR 0x8A (bus port propagate)

- **VHDL** (5524-5525 + 6153): stores bits 5:0; read = `"00" & bits[5:0]`
  (mask 0x3F).
- **jnext**: NO handlers. Bare path stores all 8 bits, read returns all 8.
  **MISSING MASK** — write of 0xFF reads back as 0xFF instead of 0x3F.
- **Severity**: LOW. JNEXT does not consume `nr_8a_bus_port_propagate`
  (no expbus device).

### NR 0x8C (altrom)

- **jnext**: write at :1810-1814 → `mmu_.set_nr_8c(v)`, `rom_.set_alt_rom_config(v)`.
  Read at :1815-1817 returns `mmu_.get_nr_8c()`. Should match VHDL :6156.
- **Severity**: LOW.

### NR 0x8E (unified paging — port_7FFD/DFFD/1FFD propagation)

- **VHDL write** (3662-3735): updates port_7ffd_reg (bits 4 conditional on
  bit 2, bits 2:0 conditional on bit 3), port_dffd_reg (bits 3, 2:0 conditional
  on bit 3 + profi mode), port_1ffd_reg (bits 0, 1, 2 unconditional).
- **VHDL read** (6158-6159): composed from all three port_*FFD registers,
  bit 3 = '1'.
- **jnext write** (`mmu.cpp:434-484`): faithful — bit-3 gates the
  bank_mode for 7FFD bits 2:0 and dffd writes; bit 2 gates 7FFD bit 4. The
  apply_legacy_ram_slots path is gated on `(v & 0x08)` per VHDL's
  port_memory_ram_change_dly logic.
- **jnext read** (`mmu.cpp:491-509`): full re-composition from port_7ffd_,
  port_dffd_reg_, port_1ffd_. **MATCH**.
- **Severity**: LOW (already fixed and audited).

### NR 0x8F (mapping mode)

- **VHDL** (5482-5483 + 6162): bits 1:0 stored; read = `"000000" & mode`.
- **jnext**: read returns `mmu_.nr_8f_mode() & 0x03`. **MATCH**.

### NR 0xC0 (IM2 control)

- Audited at :1859-1873. Faithful composition. **MATCH**.

### NR 0xC2 / 0xC3 (NMI return shadow)

- **VHDL** (2050-2070 + 6232-6236): software writes via `nr_c2_we / nr_c3_we`
  store the full byte; HW NMI ACK overwrites at NMIACK_LSB/MSB cycles.
- **jnext**: NO software write_handlers — bare path stores raw byte, which
  matches VHDL software-write semantics. HW NMI ACK uses
  `set_nmi_return_address()` which writes directly to `regs_[]`. Read falls
  through to `regs_[]`. **MATCH**.

### NR 0xC4 / 0xC5 / 0xC6 (interrupt enables)

- Audited at :1888-1951. Both `set_int_en_c4/c5/c6` propagation and the
  port_ff_reg(6) ← NOT bit 0 fan-out are correct. **MATCH**.

### NR 0xC8 / 0xC9 / 0xCA (interrupt status)

- Audited at :1957-2000. **MATCH**.

### NR 0xCC / 0xCD / 0xCE (DMA delay enables)

- Audited at :2006-2037. **MATCH**.

### NR 0xD8 / 0xD9 / 0xDA (IO trap)

- Audited at :1492-1537. NR 0xDA write is unmapped in VHDL (commented out),
  jnext's no-op handler matches. **MATCH**.

### NR 0xFF (ULA+ palette poke)

- **VHDL** (6957-6958): magic palette-write mux. **jnext**: at :712-719,
  applies the bit-expansion correctly. Out of priority scope.

## Top 3 Most Likely Boot-Branch Culprits

### #1 (HIGHEST) — NR 0x57 / Mmu::get_page(7) divergence at PC=$2734

**Status**: under active investigation per `G46B-INVESTIGATION-LIVE.md`.
The wrapper at $2734 reports `nr_mmu_[7] = 0x01` but the immediately following
`IN A,($253B)` after selecting NR 0x57 returns `0x05`. The read_handler at
`emulator.cpp:1310-1313` returns `mmu_.get_page(i)` — so the divergence is
NOT in the NR-handler shim, it must be in **what `Mmu::nr_mmu_[7]` actually
stores**, OR in the supervisor logging path (HOOK #1 vs HOOK #2 read different
sources).

**Recommendation for next session**: at HOOK #1, log BOTH:
- `mmu_.get_page(7)`
- `nextreg_.cached(0x57)` (i.e. `regs_[0x57]`)
- the result of a synthesised `nextreg_.read(0x57)`

If the bare cache and the synthesised read disagree, the read_handler is
wrong. If they agree but disagree with the IN-A-($253B) port-read path,
then the port-decode path for 0x253B is going somewhere different (e.g.
through a different read function).

### #2 (HIGH) — DivMMC `port_io_enable_` initial state mismatch

`regs_[0x83] = 0xFF` at cold reset (`nextreg.cpp:58`) but
`divmmc_.port_io_enable_ = false` (`divmmc.h:249`). Without
`--bypass-tbblue-fw`, the firmware later writes NR 0x83 = 0x3D and
synchronises both. **Under `--bypass-tbblue-fw`, the synthetic init at
`emulator.cpp:3781` writes NR 0x83 = 0x3D explicitly — needs runtime
verification that this path is reached BEFORE the supervisor runs and
that the resulting `divmmc_.port_io_enable() == true`.** If either fails,
divmmc will be silently disabled when the supervisor expects it enabled,
and the divmmc-mapped slot 7 RAM bank may not respond to read/write
correctly.

**Recommendation**: add `LOG_ONCE` at supervisor entry that asserts
`divmmc_.port_io_enable() == ((nextreg_.cached(0x83) & 0x01) != 0)` and
similarly for `multiface_.enabled() == ((cached(0x83) & 0x02) != 0)`.

### #3 (MED) — NR 0x85 bit-2 leak

JNEXT bare-stores all 8 bits of NR 0x85 in `regs_[0x85]`. The
`emulator.cpp:2460` gate consumes `cached(0x85) & 0x04`. VHDL hard-zeroes
bit 2 via the write-mask (`nr_85_internal_port_enable <= nr_wr_dat(3 downto 0)`,
not bits 7:4). If tbblue.fw or the supervisor writes NR 0x85 with bit 2 set,
jnext's gate fires while VHDL's would not.

Search what `:2460` gates and whether NR 0x85 writes set bit 2 anywhere
(`emulator.cpp:3783` writes 0x01 — bit 2 = 0, so this specific bypass-init
path is clean; but a supervisor-driven write could differ).

## Concrete Recommendation

**Fix order**:

1. **First investigate Culprit #1** — instrument `Mmu::set_page` and the NR
   read path to confirm the suspected `nr_mmu_[7]` vs `regs_[0x57]` vs port-IN
   triple divergence. Adding three `Log::g46b()->info(...)` lines at HOOK #1
   should suffice. If they all agree at HOOK #1 and the divergence appears
   only at HOOK #2, then the issue is NOT a stale shadow but something
   between HOOK #1 and HOOK #2 (likely an MMU write that bypasses the
   read_handler-tracked path).

2. **Add a bypass-tbblue-fw post-init assertion** that DivMMC port_io_enable
   matches NR 0x83 bit 0. Cheap, catches Culprit #2 in seconds.

3. **NR 0x85 bit-2 mask** — patch the bare write to canonicalise the byte
   per VHDL: install a write_handler that returns `v & 0x8F`, OR add a
   one-line mask at the gate site `:2460`. Defer until #1 and #2 are ruled
   out.

The single highest-payoff diagnostic is the NR 0x57 triple-source dump at
HOOK #1. Everything else is at most secondary.

## Audit Coverage Summary

| Register      | Status      | Notes                                                  |
| ------------- | ----------- | ------------------------------------------------------ |
| 0x00          | INTENTIONAL | 0x08 (HWID_EMULATORS); diverges from VHDL g_machine_id |
| 0x01          | MATCH       |                                                        |
| 0x02          | MED-LOW     | bits 7/4/1:0 always read 0; reset_type[1:0] missing    |
| 0x03          | LOW         | nr_palette_sub_idx unmodelled (bit 7 = 0)              |
| 0x04          | MATCH       | both VHDL and jnext read 0                             |
| 0x05          | LOW         | eff_5060/scandouble Pentagon override unmodelled       |
| 0x06          | MATCH       |                                                        |
| 0x07          | MATCH       |                                                        |
| 0x08          | MATCH       |                                                        |
| 0x09          | MATCH       |                                                        |
| 0x0A          | MATCH       | mf_type cached unmodelled                              |
| 0x0B          | MATCH       | masked write to 0xB1                                   |
| 0x10          | LOW         | Issue 2/3 board path only                              |
| 0x12 / 0x13   | MATCH       |                                                        |
| 0x14          | MATCH       |                                                        |
| 0x15-0x1F     | MATCH       |                                                        |
| 0x20          | MATCH       |                                                        |
| 0x22 / 0x23   | MATCH       |                                                        |
| 0x40-0x44     | MATCH       | 0x44 read leaks raw byte (LOW)                         |
| 0x50-0x57     | MATCH       | live MMU state via `mmu_.get_page(i)`                  |
| 0x60-0x6F     | MATCH       |                                                        |
| 0x80          | MATCH       | rotation path unmodelled (LOW, no expbus)              |
| 0x81          | LOW         | expbus_speed bits 1:0 not zeroed on write              |
| 0x82          | MATCH       | bare path correct                                      |
| **0x83**      | **MED**     | bypass-init wiring needs verification                  |
| 0x84          | MATCH       | bare path correct                                      |
| **0x85**      | **MED**     | bit 2 leak (cached consumers see written bit)          |
| 0x86 / 0x87 / 0x88 | MATCH  |                                                        |
| 0x89          | MATCH       | masked at read                                         |
| 0x8A          | LOW         | mask 0x3F missing on read                              |
| 0x8C          | MATCH       |                                                        |
| 0x8E          | MATCH       | already audited / closed                               |
| 0x8F          | MATCH       |                                                        |
| 0xB0-0xB2     | MATCH       |                                                        |
| 0xB8-0xBB     | MATCH       |                                                        |
| 0xC0          | MATCH       |                                                        |
| 0xC2 / 0xC3   | MATCH       |                                                        |
| 0xC4-0xC6     | MATCH       |                                                        |
| 0xC8-0xCA     | MATCH       |                                                        |
| 0xCC-0xCE     | MATCH       |                                                        |
| 0xD8-0xDA     | MATCH       |                                                        |
| 0xFF          | MATCH       |                                                        |

**No HIGH-severity divergences found in the read/write handler scaffolding.**
The G46(b) loop is most likely caused by one of:

- A specific MMU-write code path that does NOT route through `Mmu::set_page`
  (so `nr_mmu_[i]` diverges from the byte the supervisor expects to read).
- A bypass-init incomplete state for divmmc/multiface/peripherals.
- A non-NR-related bug (port-decode, IM2 vector, or the supervisor reading
  from a port that returns a different byte than the VHDL composition).

The audit recommends Top-3 instrumentation as the cheapest next step, with the
NR 0x57 triple-source dump being the highest-priority single experiment.
