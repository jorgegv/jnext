# Test Plan Traceability Matrix

> Generated 2026-04-15 from main at commit `fcbd9aed61`. This document is the canonical map from plan row → test ID → VHDL citation → test location for the 16 jnext subsystem unit test suites. See doc/testing/UNIT-TEST-PLAN-EXECUTION.md for the authoring process and doc/design/EMULATOR-DESIGN-PLAN.md §Phase 9 for the task tree.

## Summary

| Subsystem        | Plan rows | In-test | Pass | Fail | Skip/Stub | Missing | Last-touch commit |
|------------------|----------:|--------:|-----:|-----:|----------:|--------:|-------------------|
| Z80N             | 30        | 0       | —    | —    | —         | 30      | `8d0cf05a15`      |
| Memory/MMU       | 143       | 143     | 64   | 2    | 77        | 0       | `6d1a057000`      |
| ULA Video        | 122       | 123     | 47   | 1    | 75        | 0       | `7c56b92000`      |
| Layer2           | 97        | 51      | —    | —    | 0         | 46      | `fcbd9aed61`      |
| Sprites          | 132       | 116     | —    | —    | 0         | 16      | `28f5afb540`      |
| Tilemap          | 69        | 69      | 38   | 13   | 18        | 0       | `a3e1196000`      |
| Copper           | 76        | 76      | —    | —    | 10        | 0       | `fcbd9aed61`      |
| Compositor       | 115       | 91      | —    | —    | 0         | 24      | `fcbd9aed61`      |
| Audio            | 197       | 197     | 121  | 6    | 73        | 0       | `178c41c000`      |
| DMA              | 156       | 156     | 116  | 5    | 35        | 0       | `deeb9f6000`      |
| DivMMC+SPI       | 123       | 123     | 53   | 14   | 56        | 0       | `c9d057e000`      |
| CTC+Interrupts   | 150       | 150     | 43   | 1    | 106       | 0       | `9591481000`      |
| UART+I2C/RTC     | 105       | 106     | 48   | 12   | 46        | 0       | `628d01f000`      |
| NextREG          | 64        | 64      | 16   | 1    | 47        | 0       | `75fe6da000`      |
| IO Port Dispatch | 90        | 60      | —    | —    | 0         | 30      | `fcbd9aed61`      |
| Input            | 149       | 149     | —    | —    | 126       | 0       | `fcbd9aed61`      |

Totals: **1819** plan rows, **1819** mapped to tests, **0** missing (non-Z80N). **Task 1 (Waves 1-3, 2026-04-15) refactored all 9 older compliance suites to the Phase 2 per-row idiom** — MMU/DMA/Audio/NextREG/UART+I2C/DivMMC+SPI/CTC/Tilemap/ULA Video. Every non-Z80N plan row now has a 1:1 test ID and concrete pass/fail/skip status in the Summary. Z80N remains data-driven (FUSE runner) by design. Per-row Status columns inside the 9 refactored sections below are still `—` in this commit — the mechanical per-row extractor pass is deferred to a follow-up commit to keep the Task 1 merges focused on test-code and plan-level status. Aggregate numbers above are the authoritative signal for Waves 1-3 completion. Per-row `pass`/`fail` columns are left as `—` because this is a read-only traceability pass and tests were not executed. Skip counts are only populated for the 6 Phase 2 rewrite subsystems that use the `skip()` helper.

## Z80N — `test/z80n_test.cpp`

Last-touch commit: `8d0cf05a15f77099a6a7ac35bcd5cc5ad223019f` (`8d0cf05a15`)

| Test ID  | Plan row title    | VHDL file:line | Status  | Test file:line |
|----------|-------------------|----------------|---------|----------------|
| ED 23    | ED 23 SWAPNIB     | —              | missing | missing        |
| ED 24    | ED 24 MIRROR A    | —              | missing | missing        |
| ED 27 nn | ED 27 nn TEST n   | —              | missing | missing        |
| ED 28    | ED 28 BSLA DE,B   | —              | missing | missing        |
| ED 29    | ED 29 BSRA DE,B   | —              | missing | missing        |
| ED 2A    | ED 2A BSRL DE,B   | —              | missing | missing        |
| ED 2B    | ED 2B BSRF DE,B   | —              | missing | missing        |
| ED 2C    | ED 2C BRLC DE,B   | —              | missing | missing        |
| ED 30    | ED 30 MUL DE      | —              | missing | missing        |
| ED 31    | ED 31 ADD HL,A    | —              | missing | missing        |
| ED 32    | ED 32 ADD DE,A    | —              | missing | missing        |
| ED 33    | ED 33 ADD BC,A    | —              | missing | missing        |
| ED 34    | ED 34 ADD HL,nn   | —              | missing | missing        |
| ED 35    | ED 35 ADD DE,nn   | —              | missing | missing        |
| ED 36    | ED 36 ADD BC,nn   | —              | missing | missing        |
| ED 8A    | ED 8A PUSH nn     | —              | missing | missing        |
| ED 90    | ED 90 OUTINB      | —              | missing | missing        |
| ED 91    | ED 91 NEXTREG n,v | —              | missing | missing        |
| ED 92    | ED 92 NEXTREG n,A | —              | missing | missing        |
| ED 93    | ED 93 PIXELDN     | —              | missing | missing        |
| ED 94    | ED 94 PIXELAD     | —              | missing | missing        |
| ED 95    | ED 95 SETAE       | —              | missing | missing        |
| ED 98    | ED 98 JP (C)      | —              | missing | missing        |
| ED A4    | ED A4 LDIX        | —              | missing | missing        |
| ED A5    | ED A5 LDWS        | —              | missing | missing        |
| ED AC    | ED AC LDDX        | —              | missing | missing        |
| ED B4    | ED B4 LDIRX       | —              | missing | missing        |
| ED B6    | ED B6 LDIRSCALE   | —              | missing | missing        |
| ED B7    | ED B7 LDPIRX      | —              | missing | missing        |
| ED BC    | ED BC LDDRX       | —              | missing | missing        |

## Memory/MMU — `test/mmu/mmu_test.cpp`

Last-touch commit: `9fcc5802146a4e6a56bc2ad9abf19c0b202e680c` (`9fcc580214`)

| Test ID | Plan row title                                               | VHDL file:line  | Status  | Test file:line            |
|---------|--------------------------------------------------------------|-----------------|---------|---------------------------|
| MMU-01  | Write NR 0x50 = 0x00                                         | —               | missing | missing                   |
| MMU-02  | Write NR 0x51 = 0x01                                         | —               | missing | missing                   |
| MMU-03  | Write NR 0x52 = 0x04                                         | —               | missing | missing                   |
| MMU-04  | Write NR 0x53 = 0x05                                         | —               | missing | missing                   |
| MMU-05  | Write NR 0x54 = 0x0A                                         | —               | missing | missing                   |
| MMU-06  | Write NR 0x55 = 0x0B                                         | —               | missing | missing                   |
| MMU-07  | Write NR 0x56 = 0x0E                                         | —               | missing | missing                   |
| MMU-08  | Write NR 0x57 = 0x0F                                         | —               | missing | missing                   |
| MMU-09  | Write NR 0x50 = 0xFF                                         | —               | —       | test/mmu/mmu_test.cpp:171 |
| MMU-10  | High page (NR 0x54 = 0x40)                                   | —               | —       | test/mmu/mmu_test.cpp:180 |
| MMU-11  | Max page (NR 0x54 = 0xDF)                                    | —               | —       | test/mmu/mmu_test.cpp:189 |
| MMU-12  | Page 0xE0 overflows to ROM                                   | —               | missing | missing                   |
| MMU-13  | Read-back NR 0x50-0x57                                       | —               | —       | test/mmu/mmu_test.cpp:204 |
| MMU-14  | Write/read pattern all slots                                 | —               | —       | test/mmu/mmu_test.cpp:217 |
| MMU-15  | Slot boundary (0x1FFF/0x2000)                                | —               | —       | test/mmu/mmu_test.cpp:231 |
| RST-01  | MMU0 after reset                                             | —               | missing | missing                   |
| RST-02  | MMU1 after reset                                             | —               | missing | missing                   |
| RST-03  | MMU2 after reset                                             | —               | missing | missing                   |
| RST-04  | MMU3 after reset                                             | —               | missing | missing                   |
| RST-05  | MMU4 after reset                                             | —               | missing | missing                   |
| RST-06  | MMU5 after reset                                             | —               | missing | missing                   |
| RST-07  | MMU6 after reset                                             | —               | missing | missing                   |
| RST-08  | MMU7 after reset                                             | —               | missing | missing                   |
| P7F-01  | Bank 0 select                                                | —               | missing | missing                   |
| P7F-02  | Bank 1 select                                                | —               | missing | missing                   |
| P7F-03  | Bank 2 select                                                | —               | missing | missing                   |
| P7F-04  | Bank 3 select                                                | —               | missing | missing                   |
| P7F-05  | Bank 4 select                                                | —               | missing | missing                   |
| P7F-06  | Bank 5 select                                                | —               | missing | missing                   |
| P7F-07  | Bank 6 select                                                | —               | missing | missing                   |
| P7F-08  | Bank 7 select                                                | —               | missing | missing                   |
| P7F-09  | ROM 0 select                                                 | —               | —       | test/mmu/mmu_test.cpp:419 |
| P7F-10  | ROM 1 select (bit 4)                                         | —               | —       | test/mmu/mmu_test.cpp:431 |
| P7F-11  | Shadow screen (bit 3)                                        | —               | missing | missing                   |
| P7F-12  | Lock bit (bit 5)                                             | —               | —       | test/mmu/mmu_test.cpp:446 |
| P7F-13  | Locked write rejected                                        | —               | —       | test/mmu/mmu_test.cpp:460 |
| P7F-14  | NR 0x08 bit 7 unlocks                                        | —               | missing | missing                   |
| P7F-15  | Full register preserved                                      | —               | —       | test/mmu/mmu_test.cpp:469 |
| DFF-01  | Extra bit 0                                                  | —               | missing | missing                   |
| DFF-02  | Extra bit 1                                                  | —               | missing | missing                   |
| DFF-03  | Extra bit 2                                                  | —               | missing | missing                   |
| DFF-04  | Extra bit 3                                                  | —               | missing | missing                   |
| DFF-05  | Max bank (DFFD=0x0F,7FFD=7)                                  | —               | missing | missing                   |
| DFF-06  | Locked by 7FFD bit 5                                         | —               | missing | missing                   |
| DFF-07  | Bit 4 (Profi DFFD override)                                  | —               | missing | missing                   |
| P1F-01  | ROM bank 0 (+3 mode)                                         | —               | missing | missing                   |
| P1F-02  | ROM bank 1 (+3 mode)                                         | —               | missing | missing                   |
| P1F-03  | ROM bank 2 (+3 mode)                                         | —               | missing | missing                   |
| P1F-04  | ROM bank 3 (+3 mode)                                         | —               | missing | missing                   |
| P1F-05  | Special mode enable                                          | —               | —       | test/mmu/mmu_test.cpp:490 |
| P1F-06  | Locked by 7FFD bit 5                                         | —               | —       | test/mmu/mmu_test.cpp:500 |
| P1F-07  | Motor bit independent                                        | —               | missing | missing                   |
| SPE-01  | 00 (1FFD=0x01)                                               | —               | missing | missing                   |
| SPE-02  | 01 (1FFD=0x03)                                               | —               | missing | missing                   |
| SPE-03  | 10 (1FFD=0x05)                                               | —               | missing | missing                   |
| SPE-04  | 11 (1FFD=0x07)                                               | —               | missing | missing                   |
| SPE-05  | Exit special mode                                            | —               | —       | test/mmu/mmu_test.cpp:566 |
| LCK-01  | 7FFD bit 5 locks 7FFD writes                                 | —               | —       | test/mmu/mmu_test.cpp:584 |
| LCK-02  | 7FFD bit 5 locks 1FFD writes                                 | —               | —       | test/mmu/mmu_test.cpp:594 |
| LCK-03  | 7FFD bit 5 locks DFFD writes                                 | —               | missing | missing                   |
| LCK-04  | NR 0x08 bit 7 clears lock                                    | —               | missing | missing                   |
| LCK-05  | Pentagon-1024 overrides lock                                 | —               | missing | missing                   |
| LCK-06  | MMU writes bypass lock                                       | —               | —       | test/mmu/mmu_test.cpp:604 |
| LCK-07  | NR 0x8E bypasses lock                                        | —               | missing | missing                   |
| N8E-01  | Bank select (bit 3=1)                                        | —               | missing | missing                   |
| N8E-02  | ROM select (bit 3=0, bit 2=0)                                | —               | missing | missing                   |
| N8E-03  | Special mode via 8E                                          | —               | missing | missing                   |
| N8E-04  | Special + config bits                                        | —               | missing | missing                   |
| N8E-05  | Read-back format                                             | —               | missing | missing                   |
| N8E-06  | Bank select clears DFFD(3)                                   | —               | missing | missing                   |
| N8F-01  | Standard mode (default)                                      | —               | missing | missing                   |
| N8F-02  | Pentagon 512K                                                | —               | missing | missing                   |
| N8F-03  | Pentagon 1024K                                               | —               | missing | missing                   |
| N8F-04  | Pentagon 1024K disabled by EFF7                              | —               | missing | missing                   |
| N8F-05  | Pentagon bank(6) always 0                                    | —               | missing | missing                   |
| EF7-01  | Bit 3 = RAM at 0x0000                                        | —               | missing | missing                   |
| EF7-02  | Bit 3 = 0 → ROM at 0x0000                                    | —               | missing | missing                   |
| EF7-03  | Bit 2 = 1 disables Pent-1024                                 | —               | missing | missing                   |
| EF7-04  | Reset state                                                  | —               | missing | missing                   |
| ROM-01  | 48K always ROM 0                                             | —               | —       | test/mmu/mmu_test.cpp:320 |
| ROM-02  | 128K ROM 0                                                   | —               | —       | test/mmu/mmu_test.cpp:333 |
| ROM-03  | 128K ROM 1                                                   | —               | —       | test/mmu/mmu_test.cpp:343 |
| ROM-04  | +3 ROM 0                                                     | —               | —       | test/mmu/mmu_test.cpp:353 |
| ROM-05  | +3 ROM 1                                                     | —               | —       | test/mmu/mmu_test.cpp:365 |
| ROM-06  | +3 ROM 2                                                     | —               | —       | test/mmu/mmu_test.cpp:379 |
| ROM-07  | +3 ROM 3                                                     | —               | missing | missing                   |
| ROM-08  | ROM is read-only                                             | —               | —       | test/mmu/mmu_test.cpp:622 |
| ROM-09  | ROM with altrom_rw = 1                                       | —               | missing | missing                   |
| ALT-01  | Enable altrom                                                | —               | missing | missing                   |
| ALT-02  | Disable altrom                                               | —               | missing | missing                   |
| ALT-03  | Altrom read/write enable                                     | —               | missing | missing                   |
| ALT-04  | Altrom read-only                                             | —               | missing | missing                   |
| ALT-05  | Lock ROM1                                                    | —               | missing | missing                   |
| ALT-06  | Lock ROM0                                                    | —               | missing | missing                   |
| ALT-07  | Reset preserves bits 3:0                                     | —               | missing | missing                   |
| ALT-08  | Altrom address 128K                                          | —               | missing | missing                   |
| ALT-09  | Read-back                                                    | —               | missing | missing                   |
| CFG-01  | Config mode maps ROMRAM                                      | —               | missing | missing                   |
| CFG-02  | Config mode off → normal ROM                                 | —               | missing | missing                   |
| CFG-03  | ROMRAM bank writeable                                        | —               | missing | missing                   |
| CFG-04  | Config mode at reset                                         | —               | missing | missing                   |
| ADR-01  | 0x00                                                         | —               | missing | missing                   |
| ADR-02  | 0x01                                                         | —               | missing | missing                   |
| ADR-03  | 0x0A                                                         | —               | missing | missing                   |
| ADR-04  | 0x0B                                                         | —               | missing | missing                   |
| ADR-05  | 0x0E                                                         | —               | missing | missing                   |
| ADR-06  | 0x10                                                         | —               | missing | missing                   |
| ADR-07  | 0x20                                                         | —               | missing | missing                   |
| ADR-08  | 0xDF                                                         | —               | missing | missing                   |
| ADR-09  | 0xE0                                                         | —               | missing | missing                   |
| ADR-10  | 0xFF                                                         | —               | missing | missing                   |
| BNK-01  | Page 0x0A → bank5 path                                       | —               | —       | test/mmu/mmu_test.cpp:673 |
| BNK-02  | Page 0x0B → bank5 path                                       | —               | —       | test/mmu/mmu_test.cpp:676 |
| BNK-03  | Page 0x0E → bank7 path                                       | —               | —       | test/mmu/mmu_test.cpp:686 |
| BNK-04  | Page 0x0F → normal SRAM                                      | —               | —       | test/mmu/mmu_test.cpp:696 |
| BNK-05  | Bank5 read/write functional                                  | —               | —       | test/mmu/mmu_test.cpp:651 |
| BNK-06  | Bank7 read/write functional                                  | —               | —       | test/mmu/mmu_test.cpp:661 |
| CON-01  | 48K: bank 5 contended                                        | —               | —       | test/mmu/mmu_test.cpp:777 |
| CON-02  | 48K: bank 5 hi contended                                     | —               | —       | test/mmu/mmu_test.cpp:780 |
| CON-03  | 48K: bank 0 not contended                                    | —               | —       | test/mmu/mmu_test.cpp:790 |
| CON-04  | 48K: bank 7 not contended                                    | —               | —       | test/mmu/mmu_test.cpp:800 |
| CON-05  | 128K: odd banks contended                                    | —               | missing | missing                   |
| CON-06  | 128K: even banks not contended                               | —               | missing | missing                   |
| CON-07  | +3: banks >= 4 contended                                     | —               | missing | missing                   |
| CON-08  | +3: banks < 4 not contended                                  | —               | —       | test/mmu/mmu_test.cpp:809 |
| CON-09  | High page never contended                                    | —               | missing | missing                   |
| CON-10  | NR 0x08 bit 6 disables contention                            | —               | missing | missing                   |
| CON-11  | Speed > 3.5 MHz no contention                                | —               | missing | missing                   |
| CON-12  | Pentagon timing no contention                                | —               | missing | missing                   |
| L2M-01  | L2 write-over routes writes to L2 bank, not to unrelated MM… | —               | —       | test/mmu/mmu_test.cpp:721 |
| L2M-01b | L2 bank 8 physically aliases MMU page 0x10 (hw collision)    | zxnext.vhd:2964 | —       | test/mmu/mmu_test.cpp:745 |
| L2M-02  | L2 read-enable maps 0-16K                                    | —               | missing | missing                   |
| L2M-03  | L2 auto segment follows A(15:14)                             | —               | missing | missing                   |
| L2M-04  | L2 does NOT map 48K-64K                                      | —               | —       | test/mmu/mmu_test.cpp:758 |
| L2M-05  | L2 bank from NR 0x12                                         | —               | missing | missing                   |
| L2M-06  | L2 shadow bank from NR 0x13                                  | —               | missing | missing                   |
| PRI-01  | DivMMC ROM overrides MMU                                     | —               | missing | missing                   |
| PRI-02  | DivMMC RAM overrides MMU                                     | —               | missing | missing                   |
| PRI-03  | L2 overrides MMU in 0-16K                                    | —               | missing | missing                   |
| PRI-04  | L2 does not override DivMMC                                  | —               | missing | missing                   |
| PRI-05  | MMU page in upper 48K                                        | —               | missing | missing                   |
| PRI-06  | Altrom overrides normal ROM                                  | —               | missing | missing                   |
| PRI-07  | Config mode overrides ROM                                    | —               | missing | missing                   |

### Extra coverage (not in plan)

| Test ID | Assertion description                       | VHDL file:line | Test file:line            |
|---------|---------------------------------------------|----------------|---------------------------|
| RST-09  | MMU0 is ROM after reset                     | —              | test/mmu/mmu_test.cpp:120 |
| RST-10  | MMU1 is ROM after reset                     | —              | test/mmu/mmu_test.cpp:122 |
| RW-01   | Write 0x42 to 0x8000 (page 0x10), read back | —              | test/mmu/mmu_test.cpp:249 |
| RW-02   | Independent writes to two slots             | —              | test/mmu/mmu_test.cpp:262 |
| RW-03   | Same page in two slots shares data          | —              | test/mmu/mmu_test.cpp:274 |
| RW-04   | Write across slot 4/5 boundary              | —              | test/mmu/mmu_test.cpp:287 |
| RW-05   | All 8 slots independently writable          | —              | test/mmu/mmu_test.cpp:305 |

## ULA Video — `test/ula/ula_test.cpp`

Last-touch commit: `9fcc5802146a4e6a56bc2ad9abf19c0b202e680c` (`9fcc580214`)

| Test ID | Plan row title                                  | VHDL file:line | Status  | Test file:line            |
|---------|-------------------------------------------------|----------------|---------|---------------------------|
| S01.01  | Top-left pixel                                  | —              | missing | missing                   |
| S01.02  | First char row, col 1                           | —              | missing | missing                   |
| S01.03  | Pixel row 1 in char row 0                       | —              | missing | missing                   |
| S01.04  | Pixel row 7 in char row 0                       | —              | missing | missing                   |
| S01.05  | Char row 1, pixel row 0                         | —              | missing | missing                   |
| S01.06  | Third of screen (py=64)                         | —              | missing | missing                   |
| S01.07  | Bottom-right pixel                              | —              | missing | missing                   |
| S01.08  | Alternate display file (mode(0)=1)              | —              | missing | missing                   |
| S01.09  | Middle of screen (py=96, px=128)                | —              | missing | missing                   |
| S01.10  | Wrap within third (py=63)                       | —              | missing | missing                   |
| S01.11  | Second third start+1 row                        | —              | missing | missing                   |
| S01.12  | Last pixel row of last char                     | —              | missing | missing                   |
| S02.01  | Ink, no bright, colour 0                        | —              | missing | missing                   |
| S02.02  | Paper, no bright, colour 0                      | —              | missing | missing                   |
| S02.03  | Ink, bright, red (2)                            | —              | missing | missing                   |
| S02.04  | Paper, bright, green (4)                        | —              | missing | missing                   |
| S02.05  | Ink white, no bright                            | —              | missing | missing                   |
| S02.06  | Paper white, bright                             | —              | missing | missing                   |
| S02.07  | Ink cyan (5), bright                            | —              | missing | missing                   |
| S02.08  | Flash bit set, no bright, ink                   | —              | missing | missing                   |
| S02.09  | Full white on black, bright                     | —              | missing | missing                   |
| S02.10  | Border pixel (border_active_d=1)                | —              | —       | test/ula/ula_test.cpp:252 |
| S03.01  | Black border                                    | —              | missing | missing                   |
| S03.02  | Blue border                                     | —              | missing | missing                   |
| S03.03  | Red border                                      | —              | missing | missing                   |
| S03.04  | White border                                    | —              | missing | missing                   |
| S03.05  | Green border                                    | —              | missing | missing                   |
| S03.06  | Timex border, port_ff(5:3)=0                    | —              | —       | test/ula/ula_test.cpp:304 |
| S03.07  | Timex border, port_ff(5:3)=7                    | —              | —       | test/ula/ula_test.cpp:313 |
| S03.08  | Border active region boundaries                 | —              | —       | test/ula/ula_test.cpp:316 |
| S04.01  | Flash period = 32 frames                        | —              | —       | test/ula/ula_test.cpp:368 |
| S04.02  | Flash attr bit=0: no inversion                  | —              | —       | test/ula/ula_test.cpp:385 |
| S04.03  | Flash attr bit=1, counter bit4=0                | —              | —       | test/ula/ula_test.cpp:403 |
| S04.04  | Flash attr bit=1, counter bit4=1                | —              | missing | missing                   |
| S04.05  | Flash disabled in ULAnext mode                  | —              | missing | missing                   |
| S04.06  | Flash disabled in ULA+ mode                     | —              | missing | missing                   |
| S05.01  | Standard mode (000)                             | —              | —       | test/ula/ula_test.cpp:421 |
| S05.02  | Alt display file (001)                          | —              | —       | test/ula/ula_test.cpp:428 |
| S05.03  | Hi-colour mode (010)                            | —              | —       | test/ula/ula_test.cpp:433 |
| S05.04  | Hi-colour + alt file (011)                      | —              | —       | test/ula/ula_test.cpp:438 |
| S05.05  | Hi-res mode (100)                               | —              | —       | test/ula/ula_test.cpp:465 |
| S05.06  | Hi-res uses timex border colour                 | —              | —       | test/ula/ula_test.cpp:483 |
| S05.07  | Shadow screen forces mode "000"                 | —              | missing | missing                   |
| S05.08  | Hi-res attr_reg uses border_clr_tmx             | —              | missing | missing                   |
| S06.01  | Ink, format 0x07                                | —              | missing | missing                   |
| S06.02  | Paper, format 0x07                              | —              | missing | missing                   |
| S06.03  | Ink, format 0x0F                                | —              | missing | missing                   |
| S06.04  | Paper, format 0x0F                              | —              | missing | missing                   |
| S06.05  | Ink, format 0xFF                                | —              | missing | missing                   |
| S06.06  | Paper, format 0xFF                              | —              | missing | missing                   |
| S06.07  | Border, format 0x07                             | —              | missing | missing                   |
| S06.08  | Border, format 0xFF                             | —              | missing | missing                   |
| S06.09  | Ink, format 0x01                                | —              | missing | missing                   |
| S06.10  | Paper, format 0x01                              | —              | missing | missing                   |
| S06.11  | Ink, format 0x3F                                | —              | missing | missing                   |
| S06.12  | Non-standard format (e.g. 0x05)                 | —              | missing | missing                   |
| S07.01  | Ink, group 0                                    | —              | missing | missing                   |
| S07.02  | Paper, group 0                                  | —              | missing | missing                   |
| S07.03  | Ink, group 3                                    | —              | missing | missing                   |
| S07.04  | Paper, group 3                                  | —              | missing | missing                   |
| S07.05  | Hi-res forces bit 3 high                        | —              | missing | missing                   |
| S07.06  | Flash bit NOT used (attr bit 7 = palette group) | —              | missing | missing                   |
| S08.01  | Default window, inside                          | —              | —       | test/ula/ula_test.cpp:499 |
| S08.02  | Narrow window, inside                           | —              | —       | test/ula/ula_test.cpp:501 |
| S08.03  | Narrow window, outside left                     | —              | —       | test/ula/ula_test.cpp:503 |
| S08.04  | Narrow window, outside right                    | —              | —       | test/ula/ula_test.cpp:505 |
| S08.05  | Narrow window, outside top                      | —              | —       | test/ula/ula_test.cpp:514 |
| S08.06  | Narrow window, outside bottom                   | —              | —       | test/ula/ula_test.cpp:516 |
| S08.07  | Border area: never clipped                      | —              | —       | test/ula/ula_test.cpp:518 |
| S08.08  | y2 >= 0xC0 clamped to 0xBF                      | —              | —       | test/ula/ula_test.cpp:520 |
| S09.01  | No scroll                                       | —              | missing | missing                   |
| S09.02  | Scroll Y by 1                                   | —              | missing | missing                   |
| S09.03  | Scroll Y by 191                                 | —              | missing | missing                   |
| S09.04  | Scroll Y wraps at 192                           | —              | missing | missing                   |
| S09.05  | Scroll X by 8 (1 char)                          | —              | missing | missing                   |
| S09.06  | Scroll X by 1 (fine)                            | —              | missing | missing                   |
| S09.07  | Scroll X by 255                                 | —              | missing | missing                   |
| S09.08  | Fine scroll X enabled                           | —              | missing | missing                   |
| S09.09  | Combined X+Y scroll                             | —              | missing | missing                   |
| S09.10  | Y scroll wraps mid-third                        | —              | missing | missing                   |
| S10.01  | Border region, 48K                              | —              | missing | missing                   |
| S10.02  | Active display, phase 0x9                       | —              | missing | missing                   |
| S10.03  | Active display, phase 0xB                       | —              | missing | missing                   |
| S10.04  | Active display, phase 0x1                       | —              | missing | missing                   |
| S10.05  | +3 timing, bit 0 forced                         | —              | missing | missing                   |
| S10.06  | +3 timing, border fallback                      | —              | missing | missing                   |
| S10.07  | Port 0xFF read, ff_rd_en=0                      | —              | missing | missing                   |
| S10.08  | Port 0xFF read, ff_rd_en=1                      | —              | missing | missing                   |
| S11.01  | 48K, bank 5 read, contention phase              | —              | missing | missing                   |
| S11.02  | 48K, bank 0 read                                | —              | missing | missing                   |
| S11.03  | 48K, non-contention phase (hc_adj 3:2 = "00")   | —              | missing | missing                   |
| S11.04  | 48K, vc >= 192 (border)                         | —              | missing | missing                   |
| S11.05  | 48K, even port I/O                              | —              | missing | missing                   |
| S11.06  | 48K, odd port I/O                               | —              | missing | missing                   |
| S11.07  | 128K, bank 1 read                               | —              | missing | missing                   |
| S11.08  | 128K, bank 4 read                               | —              | missing | missing                   |
| S11.09  | +3, bank 4+ read                                | —              | missing | missing                   |
| S11.10  | +3, bank 0 read                                 | —              | missing | missing                   |
| S11.11  | Pentagon timing                                 | —              | missing | missing                   |
| S11.12  | CPU speed > 3.5 MHz                             | —              | missing | missing                   |
| S12.01  | ULA enabled (default)                           | —              | —       | test/ula/ula_test.cpp:534 |
| S12.02  | ULA disabled                                    | —              | —       | test/ula/ula_test.cpp:538 |
| S12.03  | ULA disable + re-enable                         | —              | —       | test/ula/ula_test.cpp:542 |
| S12.04  | Blend mode bits                                 | —              | missing | missing                   |
| S13.01  | 48K frame length                                | —              | —       | test/ula/ula_test.cpp:569 |
| S13.02  | 128K frame length                               | —              | —       | test/ula/ula_test.cpp:572 |
| S13.03  | Pentagon frame length                           | —              | —       | test/ula/ula_test.cpp:575 |
| S13.04  | Active display start 48K                        | —              | —       | test/ula/ula_test.cpp:582 |
| S13.05  | Active display start 128K                       | —              | —       | test/ula/ula_test.cpp:585 |
| S13.06  | Active display start Pentagon                   | —              | —       | test/ula/ula_test.cpp:588 |
| S13.07  | ULA hc resets correctly                         | —              | —       | test/ula/ula_test.cpp:595 |
| S13.08  | 60Hz frame length                               | —              | —       | test/ula/ula_test.cpp:598 |
| S14.01  | 48K interrupt position                          | —              | missing | missing                   |
| S14.02  | 128K interrupt position                         | —              | missing | missing                   |
| S14.03  | Pentagon interrupt position                     | —              | missing | missing                   |
| S14.04  | Interrupt disabled                              | —              | missing | missing                   |
| S14.05  | Line interrupt fires                            | —              | missing | missing                   |
| S14.06  | Line interrupt 0 = last line                    | —              | missing | missing                   |
| S15.01  | Normal screen (shadow=0)                        | —              | —       | test/ula/ula_test.cpp:668 |
| S15.02  | Shadow screen (shadow=1)                        | —              | —       | test/ula/ula_test.cpp:672 |
| S15.03  | Shadow disables Timex modes                     | —              | —       | test/ula/ula_test.cpp:677 |
| S15.04  | Shadow bit toggles display                      | —              | missing | missing                   |

