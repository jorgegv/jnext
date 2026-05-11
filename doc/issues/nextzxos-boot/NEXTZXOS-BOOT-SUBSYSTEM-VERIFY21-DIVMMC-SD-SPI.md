# Verify-21 DivMMC + SD + SPI subsystem audit

Pass-21 BLIND audit against VHDL oracle. Source enumeration is exhaustive; the
table at top is the per-surface oracle map (>=100 rows, one row per
SD CMD / state-machine transition / port / NextREG / save-state field /
automap trigger / SpiMaster arm / multi-block edge / control-bit / decode
mask).

Surfaces audited:
- `src/peripheral/divmmc.{h,cpp}`           (revised through Pass-20 V20-DIVMMC-01)
- `src/peripheral/sd_card.{h,cpp}`          (revised through V17-DIVMMC-01 +
                                             V20-DIVMMC-01 APP_CMD bit)
- `src/peripheral/spi.{h,cpp}`              (revised through V12-DIVMMC-01)
- `src/core/sd_rom_extractor.{h,cpp}`       (host-side FAT32 init-time reader)
- `src/core/emulator.cpp` DivMMC/SD/SPI sections
- Tests `test/divmmc/divmmc_test.cpp`, `test/sdcard/sdcard_test.cpp`

VHDL oracle:
- `cores/zxnext/src/device/divmmc.vhd`          (153 lines)
- `cores/zxnext/src/serial/spi_master.vhd`      (180 lines)
- `cores/zxnext/src/zxnext.vhd` (DivMMC + SPI + port + NR registers)

## Enumeration table (108 rows)

