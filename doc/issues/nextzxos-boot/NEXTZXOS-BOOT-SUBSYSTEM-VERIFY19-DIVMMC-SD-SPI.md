# NextZXOS boot — Pass-19 verify-audit: DivMMC + SD card + SPI

**Branch:** `task2/verify19-divmmc-sd-spi`
**Integration HEAD parent:** `ce11e9c`
**Audit date:** 2026-05-10 (Pass-19)
**Scope:** `src/peripheral/divmmc.{h,cpp}` + `src/peripheral/sd_card.{h,cpp}` +
`src/peripheral/spi.{h,cpp}` + DivMMC/SD-related NRs and ports in
`src/core/emulator.cpp` + `src/core/sd_rom_extractor.{h,cpp}`.

---

## Enumeration table

Coverage target derivation (raw surface counts):

| Source surface count | Method |
|----------------------|--------|
| `register_handler` calls in scope (`src/core/emulator.cpp` ports `0xE3 / 0xE7 / 0xEB`) | 3 |
| NR registers (read + write rows) — `0x0A`, `0x09 b3`, `0xB8`, `0xB9`, `0xBA`, `0xBB` | 11 (most reg has read + write) |
| Automap entry-point PCs in scope (RST + NMI + tape traps + 1FF8 + 3Dxx) | 14 (8 RST + NMI + 4 tape + range + wildcard) |
| `SdCardDevice` SPI commands modelled (CMD0/1/8/9/10/12/13/16/17/18/23/24/55/58 + ACMD41) | 15 |
| `SpiMaster` surfaces (`reset` / `write_cs` / `read_cs` / `write_data` / `read_data` / `set_sd_swap` / `set_flash_cs_enable` / `spi_wait_n` / `attach_device`) | 9 |
| `DivMmc` surfaces (`reset / write_control / read_control / clear_mapram / check_automap (paths) / set_enabled+ split / set_button_nmi / set_layer2_map_read / set_rom3_active / on_retn / on_m1_retn_delay / is_active / is_ram_mapped / is_read_only / read / write / save_state / load_state`) | ≈ 22 (counting per state-machine arm) |

The table below has **80 rows** covering every surface above; ✓ rows cite a specific
VHDL line, ✗ rows are findings (see per-finding sections after the table).

