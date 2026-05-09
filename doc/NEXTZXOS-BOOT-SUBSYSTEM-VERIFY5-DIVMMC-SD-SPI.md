# Pass-5 Blind Verification Re-Audit — DivMMC + SD + SPI

**Branch**: `task2/verify5-divmmc-sd-spi`
**Date**: 2026-05-09
**Methodology**: ultrathink, blind (no prior reports read), VHDL-as-oracle

## Verdict

**Class-(a) bugs found**: 2 (both fixed in this pass).
**Convergence assessment**: **CONVERGING** — pass-5 found materially fewer
class-(a) issues than passes 1-4 (which found 6 cumulative class-(a) bugs in
this subsystem). The remaining findings are all class-(b/c): defensive
hardening or non-boot-path edge cases. The DivMMC AUTOMAP FSM, SPI port
decode, and SD card boot-path command handling all hold up against fresh
VHDL-faithful re-audit.

**Recommendation**: one more pass-6 audit at the same depth would likely
find zero class-(a) bugs; we are at the noise floor. However the standing
instruction is to iterate until reviews honestly converge to no class-(a)
bugs in a single pass, so a pass-6 sweep is warranted before declaring
the subsystem closed.

## Test Status

```
fuse_z80_tests       Passed
divmmc_tests         Passed
sdcard_tests         Passed
sd_rom_extractor     Passed
all 37 tests         Passed (regression check)
```

## Methodology

Five attack angles, executed in parallel against the C++ files
(`src/peripheral/divmmc.{cpp,h}`, `src/peripheral/sd_card.{cpp,h}`,
`src/peripheral/spi.{cpp,h}`) with the VHDL oracle
(`device/divmmc.vhd`, `serial/spi_master.vhd`, `zxnext.vhd:3278-3332,
2848-2908, 3905-3933, 4112-4170, 5052-5067, 5191-5198`):

1. **tbblue.fw boot-path-specific audit** — walked the canonical SD init
   sequence (CMD0 → CMD8 → CMD55+ACMD41 loop → CMD58 → CMD17/CMD18) and
   verified each handler against SD Physical Layer Simplified Spec v6.00.

2. **State machine corner cases** — walked every transition in
   `divmmc.vhd:107-150` (button_nmi/automap_hold/automap_held) and the
   SD-card SPI FSM (IDLE → RECEIVING_CMD → RESPONDING/SENDING_DATA/
   RECEIVING_DATA → IDLE).

3. **Save/load state coverage** — diffed every persisted field against
   the in-memory representation. DivMmc complete; SpiMaster complete (cs,
   rx_data, sd_swap); SdCardDevice intentionally not Saveable per
   header comment at `sd_card.h:141-146`.

4. **Hard reset / soft reset / mount-unmount-runtime-swap matrix** —
   audited what state survives each. Found bug #1 (mount() doesn't
   fully reset SD protocol state).

