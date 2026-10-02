# SD Card Image Boot Requirements for NextZXOS

## Boot Sequence

The ZX Spectrum Next boot process (from the FPGA boot ROM) loads files in this
order:

1. **FPGA boot ROM** executes at power-on (mapped at 0x0000-0x1FFF)
2. **SD card init** via SPI (CMD0, CMD8, ACMD41, CMD58 — SDHC block addressing)
3. **MBR** (sector 0) read, partition table parsed
4. **FAT32 boot sector** at partition start (sector 63 typically)
5. **`TBBLUE.FW`** loaded from SD card root (firmware, ~300K)
6. **`machines/next/config.ini`** read for hardware settings
7. **`machines/next/menu.def`** (or `menu.ini` override) for machine personalities
8. **ROM images** loaded per config: enNextZX.rom, enNxtmmc.rom, enNextMF.rom
9. **Boot ROM disabled** via NextREG 0x03 write
10. **NextZXOS** takes over (loads enSystem.sys, etc.)

## Required Filesystem Structure

```
/                              FAT32 root
├── TBBLUE.FW                  Firmware binary (~300K)
├── MACHINES/
│   └── NEXT/
│       ├── CONFIG.INI         Hardware configuration (REQUIRED for boot)
│       ├── MENU.DEF           Default machine personality list
│       ├── MENU.INI           Optional menu override (same format as MENU.DEF)
│       ├── KEYMAP.BIN         Keyboard mapping (1K)
│       ├── ENNEXTZX.ROM      Main OS + BASIC ROM (64K)
│       ├── ENNXTMMC.ROM      DivMMC disk OS ROM (8K)
│       ├── ENNEXTMF.ROM      Multiface ROM (8K)
│       ├── 48.ROM             48K BASIC ROM (16K)
│       ├── 128.ROM            128K ROM (32K)
│       └── ...                (other personality ROMs)
├── NEXTZXOS/
│   ├── ENSYSTEM.SYS           NextZXOS system file (~28K)
│   ├── AUTOEXEC.1ST           Autoexec script
│   ├── BROWSER.CFG            File browser config
│   └── ...                    (other system files)
├── SYS/
│   ├── ENV.CFG                Environment config
│   └── ...
├── DOT/                       Dot commands
├── APPS/                      Applications
├── DEMOS/                     Demo programs
└── GAMES/                     Games
```

## config.ini Format

Plain text file, one setting per line: `name=value`

| #  | Setting     | Default | Description                             |
|----|-------------|---------|-----------------------------------------|
| 1  | scandoubler | 1       | VGA scandoubler ON(1)/OFF(0)            |
| 2  | 50_60hz     | 0       | 50Hz(0) or 60Hz(1)                      |
| 3  | timex       | 1       | Timex HiRes/Multicolour modes           |
| 4  | psgmode     | 0       | AY(0), YM(1), reserved(2), disabled(3)  |
| 5  | stereomode  | 1       | ABC(0) or ACB(1) channel order          |
| 6  | intsnd      | 1       | Internal beeper enable                  |
| 7  | turbosound  | 1       | TurboSound (3x AY) enable               |
| 8  | dac         | 1       | SpecDRUM/Covox DAC enable               |
| 9  | divmmc      | 0       | Onboard DivMMC enable                   |
| 10 | divports    | 1       | DivMMC I/O port access                  |
| 11 | mf          | 0       | Multiface hardware enable               |
| 12 | joystick1   | 2       | Joystick 1 type (0-6)                   |
| 13 | joystick2   | 0       | Joystick 2 type (0-6)                   |
| 14 | ps2         | 0       | PS/2 port mouse-first rewire            |
| 15 | scanlines   | 0       | Scanline effect: 0/50%/25%/12.5%        |
| 16 | turbokey    | 1       | Allow speeds > 3.5 MHz                  |
| 17 | timing      | 0       | Screen mode (0-7); 8 = testcard         |
| 18 | default     | 0       | Default personality index from menu.def |
| 19 | dma         | 0       | DMA controller enable                   |
| 20 | keyb_issue  | 0       | Keyboard Issue 2(1)/Issue 3(0)          |
| 21 | ay48        | 0       | AY in 48K mode                          |
| 22 | uart_i2c    | 1       | I2C and UART devices enable             |
| 23 | kmouse      | 1       | Kempston mouse enable                   |
| 24 | ulaplus     | 1       | ULA+ extensions enable                  |
| 25 | hdmisound   | 1       | Digital video port sound                |
| 26 | beepmode    | 0       | Beeper audio mixing disable(1)          |
| 27 | buttonswap  | 0       | Mouse button swap                       |
| 28 | mousedpi    | 1       | Mouse sensitivity (0-3)                 |

**Note:** `timing=8` forces testcard/config screen. For normal boot use `timing=0`.

## CSpect SD Image Analysis

The CSpect 1GB image (`cspect-next-1gb.img`) contains a valid FAT32 filesystem
labelled "NEXT1.4.1" with partition starting at sector 63.

**Present:** TBBLUE.FW, MACHINES/NEXT/MENU.DEF, KEYMAP.BIN, all ROM files,
NEXTZXOS/ENSYSTEM.SYS and supporting files.

**Missing:** `MACHINES/NEXT/CONFIG.INI` — this causes the boot ROM to show
"Error opening 'menu.ini/.def'!" and fail to boot into NextZXOS.

**Fix:** Inject a default config.ini into the image using `tools/fix-sdcard-image.sh`.

## References

- [Boot Sequence - SpecNext Wiki](https://wiki.specnext.dev/Boot_Sequence)
- [config.ini docs](https://gitlab.com/thesmog358/tbblue/-/blob/master/docs/config/config.txt)
- [Minimal NextZXOS](https://www.elite.uk.com/mike/posts/2025-08_Minimal_NextZXOS/)
- [System/Next Latest Distro](https://www.specnext.com/latestdistro/)