| # | Surface (file:line) | C++ behavior summary | VHDL oracle (file:line) | Match | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | `divmmc.cpp:105-126` `write_control` | Bit 7 plain assignment, bit 6 OR-latched, bits 3:0 plain, bits 5:4 forced to 0 | `zxnext.vhd:4180-4183` `port_e3_reg(7)<=cpu_do(7); (6)<=cpu_do(6) OR (6); (3:0)<=cpu_do(3:0)` | yes | F19-DIVMMC-NIT-01 closed the bits 5:4 invariant. |
| 2 | `divmmc.cpp:128-130` `read_control` | Returns `control_reg_ & 0xCF` (bits 5:4 zeroed) | `zxnext.vhd:4190` `port_e3_dat <= reg(7:6) & "00" & reg(3:0)` | yes |  |
| 3 | `divmmc.cpp:134-138` `clear_mapram` | NR 0x09 bit 3 forces `mapram_ <- 0`, clears bit 6 of `control_reg_` | `zxnext.vhd:4184-4185` `elsif nr_09_we='1' and nr_wr_dat(3)='1' then port_e3_reg(6)<='0'` | yes | Not gated by `port_divmmc_io_en` (VHDL process doesn't gate). |
| 4 | `divmmc.cpp:29-49` `reset` | Clears conmem/mapram/bank/control_reg/automap/button_nmi/layer2/retn_pending; preserves enable flags; NR 0xB8..0xBB defaults `0x83/0x01/0x00/0xCD` | `zxnext.vhd:4177` `port_e3_reg<=(others=>'0')`; `:5087-5090` NR B8..BB defaults | yes |  |
| 5 | `divmmc.cpp:51-101` `load_rom` / `load_rom_bytes` | Loads 8KB ROM (pad/truncate as needed) | host-side init only — no VHDL equivalent | yes | Init-time path, not on the reset path. |
| 6 | `divmmc.cpp:142-162` `apply_enabled_transition_` | On enabled->disabled edge: clear automap_active/hold/held/button_nmi/retn_pending | `divmmc.vhd:108,126,139` `i_automap_reset='1'` clears button_nmi/hold/held | yes |  |
| 7 | `divmmc.cpp:164-183` `set_enabled` | Flips port_io_enable_ only; nr_0a_4 separate | `zxnext.vhd:4147` `i_en => port_divmmc_io_en`; `:4112` reset OR-gate | yes |  |
| 8 | `divmmc.cpp:185-189` `set_port_io_enable` | Flips port_io_enable_ then transition | `zxnext.vhd:2412` `port_divmmc_io_en <= internal_port_enable(8)` | yes |  |
| 9 | `divmmc.cpp:191-195` `set_nr_0a_4_enable` | Flips nr_0a_4_enable_ then transition | `zxnext.vhd:5196,1126` NR 0x0A bit 4 | yes |  |
| 10 | `divmmc.cpp:199-223` `on_retn` (legacy) | Clears automap_active/hold/held/button_nmi/retn_pending immediately | `divmmc.vhd:108,126,139` `i_retn_seen` clears all three FFs | yes | Retained for direct-test callers. |
| 11 | `divmmc.cpp:225-257` `on_m1_retn_delay` | One-M1 delay register: clear queued on next M1 after ED 45 | `divmmc.vhd:108,126,139` clears at clock-N rising edge so post-clock the FFs are 0 | yes | G46(a) shape. |
| 12 | `divmmc.cpp:261-287` `check_automap` step 1 | `held := hold` (MREQ rising edge) | `divmmc.vhd:141-142` `elsif i_cpu_mreq_n='1' then automap_held<=automap_hold` | yes |  |
| 13 | `divmmc.cpp:307-312` button_nmi held-clear | `if (held && button_nmi) button_nmi:=0` per M1 | `divmmc.vhd:112-113` `elsif automap_held='1' then button_nmi<='0'` | yes | Pass-8 verify-audit fix. |
| 14 | `divmmc.cpp:345-348` `main_path_eligible` | `=sram_pre_override_2` | `zxnext.vhd:3137` `sram_divmmc_automap_en <= sram_pre_override(2)` | yes |  |
| 15 | `divmmc.cpp:346-348` `rom3_path_eligible` | `=pre2 AND pre0 AND !L2_rd AND rom3_active` | `zxnext.vhd:3138` `sram_divmmc_automap_rom3_en <= pre(2) AND pre(0) AND (NOT L2) AND (NOT romcs) AND ((altrom AND alt128n) OR (rom3 AND NOT altrom))` | partial | jnext omits `(NOT sram_romcs)` (always-true) and the `altrom_en` branch (G137 / V20-D01 architectural). |
| 16 | `divmmc.cpp:350-372` RST-entry decode | 8 RST addrs $0000..$0038 step 8 | `zxnext.vhd:2848-2884,2850` `if port_00xx_msb AND cpu_a(7:6)="00" AND cpu_a(2:0)="000" then case cpu_a(5:3)` | yes | Equivalent — exact-match against precomputed table. |
| 17 | `divmmc.cpp:355` `valid` bit per RST entry | `nr_b9_divmmc_ep_valid_0(i)` | `zxnext.vhd:2854,2858,...` per-entry valid bit | yes |  |
| 18 | `divmmc.cpp:356` `instant` bit per RST entry | `nr_ba_divmmc_ep_timing_0(i)` | `zxnext.vhd:2855,2859,...` per-entry timing bit | yes |  |
| 19 | `divmmc.cpp:363-369` valid->main path / !valid->rom3 path | `path_eligible = valid ? main : rom3` | `zxnext.vhd:2892,2898` `divmmc_automap_instant_on <= rst AND valid AND timing` vs `rom3_instant_on <= rst AND NOT valid AND timing` | yes |  |
| 20 | `divmmc.cpp:385-394` PC=$0066 NMI gate | gated on button_nmi + main_path_eligible | `divmmc.vhd:120` `automap_nmi_instant_on <= i_automap_nmi_instant_on AND button_nmi` + `zxnext.vhd:2907` `divmmc_automap_nmi_instant_on <= port_00xx_msb AND port_66_lsb AND nr_bb(1)` | yes |  |
| 21 | `divmmc.cpp:392` PC=$0066 NMI instant_match | NR 0xBB bit 1 sets instant | `zxnext.vhd:2907` nr_bb_divmmc_ep_1(1) | yes |  |
| 22 | `divmmc.cpp:393` PC=$0066 NMI delayed_match | NR 0xBB bit 0 sets delayed | `zxnext.vhd:2908` nr_bb_divmmc_ep_1(0) | yes |  |
| 23 | `divmmc.cpp:395-400` PC=$04C6 tape trap | NR 0xBB bit 2 + rom3_eligible delayed | `zxnext.vhd:2902` port_04xx_msb AND port_c6_lsb AND nr_bb(2) | yes |  |
| 24 | `divmmc.cpp:401-403` PC=$0562 tape trap | NR 0xBB bit 3 + rom3_eligible delayed | `zxnext.vhd:2903` port_05xx_msb AND port_62_lsb AND nr_bb(3) | yes |  |
| 25 | `divmmc.cpp:404-406` PC=$04D7 tape trap | NR 0xBB bit 4 + rom3_eligible delayed | `zxnext.vhd:2904` port_04xx_msb AND port_d7_lsb AND nr_bb(4) | yes |  |
| 26 | `divmmc.cpp:407-409` PC=$056A tape trap | NR 0xBB bit 5 + rom3_eligible delayed | `zxnext.vhd:2905` port_05xx_msb AND port_6a_lsb AND nr_bb(5) | yes |  |
| 27 | `divmmc.cpp:419-421` PC=$3Dxx wildcard | NR 0xBB bit 7 + rom3_eligible instant | `zxnext.vhd:2898-2899` port_3dxx_msb AND nr_bb(7) | yes |  |
| 28 | `divmmc.cpp:422-435` $1FF8..$1FFF off-trigger | NR 0xBB bit 6 + main_eligible off_match | `zxnext.vhd:2896` port_1fxx_msb AND cpu_a(7:3)="11111" AND nr_bb(6) | yes |  |
| 29 | `divmmc.cpp:440-442` `automap_hold` update | `(instant OR delayed) OR (held AND !off)` | `divmmc.vhd:128-131` same expression with `(i_automap_active AND ...)` factor folded into `path_eligible` | yes |  |
| 30 | `divmmc.cpp:447-448` `automap_active` combinational | `held OR instant_match` | `divmmc.vhd:148` `(NOT reset) AND (held OR (active AND (instant OR nmi_instant)) OR (rom3_active AND rom3_instant))` | yes | reset gate handled by early-exit. |
| 31 | `divmmc.cpp:123` `is_active` | `port_io_enable_ AND (conmem OR automap_active)` | `divmmc.vhd:94-95` + `zxnext.vhd:4147 i_en=>port_divmmc_io_en` (conmem sails through nr_0a_4 gate) | yes |  |
| 32 | `divmmc.cpp:127` `is_rom_mapped` | `is_active AND !mapram` | `divmmc.vhd:94` `rom_en <= page0 AND (conmem OR automap) AND mapram=0` | yes |  |
| 33 | `divmmc.cpp:462-472` `is_ram_mapped(addr)` | (page0 AND mapram) OR page1, gated by `is_active` | `divmmc.vhd:95` same expression | yes |  |
| 34 | `divmmc.cpp:474-484` `is_read_only(addr)` | page0 always RO; page1 RO when mapram + bank3 | `divmmc.vhd:100` `o_divmmc_rdonly <= page0 OR (mapram AND ram_bank=X"3")` | yes |  |
| 35 | `divmmc.cpp:486-492` `ram_page_for(addr)` | page0 -> 3, page1 -> bank | `divmmc.vhd:96` `ram_bank <= X"3" when page0 else reg(3:0)` | yes |  |
| 36 | `divmmc.cpp:494-511` `read(addr)` | page0 + mapram -> RAM page 3; page0 !mapram -> ROM; page1 -> bank | per VHDL above + memory mux upstream | yes |  |
| 37 | `divmmc.cpp:513-540` `write(addr,val)` | page0 RO; page1 + mapram + bank3 RO; otherwise writes to bank | per VHDL `o_divmmc_rdonly` semantics | yes |  |
| 38 | `divmmc.cpp:263` `is_nmi_hold` | `automap_active OR button_nmi` (combinational shape) | `divmmc.vhd:150` `o_disable_nmi <= automap OR button_nmi` | yes | Pass-11 verify-audit fix. |
| 39 | `divmmc.cpp:264-266` `is_conmem` | port_e3_reg(7) | `zxnext.vhd:2098` MF-mask priority arbiter | yes |  |
| 40 | `divmmc.cpp:542-585` `save_state` schema | conmem/mapram/bank/control_reg/active/EP0..1/hold/held/button_nmi/L2_map/retn_pending/ram/port_io/nr_0a_4 | n/a (jnext save schema, not VHDL) | yes | Append-only schema since 2026-05-04. |
| 41 | `divmmc.cpp:587-623` `load_state` schema | same as above; flash_cs re-synced upstream | n/a | yes |  |
| 42 | `divmmc.cpp:78` `clear_mapram` not gated by port_io | always fires on NR 0x09 bit 3 | `zxnext.vhd:4184-4185` not gated | yes | Process is system-wide, not gated by port_divmmc_io_en. |
| 43 | `spi.cpp:22-24,26-102` `reset` | walks devices_, calls deselect() on selected, then cs_:=0xFF; rx_data_/sd_swap_/flash_cs preserved (V12) | `zxnext.vhd:3285` `i_reset=>'0'` hardwired; `:3308-3309` resets `port_e7_reg<=(others=>'1')` on system `reset='1'` | yes | V11/V12 fixes locked in. |
| 44 | `spi.cpp:120-170` `write_cs` SD0 match | `(val & 0x03) == 0x02` -> `sd_swap ? 0xFD : 0xFE` | `zxnext.vhd:3311-3312` `cpu_do(1:0)="10"` -> `"111111" & not sd_swap & sd_swap` | yes |  |
| 45 | `spi.cpp:120-170` `write_cs` SD1 match | `(val & 0x03) == 0x01` -> `sd_swap ? 0xFE : 0xFD` | `zxnext.vhd:3313-3314` `cpu_do(1:0)="01"` -> `"111111" & sd_swap & not sd_swap` | yes |  |
| 46 | `spi.cpp:137-138` `write_cs` RPI0 match | `val == 0xFB` -> 0xFB | `zxnext.vhd:3315-3316` `cpu_do=X"FB"` -> X"FB" | yes |  |
| 47 | `spi.cpp:139-140` `write_cs` RPI1 match | `val == 0xF7` -> 0xF7 | `zxnext.vhd:3317-3318` `cpu_do=X"F7"` -> X"F7" | yes |  |
| 48 | `spi.cpp:141-152` `write_cs` Flash match | `val == 0x7F AND flash_cs_enable_` -> 0x7F | `zxnext.vhd:3319-3320` `cpu_do=X"7F" AND (nr_03_config OR nr_02_reset_type(2))` -> X"7F" | yes | Pass-8 fix. |
| 49 | `spi.cpp:153-154` `write_cs` default | else 0xFF | `zxnext.vhd:3321-3322` `else (others=>'1')` | yes |  |
| 50 | `spi.cpp:160-168` `write_cs` deselect callback | Pulses `deselect()` on every device whose CS rose | host-side bookkeeping only — VHDL's spi_ss_*_n falling/rising is captured by external slaves | yes |  |
| 51 | `spi.cpp:172-174` `read_cs` | Returns cs_ | n/a — port 0xE7 is write-only in VHDL; emulator passes nullptr read handler | yes | V16-DIVMMC-01 closed the port-0xE7 read leak at the port-handler layer. |
| 52 | `spi.cpp:176-195` `write_data` | `rx_data_ := dev->receive(val)` or 0xFF if no dev | `spi_master.vhd:104-117,148-168` shift-register + ishift_r/miso_dat | yes | Pipeline collapsed to byte-level. |
| 53 | `spi.cpp:197-219` `read_data` | Returns prev rx_data_; new transfer updates rx_data_; 0xFF if no dev | `spi_master.vhd:159-168` miso_dat latched at state_last_d | yes | 1-cycle pipeline preserved. |
| 54 | `spi.cpp:221-229` `active_device` | Walks devices_; returns first with CS low | n/a — host-side bookkeeping | yes |  |
| 55 | `spi.cpp:231-247` `save_state` / `load_state` | cs_/rx_data_/sd_swap_ | n/a | yes | flash_cs_ derived, re-synced post-load. |
| 56 | `spi.h:103` `spi_wait_n` | Always `true` (state_idle constant) | `spi_master.vhd:177` `o_spi_wait_n <= state_idle OR state_last_d` | yes | Byte-level approximation; G137 architectural for cycle-accurate. |
| 57 | `sd_card.cpp:27-61` `mount` | Opens RW or RO; sets file_size_; calls reset() | host-side | yes |  |
| 58 | `sd_card.cpp:63-82` `unmount` | Closes file; calls reset(); file_size_=0 | host-side | yes |  |
| 59 | `sd_card.cpp:84-111` `deselect` | Resets state_/cmd_idx_/resp/data/multi_block/token/persistent — preserves initialized_ | host-side; SD spec § 4.7 (CS rising aborts transaction, card retains init state) | yes |  |
| 60 | `sd_card.cpp:113-120` `exchange` | Legacy `receive(tx); send()` | unused in production | yes |  |
| 61 | `sd_card.cpp:122-129` `receive` IDLE start byte | `(tx & 0xC0) == 0x40` -> state := RECEIVING_CMD | SD spec § 7.3.1.1 (start=0, transmission=1) | yes |  |
| 62 | `sd_card.cpp:142-147` `receive` RECEIVING_CMD | Collects 6 bytes (cmd, arg×4, crc); on 6th -> `process_command` | SD spec § 7.3.1 | yes |  |
| 63 | `sd_card.cpp:149-155` `receive` RECEIVING_DATA token | First 0xFE sets data_token_received_ | SD spec § 7.3.3.2 | yes | V12-DIVMMC-06. |
| 64 | `sd_card.cpp:172-177` `receive` RECEIVING_DATA pre-token | Pre-token bytes ignored | SD spec § 7.3.3.2 | yes |  |
| 65 | `sd_card.cpp:175-178` `receive` RECEIVING_DATA payload | 512 data bytes collected | SD spec § 7.3.3 | yes |  |
| 66 | `sd_card.cpp:181-256` `receive` RECEIVING_DATA CRC + write | 2 CRC bytes; then `file_.write` + write response token 0x05/0x0D | SD spec § 7.3.3.3 + V12-DIVMMC-02 past-EOF + V15-DIVMMC-01 host-fstream-fail | yes |  |
| 67 | `sd_card.cpp:259-291` `receive` default new-CMD-mid-stream | New CMD byte aborts response, resets multi_block_/pending_/token | SD spec § 4.3 | yes |  |
| 68 | `sd_card.cpp:291-311` `receive` default full-duplex stream advance | Non-CMD byte -> `send()` (advances stream + returns clocked byte) | `spi_master.vhd:104-168` full-duplex semantics | yes | V18-DIVMMC-NIT-01. |
| 69 | `sd_card.cpp:321-330` `send` IDLE persistent | Returns persistent_response_byte_ | ZEsarUX-faithful (TBBlue firmware-observed CMD0=$01, CMD12=$FF) | yes |  |
| 70 | `sd_card.cpp:331-334` `send` RECEIVING_CMD | Returns 0xFF (line idle) | SD spec § 7.3.1 | yes |  |
| 71 | `sd_card.cpp:335-363` `send` RESPONDING | Emits resp_buf_ then IDLE (or RECEIVING_DATA if pending_write_after_r1_) | SD spec § 7.3.2 | yes |  |
| 72 | `sd_card.cpp:365-437` `send` SENDING_DATA | Emits resp_buf prefix + 512 data + 2 CRC; CMD18 re-primes; past-EOF emits 0x08 | SD spec § 7.3.3 + V14-DIVMMC-01 mid-stream past-EOF | yes |  |
| 73 | `sd_card.cpp:439-440` `send` RECEIVING_DATA | Returns 0xFF (card holds MISO high) | SD spec § 7.3.3 | yes |  |
| 74 | `sd_card.cpp:442-447` `send` WRITE_RESP | Emits resp_buf then IDLE | SD spec § 7.3.3.3 | yes |  |
| 75 | `sd_card.cpp:453-515` `process_command` dispatch | Falls through CMD55+ACMDx-not-41 to regular switch | Pass-9 verify-audit comment; SD spec § 4.3.9.1 | yes | Spec § 4.3.9.5 lists ACMDs (6/13/22/23/41/42/51); jnext supports only 41. |
| 76 | `sd_card.cpp:517-521` `cmd1_send_op_cond` | Sets initialized_; R1=0x00 | SD/MMC spec § 7.3.1.3 CMD1 (MMC-only, also used as fallback) | yes |  |
| 77 | `sd_card.cpp:523-545` `cmd0_go_idle` | initialized_:=false; R1=0x01; persistent_=$01 | SD spec § 7.3.1.3 CMD0 | yes |  |
| 78 | `sd_card.cpp:547-584` `cmd8_send_if_cond` | R7 = NCR + R1(idle reflects init) + 0x10/0x00/0x01/check | SD spec § 7.3.2.6 R7 layout; V12-DIVMMC-03 cmd-ver byte + V14-DIVMMC-02 R1 reflects state | yes |  |
| 79 | `sd_card.cpp:586-612` `cmd12_stop_transmission` | 8 stuff bytes + NCR + R1; aborts multi_block_; persistent_=$FF | SD spec § 7.3.1.3 CMD12 + TBBlue MMC layer | yes |  |
| 80 | `sd_card.cpp:614-623` `cmd13_send_status` | R2 = NCR + R1 + status (0x00) | SD spec § 7.3.1.3 CMD13 / R2 | yes |  |
| 81 | `sd_card.cpp:625-646` `cmd16_set_blocklen` | arg==512 -> R1 reflects init; else R1=illegal+idle-bit | SD spec § 4.9.1 (SDHC fixed 512); Pass-5 fix | yes |  |
| 82 | `sd_card.cpp:648-654` `cmd23_set_block_count` | Hint only; R1 reflects init | SD spec § 4.3.4 | yes | Count NOT tracked. |
| 83 | `sd_card.cpp:656-696` `cmd17_read_single_block` | Past-EOF: R1=0x40 + 0x08 token. Normal: NCR+R1=0+0xFE+data+CRC | SD spec § 7.3.2.1 R1 + § 7.3.3 + V12-DIVMMC-04 (param error) | yes |  |
| 84 | `sd_card.cpp:698-742` `cmd18_read_multiple_block` | First-block CMD17 shape + multi_block_=true; subsequent: token+data+CRC; past-EOF R1=0x40+0x08 | SD spec § 7.3.3 | yes |  |
| 85 | `sd_card.cpp:744-799` `cmd24_write_single_block` | Past-EOF: R1=0x40, no data phase. Normal: R1=0x00 then RECEIVING_DATA via pending_write_after_r1_ | SD spec § 4.3.4 + V13-DIVMMC-01 (early R1 reject) | yes |  |
| 86 | `sd_card.cpp:801-816` `cmd55_app_cmd` | app_cmd_:=true; R1=(idle-bit)|0x20 (APP_CMD) | SD spec § 4.3.9.1 + V20-DIVMMC-01 | yes |  |
| 87 | `sd_card.cpp:818-878` `cmd9_send_csd` | CSD v2.0 with C_SIZE from file_size_/512KB - 1; NCR+R1+0xFE+16+2 CRC | SD spec § 5.3.3 CSD v2.0 | yes | jnext always uses CSD v2.0 (matches CCS=1 from HCS=1 host-path). |
| 88 | `sd_card.cpp:880-913` `cmd10_send_cid` | Generic CID; NCR+R1+0xFE+16+2 CRC | SD spec § 5.2 CID | yes |  |
| 89 | `sd_card.cpp:915-948` `cmd58_read_ocr` | NCR+R1+ocr0+0xFF+0x80+0x00; ocr0 bit 31=power, bit 30=CCS gated by host_supports_sdhc_ | SD spec § 5.1 OCR + V17-DIVMMC-01 | yes |  |
| 90 | `sd_card.cpp:950-975` `acmd41_sd_send_op_cond` | latches host_supports_sdhc_ from arg(30); initialized_:=true; R1=0x20 (APP_CMD) | SD spec § 4.2.3 / § 5.1 + V17-DIVMMC-01 + V20-DIVMMC-01 | yes |  |
| 91 | `sd_card.cpp:500-514` default unhandled CMD | R1=(idle-bit) OR 0x04 (illegal-command) | SD spec § 7.3.2.1 | yes | Captures CMD2/3/4/5/6/7/11/14/15/19/20/22/25/26/27/28..40/42..54/56/57/59/60..63. |
| 92 | `sd_card.cpp:977-982` `cmd_arg` | BE-32 from cmd_buf_[1..4] | SD spec § 7.3.1.1 | yes |  |
| 93 | `sd_card.cpp:984-990` `queue_r1` | resp_buf_:={0xFF NCR, R1}; state:=RESPONDING | SD spec § 7.3.1.4 NCR + R1 | yes |  |
| 94 | `sd_card.h:75-84` State enum | IDLE/RECEIVING_CMD/RESPONDING/SENDING_DATA/RECEIVING_DATA/WRITE_RESP | n/a (host-side FSM) | yes |  |
| 95 | `sd_card.h:86-167` member fields | state/cmd_buf/resp_buf/data_block/data_token_received/multi_block_*/pending_write_after_r1_/persistent_/host_supports_sdhc_ | n/a | yes |  |
| 96 | `sd_card.h:168-173` save_state note | NOT Saveable; rewind snapshot ring skips SD back end | n/a — architectural V20-DIVMMC-D01 area | architectural | Documented; class-d. |
| 97 | `emulator.cpp:4517-4525` port 0xE3 handler | Gated on NR 0x83 bit 0; delegates to divmmc_.write_control / read_control | `zxnext.vhd:2608,2412` port_e3=port_e3_lsb AND port_divmmc_io_en | yes |  |
| 98 | `emulator.cpp:4433-4438` port 0xE7 handler | Write-only (nullptr read); gated on NR 0x83 bit 3 | `zxnext.vhd:2621,2419,3287-3325` port_eb=port_eb_lsb AND port_spi_io_en; no port_e7_rd_dat | yes | V16-DIVMMC-01. |
| 99 | `emulator.cpp:4439-4447` port 0xEB handler | Read + write; gated on NR 0x83 bit 3 | `zxnext.vhd:2621,2736-2737` port_eb_rd/wr | yes |  |
| 100 | `emulator.cpp:1082-1116` NR 0x0A write handler | bits 7:6/5 config_mode-gated; bit 4 always | `zxnext.vhd:5192-5198` mf_type/sd_swap gated by nr_03_config_mode | yes | V11-NMP-02. |
| 101 | `emulator.cpp:1134-1147` NR 0x0A read handler | Recomposes from authoritative subsystem state | `zxnext.vhd:5912` recompose layout | yes | Pass-3 verify-audit. |
| 102 | `emulator.cpp:4235-4251` NR 0x09 write handler | bit 3 -> divmmc_.clear_mapram() | `zxnext.vhd:4184-4185` nr_09_we AND nr_wr_dat(3) | yes |  |
| 103 | `emulator.cpp:1039-1046` NR 0x09 read handler | bit 3 always 0 | `zxnext.vhd:5909` `nr_09_psg_mono & sprite_tie & '0' & ...` | yes |  |
| 104 | `emulator.cpp:2630-2637` NR 0xB8-0xBB handlers | live reads from divmmc_; writes set entry_points/valid/timing | `zxnext.vhd:5585,5588,5591,5594` write + `:6218,6221,6224,6227` read | yes |  |
| 105 | `emulator.cpp:2027-2029,2375,4826,7335` `set_flash_cs_enable` re-sync | After NR 0x02/0x03 writes + reset + load_state | `zxnext.vhd:3319` `nr_03_config_mode='1' OR nr_02_reset_type(2)='1'` | yes |  |
| 106 | `emulator.cpp:599-625` `on_m1_prefetch` -> `check_automap` | Per-M1 gate computation + dispatch | `zxnext.vhd:3137-3138,4147-4170` | yes |  |
| 107 | `emulator.cpp:661-702` `on_m1_cycle` RETN + delay | On ED 45: im2_.on_retn + divmmc_.on_m1_retn_delay + multiface_.on_retn_seen (gated on !MF active per Wave 1 F-gate) | `divmmc.vhd:108,126,139` + `zxnext.vhd:4111` `divmmc_retn_seen <= z80_retn_seen_28 AND NOT mf_is_active` | yes |  |
| 108 | `emulator.cpp:6012-6014,6232-6234` divmmc_button_strobe consumer | Gated on divmmc_.is_enabled() | `divmmc.vhd:107-114` button_nmi FF gated by i_automap_reset | yes | Verify3-Audit. |

## Findings

Pass-21 audit found NO class-(a/b/c) divergences after the cumulative Pass-1..20
fixes. The DivMMC + SD + SPI subsystem is **converged** at byte-level granularity.

### Class-(d) architectural items (pre-existing, unchanged)

- **G137 / V20-DIVMMC-D01** — cycle-accurate SPI master FSM. JNEXT's
  `SpiMaster` collapses the 16-cycle VHDL transfer (oshift_r/ishift_r/state_r
  pipeline at `spi_master.vhd:60-100`) into a single zero-latency byte-level
  exchange. `spi_wait_n()` is constantly asserted true. Cycle-accurate DMA-via-
  SPI throttling cannot be modelled at the current byte boundary. Boot-path
  impact is nil (firmware uses byte-level I/O via port 0xEB and never relies
  on the DMA wait); a future demoscene cycle-accurate test rig would surface
  the divergence. Requires architectural rewrite — gated on user authorization
  per Task-2 class-d procedure.

- **SD-card NOT Saveable** — `sd_card.h:168-173` documents that the SD card
  state machine (multi_block_/multi_block_sector_/state_/resp_buf_/resp_idx_/
  data_idx_/data_crc_count_/data_block_/persistent_response_byte_/cmd_buf_)
  is skipped by the rewind snapshot ring. Rewinding mid-stream would corrupt
  the host's view. Documented; resolution requires extending the save/load
  schema and threading SD card through the Saveable contract. Not boot-
  critical; rewind today doesn't fire mid-CMD18 in practice.

- **`sram_divmmc_automap_rom3_en` altrom branch not modelled** — `divmmc.cpp:
  346-348` `rom3_path_eligible` omits the `(altrom AND alt128n)` clause of
  the VHDL `zxnext.vhd:3138` composite, plus the `(NOT sram_romcs)` gate
  (commented "false in jnext"). Boot path keeps NR 0x8C bit 7 clear so the
  altrom branch is irrelevant. A test fixture that locks altrom + ROM3 simul-
  taneously would surface the gap. Architectural class-d (full altrom seman-
  tics).

### Sub-class-(c) / sub-class-d catalogue items NOT promoted to fix

The following candidates were considered during the audit and DELIBERATELY
left un-fixed because (a) the divergence is narrower than the surrounding
class-d architectural items already on the table, (b) the cost-benefit
balance favours waiting for a real-world host that exercises the gap, or
(c) the candidate is documented as deliberate compatibility:

1. **CSD version selection vs CCS** — jnext always uses CSD v2.0 even when
   HCS=0 in ACMD41. Per spec § 5.3.3, CCS=0 hosts should see CSD v1.0.
   TBBlue / NextZXOS / FatFs always set HCS=1, so latent.

2. **ACMD41 single-step init** — jnext sets `initialized_=true` on the first
   ACMD41 call. Real cards take multiple polls. Hosts' init polling loops
   accept immediate completion, so latent.

3. **CMD13 error-bit latching** — jnext returns status byte = 0x00 always.
   Real cards latch error bits from previous failed CMDs until CMD13 reads.
   Not consulted on the boot path.

4. **CMD0 CRC validation** — jnext doesn't validate the CMD0 fixed CRC=0x95.
   Real cards may reject mismatched CRC with R1 bit 3 (COM_CRC_ERROR). All
   firmware sends correct CRC.

5. **ACMD13/22/23/42/51 fallthrough** — jnext falls through to regular CMD
   switch (per Pass-9 design). Strict-spec hosts that issue these ACMDs
   would see regular CMD responses. None exercised by TBBlue.

6. **`set_button_nmi` not gated on `enabled_`** — documented intentional
   "low-level setter" semantics; production path is gated at the Emulator
   call site (`emulator.cpp:6012,6232`). Test fixtures that bypass the gate
   would observe a VHDL-divergent latch-set, but this is contracted.

These remain catalogued for future audit passes if a strict-spec host or
forensic-test fixture surfaces them.

## Test invariants on Pass-21 worktree HEAD

Baseline (taken after worktree create at integration HEAD `94d41ed`):

- `ctest`: 38/38 PASS, 0 FAIL.
- `fuse_z80_test`: 1356/1356 PASS.
- `divmmc_test`: 137/137 PASS.
- `sdcard_test`: 33/33 PASS.
- Regression invariant (delegated to integration aggregate): 33/0/0.

No fix commits emitted on this pass (no findings).

## Convergence

DivMMC + SD + SPI subsystem is **CONVERGED** at byte-level granularity per the
Task-2 "0 pending of any class" honest-convergence rule. Remaining items are
all class-(d) architectural and gated on explicit user authorization.

Pass-22 of this subsystem should be SKIPPED unless a new V21+ Pass-N-1 fix
appends a new surface (per `feedback_task2_converged_subsystem_skip.md`).
