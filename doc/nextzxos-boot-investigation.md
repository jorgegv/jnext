# NextZXOS Boot Investigation

## Status: Post-boot firmware retries BPB read 3x then fails

Boot ROM works. Post-boot firmware (TBBLUE.FW) reads MBR+BPB correctly but
fails BPB processing. SPI data verified byte-perfect. Issue is NOT in SPI layer.

## Boot sequence (working)

```
Boot ROM:  CMD12 → CMD0 → CMD8 → ACMD41 → CMD58
           → reads sector 0 (MBR), 63 (BPB), 610 (root dir?)
           → reads sectors 197922–197976 (TBBLUE.FW, ~27K)
           → writes NR 0x07=0x03 (28 MHz), NR 0x03=0xB0 (disable boot ROM)
           → PC=0x6DBC, firmware runs from RAM

Post-boot: CMD12 → CMD0 → CMD8 → ACMD41 → CMD58
           → reads sector 0 (MBR) ✓
           → reads sector 63 (BPB) ✓
           → processes BPB → FAILS → retries 3x → "Error opening menu.ini/.def"
```

## SPI model rewrite (2026-03-29)

### Root cause: ZesarUX comparison

Reference: `/home/jorgegv/src/spectrum/zesarux/src/storage/mmc.c`

ZesarUX uses **independent read/write paths**:
- `mmc_write(value)` — receives command bytes, advances command state machine
- `mmc_read()` — returns response bytes sequentially (index-based)
- `mmc_cs(value)` — resets ALL protocol state on ANY CS change

Our original model used VHDL-accurate full-duplex SPI:
- `exchange(tx)` — simultaneous send/receive, 1-byte pipeline delay
- `read_data()` returned PREVIOUS exchange result (pipeline)
- `write_data()` consumed response bytes during command send

This caused: (a) 1-byte data offset in responses, (b) leftover response bytes
corrupting next command, (c) CMD8 R7 response consuming CMD55/ACMD41 bytes.

### Fixes applied

| Fix | File | Description |
|-----|------|-------------|
| Split SPI model | `spi.h/cpp` | `SpiDevice::receive(tx)` + `send()` instead of `exchange()` |
| CS deselect | `spi.cpp`, `sd_card.cpp` | `deselect()` resets protocol to IDLE |
| Command abort | `sd_card.cpp` | New command start byte (0x40\|cmd) aborts pending response |
| CMD1 | `sd_card.cpp` | `SEND_OP_COND` for boot ROM MMC init path |
| NCR byte | `sd_card.cpp` | 0xFF prepended to all R1 responses (matches ZesarUX) |
| CMD12 stuff | `sd_card.cpp` | 8×0xFF before R1 (firmware reads 8 stuff bytes first) |

### ZesarUX mmc_read() response format

```
CMD17 (READ_SINGLE_BLOCK):
  index 0:       0xFF  (NCR/busy)
  index 1:       0x00  (R1 = OK)
  index 2:       0xFE  (data token)
  index 3–514:   512 data bytes
  index 515–516: 0xFF  (CRC)

CMD58 (READ_OCR):
  index 0:       0xFF  (NCR)
  index 1:       0x00  (R1)
  index 2–6:     mmc_ocr[0..4] = {5, 0, 0, 0, 0}
  index 7–8:     0xFF  (CRC)

CMD12 (STOP_TRANSMISSION):
  always returns: 1 (idle)

CMD0 (GO_IDLE):
  always returns: 1 (idle)

Default (unknown command):
  switch falls through, function returns 0
```

## What's been ruled out

- SPI pipeline delay → fixed (split model)
- SPI read/write coupling → fixed (independent paths)
- CS deselect state → fixed (resets to IDLE)
- CMD12 stuff bytes → fixed (8×0xFF)
- NCR byte before R1 → added
- BPB data integrity → verified byte-perfect match with image
- MBR partition table → valid: type=0x0C (FAT32 LBA), LBA=63, size=2097089
- BPB fields → valid: 512 B/sector, 64 sect/cluster, 33 reserved, 2 FATs
- NextREG reads during processing → none observed
- DivMMC overlay interference → data stored at 0xCAC2+ (regular RAM, slot 6)
- Boot ROM type → regular `bootrom.vhd` (matches `bootrom_ab.vhd` is different/anti-brick)
- Port conflicts → SPI port 0x??EB has no handler conflicts

## Firmware code analysis

The TBBLUE.FW code is C-compiled (sdcc or z88dk). Key functions at 0x7800–0x7CFF:

```
0x786A: SPI send wrapper — sends DE bytes from (BC) to port 0xEB
0x787F: SPI read wrapper — reads DE bytes from port 0xEB to (BC)
0x788A: Single SPI byte read: IN A,(0xEB); LD (BC),A; INC BC
0x78C6: R1 poll — reads until non-0xFF, timeout 250 (0xFA)
0x792F: Sector read — polls for data token 0xFE, reads 512+2 bytes
0x7905: CS deselect — OUT (0xE7),0xFF
0x791A: CS select — OUT (0xE7),0xFE
0x7995: CMD write+read — sends CMD byte+args, reads response
0x79FB: SD command — CS select, build 6-byte command, send, read R1
0x7AD5: SD init — CMD12, CMD0, CMD8, ACMD41, CMD58, card type detect
0x7C39: Sector read — called with HL=sector number, IY=buffer (0xCAC2)
```

SD init at 0x7AD5 returns L=0 (success) or L=1 (failure). The caller retries 3x.

## Next investigation leads

1. **Run ZesarUX with same SD image** — compare full SPI byte sequence to find
   remaining protocol differences
2. **Check default return value** — ZesarUX's `mmc_read()` returns 0 at end of
   switch (not 0xFF). Our `send()` returns 0xFF in IDLE. Firmware may check
   extra bytes after sector read that differ.
3. **Inspect BPB processing code** — the firmware at 0x7C39+ reads sectors and
   processes them. Need to trace the exact C code logic that decides to retry.
4. **NR 0x05 (board revision)** — firmware reads this during init; our default is
   0x00. Real hardware has non-zero lower nibble. May affect card type detection.
5. **Data breakpoints** — use the emulator's debugger to watch reads from the BPB
   buffer at 0xCAC2–0xCCC1 to see what fields the firmware checks.
6. **FS Info sector** — firmware might try to read sector 1 (relative to partition
   start = sector 64) which we haven't seen in the trace. Maybe it reads it but
   the data is wrong.

## SD card image details

```
Image: roms/cspect-next-1gb.img (1GB, CSpect format)
Partition: FAT32, starts at sector 63, label "NEXT1.4.1"
BPB: 512 B/sector, 64 sect/cluster, 33 reserved, 2 FATs, 257 sect/FAT
Root cluster: 2, FS info: sector 1 (relative), backup boot: sector 6
```

## Commits (2026-03-29)

- `5bef014` — SPI model rewrite: split read/write, CS deselect, CMD1, command abort
- `971aaee` — NCR busy byte before R1 responses
- `7723008` — CMD12 stuff bytes (8×0xFF before R1)