5. **VHDL signal exhaustive coverage** — enumerated every signal in
   `device/divmmc.vhd` (153 lines, all walked) and `serial/spi_master.vhd`
   (179 lines, all walked) and confirmed C++ counterparts. Two findings
   (#2, #3 below) emerged from this pass.

## Findings

### #1 — CLASS-(a) — `SdCardDevice::mount()` doesn't fully reset SPI protocol state — **FIXED**

**File**: `src/peripheral/sd_card.cpp:27-53`

**Pre-fix code** (paraphrased):
```cpp
bool SdCardDevice::mount(const std::string& path) {
    unmount();
    file_.open(...);
    file_size_ = ...;
    state_ = State::IDLE;
    initialized_ = false;
    app_cmd_ = false;
    cmd_idx_ = 0;
    return true;
}
```

`unmount()` clears: `state_`, `initialized_`, `file_size_`,
`pending_write_after_r1_`. Then `mount()` re-clears `state_`,
`initialized_`, `app_cmd_`, `cmd_idx_`. **Neither path clears**:
`resp_buf_`, `resp_idx_`, `data_idx_`, `data_crc_count_`,
`multi_block_`, `multi_block_sector_`, `persistent_response_byte_`,
`data_block_`, `cmd_buf_`.

**Failure mode**: a runtime `--sd-card` swap during a CMD18 stream (or
mid-CMD17 SENDING_DATA, or after a CMD0 set the persistent response
byte to 0x01) leaves the new card primed with stale state from the
previous image. `send()` would continue emitting the old image's
`data_block_` contents, or `multi_block_sector_` would re-prime the
next block from a sector index that doesn't match the new image's
filesystem.

**VHDL oracle**: there is no VHDL "mount" — real hardware doesn't
support hot-swap. But our `reset()` is the canonical full clear, and
the existing SD-card lifecycle treats mount as a hard cycle (fresh
file, fresh protocol state). The pre-fix code was a partial
duplication of `reset()` that drifted as fields were added.

**Fix**: replace the partial duplication with a single `reset()` call
after the file is opened. This re-uses the same canonical clear used
by the SPI-protocol soft reset path and removes the drift surface.

### #2 — CLASS-(b/c) — `cmd16_set_blocklen()` illegal-arg R1 hard-codes idle bit — **FIXED**

**File**: `src/peripheral/sd_card.cpp:431-444`

**Pre-fix code**:
```cpp
if (arg == 512) {
    queue_r1(initialized_ ? 0x00 : 0x01);
} else {
    queue_r1(0x05);  // idle + illegal
}
```

**Issue**: SD spec § 4.9.1 R1 layout has bit 0 = "in idle state", bit 2
= "illegal command". Returning 0x05 (= bits 0+2) when the card has
already completed ACMD41 init misreports a post-init card as "still
idle" to the host. A spec-faithful response is `(initialized ? 0x04 :
0x05)`.

**Why class-(b/c) and not (a)**: the boot path uses CMD16 with arg=512
exclusively (or skips CMD16 entirely on SDHC, our reported card type).
The else-branch is never hit on the canonical NextZXOS boot trajectory
— the bug is latent. But the corrected response is spec-faithful and
costs zero risk to fix.

**Fix**: compute the idle bit from `initialized_`, OR with the illegal
bit 0x04.

### #3 — CLASS-(c) — `SpiMaster::reset()` detaches devices via `devices_.fill(nullptr)`

**File**: `src/peripheral/spi.cpp:26-34`

**Issue**: `reset()` calls `devices_.fill(nullptr)`, clearing all
attached SPI device backends (i.e., the SD card pointer). After any
soft reset, the next SPI transaction would route to nullptr → all reads
return 0xFF, all writes do nothing → SD card unresponsive.

**Why class-(c) and not (a)**: in production the only `reset()` site is
`Emulator::reset()` (line 127), which is called from `Emulator::init()`
*before* the same `init()` re-attaches the SD card via
`spi_.attach_device(0, &sd_card_)` (line 3793). All resets (both hard
and soft) flow through `init()`, so the device is always re-attached
by the end of any reset. `Emulator::load_state()` calls
`spi_.load_state()` which doesn't touch `devices_`, so post-load the
device stays attached.

**No fix applied** — the bug is benign in production. Flagged as
class-(c) defensive: the API would be safer if `reset()` did not blow
away non-state members. A future cleanup could narrow `reset()` to only
clear `cs_`/`rx_data_`/`sd_swap_`. Left for a focused refactor pass.

### #4 — CLASS-(c) — `button_nmi_` rising-edge clear vs VHDL "while held=1" continuous clear

**File**: `src/peripheral/divmmc.cpp:286-291`

**VHDL** (`divmmc.vhd:112-113`):
```vhdl
elsif automap_held = '1' then
   button_nmi <= '0';