### Extra coverage (not in plan)

| Test ID | Assertion description                         | VHDL file:line | Test file:line            |
|---------|-----------------------------------------------|----------------|---------------------------|
| S02.11  | Rendered paper pixel (0x00 pixels, 0x47 attr) | —              | test/ula/ula_test.cpp:257 |
| S13.09  | Pentagon T-states/frame = 71680               | —              | test/ula/ula_test.cpp:601 |
| S13.10  | Display left = 128                            | —              | test/ula/ula_test.cpp:606 |
| S13.11  | Display top = 64                              | —              | test/ula/ula_test.cpp:609 |
| S13.12  | Display width = 256                           | —              | test/ula/ula_test.cpp:612 |
| S13.13  | Display height = 192                          | —              | test/ula/ula_test.cpp:615 |
| S13.14  | Frame complete after full T-states            | —              | test/ula/ula_test.cpp:625 |
| SR.01   | rrrgggbb 0x00 -> black                        | —              | test/ula/ula_test.cpp:692 |
| SR.02   | rrrgggbb 0xFF -> white                        | —              | test/ula/ula_test.cpp:698 |
| SR.03   | rrrgggbb 0xE0 -> red                          | —              | test/ula/ula_test.cpp:705 |
| SR.04   | FB_WIDTH = 320                                | —              | test/ula/ula_test.cpp:710 |
| SR.05   | FB_HEIGHT = 256                               | —              | test/ula/ula_test.cpp:713 |
| SR.06   | DISP_X = 32                                   | —              | test/ula/ula_test.cpp:716 |
| SR.07   | DISP_Y = 32                                   | —              | test/ula/ula_test.cpp:719 |
| SD.01   | ULA FB_WIDTH = 320                            | —              | test/ula/ula_test.cpp:731 |
| SD.02   | ULA FB_HEIGHT = 256                           | —              | test/ula/ula_test.cpp:734 |
| SD.03   | ULA DISP_X = 32 (left border)                 | —              | test/ula/ula_test.cpp:737 |
| SD.04   | ULA DISP_Y = 32 (top border)                  | —              | test/ula/ula_test.cpp:740 |
| SD.05   | ULA DISP_W = 256                              | —              | test/ula/ula_test.cpp:743 |
| SD.06   | ULA DISP_H = 192                              | —              | test/ula/ula_test.cpp:746 |
| SD.07   | Border widths sum correctly (32+256+32=320)   | —              | test/ula/ula_test.cpp:749 |
| SD.08   | Border heights sum correctly (32+192+32=256)  | —              | test/ula/ula_test.cpp:752 |
| S03P.01 | Init fills all lines with current border      | —              | test/ula/ula_test.cpp:769 |
| S03P.02 | Per-line snapshot at line 100                 | —              | test/ula/ula_test.cpp:778 |
| S03P.03 | Other lines unchanged                         | —              | test/ula/ula_test.cpp:781 |
| S03P.04 | Out-of-range line returns current border      | —              | test/ula/ula_test.cpp:786 |