| # | Surface (file:line)                                               | C++ behavior                                                                         | VHDL oracle (file:line)                                                                                                  | Match | Notes                                                                                                                                                                       |
|---|-------------------------------------------------------------------|--------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|-------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1 | `divmmc.cpp:29-49 reset()`                                         | clears conmem/mapram/bank/control_reg/automap*/button_nmi/layer2_map_read/retn pending; resets entry-point defaults to 0x83/0x01/0x00/0xCD; preserves enabled/port_io/nr_0a_4 | `zxnext.vhd:4176-4177 (port_e3_reg<=0)`, `zxnext.vhd:5087-5090 (NR B8/B9/BA/BB defaults)`, `divmmc.vhd:108,126,139 (reset clears button_nmi/hold/held)`, `zxnext.vhd:1124-1128 (NR 0a fields no-reset)` | ✓ | Defaults match VHDL; no-reset preservation matches initial-value-only signals.                                                                                              |
| 2 | `divmmc.cpp:105-113 write_control()`                               | conmem←val(7); mapram OR-latches with prev; bank←val(3:0); control_reg keeps mapram OR-latched, drops 5:4 from raw input | `zxnext.vhd:4180-4183 (port_e3_wr branch)` | ✓ | OR-latch on bit 6 matches `cpu_do(6) or port_e3_reg(6)`. Bits 5:4 in `control_reg_` follow `val` raw, but read mask hides them — see Finding F19-DIVMMC-NIT-01. |
| 3 | `divmmc.cpp:115-117 read_control()`                                | returns control_reg & 0xCF (zeros bits 5:4)                                          | `zxnext.vhd:4190 (port_e3_dat <= reg(7:6) & "00" & reg(3:0))`                                                            | ✓ | Read mask matches.                                                                                                                                                          |
| 4 | `divmmc.cpp:121-125 clear_mapram()`                                | clears mapram_ + clears bit 6 of control_reg                                         | `zxnext.vhd:4184-4185 (NR 0x09 b3 → port_e3_reg(6) := '0')`                                                              | ✓ | Driven from NR 0x09 write at `emulator.cpp:4067-4069`.                                                                                                                       |
| 5 | `divmmc.cpp:151-170 set_enabled()`                                 | flips port_io_enable_ only; recomputes enabled = port_io & nr_0a_4; clears latches on falling edge | `zxnext.vhd:4112 (automap_reset = port_io_en=0 OR nr_0a_automap_en=0)`, `divmmc.vhd:108,126,139` | ✓ | Falling edge clear matches the VHDL FF reset clause path.                                                                                                                   |
| 6 | `divmmc.cpp:172-176 set_port_io_enable()`                          | port_io_enable_←v; recompute enabled                                                  | same as row 5                                                                                                            | ✓ |                                                                                                                                                                             |
| 7 | `divmmc.cpp:178-182 set_nr_0a_4_enable()`                          | nr_0a_4_enable_←v; recompute enabled                                                  | `zxnext.vhd:5196 (NR 0x0A bit 4 → nr_0a_divmmc_automap_en)` + `zxnext.vhd:4112`                                          | ✓ |                                                                                                                                                                             |
| 8 | `divmmc.cpp:186-210 on_retn()`                                     | one-shot clear of automap_active/hold/held/button_nmi/retn_pending                   | `divmmc.vhd:108,126,139 (i_retn_seen sets all three FFs to 0)`                                                            | ✓ | Used by direct test callers; production path uses `on_m1_retn_delay` instead.                                                                                                |
| 9 | `divmmc.cpp:212-244 on_m1_retn_delay()`                            | one-M1-cycle delay register; on apply: clears automap_active/hold/held/button_nmi    | `divmmc.vhd:108,126,139` + `zxnext.vhd:4111 (divmmc_retn_seen = z80_retn_seen and not mf_is_active)`                     | ✓ | Wired from on_m1_cycle with the VHDL F-gate (`!multiface_.is_active()`) at `emulator.cpp:670`.                                                                              |
| 10 | `divmmc.cpp:248-251 check_automap() guard`                        | early return when not M1 OR not enabled                                              | `divmmc.vhd:128 (cpu_mreq_n=0 AND cpu_m1_n=0 gate)` + `zxnext.vhd:4112 (reset gate)`                                     | ✓ | Combined gate is correct.                                                                                                                                                   |
| 11 | `divmmc.cpp:273-274 hold→held promotion`                          | `automap_held_ = automap_hold_;`                                                      | `divmmc.vhd:141-142 (cpu_mreq_n=1 → automap_held<=automap_hold)`                                                          | ✓ | Models the M1's MREQ rising edge.                                                                                                                                            |
| 12 | `divmmc.cpp:294-299 button_nmi clear while held`                  | clears button_nmi if post-promoted held=1                                            | `divmmc.vhd:112-113 (continuous-while-held button_nmi clear)`                                                             | ✓ | Pass-8 fix; uses post-promotion held = same as VHDL FF value during this M1's data-phase clocks.                                                                            |
| 13 | `divmmc.cpp:332-335 main_path_eligible / rom3_path_eligible`      | computes per-VHDL gates: main = pre_override(2); rom3 = pre_override(2 AND 0) AND !L2map AND rom3_active | `zxnext.vhd:3137-3138 (sram_divmmc_automap_en / sram_divmmc_automap_rom3_en)`                                            | ✓ | Documented simplification: omits `sram_romcs` and altrom branch; OK for boot path (per inline comment).                                                                     |
| 14 | `divmmc.cpp:340-358 RST entry decode (NR 0xB8)`                   | for each i in 0..7: pc==rst_addr[i] AND ep_0[i] AND eligibility                       | `zxnext.vhd:2848-2890 (cpu_a(7:6)="00" AND cpu_a(2:0)="000" → cpu_a(5:3) selects index)`                                | ✓ | Equivalent to per-index match on $0000/$0008/.../$0038.                                                                                                                      |
| 15 | `divmmc.cpp:343 instant vs delayed timing`                        | `instant = entry_timing_0_ & (1<<i); valid → main path; !valid → rom3 path`           | `zxnext.vhd:2892-2901 (instant_on/delayed_on split on validity bit)`                                                      | ✓ |                                                                                                                                                                             |
| 16 | `divmmc.cpp:372-381 NMI@$0066 (NR 0xBB bits 0/1)`                | requires button_nmi_ AND main_path_eligible; bit 1 → instant; bit 0 → delayed         | `divmmc.vhd:120-121 (automap_nmi_*_on AND button_nmi)` + `zxnext.vhd:2907-2908`                                          | ✓ |                                                                                                                                                                             |
| 17 | `divmmc.cpp:382-396 ROM3 tape traps (bits 2-5)`                   | bit2→04C6, bit3→0562, bit4→04D7, bit5→056A; rom3_path_eligible                         | `zxnext.vhd:2902-2905 (port_04xx+c6 / 05xx+62 / 04xx+d7 / 05xx+6a)`                                                       | ✓ | Address mapping matches per-line.                                                                                                                                            |
| 18 | `divmmc.cpp:406-408 $3Dxx wildcard (bit 7)`                       | rom3_path_eligible + (pc & 0xFF00) == 0x3D00 → instant_match                           | `zxnext.vhd:2898-2899 (port_3dxx_msb AND nr_bb_divmmc_ep_1(7))`                                                           | ✓ |                                                                                                                                                                             |
| 19 | `divmmc.cpp:409-422 auto-unmap $1FF8-$1FFF (bit 6)`               | main_path_eligible + range check → off_match                                          | `zxnext.vhd:2896 (port_1fxx_msb AND cpu_a(7:3)="11111" AND nr_bb(6))` + `divmmc.vhd:131 (off-fire factor)`              | ✓ | Off match gated by `i_automap_active` (`main_path_eligible`) per VHDL line 131.                                                                                              |
| 20 | `divmmc.cpp:427-429 hold update`                                  | `hold = (instant or delayed) || (held && !off)`                                       | `divmmc.vhd:129-131 (automap_hold load)`                                                                                  | ✓ | Equivalent OR.                                                                                                                                                              |
| 21 | `divmmc.cpp:434-435 active output`                                | `automap_active_ = held || instant_match`                                             | `divmmc.vhd:148 (automap = (held OR (active AND instant_*) OR (rom3 AND rom3_instant_*)))`                                | ✓ | C++ `instant_match` already gated by main/rom3 path eligibility.                                                                                                            |
| 22 | `divmmc.cpp:449-459 is_ram_mapped()`                              | page0+mapram OR page1, AND is_active                                                  | `divmmc.vhd:95 (ram_en computation)`                                                                                      | ✓ |                                                                                                                                                                             |
| 23 | `divmmc.cpp:461-471 is_read_only()`                               | slot 0 always RO; slot 1 RO when mapram + bank=3                                     | `divmmc.vhd:100 (rdonly = page0 OR (mapram AND bank=3))`                                                                  | ✓ |                                                                                                                                                                             |
| 24 | `divmmc.cpp:481-498 read()`                                       | slot 0: ROM or RAM page 3 if mapram; slot 1: RAM[bank]                                | `divmmc.vhd:96 (ram_bank = 3 if page0 else reg(3:0))` + memory mux at `zxnext.vhd:3081-3099`                              | ✓ |                                                                                                                                                                             |
| 25 | `divmmc.cpp:500-527 write()`                                      | slot 0 always discarded; slot 1 RO when mapram + bank=3                                | `divmmc.vhd:100 (rdonly), zxnext.vhd:3097 (sram_rdonly = divmmc_rdonly)`                                                  | ✓ |                                                                                                                                                                             |
| 26 | `divmmc.cpp:529-572 save_state() schema`                          | enabled, conmem, mapram, bank, control_reg, automap_active, ep0/valid0/timing0/ep1, hold/held, button_nmi, layer2_map_read, retn_pending, RAM, port_io_enable, nr_0a_4_enable | n/a — host-side schema; no VHDL backing                                                                                  | ✓ | "no VHDL backing" — intentional; mirrors all VHDL FFs.                                                                                                                       |
| 27 | `divmmc.cpp:574-609 load_state()`                                 | mirror of save                                                                        | n/a                                                                                                                       | ✓ | Append-only schema; pre-2026-05-04 snapshots intentionally incompatible.                                                                                                    |
| 28 | `emulator.cpp:4344-4352 port 0xE3 read+write`                     | both gate on NR 0x83 bit 0 (port_divmmc_io_en); read→divmmc_.read_control(); write→divmmc_.write_control() | `zxnext.vhd:2412 (port_divmmc_io_en=NR 0x83 b0)`, `:2608 (port_e3 gate)`, `:2727-2728 (rd/wr signals)`, `:2815 (rd_dat)` | ✓ |                                                                                                                                                                             |
| 29 | `emulator.cpp:4260-4265 port 0xE7 (write-only)`                   | write→spi_.write_cs(); read→nullptr→0xFF                                              | `zxnext.vhd:614-622 (only port_e7_wr exists)` + `:1875-1877 (no internal port_e7_rd path → cpu_di='1' default)`         | ✓ | V16 fix; documented.                                                                                                                                                        |
| 30 | `emulator.cpp:4266-4274 port 0xEB read+write`                     | gate on NR 0x83 bit 3 (port_spi_io_en); read→spi_.read_data(); write→spi_.write_data() | `zxnext.vhd:2419 (port_spi_io_en=NR 0x83 b3)`, `:2620-2621 (port_eb gate)`, `:2736-2737 (rd/wr)`, `:2817 (rd_dat)`     | ✓ |                                                                                                                                                                             |
| 31 | `emulator.cpp:1061-1095 NR 0x0A write` (gated bits)               | bit 5 (sd_swap) and bits 7:6 (mf_type) gated on config_mode; bit 4 (automap_en) ungated; bits 3,1:0 mouse | `zxnext.vhd:5191-5198`                                                                                                    | ✓ | V11-NMP-02 stale-cache canonicalisation.                                                                                                                                    |
| 32 | `emulator.cpp:1113-1122 NR 0x0A read`                             | composes from authoritative subsystem state (multiface mf_type, spi sd_swap, divmmc nr_0a_4, mouse) | `zxnext.vhd:5912 (port_253b_dat composition for NR 0x0A read)`                                                            | ✓ | Pass-3 verify-audit fix.                                                                                                                                                    |
| 33 | `emulator.cpp:4062-4078 NR 0x09 write (bit 3 → clear_mapram)`     | if v & 0x08 → divmmc_.clear_mapram()                                                  | `zxnext.vhd:4184-4185`                                                                                                    | ✓ |                                                                                                                                                                             |
| 34 | `emulator.cpp:2477 NR 0xB8 write_handler`                         | divmmc_.set_entry_points_0(v)                                                         | `zxnext.vhd:5584-5585`                                                                                                    | ✓ |                                                                                                                                                                             |
| 35 | `emulator.cpp:2478 NR 0xB9 write_handler`                         | divmmc_.set_entry_valid_0(v)                                                          | `zxnext.vhd:5587-5588`                                                                                                    | ✓ |                                                                                                                                                                             |
| 36 | `emulator.cpp:2479 NR 0xBA write_handler`                         | divmmc_.set_entry_timing_0(v)                                                         | `zxnext.vhd:5590-5591`                                                                                                    | ✓ |                                                                                                                                                                             |
| 37 | `emulator.cpp:2480 NR 0xBB write_handler`                         | divmmc_.set_entry_points_1(v)                                                         | `zxnext.vhd:5593-5594`                                                                                                    | ✓ |                                                                                                                                                                             |
| 38 | `emulator.cpp:2481 NR 0xB8 read_handler`                          | returns divmmc_.entry_points_0() (live)                                                | `zxnext.vhd:6217-6218`                                                                                                    | ✓ | V17-NMP-01 fix.                                                                                                                                                              |
| 39 | `emulator.cpp:2482 NR 0xB9 read_handler`                          | returns divmmc_.entry_valid_0() (live)                                                 | `zxnext.vhd:6220-6221`                                                                                                    | ✓ |                                                                                                                                                                             |
| 40 | `emulator.cpp:2483 NR 0xBA read_handler`                          | returns divmmc_.entry_timing_0() (live)                                                | `zxnext.vhd:6223-6224`                                                                                                    | ✓ |                                                                                                                                                                             |
| 41 | `emulator.cpp:2484 NR 0xBB read_handler`                          | returns divmmc_.entry_points_1() (live)                                                | `zxnext.vhd:6226-6227`                                                                                                    | ✓ |                                                                                                                                                                             |
| 42 | `emulator.cpp:670 on_m1_cycle wires retn_seen with MF gate`       | on_m1_retn_delay(im2_.retn_seen_this_cycle() && !multiface_.is_active())              | `zxnext.vhd:4111 (divmmc_retn_seen = z80_retn_seen AND NOT mf_is_active)`                                                | ✓ |                                                                                                                                                                             |
| 43 | `emulator.cpp:578-608 on_m1_prefetch fans pre_override gates`     | computes pre_override(2) and (0) per MF/config_mode; calls divmmc_.check_automap(pc, true, …) | `zxnext.vhd:3037-3057, 4137-4171 (sram_pre_override, divmmc_mod port map)`                                                | ✓ | G46(b) infrastructure.                                                                                                                                                      |
| 44 | `emulator.cpp:5723-5724 button_nmi gating`                        | if `nmi_source_.divmmc_button_strobe() && divmmc_.is_enabled()` → set_button_nmi(true) | `divmmc.vhd:107-114` + automap_reset gate                                                                                  | ✓ | Production caller honours VHDL gate; div_mmc.h:209-216 documents the contract.                                                                                              |
| 45 | `sd_card.cpp:27-61 mount()`                                       | tries RW, falls back to RO; full reset() (V8 fix); logs                              | host-side, no VHDL                                                                                                        | ✓ | "no VHDL backing" — host SD image management; full reset() avoids stale state.                                                                                              |
| 46 | `sd_card.cpp:63-82 unmount()`                                     | closes file; full reset() (V8 fix); clears file_size_                                | host-side                                                                                                                | ✓ |                                                                                                                                                                             |
| 47 | `sd_card.cpp:84-111 deselect()`                                   | resets state/cmd/resp/data*/data_token/app_cmd/multi_block/pending_write/persistent  | host-side (CS deassert is host-driven; no VHDL clock equivalent)                                                          | ✓ | V11/V12 fix; symmetric with reset().                                                                                                                                        |
| 48 | `sd_card.cpp:122-140 receive() IDLE → cmd_start_byte detection`   | (tx & 0xC0) == 0x40 → cmd_idx_=1, state→RECEIVING_CMD, persistent←0xFF                | SPI-protocol behaviour (no VHDL — the SD card chip is external to the FPGA core, host SD-MISO line is `i_SPI_SD_MISO`)    | ✓ | "no VHDL backing" — SD device behaviour from SD spec.                                                                                                                       |
| 49 | `sd_card.cpp:142-147 receive() RECEIVING_CMD`                     | collect 6 cmd bytes, then process_command()                                           | n/a                                                                                                                       | ✓ |                                                                                                                                                                             |
| 50 | `sd_card.cpp:149-178 receive() RECEIVING_DATA pre-token`          | wait for 0xFE token; ignore pre-token bytes                                           | n/a                                                                                                                       | ✓ | V12-DIVMMC-06 fix (data_token_received_ flag).                                                                                                                              |
| 51 | `sd_card.cpp:175-178 RECEIVING_DATA collect 512`                  | collect data_block_[0..511]                                                           | n/a                                                                                                                       | ✓ |                                                                                                                                                                             |
| 52 | `sd_card.cpp:179-256 RECEIVING_DATA CRC + finalize`               | 2 CRC bytes; then conditional past-EOF check → write_ok / 0x0D; flush; emit token    | SD spec § 7.3.3.3 (data response token) — V12-DIVMMC-02 / V15-DIVMMC-01                                                  | ✓ |                                                                                                                                                                             |
| 53 | `sd_card.cpp:259-312 receive() default (RESPONDING/SENDING_DATA/WRITE_RESP)` | new cmd start byte → reset state for new CMD; else delegate to send() (V18 NIT-01) | SD spec full-duplex behaviour                                                                                            | ✓ | V18-DIVMMC-NIT-01 fix.                                                                                                                                                       |
| 54 | `sd_card.cpp:316-330 send() IDLE`                                  | returns persistent_response_byte_                                                     | n/a                                                                                                                       | ✓ | ZEsarUX-style sustained byte.                                                                                                                                               |
| 55 | `sd_card.cpp:331-333 send() RECEIVING_CMD`                        | returns 0xFF (cmd not complete)                                                       | n/a                                                                                                                       | ✓ |                                                                                                                                                                             |
| 56 | `sd_card.cpp:335-363 send() RESPONDING`                            | emit resp_buf_; on last byte: pending_write_after_r1_ → RECEIVING_DATA, else IDLE     | SD spec § 7.2.4 / § 7.3.3.1                                                                                              | ✓ |                                                                                                                                                                             |
| 57 | `sd_card.cpp:365-437 send() SENDING_DATA`                         | emit token + 512 + CRC; CMD18 mid-stream past-EOF → 0x08 token (V14-DIVMMC-01)        | SD spec § 7.3.3.3                                                                                                         | ✓ |                                                                                                                                                                             |
| 58 | `sd_card.cpp:439-440 send() RECEIVING_DATA`                        | returns 0xFF (host shouldn't read here, line idle)                                    | n/a                                                                                                                       | ✓ |                                                                                                                                                                             |
| 59 | `sd_card.cpp:442-447 send() WRITE_RESP`                            | emit response token, then IDLE                                                        | n/a                                                                                                                       | ✓ |                                                                                                                                                                             |
| 60 | `sd_card.cpp:453-515 process_command() dispatch`                  | ACMD41 if app_cmd_, else fall-through; switch on cmd code; default→illegal R1 (V8 fix) | SD spec § 7.3.2.1 (R1 illegal command bit), Pass-9 fall-through                                                          | ✓ |                                                                                                                                                                             |
| 61 | `sd_card.cpp:517-521 cmd1_send_op_cond()`                         | initializes; queue R1=0x00                                                            | SD spec § 7.3.1.3                                                                                                         | ✓ |                                                                                                                                                                             |
| 62 | `sd_card.cpp:523-545 cmd0_go_idle()`                              | initialised←false; queue R1=0x01; persistent←0x01                                    | SD spec § 7.3.2.1                                                                                                         | ✓ |                                                                                                                                                                             |
| 63 | `sd_card.cpp:547-584 cmd8_send_if_cond()`                         | queue R7 = NCR + R1(reflects init state V14) + 0x10 + 0x00 + 0x01 + check echo (V12-DIVMMC-03) | SD spec § 7.3.2.6                                                                                                          | ✓ |                                                                                                                                                                             |
| 64 | `sd_card.cpp:586-612 cmd12_stop_transmission()`                   | abort multi_block; resp = 8 stuff bytes + NCR + R1; persistent←0xFF                  | SD spec § 7.3.2.7                                                                                                         | ✓ |                                                                                                                                                                             |
| 65 | `sd_card.cpp:614-623 cmd13_send_status()`                         | NCR + R1 + 0x00 (R2 byte 1)                                                           | SD spec § 7.3.2.3                                                                                                         | ✓ |                                                                                                                                                                             |
| 66 | `sd_card.cpp:625-646 cmd16_set_blocklen()`                        | only arg=512 OK; else illegal command bit (V5 fix idle bit reflects state)            | SD spec § 4.9.1                                                                                                          | ✓ |                                                                                                                                                                             |
| 67 | `sd_card.cpp:648-654 cmd23_set_block_count()`                     | ack with R1                                                                            | SD spec — block count hint                                                                                                | ✓ |                                                                                                                                                                             |
| 68 | `sd_card.cpp:656-696 cmd17_read_single_block()`                   | initialised gate; past-EOF → R1 b6 + 0x08 token (V12-DIVMMC-04); else load + RESPONDING → SENDING_DATA chain | SD spec § 7.3.3.2                                                                                                          | ✓ |                                                                                                                                                                             |
| 69 | `sd_card.cpp:698-742 cmd18_read_multiple_block()`                 | initialised gate; past-EOF → R1 b6 + 0x08 token; else multi_block_=true; first block via SENDING_DATA | SD spec § 7.3.3.2                                                                                                          | ✓ |                                                                                                                                                                             |
| 70 | `sd_card.cpp:744-799 cmd24_write_single_block()`                  | initialised gate; past-EOF early-reject R1 b6 (V13); else queue R1=0x00 + pending_write_after_r1_=true | SD spec § 4.3.4 + § 7.3.2.1                                                                                                | ✓ |                                                                                                                                                                             |
| 71 | `sd_card.cpp:801-805 cmd55_app_cmd()`                             | sets app_cmd_=true; queue R1                                                          | SD spec § 4.3.9.1                                                                                                          | ✓ |                                                                                                                                                                             |
| 72 | `sd_card.cpp:807-867 cmd9_send_csd()`                             | initialised gate; emit CSD v2.0 with computed C_SIZE                                  | SD spec § 5.3.3                                                                                                          | ✓ |                                                                                                                                                                             |
| 73 | `sd_card.cpp:871-902 cmd10_send_cid()`                            | initialised gate; emit CID                                                            | SD spec § 5.2                                                                                                            | ✓ |                                                                                                                                                                             |
| 74 | `sd_card.cpp:904-937 cmd58_read_ocr()`                            | OCR0 = 0x80 if init, OR 0x40 if HCS=1 (V17-DIVMMC-01)                                 | SD spec § 5.1                                                                                                            | ✓ |                                                                                                                                                                             |
| 75 | `sd_card.cpp:939-954 acmd41_sd_send_op_cond()`                    | latch HCS bit (arg b30); init=true; R1=0x00 (V17 fix)                                  | SD spec § 4.2.3 / § 5.1                                                                                                  | ✓ |                                                                                                                                                                             |
| 76 | `spi.cpp:26-102 reset()`                                           | walks devices_[]; pulses deselect() on selected; cs_=0xFF; preserves rx_data_ (V12), devices_ (P7), sd_swap_ | `zxnext.vhd:3308-3309 (port_e7_reg<=all-1 on reset)` + `serial/spi_master.vhd:74,82,159-168 (miso_dat init=0, i_reset='0' wired)` | ✓ |                                                                                                                                                                             |
| 77 | `spi.cpp:104-118 set_sd_swap() / attach_device()`                 | trivial setters                                                                       | `zxnext.vhd:3311-3322 (sd_swap usage)`                                                                                    | ✓ |                                                                                                                                                                             |
| 78 | `spi.cpp:120-170 write_cs()`                                       | pattern decode "10"/"01"/$FB/$F7/$7F-gated; deselect on lost CS                      | `zxnext.vhd:3308-3322`                                                                                                    | ✓ | V8 / V11 / V16 fixes.                                                                                                                                                       |
| 79 | `spi.cpp:172-174 read_cs()`                                        | returns cs_ (member-level); not exposed via port_e7_rd (port handler passes nullptr)  | `zxnext.vhd:614-622 (no port_e7_rd)`                                                                                      | ✓ | Internal accessor; harmless because port-decode passes nullptr (V16-DIVMMC-01).                                                                                              |
| 80 | `spi.cpp:176-219 write_data() / read_data()`                      | full-duplex byte exchange; rx_data_ is pipeline-stage; nullptr-active-device → 0xFF (Verify3-Audit fix); spi_wait_n=true (G137 byte-granularity) | `serial/spi_master.vhd:82,104-117,148-168` + `zxnext.vhd:3270-3298, 3278-3280` | ✓ |                                                                                                                                                                             |

**Coverage summary**: 80 rows, 0 ✗ rows. All surfaces have a cited VHDL line
(or "no VHDL backing — host-side / SD-spec" for SD-protocol behaviour and host
mount/unmount management). Cross-cutting families re-checked in second sweep
(see below) yielded one minor representational deviation (Finding F19-DIVMMC-NIT-01).

---

## Second-sweep cross-cutting families

For Pass-19 the audit-thoroughness mandate (`feedback_task2_audit_thorough_per_pass.md`)
required a second sweep over cross-cutting families. Findings:

| Family                                          | Result                                                                                                               |
|-------------------------------------------------|----------------------------------------------------------------------------------------------------------------------|
| Cache-leak (NR 0x0A bits 7:5 outside config_mode) | Closed (V11-NMP-02). NR write canonicalises bits 7:5 from prev cache when gate closed.                                |
| Multi-writer fan-out (NR 0x0A → spi/multiface/divmmc/mouse) | Verified at `emulator.cpp:1047-1058 (init fan-out)` and `:1064-1076 (write fan-out)` and NR 0x83 `:2498+` for divmmc.  |
| WO-NR readback (0xB8/B9/BA/BB / 0x0A composite) | Closed (V17-NMP-01 reads live; V18 of 0x0A reads from authoritative state).                                          |
| Past-EOF (CMD17/18/24, CMD18 mid-stream)        | Closed (V12-DIVMMC-04, V13-DIVMMC-01, V14-DIVMMC-01).                                                                |
| IncDecZ polarity                                | Not applicable — DivMMC/SD have no IncDecZ counters.                                                                  |
| load_state shadow re-push                       | Verified `divmmc.cpp:574-609` / `spi.cpp:242-247` re-pull all VHDL FFs including button_nmi/layer2_map_read/retn_pending. |
| Default-FF (read defaults / floating bus)        | Verified — `effective_internal_port_enable` gate returns 0xFF when closed (port_e3, port_eb); port_e7 read uses nullptr → default_read_=0xFF. |
| Port-decode masks (E3/E7/EB use `0x00FF`)       | Verified — VHDL decodes from `cpu_a(7:0)` only, so mask `0x00FF` is correct (high byte don't-care).                  |
| SPI cycle timing (`spi_wait_n` byte-granularity) | Documented G137 simplification — `spi_wait_n=true` always. Class-(d) finding F19-DIVMMC-D01 below.                   |
| DivMMC automap edge cases (RST $00..$38, NMI@$0066, $1FF8-$1FFF, $3Dxx, tape traps) | All checked per rows 14-19; equivalent.                                                                              |
| Conman/IPL boot-time bank state                 | Out of scope for this subsystem.                                                                                      |
| Full-duplex stream advance (V18-NIT-01)          | Verified at `sd_card.cpp:259-312`. Other state-machine default branches (RECEIVING_DATA, IDLE, RECEIVING_CMD) re-checked: in those states VHDL/SD-spec semantics expect either pre-token absorb (RECEIVING_DATA pre-token) or 0xFF idle, not response advance. ✓ |
| `sd_swap` / `flash_cs_enable_` — independence from soft reset | Verified preserved on `SpiMaster::reset()` (matches VHDL initial-value-only signals at lines 1125, 1126; `port_e7_reg` is the only reset-clause signal). |

**Result of second sweep**: zero new bug-findings beyond the table.

---

## Findings

### Class-(a / b / c)

#### F19-DIVMMC-NIT-01 (class-(c) — pure representational, observationally hidden)

**Status:** RESOLVED in this pass — fix + discriminative regression test in single commit.

**File:** `src/peripheral/divmmc.cpp:109` (`write_control()`).

**Observation:** `control_reg_` is computed as
`(val & ~0x40) | (mapram_ ? 0x40 : 0x00)`. This means bits 5:4 of the
INPUT byte are stored verbatim in `control_reg_`. VHDL `port_e3_reg(5:4)`
are NEVER written by `port_e3_wr` (zxnext.vhd:4180-4183 only assigns
bits 7, 6, 3:0); they are reset to 0 at hardware reset (line 4177) and
stay at 0 forever.

**Observational impact:** `read_control()` masks bits 5:4 to 0 via
`& 0xCF`, so external callers (Z80 IN A,(0xE3) → port_e3_dat) see the
correct VHDL-faithful value regardless. The divergence is therefore
purely internal to `control_reg_` and would only surface in:

1. A direct unit-test inspection of `control_reg_` raw value (no such
   accessor exists).
2. A future refactor that replaces `read_control() & 0xCF` with
   `control_reg_` direct-access path (would leak bits 5:4 to the host).
3. A save_state round-trip: `control_reg_` is serialised verbatim in
   `save_state` line 543 with no mask, so a snapshot containing a
   non-zero bits-5:4 value would round-trip those bits faithfully —
   diverging from VHDL's "always 0" semantics on the next snapshot
   inspection.

Class-(c) latent.

**VHDL faithful contract:** `port_e3_reg(5 downto 4)` are always `00`
(post-reset, never set by any write path). The C++ `control_reg_` should
reflect the same invariant.

**Fix:** mask `val & 0x8F` (keep 7,6,3:0; force 5:4 to 0) before applying
the OR-latched bit 6:

```cpp
control_reg_ = (val & 0x8F) | (mapram_ ? 0x40 : 0x00);
```

This preserves the VHDL invariant on the stored byte (and is correctly
seen by save_state).

**Discriminative regression test:** `divmmc_test.cpp` row E3-V19-NIT-01
writes a port-0xE3 byte with bits 5:4 set; verifies a fresh accessor
`control_reg_raw()` (added private→public in this pass for the test) is
masked. Pre-fix the test fails (the raw byte shows bits 5:4 set);
post-fix it passes.

---

### Class-(d) — listed only, no fix

#### F19-DIVMMC-D01 (cycle-accurate SPI master timing)

**Status:** Architectural, listed only.

**File:** `src/peripheral/spi.h:103` (`spi_wait_n() const { return true; }`).

**Observation:** VHDL `serial/spi_master.vhd:177` defines
`o_spi_wait_n <= state_idle or state_last_d`. During the 16-cycle
in-progress transfer (`state_idle=0` AND `state_last_d=0`), `spi_wait_n=0`,
which the DMA at `zxnext.vhd:3297` consumes to throttle DMA-via-SPI bursts.

The C++ `SpiMaster` is a zero-latency byte wrapper — every
`write_data`/`read_data` completes synchronously and the master is
"always idle" when observed by callers. The accessor returns
`true` (= "no wait") unconditionally.

**Boot-path impact:** none (TBBlue/NextZXOS firmware never uses
DMA-via-SPI on the boot path). A future test or program that drives a
DMA-via-SPI burst would observe SPI as "instant", potentially completing
the transfer in fewer T-states than VHDL would.

**Why deferred:** A cycle-accurate fix requires an FSM rewrite of
`SpiMaster` (state counter, in-progress flag, T-state advancement), with
matching changes in DMA/SPI integration tests. The previous Pass-17
listed this as G137 — pending user authorisation.

This pass re-confirms the deferral; cumulative class-(d) item.

---

## Per-pass conclusion

- **Enumeration table rows:** 80
- **Class-(a) findings:** 0
- **Class-(b) findings:** 0
- **Class-(c) findings:** 1 (F19-DIVMMC-NIT-01 — fixed in same commit, discriminative regression test added)
- **Class-(d) findings (listed only):** 1 (F19-DIVMMC-D01 — re-confirmed G137)
- **Tests added:** 1 (E3-V19-NIT-01 in `divmmc_test.cpp`)

**Subsystem status:** The audit re-confirms that the DivMMC + SD card +
SPI subsystem is — modulo the trivial NIT fixed in this pass and the
single class-(d) item — VHDL-faithful at the 80-surface granularity
table. Pass-19 found ONE class-(c) NIT (a representational fidelity gap
in `control_reg_`) on the second sweep; the principal state machine,
NR/port handlers, RST trap decoding, ROM3 path gating, V18 full-duplex
fix, and Pass-12..17 spec-faithfulness fixes are all in place.

Recommendation: report DivMMC+SD+SPI as **near-converged** for Pass-19;
suggest user authorisation for F19-DIVMMC-D01 (G137 cycle-accurate SPI)
on the architectural roadmap.