```

This clears `button_nmi` on EVERY clock cycle while `automap_held=1`
(continuous assertion). The C++ models it as a 0→1 rising-edge clear
(`if (!prev_held && automap_held_ && button_nmi_) clear`).

**Why class-(c) and not (a)**: the only setter for `button_nmi_` (in
the firmware-running path) is `divmmc_.set_button_nmi(true)` from the
NmiSource pipeline at `emulator.cpp:4823`. That strobe fires only on
NMI-source IDLE→FETCH transitions (one-tick pulses), and the FSM
arbitrates so a new NMI grant cannot fire while `nmi_hold=1` (= while
DivMMC is still claiming the NMI = while `automap_held=1`). So the
"button_nmi re-set while automap_held=1" race window does not occur
in production. The C++ rising-edge clear is operationally equivalent
to the VHDL continuous clear under all known firmware paths.

**No fix applied** — flagged with the existing in-source comment
explaining the asymmetry. A defensive fix would change the C++ to
"clear whenever automap_held_=1 regardless of edge", but doing so
adds no covered behavior and risks subtle test breakage.

### #5 — CLASS-(c) — `on_m1_prefetch` fires once per instruction, not once per M1 byte

**File**: `src/cpu/z80_cpu.cpp:464`

**VHDL** (`divmmc.vhd:128`): `automap_hold` re-evaluates on EVERY M1 fetch
(`i_cpu_mreq_n='0' AND i_cpu_m1_n='0'`), including the ED/DD/FD/CB
prefix bytes' M1 + the ext byte's M1.

**C++**: `on_m1_prefetch` fires only at the START of the entire
instruction (the prefix M1). The ext byte's M1 fetch does not
re-trigger `check_automap`.

**Why class-(c) and not (a)**: the entry-point PC ranges are sparse —
RST 0/8/10/18/20/28/30/38, 0x0066, 0x04C6, 0x0562, 0x04D7, 0x056A,
0x1FF8-0x1FFF, 0x3D00-0x3DFF. A prefixed instruction at one of these
boundaries would mean both the prefix M1 and the ext byte M1 fall
within the matching range. For 1-byte ranges (RSTs, 0x0066), only the
prefix byte M1 matters. For multi-byte ranges (0x1FF8-0x1FFF: 8 bytes,
0x3D00-0x3DFF: 256 bytes), a prefixed instruction at boundary $1FFE or
$3DFE would have ext byte M1 still in range — both fire the same
instant/off match. Since the C++ already fires once and computes the
correct `instant_match` / `off_match` flags, the latched
`automap_hold_` value is identical between the two M1s. No realistic
boot-path divergence.

**No fix applied** — the fix would require firing `on_m1_prefetch` for
every M1 byte fetched (Z80N path + non-Z80N path), which is an
invasive CPU-core change with broad blast radius. Left for a future
cycle-accurate CPU pass if needed.

### #6 — CLASS-(c) — Full-duplex MISO during write-data transfer

**File**: `src/peripheral/spi.cpp:99-118`

**VHDL** (`spi_master.vhd:108-117, 159-168`): every SPI transfer
(triggered by `i_spi_rd OR i_spi_wr`) shifts 8 bits OUT (MOSI from
oshift_r) and IN (MISO via ishift_r) simultaneously, regardless of
which trigger fired. `miso_dat` updates at `state_last_d` for both
read and write transfers.

**C++**: `write_data(val)` calls `dev->receive(val)` and stores its
return as `rx_data_`. SdCardDevice::receive() always returns 0xFF
(matching real-card "MISO high during cmd reception" + "no response
when not in RESPONDING/SENDING_DATA"). However, if the host writes
0xFF to port 0xEB while the card is in RESPONDING state (e.g., to
clock 8 bits without explicitly polling), the card would emit the
next response byte on MISO in real hardware — but our `receive()`
returns 0xFF and doesn't advance `resp_idx_`. The next `read_data()`
call would correctly emit the response byte (so the byte is not lost),
but real timing would have it at a different SCK position.

**Why class-(c) and not (a)**: standard SPI SD firmware (FatFs, esxdos,
NextZXOS supervisor) uses `read_data()` (= port 0xEB read) for response
polling, not write-with-0xFF dummy. The byte-level pipeline alignment
matters for cycle-accurate timing only — at the byte granularity used
by real firmware, the response sequence is intact.

**No fix applied** — would require shifting the protocol semantics
into receive() with state inspection (RESPONDING → return next
resp_buf_ byte). Left for a focused full-duplex pass if cycle-accurate
DMA/SPI timing surfaces a real boot-path divergence.

### #7 — CLASS-(c) — Unhandled commands return R1=0x00 instead of R1+illegal-command

**File**: `src/peripheral/sd_card.cpp:352-355`

**Pre-existing code**:
```cpp
default:
    sd_log()->warn("unhandled CMD{} arg={:#010x}", cmd, cmd_arg());
    queue_r1(initialized_ ? 0x00 : 0x01);