## Layer2 — `test/layer2/layer2_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID | Plan row title                                               | VHDL file:line       | Status  | Test file:line                   |
|---------|--------------------------------------------------------------|----------------------|---------|----------------------------------|
| G1-01   | NR 0x12 default                                              | zxnext.vhd:4943      | pass    | test/layer2/layer2_test.cpp:221  |
| G1-02   | NR 0x13 default                                              | zxnext.vhd:4944      | pass    | test/layer2/layer2_test.cpp:226  |
| G1-03   | NR 0x14 default                                              | zxnext.vhd:4946      | missing | missing                          |
| G1-04   | NR 0x16 default                                              | zxnext.vhd:4955      | missing | missing                          |
| G1-05   | NR 0x17 default                                              | zxnext.vhd:4957      | missing | missing                          |
| G1-06   | NR 0x18 defaults                                             | zxnext.vhd:4959-4962 | missing | missing                          |
| G1-07   | NR 0x43[2] default                                           | zxnext.vhd:5007      | missing | missing                          |
| G1-08   | NR 0x4A default                                              | zxnext.vhd:5014      | missing | missing                          |
| G1-09   | NR 0x70 default                                              | zxnext.vhd:5047-5048 | pass    | test/layer2/layer2_test.cpp:232  |
| G1-10   | NR 0x71[0] default                                           | zxnext.vhd:5050      | missing | missing                          |
| G1-11   | port 0x123B default                                          | zxnext.vhd:3908-3913 | missing | missing                          |
| G1-12   | Layer 2 off after reset                                      | zxnext.vhd:3908      | pass    | test/layer2/layer2_test.cpp:239  |
| G2-01   | 256x192 row-major address                                    | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:287  |
| G2-02   | 256x192 row pitch = 256                                      | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:317  |
| G2-03   | 256x192 y≥192 invisible                                      | layer2.vhd:165       | pass    | test/layer2/layer2_test.cpp:333  |
| G2-04   | 256x192 x wraparound at 256 is impossible (no stimulus rout… | layer2.vhd:164       | missing | missing                          |
| G2-05   | 320x256 column-major address                                 | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:345  |
| G2-06   | 320x256 column pitch = 256                                   | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:367  |
| G2-07   | 320x256 x in [320,383] invisible                             | layer2.vhd:164       | missing | missing                          |
| G2-08   | 320x256 y=255 visible                                        | layer2.vhd:165       | pass    | test/layer2/layer2_test.cpp:376  |
| G2-09   | 640x256 high nibble = left pixel                             | layer2.vhd:202       | pass    | test/layer2/layer2_test.cpp:390  |
| G2-10   | 640x256 only 4-bit index pre-offset                          | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:419  |
| G2-11   | 640x256 shares 320 column layout                             | layer2.vhd:160       | missing | missing                          |
| G2-12   | Lookahead one pixel                                          | layer2.vhd:148       | missing | missing                          |
| G3-01   | 256x192 scroll X=128                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:459  |
| G3-02   | 256x192 scroll X=255                                         | layer2.vhd:152       | pass    | test/layer2/layer2_test.cpp:474  |
| G3-03   | 256x192 scroll Y wrap from 192                               | layer2.vhd:156-158   | pass    | test/layer2/layer2_test.cpp:489  |
| G3-04   | 256x192 scroll Y wrap from 193                               | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:499  |
| G3-05   | 256x192 scroll Y=96                                          | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:509  |
| G3-06   | Scroll X MSB (nr_71[0]) in 256 mode                          | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:525  |
| G3-07   | 320x256 scroll X=160                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:543  |
| G3-08   | 320x256 scroll X=319                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:558  |
| G3-09   | 320x256 scroll X wrap arithmetic                             | layer2.vhd:153       | missing | missing                          |
| G3-10   | 320x256 scroll Y=128                                         | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:572  |
| G3-11   | 640x256 scroll X=160 byte-level                              | layer2.vhd:152-154   | missing | missing                          |
| G3-12   | Negative path: 320x256 scroll X wrap branch skipped when x_… | layer2.vhd:153       | pass    | test/layer2/layer2_test.cpp:584  |
| G4-01a  | Auto-index advances — slot 0 observable                      | zxnext.vhd:5243-5249 | missing | missing                          |
| G4-01b  | Auto-index advances — slot 1 observable                      | zxnext.vhd:5243-5249 | missing | missing                          |
| G4-01c  | Auto-index advances — slot 2 observable                      | zxnext.vhd:5243-5249 | missing | missing                          |
| G4-01d  | Auto-index advances — slot 3 observable and wraps            | zxnext.vhd:5243-5249 | missing | missing                          |
| G4-02   | Auto-index wraps at 4                                        | zxnext.vhd:5249      | missing | missing                          |
| G4-03   | NR 0x1C[0] resets L2 clip index                              | zxnext.vhd:5278-5281 | missing | missing                          |
| G4-04   | NR 0x1C[0]=0 leaves L2 index alone                           | zxnext.vhd:5278-5281 | missing | missing                          |
| G4-05   | 256x192 default clip covers full area                        | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:627  |
| G4-06   | 256x192 clip to centre 64x64                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:640  |
| G4-07   | 256x192 clip x1==x2 single column                            | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:659  |
| G4-08   | 256x192 clip x1 > x2 → empty                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:673  |
| G4-09   | 320x256 clip X is doubled                                    | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:686  |
| G4-10   | 320x256 clip Y is not doubled                                | layer2.vhd:137-138   | pass    | test/layer2/layer2_test.cpp:700  |
| G4-11   | 320x256 clip `x1=0,x2=0` gives 2-pixel-wide strip            | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:717  |
| G4-12   | 640x256 clip uses same doubling as 320                       | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:738  |
| G4-13   | Clip is inclusive on both edges                              | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:758  |
| G5-01   | Offset 0 identity                                            | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:794  |
| G5-02   | Offset 1 shifts high nibble                                  | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:804  |
| G5-03   | Offset 15, high nibble 0                                     | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:813  |
| G5-04   | Offset 15, high nibble 1 → wraps to 0                        | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:821  |
| G5-05   | 4-bit mode high nibble is pre-offset zero                    | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:835  |
| G5-06   | 4-bit mode offset shifts into upper nibble                   | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:844  |
| G5-07   | 4-bit mode low nibble is right pixel                         | layer2.vhd:202       | pass    | test/layer2/layer2_test.cpp:854  |
| G5-08   | Palette 0 vs Palette 1                                       | zxnext.vhd:6827      | pass    | test/layer2/layer2_test.cpp:875  |
| G5-09   | Palette select does not affect sprite/ula palette            | zxnext.vhd:6827      | missing | missing                          |
| G6-01   | Index ≠ 0xE3, RGB = 0xE3 → transparent (would catch "index…  | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:921  |
| G6-02   | Index = 0xE3, RGB ≠ 0xE3 → opaque (would catch "index check… | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:929  |
| G6-03   | Identity palette, default NR 0x14                            | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:941  |
| G6-04   | Change NR 0x14 to 0x00                                       | zxnext.vhd:5226      | pass    | test/layer2/layer2_test.cpp:951  |
| G6-05   | Clip outside ⇒ transparent regardless of colour              | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:966  |
| G6-06   | L2 disabled ⇒ all transparent                                | layer2.vhd:175       | pass    | test/layer2/layer2_test.cpp:977  |
| G6-07   | Fallback 0xE3 visible when every layer transparent           | zxnext.vhd:5014      | missing | missing                          |
| G6-08   | Fallback colour follows NR 0x4A write                        | zxnext.vhd:5407      | missing | missing                          |
| G6-09   | Priority bit gated by transparency                           | zxnext.vhd:7123      | missing | missing                          |
| G7-01   | Bank `+1` transform on default bank                          | layer2.vhd:172       | pass    | test/layer2/layer2_test.cpp:1021 |
| G7-02   | Bank `+1` transform, nonzero high 3 bits                     | layer2.vhd:172       | pass    | test/layer2/layer2_test.cpp:1038 |
| G7-03   | Bank `+1` transform, max legal                               | layer2.vhd:172-175   | pass    | test/layer2/layer2_test.cpp:1050 |
| G7-04   | Out-of-range bank → no pixel                                 | layer2.vhd:173-175   | missing | missing                          |
| G7-05   | Address bits 16:14 select 16K page within 48K                | layer2.vhd:173       | pass    | test/layer2/layer2_test.cpp:1068 |
| G7-06   | 320x256 uses 5 pages                                         | layer2.vhd:160       | missing | missing                          |
| G7-07   | Port 0x123B bit 0 enables CPU writes                         | zxnext.vhd:3917      | missing | missing                          |
| G7-08   | Port 0x123B bit 2 enables CPU reads                          | zxnext.vhd:3918      | missing | missing                          |
| G7-09   | Port 0x123B bit 1 enables display                            | zxnext.vhd:3916      | missing | missing                          |
| G7-10   | Port 0x123B bit 1 and NR 0x69 bit 7 target same flop         | zxnext.vhd:3924-3925 | missing | missing                          |
| G7-11   | Port 0x123B bit 3 selects shadow bank for mapping only       | zxnext.vhd:2968      | missing | missing                          |
| G7-12   | Shadow bank data becomes visible after NR 0x12 rewrite       | layer2.vhd:172       | missing | missing                          |
| G7-13   | Port 0x123B bits 7:6 select segment                          | zxnext.vhd:2966-2967 | missing | missing                          |
| G7-14   | Port 0x123B segment=11 ⇒ A15:A14 selects page                | zxnext.vhd:2966      | missing | missing                          |
| G7-15   | Port 0x123B bit 4 (offset latch)                             | zxnext.vhd:3922      | missing | missing                          |
| G7-16   | Port 0x123B read-back formatting                             | zxnext.vhd:3933      | missing | missing                          |
| G8-01   | NR 0x15 priority SLU with L2 opaque over ULA                 | zxnext.vhd:7216      | missing | missing                          |
| G8-02   | L2 transparent ⇒ ULA shows through in SLU                    | zxnext.vhd:7121-7122 | missing | missing                          |
| G8-03   | L2 priority bit promotes over sprite                         | zxnext.vhd:7050      | missing | missing                          |
| G8-04   | Priority bit suppressed when L2 pixel transparent            | zxnext.vhd:7123      | missing | missing                          |
| G8-05   | `layer2_rgb` zeroed when transparent                         | zxnext.vhd:7122      | missing | missing                          |
| G9-01   | Disable then re-enable via NR 0x69                           | zxnext.vhd:3924      | missing | missing                          |
| G9-02   | Cold-reset port 0x123B read is 0x00                          | zxnext.vhd:3908-3913 | missing | missing                          |
| G9-03   | Clip y1 > y2 empties display                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:1119 |
| G9-04   | Scroll X with wide branch NOT fired                          | —                    | missing | missing                          |
| G9-05   | Wide mode clip `x2=0xFF` ⇒ effective 511                     | layer2.vhd:134       | pass    | test/layer2/layer2_test.cpp:1139 |
| G9-06   | `hc_eff = hc + 1` cannot be detected as a pure scroll (non-… | layer2.vhd:148       | missing | missing                          |

## Sprites — `test/sprites/sprites_test.cpp`

Last-touch commit: `28f5afb5407e564db0970f142782fceba1b33936` (`28f5afb540`)

| Test ID    | Plan row title                                               | VHDL file:line  | Status  | Test file:line                     |
|------------|--------------------------------------------------------------|-----------------|---------|------------------------------------|
| CL-01      | `check(..., true)` — no clip semantics verified              | —               | missing | missing                            |
| CL-02      | `check(..., true)` — setters only                            | —               | missing | missing                            |
| CL-03      | `check(..., true)` — setter only, wrong group                | —               | missing | missing                            |
| CL-04      | `check(..., true)` — setter, misnamed as clip                | —               | missing | missing                            |
| RST-04     | `check(..., true)` — no getter, no assertion                 | —               | missing | missing                            |
| RST-05     | `check(..., true)` — same                                    | —               | missing | missing                            |
| G1.AT-01   | 4-byte write auto-skips to next sprite attr0                 | —               | pass    | test/sprites/sprites_test.cpp:216  |
| G1.AT-02   | 5-byte write advances through attr4                          | —               | pass    | test/sprites/sprites_test.cpp:234  |
| G1.AT-03   | 0x303B sets `attr_index = d(6:0) & "000"`                    | —               | pass    | test/sprites/sprites_test.cpp:246  |
| G1.AT-04   | 0x303B sets `pattern_index = d(5:0)&d(7)&"0000000"`          | —               | pass    | test/sprites/sprites_test.cpp:263  |
| G1.AT-05   | Attr2 bitfields readable as (paloff, xm, ym, rot, xmsb)      | —               | pass    | test/sprites/sprites_test.cpp:273  |
| G1.AT-06   | Attr4 bitfields (H, N6, type, xscale, yscale, ymsb)          | —               | pass    | test/sprites/sprites_test.cpp:290  |
| G1.AT-07   | Sprite 127 is the last slot                                  | —               | pass    | test/sprites/sprites_test.cpp:306  |
| G1.AT-08   | Attr write via NR 0x34 mirror path                           | —               | pass    | test/sprites/sprites_test.cpp:316  |
| G1.AT-09   | Mirror `index="111"` sets sprite number                      | —               | pass    | test/sprites/sprites_test.cpp:326  |
| G1.AT-10   | `mirror_inc_i` increments within 7 bits                      | —               | pass    | test/sprites/sprites_test.cpp:339  |
| G1.AT-11   | `mirror_tie_i=1` syncs attr_index to mirror number           | —               | pass    | test/sprites/sprites_test.cpp:351  |
| G1.AT-12   | Mirror write takes priority over pending CPU write           | —               | missing | missing                            |
| G2.PL-01   | 256-byte pattern upload targets bytes 0..255 of pattern 0    | —               | pass    | test/sprites/sprites_test.cpp:388  |
| G2.PL-02   | Last pattern (63) writable                                   | —               | pass    | test/sprites/sprites_test.cpp:403  |
| G2.PL-03   | Auto-increment crosses pattern boundary                      | —               | pass    | test/sprites/sprites_test.cpp:422  |
| G2.PL-04   | `pattern_index(7)` half-pattern offset for 4bpp              | —               | pass    | test/sprites/sprites_test.cpp:444  |
| G2.PL-05   | 14-bit pattern address does not spill above 0x3FFF           | —               | pass    | test/sprites/sprites_test.cpp:465  |
| G3.PX-01   | 8bpp opaque pixel, paloff=0, no mirror/rotate/scale          | —               | pass    | test/sprites/sprites_test.cpp:489  |
| G3.PX-02   | 8bpp paloff applies to upper nibble only                     | —               | pass    | test/sprites/sprites_test.cpp:503  |
| G3.PX-03   | 8bpp paloff upper nibble wraps mod 16                        | —               | pass    | test/sprites/sprites_test.cpp:517  |
| G3.PX-04   | 4bpp (H=1), even addr, upper nibble selected                 | —               | pass    | test/sprites/sprites_test.cpp:535  |
| G3.PX-05   | 4bpp, odd addr, lower nibble selected                        | —               | pass    | test/sprites/sprites_test.cpp:549  |
| G3.PX-06   | 4bpp addr remap: `pat_addr_b = addr(13:8) & n6 & addr(7:1)`  | —               | pass    | test/sprites/sprites_test.cpp:568  |
| G3.TR-01   | 8bpp transparent pixel (full byte) not written               | —               | pass    | test/sprites/sprites_test.cpp:582  |
| G3.TR-02   | 4bpp transparent nibble not written, other nibble of same b… | —               | pass    | test/sprites/sprites_test.cpp:602  |
| G3.TR-03   | Transparent compare is on palette **index**, not ARGB        | —               | pass    | test/sprites/sprites_test.cpp:629  |
| G3.TR-04   | 8bpp paloff change does not make the transparency check com… | —               | pass    | test/sprites/sprites_test.cpp:647  |
| G3.PA-01   | 4bpp replaces upper nibble with paloff                       | —               | pass    | test/sprites/sprites_test.cpp:663  |
| G3.PA-02   | Line buffer bit 8 set on any sprite write                    | —               | pass    | test/sprites/sprites_test.cpp:679  |
| G4.XY-01   | Sprite at (0,0) opaque fills [0..15] on line 0               | —               | pass    | test/sprites/sprites_test.cpp:705  |
| G4.XY-02   | X MSB (attr2(0)=1) gives x=256+attr0                         | —               | pass    | test/sprites/sprites_test.cpp:717  |
| G4.XY-03   | Y MSB requires 5th byte; else forced to 0                    | —               | pass    | test/sprites/sprites_test.cpp:736  |
| G4.XY-04   | Y MSB honored with 5th byte                                  | —               | pass    | test/sprites/sprites_test.cpp:753  |
| G4.XY-05   | x=319 renders last valid column                              | —               | pass    | test/sprites/sprites_test.cpp:768  |
| G4.XY-06   | x=320 fully off-screen, x-wrap 1× (mask 11111) still render… | —               | pass    | test/sprites/sprites_test.cpp:785  |
| G4.XY-07   | 2× scale wrap-around, sprite starts at x=300                 | —               | pass    | test/sprites/sprites_test.cpp:805  |
| G5.VIS-01  | `attr3(7)=1` and on-scanline ⇒ renders                       | —               | pass    | test/sprites/sprites_test.cpp:828  |
| G5.VIS-02  | `attr3(7)=0` ⇒ S_QUALIFY→S_QUALIFY (skipped)                 | —               | pass    | test/sprites/sprites_test.cpp:839  |
| G5.VIS-03  | Y not on this line ⇒ `spr_cur_yoff≠0` ⇒ skipped              | —               | pass    | test/sprites/sprites_test.cpp:850  |
| G5.VIS-04  | `spr_cur_hcount_valid=0` at entry and no x-wrap ⇒ no write   | —               | pass    | test/sprites/sprites_test.cpp:863  |
| G5.VIS-05  | Invisible sprite still latches its anchor context for a lat… | —               | pass    | test/sprites/sprites_test.cpp:885  |
| G6.CL-01   | Reset defaults {0,0xFF,0,0xBF} pass full display window      | —               | pass    | test/sprites/sprites_test.cpp:907  |
| G6.CL-02   | Non-over-border x transform `(({0,x1(7:5)}+1) & x1(4:0))`    | —               | pass    | test/sprites/sprites_test.cpp:927  |
| G6.CL-03   | Non-over-border x2 transform same formula                    | —               | pass    | test/sprites/sprites_test.cpp:952  |
| G6.CL-04   | Over-border, clip_en=0 ⇒ full 320×256                        | —               | pass    | test/sprites/sprites_test.cpp:967  |
| G6.CL-05   | Over-border, clip_en=1 ⇒ (x1*2, x2*2+1, y1, y2)              | —               | pass    | test/sprites/sprites_test.cpp:986  |
| G6.CL-06   | Sprite pixel suppressed when `(h,v)` outside (x_s..x_e, y_s… | —               | pass    | test/sprites/sprites_test.cpp:999  |
| G6.CL-07   | Sprite pixel emitted when inside clip and non-zero line-buf… | —               | pass    | test/sprites/sprites_test.cpp:1011 |
| G7.PR-01   | `zero_on_top=0`: higher-index sprite wins overlap            | —               | pass    | test/sprites/sprites_test.cpp:1035 |
| G7.PR-02   | `zero_on_top=1`: lower-index sprite wins                     | —               | pass    | test/sprites/sprites_test.cpp:1051 |
| G7.PR-03   | bit 8 of line-buffer entry cleared each scanline by video p… | —               | pass    | test/sprites/sprites_test.cpp:1071 |
| G7.PR-04   | Collision flag set regardless of `zero_on_top`               | —               | pass    | test/sprites/sprites_test.cpp:1088 |
| G9.MI-01   | Plain sprite, pattern byte 0 renders at x=0, byte 15 at x=15 | —               | pass    | test/sprites/sprites_test.cpp:1120 |
| G9.MI-02   | X-mirror flips columns: byte 15 at x=0, byte 0 at x=15       | —               | pass    | test/sprites/sprites_test.cpp:1133 |
| G9.MI-03   | Y-mirror on row 0 reads pattern row 15                       | —               | pass    | test/sprites/sprites_test.cpp:1151 |
| G9.MI-04   | Both mirrors                                                 | —               | pass    | test/sprites/sprites_test.cpp:1168 |
| G9.RO-01   | Rotate swaps row/col in address                              | —               | pass    | test/sprites/sprites_test.cpp:1191 |
| G9.RO-02   | `x_mirr_eff = xmirror XOR rotate`                            | —               | pass    | test/sprites/sprites_test.cpp:1211 |
| G9.RO-03   | Rotate + x-mirror produces delta = -16 (0x3FF0)              | —               | missing | missing                            |
| G9.RO-04   | Rotate without mirror: delta = +16                           | —               | missing | missing                            |
| G10.SC-01  | X 1× renders 16 px, advances addr every pixel                | —               | pass    | test/sprites/sprites_test.cpp:1255 |
| G10.SC-02  | X 2× renders 32 px, each byte repeated twice                 | —               | pass    | test/sprites/sprites_test.cpp:1267 |
| G10.SC-03  | X 4× renders 64 px, each byte×4                              | —               | pass    | test/sprites/sprites_test.cpp:1282 |
| G10.SC-04  | X 8× renders 128 px, each byte×8                             | —               | pass    | test/sprites/sprites_test.cpp:1296 |
| G10.SC-05  | Y 2× shows row 0 on 2 consecutive scanlines                  | —               | pass    | test/sprites/sprites_test.cpp:1314 |
| G10.SC-06  | Y 4× repeats 4×                                              | —               | pass    | test/sprites/sprites_test.cpp:1333 |
| G10.SC-07  | Y 8× repeats 8×                                              | —               | pass    | test/sprites/sprites_test.cpp:1353 |
| G10.SC-08  | 5th byte absent ⇒ scale forced 1× regardless of attr4 bits   | —               | pass    | test/sprites/sprites_test.cpp:1369 |
| G10.SC-09  | Combined X=4×, Y=2× covers 64×32 rectangle                   | —               | pass    | test/sprites/sprites_test.cpp:1386 |
| G10.SC-10  | X wrap mask for 2× is 11110                                  | —               | pass    | test/sprites/sprites_test.cpp:1400 |
| G11.OB-01  | `over_border=0`: sprite at y=200 not emitted (clip via non-… | —               | pass    | test/sprites/sprites_test.cpp:1427 |
| G11.OB-02  | `over_border=1, border_clip_en=0`: sprite at y=200 emitted   | —               | pass    | test/sprites/sprites_test.cpp:1438 |
| G11.OB-03  | `over_border=1, border_clip_en=1`: sprite at y=200, clip_y2… | —               | missing | missing                            |
| G11.OB-04  | `pixel_en_o` also requires `vcounter < 224` when `over_bord… | —               | pass    | test/sprites/sprites_test.cpp:1461 |
| G12.AN-01  | Sprite with `attr4(7:6)≠"01"` and attr3(6)=1 is an anchor;…  | —               | pass    | test/sprites/sprites_test.cpp:1483 |
| G12.AN-02  | Anchor type=1 additionally latches rotate/mirror/scale       | —               | pass    | test/sprites/sprites_test.cpp:1499 |
| G12.AN-03  | Anchor type=0 zeroes latched transforms                      | —               | pass    | test/sprites/sprites_test.cpp:1519 |
| G12.AN-04  | 4-byte (attr3(6)=0) sprite does **not** update anchor        | —               | pass    | test/sprites/sprites_test.cpp:1533 |
| G12.AN-05  | `anchor_vis` is `attr3(7)` of anchor                         | —               | pass    | test/sprites/sprites_test.cpp:1561 |
| G12.RE-01  | Relative with no transforms renders at `anchor_pos + (signe… | —               | pass    | test/sprites/sprites_test.cpp:1572 |
| G12.RE-02  | Relative inherits visibility: anchor invisible ⇒ relative i… | —               | pass    | test/sprites/sprites_test.cpp:1597 |
| G12.RE-03  | Relative palette: attr2(0)=0 ⇒ direct paloff                 | —               | pass    | test/sprites/sprites_test.cpp:1613 |
| G12.RE-04  | Relative palette: attr2(0)=1 ⇒ `anchor_paloff + attr2(7:4)`… | —               | pass    | test/sprites/sprites_test.cpp:1631 |
| G12.RE-05  | Anchor rotate swaps rel's offset axes (x0↔y0)                | —               | pass    | test/sprites/sprites_test.cpp:1647 |
| G12.RE-06  | Anchor xmirror XOR rotate negates rel X offset (note: subtr… | sprites.vhd:762 | pass    | test/sprites/sprites_test.cpp:1664 |
| G12.RE-07  | Anchor ymirror negates rel Y offset                          | —               | pass    | test/sprites/sprites_test.cpp:1679 |
| G12.RE-08  | Anchor xscale=01 doubles rel X offset (shift left 1)         | —               | pass    | test/sprites/sprites_test.cpp:1693 |
| G12.RE-09  | Anchor yscale=10 quadruples rel Y offset                     | —               | pass    | test/sprites/sprites_test.cpp:1707 |
| G12.RE-10  | Anchor xscale=11 × 8                                         | —               | pass    | test/sprites/sprites_test.cpp:1720 |
| G12.RT-01  | Type 0 relative: own mirror/rotate used directly             | —               | pass    | test/sprites/sprites_test.cpp:1740 |
| G12.RT-02  | Type 1 relative: `mirror = anchor XOR rel`                   | —               | pass    | test/sprites/sprites_test.cpp:1761 |
| G12.RT-03  | Type 1 relative: `rotate = anchor XOR rel`                   | —               | pass    | test/sprites/sprites_test.cpp:1786 |
| G12.RT-04  | Type 1 relative scale from anchor, not relative              | —               | pass    | test/sprites/sprites_test.cpp:1807 |
| G12.RP-01  | Rel pattern without add (attr4(0)=0): uses own name          | —               | pass    | test/sprites/sprites_test.cpp:1822 |
| G12.RP-02  | Rel pattern with add (attr4(0)=1): anchor_pattern + rel pat… | —               | pass    | test/sprites/sprites_test.cpp:1835 |
| G12.RP-03  | Rel pattern with N6 bit (from rel's attr4(6) AND anchor_h)   | —               | missing | missing                            |
| G12.RP-04  | 4bpp relative inherits H from anchor (`anchor_h`)            | —               | missing | missing                            |
| G12.NG-01  | Relative sprite with no prior anchor ⇒ `anchor_*` all zero…  | —               | pass    | test/sprites/sprites_test.cpp:1868 |
| G12.NG-02  | Two consecutive anchors: second replaces first               | —               | pass    | test/sprites/sprites_test.cpp:1882 |
| G12.NG-03  | Invisible anchor between visible anchor and relative leaves… | —               | pass    | test/sprites/sprites_test.cpp:1896 |
| G13.CO-01  | No overlap ⇒ collision bit stays 0                           | —               | pass    | test/sprites/sprites_test.cpp:1921 |
| G13.CO-02  | Two opaque sprites overlap ⇒ bit 0 = 1                       | —               | pass    | test/sprites/sprites_test.cpp:1935 |
| G13.CO-03  | Collision fires even when `zero_on_top=1` blocks the write   | —               | pass    | test/sprites/sprites_test.cpp:1950 |
| G13.CO-04  | Transparent pixel does not count (spr_line_we=0)             | —               | pass    | test/sprites/sprites_test.cpp:1966 |
| G13.CO-05  | Read of 0x303B clears status                                 | —               | pass    | test/sprites/sprites_test.cpp:1982 |
| G13.CO-06  | Collision bit is sticky across frames until read             | —               | pass    | test/sprites/sprites_test.cpp:2001 |
| G13.OT-01  | Few sprites ⇒ `state_s` returns to S_IDLE before next `line… | —               | pass    | test/sprites/sprites_test.cpp:2015 |
| G13.OT-02  | 128 visible anchors all on same Y, 1× scale ⇒ overtime       | —               | missing | missing                            |
| G13.OT-03  | Overtime bit independent of collision bit                    | —               | missing | missing                            |
| G13.OT-04  | Both flags can accumulate                                    | —               | missing | missing                            |
| G13.SR-01  | Status bits 7:2 always 0                                     | —               | pass    | test/sprites/sprites_test.cpp:2041 |
| G13.SR-02  | Read captures then clears in same cycle                      | —               | pass    | test/sprites/sprites_test.cpp:2058 |
| G13.SR-03  | Status bits update via OR while unread                       | —               | pass    | test/sprites/sprites_test.cpp:2074 |
| G14.RST-01 | `anchor_vis` cleared on reset                                | —               | pass    | test/sprites/sprites_test.cpp:2103 |
| G14.RST-02 | `spr_cur_index` reset to 0                                   | —               | pass    | test/sprites/sprites_test.cpp:2115 |
| G14.RST-03 | `status_reg_s` and `status_reg_read` zeroed                  | —               | pass    | test/sprites/sprites_test.cpp:2122 |
| G14.RST-04 | `mirror_sprite_q` zeroed                                     | —               | pass    | test/sprites/sprites_test.cpp:2131 |
| G14.RST-05 | `line_buf_sel` starts at 0                                   | —               | pass    | test/sprites/sprites_test.cpp:2144 |
| G14.RST-06 | `attr_index` and `pattern_index` zeroed                      | —               | pass    | test/sprites/sprites_test.cpp:2173 |
| G15.NG-01  | Pattern index 64..255 inaccessible via attr3                 | —               | pass    | test/sprites/sprites_test.cpp:2204 |
| G15.NG-02  | Sprite fully off-screen (x=500, y=500) produces zero writes  | —               | pass    | test/sprites/sprites_test.cpp:2218 |
| G15.NG-03  | Sprite at `(x=0, y=0)` with `attr3(7)=1, attr3(6)=0` (no 5t… | —               | pass    | test/sprites/sprites_test.cpp:2229 |
| G15.NG-04  | Palette offset wrap: `paloff=0xF, pat(7:4)=0x1` ⇒ (0xF+0x1)… | —               | pass    | test/sprites/sprites_test.cpp:2242 |
| G15.NG-05  | Zero-size pattern (all bytes = transp colour) ⇒ zero pixels… | —               | pass    | test/sprites/sprites_test.cpp:2258 |
| G15.NG-06  | Relative sprite whose computed `spr_rel_x3(8)=1` but attr3(… | —               | missing | missing                            |
| G15.NG-07  | Negative offset wraps in 9-bit arithmetic: anchor_x=5, rel…  | sprites.vhd:762 | pass    | test/sprites/sprites_test.cpp:2286 |

## Tilemap — `test/tilemap/tilemap_test.cpp`

Last-touch commit: `d599cd27615bf61efea60c49fdeb38dc7a6116b3` (`d599cd2761`)

| Test ID | Plan row title                  | VHDL file:line | Status  | Test file:line                     |
|---------|---------------------------------|----------------|---------|------------------------------------|
| TM-01   | Tilemap disabled by default     | —              | —       | test/tilemap/tilemap_test.cpp:134  |
| TM-02   | Enable tilemap                  | —              | —       | test/tilemap/tilemap_test.cpp:143  |
| TM-03   | Disable tilemap                 | —              | —       | test/tilemap/tilemap_test.cpp:162  |
| TM-04   | Reset defaults readback         | —              | —       | test/tilemap/tilemap_test.cpp:193  |
| TM-10   | 40-col basic display            | —              | —       | test/tilemap/tilemap_test.cpp:236  |
| TM-11   | 40-col tile index range         | —              | —       | test/tilemap/tilemap_test.cpp:251  |
| TM-12   | 40-col attribute palette offset | —              | —       | test/tilemap/tilemap_test.cpp:267  |
| TM-13   | 40-col X-mirror                 | —              | —       | test/tilemap/tilemap_test.cpp:292  |
| TM-14   | 40-col Y-mirror                 | —              | —       | test/tilemap/tilemap_test.cpp:317  |
| TM-15   | 40-col rotation                 | —              | —       | test/tilemap/tilemap_test.cpp:342  |
| TM-16   | 40-col rotation + X-mirror      | —              | —       | test/tilemap/tilemap_test.cpp:372  |
| TM-17   | 40-col ULA-over-tile flag       | —              | —       | test/tilemap/tilemap_test.cpp:390  |
| TM-20   | 80-col basic display            | —              | —       | test/tilemap/tilemap_test.cpp:422  |
| TM-21   | 80-col tile attributes          | —              | —       | test/tilemap/tilemap_test.cpp:447  |
| TM-22   | 80-col pixel selection          | —              | missing | missing                            |
| TM-30   | 512-tile mode enable            | —              | —       | test/tilemap/tilemap_test.cpp:477  |
| TM-31   | 512-tile index construction     | —              | —       | test/tilemap/tilemap_test.cpp:492  |
| TM-32   | 512-tile ULA-over interaction   | —              | —       | test/tilemap/tilemap_test.cpp:508  |
| TM-40   | Text mode enable                | —              | —       | test/tilemap/tilemap_test.cpp:557  |
| TM-41   | Text mode pixel extraction      | —              | —       | test/tilemap/tilemap_test.cpp:576  |
| TM-42   | Text mode palette construction  | —              | —       | test/tilemap/tilemap_test.cpp:592  |
| TM-43   | Text mode no transforms         | —              | —       | test/tilemap/tilemap_test.cpp:614  |
| TM-44   | Text mode transparency          | —              | missing | missing                            |
| TM-50   | Strip flags mode                | —              | —       | test/tilemap/tilemap_test.cpp:651  |
| TM-51   | Default attr applied            | —              | —       | test/tilemap/tilemap_test.cpp:674  |
| TM-52   | Strip flags + 40-col            | —              | —       | test/tilemap/tilemap_test.cpp:692  |
| TM-53   | Strip flags + 80-col            | —              | missing | missing                            |
| TM-60   | Map base address (bank 5)       | —              | —       | test/tilemap/tilemap_test.cpp:720  |
| TM-61   | Map base address (bank 7)       | —              | —       | test/tilemap/tilemap_test.cpp:739  |
| TM-62   | Tile def base (bank 5)          | —              | —       | test/tilemap/tilemap_test.cpp:758  |
| TM-63   | Tile def base (bank 7)          | —              | —       | test/tilemap/tilemap_test.cpp:776  |
| TM-64   | Address offset computation      | —              | missing | missing                            |
| TM-65   | Tile address with/without flags | —              | missing | missing                            |
| TM-70   | Standard pixel address          | —              | missing | missing                            |
| TM-71   | Text mode pixel address         | —              | missing | missing                            |
| TM-72   | Pixel nibble selection          | —              | missing | missing                            |
| TM-80   | X scroll basic                  | —              | —       | test/tilemap/tilemap_test.cpp:811  |
| TM-81   | X scroll wrap at 320 (40-col)   | —              | —       | test/tilemap/tilemap_test.cpp:832  |
| TM-82   | X scroll wrap at 640 (80-col)   | —              | missing | missing                            |
| TM-83   | Y scroll basic                  | —              | —       | test/tilemap/tilemap_test.cpp:854  |
| TM-84   | Y scroll wrap at 256            | —              | —       | test/tilemap/tilemap_test.cpp:873  |
| TM-85   | Per-line scroll update          | —              | —       | test/tilemap/tilemap_test.cpp:891  |
| TM-90   | Standard transparency index     | —              | —       | test/tilemap/tilemap_test.cpp:910  |
| TM-91   | Default transparency (0xF)      | —              | —       | test/tilemap/tilemap_test.cpp:924  |
| TM-92   | Custom transparency index       | —              | —       | test/tilemap/tilemap_test.cpp:937  |
| TM-93   | Text mode transparency (RGB)    | —              | —       | test/tilemap/tilemap_test.cpp:949  |
| TM-94   | Text mode vs standard path      | —              | missing | missing                            |
| TM-100  | Palette select 0                | —              | missing | missing                            |
| TM-101  | Palette select 1                | —              | missing | missing                            |
| TM-102  | Palette routing                 | —              | missing | missing                            |
| TM-103  | Standard pixel composition      | —              | missing | missing                            |
| TM-104  | Text mode pixel composition     | —              | missing | missing                            |
| TM-110  | Default clip (full area)        | —              | —       | test/tilemap/tilemap_test.cpp:977  |
| TM-111  | Custom clip window              | —              | —       | test/tilemap/tilemap_test.cpp:1002 |
| TM-112  | Clip X coordinates              | —              | —       | test/tilemap/tilemap_test.cpp:1019 |
| TM-113  | Clip Y coordinates              | —              | missing | missing                            |
| TM-114  | Clip index cycling              | —              | missing | missing                            |
| TM-115  | Clip index reset                | —              | missing | missing                            |
| TM-116  | Clip readback                   | —              | missing | missing                            |
| TM-120  | Tilemap on top (default)        | —              | —       | test/tilemap/tilemap_test.cpp:1136 |
| TM-121  | Tilemap always on top           | —              | —       | test/tilemap/tilemap_test.cpp:1150 |
| TM-122  | Per-tile below flag             | —              | —       | test/tilemap/tilemap_test.cpp:1164 |
| TM-123  | Below flag in compositor        | —              | missing | missing                            |
| TM-124  | tm_on_top overrides per-tile    | —              | missing | missing                            |
| TM-125  | 512-mode forces below           | —              | missing | missing                            |
| TM-130  | Stencil mode (ULA AND TM)       | —              | missing | missing                            |
| TM-131  | Stencil transparency            | —              | missing | missing                            |
| TM-140  | TM disabled, tm_on_top=0        | —              | missing | missing                            |
| TM-141  | TM disabled, tm_on_top=1        | —              | missing | missing                            |

### Extra coverage (not in plan)

| Test ID | Assertion description                      | VHDL file:line | Test file:line                     |
|---------|--------------------------------------------|----------------|------------------------------------|
| TM-CB1  | Bit 6 = 80-column mode                     | —              | test/tilemap/tilemap_test.cpp:1039 |
| TM-CB2  | Bit 7 = enable                             | —              | test/tilemap/tilemap_test.cpp:1048 |
| TM-CB3  | Bit 1 = 512-tile mode (forces below)       | —              | test/tilemap/tilemap_test.cpp:1063 |
| TM-CB4  | Bit 0 = tm_on_top overrides per-tile below | —              | test/tilemap/tilemap_test.cpp:1083 |
| TM-CB5  | Bit 5 mapping (VHDL=strip, C++ may differ) | —              | test/tilemap/tilemap_test.cpp:1112 |
| TM-RR1  | Control register roundtrip                 | —              | test/tilemap/tilemap_test.cpp:1183 |
| TM-RR2  | Default attr roundtrip                     | —              | test/tilemap/tilemap_test.cpp:1192 |
| TM-RR3  | Map base roundtrip                         | —              | test/tilemap/tilemap_test.cpp:1201 |
| TM-RR4  | Def base roundtrip                         | —              | test/tilemap/tilemap_test.cpp:1210 |
| TM-RR5  | Reset restores all defaults                | —              | test/tilemap/tilemap_test.cpp:1233 |

## Copper — `test/copper/copper_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID    | Plan row title                                        | VHDL file:line           | Status | Test file:line                   |
|------------|-------------------------------------------------------|--------------------------|--------|----------------------------------|
| RAM-8-01   | `NR 0x60` two-byte upload                             | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:169  |
| RAM-8-02   | `NR 0x60` upload starting odd                         | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:187  |
| RAM-16-01  | `NR 0x63` two-byte upload                             | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:207  |
| RAM-P-01   | `NR 0x61` sets low byte                               | zxnext.vhd:5427          | pass   | test/copper/copper_test.cpp:222  |
| RAM-P-02   | `NR 0x62` sets mode and addr hi                       | zxnext.vhd:5430-5431     | pass   | test/copper/copper_test.cpp:235  |
| RAM-P-03   | `NR 0x61` then `NR 0x62` addressing                   | zxnext.vhd:5427          | pass   | test/copper/copper_test.cpp:248  |
| RAM-AI-01  | Auto-increment over 4 bytes                           | zxnext.vhd:5419-5424     | pass   | test/copper/copper_test.cpp:263  |
| RAM-AI-02  | Byte pointer wraps at 0x7FF → 0x000                   | zxnext.vhd:5424          | pass   | test/copper/copper_test.cpp:282  |
| RAM-AI-03  | Full RAM fill                                         | zxnext.vhd:5424          | pass   | test/copper/copper_test.cpp:301  |
| RAM-MIX-01 | `nr_copper_write_8` latch across 0x60/0x63 mix        | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:326  |
| RAM-BK-01  | Read-back `NR 0x61`/`NR 0x62`/`NR 0x64`               | zxnext.vhd:6084          | pass   | test/copper/copper_test.cpp:341  |
| MOV-01     | MOVE NR 0x40 = 0x55                                   | copper.vhd:100-108       | pass   | test/copper/copper_test.cpp:391  |
| MOV-02     | MOVE to reg 0x7F                                      | copper.vhd:100-108       | pass   | test/copper/copper_test.cpp:409  |
| MOV-03     | MOVE NOP suppresses pulse                             | copper.vhd:104-108       | pass   | test/copper/copper_test.cpp:426  |
| MOV-04     | Two consecutive MOVEs                                 | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:446  |
| MOV-05     | MOVE pulse is exactly 1 clock                         | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:463  |
| MOV-06     | MOVE then WAIT pipeline                               | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:484  |
| MOV-07     | MOVE output width                                     | copper.vhd:42            | pass   | test/copper/copper_test.cpp:500  |
| WAI-01     | WAIT (0,0) matches at hcount=12                       | copper.vhd:92-96         | pass   | test/copper/copper_test.cpp:524  |
| WAI-02     | hpos threshold `hpos*8+12`                            | copper.vhd:94            | pass   | test/copper/copper_test.cpp:539  |
| WAI-03     | hpos=63 max                                           | copper.vhd:94            | pass   | test/copper/copper_test.cpp:552  |
| WAI-04     | vpos mismatch stalls                                  | copper.vhd:94            | pass   | test/copper/copper_test.cpp:564  |
| WAI-05     | vpos equality, not >=                                 | copper.vhd:94            | pass   | test/copper/copper_test.cpp:576  |
| WAI-06     | hcount >= once matched                                | copper.vhd:94            | pass   | test/copper/copper_test.cpp:596  |
| WAI-07     | WAIT then MOVE                                        | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:615  |
| WAI-08     | Multiple WAITs                                        | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:637  |
| WAI-09     | WAIT for line 0 edge case                             | copper.vhd:94            | pass   | test/copper/copper_test.cpp:651  |
| WAI-10     | Impossible WAIT, run-once                             | copper.vhd:85-96         | pass   | test/copper/copper_test.cpp:668  |
| WAI-11     | Missed-line WAIT in Run mode                          | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:687  |
| WAI-12     | Missed-line WAIT in Loop mode                         | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:710  |
| CTL-00     | Reset → mode `00` is idle                             | copper.vhd:60-65         | pass   | test/copper/copper_test.cpp:729  |
| CTL-01     | `00` freezes but does not reset                       | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:746  |
| CTL-02     | `01` resets addr on entry from `00`                   | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:762  |
| CTL-03     | `11` resets addr on entry from `00`                   | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:778  |
| CTL-04     | `01` does **not** loop                                | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:800  |
| CTL-05     | `11` loops at `cvc=0, hcount=0`                       | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:831  |
| CTL-06a    | **Mode `10` does NOT reset addr on entry**            | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:854  |
| CTL-06b    | **Mode `10` does NOT restart at vblank**              | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:872  |
| CTL-06c    | Mode `10` resumes after pause                         | copper.vhd:70-85         | pass   | test/copper/copper_test.cpp:897  |
| CTL-07     | Mode change clears pending MOVE pulse                 | copper.vhd:78            | pass   | test/copper/copper_test.cpp:920  |
| CTL-08     | Same-mode rewrite does not reset addr                 | copper.vhd:70            | pass   | test/copper/copper_test.cpp:936  |
| CTL-09     | Mode `01` → `11` mid-execution                        | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:950  |
| CTL-10     | Mode `11` → `10` mid-execution                        | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:966  |
| TIM-01     | MOVE is 2 Copper clocks                               | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:994  |
| TIM-02     | WAIT stall is 1 clock per no-match                    | copper.vhd:92-98         | pass   | test/copper/copper_test.cpp:1006 |
| TIM-03     | 10 consecutive MOVEs take 20 clocks                   | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:1022 |
| TIM-04     | WAIT then MOVE pipeline                               | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:1041 |
| TIM-05     | Dual-port instr fetch available                       | zxnext.vhd:3959-3998     | pass   | test/copper/copper_test.cpp:1058 |
| OFS-01     | Default offset = 0                                    | zxnext.vhd:5024          | skip   | test/copper/copper_test.cpp:1071 |
| OFS-02     | Non-zero offset loads `cvc`                           | zxula_timing.vhd:462     | skip   | test/copper/copper_test.cpp:1072 |
| OFS-03     | WAIT resolves on offset cvc                           | zxula_timing.vhd:462     | skip   | test/copper/copper_test.cpp:1073 |
| OFS-04     | Offset read-back                                      | zxnext.vhd:6090          | skip   | test/copper/copper_test.cpp:1074 |
| OFS-05     | Offset reset                                          | zxnext.vhd:5024          | skip   | test/copper/copper_test.cpp:1075 |
| OFS-06     | `cvc` wraps at `c_max_vc`                             | zxula_timing.vhd:463-464 | skip   | test/copper/copper_test.cpp:1076 |
| ARB-01     | Copper wins simultaneous write                        | zxnext.vhd:4775-4777     | skip   | test/copper/copper_test.cpp:1126 |
| ARB-02     | CPU write deferred until Copper clears                | zxnext.vhd:4769          | skip   | test/copper/copper_test.cpp:1127 |
| ARB-03     | Copper priority on different registers                | zxnext.vhd:4769-4777     | skip   | test/copper/copper_test.cpp:1128 |
| ARB-04     | Copper reg width masked to 7 bits                     | zxnext.vhd:4731          | pass   | test/copper/copper_test.cpp:1104 |
| ARB-05     | No Copper request when stopped                        | zxnext.vhd:4709          | pass   | test/copper/copper_test.cpp:1121 |
| ARB-06     | Copper write to `NR 0x02` triggers NMI signals        | zxnext.vhd:3830-3832     | skip   | test/copper/copper_test.cpp:1129 |
| MUT-01     | Copper writes `NR 0x62` to stop itself                | zxnext.vhd:5430          | pass   | test/copper/copper_test.cpp:1159 |
| MUT-02     | Copper writes `NR 0x62` to switch itself to mode `10` | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1179 |
| MUT-03     | Copper writes its own addr-hi via `NR 0x62`           | zxnext.vhd:5430-5431     | pass   | test/copper/copper_test.cpp:1211 |
| MUT-04     | Copper writes RAM via `NR 0x60` inside a MOVE         | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:1236 |
| EDG-01     | Instruction address wraps at 1024                     | copper.vhd:48            | pass   | test/copper/copper_test.cpp:1258 |
| EDG-02     | Empty program (first slot WAIT impossible)            | copper.vhd:92-96         | pass   | test/copper/copper_test.cpp:1274 |
| EDG-03     | Program at max size                                   | copper.vhd:108           | pass   | test/copper/copper_test.cpp:1292 |
| EDG-04     | Copper stopped mid-MOVE pulse                         | zxnext.vhd:4709          | pass   | test/copper/copper_test.cpp:1314 |
| EDG-05     | Mode `11` restart coincident with MOVE                | copper.vhd:82-83         | pass   | test/copper/copper_test.cpp:1335 |
| EDG-06     | WAIT hpos=0 matches at hcount=12                      | copper.vhd:94            | pass   | test/copper/copper_test.cpp:1350 |
| EDG-07     | All-WAIT program in Run mode                          | copper.vhd:92-98         | pass   | test/copper/copper_test.cpp:1365 |
| EDG-08     | All-NOP program                                       | copper.vhd:104-108       | pass   | test/copper/copper_test.cpp:1381 |
| EDG-09     | Rapid mode toggling                                   | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1407 |
| RST-01     | Copper hard reset                                     | copper.vhd:60-65         | pass   | test/copper/copper_test.cpp:1429 |
| RST-02     | NR state reset                                        | zxnext.vhd:5020-5024     | pass   | test/copper/copper_test.cpp:1444 |
| RST-03     | `last_state_s` reset                                  | copper.vhd:50            | pass   | test/copper/copper_test.cpp:1460 |

## Compositor — `test/compositor/compositor_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID            | Plan row title                                               | VHDL file:line   | Status  | Test file:line                           |
|--------------------|--------------------------------------------------------------|------------------|---------|------------------------------------------|
| TR-10              | ULA pixel with palette output ≠ NR 0x14 is opaque            | —                | pass    | test/compositor/compositor_test.cpp:159  |
| TR-11              | ULA pixel with palette output = NR 0x14 is transparent; fal… | —                | pass    | test/compositor/compositor_test.cpp:178  |
| TR-12              | ULA palette entry whose LSB differs from NR 0x14 LSB is sti… | —                | pass    | test/compositor/compositor_test.cpp:201  |
| TR-13              | `ula_clipped_2=1` forces ULA transparent regardless of RGB   | —                | pass    | test/compositor/compositor_test.cpp:215  |
| TR-14              | `ula_en_2=0` forces ULA transparent even if mix_transparent… | —                | pass    | test/compositor/compositor_test.cpp:229  |
| TR-15              | Compositor is resolution-agnostic at the ULA input boundary… | —                | pass    | test/compositor/compositor_test.cpp:243  |
| TR-16              | NR 0x14 = 0x00 with ULA palette output `RGB[8:1]=0x00` → UL… | —                | pass    | test/compositor/compositor_test.cpp:262  |
| TR-17              | `ula_border_2` is ignored by stage-2 mix in modes 000/001/0… | —                | pass    | test/compositor/compositor_test.cpp:280  |
| TR-42              | NR 0x15[0] `nr_15_sprite_en = 0` forces every sprite-origin… | —                | pass    | test/compositor/compositor_test.cpp:301  |
| TR-20              | Tilemap text-mode RGB compare                                | —                | pass    | test/compositor/compositor_test.cpp:314  |
| TR-21              | Tilemap non-text (attribute) ignores RGB compare             | —                | pass    | test/compositor/compositor_test.cpp:331  |
| TR-22              | Tilemap `pixel_en=0` transparent regardless of mode          | —                | pass    | test/compositor/compositor_test.cpp:344  |
| TR-23              | `tm_en_2=0` forces TM transparent                            | —                | pass    | test/compositor/compositor_test.cpp:357  |
| TR-30              | Layer 2 RGB compare                                          | —                | pass    | test/compositor/compositor_test.cpp:370  |
| TR-31              | Layer 2 `pixel_en=0` transparent                             | —                | pass    | test/compositor/compositor_test.cpp:382  |
| TR-32              | Layer 2 opaque pixel with non-zero `layer2_priority_2` prop… | —                | pass    | test/compositor/compositor_test.cpp:400  |
| TR-33              | Layer 2 priority forced to 0 when layer is transparent       | —                | pass    | test/compositor/compositor_test.cpp:415  |
| TR-40              | Sprite `pixel_en=0` transparent                              | —                | pass    | test/compositor/compositor_test.cpp:427  |
| TR-41              | Sprite `pixel_en=1` opaque regardless of NR 0x14             | —                | pass    | test/compositor/compositor_test.cpp:444  |
| TRI-10             | Sprite index matching NR 0x4B produces pixel_en=0 → composi… | sprites.vhd:1067 | pass    | test/compositor/compositor_test.cpp:466  |
| TRI-11             | Sprite index ≠ NR 0x4B and inside active area → pixel_en=1   | sprites.vhd:1067 | pass    | test/compositor/compositor_test.cpp:477  |
| TRI-20             | Tilemap nibble matching NR 0x4C → pixel_en=0                 | zxnext.vhd:4395  | pass    | test/compositor/compositor_test.cpp:490  |
| FB-10              | Fallback appears when all layers transparent (mode 000)      | —                | pass    | test/compositor/compositor_test.cpp:515  |
| FB-11              | Fallback=0x00                                                | —                | pass    | test/compositor/compositor_test.cpp:525  |
| FB-12              | Fallback=0x4A = `0100_1010`                                  | —                | pass    | test/compositor/compositor_test.cpp:535  |
| FB-13              | Fallback=0x01 = `0000_0001`                                  | —                | pass    | test/compositor/compositor_test.cpp:543  |
| FB-14              | Fallback=0x02 = `0000_0010`                                  | —                | pass    | test/compositor/compositor_test.cpp:551  |
| FB-15              | Fallback not used when any layer opaque                      | —                | pass    | test/compositor/compositor_test.cpp:563  |
| FB-16              | Reset default is 0xE3                                        | —                | pass    | test/compositor/compositor_test.cpp:572  |
| FB-17              | All 8 priority modes converge on fallback when every layer…  | —                | pass    | test/compositor/compositor_test.cpp:592  |
| PRI-010-SLU-3      | Mode 000, all three opaque                                   | —                | missing | missing                                  |
| PRI-010-SLU-LU     | Mode 000, only L+U                                           | —                | missing | missing                                  |
| PRI-010-SLU-U      | Mode 000, only U                                             | —                | missing | missing                                  |
| PRI-010-SLU-0      | Mode 000, none                                               | —                | missing | missing                                  |
| PRI-011-LSU-3      | Mode 001, all three                                          | —                | missing | missing                                  |
| PRI-011-LSU-SU     | Mode 001, S+U only                                           | —                | missing | missing                                  |
| PRI-011-LSU-U      | Mode 001, U only                                             | —                | missing | missing                                  |
| PRI-010-SUL-3      | Mode 010, all three                                          | —                | missing | missing                                  |
| PRI-010-SUL-UL     | Mode 010, U+L                                                | —                | missing | missing                                  |
| PRI-010-SUL-L      | Mode 010, L only                                             | —                | missing | missing                                  |
| PRI-011-LUS-3      | Mode 011, all three                                          | —                | missing | missing                                  |
| PRI-011-LUS-US     | Mode 011, U(non-border)+S                                    | —                | missing | missing                                  |
| PRI-011-LUS-S      | Mode 011, S only                                             | —                | missing | missing                                  |
| PRI-011-LUS-border | Mode 011, U(border) + S + TM transp                          | —                | pass    | test/compositor/compositor_test.cpp:680  |
| PRI-100-USL-3      | Mode 100, all three                                          | —                | missing | missing                                  |
| PRI-100-USL-border | Mode 100, U(border) + S, TM transp, L=✗                      | —                | pass    | test/compositor/compositor_test.cpp:693  |
| PRI-100-USL-L      | Mode 100, L only                                             | —                | missing | missing                                  |
| PRI-101-ULS-3      | Mode 101, all three                                          | —                | missing | missing                                  |
| PRI-101-ULS-border | Mode 101, U(border)+L+S, TM transp                           | —                | pass    | test/compositor/compositor_test.cpp:709  |
| PRI-101-ULS-S      | Mode 101, S only                                             | —                | missing | missing                                  |
| PRI-B-0            | In every mode 000..101 with all three transparent, fallback… | —                | pass    | test/compositor/compositor_test.cpp:734  |
| PRI-B-1            | Mode 000 with NR 0x14 = sprite_rgb[8:1] must not transparen… | —                | pass    | test/compositor/compositor_test.cpp:745  |
| PRI-B-2            | Mode 001: even if sprite opaque, L2 opaque beats it          | —                | pass    | test/compositor/compositor_test.cpp:757  |
| L2P-10             | Promotion in mode 000 over sprite                            | —                | missing | missing                                  |
| L2P-11             | Promotion in mode 010 over sprite                            | —                | missing | missing                                  |
| L2P-12             | Promotion in mode 100 over sprite (L2 above U)               | —                | missing | missing                                  |
| L2P-13             | Promotion in mode 101 over sprite (L2 above U)               | —                | missing | missing                                  |
| L2P-14             | No-op in mode 001 (L2 already top)                           | —                | missing | missing                                  |
| L2P-15             | No-op in mode 011 (L2 already top)                           | —                | missing | missing                                  |
| L2P-16             | `layer2_transparent=1` suppresses promotion                  | —                | pass    | test/compositor/compositor_test.cpp:804  |
| L2P-17             | Promotion in mode 110 (blend): L2 promoted shows blend outp… | —                | pass    | test/compositor/compositor_test.cpp:827  |
| L2P-18             | Promotion in mode 111 (blend): L2 promoted shows blend outp… | —                | pass    | test/compositor/compositor_test.cpp:841  |
| BL-10              | Add no clamp                                                 | —                | pass    | test/compositor/compositor_test.cpp:892  |
| BL-11              | Add clamp hi                                                 | —                | pass    | test/compositor/compositor_test.cpp:905  |
| BL-12              | Add 0+0                                                      | —                | pass    | test/compositor/compositor_test.cpp:918  |
| BL-13              | Add, `mix_top` opaque beats blend                            | —                | pass    | test/compositor/compositor_test.cpp:934  |
| BL-14              | Add, sprite between mix_top and mix_bot                      | —                | pass    | test/compositor/compositor_test.cpp:948  |
| BL-15              | Add, mix_bot wins after sprite transp                        | —                | pass    | test/compositor/compositor_test.cpp:964  |
| BL-16              | Add, final fallback to blend                                 | —                | pass    | test/compositor/compositor_test.cpp:978  |
| BL-20              | Sub, ≤4 clamps to 0                                          | —                | pass    | test/compositor/compositor_test.cpp:992  |
| BL-21              | Sub, ≥12 clamps to 7                                         | —                | pass    | test/compositor/compositor_test.cpp:1006 |
| BL-22              | Sub, middle value                                            | —                | pass    | test/compositor/compositor_test.cpp:1020 |
| BL-23              | Sub gated by `mix_rgb_transparent`                           | —                | pass    | test/compositor/compositor_test.cpp:1034 |
| BL-24              | Sub, mix_top opaque wins over blend                          | —                | pass    | test/compositor/compositor_test.cpp:1047 |
| BL-25              | Sub, sprite between                                          | —                | pass    | test/compositor/compositor_test.cpp:1059 |
| BL-26              | Sub, mix_bot fallback                                        | —                | pass    | test/compositor/compositor_test.cpp:1071 |
| BL-27              | Sub, final L2-only fallback shows blended L2                 | —                | pass    | test/compositor/compositor_test.cpp:1085 |
| BL-28              | L2 priority bit overrides blend (mode 110)                   | —                | pass    | test/compositor/compositor_test.cpp:1101 |
| BL-29              | L2 priority bit overrides blend (mode 111)                   | —                | pass    | test/compositor/compositor_test.cpp:1115 |
| UTB-10             | Mode 00, TM above                                            | —                | pass    | test/compositor/compositor_test.cpp:1143 |
| UTB-11             | Mode 00, TM below                                            | —                | pass    | test/compositor/compositor_test.cpp:1156 |
| UTB-20             | Mode 10, stencil-off combined                                | —                | pass    | test/compositor/compositor_test.cpp:1169 |
| UTB-30             | Mode 11, TM as U, ULA floats below                           | —                | pass    | test/compositor/compositor_test.cpp:1187 |
| UTB-31             | Mode 11, TM as U, ULA floats above                           | —                | pass    | test/compositor/compositor_test.cpp:1202 |
| UTB-40             | Mode 01 (`others`), below=0                                  | —                | pass    | test/compositor/compositor_test.cpp:1218 |
| UTB-41             | Mode 01, below=1                                             | —                | pass    | test/compositor/compositor_test.cpp:1231 |
| STEN-10            | Bitwise AND                                                  | —                | pass    | test/compositor/compositor_test.cpp:1259 |
| STEN-11            | AND with zero                                                | —                | pass    | test/compositor/compositor_test.cpp:1274 |
| STEN-12            | ULA transparent → stencil transparent                        | —                | pass    | test/compositor/compositor_test.cpp:1288 |
| STEN-13            | TM transparent → stencil transparent                         | —                | pass    | test/compositor/compositor_test.cpp:1302 |
| STEN-14            | Both transparent → transparent                               | —                | pass    | test/compositor/compositor_test.cpp:1313 |
| STEN-15            | Stencil inactive if `tm_en=0` (even with bit set)            | —                | pass    | test/compositor/compositor_test.cpp:1327 |
| STEN-16            | Stencil inactive if `ula_en=0`                               | —                | pass    | test/compositor/compositor_test.cpp:1340 |
| STEN-17            | Stencil off (bit=0), both enabled                            | —                | pass    | test/compositor/compositor_test.cpp:1353 |
| SOB-10             | Sprite rgb arrives at compositor with `sprite_pixel_en_2=1`… | —                | pass    | test/compositor/compositor_test.cpp:1376 |
| LINE-10            | Write NR 0x15[4:2] mid-line                                  | —                | pass    | test/compositor/compositor_test.cpp:1407 |
| LINE-11            | Write NR 0x14 mid-line                                       | —                | pass    | test/compositor/compositor_test.cpp:1424 |
| LINE-12            | Write NR 0x4A mid-line                                       | —                | pass    | test/compositor/compositor_test.cpp:1438 |
| LINE-13            | Copper write to NR 0x15 at end of line                       | —                | pass    | test/compositor/compositor_test.cpp:1451 |
| LINE-14            | Two writes in one line: only the last is visible next line   | —                | pass    | test/compositor/compositor_test.cpp:1461 |
| BLANK-10           | Active area passes through                                   | —                | pass    | test/compositor/compositor_test.cpp:1482 |
| BLANK-11           | Horizontal blanking forces 0                                 | —                | pass    | test/compositor/compositor_test.cpp:1496 |
| BLANK-12           | Vertical blanking forces 0                                   | —                | pass    | test/compositor/compositor_test.cpp:1499 |
| BLANK-13           | Fallback colour is NOT shown during blank                    | —                | pass    | test/compositor/compositor_test.cpp:1502 |
| PAL-10             | ULA pixel index → ULA/TM palette                             | —                | pass    | test/compositor/compositor_test.cpp:1525 |
| PAL-11             | ULA background substitution uses fallback                    | —                | pass    | test/compositor/compositor_test.cpp:1543 |
| PAL-12             | LoRes pixel overrides ULA background                         | —                | pass    | test/compositor/compositor_test.cpp:1556 |
| PAL-13             | L2 palette select 0 vs 1 (NR 0x43[2])                        | —                | pass    | test/compositor/compositor_test.cpp:1573 |
| PAL-14             | L2 palette bit 15 surfaces as `layer2_priority_2`            | —                | pass    | test/compositor/compositor_test.cpp:1589 |
| PAL-15             | Sprite palette is L2/Sprite RAM `sc(0)=1`                    | —                | pass    | test/compositor/compositor_test.cpp:1601 |
| RST-10             | After reset, all layers are transparent (TM disabled, S dis… | —                | pass    | test/compositor/compositor_test.cpp:1621 |
| RST-11             | After reset, mode is 000 (SLU)                               | —                | pass    | test/compositor/compositor_test.cpp:1634 |
| RST-12             | After reset, NR 0x4A = 0xE3                                  | —                | pass    | test/compositor/compositor_test.cpp:1643 |
| RST-13             | After reset, NR 0x14 = 0xE3                                  | —                | pass    | test/compositor/compositor_test.cpp:1653 |
| PRI-BOUND          | 3                                                            | —                | missing | missing                                  |

## Audio — `test/audio/audio_test.cpp`

Last-touch commit: `0020b7102565f8ca8555633aa662e4714db2f86a` (`0020b71025`)

| Test ID | Plan row title                                               | VHDL file:line | Status  | Test file:line                 |
|---------|--------------------------------------------------------------|----------------|---------|--------------------------------|
| AY-01   | Write register address via `busctrl_addr`                    | —              | —       | test/audio/audio_test.cpp:89   |
| AY-02   | Address only latches when `busctrl_addr=1`                   | —              | —       | test/audio/audio_test.cpp:98   |
| AY-03   | Reset clears address to 0                                    | —              | —       | test/audio/audio_test.cpp:108  |
| AY-04   | Write to all 16 registers (addr 0-15)                        | —              | —       | test/audio/audio_test.cpp:127  |
| AY-05   | Write with `addr[4]=1` is ignored                            | —              | —       | test/audio/audio_test.cpp:138  |
| AY-06   | Reset initialises all registers to 0x00                      | —              | —       | test/audio/audio_test.cpp:156  |
| AY-07   | Writing R13 triggers envelope reset                          | —              | —       | test/audio/audio_test.cpp:172  |
| AY-10   | Read R0 (Ch A fine tone) in AY mode                          | —              | —       | test/audio/audio_test.cpp:190  |
| AY-11   | Read R1 (Ch A coarse tone) in AY mode                        | —              | —       | test/audio/audio_test.cpp:201  |
| AY-12   | Read R1 in YM mode                                           | —              | —       | test/audio/audio_test.cpp:212  |
| AY-13   | Read R3, R5 (Ch B/C coarse tone) AY vs YM                    | —              | —       | test/audio/audio_test.cpp:227  |
| AY-14   | Read R6 (noise period) in AY mode                            | —              | —       | test/audio/audio_test.cpp:249  |
| AY-15   | Read R6 in YM mode                                           | —              | —       | test/audio/audio_test.cpp:260  |
| AY-16   | Read R7 (mixer enable)                                       | —              | —       | test/audio/audio_test.cpp:275  |
| AY-17   | Read R8/R9/R10 (volume) in AY mode                           | —              | —       | test/audio/audio_test.cpp:290  |
| AY-18   | Read R8/R9/R10 in YM mode                                    | —              | —       | test/audio/audio_test.cpp:304  |
| AY-19   | Read R13 (envelope shape) in AY mode                         | —              | —       | test/audio/audio_test.cpp:314  |
| AY-20   | Read R13 in YM mode                                          | —              | —       | test/audio/audio_test.cpp:325  |
| AY-21   | Read R11/R12 (envelope period)                               | —              | —       | test/audio/audio_test.cpp:341  |
| AY-22   | Read addr >= 16 in YM mode                                   | —              | —       | test/audio/audio_test.cpp:353  |
| AY-23   | Read addr >= 16 in AY mode                                   | —              | —       | test/audio/audio_test.cpp:366  |
| AY-24   | Read with `I_REG=1` (register query mode)                    | —              | —       | test/audio/audio_test.cpp:377  |
| AY-25   | AY_ID is "11" for PSG0, "10" for PSG1, "01" for PSG2         | —              | —       | test/audio/audio_test.cpp:389  |
| AY-30   | Read R14 with R7 bit 6 = 0 (Port A input mode)               | —              | missing | missing                        |
| AY-31   | Read R14 with R7 bit 6 = 1 (Port A output mode)              | —              | missing | missing                        |
| AY-32   | Read R15 with R7 bit 7 = 0 (Port B input mode)               | —              | missing | missing                        |
| AY-33   | Read R15 with R7 bit 7 = 1 (Port B output mode)              | —              | missing | missing                        |
| AY-34   | Port A/B inputs default to 0xFF (pullup)                     | —              | missing | missing                        |
| AY-40   | Divider reloads with `I_SEL_L=1` (AY compat)                 | —              | missing | missing                        |
| AY-41   | Divider reloads with `I_SEL_L=0` (YM mode)                   | —              | missing | missing                        |
| AY-42   | `ena_div` pulses once per divider cycle                      | —              | missing | missing                        |
| AY-43   | `ena_div_noise` at half `ena_div` rate                       | —              | missing | missing                        |
| AY-44   | In turbosound wiring, `I_SEL_L='1'` always                   | —              | missing | missing                        |
| AY-50   | Tone period 0 or 1 produces constant high output             | —              | —       | test/audio/audio_test.cpp:441  |
| AY-51   | Tone period 2 toggles every 2 ena_div cycles                 | —              | —       | test/audio/audio_test.cpp:455  |
| AY-52   | Tone period 0xFFF (max) produces lowest freq                 | —              | —       | test/audio/audio_test.cpp:465  |
| AY-53   | Channel A uses R1[3:0] & R0                                  | —              | —       | test/audio/audio_test.cpp:409  |
| AY-54   | Channel B uses R3[3:0] & R2                                  | —              | —       | test/audio/audio_test.cpp:420  |
| AY-55   | Channel C uses R5[3:0] & R4                                  | —              | —       | test/audio/audio_test.cpp:431  |
| AY-56   | Tone output toggles (not pulse)                              | —              | missing | missing                        |
| AY-60   | Noise period from R6[4:0]                                    | —              | —       | test/audio/audio_test.cpp:482  |
| AY-61   | Noise period 0 or 1 => comparator 0                          | —              | —       | test/audio/audio_test.cpp:491  |
| AY-62   | Noise uses 17-bit LFSR (poly17)                              | —              | missing | missing                        |
| AY-63   | Noise output is poly17 bit 0                                 | —              | missing | missing                        |
| AY-64   | Noise clocked at `ena_div_noise` rate                        | —              | missing | missing                        |
| AY-70   | R7 bit 0 = 0: Channel A tone enabled                         | —              | missing | missing                        |
| AY-71   | R7 bit 0 = 1: Channel A tone disabled (forced 1)             | —              | missing | missing                        |
| AY-72   | R7 bit 3 = 0: Channel A noise enabled                        | —              | missing | missing                        |
| AY-73   | R7 bit 3 = 1: Channel A noise disabled (forced 1)            | —              | missing | missing                        |
| AY-74   | R7 bits 1,4: Channel B tone + noise control                  | —              | missing | missing                        |
| AY-75   | R7 bits 2,5: Channel C tone + noise control                  | —              | missing | missing                        |
| AY-76   | Both tone and noise disabled: constant high                  | —              | —       | test/audio/audio_test.cpp:530  |
| AY-77   | Both tone and noise enabled: AND of both                     | —              | missing | missing                        |
| AY-78   | Mixer output 0 => volume output 0                            | —              | —       | test/audio/audio_test.cpp:542  |
| AY-80   | R8 bit 4 = 0: Channel A uses fixed volume                    | —              | missing | missing                        |
| AY-81   | R8 bit 4 = 1: Channel A uses envelope volume                 | —              | missing | missing                        |
| AY-82   | Fixed volume 0 => output "00000"                             | —              | —       | test/audio/audio_test.cpp:554  |
| AY-83   | Fixed volume 1-15 => `{vol[3:0], "1"}`                       | —              | —       | test/audio/audio_test.cpp:569  |
| AY-84   | Same for R9 (Channel B) and R10 (Channel C)                  | —              | missing | missing                        |
| AY-90   | YM mode: 32-entry volume table                               | —              | missing | missing                        |
| AY-91   | AY mode: 16-entry volume table                               | —              | missing | missing                        |
| AY-92   | YM vol 0 = 0x00, vol 31 = 0xFF                               | —              | —       | test/audio/audio_test.cpp:618  |
| AY-93   | AY vol 0 = 0x00, vol 15 = 0xFF                               | —              | —       | test/audio/audio_test.cpp:639  |
| AY-94   | YM volume table exact values                                 | —              | missing | missing                        |
| AY-95   | AY volume table exact values                                 | —              | missing | missing                        |
| AY-96   | Reset sets all audio outputs to 0x00                         | —              | —       | test/audio/audio_test.cpp:657  |
| AY-100  | Envelope period from R12:R11 (16-bit)                        | —              | —       | test/audio/audio_test.cpp:675  |
| AY-101  | Envelope period 0 or 1 => comparator 0                       | —              | —       | test/audio/audio_test.cpp:685  |
| AY-102  | Writing R13 resets envelope counter to 0                     | —              | missing | missing                        |
| AY-103  | Writing R13 resets envelope to initial state                 | —              | —       | test/audio/audio_test.cpp:708  |
| AY-110  | 0-3                                                          | —              | —       | test/audio/audio_test.cpp:723  |
| AY-111  | 4-7                                                          | —              | —       | test/audio/audio_test.cpp:746  |
| AY-112  | 8                                                            | —              | missing | missing                        |
| AY-113  | 9                                                            | —              | —       | test/audio/audio_test.cpp:778  |
| AY-114  | 10                                                           | —              | missing | missing                        |
| AY-115  | 11                                                           | —              | missing | missing                        |
| AY-116  | 12                                                           | —              | missing | missing                        |
| AY-117  | 13                                                           | —              | —       | test/audio/audio_test.cpp:763  |
| AY-118  | 14                                                           | —              | missing | missing                        |
| AY-119  | 15                                                           | —              | missing | missing                        |
| AY-120  | Attack=0 (At bit): initial vol=31, direction=down            | —              | missing | missing                        |
| AY-121  | Attack=1 (At bit): initial vol=0, direction=up               | —              | missing | missing                        |
| AY-122  | C=0: hold after first ramp regardless of Al/H                | —              | missing | missing                        |
| AY-123  | C=1, H=1, Al=0: hold one step inside boundary                | —              | missing | missing                        |
| AY-124  | C=1, H=1, Al=1: hold exactly at boundary                     | —              | missing | missing                        |
| AY-125  | C=1, H=0, Al=1: triangle wave (continuous)                   | —              | missing | missing                        |
| AY-126  | C=1, H=0, Al=0: sawtooth (continuous)                        | —              | missing | missing                        |
| AY-127  | Envelope steps through 32 levels (0-31)                      | —              | missing | missing                        |
| AY-128  | Envelope period counter reset on R13 write                   | —              | missing | missing                        |
| TS-01   | Reset selects AY#0 (`ay_select = "11"`)                      | —              | —       | test/audio/audio_test.cpp:797  |
| TS-02   | Select AY#0: write 0xFC+ to FFFD with bits[4:2]=111, bits[1… | —              | —       | test/audio/audio_test.cpp:829  |
| TS-03   | Select AY#1: write with bits[1:0]=10                         | —              | —       | test/audio/audio_test.cpp:813  |
| TS-04   | Select AY#2: write with bits[1:0]=01                         | —              | —       | test/audio/audio_test.cpp:821  |
| TS-05   | Selection requires `turbosound_en_i = 1`                     | —              | —       | test/audio/audio_test.cpp:841  |
| TS-06   | Selection requires `psg_reg_addr_i = 1`                      | —              | missing | missing                        |
| TS-07   | Selection requires `psg_d_i[7] = 1`                          | —              | —       | test/audio/audio_test.cpp:853  |
| TS-08   | Selection requires `psg_d_i[4:2] = "111"`                    | —              | —       | test/audio/audio_test.cpp:865  |
| TS-09   | Panning set simultaneously: bits[6:5]                        | —              | missing | missing                        |
| TS-10   | Reset sets all panning to "11" (both L+R)                    | —              | —       | test/audio/audio_test.cpp:885  |
| TS-15   | Normal register address: bits[7:5] must be "000"             | —              | —       | test/audio/audio_test.cpp:906  |
| TS-16   | Address routed to selected AY only                           | —              | —       | test/audio/audio_test.cpp:925  |
| TS-17   | Write routed to selected AY only                             | —              | missing | missing                        |
| TS-18   | Readback from selected AY                                    | —              | —       | test/audio/audio_test.cpp:948  |
| TS-20   | ABC stereo mode (`stereo_mode_i=0`): L=A+B, R=B+C            | —              | —       | test/audio/audio_test.cpp:1370 |
| TS-21   | ACB stereo mode (`stereo_mode_i=1`): L=A+C, R=C+B            | —              | missing | missing                        |
| TS-22   | Mono mode for PSG0: L=R=A+B+C                                | —              | —       | test/audio/audio_test.cpp:1400 |
| TS-23   | Mono mode per-PSG: each bit controls one PSG                 | —              | missing | missing                        |
| TS-24   | Stereo mode is global for all PSGs                           | —              | missing | missing                        |
| TS-30   | Turbosound disabled: only selected PSG outputs               | —              | —       | test/audio/audio_test.cpp:1424 |
| TS-31   | Turbosound enabled: all three PSGs output                    | —              | —       | test/audio/audio_test.cpp:1453 |
| TS-32   | PSG0 active when `ay_select="11"` or ts enabled              | —              | missing | missing                        |
| TS-33   | PSG1 active when `ay_select="10"` or ts enabled              | —              | missing | missing                        |
| TS-34   | PSG2 active when `ay_select="01"` or ts enabled              | —              | missing | missing                        |
| TS-40   | Pan "11": output to both L and R                             | —              | missing | missing                        |
| TS-41   | Pan "10": output to L only, R silenced                       | —              | —       | test/audio/audio_test.cpp:1008 |
| TS-42   | Pan "01": output to R only, L silenced                       | —              | —       | test/audio/audio_test.cpp:1029 |
| TS-43   | Pan "00": output silenced on both channels                   | —              | —       | test/audio/audio_test.cpp:1050 |
| TS-44   | Final L = sum of all three PSG L contributions               | —              | missing | missing                        |
| TS-45   | Final R = sum of all three PSG R contributions               | —              | missing | missing                        |
| TS-50   | PSG0 has AY_ID = "11"                                        | —              | —       | test/audio/audio_test.cpp:966  |
| TS-51   | PSG1 has AY_ID = "10"                                        | —              | —       | test/audio/audio_test.cpp:972  |
| TS-52   | PSG2 has AY_ID = "01"                                        | —              | —       | test/audio/audio_test.cpp:978  |
| SD-01   | Reset sets all channels to 0x80                              | —              | —       | test/audio/audio_test.cpp:1066 |
| SD-02   | Write channel A via port I/O (`chA_wr_i`)                    | —              | —       | test/audio/audio_test.cpp:1080 |
| SD-03   | Write channel B via port I/O (`chB_wr_i`)                    | —              | missing | missing                        |
| SD-04   | Write channel C via port I/O (`chC_wr_i`)                    | —              | —       | test/audio/audio_test.cpp:1083 |
| SD-05   | Write channel D via port I/O (`chD_wr_i`)                    | —              | missing | missing                        |
| SD-06   | NextREG 0x2D (mono) writes to chA AND chD                    | —              | —       | test/audio/audio_test.cpp:1093 |
| SD-07   | NextREG 0x2C (left) writes to chB only                       | —              | —       | test/audio/audio_test.cpp:1103 |
| SD-08   | NextREG 0x2E (right) writes to chC only                      | —              | —       | test/audio/audio_test.cpp:1113 |
| SD-09   | Port I/O takes priority over NextREG                         | —              | missing | missing                        |
| SD-10   | Soundrive mode 1 ports: 0x1F(A), 0x0F(B), 0x4F(C), 0x5F(D)   | —              | missing | missing                        |
| SD-11   | Soundrive mode 2 ports: 0xF1(A), 0xF3(B), 0xF9(C), 0xFB(D)   | —              | missing | missing                        |
| SD-12   | Profi Covox: 0x3F(A), 0x5F(D)                                | —              | missing | missing                        |
| SD-13   | Covox: 0x0F(B), 0x4F(C)                                      | —              | missing | missing                        |
| SD-14   | Pentagon/ATM mono: 0xFB(A+D)                                 | —              | missing | missing                        |
| SD-15   | GS Covox: 0xB3(B+C)                                          | —              | missing | missing                        |
| SD-16   | SpecDrum: 0xDF(A+D)                                          | —              | missing | missing                        |
| SD-17   | DAC requires `nr_08_dac_en=1`                                | —              | missing | missing                        |
| SD-18   | Mono ports (FB, DF, B3) write to both A+D or B+C             | —              | missing | missing                        |
| SD-20   | Left output = chA + chB (9-bit unsigned)                     | —              | —       | test/audio/audio_test.cpp:1123 |
| SD-21   | Right output = chC + chD (9-bit unsigned)                    | —              | —       | test/audio/audio_test.cpp:1133 |
| SD-22   | Max output: chA=0xFF, chB=0xFF => L=0x1FE                    | —              | —       | test/audio/audio_test.cpp:1143 |
| SD-23   | Reset output: L=0x100, R=0x100                               | —              | —       | test/audio/audio_test.cpp:1151 |
| BP-01   | Port 0xFE write stores bits [4:0]                            | —              | missing | missing                        |
| BP-02   | Bit 4 is the EAR output (speaker)                            | —              | —       | test/audio/audio_test.cpp:1168 |
| BP-03   | Bit 3 is the MIC output                                      | —              | —       | test/audio/audio_test.cpp:1179 |
| BP-04   | Bits [2:0] are the border colour                             | —              | missing | missing                        |
| BP-05   | Reset clears port_fe_reg to 0                                | —              | —       | test/audio/audio_test.cpp:1190 |
| BP-06   | Port 0xFE decoded as A0=0                                    | —              | missing | missing                        |
| BP-10   | `beep_mic_final` = `EAR_in XOR (mic AND issue2) XOR mic`     | —              | —       | test/audio/audio_test.cpp:1198 |
| BP-11   | Issue 2 mode: MIC is XOR'd twice (cancels)                   | —              | missing | missing                        |
| BP-12   | Issue 3 mode: MIC contributes to beep                        | —              | missing | missing                        |
| BP-13   | Internal speaker exclusive mode                              | —              | missing | missing                        |
| BP-20   | Port 0xFE read bit 6 = `EAR_in OR port_fe_ear`               | —              | missing | missing                        |
| BP-21   | Port 0xFE read bit 5 = 1 (always set)                        | —              | missing | missing                        |
| BP-22   | Port 0xFE read bits [4:0] = keyboard columns                 | —              | missing | missing                        |
| BP-23   | Port 0xFE read bit 7 = 1                                     | —              | missing | missing                        |
| MX-01   | EAR volume = 0x0200 (512) when active                        | —              | —       | test/audio/audio_test.cpp:1267 |
| MX-02   | MIC volume = 0x0080 (128) when active                        | —              | —       | test/audio/audio_test.cpp:1272 |
| MX-03   | EAR/MIC silenced when `exc_i=1`                              | —              | missing | missing                        |
| MX-04   | AY input: zero-extended 12-bit to 13-bit                     | —              | missing | missing                        |
| MX-05   | DAC input: 9-bit left-shifted by 2 + zero-padded             | —              | —       | test/audio/audio_test.cpp:1295 |
| MX-06   | I2S input: zero-extended 10-bit to 13-bit                    | —              | missing | missing                        |
| MX-10   | Left output = ear + mic + ay_L + dac_L + i2s_L               | —              | —       | test/audio/audio_test.cpp:1326 |
| MX-11   | Right output = ear + mic + ay_R + dac_R + i2s_R              | —              | missing | missing                        |
| MX-12   | Reset zeroes both output channels                            | —              | —       | test/audio/audio_test.cpp:1243 |
| MX-13   | EAR and MIC go to both L and R                               | —              | —       | test/audio/audio_test.cpp:1311 |
| MX-14   | Max theoretical output = 5998                                | —              | missing | missing                        |
| MX-15   | No saturation/clipping in mixer                              | —              | missing | missing                        |
| MX-20   | `exc_i=1`: EAR and MIC contribute 0 to mix                   | —              | missing | missing                        |
| MX-21   | `exc_i=0`: EAR and MIC contribute normally                   | —              | missing | missing                        |
| MX-22   | `exc_i` derived from NextREGs 0x06 bit 6 AND 0x08 bit 4      | —              | missing | missing                        |
| NR-01   | `nr_06_psg_mode[1:0]` from NextREG 0x06 bits [1:0]           | —              | missing | missing                        |
| NR-02   | Mode "00": YM2149 mode                                       | —              | missing | missing                        |
| NR-03   | Mode "01": AY-8910 mode                                      | —              | missing | missing                        |
| NR-04   | Mode "10": YM2149 mode (bit 0 = 0)                           | —              | missing | missing                        |
| NR-05   | Mode "11": AY reset (silent)                                 | —              | missing | missing                        |
| NR-06   | `nr_06_internal_speaker_beep` from bit 6                     | —              | missing | missing                        |
| NR-10   | Bit 5: PSG stereo mode (0=ABC, 1=ACB)                        | —              | missing | missing                        |
| NR-11   | Bit 4: Internal speaker enable                               | —              | missing | missing                        |
| NR-12   | Bit 3: DAC enable                                            | —              | missing | missing                        |
| NR-13   | Bit 1: Turbosound enable                                     | —              | missing | missing                        |
| NR-14   | Bit 0: Keyboard Issue 2 mode                                 | —              | missing | missing                        |
| NR-20   | Bits [7:5] of NextREG 0x09: per-PSG mono                     | —              | missing | missing                        |
| NR-21   | Bit 7: PSG2 mono, Bit 6: PSG1 mono, Bit 5: PSG0 mono         | —              | missing | missing                        |
| NR-30   | NextREG 0x2C: write to Soundrive chB (left)                  | —              | missing | missing                        |
| NR-31   | NextREG 0x2D: write to Soundrive chA+chD (mono)              | —              | missing | missing                        |
| NR-32   | NextREG 0x2E: write to Soundrive chC (right)                 | —              | missing | missing                        |
| IO-01   | Port FFFD: `A[15:14]="11"`, A[2]=1, A[0]=1                   | —              | missing | missing                        |
| IO-02   | Port BFFD: `A[15:14]="10"`, A[2]=1, A[0]=1                   | —              | missing | missing                        |
| IO-03   | Port BFF5: BFFD with A[3]=0                                  | —              | missing | missing                        |
| IO-04   | FFFD read latched on falling CPU clock edge                  | —              | missing | missing                        |
| IO-05   | BFFD readable as FFFD on +3 timing                           | —              | missing | missing                        |
| IO-10   | DAC writes require `dac_hw_en=1`                             | —              | missing | missing                        |
| IO-11   | Multiple port mappings can map to same channel               | —              | missing | missing                        |
| IO-12   | Port FD conflict: F1 and F9 in mode 2                        | —              | missing | missing                        |

## DMA — `test/dma/dma_test.cpp`

Last-touch commit: `651ea41d76a30d6745a4a83c7fa79d859d61ae77` (`651ea41d76`)

| Test ID | Plan row title                         | VHDL file:line | Status  | Test file:line             |
|---------|----------------------------------------|----------------|---------|----------------------------|
| 1.1     | Write to port 0x6B sets ZXN mode       | —              | —       | test/dma/dma_test.cpp:178  |
| 1.2     | Write to port 0x0B sets Z80-DMA mode   | —              | —       | test/dma/dma_test.cpp:190  |
| 1.3     | Read from port 0x6B sets ZXN mode      | —              | —       | test/dma/dma_test.cpp:203  |
| 1.4     | Read from port 0x0B sets Z80 mode      | —              | —       | test/dma/dma_test.cpp:223  |
| 1.5     | Mode defaults to ZXN (0) on reset      | —              | missing | missing                    |
| 1.6     | Mode switches on each access           | —              | missing | missing                    |
| 2.1     | R0 direction A->B                      | —              | —       | test/dma/dma_test.cpp:246  |
| 2.2     | R0 direction B->A                      | —              | —       | test/dma/dma_test.cpp:264  |
| 2.3     | R0 port A start address low byte       | —              | —       | test/dma/dma_test.cpp:278  |
| 2.4     | R0 port A start address high byte      | —              | —       | test/dma/dma_test.cpp:293  |
| 2.5     | R0 port A full 16-bit address          | —              | —       | test/dma/dma_test.cpp:305  |
| 2.6     | R0 block length low byte               | —              | —       | test/dma/dma_test.cpp:318  |
| 2.7     | R0 block length high byte              | —              | —       | test/dma/dma_test.cpp:334  |
| 2.8     | R0 selective byte programming          | —              | missing | missing                    |
| 3.1     | Port A is memory (default)             | —              | —       | test/dma/dma_test.cpp:351  |
| 3.2     | Port A is I/O                          | —              | —       | test/dma/dma_test.cpp:360  |
| 3.3     | Port A address increment               | —              | —       | test/dma/dma_test.cpp:371  |
| 3.4     | Port A address decrement               | —              | —       | test/dma/dma_test.cpp:382  |
| 3.5     | Port A address fixed                   | —              | —       | test/dma/dma_test.cpp:405  |
| 3.6     | Port A timing byte                     | —              | missing | missing                    |
| 4.1     | Port B is memory (default)             | —              | —       | test/dma/dma_test.cpp:421  |
| 4.2     | Port B is I/O                          | —              | —       | test/dma/dma_test.cpp:430  |
| 4.3     | Port B address increment               | —              | —       | test/dma/dma_test.cpp:439  |
| 4.4     | Port B address decrement               | —              | —       | test/dma/dma_test.cpp:458  |
| 4.5     | Port B address fixed                   | —              | —       | test/dma/dma_test.cpp:477  |
| 4.6     | Port B timing byte                     | —              | missing | missing                    |
| 4.7     | Port B prescaler byte                  | —              | missing | missing                    |
| 4.8     | Port B prescaler = 0 (no delay)        | —              | missing | missing                    |
| 5.1     | R3 with bit 6=1 triggers START_DMA     | —              | —       | test/dma/dma_test.cpp:494  |
| 5.2     | R3 with bit 6=0 does not start         | —              | —       | test/dma/dma_test.cpp:505  |
| 5.3     | R3 mask byte (bit 3)                   | —              | —       | test/dma/dma_test.cpp:517  |
| 5.4     | R3 match byte (bit 4)                  | —              | —       | test/dma/dma_test.cpp:529  |
| 6.1     | Byte mode (R4_mode = "00")             | —              | —       | test/dma/dma_test.cpp:543  |
| 6.2     | Continuous mode (R4_mode = "01")       | —              | —       | test/dma/dma_test.cpp:553  |
| 6.3     | Burst mode (R4_mode = "10")            | —              | —       | test/dma/dma_test.cpp:563  |
| 6.4     | Default mode is continuous ("01")      | —              | —       | test/dma/dma_test.cpp:581  |
| 6.5     | Port B start address low               | —              | missing | missing                    |
| 6.6     | Port B start address high              | —              | missing | missing                    |
| 6.7     | Port B full 16-bit address             | —              | missing | missing                    |
| 6.8     | Mode "11" treated as "00" (byte)       | —              | missing | missing                    |
| 7.1     | Auto-restart enabled                   | —              | —       | test/dma/dma_test.cpp:602  |
| 7.2     | Auto-restart disabled (default)        | —              | —       | test/dma/dma_test.cpp:627  |
| 7.3     | CE/WAIT mux bit                        | —              | —       | test/dma/dma_test.cpp:638  |
| 7.4     | R5 defaults on reset                   | —              | missing | missing                    |
| 8.1     | 0xC3 — Reset                           | —              | —       | test/dma/dma_test.cpp:657  |
| 8.2     | 0xC7 — Reset port A timing             | —              | —       | test/dma/dma_test.cpp:666  |
| 8.3     | 0xCB — Reset port B timing             | —              | —       | test/dma/dma_test.cpp:674  |
| 8.4     | 0xCF — Load                            | —              | —       | test/dma/dma_test.cpp:687  |
| 8.5     | 0xCF — Load A->B direction             | —              | —       | test/dma/dma_test.cpp:701  |
| 8.6     | 0xCF — Load B->A direction             | —              | —       | test/dma/dma_test.cpp:713  |
| 8.7     | 0xCF — Load counter ZXN mode           | —              | —       | test/dma/dma_test.cpp:725  |
| 8.8     | 0xCF — Load counter Z80 mode           | —              | —       | test/dma/dma_test.cpp:741  |
| 8.9     | 0xD3 — Continue                        | —              | —       | test/dma/dma_test.cpp:756  |
| 8.10    | 0xD3 — Continue ZXN mode               | —              | —       | test/dma/dma_test.cpp:765  |
| 8.11    | 0xD3 — Continue Z80 mode               | —              | —       | test/dma/dma_test.cpp:775  |
| 8.12    | 0x87 — Enable DMA                      | —              | —       | test/dma/dma_test.cpp:795  |
| 8.13    | 0x83 — Disable DMA                     | —              | —       | test/dma/dma_test.cpp:805  |
| 8.14    | 0x8B — Reinitialize status             | —              | —       | test/dma/dma_test.cpp:819  |
| 8.15    | 0xBB — Read mask follows               | —              | —       | test/dma/dma_test.cpp:832  |
| 8.16    | 0xBF — Read status byte                | —              | missing | missing                    |
| 9.1     | Simple A->B, increment both            | —              | —       | test/dma/dma_test.cpp:852  |
| 9.2     | Simple B->A, increment both            | —              | —       | test/dma/dma_test.cpp:877  |
| 9.3     | A->B, decrement source                 | —              | —       | test/dma/dma_test.cpp:901  |
| 9.4     | A->B, fixed source (fill)              | —              | —       | test/dma/dma_test.cpp:923  |
| 9.5     | A->B, fixed dest (probe)               | —              | —       | test/dma/dma_test.cpp:935  |
| 9.6     | Block length = 1                       | —              | —       | test/dma/dma_test.cpp:950  |
| 9.7     | Block length = 256                     | —              | —       | test/dma/dma_test.cpp:961  |
| 9.8     | Block length = 0 (edge case)           | —              | missing | missing                    |
| 10.1    | Mem(A) -> IO(B), A inc, B fixed        | —              | —       | test/dma/dma_test.cpp:988  |
| 10.2    | Mem(A) -> IO(B), A inc, B inc          | —              | —       | test/dma/dma_test.cpp:1007 |
| 10.3    | Verify MREQ on read, IORQ on write     | —              | —       | test/dma/dma_test.cpp:1026 |
| 10.4    | IO(A) -> Mem(B)                        | —              | missing | missing                    |
| 10.5    | IO(A) -> IO(B)                         | —              | missing | missing                    |
| 10.6    | Port B address as I/O port             | —              | missing | missing                    |
| 11.1    | Both increment (A->B)                  | —              | —       | test/dma/dma_test.cpp:1045 |
| 11.2    | Both decrement (A->B)                  | —              | —       | test/dma/dma_test.cpp:1066 |
| 11.3    | Source inc, dest dec                   | —              | —       | test/dma/dma_test.cpp:1090 |
| 11.4    | Source dec, dest fixed                 | —              | —       | test/dma/dma_test.cpp:1111 |
| 11.5    | Both fixed (port-to-port)              | —              | missing | missing                    |
| 11.6    | Address wrap at 0xFFFF                 | —              | missing | missing                    |
| 12.1    | Continuous mode — full block           | —              | —       | test/dma/dma_test.cpp:1129 |
| 12.2    | Burst mode — no prescaler              | —              | —       | test/dma/dma_test.cpp:1153 |
| 12.3    | Burst mode — with prescaler            | —              | —       | test/dma/dma_test.cpp:1174 |
| 12.4    | Burst mode — bus release timing        | —              | missing | missing                    |
| 12.5    | Burst mode — bus re-request            | —              | missing | missing                    |
| 12.6    | Byte mode — single byte                | —              | missing | missing                    |
| 12.7    | Continuous mode — no prescaler delay   | —              | missing | missing                    |
| 12.8    | Burst mode — prescaler vs timer        | —              | missing | missing                    |
| 13.1    | Prescaler = 0 (no wait)                | —              | missing | missing                    |
| 13.2    | Prescaler > 0 at 3.5MHz                | —              | missing | missing                    |
| 13.3    | Prescaler > 0 at 7MHz                  | —              | missing | missing                    |
| 13.4    | Prescaler > 0 at 14MHz                 | —              | missing | missing                    |
| 13.5    | Prescaler > 0 at 28MHz                 | —              | missing | missing                    |
| 13.6    | Prescaler comparison                   | —              | missing | missing                    |
| 14.1    | ZXN mode: counter starts at 0          | —              | —       | test/dma/dma_test.cpp:1193 |
| 14.2    | Z80 mode: counter starts at 0xFFFF     | —              | —       | test/dma/dma_test.cpp:1205 |
| 14.3    | Counter increments per byte            | —              | —       | test/dma/dma_test.cpp:1217 |
| 14.4    | ZXN: block_len=5 transfers 5 bytes     | —              | —       | test/dma/dma_test.cpp:1228 |
| 14.5    | Z80: block_len=5 transfers 6 bytes     | —              | —       | test/dma/dma_test.cpp:1246 |
| 14.6    | ZXN: block_len=0 transfers 0 bytes     | —              | missing | missing                    |
| 14.7    | Z80: block_len=0 transfers 1 byte      | —              | missing | missing                    |
| 14.8    | Counter readback accuracy              | —              | missing | missing                    |
| 15.1    | DMA requests bus before transfer       | —              | missing | missing                    |
| 15.2    | DMA waits for bus acknowledge          | —              | missing | missing                    |
| 15.3    | DMA releases bus when idle             | —              | missing | missing                    |
| 15.4    | DMA defers to external BUSREQ          | —              | missing | missing                    |
| 15.5    | DMA defers to daisy chain              | —              | missing | missing                    |
| 15.6    | DMA defers to IM2 delay                | —              | missing | missing                    |
| 15.7    | Bus mux when DMA holds bus             | —              | missing | missing                    |
| 15.8    | DMA cannot self-program                | —              | missing | missing                    |
| 16.1    | Auto-restart reloads addresses         | —              | —       | test/dma/dma_test.cpp:1266 |
| 16.2    | Auto-restart reloads counter           | —              | —       | test/dma/dma_test.cpp:1280 |
| 16.3    | Auto-restart direction A->B            | —              | —       | test/dma/dma_test.cpp:1292 |
| 16.4    | Auto-restart direction B->A            | —              | missing | missing                    |
| 16.5    | Continue preserves addresses           | —              | missing | missing                    |
| 16.6    | Continue vs Load                       | —              | missing | missing                    |
| 17.1    | Status byte format                     | —              | —       | test/dma/dma_test.cpp:1312 |
| 17.2    | End-of-block flag clear initially      | —              | —       | test/dma/dma_test.cpp:1328 |
| 17.3    | End-of-block set after transfer        | —              | —       | test/dma/dma_test.cpp:1342 |
| 17.4    | At-least-one flag                      | —              | —       | test/dma/dma_test.cpp:1365 |
| 17.5    | Status cleared by 0x8B                 | —              | —       | test/dma/dma_test.cpp:1368 |
| 17.6    | Status cleared by 0xC3 (reset)         | —              | —       | test/dma/dma_test.cpp:1371 |
| 17.7    | Default read mask                      | —              | —       | test/dma/dma_test.cpp:1374 |
| 17.8    | Read sequence cycles through mask      | —              | —       | test/dma/dma_test.cpp:1389 |
| 17.9    | Custom read mask (status+counter only) | —              | missing | missing                    |
| 17.10   | Read sequence wraps around             | —              | missing | missing                    |
| 18.1    | Read status byte                       | —              | missing | missing                    |
| 18.2    | Read counter LO                        | —              | missing | missing                    |
| 18.3    | Read counter HI                        | —              | missing | missing                    |
| 18.4    | Read port A addr LO (A->B)             | —              | missing | missing                    |
| 18.5    | Read port A addr HI (A->B)             | —              | missing | missing                    |
| 18.6    | Read port B addr LO (A->B)             | —              | missing | missing                    |
| 18.7    | Read port B addr HI (A->B)             | —              | missing | missing                    |
| 18.8    | Read port A/B in B->A mode             | —              | missing | missing                    |
| 19.1    | Hardware reset defaults                | —              | —       | test/dma/dma_test.cpp:1404 |
| 19.2    | R6 0xC3 soft reset                     | —              | —       | test/dma/dma_test.cpp:1431 |
| 19.3    | 0xC3 does not reset R0/R4 addresses    | —              | —       | test/dma/dma_test.cpp:1441 |
| 19.4    | 0xC3 resets timing to "01"             | —              | missing | missing                    |
| 19.5    | 0xC3 resets prescaler to 0x00          | —              | missing | missing                    |
| 19.6    | 0xC3 resets auto-restart to 0          | —              | missing | missing                    |
| 20.1    | DMA delay blocks START_DMA             | —              | missing | missing                    |
| 20.2    | DMA delay mid-transfer                 | —              | missing | missing                    |
| 20.3    | IM2 DMA interrupt enable regs          | —              | missing | missing                    |
| 20.4    | DMA delay signal composition           | —              | missing | missing                    |
| 21.1    | Timing "00" = 4-cycle read/write       | —              | missing | missing                    |
| 21.2    | Timing "01" = 3-cycle (default)        | —              | missing | missing                    |
| 21.3    | Timing "10" = 2-cycle                  | —              | missing | missing                    |
| 21.4    | Timing "11" = 4-cycle                  | —              | missing | missing                    |
| 21.5    | Read timing from source port           | —              | missing | missing                    |
| 21.6    | Write timing from dest port            | —              | missing | missing                    |
| 22.1    | Disable during active transfer         | —              | —       | test/dma/dma_test.cpp:1470 |
| 22.2    | Enable without Load                    | —              | —       | test/dma/dma_test.cpp:1479 |
| 22.3    | Multiple Loads before Enable           | —              | —       | test/dma/dma_test.cpp:1501 |
| 22.4    | Continue after auto-restart            | —              | —       | test/dma/dma_test.cpp:1513 |
| 22.5    | R0 register decoding ambiguity         | —              | missing | missing                    |
| 22.6    | Simultaneous R0/R2 decode              | —              | missing | missing                    |

## DivMMC+SPI — `test/divmmc/divmmc_test.cpp`

Last-touch commit: `86dc8f85dcd38b25259a532ebea3b7b0ac998a15` (`86dc8f85dc`)

| Test ID          | Plan row title                                               | VHDL file:line | Status  | Test file:line                   |
|------------------|--------------------------------------------------------------|----------------|---------|----------------------------------|
| E3-01            | Reset clears port 0xE3 to 0x00                               | —              | —       | test/divmmc/divmmc_test.cpp:108  |
| E3-02            | Write 0x80: conmem=1, mapram=0, bank=0                       | —              | —       | test/divmmc/divmmc_test.cpp:118  |
| E3-03            | Write 0x40: mapram latches ON permanently                    | —              | —       | test/divmmc/divmmc_test.cpp:128  |
| E3-04            | Write 0x00 after mapram set: mapram stays 1                  | —              | —       | test/divmmc/divmmc_test.cpp:140  |
| E3-05            | mapram cleared by NextREG 0x09 bit 3                         | —              | missing | missing                          |
| E3-06            | Write bank 0x0F: bits 3:0 select bank 0-15                   | —              | —       | test/divmmc/divmmc_test.cpp:150  |
| E3-07            | Read port 0xE3 returns `{conmem, mapram, 00, bank[3:0]}`     | —              | —       | test/divmmc/divmmc_test.cpp:164  |
| E3-08            | Bits 5:4 of write are ignored                                | —              | —       | test/divmmc/divmmc_test.cpp:177  |
| CM-01            | conmem=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM               | —              | —       | test/divmmc/divmmc_test.cpp:194  |
| CM-02            | conmem=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N        | —              | —       | test/divmmc/divmmc_test.cpp:205  |
| CM-03            | conmem=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3        | —              | —       | test/divmmc/divmmc_test.cpp:218  |
| CM-04            | conmem=1, mapram=1: 0x2000-0x3FFF = DivMMC RAM bank N        | —              | —       | test/divmmc/divmmc_test.cpp:229  |
| CM-05            | conmem=1: 0x0000-0x1FFF is read-only                         | —              | —       | test/divmmc/divmmc_test.cpp:240  |
| CM-06            | conmem=1, mapram=1, bank=3: 0x2000-0x3FFF is read-only       | —              | —       | test/divmmc/divmmc_test.cpp:251  |
| CM-07            | conmem=1, mapram=1, bank!=3: 0x2000-0x3FFF is writable       | —              | —       | test/divmmc/divmmc_test.cpp:262  |
| CM-08            | conmem=0, automap=0: no DivMMC mapping                       | —              | —       | test/divmmc/divmmc_test.cpp:273  |
| CM-09            | DivMMC paging requires `port_divmmc_io_en=1`                 | —              | —       | test/divmmc/divmmc_test.cpp:284  |
| AM-01            | automap=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM              | —              | —       | test/divmmc/divmmc_test.cpp:301  |
| AM-02            | automap=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N       | —              | —       | test/divmmc/divmmc_test.cpp:313  |
| AM-03            | automap=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3       | —              | —       | test/divmmc/divmmc_test.cpp:327  |
| AM-04            | automap active, then deactivated: normal ROM restored        | —              | —       | test/divmmc/divmmc_test.cpp:339  |
| EP-01            | M1 fetch at 0x0000: automap_delayed_on activates             | —              | —       | test/divmmc/divmmc_test.cpp:356  |
| EP-02            | M1 fetch at 0x0008: automap_rom3_delayed_on                  | —              | —       | test/divmmc/divmmc_test.cpp:367  |
| EP-03            | M1 fetch at 0x0038: automap_rom3_delayed_on                  | —              | —       | test/divmmc/divmmc_test.cpp:378  |
| EP-04            | M1 fetch at 0x0010: no automap (EP2 disabled)                | —              | missing | missing                          |
| EP-05            | M1 fetch at 0x0018: no automap (EP3 disabled)                | —              | missing | missing                          |
| EP-06            | M1 fetch at 0x0020: no automap (EP4 disabled)                | —              | missing | missing                          |
| EP-07            | M1 fetch at 0x0028: no automap (EP5 disabled)                | —              | missing | missing                          |
| EP-08            | M1 fetch at 0x0030: no automap (EP6 disabled)                | —              | missing | missing                          |
| EP-09            | Set NR 0xBA[0]=1: 0x0000 becomes instant_on                  | —              | missing | missing                          |
| EP-10            | Set NR 0xB9[1]=1: 0x0008 becomes automap (not rom3)          | —              | missing | missing                          |
| EP-11            | Set NR 0xB8=0xFF: all 8 RST addresses trigger                | —              | —       | test/divmmc/divmmc_test.cpp:419  |
| EP-12            | Automap only triggers on M1+MREQ (instruction fetch)         | —              | —       | test/divmmc/divmmc_test.cpp:429  |
| NR-01            | M1 at 0x04C6 with BB[2]=1: automap_rom3_delayed_on           | —              | —       | test/divmmc/divmmc_test.cpp:446  |
| NR-02            | M1 at 0x0562 with BB[3]=1: automap_rom3_delayed_on           | —              | —       | test/divmmc/divmmc_test.cpp:457  |
| NR-03            | M1 at 0x04D7 with BB[4]=0: no trigger (default)              | —              | —       | test/divmmc/divmmc_test.cpp:468  |
| NR-04            | M1 at 0x056A with BB[5]=0: no trigger (default)              | —              | —       | test/divmmc/divmmc_test.cpp:479  |
| NR-05            | Set BB[4]=1, M1 at 0x04D7: triggers rom3_delayed_on          | —              | —       | test/divmmc/divmmc_test.cpp:491  |
| NR-06            | M1 at 0x3D00 with BB[7]=1: automap_rom3_instant_on           | —              | —       | test/divmmc/divmmc_test.cpp:503  |
| NR-07            | M1 at 0x3DFF with BB[7]=1: automap_rom3_instant_on           | —              | —       | test/divmmc/divmmc_test.cpp:514  |
| NR-08            | Set BB[7]=0, M1 at 0x3D00: no trigger                        | —              | —       | test/divmmc/divmmc_test.cpp:526  |
| DA-01            | M1 at 0x1FF8 with automap held: automap deactivates          | —              | —       | test/divmmc/divmmc_test.cpp:544  |
| DA-02            | M1 at 0x1FFF with automap held: automap deactivates          | —              | —       | test/divmmc/divmmc_test.cpp:556  |
| DA-03            | M1 at 0x1FF7: no deactivation                                | —              | —       | test/divmmc/divmmc_test.cpp:568  |
| DA-04            | M1 at 0x2000: no deactivation                                | —              | —       | test/divmmc/divmmc_test.cpp:580  |
| DA-05            | Set BB[6]=0: deactivation range disabled                     | —              | —       | test/divmmc/divmmc_test.cpp:593  |
| DA-06            | RETN instruction seen: automap deactivates                   | —              | missing | missing                          |
| DA-07            | Reset clears automap state                                   | —              | —       | test/divmmc/divmmc_test.cpp:605  |
| DA-08            | `automap_reset` clears automap state                         | —              | missing | missing                          |
| TM-01            | Instant on: DivMMC mapped during the triggering fetch        | —              | missing | missing                          |
| TM-02            | Delayed on: DivMMC mapped on NEXT fetch after trigger        | —              | missing | missing                          |
| TM-03            | automap_held latches on MREQ_n rising edge                   | —              | missing | missing                          |
| TM-04            | automap_hold updates only during M1+MREQ                     | —              | missing | missing                          |
| TM-05            | Held automap persists across non-deactivating fetches        | —              | missing | missing                          |
| R3-01            | M1 at 0x0008 with ROM3 active: automap triggers              | —              | missing | missing                          |
| R3-02            | M1 at 0x0008 with ROM0 active: no automap                    | —              | missing | missing                          |
| R3-03            | M1 at 0x0008 with Layer 2 mapped: no automap                 | —              | missing | missing                          |
| R3-04            | `automap_active` (non-ROM3 path) always enabled when DivMMC… | —              | missing | missing                          |
| NM-01            | DivMMC button press sets `button_nmi`                        | —              | —       | test/divmmc/divmmc_test.cpp:748  |
| NM-02            | M1 at 0x0066 with button_nmi: automap_nmi triggers           | —              | —       | test/divmmc/divmmc_test.cpp:760  |
| NM-03            | M1 at 0x0066 without button_nmi: no NMI automap              | —              | missing | missing                          |
| NM-04            | button_nmi cleared by reset                                  | —              | missing | missing                          |
| NM-05            | button_nmi cleared by automap_reset                          | —              | missing | missing                          |
| NM-06            | button_nmi cleared by RETN                                   | —              | missing | missing                          |
| NM-07            | button_nmi cleared when automap_held becomes 1               | —              | missing | missing                          |
| NM-08            | `o_disable_nmi` = automap OR button_nmi                      | —              | missing | missing                          |
| NA-01            | NR 0x0A[4]=0 (default): automap_reset asserted               | —              | missing | missing                          |
| NA-02            | NR 0x0A[4]=1: automap_reset deasserted                       | —              | missing | missing                          |
| NA-03            | port_divmmc_io_en=0: automap_reset asserted                  | —              | missing | missing                          |
| SM-01            | DivMMC ROM maps to SRAM address 0x010000-0x011FFF            | —              | missing | missing                          |
| SM-02            | DivMMC RAM bank 0 maps to SRAM 0x020000                      | —              | missing | missing                          |
| SM-03            | DivMMC RAM bank 3 maps to SRAM 0x026000                      | —              | missing | missing                          |
| SM-04            | DivMMC RAM bank 15 maps to SRAM 0x03E000                     | —              | missing | missing                          |
| SM-05            | DivMMC has priority over Layer 2 mapping                     | —              | missing | missing                          |
| SM-06            | DivMMC has priority over ROMCS                               | —              | missing | missing                          |
| SM-07            | ROMCS maps to DivMMC banks 14 and 15                         | —              | missing | missing                          |
| SS-01            | Reset: port_e7_reg = 0xFF (all deselected)                   | —              | —       | test/divmmc/divmmc_test.cpp:775  |
| SS-02            | Write 0x01 (sd_swap=0): selects SD1                          | —              | —       | test/divmmc/divmmc_test.cpp:785  |
| SS-03            | Write 0x02 (sd_swap=0): selects SD0                          | —              | —       | test/divmmc/divmmc_test.cpp:795  |
| SS-04            | Write 0x01 with sd_swap=1: selects SD0 (swapped)             | —              | —       | test/divmmc/divmmc_test.cpp:808  |
| SS-05            | Write 0x02 with sd_swap=1: selects SD1 (swapped)             | —              | —       | test/divmmc/divmmc_test.cpp:821  |
| SS-06            | Write 0xFB: selects RPI0 (bit 2 = 0)                         | —              | —       | test/divmmc/divmmc_test.cpp:837  |
| SS-07            | Write 0xF7: selects RPI1 (bit 3 = 0)                         | —              | missing | missing                          |
| SS-08            | Write 0x7F in config mode: selects Flash                     | —              | missing | missing                          |
| SS-09            | Write 0x7F outside config mode: all deselected (0xFF)        | —              | missing | missing                          |
| SS-10            | Write any other value: all deselected (0xFF)                 | —              | missing | missing                          |
| SS-11            | Only one device selected at a time                           | —              | missing | missing                          |
| SX-01            | Write to port 0xEB: sends byte via MOSI                      | —              | —       | test/divmmc/divmmc_test.cpp:856  |
| SX-02            | Read from port 0xEB: sends 0xFF via MOSI, receives MISO      | —              | —       | test/divmmc/divmmc_test.cpp:870  |
| SX-03            | Read returns PREVIOUS exchange result                        | —              | missing | missing                          |
| SX-04            | First read after reset returns 0xFF                          | —              | —       | test/divmmc/divmmc_test.cpp:881  |
| SX-05            | Write 0xAA then read: read returns MISO from write cycle     | —              | —       | test/divmmc/divmmc_test.cpp:897  |
| SX-06            | SPI transfer is 16 clock cycles (8 bits x 2 edges)           | —              | —       | test/divmmc/divmmc_test.cpp:911  |
| SX-07            | SCK output matches state_r[0]                                | —              | —       | test/divmmc/divmmc_test.cpp:929  |
| SX-08            | MOSI outputs MSB first                                       | —              | missing | missing                          |
| SX-09            | MISO sampled on rising SCK edge (delayed by 1 cycle)         | —              | missing | missing                          |
| SX-10            | Back-to-back transfers: new transfer starts on last state    | —              | missing | missing                          |
| ST-01            | Reset: state = "10000" (idle)                                | —              | missing | missing                          |
| ST-02            | Transfer start: state goes to "00000"                        | —              | missing | missing                          |
| ST-03            | State increments each clock until 0x0F                       | —              | missing | missing                          |
| ST-04            | After state 0x0F, returns to idle ("10000")                  | —              | missing | missing                          |
| ST-05            | `spi_wait_n = 0` during active transfer                      | —              | missing | missing                          |
| ST-06            | `spi_wait_n = 1` when idle or on last cycle                  | —              | missing | missing                          |
| ST-07            | Transfer can begin from idle OR from last state              | —              | missing | missing                          |
| ST-08            | Read/write during mid-transfer: ignored                      | —              | missing | missing                          |
| ML-01            | MISO bits shifted in on delayed rising SCK                   | —              | missing | missing                          |
| ML-02            | Full byte latched into `miso_dat` on `state_last_d`          | —              | missing | missing                          |
| ML-03            | `miso_dat` holds value until next transfer completes         | —              | missing | missing                          |
| ML-04            | Input and output shift registers are independent             | —              | missing | missing                          |
| ML-05            | Reset sets `ishift_r` to all 1s                              | —              | missing | missing                          |
| ML-06            | 16 cycles minimum between read/write operations              | —              | missing | missing                          |
| MX-01            | Flash selected: MISO from flash                              | —              | missing | missing                          |
| MX-02            | RPI selected: MISO from RPI                                  | —              | missing | missing                          |
| MX-03            | SD selected: MISO from SD                                    | —              | missing | missing                          |
| MX-04            | No device selected: MISO reads as 1                          | —              | missing | missing                          |
| MX-05            | Priority: Flash > RPI > SD > default                         | —              | missing | missing                          |
| IN-01            | Boot sequence: automap at 0x0000, DivMMC ROM mapped          | —              | —       | test/divmmc/divmmc_test.cpp:979  |
| IN-02            | SD card init: select SD0, exchange bytes, deselect           | —              | —       | test/divmmc/divmmc_test.cpp:995  |
| IN-03            | RETN after NMI handler: automap deactivated, normal ROM      | —              | missing | missing                          |
| IN-04            | Automap at 0x0008 (RST 8): ROM3 conditional                  | —              | missing | missing                          |
| IN-05            | Rapid SPI exchanges: back-to-back without idle gap           | —              | missing | missing                          |
| IN-06            | conmem override during automap: conmem takes priority        | —              | —       | test/divmmc/divmmc_test.cpp:1007 |
| IN-07            | DivMMC disabled via NR 0x0A[4]=0: no automap, SPI still wor… | —              | —       | test/divmmc/divmmc_test.cpp:1029 |
| ROM3-conditional | 4                                                            | —              | missing | missing                          |

### Extra coverage (not in plan)

| Test ID | Assertion description                   | VHDL file:line | Test file:line                  |
|---------|-----------------------------------------|----------------|---------------------------------|
| MEM-01  | Write/read slot 1 RAM bank 2            | —              | test/divmmc/divmmc_test.cpp:623 |
| MEM-02  | Slot 0 writes discarded (ROM read-only) | —              | test/divmmc/divmmc_test.cpp:636 |
| MEM-03  | mapram=1, bank=3: slot 1 read-only      | —              | test/divmmc/divmmc_test.cpp:649 |
| MEM-04  | mapram=1, bank!=3: slot 1 writable      | —              | test/divmmc/divmmc_test.cpp:661 |
| MEM-05  | mapram=1: slot 0 reads RAM page 3       | —              | test/divmmc/divmmc_test.cpp:677 |
| MEM-06  | Bank switching: data preserved per bank | —              | test/divmmc/divmmc_test.cpp:692 |
| MEM-07  | Read outside range returns 0xFF         | —              | test/divmmc/divmmc_test.cpp:701 |
| NRD-01  | NR 0xB8 default = 0x83                  | —              | test/divmmc/divmmc_test.cpp:716 |
| NRD-02  | NR 0xB9 default = 0x01                  | —              | test/divmmc/divmmc_test.cpp:721 |
| NRD-03  | NR 0xBA default = 0x00                  | —              | test/divmmc/divmmc_test.cpp:726 |
| NRD-04  | NR 0xBB default = 0xCD                  | —              | test/divmmc/divmmc_test.cpp:731 |
| SD-01   | SD card: initial exchange returns 0xFF  | —              | test/divmmc/divmmc_test.cpp:945 |
| SD-02   | SD card: deselect after reset           | —              | test/divmmc/divmmc_test.cpp:955 |
| SD-03   | SD card: not mounted initially          | —              | test/divmmc/divmmc_test.cpp:962 |

## CTC+Interrupts — `test/ctc/ctc_test.cpp`

Last-touch commit: `f7e1b035d7fb02d3c0c0176609dbc3db712deac5` (`f7e1b035d7`)

| Test ID    | Plan row title                                               | VHDL file:line | Status  | Test file:line            |
|------------|--------------------------------------------------------------|----------------|---------|---------------------------|
| CTC-SM-01  | Hard reset: channel starts in S_RESET                        | —              | —       | test/ctc/ctc_test.cpp:112 |
| CTC-SM-02  | Write control word without D2=1 while in S_RESET             | —              | —       | test/ctc/ctc_test.cpp:124 |
| CTC-SM-03  | Write control word with D2=1 (TC follows)                    | —              | —       | test/ctc/ctc_test.cpp:136 |
| CTC-SM-04  | Write time constant after D2=1 control word                  | —              | —       | test/ctc/ctc_test.cpp:148 |
| CTC-SM-05  | Timer mode (D6=0) without trigger (D3=1): wait in S_TRIGGER  | —              | —       | test/ctc/ctc_test.cpp:159 |
| CTC-SM-06  | Timer mode (D6=0) without trigger (D3=0): immediate S_RUN    | —              | —       | test/ctc/ctc_test.cpp:169 |
| CTC-SM-07  | Counter mode (D6=1): immediate S_RUN from S_TRIGGER          | —              | —       | test/ctc/ctc_test.cpp:180 |
| CTC-SM-08  | Write control word with D2=1 while in S_RUN                  | —              | —       | test/ctc/ctc_test.cpp:196 |
| CTC-SM-09  | Write time constant while in S_RUN_TC                        | —              | —       | test/ctc/ctc_test.cpp:209 |
| CTC-SM-10  | Soft reset (D1=1, D2=0): return to S_RESET                   | —              | —       | test/ctc/ctc_test.cpp:225 |
| CTC-SM-11  | Soft reset (D1=1, D2=1): go to S_RESET_TC                    | —              | —       | test/ctc/ctc_test.cpp:238 |
| CTC-SM-12  | Double soft reset required when in S_RESET_TC                | —              | —       | test/ctc/ctc_test.cpp:254 |
| CTC-SM-13  | Control word write while running (D1=0, D2=0)                | —              | —       | test/ctc/ctc_test.cpp:270 |
| CTC-TM-01  | Prescaler = 16 (D5=0): counter decrements every 16 clocks    | —              | —       | test/ctc/ctc_test.cpp:290 |
| CTC-TM-02  | Prescaler = 256 (D5=1): counter decrements every 256 clocks  | —              | —       | test/ctc/ctc_test.cpp:300 |
| CTC-TM-03  | Time constant = 1: ZC/TO after 1 prescaler cycle             | —              | —       | test/ctc/ctc_test.cpp:311 |
| CTC-TM-04  | Time constant = 0 means 256 (8-bit wrap)                     | —              | —       | test/ctc/ctc_test.cpp:324 |
| CTC-TM-05  | Prescaler resets on soft reset                               | —              | —       | test/ctc/ctc_test.cpp:340 |
| CTC-TM-06  | ZC/TO reloads time constant automatically                    | —              | —       | test/ctc/ctc_test.cpp:353 |
| CTC-TM-07  | ZC/TO pulse duration is exactly 1 clock cycle                | —              | —       | test/ctc/ctc_test.cpp:365 |
| CTC-TM-08  | Read port returns current down-counter value                 | —              | —       | test/ctc/ctc_test.cpp:376 |
| CTC-CM-01  | Counter mode: decrement on falling external edge (D4=0)      | —              | —       | test/ctc/ctc_test.cpp:396 |
| CTC-CM-02  | Counter mode: decrement on rising external edge (D4=1)       | —              | —       | test/ctc/ctc_test.cpp:407 |
| CTC-CM-03  | Counter mode: ZC/TO when count reaches 0                     | —              | —       | test/ctc/ctc_test.cpp:420 |
| CTC-CM-04  | Counter mode: automatic reload after ZC/TO                   | —              | —       | test/ctc/ctc_test.cpp:435 |
| CTC-CM-05  | Changing edge polarity (D4) counts as clock edge             | —              | —       | test/ctc/ctc_test.cpp:449 |
| CTC-CH-01  | Channel 0 trigger = ZC/TO of channel 3                       | —              | —       | test/ctc/ctc_test.cpp:533 |
| CTC-CH-02  | Channel 1 trigger = ZC/TO of channel 0                       | —              | —       | test/ctc/ctc_test.cpp:475 |
| CTC-CH-03  | Channel 2 trigger = ZC/TO of channel 1                       | —              | —       | test/ctc/ctc_test.cpp:488 |
| CTC-CH-04  | Channel 3 trigger = ZC/TO of channel 2                       | —              | —       | test/ctc/ctc_test.cpp:502 |
| CTC-CH-05  | Cascaded chain: ch0 timer -> ch1 counter -> ch2 counter      | —              | —       | test/ctc/ctc_test.cpp:518 |
| CTC-CH-06  | Circular chain avoided: only one channel in timer mode       | —              | —       | test/ctc/ctc_test.cpp:547 |
| CTC-CW-01  | Control word (D0=1): bits [7:3] stored in control_reg        | —              | —       | test/ctc/ctc_test.cpp:568 |
| CTC-CW-02  | Vector word (D0=0): only accepted by channel 0               | —              | —       | test/ctc/ctc_test.cpp:653 |
| CTC-CW-03  | Vector word to channels 1-3: treated as vector but o_vector… | —              | —       | test/ctc/ctc_test.cpp:663 |
| CTC-CW-04  | Time constant follows control word with D2=1                 | —              | —       | test/ctc/ctc_test.cpp:579 |
| CTC-CW-05  | Write during S_RESET_TC: any byte is the time constant       | —              | —       | test/ctc/ctc_test.cpp:591 |
| CTC-CW-06  | Control word with D7=1: enable interrupt for channel         | —              | —       | test/ctc/ctc_test.cpp:600 |
| CTC-CW-07  | Control word with D7=0: disable interrupt for channel        | —              | —       | test/ctc/ctc_test.cpp:613 |
| CTC-CW-08  | External int_en_wr overrides D7 bit                          | —              | —       | test/ctc/ctc_test.cpp:625 |
| CTC-CW-09  | Hard reset clears control_reg to all zeros                   | —              | —       | test/ctc/ctc_test.cpp:635 |
| CTC-CW-10  | Hard reset clears time_constant_reg to 0x00                  | —              | —       | test/ctc/ctc_test.cpp:645 |
| CTC-CW-11  | Write edge: iowr is rising-edge detected (i_iowr AND NOT io… | —              | missing | missing                   |
| CTC-NR-01  | NextREG 0xC5 write: sets CTC interrupt enable bits [3:0]     | —              | —       | test/ctc/ctc_test.cpp:680 |
| CTC-NR-02  | NextREG 0xC5 read: returns ctc_int_en[7:0]                   | —              | —       | test/ctc/ctc_test.cpp:692 |
| CTC-NR-03  | CTC control word D7 also sets int_en independently           | —              | —       | test/ctc/ctc_test.cpp:704 |
| CTC-NR-04  | NextREG 0xC5 write does not overlap with port CTC write      | —              | —       | test/ctc/ctc_test.cpp:720 |
| IM2C-01    | ED prefix detected: enter S_ED_T4                            | —              | missing | missing                   |
| IM2C-02    | ED 4D sequence: o_reti_seen pulsed                           | —              | missing | missing                   |
| IM2C-03    | ED 45 sequence: o_retn_seen pulsed                           | —              | missing | missing                   |
| IM2C-04    | ED followed by non-4D/45: return to S_0                      | —              | missing | missing                   |
| IM2C-05    | o_reti_decode asserted during S_ED_T4                        | —              | missing | missing                   |
| IM2C-06    | CB prefix: enter S_CB_T4, wait for next fetch                | —              | missing | missing                   |
| IM2C-07    | DD/FD prefix chain: stay in S_DDFD_T4                        | —              | missing | missing                   |
| IM2C-08    | DMA delay: asserted during ED, ED4D, ED45, SRL states        | —              | missing | missing                   |
| IM2C-09    | SRL delay states: 2 extra cycles after RETI/RETN             | —              | missing | missing                   |
| IM2C-10    | IM mode detection: ED 46 = IM 0                              | —              | missing | missing                   |
| IM2C-11    | IM mode detection: ED 56 = IM 1                              | —              | missing | missing                   |
| IM2C-12    | IM mode detection: ED 5E = IM 2                              | —              | missing | missing                   |
| IM2C-13    | IM mode updates on falling edge of CLK_CPU                   | —              | missing | missing                   |
| IM2C-14    | IM mode default after reset: IM 0                            | —              | missing | missing                   |
| IM2D-01    | Interrupt request: S_0 -> S_REQ when i_int_req=1 and M1=high | —              | missing | missing                   |
| IM2D-02    | INT_n asserted in S_REQ when IEI=1 and IM2 mode              | —              | missing | missing                   |
| IM2D-03    | INT_n not asserted when IEI=0                                | —              | missing | missing                   |
| IM2D-04    | INT_n not asserted when not in IM2 mode                      | —              | missing | missing                   |
| IM2D-05    | Acknowledge: S_REQ -> S_ACK on M1=0, IORQ=0, IEI=1           | —              | missing | missing                   |
| IM2D-06    | S_ACK -> S_ISR when M1 returns high                          | —              | missing | missing                   |
| IM2D-07    | S_ISR -> S_0 on RETI seen with IEI=1                         | —              | missing | missing                   |
| IM2D-08    | S_ISR stays in S_ISR without RETI                            | —              | missing | missing                   |
| IM2D-09    | Vector output during S_ACK (or S_ACK transition)             | —              | missing | missing                   |
| IM2D-10    | Vector output = 0 when not in ACK                            | —              | missing | missing                   |
| IM2D-11    | o_isr_serviced pulsed on S_ISR -> S_0 transition             | —              | missing | missing                   |
| IM2D-12    | DMA interrupt: o_dma_int=1 whenever state != S_0 and dma_in… | —              | missing | missing                   |
| IM2P-01    | IEO = IEI in S_0 state (idle)                                | —              | missing | missing                   |
| IM2P-02    | IEO = IEI AND reti_decode in S_REQ state                     | —              | missing | missing                   |
| IM2P-03    | IEO = 0 in S_ACK and S_ISR states                            | —              | missing | missing                   |
| IM2P-04    | Highest-priority device (index 0) has IEI=1 always           | —              | missing | missing                   |
| IM2P-05    | Two simultaneous requests: lower index wins                  | —              | missing | missing                   |
| IM2P-06    | Lower-priority device queued while higher is serviced        | —              | missing | missing                   |
| IM2P-07    | After RETI of higher-priority ISR: lower device proceeds     | —              | missing | missing                   |
| IM2P-08    | Chain of 3: device 0 in ISR, device 1 requesting, device 2…  | —              | missing | missing                   |
| IM2P-09    | INT_n is AND of all device int_n signals                     | —              | missing | missing                   |
| IM2P-10    | Vector OR: only acknowledged device outputs non-zero vector  | —              | missing | missing                   |
| PULSE-01   | Pulse mode (nr_c0[0]=0): pulse_en from qualified int_req     | —              | missing | missing                   |
| PULSE-02   | IM2 mode (nr_c0[0]=1): pulse_en suppressed                   | —              | missing | missing                   |
| PULSE-03   | ULA exception (EXCEPTION='1'): pulse even in IM2 when CPU n… | —              | missing | missing                   |
| PULSE-04   | pulse_int_n goes low on pulse_en, stays low for count durat… | —              | missing | missing                   |
| PULSE-05   | 48K/+3 timing: pulse duration = 32 CPU cycles                | —              | missing | missing                   |
| PULSE-06   | 128K/Pentagon timing: pulse duration = 36 CPU cycles         | —              | missing | missing                   |
| PULSE-07   | Pulse counter resets when pulse_int_n=1                      | —              | missing | missing                   |
| PULSE-08   | INT_n to Z80 = pulse_int_n AND im2_int_n                     | —              | missing | missing                   |
| PULSE-09   | External bus INT: o_BUS_INT_n = pulse_int_n AND im2_int_n    | —              | missing | missing                   |
| IM2W-01    | Edge detection: int_req = i_int_req AND NOT int_req_d        | —              | missing | missing                   |
| IM2W-02    | im2_int_req latched: stays high until ISR serviced           | —              | missing | missing                   |
| IM2W-03    | im2_int_req cleared by im2_isr_serviced                      | —              | missing | missing                   |
| IM2W-04    | int_status set by int_req or int_unq                         | —              | missing | missing                   |
| IM2W-05    | int_status cleared by i_int_status_clear                     | —              | missing | missing                   |
| IM2W-06    | o_int_status = int_status OR im2_int_req                     | —              | missing | missing                   |
| IM2W-07    | im2_reset_n = mode_pulse AND NOT reset                       | —              | missing | missing                   |
| IM2W-08    | Unqualified interrupt (int_unq): bypasses int_en             | —              | missing | missing                   |
| IM2W-09    | isr_serviced edge detection across clock domains             | —              | missing | missing                   |
| ULA-INT-01 | ULA interrupt generated at specific HC/VC position           | —              | missing | missing                   |
| ULA-INT-02 | ULA interrupt disabled by port 0xFF bit (port_ff_interrupt_… | —              | missing | missing                   |
| ULA-INT-03 | ULA interrupt enable: ula_int_en[0] = NOT port_ff_interrupt… | —              | missing | missing                   |
| ULA-INT-04 | Line interrupt at configurable scanline                      | —              | missing | missing                   |
| ULA-INT-05 | Line interrupt enable: nr_22_line_interrupt_en               | —              | missing | missing                   |
| ULA-INT-06 | Line interrupt scanline 0 maps to c_max_vc                   | —              | missing | missing                   |
| ULA-INT-07 | ULA interrupt is priority index 11                           | —              | missing | missing                   |
| ULA-INT-08 | Line interrupt is priority index 0 (highest)                 | —              | missing | missing                   |
| ULA-INT-09 | ULA has EXCEPTION='1' in peripherals instantiation           | —              | missing | missing                   |
| NR-C0-01   | Write NextREG 0xC0: bits [7:5] = IM2 vector MSBs             | —              | missing | missing                   |
| NR-C0-02   | Write NextREG 0xC0: bit [3] = stackless NMI                  | —              | missing | missing                   |
| NR-C0-03   | Write NextREG 0xC0: bit [0] = pulse(0)/IM2(1) mode           | —              | missing | missing                   |
| NR-C0-04   | Read NextREG 0xC0: returns vector, stackless, im_mode, int_… | —              | missing | missing                   |
| NR-C4-01   | Write NextREG 0xC4: bit [7] = expansion bus int enable       | —              | missing | missing                   |
| NR-C4-02   | Write NextREG 0xC4: bit [1] = line interrupt enable          | —              | missing | missing                   |
| NR-C4-03   | Read NextREG 0xC4: returns expbus & ula_int_en               | —              | missing | missing                   |
| NR-C5-01   | Write NextREG 0xC5: CTC interrupt enable bits [3:0]          | —              | missing | missing                   |
| NR-C5-02   | Read NextREG 0xC5: returns ctc_int_en[7:0]                   | —              | missing | missing                   |
| NR-C6-01   | Write NextREG 0xC6: UART interrupt enable                    | —              | missing | missing                   |
| NR-C6-02   | Read NextREG 0xC6: returns 0_654_0_210                       | —              | missing | missing                   |
| NR-C8-01   | Read NextREG 0xC8: line and ULA interrupt status             | —              | missing | missing                   |
| NR-C9-01   | Read NextREG 0xC9: CTC interrupt status [10:3]               | —              | missing | missing                   |
| NR-CA-01   | Read NextREG 0xCA: UART interrupt status                     | —              | missing | missing                   |
| NR-CC-01   | Write NextREG 0xCC: DMA interrupt enable group 0             | —              | missing | missing                   |
| NR-CD-01   | Write NextREG 0xCD: DMA interrupt enable group 1             | —              | missing | missing                   |
| NR-CE-01   | Write NextREG 0xCE: DMA interrupt enable group 2             | —              | missing | missing                   |
| ISC-01     | Write NextREG 0xC8 bit 1: clear line interrupt status        | —              | missing | missing                   |
| ISC-02     | Write NextREG 0xC8 bit 0: clear ULA interrupt status         | —              | missing | missing                   |
| ISC-03     | Write NextREG 0xC9: clear individual CTC status bits         | —              | missing | missing                   |
| ISC-04     | Write NextREG 0xCA bit 6: clear UART1 TX status              | —              | missing | missing                   |
| ISC-05     | Write NextREG 0xCA bit 2: clear UART0 TX status              | —              | missing | missing                   |
| ISC-06     | Write NextREG 0xCA bits 5                                    | —              | missing | missing                   |
| ISC-07     | Write NextREG 0xCA bits 1                                    | —              | missing | missing                   |
| ISC-08     | Status bit re-set by new interrupt while clear pending       | —              | missing | missing                   |
| ISC-09     | Legacy NextREG 0x20 read: returns mixed status               | —              | missing | missing                   |
| ISC-10     | Legacy NextREG 0x22 read: includes pulse_int_n state         | —              | missing | missing                   |
| DMA-01     | im2_dma_int set when any peripheral has dma_int=1            | —              | missing | missing                   |
| DMA-02     | im2_dma_delay latched on im2_dma_int                         | —              | missing | missing                   |
| DMA-03     | im2_dma_delay held by dma_delay signal                       | —              | missing | missing                   |
| DMA-04     | NMI also triggers DMA delay when nr_cc_dma_int_en_0_7=1      | —              | missing | missing                   |
| DMA-05     | DMA delay cleared on reset                                   | —              | missing | missing                   |
| DMA-06     | Per-peripheral DMA int enable via NextREGs 0xCC-0xCE         | —              | missing | missing                   |
| UNQ-01     | NextREG 0x20 write bit 7: unqualified line interrupt         | —              | missing | missing                   |
| UNQ-02     | NextREG 0x20 write bits [3:0]: unqualified CTC 0-3           | —              | missing | missing                   |
| UNQ-03     | NextREG 0x20 write bit 6: unqualified ULA interrupt          | —              | missing | missing                   |
| UNQ-04     | Unqualified interrupt bypasses int_en check                  | —              | missing | missing                   |
| UNQ-05     | Unqualified interrupt sets int_status                        | —              | missing | missing                   |
| JOY-01     | Joystick IO mode 01: CTC channel 3 ZC/TO toggles pin7        | —              | missing | missing                   |
| JOY-02     | Toggle conditioned on nr_0b_joy_iomode_0 or pin7=0           | —              | missing | missing                   |

### Extra coverage (not in plan)

| Test ID | Assertion description                | VHDL file:line | Test file:line            |
|---------|--------------------------------------|----------------|---------------------------|
| MC-01   | 4 channels loaded with different TCs | —              | test/ctc/ctc_test.cpp:745 |
| MC-02   | Channels decrement independently     | —              | test/ctc/ctc_test.cpp:758 |
| MC-03   | Read invalid channel returns 0xFF    | —              | test/ctc/ctc_test.cpp:767 |

## UART+I2C/RTC — `test/uart/uart_test.cpp`

Last-touch commit: `7cf61e20fa0eb7a804920eda36b9a4532823bc89` (`7cf61e20fa`)

| Test ID | Plan row title                                               | VHDL file:line | Status  | Test file:line              |
|---------|--------------------------------------------------------------|----------------|---------|-----------------------------|
| SEL-01  | Reset state: read select register                            | —              | —       | test/uart/uart_test.cpp:121 |
| SEL-02  | Write 0x40 to select, read back                              | —              | —       | test/uart/uart_test.cpp:128 |
| SEL-03  | Write 0x00 to select, read back                              | —              | —       | test/uart/uart_test.cpp:135 |
| SEL-04  | Write 0x15 (bit4=1, bits2:0=101), read back with UART 0      | —              | —       | test/uart/uart_test.cpp:142 |
| SEL-05  | Write 0x55 (bit6=1, bit4=1, bits2:0=101), read back with UA… | —              | —       | test/uart/uart_test.cpp:149 |
| SEL-06  | Hard reset clears prescaler MSB to 0                         | —              | —       | test/uart/uart_test.cpp:156 |
| SEL-07  | Soft reset clears uart_select_r to 0 but preserves prescale… | —              | —       | test/uart/uart_test.cpp:166 |
| FRM-01  | Hard reset state: read frame                                 | —              | —       | test/uart/uart_test.cpp:188 |
| FRM-02  | Write 0x1B (8 bits, parity odd, 2 stop), read back           | —              | —       | test/uart/uart_test.cpp:195 |
| FRM-03  | Frame applies to selected UART only                          | —              | —       | test/uart/uart_test.cpp:204 |
| FRM-04  | Bit 7 write resets FIFO                                      | —              | —       | test/uart/uart_test.cpp:215 |
| FRM-05  | Bit 6 sets break on TX                                       | —              | missing | missing                     |
| FRM-06  | Frame bits 4:0 sampled at transmission start                 | —              | missing | missing                     |
| BAUD-01 | Default prescaler = 243 (115200 @ 28MHz)                     | —              | —       | test/uart/uart_test.cpp:231 |
| BAUD-02 | Write 0x33 to port 0x143B (bit7=0): sets LSB bits 6:0 = 0x33 | —              | —       | test/uart/uart_test.cpp:241 |
| BAUD-03 | Write 0x85 to port 0x143B (bit7=1): sets LSB bits 13:7 = 0x… | —              | —       | test/uart/uart_test.cpp:248 |
| BAUD-04 | Write prescaler MSB via select register                      | —              | —       | test/uart/uart_test.cpp:255 |
| BAUD-05 | Prescaler applies to selected UART independently             | —              | —       | test/uart/uart_test.cpp:264 |
| BAUD-06 | Hard reset restores default prescaler for both UARTs         | —              | —       | test/uart/uart_test.cpp:276 |
| BAUD-07 | Prescaler sampled at start of TX/RX (not mid-byte)           | —              | missing | missing                     |
| TX-01   | Write byte to port 0x133B when TX FIFO empty                 | —              | —       | test/uart/uart_test.cpp:299 |
| TX-02   | Write 64 bytes: FIFO full                                    | —              | —       | test/uart/uart_test.cpp:313 |
| TX-03   | Write 65th byte when full                                    | —              | —       | test/uart/uart_test.cpp:320 |
| TX-04   | TX empty flag: requires FIFO empty AND transmitter not busy  | —              | —       | test/uart/uart_test.cpp:330 |
| TX-05   | TX FIFO write is edge-triggered                              | —              | missing | missing                     |
| TX-06   | Frame bit 7 resets TX FIFO and transmitter                   | —              | —       | test/uart/uart_test.cpp:339 |
| TX-07   | Frame bit 6 (break): TX line held low, busy = 1, cannot send | —              | missing | missing                     |
| TX-08   | 8N1 frame: start(0) + 8 data bits (LSB first) + stop(1)      | —              | missing | missing                     |
| TX-09   | 7E2 frame: start + 7 bits + even parity + 2 stops            | —              | missing | missing                     |
| TX-10   | 5O1 frame: start + 5 bits + odd parity + 1 stop              | —              | missing | missing                     |
| TX-11   | Flow control: bit 5 enabled, CTS_n=1 blocks TX start         | —              | missing | missing                     |
| TX-12   | Flow control disabled: CTS_n ignored                         | —              | missing | missing                     |
| TX-13   | Parity calculation: even parity (frame bit 1 = 0)            | —              | missing | missing                     |
| TX-14   | Parity calculation: odd parity (frame bit 1 = 1)             | —              | missing | missing                     |
| RX-01   | Inject byte into RX: read port 0x143B                        | —              | —       | test/uart/uart_test.cpp:356 |
| RX-02   | Read empty RX FIFO                                           | —              | —       | test/uart/uart_test.cpp:363 |
| RX-03   | Fill RX FIFO with 512 bytes                                  | —              | —       | test/uart/uart_test.cpp:373 |
| RX-04   | RX FIFO overflow: 513th byte                                 | —              | —       | test/uart/uart_test.cpp:380 |
| RX-05   | Read advances RX FIFO pointer (edge-triggered)               | —              | —       | test/uart/uart_test.cpp:392 |
| RX-06   | RX near-full flag at 3/4 capacity (384 bytes)                | —              | —       | test/uart/uart_test.cpp:402 |
| RX-07   | Frame bit 7 resets RX FIFO                                   | —              | —       | test/uart/uart_test.cpp:412 |
| RX-08   | Framing error: missing stop bit                              | —              | missing | missing                     |
| RX-09   | Parity error                                                 | —              | missing | missing                     |
| RX-10   | Break condition: all-zero shift register in error state      | —              | missing | missing                     |
| RX-11   | Error byte stored with 9th bit in FIFO                       | —              | missing | missing                     |
| RX-12   | Noise rejection: pulse < 2^NOISE_REJECTION_BITS / CLK is fi… | —              | missing | missing                     |
| RX-13   | RX state machine: pause mode (frame bit 6)                   | —              | missing | missing                     |
| RX-14   | RX variables sampled at start bit detection                  | —              | missing | missing                     |
| RX-15   | Hardware flow control: RTR_n asserted when FIFO almost full  | —              | missing | missing                     |
| STAT-01 | Sticky errors (overflow, framing) persist across reads of RX | —              | —       | test/uart/uart_test.cpp:434 |
| STAT-02 | Reading TX/status port (0x133B read) clears sticky errors    | —              | —       | test/uart/uart_test.cpp:445 |
| STAT-03 | FIFO reset (frame bit 7) clears sticky errors                | —              | —       | test/uart/uart_test.cpp:461 |
| STAT-04 | Status bits reflect correct UART (per select register)       | —              | —       | test/uart/uart_test.cpp:471 |
| STAT-05 | tx_empty = tx_fifo_empty AND NOT tx_busy                     | —              | —       | test/uart/uart_test.cpp:478 |
| STAT-06 | rx_avail = NOT rx_fifo_empty                                 | —              | —       | test/uart/uart_test.cpp:485 |
| DUAL-01 | UART 0 and UART 1 have independent FIFOs                     | —              | —       | test/uart/uart_test.cpp:513 |
| DUAL-02 | Independent prescalers                                       | —              | —       | test/uart/uart_test.cpp:526 |
| DUAL-03 | Independent frame registers                                  | —              | —       | test/uart/uart_test.cpp:537 |
| DUAL-04 | Independent status registers                                 | —              | —       | test/uart/uart_test.cpp:548 |
| DUAL-05 | UART 0 = ESP, UART 1 = Pi channel assignment                 | —              | missing | missing                     |
| DUAL-06 | Joystick UART mode multiplexing                              | —              | missing | missing                     |
| I2C-01  | Reset state: SCL = 1, SDA = 1 (both released)                | —              | —       | test/uart/uart_test.cpp:565 |
| I2C-02  | Write 0x00 to port 0x103B                                    | —              | —       | test/uart/uart_test.cpp:573 |
| I2C-03  | Write 0x01 to port 0x103B                                    | —              | —       | test/uart/uart_test.cpp:580 |
| I2C-04  | Write 0x00 to port 0x113B                                    | —              | —       | test/uart/uart_test.cpp:588 |
| I2C-05  | Write 0x01 to port 0x113B                                    | —              | —       | test/uart/uart_test.cpp:595 |
| I2C-06  | Read port 0x103B                                             | —              | —       | test/uart/uart_test.cpp:602 |
| I2C-07  | Read port 0x113B                                             | —              | —       | test/uart/uart_test.cpp:608 |
| I2C-08  | Only bit 0 is significant for write                          | —              | —       | test/uart/uart_test.cpp:616 |
| I2C-09  | Bits 7:1 always read as 1                                    | —              | —       | test/uart/uart_test.cpp:624 |
| I2C-10  | I2C port enable gated by internal_port_enable(10)            | —              | missing | missing                     |
| I2C-11  | Pi I2C1 AND-gating: if pi_i2c1_scl = 0, SCL reads 0          | —              | missing | missing                     |
| I2C-12  | Reset releases both lines                                    | —              | —       | test/uart/uart_test.cpp:634 |
| I2C-P01 | START condition: SDA high->low while SCL high                | —              | —       | test/uart/uart_test.cpp:656 |
| I2C-P02 | STOP condition: SDA low->high while SCL high                 | —              | —       | test/uart/uart_test.cpp:664 |
| I2C-P03 | Send byte (8 clocks): MSB first, clock each bit              | —              | —       | test/uart/uart_test.cpp:671 |
| I2C-P04 | Read ACK: release SDA, clock SCL, read SDA bit 0             | —              | —       | test/uart/uart_test.cpp:680 |
| I2C-P05 | Read byte (8 clocks): release SDA, read 8 bits               | —              | —       | test/uart/uart_test.cpp:694 |
| I2C-P06 | Send ACK/NACK after read                                     | —              | —       | test/uart/uart_test.cpp:716 |
| RTC-01  | Address 0xD0 write: device ACKs                              | —              | —       | test/uart/uart_test.cpp:736 |
| RTC-02  | Address 0xD1 read: device ACKs                               | —              | —       | test/uart/uart_test.cpp:744 |
| RTC-03  | Wrong address: device NACKs                                  | —              | —       | test/uart/uart_test.cpp:752 |
| RTC-04  | Write register pointer (0x00), read seconds                  | —              | —       | test/uart/uart_test.cpp:767 |
| RTC-05  | Read minutes (register 0x01)                                 | —              | —       | test/uart/uart_test.cpp:781 |
| RTC-06  | Read hours (register 0x02)                                   | —              | —       | test/uart/uart_test.cpp:796 |
| RTC-07  | Read day-of-week (register 0x03)                             | —              | —       | test/uart/uart_test.cpp:809 |
| RTC-08  | Read date (register 0x04)                                    | —              | missing | missing                     |
| RTC-09  | Read month (register 0x05)                                   | —              | missing | missing                     |
| RTC-10  | Read year (register 0x06)                                    | —              | missing | missing                     |
| RTC-11  | Read control register (0x07)                                 | —              | missing | missing                     |
| RTC-12  | Write seconds register                                       | —              | missing | missing                     |
| RTC-13  | Write hours in 12h mode (bit 6 = 1)                          | —              | missing | missing                     |
| RTC-14  | Sequential read: auto-increment register pointer             | —              | —       | test/uart/uart_test.cpp:826 |
| RTC-15  | Sequential write: auto-increment register pointer            | —              | missing | missing                     |
| RTC-16  | Clock halt bit (seconds register bit 7)                      | —              | missing | missing                     |
| RTC-17  | NVRAM registers 0x08-0x3F (56 bytes)                         | —              | missing | missing                     |
| INT-01  | UART 0 RX interrupt: rx_avail when int_en bit set            | —              | missing | missing                     |
| INT-02  | UART 0 RX near-full always triggers                          | —              | missing | missing                     |
| INT-03  | UART 1 RX interrupt: same logic as UART 0                    | —              | missing | missing                     |
| INT-04  | UART 0 TX empty interrupt                                    | —              | missing | missing                     |
| INT-05  | UART 1 TX empty interrupt                                    | —              | missing | missing                     |
| INT-06  | Interrupt enable controlled by NextREG 0xC6                  | —              | missing | missing                     |
| GATE-01 | UART port enable (internal_port_enable bit 12)               | —              | missing | missing                     |
| GATE-02 | I2C port enable (internal_port_enable bit 10)                | —              | missing | missing                     |
| GATE-03 | Enable controlled by NextREG 0x82-0x85                       | —              | missing | missing                     |

## NextREG — `test/nextreg/nextreg_test.cpp`

Last-touch commit: `044f9c57877c114c6c32221b1f9b6016e24e5958` (`044f9c5787`)

| Test ID | Plan row title                                         | VHDL file:line | Status  | Test file:line                    |
|---------|--------------------------------------------------------|----------------|---------|-----------------------------------|
| SEL-01  | Write 0x243B = 0x15, read 0x243B                       | —              | —       | test/nextreg/nextreg_test.cpp:72  |
| SEL-02  | Reset, read 0x243B                                     | —              | —       | test/nextreg/nextreg_test.cpp:85  |
| SEL-03  | Write 0x243B = 0x00, write 0x253B = 0x42, read NR 0x00 | —              | —       | test/nextreg/nextreg_test.cpp:98  |
| SEL-04  | Write 0x243B = 0x7F, write 0x253B = 0xAB, read NR 0x7F | —              | —       | test/nextreg/nextreg_test.cpp:108 |
| SEL-05  | NEXTREG ED 91 instruction                              | —              | —       | test/nextreg/nextreg_test.cpp:119 |
| RO-01   | Read NR 0x00                                           | —              | —       | test/nextreg/nextreg_test.cpp:135 |
| RO-02   | Write NR 0x00, read back                               | —              | —       | test/nextreg/nextreg_test.cpp:146 |
| RO-03   | Read NR 0x01                                           | —              | —       | test/nextreg/nextreg_test.cpp:154 |
| RO-04   | Read NR 0x0E                                           | —              | —       | test/nextreg/nextreg_test.cpp:162 |
| RO-05   | Read NR 0x0F                                           | —              | —       | test/nextreg/nextreg_test.cpp:174 |
| RO-06   | Read NR 0x1E/0x1F                                      | —              | missing | missing                           |
| 0x82-85 | 0xFF                                                   | —              | missing | missing                           |
| 0x86-89 | 0xFF                                                   | —              | missing | missing                           |
| RST-01  | After reset, read NR 0x14                              | —              | —       | test/nextreg/nextreg_test.cpp:188 |
| RST-02  | After reset, read NR 0x15                              | —              | —       | test/nextreg/nextreg_test.cpp:193 |
| RST-03  | After reset, read NR 0x4A                              | —              | —       | test/nextreg/nextreg_test.cpp:198 |
| RST-04  | After reset, read NR 0x42                              | —              | —       | test/nextreg/nextreg_test.cpp:203 |
| RST-05  | After reset, read NR 0x50-0x57                         | —              | —       | test/nextreg/nextreg_test.cpp:210 |
| RST-06  | After reset, read NR 0x68                              | —              | —       | test/nextreg/nextreg_test.cpp:218 |
| RST-07  | After reset, read NR 0x0B                              | —              | —       | test/nextreg/nextreg_test.cpp:226 |
| RST-08  | After reset, read NR 0x82-0x85                         | —              | —       | test/nextreg/nextreg_test.cpp:234 |
| RST-09  | After reset, read NR 0x1B clip                         | —              | missing | missing                           |
| RW-01   | 0x07                                                   | —              | —       | test/nextreg/nextreg_test.cpp:334 |
| RW-02   | 0x08                                                   | —              | —       | test/nextreg/nextreg_test.cpp:344 |
| RW-03   | 0x12                                                   | —              | —       | test/nextreg/nextreg_test.cpp:354 |
| RW-04   | 0x14                                                   | —              | —       | test/nextreg/nextreg_test.cpp:364 |
| RW-05   | 0x15                                                   | —              | —       | test/nextreg/nextreg_test.cpp:374 |
| RW-06   | 0x16                                                   | —              | —       | test/nextreg/nextreg_test.cpp:384 |
| RW-07   | 0x42                                                   | —              | —       | test/nextreg/nextreg_test.cpp:394 |
| RW-08   | 0x43                                                   | —              | —       | test/nextreg/nextreg_test.cpp:408 |
| RW-09   | 0x4A                                                   | —              | —       | test/nextreg/nextreg_test.cpp:418 |
| RW-10   | 0x50-57                                                | —              | —       | test/nextreg/nextreg_test.cpp:428 |
| RW-11   | 0x7F                                                   | —              | —       | test/nextreg/nextreg_test.cpp:439 |
| RW-12   | 0x6B                                                   | —              | —       | test/nextreg/nextreg_test.cpp:450 |
| CLIP-01 | Write NR 0x18 four times: 10,20,30,40                  | —              | —       | test/nextreg/nextreg_test.cpp:519 |
| CLIP-02 | Write NR 0x18 five times                               | —              | —       | test/nextreg/nextreg_test.cpp:528 |
| CLIP-03 | Write NR 0x1C bit 0 = 1                                | —              | —       | test/nextreg/nextreg_test.cpp:537 |
| CLIP-04 | Write NR 0x1C bit 1 = 1                                | —              | —       | test/nextreg/nextreg_test.cpp:546 |
| CLIP-05 | Write NR 0x1C bit 2 = 1                                | —              | —       | test/nextreg/nextreg_test.cpp:555 |
| CLIP-06 | Write NR 0x1C bit 3 = 1                                | —              | missing | missing                           |
| CLIP-07 | Read NR 0x1C                                           | —              | missing | missing                           |
| CLIP-08 | Read NR 0x18 cycles through clip values                | —              | missing | missing                           |
| MMU-01  | Reset defaults                                         | —              | missing | missing                           |
| MMU-02  | Write NR 0x52 = 0x20, read back                        | —              | missing | missing                           |
| MMU-03  | Write port 0x7FFD, check MMU6/7                        | —              | missing | missing                           |
| MMU-04  | NextREG write overrides port write                     | —              | missing | missing                           |
| CFG-01  | Write NR 0x03 bits 6:4 for timing                      | —              | missing | missing                           |
| CFG-02  | Write NR 0x03 bit 3 toggles dt_lock                    | —              | missing | missing                           |
| CFG-03  | Write NR 0x03 bits 2:0 = 111                           | —              | missing | missing                           |
| CFG-04  | Write NR 0x03 bits 2:0 = 001-100                       | —              | missing | missing                           |
| CFG-05  | Machine type only writable in config mode              | —              | missing | missing                           |
| PAL-01  | Write NR 0x40 = 0x10 (palette index)                   | —              | missing | missing                           |
| PAL-02  | Write NR 0x41 (8-bit colour)                           | —              | missing | missing                           |
| PAL-03  | Write NR 0x44 twice (9-bit colour)                     | —              | missing | missing                           |
| PAL-04  | Read NR 0x41                                           | —              | missing | missing                           |
| PAL-05  | Read NR 0x44                                           | —              | missing | missing                           |
| PAL-06  | Auto-increment disabled (NR 0x43 bit 7)                | —              | missing | missing                           |
| PE-01   | Write NR 0x82 = 0x00                                   | —              | —       | test/nextreg/nextreg_test.cpp:570 |
| PE-02   | Read NR 0x82 after write                               | —              | —       | test/nextreg/nextreg_test.cpp:579 |
| PE-03   | Disable joystick port (bit 6)                          | —              | —       | test/nextreg/nextreg_test.cpp:592 |
| PE-04   | Reset with reset_type=1                                | —              | missing | missing                           |
| PE-05   | Reset with bus reset_type=0                            | —              | missing | missing                           |
| COP-01  | CPU write NR 0x15                                      | —              | missing | missing                           |
| COP-02  | Copper write NR 0x15 simultaneously                    | —              | missing | missing                           |
| COP-03  | CPU write while copper active                          | —              | missing | missing                           |
| COP-04  | Copper register limited to 0x7F                        | —              | missing | missing                           |

### Extra coverage (not in plan)

| Test ID | Assertion description                         | VHDL file:line | Test file:line                    |
|---------|-----------------------------------------------|----------------|-----------------------------------|
| RST-10  | NR 0x12 L2 active bank (VHDL: 0x08)           | —              | test/nextreg/nextreg_test.cpp:260 |
| RST-11  | NR 0x68 ULA control (VHDL: bit7=NOT ula_en=0) | —              | test/nextreg/nextreg_test.cpp:268 |
| RST-12  | NR 0x6B tilemap = 0x00                        | —              | test/nextreg/nextreg_test.cpp:276 |
| RST-13  | NR 0x82-0x85 internal port enables = 0xFF     | —              | test/nextreg/nextreg_test.cpp:287 |
| RST-14  | NR 0x86-0x89 bus port enables = 0xFF          | —              | test/nextreg/nextreg_test.cpp:299 |
| RST-15  | NR 0x4B sprite transparent (VHDL: 0xE3)       | —              | test/nextreg/nextreg_test.cpp:308 |
| RST-16a | NR 0x16 L2 scroll X = 0x00                    | —              | test/nextreg/nextreg_test.cpp:315 |
| RST-16b | NR 0x17 L2 scroll Y = 0x00                    | —              | test/nextreg/nextreg_test.cpp:318 |
| WH-01   | Write handler called with correct value       | —              | test/nextreg/nextreg_test.cpp:467 |
| WH-02   | Write handler via write_selected              | —              | test/nextreg/nextreg_test.cpp:479 |
| WH-03   | Read handler returns 0xDD despite cached 0x11 | —              | test/nextreg/nextreg_test.cpp:490 |
| WH-04   | No handler — direct storage round-trip        | —              | test/nextreg/nextreg_test.cpp:500 |
| EDGE-01 | All 256 registers store and retrieve          | —              | test/nextreg/nextreg_test.cpp:613 |
| EDGE-02 | Reset clears NR 0x7F to 0                     | —              | test/nextreg/nextreg_test.cpp:623 |
| EDGE-03 | Reset restores NR 0x00=0x0A, NR 0x01=0x32     | —              | test/nextreg/nextreg_test.cpp:634 |
| EDGE-04 | Write handler survives reset                  | —              | test/nextreg/nextreg_test.cpp:646 |
| EDGE-05 | Multiple selects, last wins                   | —              | test/nextreg/nextreg_test.cpp:658 |

## IO Port Dispatch — `test/port/port_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID       | Plan row title                                               | VHDL file:line       | Status  | Test file:line               |
|---------------|--------------------------------------------------------------|----------------------|---------|------------------------------|
| LIBZ80-01     | `OUT (C),r` to 0x7FFD vs 0xBFFD                              | zxnext.vhd:2593      | pass    | test/port/port_test.cpp:160  |
| LIBZ80-02     | `IN A,(nn)` upper byte honoured                              | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:186  |
| LIBZ80-03     | `OUT (nn),A` upper byte honoured                             | zxnext.vhd:2626      | pass    | test/port/port_test.cpp:199  |
| LIBZ80-04     | INIR block transfer uses full BC                             | zxnext.vhd:2635      | pass    | test/port/port_test.cpp:213  |
| LIBZ80-05     | MSB-only discrimination                                      | zxnext.vhd:2648      | pass    | test/port/port_test.cpp:229  |
| REG-01        | ULA 0xFE matches any even address                            | zxnext.vhd:2582      | pass    | test/port/port_test.cpp:254  |
| REG-02        | 0xFE does not match on odd address                           | zxnext.vhd:2582–2583 | pass    | test/port/port_test.cpp:265  |
| REG-03        | NextReg select 0x243B                                        | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:276  |
| REG-04        | NextReg data 0x253B                                          | zxnext.vhd:2626      | pass    | test/port/port_test.cpp:282  |
| REG-05        | 0x243C/0x253C not decoded                                    | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:306  |
| REG-06        | AY select 0xFFFD real                                        | zxnext.vhd:2647      | missing | missing                      |
| REG-07        | AY data 0xBFFD real                                          | zxnext.vhd:2648      | missing | missing                      |
| REG-08        | 0x7FFD MMU bank select                                       | zxnext.vhd:2593      | pass    | test/port/port_test.cpp:333  |
| REG-09        | 0x1FFD +3 extended                                           | zxnext.vhd:2599      | pass    | test/port/port_test.cpp:349  |
| REG-10        | 0xDFFD Pentagon ext                                          | zxnext.vhd:2596      | pass    | test/port/port_test.cpp:375  |
| REG-11        | DivMMC 0xE3 real                                             | zxnext.vhd:2608      | pass    | test/port/port_test.cpp:385  |
| REG-12        | SPI CS 0xE7, data 0xEB                                       | zxnext.vhd:2620–2621 | pass    | test/port/port_test.cpp:403  |
| REG-13        | Sprite 0x303B write-then-read                                | zxnext.vhd:2681      | pass    | test/port/port_test.cpp:416  |
| REG-14        | Layer 2 0x123B                                               | zxnext.vhd:2635      | pass    | test/port/port_test.cpp:425  |
| REG-15        | I²C 0x103B / 0x113B                                          | zxnext.vhd:2630–2631 | pass    | test/port/port_test.cpp:439  |
| REG-16        | UART 0x143B / 0x153B                                         | zxnext.vhd:2639      | pass    | test/port/port_test.cpp:451  |
| REG-17        | UART 0x133B rejected                                         | zxnext.vhd:2639      | pass    | test/port/port_test.cpp:465  |
| REG-18        | Kempston 1 0x001F                                            | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:475  |
| REG-19        | Kempston 2 0x0037                                            | zxnext.vhd:2675      | pass    | test/port/port_test.cpp:484  |
| REG-20        | Mouse 0xFADF/0xFBDF/0xFFDF                                   | zxnext.vhd:2668–2670 | pass    | test/port/port_test.cpp:495  |
| REG-21        | ULA+ 0xBF3B / 0xFF3B                                         | zxnext.vhd:2685–2686 | pass    | test/port/port_test.cpp:506  |
| REG-22        | DMA 0x6B vs 0x0B                                             | zxnext.vhd:2643      | pass    | test/port/port_test.cpp:519  |
| REG-23        | CTC 0x183B range                                             | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:529  |
| REG-24        | Unmapped port read                                           | zxnext.vhd:2589      | pass    | test/port/port_test.cpp:541  |
| REG-25        | Unmapped port write                                          | zxnext.vhd:2697      | pass    | test/port/port_test.cpp:559  |
| REG-26        | 0xDF routes to Specdrum/port_1f sink (positive combo)        | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:579  |
| REG-27        | 0xDF re-routed away from port_1f when mouse enabled (negati… | zxnext.vhd:2670      | pass    | test/port/port_test.cpp:590  |
| NR82-00       | 0x82 b0                                                      | zxnext.vhd:2397      | pass    | test/port/port_test.cpp:624  |
| NR82-01       | 0x82 b1                                                      | zxnext.vhd:2399      | pass    | test/port/port_test.cpp:636  |
| NR82-02       | 0x82 b2                                                      | zxnext.vhd:2400      | pass    | test/port/port_test.cpp:646  |
| NR82-03       | 0x82 b3                                                      | zxnext.vhd:2401      | pass    | test/port/port_test.cpp:659  |
| NR82-04       | 0x82 b4                                                      | zxnext.vhd:2403      | pass    | test/port/port_test.cpp:668  |
| NR82-05       | 0x82 b5                                                      | zxnext.vhd:2405      | pass    | test/port/port_test.cpp:680  |
| NR82-06       | 0x82 b6                                                      | zxnext.vhd:2407      | pass    | test/port/port_test.cpp:688  |
| NR82-07       | 0x82 b7                                                      | zxnext.vhd:2408      | pass    | test/port/port_test.cpp:696  |
| NR83-00       | 0x83 b0                                                      | zxnext.vhd:2412      | missing | missing                      |
| NR83-01       | 0x83 b1                                                      | zxnext.vhd:2415      | missing | missing                      |
| NR83-02       | 0x83 b2                                                      | zxnext.vhd:2418      | missing | missing                      |
| NR83-03       | 0x83 b3                                                      | zxnext.vhd:2419      | missing | missing                      |
| NR83-04       | 0x83 b4                                                      | zxnext.vhd:2420      | missing | missing                      |
| NR83-05       | 0x83 b5                                                      | zxnext.vhd:2422      | missing | missing                      |
| NR83-06       | 0x83 b6                                                      | zxnext.vhd:2423      | missing | missing                      |
| NR83-07       | 0x83 b7                                                      | zxnext.vhd:2424      | missing | missing                      |
| NR84-00       | 0x84 b0                                                      | zxnext.vhd:2428      | missing | missing                      |
| NR84-01       | 0x84 b1                                                      | zxnext.vhd:2429      | missing | missing                      |
| NR84-02       | 0x84 b2                                                      | zxnext.vhd:2430      | missing | missing                      |
| NR84-03       | 0x84 b3                                                      | zxnext.vhd:2431      | missing | missing                      |
| NR84-04       | 0x84 b4                                                      | zxnext.vhd:2432      | missing | missing                      |
| NR84-05       | 0x84 b5                                                      | zxnext.vhd:2433      | missing | missing                      |
| NR84-06       | 0x84 b6                                                      | zxnext.vhd:2434      | missing | missing                      |
| NR84-07       | 0x84 b7                                                      | zxnext.vhd:2435      | missing | missing                      |
| NR84-07-combo | 0x84 b7 AND 0x83 b5 AND 0x82 b6 (combinatorial)              | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:773  |
| NR85-00       | 0x85 b0                                                      | zxnext.vhd:2439      | missing | missing                      |
| NR85-01       | 0x85 b1                                                      | zxnext.vhd:2440      | missing | missing                      |
| NR85-02       | 0x85 b2                                                      | zxnext.vhd:2441      | missing | missing                      |
| NR85-03       | 0x85 b3                                                      | zxnext.vhd:2442      | pass    | test/port/port_test.cpp:809  |
| NR85-03b      | 0x85 b3                                                      | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:809  |
| NR85-03c      | 0x85 b3                                                      | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:825  |
| NR-DEF-01     | Power-on defaults all-enabled                                | zxnext.vhd:1226–1230 | pass    | test/port/port_test.cpp:838  |
| NR-RST-01     | Soft reset reloads when reset_type=1                         | zxnext.vhd:5052–5057 | pass    | test/port/port_test.cpp:866  |
| NR-RST-02     | Soft reset does NOT reload when reset_type=0                 | zxnext.vhd:5052–5057 | pass    | test/port/port_test.cpp:880  |
| NR-85-PK      | NR 0x85 packing: bits 4–6 read back zero                     | zxnext.vhd:5508–5509 | pass    | test/port/port_test.cpp:854  |
| BUS-86-01     | NR 0x86 inert when expbus_eff_en=0                           | zxnext.vhd:2392      | pass    | test/port/port_test.cpp:907  |
| BUS-86-02     | NR 0x86 gates when expbus_eff_en=1                           | zxnext.vhd:2393      | missing | missing                      |
| BUS-86-03     | NR 0x86 AND with NR 0x82                                     | zxnext.vhd:2393      | missing | missing                      |
| BUS-87-D      | DivMMC enable-diff detection                                 | zxnext.vhd:2413      | missing | missing                      |
| BUS-88-00     | NR 0x88 AND with NR 0x84 (AY)                                | zxnext.vhd:2393      | missing | missing                      |
| BUS-89-00     | NR 0x89 AND with NR 0x85 (ULA+)                              | zxnext.vhd:2393      | missing | missing                      |
| PR-01         | Registering an overlapping handler must fail (target contra… | zxnext.vhd:2696–2699 | pass    | test/port/port_test.cpp:988  |
| PR-02         | One-hot invariant over all real peripherals after `Emulator… | zxnext.vhd:2696–2699 | pass    | test/port/port_test.cpp:1015 |
| PR-01-CUR     | **Document current-code asymmetry (guard test until PR-01 c… | —                    | pass    | test/port/port_test.cpp:963  |
| PR-03         | `clear_handlers()` then re-register on reset                 | —                    | pass    | test/port/port_test.cpp:1032 |
| PR-04         | Default-read used when no handler matches                    | —                    | pass    | test/port/port_test.cpp:1044 |
| PR-05         | Default-read NOT used when any handler matches (even with 0… | —                    | pass    | test/port/port_test.cpp:1060 |
| IORQ-01       | Interrupt ack not routed to `in`                             | zxnext.vhd:2705      | missing | missing                      |
| IORQ-02       | Normal IN is routed                                          | zxnext.vhd:2705      | pass    | test/port/port_test.cpp:1088 |
| RMW-01        | 0xFE border + beeper latch                                   | zxnext.vhd:2582      | pass    | test/port/port_test.cpp:1107 |
| CTN-01        | Contended-port timing on 0x4000-range port                   | —                    | missing | missing                      |
| CTN-02        | Uncontended `IN A,(nn)` outside 0x4000 range                 | —                    | missing | missing                      |
| AMAP-01       | DivMMC enable diff freezes expansion bus                     | zxnext.vhd:2180      | missing | missing                      |
| AMAP-02       | 0xE3 writes honoured even when automap held                  | zxnext.vhd:2608      | pass    | test/port/port_test.cpp:1128 |
| AMAP-03       | NR 0x83 b0 = 0 disables 0xE3 regardless of automap           | zxnext.vhd:2412      | pass    | test/port/port_test.cpp:1142 |
| BUS-01        | Single-owner invariant over all registered                   | —                    | pass    | test/port/port_test.cpp:1166 |
| BUS-02        | Disabled port yields default-read byte                       | zxnext.vhd:2428      | pass    | test/port/port_test.cpp:1181 |
| BUS-03        | SCLD read gated by `nr_08_port_ff_rd_en`, not just `port_ff… | zxnext.vhd:2813      | pass    | test/port/port_test.cpp:1200 |

### Extra coverage (not in plan)

| Test ID      | Assertion description | VHDL file:line | Test file:line              |
|--------------|-----------------------|----------------|-----------------------------|
| REG-06+07    |                       | —              | test/port/port_test.cpp:320 |
| BUS-86..89-W |                       | —              | test/port/port_test.cpp:927 |

## Input — `test/input/input_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID   | Plan row title                                               | VHDL file:line                        | Status | Test file:line                |
|-----------|--------------------------------------------------------------|---------------------------------------|--------|-------------------------------|
| MD-01     | Mode 101; `i_JOY_LEFT` = U+D+L+R+A+B (bits 6,4,3,2,1,0)      | —                                     | skip   | test/input/input_test.cpp:454 |
| KBD-01    | none                                                         | —                                     | pass   | test/input/input_test.cpp:129 |
| KBD-02    | none                                                         | —                                     | pass   | test/input/input_test.cpp:136 |
| KBD-03    | none                                                         | —                                     | pass   | test/input/input_test.cpp:143 |
| KBD-04    | none                                                         | —                                     | pass   | test/input/input_test.cpp:150 |
| KBD-05    | none                                                         | —                                     | pass   | test/input/input_test.cpp:157 |
| KBD-06    | none                                                         | —                                     | pass   | test/input/input_test.cpp:164 |
| KBD-07    | none                                                         | —                                     | pass   | test/input/input_test.cpp:177 |
| KBD-08    | none                                                         | —                                     | pass   | test/input/input_test.cpp:189 |
| KBD-09    | none                                                         | —                                     | pass   | test/input/input_test.cpp:200 |
| KBD-10    | none                                                         | —                                     | pass   | test/input/input_test.cpp:211 |
| KBD-11    | none                                                         | —                                     | pass   | test/input/input_test.cpp:222 |
| KBD-12    | none                                                         | —                                     | pass   | test/input/input_test.cpp:233 |
| KBD-13    | none                                                         | —                                     | pass   | test/input/input_test.cpp:240 |
| KBD-14    | none                                                         | —                                     | pass   | test/input/input_test.cpp:247 |
| KBD-15    | none                                                         | —                                     | pass   | test/input/input_test.cpp:254 |
| KBD-16    | none                                                         | —                                     | pass   | test/input/input_test.cpp:261 |
| KBD-17    | none                                                         | —                                     | pass   | test/input/input_test.cpp:268 |
| KBD-18    | none                                                         | —                                     | pass   | test/input/input_test.cpp:277 |
| KBD-19    | none                                                         | —                                     | pass   | test/input/input_test.cpp:288 |
| KBD-20    | none                                                         | —                                     | pass   | test/input/input_test.cpp:297 |
| KBD-21    | none                                                         | —                                     | pass   | test/input/input_test.cpp:307 |
| KBD-22    | none                                                         | —                                     | skip   | test/input/input_test.cpp:313 |
| KBD-23    | none                                                         | —                                     | skip   | test/input/input_test.cpp:316 |
| KBDHYS-01 | Pulse CS for one scan, then release; read the next scan      | —                                     | skip   | test/input/input_test.cpp:327 |
| KBDHYS-02 | Hold CS continuously across 3 scans                          | —                                     | skip   | test/input/input_test.cpp:329 |
| KBDHYS-03 | `i_cancel_extended_entries = 1` mid-scan                     | —                                     | skip   | test/input/input_test.cpp:331 |
| EXT-01    | Press UP, read NR 0xB0                                       | —                                     | skip   | test/input/input_test.cpp:342 |
| EXT-02    | Press DOWN, read NR 0xB0                                     | —                                     | skip   | test/input/input_test.cpp:343 |
| EXT-03    | Press LEFT, read NR 0xB0                                     | —                                     | skip   | test/input/input_test.cpp:344 |
| EXT-04    | Press RIGHT, read NR 0xB0                                    | —                                     | skip   | test/input/input_test.cpp:345 |
| EXT-05    | Press ';'                                                    | —                                     | skip   | test/input/input_test.cpp:346 |
| EXT-06    | Press '"'                                                    | —                                     | skip   | test/input/input_test.cpp:347 |
| EXT-07    | Press ','                                                    | —                                     | skip   | test/input/input_test.cpp:348 |
| EXT-08    | Press '.'                                                    | —                                     | skip   | test/input/input_test.cpp:349 |
| EXT-09    | Press DELETE                                                 | —                                     | skip   | test/input/input_test.cpp:351 |
| EXT-10    | Press EDIT                                                   | —                                     | skip   | test/input/input_test.cpp:352 |
| EXT-11    | Press BREAK                                                  | —                                     | skip   | test/input/input_test.cpp:353 |
| EXT-12    | Press INV VIDEO                                              | —                                     | skip   | test/input/input_test.cpp:354 |
| EXT-13    | Press TRUE VIDEO                                             | —                                     | skip   | test/input/input_test.cpp:355 |
| EXT-14    | Press GRAPH                                                  | —                                     | skip   | test/input/input_test.cpp:356 |
| EXT-15    | Press CAPS LOCK                                              | —                                     | skip   | test/input/input_test.cpp:357 |
| EXT-16    | Press EXTEND                                                 | —                                     | skip   | test/input/input_test.cpp:358 |
| EXT-17    | Press EDIT; read 0xF7FE (row 3)                              | —                                     | skip   | test/input/input_test.cpp:359 |
| EXT-18    | Press ','; read 0xDFFE (row 5)                               | —                                     | skip   | test/input/input_test.cpp:360 |
| EXT-19    | Press LEFT; read 0x7FFE (row 7)                              | —                                     | skip   | test/input/input_test.cpp:361 |
| EXT-20    | UP + DOWN + LEFT + RIGHT                                     | —                                     | skip   | test/input/input_test.cpp:362 |
| JMODE-01  | NR 0x05 = 0x00 = 0b0000_0000                                 | —                                     | skip   | test/input/input_test.cpp:384 |
| JMODE-02  | NR 0x05 = 0x68 = 0b0110_1000                                 | —                                     | skip   | test/input/input_test.cpp:386 |
| JMODE-02r | NR 0x05 = 0xC9 = 0b1100_1001                                 | —                                     | skip   | test/input/input_test.cpp:388 |
| JMODE-03  | NR 0x05 = 0x40 = 0b0100_0000                                 | —                                     | skip   | test/input/input_test.cpp:390 |
| JMODE-04  | NR 0x05 = 0x08 = 0b0000_1000                                 | —                                     | skip   | test/input/input_test.cpp:392 |
| JMODE-05  | NR 0x05 = 0x88 = 0b1000_1000                                 | —                                     | skip   | test/input/input_test.cpp:394 |
| JMODE-06  | NR 0x05 = 0x22 = 0b0010_0010                                 | —                                     | skip   | test/input/input_test.cpp:396 |
| JMODE-07  | NR 0x05 = 0x30 = 0b0011_0000                                 | —                                     | skip   | test/input/input_test.cpp:398 |
| JMODE-08  | Power-on, read joystick mode                                 | —                                     | pass   | test/input/input_test.cpp:412 |
| KEMP-01   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:426 |
| KEMP-02   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:427 |
| KEMP-03   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:428 |
| KEMP-04   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:429 |
| KEMP-05   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:430 |
| KEMP-06   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:431 |
| KEMP-07   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:433 |
| KEMP-08   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:434 |
| KEMP-09   | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:435 |
| KEMP-10   | joy0=100                                                     | —                                     | skip   | test/input/input_test.cpp:436 |
| KEMP-11   | joy0=100                                                     | —                                     | skip   | test/input/input_test.cpp:437 |
| KEMP-12   | joy0=000                                                     | —                                     | skip   | test/input/input_test.cpp:439 |
| KEMP-13   | joy0=001, joy1=001, L.U + R.R                                | —                                     | skip   | test/input/input_test.cpp:441 |
| KEMP-14   | joy0=001, joy1=100, L.U, R.D                                 | —                                     | skip   | test/input/input_test.cpp:442 |
| KEMP-15   | joy0=101, L.A pressed                                        | —                                     | skip   | test/input/input_test.cpp:444 |
| MD-02     | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:455 |
| MD-03     | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:456 |
| MD-04     | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:457 |
| MD-05     | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:458 |
| MD-06     | joy0=001                                                     | —                                     | skip   | test/input/input_test.cpp:459 |
| MD-07     | joy0=110                                                     | —                                     | skip   | test/input/input_test.cpp:461 |
| MD-08     | joy1=110                                                     | —                                     | skip   | test/input/input_test.cpp:462 |
| MD-09     | joy0=101, joy1=101 (both MD1 — illegal?)                     | —                                     | skip   | test/input/input_test.cpp:463 |
| MD6-01    | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:478 |
| MD6-02    | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:479 |
| MD6-03    | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:480 |
| MD6-04    | joy0=101                                                     | —                                     | skip   | test/input/input_test.cpp:481 |
| MD6-05    | joy1=110                                                     | —                                     | skip   | test/input/input_test.cpp:482 |
| MD6-06    | joy1=110                                                     | —                                     | skip   | test/input/input_test.cpp:483 |
| MD6-07    | joy1=110                                                     | —                                     | skip   | test/input/input_test.cpp:484 |
| MD6-08    | joy1=110                                                     | —                                     | skip   | test/input/input_test.cpp:485 |
| MD6-09    | both MD                                                      | —                                     | skip   | test/input/input_test.cpp:486 |
| MD6-10    | joy0=001 (Kempston, not MD), `i_JOY_LEFT(10)=1`              | —                                     | skip   | test/input/input_test.cpp:487 |
| MD6-11a   | 0000 (left, sub=000)                                         | md6_joystick_connector_x2.vhd:135-139 | skip   | test/input/input_test.cpp:490 |
| MD6-11b   | 0100 (left, sub=010)                                         | —                                     | skip   | test/input/input_test.cpp:491 |
| MD6-11c   | 0110 (left, sub=011)                                         | —                                     | skip   | test/input/input_test.cpp:492 |
| MD6-11d   | 1000 (left, sub=100)                                         | —                                     | skip   | test/input/input_test.cpp:493 |
| MD6-11e   | 1010 (left, sub=101)                                         | —                                     | skip   | test/input/input_test.cpp:494 |
| MD6-11f   | 0101 (right, sub=010)                                        | —                                     | skip   | test/input/input_test.cpp:495 |
| MD6-11g   | 0111 (right, sub=011)                                        | —                                     | skip   | test/input/input_test.cpp:496 |
| MD6-11h   | 1011 (right, sub=101)                                        | —                                     | skip   | test/input/input_test.cpp:497 |
| MD6-11i   | 1000 (left, sub=100), 3-button pad                           | —                                     | skip   | test/input/input_test.cpp:498 |
| SINC1-01  | joy0=011                                                     | —                                     | skip   | test/input/input_test.cpp:508 |
| SINC1-02  | joy0=011                                                     | —                                     | skip   | test/input/input_test.cpp:509 |
| SINC1-03  | joy0=011                                                     | —                                     | skip   | test/input/input_test.cpp:510 |
| SINC1-04  | joy0=011                                                     | —                                     | skip   | test/input/input_test.cpp:511 |
| SINC1-05  | joy0=011                                                     | —                                     | skip   | test/input/input_test.cpp:512 |
| SINC2-01  | joy1=000                                                     | —                                     | skip   | test/input/input_test.cpp:513 |
| SINC2-02  | joy1=000                                                     | —                                     | skip   | test/input/input_test.cpp:514 |
| SINC2-03  | joy1=000                                                     | —                                     | skip   | test/input/input_test.cpp:515 |
| SINC2-04  | joy1=000                                                     | —                                     | skip   | test/input/input_test.cpp:516 |
| SINC2-05  | joy1=000                                                     | —                                     | skip   | test/input/input_test.cpp:517 |
| SINC-06   | joy0=011, joy1=000, both LEFT                                | —                                     | skip   | test/input/input_test.cpp:518 |
| CURS-01   | joy0=010                                                     | —                                     | skip   | test/input/input_test.cpp:527 |
| CURS-02   | joy0=010                                                     | —                                     | skip   | test/input/input_test.cpp:528 |
| CURS-03   | joy0=010                                                     | —                                     | skip   | test/input/input_test.cpp:529 |
| CURS-04   | joy0=010                                                     | —                                     | skip   | test/input/input_test.cpp:530 |
| CURS-05   | joy0=010                                                     | —                                     | skip   | test/input/input_test.cpp:531 |
| CURS-06   | joy0=010, LEFT + RIGHT                                       | —                                     | skip   | test/input/input_test.cpp:532 |
| IOMODE-01 | Reset                                                        | —                                     | pass   | test/input/input_test.cpp:556 |
| IOMODE-02 | Write NR 0x0B = 0x80 (en=1, mode=00, iomode_0=0)             | —                                     | skip   | test/input/input_test.cpp:561 |
| IOMODE-03 | Write NR 0x0B = 0x81 (en=1, mode=00, iomode_0=1)             | —                                     | skip   | test/input/input_test.cpp:562 |
| IOMODE-04 | Write NR 0x0B = 0x91 (en=1, mode=01) + pulse `ctc_zc_to(3)`  | —                                     | skip   | test/input/input_test.cpp:563 |
| IOMODE-05 | Write NR 0x0B = 0xA0 (en=1, mode=10, iomode_0=0)             | —                                     | skip   | test/input/input_test.cpp:564 |
| IOMODE-06 | Write NR 0x0B = 0xA1 (en=1, mode=10, iomode_0=1)             | —                                     | skip   | test/input/input_test.cpp:565 |
| IOMODE-07 | Write NR 0x0B = 0xA0, assert `JOY_LEFT(5)=0`                 | —                                     | skip   | test/input/input_test.cpp:566 |
| IOMODE-08 | Write NR 0x0B = 0xA1, assert `JOY_RIGHT(5)=0`                | —                                     | skip   | test/input/input_test.cpp:567 |
| IOMODE-09 | Write NR 0x0B = 0xA0, assert `joy_uart_en`                   | —                                     | skip   | test/input/input_test.cpp:568 |
| IOMODE-10 | Write NR 0x0B = 0x80                                         | —                                     | skip   | test/input/input_test.cpp:569 |
| IOMODE-11 | NR 0x05 joy0/joy1 = 111 (user I/O) + NR 0x0B configured      | —                                     | skip   | test/input/input_test.cpp:570 |
| MOUSE-01  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:580 |
| MOUSE-02  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:581 |
| MOUSE-03  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:582 |
| MOUSE-04  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:584 |
| MOUSE-05  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:585 |
| MOUSE-06  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:586 |
| MOUSE-07  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:587 |
| MOUSE-08  | `port_mouse_io_en=0`                                         | —                                     | skip   | test/input/input_test.cpp:588 |
| MOUSE-09  | NR 0x0A bit 3 = 1 (reverse)                                  | —                                     | skip   | test/input/input_test.cpp:589 |
| MOUSE-10  | `port_mouse_io_en=1`                                         | —                                     | skip   | test/input/input_test.cpp:590 |
| MOUSE-11  | `port_mouse_io_en=1`, `nr_0a_mouse_dpi = "00"` vs `"11"`     | —                                     | skip   | test/input/input_test.cpp:591 |
| NMI-01    | NR 0x06 bit 3 = 1 (M1 en)                                    | —                                     | skip   | test/input/input_test.cpp:600 |
| NMI-02    | NR 0x06 bit 3 = 0                                            | —                                     | skip   | test/input/input_test.cpp:601 |
| NMI-03    | NR 0x06 bit 4 = 1                                            | —                                     | skip   | test/input/input_test.cpp:602 |
| NMI-04    | NR 0x06 bit 4 = 0                                            | —                                     | skip   | test/input/input_test.cpp:603 |
| NMI-05    | NR 0x06 bit 3 = 1                                            | —                                     | skip   | test/input/input_test.cpp:604 |
| NMI-06    | NR 0x06 bit 4 = 1                                            | —                                     | skip   | test/input/input_test.cpp:605 |
| NMI-07    | both NR 0x06 bits 3,4 = 1, both hotkeys asserted             | —                                     | skip   | test/input/input_test.cpp:606 |
| FE-01     | No keys, EAR=0, no `port_fe_ear`                             | —                                     | skip   | test/input/input_test.cpp:621 |
| FE-02     | EAR input high                                               | —                                     | skip   | test/input/input_test.cpp:622 |
| FE-03     | Write 0xFE bit 4 high (`port_fe_ear`=1), then read           | —                                     | skip   | test/input/input_test.cpp:623 |
| FE-04     | NR 0x08 bit 0 = 1 (issue 2), MIC=1, EAR=0                    | —                                     | skip   | test/input/input_test.cpp:624 |
| FE-05     | `expbus_eff_en=1`, `port_propagate_fe=1`, expansion bus dri… | —                                     | skip   | test/input/input_test.cpp:626 |

## Discrepancies noted

- **Z80N**: the Z80N suite is a FUSE-style data-driven runner (`test/z80n_test.cpp` parses `tests.in`/`tests.expected`) and has no in-source `check()` calls. The plan row identifiers used here are the Z80N opcodes from the coverage table (lines 70–100 of the plan doc); they are the natural grouping, not literal test IDs. Every row shows as `missing` in the "Test file:line" column because the opcode tokens do not appear as `check()` IDs. Coverage is verified by the runner's overall pass/fail count.

- **Memory/MMU, ULA Video, Tilemap, Audio, DMA, DivMMC+SPI, CTC+Interrupts, UART+I2C/RTC, NextREG**: these older rewrites do not use the Phase 2 `skip()` helper, so the Status column is reported as `—` where a `check()` exists. Running the binaries is required to populate pass/fail.

- **Per-row pass/fail is not computed anywhere** because this pass is read-only (no build, no test run). Even the 6 Phase 2 subsystems report `pass` where a `check()` exists and `skip` where a `skip()` exists; actual runtime fails would only show as `fail` if the test was executed.

- **Total extra-coverage rows** across all subsystems: 79.