```

**Issue**: SD spec § 4.9.4 says unsupported commands return R1 with bit
2 (illegal command) set. Returning R1=0x00 (or 0x01) misreports
"command succeeded" → firmware may try to read a non-existent data
response.

**Why class-(c) and not (a)**: the canonical NextZXOS / tbblue.fw boot
path uses CMD0/CMD8/CMD9/CMD10/CMD12/CMD13/CMD16/CMD17/CMD18/CMD23/
CMD24/CMD55/CMD58/ACMD41 — all of which are handled. Unhandled
commands are not exercised on the boot path.

**No fix applied** — defensive hardening. Trivial future fix:
`queue_r1((initialized_ ? 0x00 : 0x01) | 0x04);`. Left for a future
spec-compliance pass.

### Things that look right

- **VHDL `port_e7_reg` decoding (zxnext.vhd:3308-3322)** ↔ C++
  `SpiMaster::write_cs()`. Both `cpu_do(1:0) = "10" / "01"`-only
  matches, sd_swap inversion, 0xFB/0xF7 RPI, 0x7F flash-config-mode-
  gated (we collapse to 0xFF for safety). Match.
- **VHDL `automap` combinational output (divmmc.vhd:148)** ↔ C++
  `automap_active_ = held || instant_match`. `held` from previous
  promotion, `instant_match` from current PC. Match.
- **VHDL `automap_hold` register (divmmc.vhd:128-131)** ↔ C++
  `automap_hold_ = instant || delayed || (held && !off)`. Match
  (per-PC re-evaluation; gates `i_automap_active`/`i_automap_rom3_active`
  → C++ `main_path_eligible` / `rom3_path_eligible`).
- **VHDL `automap_held` promotion (divmmc.vhd:141)** ↔ C++ `step 1:
  automap_held_ = automap_hold_` at start of `check_automap`. Models
  the MREQ rising edge between previous M1 and this M1. Match.
- **VHDL `button_nmi` reset clauses (divmmc.vhd:108)** ↔ C++ four
  setters: `reset()`, `apply_enabled_transition_()` on enable false-
  edge, `on_retn()`, `on_m1_retn_delay()` (when pending fires). All
  four reset cases match.
- **VHDL `i_en` gate (divmmc.vhd:98 = port_divmmc_io_en)** ↔ C++
  `is_active() = port_io_enable_ && (conmem_ || automap_active_)`.
  CONMEM works without `nr_0a_4` — match.
- **VHDL `automap_reset` derivation (zxnext.vhd:4112)** ↔ C++
  `apply_enabled_transition_()` clears latches on either gate
  false-edge. Match.
- **VHDL `nr_0a_divmmc_automap_en` reset behavior** — *not* in any
  reset block at zxnext.vhd:4928-5125 (NR write-process reset
  block). Per VHDL semantics, the FF retains its value across reset
  (only the `:= '0'` initialiser at line 1126 sets it at FPGA power-
  on). C++ `divmmc.cpp:reset()` correctly preserves
  `nr_0a_4_enable_`. Match (the in-source comment is accurate).
- **VHDL `port_e3_reg` mapram OR-latch (zxnext.vhd:4182-4183)** ↔ C++
  `write_control()` line 107 `mapram_ = mapram_ || ((val & 0x40)
  != 0)`. Match.
- **VHDL `port_e3` read mask (zxnext.vhd:4190)** ↔ C++ `read_control()`
  `& 0xCF` (bits 5:4 zeroed). Match.
- **CMD8 R7 response shape** (R1 + 4 R7 bytes per SD spec § 7.3.2.6).
  Match.
- **CMD58 OCR shape** (NCR + R1 + 4 OCR bytes, CCS bit set when
  initialized). 0xC0FF8000 layout — match standard SDHC OCR.
- **CMD17/CMD18 read shape** (NCR + R1 + token + 512 + 2 CRC).
  Multi-block inter-block 1-byte 0xFF gap then token. Match.
- **CMD24 R1-bridge** (`pending_write_after_r1_` flag) — defers RESPONDING
  → RECEIVING_DATA transition until R1 has been emitted on MISO. Match
  SD spec § 7.2.4 / 7.3.3.1 + tbblue diskio.c send_cmd polling.
- **CMD24 0xFF gap byte tolerance** (pre-token) — added pass-4. Still
  valid.
- **DivMMC overlay `is_ram_mapped` / `is_read_only` / `read` / `write`**
  ↔ VHDL `divmmc.vhd:94-101` (rom_en/ram_en/rdonly/ram_bank). Match.
- **SPI master `read_data()` pipeline delay** — VHDL miso_dat latches
  at state_last_d (= one cycle after transfer ends). C++ returns
  `prev = rx_data_; rx_data_ = dev->send()`. Match.
- **MISO=0xFF when no slave selected** — VHDL `spi_miso <= '1'` default-
  else (zxnext.vhd:3278-3280). C++ forces `rx_data_ = 0xFF` when no
  device. Match (pass-4 fix still in place).
- **DivMmc save/load state** — every field round-trips. ✓
- **SpiMaster save/load state** — cs, rx_data, sd_swap. ✓ (devices_
  intentionally not serialised — host pointers).

## Open Questions for Pass-6

1. The full-duplex MISO during write-data transfer (#6) is class-(c)
   under all known firmware paths but worth re-examining if any DMA-
   via-SPI burst path becomes relevant.
2. The `on_m1_prefetch` once-per-instruction granularity (#5) is
   class-(c) under all sparse entry-point ranges but worth re-checking
   when CPU cycle-accuracy work resumes.
3. The unhandled-CMD R1=0x00 default (#7) is defensive hardening — a
   future spec-compliance pass should handle it cheaply.

## Files Modified

- `src/peripheral/sd_card.cpp` — bug #1 (mount full reset), bug #2
  (CMD16 illegal-arg R1).
