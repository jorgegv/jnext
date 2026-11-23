# Audio Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the audio subsystem of the JNEXT ZX
Spectrum Next emulator. Covers: AY/YM2149, Turbosound (triple AY), Soundrive
4-channel DAC, Beeper/Tape, and the final audio mixer.

All behavioural specifications are derived exclusively from the FPGA VHDL
sources in `ZX_Spectrum_Next_FPGA/cores/zxnext/src/audio/`.

## Current status

Live as of 2026-04-24 (Audio skip-reduction plan closed end-to-end):

- **`audio_test`: 160 / 160 pass / 0 fail / 0 skip** — ZERO skips.
  (Was 132 / 132 on 2026-04-24; +28 rows since, of which +8 on
  2026-09-24 for GH #201: AY-43, AY-63a, AY-63b, AY-64, TS-17, TS-40,
  MX-15, MX-21.)
- **`audio_nextreg_test` (new): 25 / 25 pass / 0 fail / 0 skip.**
- **`audio_port_dispatch_test` (new): 16 / 16 pass / 0 fail / 0 skip.**
- **`input_integration_test` extended**: 7/5/0/2 → 12/10/0/2 (+5 FE-READ rows).

The Audio skip-reduction plan at
[`doc/design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md`](../design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md)
closed end-to-end (Phases 0→4) on 2026-04-24. Summary:

- **Phase 0** demoted 21 E-class skips to inline `// A:` / `// G:`
  comments per `feedback_unobservable_audit_rule`: 16 A-class
  (duplicate coverage) + 5 G-class (unobservable without src/ API
  extension). Originally proposed 25; user + critic review reinstated
  TS-24 + TS-32/33/34 as F-skips (aggregate TS-30/31 can't distinguish
  per-PSG contribution) → later flipped to check() in Wave E.
- **Phase 1** added `src/audio/i2s.{h,cpp}` stub mirroring
  `audio_mixer.vhd:89-90,99-100` (10-bit latched LR sample pair, zero-
  extended to 13 for the Mixer sum). Mixer gains `set_i2s_source()`;
  Emulator owns `I2s i2s_` member + accessor. No port/NextREG wiring
  — programmatic-only stub for test observability. Phase 1 critic
  caught 1 BLOCKING (mixer sum data-flow mirrored VHDL in `158a70e`).
- **Phase 2** ran 4 parallel waves: A (flip MX-06 against I2s stub),
  B (new `audio_port_dispatch_test.cpp` 17 rows — 10 pass + 7 F-skip for
  real emulator port-decode gaps), C (new `audio_nextreg_test.cpp` 25
  rows + NR 0x06 bit-decode fix + NR 0x2C/2D/2E handler additions +
  several const getters), D (FE-READ group in `input_integration_test`
  covering BP-04, BP-20..23).
- **Wave E** (2026-04-24, post-Wave-B-critic) flipped the 4
  F-reinstated TS rows with distinct-amplitude per-PSG isolation
  proofs.
- **GH #201** (2026-09-24) closed eight rows that had been `// A:` /
  `// G:` / `WONT` comments and were publishing as `missing` in the
  traceability matrix, and retired four that cannot honestly become a
  `check()` (AY-41, SD-09, MX-30, IO-04 — each struck in place with its
  rationale). The producer MX-30 lacked now exists — NextPi under QEMU,
  through `audio/pi_audio.*` — and its stream is MX-41, with MX-31..40 and MX-42..47,
  all in `audio_posix_test` (they need a real FIFO; `# os: posix`, GH #214);
  MX-30 stays retired and its ID is not reused (owner, PR #315).
  Writing AY-43 found and fixed a real emulator defect: the
  `ena_div_noise` phase in `src/audio/ay_chip.cpp` read the TOGGLED
  `noise_div` where `ym2149.vhd:270-272` reads the value held on entry
  to the process, so the noise clock ran on the 1st, 3rd, 5th `ena_div`
  instead of the 2nd, 4th, 6th. Rate was right, phase inverted.
- **Wave F** (2026-04-24, post-Wave-B-critic) fixed 6 of 7 emulator
  port-dispatch gaps surfaced by Wave B: SD-10 (port 0x5F Soundrive
  mode 1 ch D), AUD-SD-12 (port 0x3F Profi Covox ch A), AUD-SD-14 (port 0xFB
  mono fan-out gated per `zxnext.vhd:2433` NR 0x84 bit 5 AND NOT bit 2),
  AUD-SD-15 (port 0xB3 GS Covox ch B+C), IO-03 (port 0xBFF5 reg-query
  mode read), IO-05 (port 0xBFFD → FFFD alias on +3 only). IO-04
  (FFFD falling-edge latch) demoted to `// G:` comment — invisible
  at Z80 instruction-boundary granularity.
  (IDs AUD-SD-12/14/15 renamed 2026-08-01, GH #196 dedup — were
  SD-12/14/15; pure rename, no behaviour change.)
- **Phase 3** refreshed BP-04 assertion per Wave D critic (sweep
  border=0/5/7 instead of single value).

Historical C-class bugs fixed on earlier branches (for reference —
these were listed in prior revisions of this plan):

- AY-81, AY-102, AY-110, AY-122 — envelope "hold at 0" family was
  addressed in Task 2 backlog before this plan.
- TS-10, TS-42 — TurboSound default panning + pan=01 decode addressed
  in Task 2 backlog.

## VHDL Source Files

| File               | Component                                         |
|--------------------|---------------------------------------------------|
| `ym2149.vhd`       | Single AY-3-8910 / YM2149 PSG chip               |
| `turbosound.vhd`   | Turbosound Next (3x YM2149 + select + stereo mix) |
| `soundrive.vhd`    | 4-channel 8-bit DAC (Soundrive / Covox)           |
| `dac.vhd`          | Delta-Sigma 1-bit DAC (analog output stage)       |
| `audio_mixer.vhd`  | Final L/R mixer (beeper + AY + DAC + I2S)         |
| `zxnext.vhd`       | Top-level wiring, port decode, NextREG config     |

## Architecture

### Separate test harness

The audio test suite uses a dedicated harness that instantiates the emulator's
audio components in isolation (or through the emulator core's I/O path). Tests
exercise the audio subsystem via port I/O and NextREG writes, then inspect
internal state and PCM output buffers.

### Test data format

Each test case specifies:
- Setup: NextREG configuration, port writes (sequence + data)
- Observation: expected PCM sample values, register readback, channel enable
  state, or stereo routing

Tests are grouped by component and numbered for traceability.

## 1. YM2149 / AY-3-8910 PSG

### 1.1 Register Address and Write

VHDL ref: `ym2149.vhd` lines 167-214

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-01  | Write register address via `busctrl_addr`        | `addr` latches bits [4:0] of data bus                      |
| AY-02  | Address only latches when `busctrl_addr=1`       | No change when `busctrl_addr=0`                            |
| AY-03  | Reset clears address to 0                        | After reset, `addr = 00000`                                |
| AY-04  | Write to all 16 registers (addr 0-15)            | Each register stores the written byte                      |
| AY-05  | Write with `addr[4]=1` is ignored                | Registers 16-31 do not exist; writes are no-ops            |
| AY-06  | Reset initialises all registers to 0x00          | Except R7 which resets to 0xFF (all channels disabled)     |
| AY-07  | Writing R13 triggers envelope reset              | `env_reset` pulses for one clock cycle                     |

### 1.2 Register Readback (AY vs YM mode)

VHDL ref: `ym2149.vhd` lines 217-254

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-10  | Read R0 (Ch A fine tone) in AY mode              | Returns full 8-bit value as written                        |
| AY-11  | Read R1 (Ch A coarse tone) in AY mode            | Bits [7:4] masked to 0 (only [3:0] returned)              |
| AY-12  | Read R1 in YM mode                               | Full 8-bit value returned (no masking)                     |
| AY-13  | Read R3, R5 (Ch B/C coarse tone) AY vs YM        | AY: bits [7:4]=0; YM: full 8 bits                         |
| AY-14  | Read R6 (noise period) in AY mode                | Bits [7:5] masked to 0; [4:0] returned                    |
| AY-15  | Read R6 in YM mode                               | Full 8-bit value returned                                 |
| AY-16  | Read R7 (mixer enable)                           | Full 8 bits returned in both modes                        |
| AY-17  | Read R8/R9/R10 (volume) in AY mode               | Bits [7:5] masked to 0; [4:0] returned                    |
| AY-18  | Read R8/R9/R10 in YM mode                        | Full 8 bits returned                                      |
| AY-19  | Read R13 (envelope shape) in AY mode             | Bits [7:4] masked to 0                                    |
| AY-20  | Read R13 in YM mode                              | Full 8 bits returned                                      |
| AY-21  | Read R11/R12 (envelope period)                   | Full 8 bits in both modes                                 |
| AY-22  | Read addr >= 16 in YM mode                       | Returns 0xFF                                               |
| AY-23  | Read addr >= 16 in AY mode                       | Returns register contents (AY ignores bit 4)              |
| AY-24  | Read with `I_REG=1` (register query mode)        | Returns `AY_ID & '0' & addr` (not register contents)      |
| AY-25  | AY_ID is "11" for PSG0, "10" for PSG1, "01" for PSG2 | Verify per-chip ID in query mode                     |

### 1.3 Register Readback: I/O Ports

VHDL ref: `ym2149.vhd` lines 240-249

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-30  | Read R14 with R7 bit 6 = 0 (Port A input mode)  | Returns `port_a_i` = 0xFF, NOT the latched byte                                       |
| AY-31  | Read R14 with R7 bit 6 = 1 (Port A output mode) | Returns `reg(14) AND port_a_i` = the latched byte                                     |
| AY-32  | Read R15 with R7 bit 7 = 0 (Port B input mode)  | Returns `port_b_i` = 0xFF, NOT the latched byte                                       |
| AY-33  | Read R15 with R7 bit 7 = 1 (Port B output mode) | Returns `reg(15) AND port_b_i` = the latched byte                                     |
| AY-34  | Port A/B inputs default to 0xFF (pullup)         | `turbosound.vhd:174-176, :229-231, :284-286` tie port_a_i/port_b_i all-ones on all three PSGs |

> **GH #201 (2026-09-23): AY-30..34 are live rows, no longer G30 skips.**
> The G30 WONT rested on "AyChip lacks port_a_i / port_b_i accessors". That
> premise was false: the Next routes those pins nowhere — `turbosound.vhd`
> hard-ties both to all-ones for all three PSGs, so their value is a constant
> of this hardware and there is nothing to plumb. What remained was a plain
> register read any guest can perform, and jnext answered it with the stored
> byte in BOTH directions. Fixed in `src/audio/ay_chip.cpp`; the five rows are
> asserted in `audio_test` group `AY-ports`.
>
> The tie-high citation above was also wrong throughout this document and in
> the test source: `turbosound.vhd:158` is psg0's `AY_ID` generic map, not the
> port tie-off.

### 1.4 Clock Divider

VHDL ref: `ym2149.vhd` lines 260-279

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-40  | Divider reloads with `I_SEL_L=1` (AY compat)    | Reload value = `"0111"` (divide by 8)                      |
| ~~AY-41~~ | ~~Divider reloads with `I_SEL_L=0` (YM mode)~~ | **RETIRED 2026-09-24 (GH #201)** — the ZX Next instantiates all three PSGs with `I_SEL_L => '1'` (`turbosound.vhd:164`, `:219`, `:274`), so `cnt_div <= (not I_SEL_L) & "111"` (`ym2149.vhd:267`) can only ever reload `"0111"`. The `"1111"` (/16) reload is a branch of the generic ym2149 IP core that no ZX Next signal can select — the hardware cannot enter this state. AY-44 asserts the /8 reload that IS reachable and cites `turbosound.vhd:164` for exactly this reason. No `check()` row exists. |
| AY-42  | `ena_div` pulses once per divider cycle          | Tone generators clocked at this rate                       |
| AY-43  | `ena_div_noise` at half `ena_div` rate           | Noise generator runs at half the tone clock, and lags it by one `ena_div` period: `noise_div <= not noise_div` is a signal assignment, so the `if (noise_div = '1')` on the next line reads the OLD value (`ym2149.vhd:270-272`) and the first pulse lands on the SECOND `ena_div`. Measured as tone edges on an 8-tick grid vs noise edges on a 16-tick grid, offset 8 |
| AY-44  | In turbosound wiring, `I_SEL_L='1'` always      | AY-compatible divide-by-8 mode used                        |

### 1.5 Tone Generators

VHDL ref: `ym2149.vhd` lines 304-330

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-50  | Tone period 0 or 1 produces constant high output | When `freq[11:1]=0`, comparator is 0, immediate toggle     |
| AY-51  | Tone period 2 toggles every 2 ena_div cycles     | Square wave at half the programmed frequency               |
| AY-52  | Tone period 0xFFF (max) produces lowest freq     | Counter counts up to 0xFFE, then toggles                   |
| AY-53  | Channel A uses R1[3:0] & R0                      | 12-bit period = `{R1[3:0], R0}`                            |
| AY-54  | Channel B uses R3[3:0] & R2                      | 12-bit period = `{R3[3:0], R2}`                            |
| AY-55  | Channel C uses R5[3:0] & R4                      | 12-bit period = `{R5[3:0], R4}`                            |
| AY-56  | Tone output toggles (not pulse)                  | Each period completion flips the output bit                |

### 1.6 Noise Generator

VHDL ref: `ym2149.vhd` lines 282-302

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-60  | Noise period from R6[4:0]                        | Period counter uses `R6[4:0]-1` as comparator              |
| AY-61  | Noise period 0 or 1 => comparator 0              | When `R6[4:1]=0000`, forced to 0 (fastest noise)          |
| AY-62  | Noise uses 17-bit LFSR (poly17)                  | Feedback taps: bit 0 XOR bit 2 XOR zero-detect            |
| AY-63  | Noise output is poly17 bit 0                     | AY-63a: 96 shifts match an independent re-implementation of the VHDL LFSR recurrence (`ym2149.vhd:111,284,293,302`) bit for bit. AY-63b: one shared generator — A, B and C are equal on every tick (`:302,470-472`) |
| AY-64  | Noise clocked at `ena_div_noise` rate            | One poly17 shift per `(comp+1)` `ena_div_noise` pulses (`ym2149.vhd:283,290-296`): the output-edge grid is `16*(comp+1)` ticks for R6 = 0 / 3 / 5 |

### 1.7 Channel Mixer

VHDL ref: `ym2149.vhd` lines 469-471

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-70  | R7 bit 0 = 0: Channel A tone enabled             | `chan_mixed(0)` depends on `tone_gen_op(1)`                |
| AY-71  | R7 bit 0 = 1: Channel A tone disabled (forced 1) | Tone OR'd with disable bit                                 |
| AY-72  | R7 bit 3 = 0: Channel A noise enabled            | `chan_mixed(0)` ANDs noise                                  |
| AY-73  | R7 bit 3 = 1: Channel A noise disabled (forced 1)| Noise OR'd with disable bit                                |
| AY-74  | R7 bits 1,4: Channel B tone + noise control      | Same logic for channel B                                   |
| AY-75  | R7 bits 2,5: Channel C tone + noise control      | Same logic for channel C                                   |
| AY-76  | Both tone and noise disabled: constant high       | `1 AND 1 = 1`, channel always "on"                         |
| AY-77  | Both tone and noise enabled: AND of both          | Channel output = `(tone OR disable) AND (noise OR disable)`|
| AY-78  | Mixer output 0 => volume output 0                 | When channel mixed bit = 0, output is zero                 |

### 1.8 Volume and Envelope Mode

VHDL ref: `ym2149.vhd` lines 472-520

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-80  | R8 bit 4 = 0: Channel A uses fixed volume        | 4-bit volume from R8[3:0]                                  |
| AY-81  | R8 bit 4 = 1: Channel A uses envelope volume     | 5-bit `env_vol` used directly                              |
| AY-82  | Fixed volume 0 => output "00000"                  | Special case: vol 0 is zero, not `"00001"`                 |
| AY-83  | Fixed volume 1-15 => `{vol[3:0], "1"}`           | 5-bit value with LSB forced to 1                           |
| AY-84  | Same for R9 (Channel B) and R10 (Channel C)      | Identical logic per channel                                |

### 1.9 Volume Tables (AY vs YM)

VHDL ref: `ym2149.vhd` lines 150-162, 522-541

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-90  | YM mode: 32-entry volume table                   | Uses 5-bit index into `volTableYm`                         |
| AY-91  | AY mode: 16-entry volume table                   | Uses bits [4:1] of 5-bit volume into `volTableAy`          |
| AY-92  | YM vol 0 = 0x00, vol 31 = 0xFF                   | Verify boundary entries                                    |
| AY-93  | AY vol 0 = 0x00, vol 15 = 0xFF                   | Verify boundary entries                                    |
| AY-94  | YM volume table exact values                      | `{00,01,01,02,02,03,03,04,06,07,09,0a,0c,0e,11,13,17,1b,20,25,2c,35,3e,47,54,66,77,88,a1,c0,e0,ff}` |
| AY-95  | AY volume table exact values                      | `{00,03,04,06,0a,0f,15,22,28,41,5b,72,90,b5,d7,ff}`       |
| AY-96  | Reset sets all audio outputs to 0x00              | O_AUDIO_A/B/C = 0 after reset                             |

### 1.10 Envelope Generator

VHDL ref: `ym2149.vhd` lines 332-465

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-100 | Envelope period from R12:R11 (16-bit)            | `env_gen_freq = {R12, R11}`                                |
| AY-101 | Envelope period 0 or 1 => comparator 0           | When `freq[15:1]=0`, forced to 0 (fastest envelope)        |
| AY-102 | Writing R13 resets envelope counter to 0          | `env_gen_cnt` cleared on `env_reset`                       |
| AY-103 | Writing R13 resets envelope to initial state      | Direction and volume set from attack bit                   |

**Envelope shapes (R13 bits [3:0] = C, At, Al, H):**

| ID     | Shape | R13 | Description       | Verification                                             |
|--------|-------|-----|-------------------|----------------------------------------------------------|
| AY-110 | 0-3   | 0x  | `\___`            | C=0,At=0: start at 31, count down, hold at 0            |
| AY-111 | 4-7   | 0x  | `/___`            | C=0,At=1: start at 0, count up, hold at max             |
| AY-112 | 8     | 08  | `\\\\` (saw down) | C=1,At=0,Al=0,H=0: repeat down cycles                   |
| AY-113 | 9     | 09  | `\___`            | C=1,At=0,Al=0,H=1: down once, hold at is_bot_p1 → YM[1]=0x01 (VHDL ym2149.vhd:428-431) |
| AY-114 | 10    | 0A  | `\/\/` (triangle) | C=1,At=0,Al=1,H=0: down then up, repeat                 |
| AY-115 | 11    | 0B  | `\___` hold at 0  | C=1,At=0,Al=1,H=1: down once, hold at 0 (is_bot, vol=0) |
| AY-116 | 12    | 0C  | `////` (saw up)   | C=1,At=1,Al=0,H=0: repeat up cycles                     |
| AY-117 | 13    | 0D  | `/‾‾‾` hold near max | C=1,At=1,Al=0,H=1: up once, hold at vol=30 (is_top_m1); YM out = 0xE0 |
| AY-118 | 14    | 0E  | `/\/\` (triangle) | C=1,At=1,Al=1,H=0: up then down, repeat                 |
| AY-119 | 15    | 0F  | `/‾‾‾` hold at 31 | C=1,At=1,Al=1,H=1: up once, hold at 31 (is_top, vol=31) |

**Envelope state machine details:**

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-120 | Attack=0 (At bit): initial vol=31, direction=down| `env_vol="11111"`, `env_inc='0'`                           |
| AY-121 | Attack=1 (At bit): initial vol=0, direction=up   | `env_vol="00000"`, `env_inc='1'`                           |
| AY-122 | C=0: hold after first ramp regardless of Al/H    | Single ramp then stop                                      |
| AY-123 | C=1, H=1, Al=0: hold one step inside boundary     | Hold at is_top_m1 (vol=30) when up, is_bot_p1 (vol=1) when down |
| AY-124 | C=1, H=1, Al=1: hold exactly at boundary          | Hold at is_top (vol=31) when up, is_bot (vol=0) when down       |
| AY-125 | C=1, H=0, Al=1: triangle wave (continuous)       | Direction reverses at each boundary, no hold               |
| AY-126 | C=1, H=0, Al=0: sawtooth (continuous)            | One-direction hold then restart (no direction reversal)    |
| AY-127 | Envelope steps through 32 levels (0-31)          | 5-bit counter increments/decrements by 1                   |
| AY-128 | Envelope period counter reset on R13 write        | Counter and ena both reset                                 |
| AY-129 | RESET_H keeps the divider and tone generators     | Twin chips in lockstep across a reset (`ym2149.vhd:258-278,314-330`, no reset clause) |
| AY-130 | RESET_H keeps the noise LFSR and the envelope     | Twin lockstep of noise and envelope outputs across a reset (`:286-300,338-462`) |
| AY-131 | A reset between an R13 write and the next tick keeps the envelope load | Twin lockstep; the load is applied before `reg(13)` is cleared (`:180-207,340-341,393-401`) |
| AY-132 | `power_on()` restores the declared initial values, keeps AY/YM | Lockstep with a fresh chip; `ctrl_aymode` is an input port (`:105-125`) |
| AY-133 | The first envelope step comes with the R13 write (YM, shape 0, period 0x0100, channel A forced on) | One tick after the write the output is `volTableYm[30]` (0xE0), not 0xFF; the next change (0xC0) follows 2040 to 2048 ticks later, whatever the divider phase (`:340-342,344-349,393-409`) |

## 2. Turbosound Next (3x AY)

### 2.1 AY Selection

VHDL ref: `turbosound.vhd` lines 118-139

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-01  | Reset selects AY#0 (`ay_select = "11"`)          | Default chip is PSG0                                       |
| TS-02  | Select AY#0: write 0xFC+ to FFFD with bits[4:2]=111, bits[1:0]=11 | `ay_select <= "11"`                     |
| TS-03  | Select AY#1: write with bits[1:0]=10             | `ay_select <= "10"`                                        |
| TS-04  | Select AY#2: write with bits[1:0]=01             | `ay_select <= "01"`                                        |
| TS-05  | Selection requires `turbosound_en_i = 1`         | When turbosound disabled, selection is ignored             |
| TS-06  | Selection requires `psg_reg_addr_i = 1`          | Only during address-write cycle (port FFFD)                |
| TS-07  | Selection requires `psg_d_i[7] = 1`              | Bit 7 must be set to trigger chip select                   |
| TS-08  | Selection requires `psg_d_i[4:2] = "111"`        | Bits 4:2 must all be 1                                     |
| TS-09  | Panning set simultaneously: bits[6:5]            | Each AY gets its own L/R pan from bits 6:5 of select byte |
| TS-10  | Reset sets all panning to "11" (both L+R)        | Default: all three PSGs output to both channels            |

### 2.2 Register Address Filtering

VHDL ref: `turbosound.vhd` lines 141-150

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-15  | Normal register address: bits[7:5] must be "000" | `psg_addr` only asserts when top 3 bits are 0             |
| TS-16  | Address routed to selected AY only               | Only selected PSG gets `busctrl_addr = 1`                  |
| TS-17  | Write routed to selected AY only                 | `psgN_we` is gated on `ay_select` ALONE (`turbosound.vhd:144,147,150`) — unlike `psgN_addr`, which also needs `psg_d_i(7:5) = "000"` (`:141,143`). Each PSG keeps its own register contents AND its own address latch across selection changes |
| TS-18  | Readback from selected AY                        | `psg_d_o` muxes based on `ay_select`                      |

### 2.3 Stereo Mixing per PSG

VHDL ref: `turbosound.vhd` lines 186-205 (psg0), similar for psg1/psg2

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-20  | ABC stereo mode (`stereo_mode_i=0`): L=A+B, R=B+C | Left = A + B, Right = B + C                             |
| TS-21  | ACB stereo mode (`stereo_mode_i=1`): L=A+C, R=C+B | Left = A + C, Right = C + B (C swaps with B on L)       |
| TS-22  | Mono mode for PSG0: L=R=A+B+C                    | `mono_mode_i(0)=1`: all channels summed to both sides     |
| TS-23  | Mono mode per-PSG: each bit controls one PSG      | `mono_mode_i[2:0]` independently controls PSG2/1/0        |
| TS-24  | Stereo mode is global for all PSGs                | Single `stereo_mode_i` bit applies to all three           |

### 2.4 PSG Enable/Disable

VHDL ref: `turbosound.vhd` lines 162, 194-205, 217, 249-260, 272, 304-315; `zxnext.vhd` line 6385

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-30  | Turbosound disabled: only selected PSG outputs    | Non-selected PSGs output zero                              |
| TS-31  | Turbosound enabled: all three PSGs output         | All PSGs contribute to the mix regardless of selection     |
| TS-32  | PSG0 active when `ay_select="11"` or ts enabled   | PSG0 zeroed only when not selected AND ts disabled         |
| TS-33  | PSG1 active when `ay_select="10"` or ts enabled   | Same logic for PSG1                                        |
| TS-34  | PSG2 active when `ay_select="01"` or ts enabled   | Same logic for PSG2                                        |
| TS-35  | TurboSound off, AY#0 selected: unselected AY#1 keeps running | AY#1 `output_a` equals AY#0's on every tick (identical ym2149 on one `clock_en_i`, `:162,217`); mix carries AY#0 only (`:252`); re-enable adds AY#1 (`:334`) |
| TS-36  | TurboSound off, AY#0 selected: unselected AY#2 keeps running | Same for AY#2 (`:162,272,307,334`) |
| TS-37  | TurboSound off, AY#1 selected: all three chips run, only AY#1 reaches the mix | Lockstep of all three; `pcm_left` = AY#1 alone (`:197,307` zero AY#0/AY#2); re-enable gives 3x |
| TS-38  | Noise and envelope generators of unselected chips run | R6/R7/R9/R11/R13 program on all three; A and B outputs equal across chips for 3112 off ticks and 64 on ticks (`ym2149.vhd:281-300,334-462`) |
| TS-62  | `TurboSound::reset()` keeps all three chips' generators | Twin lockstep of 9 outputs and the mix across a reset (`turbosound.vhd:162/217/272`, `ym2149.vhd:258-519`) |
| TS-63  | `TurboSound::power_on()` equals a freshly constructed TurboSound | Shadows cleared, hold cleared, id read 0xC0; lockstep with a fresh instance (`ym2149.vhd:105-125`, `turbosound.vhd:123-127`) |
| TS-64  | Held in reset: register select and writes ignored | R0/R5 stay 0, addr stays 0 (`ym2149.vhd:170,184,216-250`) |
| TS-65  | Held in reset: the select/pan command is ignored | `ay_select` stays "11", pan stays "11" (`turbosound.vhd:121-135`) |
| TS-66  | Held in reset: outputs 0, generators keep running | Outputs 0 during the hold; twin lockstep after release (`ym2149.vhd:258-330,525-528`, `zxnext.vhd:6379`) |
| TS-67  | Release: reset values stand, writes land again | R7 = 0xFF, R0 = 0, then a write lands (`ym2149.vhd:184-188`) |
| TS-60  | `TurboSound::reset()` clears only `ay_select` + `psg{0,1,2}_pan` | `enabled` / `stereo_mode` / `mono_mode` are external NR-driven inputs; reset must NOT touch them. skip — `turbosound.cpp:10-20` over-clears (see G115). Note: TS-39..59 reserved for future TS work |
| TS-61  | psg_mode=11 → `audio_ay_reset` does not clobber NR 0x08/0x09 | After psg_mode 11 strobe, NR 0x08 b1 (turbosound_en), b5 (stereo_mode), and NR 0x09 mono bits stay as-set. skip — split needed: full reset (power-on) vs ay-only reset (audio_ay_reset) (see G115) |

### 2.5 Panning and Final Sum

VHDL ref: `turbosound.vhd` lines 323-337

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-40  | Pan "11": output to both L and R                  | Driven from pan `"00"` (both sides silent) to `"11"` through the select-byte write path (`turbosound.vhd:129-134`); both pan gates open (`:323`, `:327`) |
| TS-41  | Pan "10": output to L only, R silenced            | R channel zeroed for this PSG                              |
| TS-42  | Pan "01": output to R only, L silenced            | L channel zeroed for this PSG                              |
| TS-43  | Pan "00": output silenced on both channels        | PSG contributes nothing                                    |
| TS-44  | Final L = sum of all three PSG L contributions    | 12-bit output: `psg0_L_pan + psg1_L_pan + psg2_L_pan`     |
| TS-45  | Final R = sum of all three PSG R contributions    | 12-bit output: `psg0_R_pan + psg1_R_pan + psg2_R_pan`     |

### 2.6 AY IDs

VHDL ref: `turbosound.vhd` lines 157, 212, 267

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-50  | PSG0 has AY_ID = "11"                             | Reading via port BFF5 returns "11" in top bits             |
| TS-51  | PSG1 has AY_ID = "10"                             | Used for chip identification                               |
| TS-52  | PSG2 has AY_ID = "01"                             | Used for chip identification                               |

## 3. Soundrive 4-Channel DAC

### 3.1 Channel Writes

VHDL ref: `soundrive.vhd` lines 69-107

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| SD-01  | Reset sets all channels to 0x80                   | DC midpoint (unsigned), not 0x00                           |
| AUD-SD-02 | Write channel A via port I/O (`chA_wr_i`)        | `chA` latches `cpu_d_i`                                    |
| SD-03  | Write channel B via port I/O (`chB_wr_i`)        | `chB` latches `cpu_d_i`                                    |
| SD-04  | Write channel C via port I/O (`chC_wr_i`)        | `chC` latches `cpu_d_i`                                    |
| SD-05  | Write channel D via port I/O (`chD_wr_i`)        | `chD` latches `cpu_d_i`                                    |
| SD-06  | NextREG 0x2D (mono) writes to chA AND chD        | Both channels A and D updated simultaneously               |
| SD-07  | NextREG 0x2C (left) writes to chB only           | Channel B updated                                          |
| SD-08  | NextREG 0x2E (right) writes to chC only          | Channel C updated                                          |
| ~~SD-09~~ | ~~Port I/O takes priority over NextREG~~ | **RETIRED 2026-09-24 (GH #201)**, was the G31 WONT. `soundrive.vhd:80-84` is an `if/elsif` in ONE clocked process, so the row only has content when both strobes are high on the SAME 28 MHz edge. A CPU cycle drives exactly one `iowr`, so the only hardware source of a genuine collision is the Copper writing NR 0x2D in the same cycle as a CPU `OUT` to a Soundrive port. jnext serialises CPU and Copper NextREG writes and modelling their per-cycle arbitration is the project-level WONT in [EMULATOR-DESIGN-PLAN.md](../design/EMULATOR-DESIGN-PLAN.md) Phase 11 (resolved as option (a); the same decision is why Copper ARB-01/02/03 order their stimulus by hand). The simultaneity this row needs cannot arise in jnext's execution model. SD-02..SD-08 cover both write paths individually. No `check()` row exists. |
| AUD-SD-19 | DAC channels reset to 0x80 on `nr_08_dac_en` 1→0 transition   | `soundrive.reset_i = reset OR NOT nr_08_dac_en` — disable rezeroes chA/B/C/D to DC midpoint. skip — `emulator.cpp:1674` only flips `dac_enabled_`; pre-existing values in `Dac::ch_[]` persist (see G111). Note: SD-10..18 already used in §3.2 |

### 3.2 Port Mapping (from zxnext.vhd)

VHDL ref: `zxnext.vhd` lines 2429-2435, 2658-2664

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| SD-10  | Soundrive mode 1 ports: 0x1F(A), 0x0F(B), 0x4F(C), 0x5F(D) | `port_dac_sd1_ABCD_1f0f4f5f_io_en`          |
| SD-11  | Soundrive mode 2 ports: 0xF1(A), 0xF3(B), 0xF9(C), 0xFB(D) | `port_dac_sd2_ABCD_f1f3f9fb_io_en`          |
| AUD-SD-12 | Profi Covox: 0x3F(A), 0x5F(D)                    | `port_dac_stereo_AD_3f5f_io_en`                           |
| AUD-SD-13 | Covox: 0x0F(B), 0x4F(C)                          | `port_dac_stereo_BC_0f4f_io_en`                           |
| AUD-SD-14 | Pentagon/ATM mono: 0xFB(A+D)                      | `port_dac_mono_AD_fb_io_en` (disabled when mode 2 active) |
| AUD-SD-15 | GS Covox: 0xB3(B+C)                              | `port_dac_mono_BC_b3_io_en`                                |
| AUD-SD-16 | SpecDrum: 0xDF(A+D)                               | `port_dac_mono_AD_df_io_en`                                |
| AUD-SD-17 | DAC requires `nr_08_dac_en=1`                     | Soundrive held in reset when `nr_08_dac_en=0`             |
| AUD-SD-18 | Mono ports (FB, DF, B3) write to both A+D or B+C | Single port write updates two channels                     |

### 3.3 PCM Output

VHDL ref: `soundrive.vhd` lines 109-116

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AUD-SD-20 | Left output = chA + chB (9-bit unsigned)          | `pcm_L_o = ('0' & chA) + ('0' & chB)`                     |
| AUD-SD-21 | Right output = chC + chD (9-bit unsigned)         | `pcm_R_o = ('0' & chC) + ('0' & chD)`                     |
| AUD-SD-22 | Max output: chA=0xFF, chB=0xFF => L=0x1FE        | No overflow, 9-bit result                                  |
| AUD-SD-23 | Reset output: L=0x100, R=0x100                    | 0x80 + 0x80 = 0x100 (DC midpoint)                         |

## 4. Beeper and Tape Audio

### 4.1 Port 0xFE Write

VHDL ref: `zxnext.vhd` lines 3591-3599

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| BP-01  | Port 0xFE write stores bits [4:0]                | `port_fe_reg <= cpu_do(4 downto 0)`                        |
| BP-02  | Bit 4 is the EAR output (speaker)                | `port_fe_ear <= port_fe_reg(4)`                            |
| BP-03  | Bit 3 is the MIC output                          | `port_fe_mic <= port_fe_reg(3)`                            |
| BP-05  | Reset clears port_fe_reg to 0                    | All bits zero after reset                                  |
| BP-06  | Port 0xFE decoded as A0=0                        | `port_fe = '1' when cpu_a(0) = '0'`                       |

### 4.2 Beeper/MIC Signal Processing

VHDL ref: `zxnext.vhd` lines 6503-6504

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| BP-10  | `beep_mic_final` = `EAR_in XOR (mic AND issue2) XOR mic` | Three-way XOR combining tape input, MIC, and issue2 mode |
| BP-11  | Issue 2 mode: MIC is XOR'd twice (cancels)       | When `nr_08_keyboard_issue2=1`: `EAR_in XOR mic XOR mic = EAR_in` |
| BP-12  | Issue 3 mode: MIC contributes to beep            | When `nr_08_keyboard_issue2=0`: `EAR_in XOR 0 XOR mic = EAR_in XOR mic` |
| BP-13  | Internal speaker exclusive mode                   | `beep_spkr_excl = nr_06_internal_speaker_beep AND nr_08_internal_speaker_en` |
| MX-63  | Issue 3: the tape level is XORed with the port 0xFE MIC bit in the mix | NR 0x08 = 0x10, port 0xFE = 0x08, tape playing: tape 1 -> 0, tape 0 -> 512 (`zxnext.vhd:6503`: 1 xor 0 xor 1 = 0). Real mixer output of the full machine (GH #550) |
| MX-64  | Issue 2: the MIC bit cancels while a tape plays   | NR 0x08 = 0x11, port 0xFE = 0x08, tape playing: tape 1 -> 512, tape 0 -> 0 (`t xor 1 xor 1 = t`) |
| MX-65  | No tape: the MIC term is the MIC bit on both settings; after an eject it is the relaxed `issue2_mic` | issue 2 + MIC, tape level 0: 0; eject, two output samples later: 512; issue 3 no tape: MIC 1 -> 512, 0 -> 0 (`zxnext.vhd:6503, 1636`). The after-eject value is jnext's model, not the board's: on the board `ear_relax` holds the last tape level 64 x 512 master cycles before relaxing to `issue2_mic` (`symmetric_relaxation.vhd:76,89-90`), the held-back D6 class |

### 4.3 Port 0xFE Read

VHDL ref: `zxnext.vhd` lines 3453-3468

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|

## 5. Audio Mixer

### 5.1 Input Scaling

VHDL ref: `audio_mixer.vhd` lines 63-90

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-01  | EAR volume = 0x0200 (512) when active             | `ear_volume = "0001000000000"` when `ear_i=1, exc_i=0`    |
| MX-02  | MIC volume = 0x0080 (128) when active             | `mic_volume = "0000010000000"` when `mic_i=1, exc_i=0`    |
| AUD-MX-03 | EAR/MIC silenced when `exc_i=1`                   | Speaker-exclusive mode zeroes beeper in mix                |
| AUD-MX-04 | AY input: zero-extended 12-bit to 13-bit          | `ay_L = '0' & ay_L_i` (range 0-2295)                      |
| MX-05  | DAC input: 9-bit left-shifted by 2 + zero-padded  | `dac_L = "00" & dac_L_i & "00"` (range 0-2040)            |
| MX-06  | I2S input: zero-extended 10-bit to 13-bit         | `i2s_L = "000" & pi_i2s_L_i` (range 0-1023)               |
| MX-07  | I2S input is OFFSET BINARY: silence = 0x200, 0 = full-negative | `i2s.vhd:179` `o_audio_pi_L <= (not audio_pi_L(12)) & audio_pi_L(11 downto 3)` inverts the sign bit; `zxnext.vhd:2674-2675` substitutes the same 0x200 when disabled/muted/EAR |
| ~~MX-30~~ | ~~Pi I2S source delivers a continuous 10-bit sample stream~~ | **RETIRED 2026-09-24 (GH #201)**, was the G29 WONT. The row asks for a Pi I2S SOURCE. jnext models no such source and by scope decision never will: [EMULATOR-DESIGN-PLAN.md](../design/EMULATOR-DESIGN-PLAN.md) §3.1 lists `audio/i2s*.vhd` with scope **no** ("I2S; SDL audio queue used instead") and its Phase 5 records Pi GPIO NR 0x90-0xA9 as "intentionally stubbed"; `src/audio/i2s.h:10-13` states the class is "a pure latched sample-pair register — no real I2S wire / clocking / protocol emulation"; and nothing in `src/` ever calls `I2s::set_sample()`. There is no emulated Raspberry Pi to be the producer — an absent SUBSYSTEM, not an untested behaviour. What jnext does model stays covered by MX-06 (zero-extension into the 13-bit sum) and MX-07 (offset-binary midpoint, GH #116). No `check()` row exists. |
| MX-31  | Pi I2S source: prebuffer and underrun | Nothing plays with 2204 frames in, and 2205 (50 ms) all play; running dry returns silence, counts one underrun, and prebuffers again. Literal numbers, so a changed `kPrebuffer` fails. jnext-only, no VHDL counterpart |
| MX-32  | Pi I2S source: latency trim | A backlog of 20000 frames is cut to 4410 (100 ms) by dropping the OLDEST; the next frame is number 20000-4410. Literal numbers, so a changed `kTarget` fails. jnext-only, no VHDL counterpart |
| MX-33  | Pi I2S source: ring full | With nothing popping, 131072 + 1000 frames written: exactly 131072 kept and received, 1000 dropped and counted. Literal numbers, so a changed `kCapacity` fails. jnext-only buffering, no VHDL counterpart |
| MX-42  | Pi I2S source: a pause longer than the ring | 140000 frames written with nothing popping: the next pop returns silence with `available()` 0 and all 140000 counted dropped (the stale ring flushed); a fresh stream then plays from its own first frame after the prebuffer. jnext-only, no VHDL counterpart |
| MX-43  | Pi I2S source: back-to-back streams | Writer B opens before writer A closes (no EOF between them); each sends a header and 3000 frames: exactly 6000 frames received, A's then B's, exact. B's header, split across two reads (6 bytes, a 150 ms pause, the rest), is recognised in-stream (RIFF...WAVE at a frame boundary). jnext-only, no VHDL counterpart |
| MX-44  | Pi I2S source: write latency | Ten one-frame writes after the reader has gone idle, each timed until `available()` shows it: median under 25 ms (woken only by the 100 ms poll timeout, ~50 ms). jnext-only, no VHDL counterpart |
| MX-45  | Pi I2S source: close() is prompt | With a writer connected but silent, `close()` 30 ms after `open` returns within 250 ms (~70 ms with the 100 ms poll; ~1 s with a 1000 ms one). jnext-only, no VHDL counterpart |
| MX-46  | Pi I2S source: reconnect latency | 30 ms after the reader logs a writer's EOF (its pause over, the reader already in `poll()` with no writer), a new writer's frame is available within 100 ms. With no writer the reader polls for 20 ms at a time, which bounds a reconnect's latency. 127.5 ms was measured on a macOS CI runner with the 100 ms timeout and a pause after the EOF; that is consistent with the pause being stretched by macOS timer leeway (inferred, not proved; XNU wakes a poll() on a new writer's data), and the pause is now taken only after a poll() that returned at once (GH #354). jnext-only, no VHDL counterpart |
| MX-47  | Pi I2S source: an idle reader costs nothing | With a writer connected but silent, 300 ms of waiting uses under 30 ms of CPU (`getrusage`) and 1..6 `idle_reads()`: the reader sleeps in `poll()` for 100 ms at a time, not the 20 ms it uses with no writer. Polling for the wrong event spins a core (~300 ms measured). jnext-only, no VHDL counterpart |
| MX-34  | Pi I2S source: reconnect | Writer A: header, one frame and half of another, then closes; writer B: a fresh header and `kPrebuffer` frames. B's header is skipped and A's half frame discarded: A's frame, then all of B's, arrive exact. jnext-only, no VHDL counterpart |
| MX-35  | Pi I2S source: open errors and permissions | `open` of a regular file fails with "exists and is not a FIFO"; of a path in a missing directory, with mkfifo's error; a new FIFO is created 0600 (under umask 022). jnext-only, no VHDL counterpart |
| MX-36  | Pi I2S source: trim boundary | A backlog of exactly 13230 frames (300 ms) is not trimmed (first frame 0, 13229 left, none dropped); 13231 is trimmed to 4410 (first frame 8821). jnext-only, no VHDL counterpart |
| MX-37  | Pi I2S source: no spin without a writer | After the writer closes, `eof_reads()` grows by 8..60 in 600 ms: the reader pauses 20 ms after each read that finds no writer where `poll()` returned at once, and polls for 20 ms at a time while there is none (30 wakeups on both Linux and macOS; about 6 on macOS with a 100 ms timeout). Discriminates on Linux, where `poll()` reports a gone writer at once; on macOS `poll()` times out instead. jnext-only, no VHDL counterpart |
| MX-38  | Pi I2S source: a live writer's pause | A writer sends a header and one frame and a half, pauses 250 ms (the reader polls out and reads EAGAIN), then the rest: all `kPrebuffer` + 1 frames arrive exact. Treating EAGAIN as the writer's end would take 44 bytes after the pause for a header. (EINTR from the non-blocking FIFO `read()` cannot be provoked; the `poll()` EINTR is MX-40.) jnext-only, no VHDL counterpart |
| MX-39  | Pi I2S source: descriptor hygiene | Found by inode: one descriptor on the FIFO after `open`, with `FD_CLOEXEC`; still one after a second `open`; none after `close()`; a second `close()` leaves a descriptor that reused the number open. jnext-only, no VHDL counterpart |
| MX-40  | Pi I2S source: a signal during poll | A SIGUSR1 handler without SA_RESTART, the signal blocked in the test thread, five `kill()`s land on the reader's `poll()` (EINTR); a stream written afterwards still arrives (10 frames). jnext-only, no VHDL counterpart |
| MX-41  | Pi I2S source delivers a continuous 10-bit stream | What MX-30 asked for, now that NextPi (`--nextpi`) is a producer; MX-30 itself stays retired and its ID is not reused (owner, PR #315): `PiAudio` reads QEMU's WAV stream from a FIFO frame for frame (header and frames split across writes are reassembled); `PiAudio::to_i2s` maps -32768/0/32767 to 0/0x200/1023 (`i2s.vhd:177-180`: the 13-bit sample's sign bit inverted, its top 10 bits kept); with NR 0xA2 = 0xC0 a frame of +16384/-16384 mixes to +1024/-1024. All 3000 frames are counted received (`frames_received()`), none dropped. |
| MX-48  | NR 0xA2 bit 0: the EAR comparator's thresholds and hysteresis | With NR 0xA2 = 0xC1, latched samples step through rest, 0x2FF (no rise), 0x300 (rise), back to rest (hold), 0x100 (hold), 0x0FF (fall), R alone at 0x3FF (rise), and L 0x2FF with R 0x100 (bitwise t = 11, rise): `fe_ear()` follows t = L(9:8) OR R(9:8) exactly (`zxnext.vhd:2677-2690`) |
| MX-49  | NR 0xA2 bit 0: the EAR comparator's gating | With samples at 0x3FF: 0 at reset, with bit 0 clear, and with I2S disabled; 1 at once when NR 0xA2 becomes 0x81 or 0x41; with bit 0 set `pi_audio_L/R` read 0x200; forced to 0 when bit 0 clears; reset clears it (`zxnext.vhd:2674-2690`) |
| MX-50  | Pi I2S source: the prebuffer after a flush | Playing (2300 frames written, one popped), then 140000 frames with nothing popping: the next pop flushes and returns silence. 100 fresh frames: pop is still silence (`available()` 100, below the 2205-frame prebuffer); 2105 more: pop returns the first fresh frame. jnext-only, no VHDL counterpart |
| MX-51  | Pi I2S source: a header split between bytes 12 and 44 | Back-to-back streams as MX-43, but B's header is split after 20 bytes (RIFF...WAVE recognisable, header incomplete): the other 24 bytes are skipped on the next read; exactly 6000 frames arrive, exact. jnext-only, no VHDL counterpart |
| MX-52  | Pi I2S source: RIFF without WAVE is audio | Frames whose bytes spell `RIFF`, `abcd`, `NOTW` at a frame boundary, then 3000 more: all 3003 frames arrive, exact (a header needs `WAVE` at byte 8). jnext-only, no VHDL counterpart |
| MX-53  | Pi I2S source: no pause after a poll that waited | On a FIFO that has never had a writer, once `eof_reads()` reaches 5, `no_writer_pauses()` is 0: `poll()` timed out and that wait paced the loop, so no further 20 ms pause (oracles: Linux `pipe_poll` EPOLLHUP only after a writer has come and gone; XNU `filt_vnode_common` readable only when `FIONREAD != 0`). A counter, not a timing. jnext-only, no VHDL counterpart |
| MX-54  | Pi I2S source: tape mode prebuffer | In tape mode nothing plays with 11024 frames in; the 11025th (250 ms) plays frame 0. Literal numbers. jnext-only buffering |
| MX-55  | Pi I2S source: tape mode skips single frames | Tape mode, 30000 frames in, 6001 popped: every step between frames is 1 or 2; exactly 12 steps of 2, the first at pop 500 and each 500 after, equal to `frames_skipped()`; nothing dropped or repeated (under the 44100-frame tape limit). Pins the 500-pop spacing. jnext-only buffering |
| MX-56  | Pi I2S source: tape mode repeats single frames | Tape mode, 11025 frames in, 5001 popped with nothing more written: every step is 0 or 1; exactly 4 repeats, the first at pop 3310 (pop 3309 is the first to read below 7717 = 11025 - 3308) and each 500 after, equal to `frames_repeated()`; no underrun, nothing skipped. Pins the band's lower edge and the spacing. jnext-only buffering |
| MX-57  | Pi I2S source: entering and leaving tape mode | Playing outside tape mode (3000 in, frame 0 popped); entering tape mode, pop is silence with 2999 in; 8100 more: pop is frame 1. Leaving it: `available()` drops from 11098 to 4410, 6688 counted dropped, and the next pop is frame 6690. jnext-only buffering |
| MX-58  | I2S: NR 0xA2 routes the Pi to EAR (`I2s::ear_routed`) | True for 0xC1, 0x81, 0x41, 0xFF; false for 0x01, 0xC0, 0x00, 0x3F; true at 0xC1 while the comparator (`fe_ear()`) is 0, so it is the route, not the level (`zxnext.vhd:2677-2690`). In `audio_test`, every OS |
| MX-59  | Pi I2S source: the tape trim | Tape mode: 44100 frames in, one pop: frame 0, 44099 left, nothing dropped. 44101 frames in: the pop returns frame 33076, 11024 left, 33076 dropped (cut to 11025, the oldest first). Literal numbers. jnext-only buffering |
| MX-60  | Pi I2S source: the band's upper edge | Tape mode, 1000 pops: with 14832 frames in, pop 500 reads 14333 (11025 + 3308, on the edge) and nothing is skipped; with 14833 it reads 14334 and exactly one frame is skipped, seen at pop 500. jnext-only buffering |
| MX-61  | Pi I2S source: the adjust timer restarts with each playback | Tape mode: 600 pops in the band, then pops until dry (one underrun, nothing left), then a refill to 14833 frames: exactly one frame is skipped, seen at pop 500 of the new playback (not sooner, whatever the timer held when the last playback ended). jnext-only buffering |

### 5.2 Final Mix

VHDL ref: `audio_mixer.vhd` lines 92-107

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-10  | Left output = ear + mic + ay_L + dac_L + i2s_L   | Simple addition, 13-bit result (range 0-5998)              |
| MX-11  | Right output = ear + mic + ay_R + dac_R + i2s_R   | Same formula for right channel                             |
| MX-12  | Reset zeroes both output channels                  | `pcm_L = 0`, `pcm_R = 0`                                  |
| MX-13  | EAR and MIC go to both L and R                     | Beeper is always mono in the mix                           |
| MX-14  | Max theoretical output = 5998                      | 512 + 128 + 2295 + 2040 + 1023 = 5998                     |
| MX-15  | No saturation/clipping in mixer                    | All FIVE terms at their ceiling at once (`audio_mixer.vhd:63-64,80-89`): 512+128+2295+2040+1023 = 5998, still inside the 13-bit signal, arrives intact on both channels. ay = 2295 needs all three PSGs in mono mode. MX-05/MX-14 only assert sub-maximal sums |
| MX-16  | Silence with the Pi I2S input WIRED and idle is digital 0 | The resting mix is DAC 0x100&lt;&lt;2 + I2S 0x200 = 1536; the host AC-coupling reference must be that whole sum. MX-10/MX-11 assert the same on a Mixer with NO I2s — a configuration the emulator never has (`src/core/emulator.cpp:44`) |
| MX-17  | An ASSEMBLED power-on machine emits digital 0 (end-to-end) | GH #116: silence read +2048 on every sample of every run, so SDL's zero-padding of a starved device buffer became a ~21.5 Hz square wave — the reported "motor" noise. Lives in `audio_nextreg_test` because only the full machine wires the I2s |

### 5.3 Speaker-Exclusive Mode

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-20  | `exc_i=1`: EAR and MIC contribute 0 to mix       | Beeper only goes to internal speaker, not line out         |
| MX-21  | `exc_i=0`: EAR and MIC contribute normally        | The muxes at `audio_mixer.vhd:80-81` are COMBINATIONAL, so `exc_i` returning to `'0'` reopens them on the spot. Driven 1 → 0 on ONE Mixer, asserting EAR 512, MIC 128 and both 640 (×4 into int16). MX-01/MX-02 only see the power-on default and cannot tell a gate from a latch |
| MX-22  | `exc_i` derived from NextREGs 0x06 bit 6 AND 0x08 bit 4 | `beep_spkr_excl = nr_06_internal_speaker_beep AND nr_08_internal_speaker_en` |
| MX-23  | Mixer drops EAR/MIC contribution when `exc_i=1` (downstream)  | `pcm_L/R = ay_L/R + dac_L/R + i2s_L/R` only — `ear/mic = 0` regardless of `ear_i/mic_i`. skip — `mixer.cpp:28-29` sums EAR+MIC+AY+DAC+I2S unconditionally; Mixer never reads `Emulator::beep_spkr_excl()` (see G110) |

### 5.4 Tape, Pi EAR and the MIC term (GH #550)

`audio_mixer.vhd` has no tape input (`:35-37`): the tape (`i_AUDIO_EAR`) and the Pi's audio routed to EAR reach
the mix only inside `mic_i = beep_mic_final` (`zxnext.vhd:6503, 6516`), at 128. Signed sample = (13-bit sum - 1536) x 4,
so EAR alone = 2048 and the MIC term alone = 512; the beeper is mono (L == R). `i_AUDIO_EAR` is the jack through
`ear_relax` (`zxnext_top_issue2.vhd:662-677`, `symmetric_relaxation.vhd:62,76,89-90`): a level with no edge for
64 x 512 master cycles relaxes to `zxn_issue2_fe_mic`. jnext applies that to the Pi's EAR route and to a playing tape (the last edge of a restored
deck is derived from its position, MX-93..100). Full-machine rows in `audio_nextreg_test`.

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-62  | The tape reaches the mix through the MIC term at 128, not as a term of its own | issue 3, port 0xFE = 0, tape 1 -> 512, tape 0 -> 0 (`audio_mixer.vhd:64,81,99`) |
| MX-66  | EAR stays 512 and the tape adds 128 on top (4:1)  | port 0xFE = 0x10, tape 1 -> 2560, tape 0 -> 2048 |
| MX-67  | `exc_i` silences the tape's term with EAR and MIC | NR 0x06 = 0x40, NR 0x08 = 0x10, port 0xFE = 0x18: 0 / 0 (`audio_mixer.vhd:80-81`) |
| MX-68  | Pi audio routed to EAR reaches the MIC term; the I2S term stays at rest | `set_sample(0x3FF,0x3FF)`, NR 0xA2 = 0xC1: 512 (`zxnext.vhd:2358,2364-2373,6503`) |
| MX-69  | While routed and unrelaxed, port 0xFE MIC and NR 0x08 issue 2 both feed the MIC term | MIC 1: 0 (`1 xor 0 xor 1`); issue 2: 512 (`1 xor 1 xor 1`) |
| MX-70  | `ear_relax`: a Pi level held 64 x 512 master cycles relaxes to the issue-2 MIC level | +30000 cycles: 512; +34500: 0 (`symmetric_relaxation.vhd:76,89-90`) |
| MX-71  | A state load re-derives the MIC term              | save with port 0xFE = 0x08, load into a fresh machine: 512 |
| MX-72  | A state load re-derives the exc gate (R1)         | NR 0x06 = 0x40 and NR 0x08 = 0x10 saved: the loaded machine has `beep_spkr_excl()` and mixes 0 |
| MX-73  | The mixer's inputs are `ear_i` and `mic_i` only (bare Mixer) | raw tape alone adds 0; `mic_final` alone adds 512; `set_mic()` sets `mic_final` |
| MX-78  | A state load re-derives the NMI button enables (NR 0x06 bits 4:3) | restored and cleared directions (D9) |
| MX-79  | A state load re-derives `nr_06_ps2_mode`           | the NR 0x06 read returns bit 2 as saved (D9) |
| MX-80  | A state load re-derives the hotkey enables (NR 0x06 bits 7, 5) | F8 with bit 7 clear leaves the CPU speed alone (D9) |
| MX-81  | A state load re-derives the exc gate in the other direction | snapshot with the gate open into a machine with it closed: mixes 2048 |
| MX-82  | Replay mode holds the Pi's EAR term off, and the MIC term follows it | 512 / 0 / 512 |
| MX-84  | A reset clears the mixer's MIC term with the port 0xFE MIC bit | MIC 1: 512; after a reset: 0 (`zxnext.vhd:3588-3594,6503`) |
| MX-85  | A tape stopped at level 1 leaves the raw tape level 0 | one instruction after the eject: no stale EAR level |
| MX-86  | A restore with no tape playing clears a saved raw tape level | one instruction after the load |
| MX-87  | A tape edge reaches the MIC term in its own instruction | 8 edges: `mic_final` equals the raw level in the instruction that shows it (issue 3, MIC 0), not one output sample later |
| MX-88  | An OUT 0xFE while a tape plays | tape 1: MIC 0 -> 512, MIC 1 -> 0, MIC 0 -> 512 (`zxnext.vhd:6503`) |
| MX-89  | A restore while a tape plays re-derives the MIC term | saved 0, changed 512, restored 0 |
| MX-90  | A state load re-derives the F3 hotkey enable (NR 0x06 bit 5) | F3 with the bit clear leaves NR 0x05 bit 2 alone (D9) |
| MX-91  | A state load re-derives `nr_06_ps2_mode` the other way | saved 0 into a machine holding 1 (D9) |
| MX-92  | In a HALT batch the Pi's `ear_relax` hold runs out at its time | the last samples of the frame are at rest (`pi_sample_thunk_`) |
| MX-74  | A held tape level relaxes in the mix              | TZX pure tone: edge 512, held < hold 512, held > hold 0, raw tape level still 1 (D6) |
| MX-75  | The relaxation reaches the port 0xFE read          | bit 6 follows the held tape until the hold, then the issue-2 MIC level (D6) |
| MX-76  | Issue 2 + MIC: a held tape 0 relaxes to the MIC level, the next edge restarts the hold | mix 0 -> 512, then next edges 512 / 0 (D6) |
| MX-77  | The hold restarts at an edge of tape XOR `pi_fe_ear` | a Pi edge inside the tape's hold extends it (D6) |
| MX-83  | A tape stopped and started again restarts the hold at its first edge | the restarted tape is heard at once (512), not relaxed (D6) |
| MX-93  | A TAP restore mid-pilot derives the tape edge     | 500 T into a 2168 T pulse: edge 500 T back |
| MX-94  | A TAP restore mid-pause, before the relaxation    | 1000 T after the edge: age derived, mix 512 |
| MX-95  | A TAP restore mid-pause, after the relaxation     | 8000 T after the edge: relaxed, mix 0 |
| MX-96  | A TAP restore inside a data bit                   | a one bit (1710 T), 300 T left: age 1410 T |
| MX-97  | A TZX restore in the pause's 1 ms hold            | derived edge equals the continuous one |
| MX-98  | A TZX restore in the pause's remainder            | derived edge equals the continuous one |
| MX-99  | A TZX restore in a pulse phase                    | age 0 (documented rule): never earlier than the continuous edge, at most one pulse later |
| MX-100 | A WAV restore finds the edge by looking back      | young (1000 T): mix 512; old (9000 T, look-back capped at 8192 T): mix 0 |
| MX-101 | A `.jns` load inside a tape's hold re-derives the tape edge at the deck reattach | TAP played 1000 T into the first pause, saved, loaded into a fresh machine: age within 800 master cycles, mix 512, deck playing |

## 6. NextREG Configuration

### 6.1 PSG Mode (NextREG 0x06)

VHDL ref: `zxnext.vhd` lines 5163-5170, 6379

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AUD-NR-01 | `nr_06_psg_mode[1:0]` from NextREG 0x06 bits [1:0] | Two-bit mode selector                                   |
| AUD-NR-02 | Mode "00": YM2149 mode                            | `aymode_i = 0` (YM volume table, full register readback)  |
| AUD-NR-03 | Mode "01": AY-8910 mode                           | `aymode_i = 1` (AY volume table, masked readback)         |
| AUD-NR-04 | Mode "10": YM2149 mode (bit 0 = 0)                | Same as "00" for AY chip behaviour                        |
| AUD-NR-05 | Mode "11": AY reset (silent)                       | `audio_ay_reset = 1` is a level: reset held while psg_mode = "11"; all AY output zeroed |
| AUD-NR-07 | Mode "11" holds the AY reset                        | Writes ignored while held, accepted after leaving it (`zxnext.vhd:6379,6387`) |
| AUD-NR-08 | The hold survives a soft reset                      | `nr_06_psg_mode` is init-only (`zxnext.vhd:1112`) |
| AUD-NR-09 | A state load restores the hold                      | The hold is the NR 0x06 value, re-derived, not serialised |
| AUD-NR-35 | psg_mode "10" is not a hold                      | Writes land at once, after a soft reset and after a state load (`zxnext.vhd:6379`) |
| AUD-NR-33 | Power-on PSG mode is YM until NR 0x06 is written    | `aymode_i = 0` (`zxnext.vhd:1112,6389`) |
| AUD-NR-34 | The NR 0x06 PSG mode survives a soft reset          | `zxnext.vhd:1112,6389` |
| AUD-NR-06 | `nr_06_internal_speaker_beep` from bit 6          | Controls speaker-exclusive mode                            |

### 6.6 Reset scope

VHDL ref: `ym2149.vhd` lines 105-125, 170-186, 258-519, 525-528; `zxnext_top_issue2.vhd` lines 840, 1158-1177, 1195

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AUD-NR-30 | A soft reset keeps every AY generator field     | Only addr, registers and outputs are cleared              |
| AUD-NR-31 | A soft reset keeps the PSG clock-enable phase   | `clk_28_div` has no reset                                 |
| AUD-NR-32 | `init()` without `preserve_memory` is power-on  | Generators and PSG phase at their initial values (hard reset = flashboot) |

### 6.2 Audio Config (NextREG 0x08)

VHDL ref: `zxnext.vhd` lines 5176-5182

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AUD-NR-10 | Bit 5: PSG stereo mode (0=ABC, 1=ACB)            | `nr_08_psg_stereo_mode`                                    |
| AUD-NR-11 | Bit 4: Internal speaker enable                    | `nr_08_internal_speaker_en`                                |
| NR-12  | Bit 3: DAC enable                                  | `nr_08_dac_en`; when 0, Soundrive held in reset           |
| AUD-NR-13 | Bit 1: Turbosound enable                           | `nr_08_psg_turbosound_en`                                  |
| AUD-NR-14 | Bit 0: Keyboard Issue 2 mode                       | `nr_08_keyboard_issue2`; affects beeper signal             |

### 6.3 PSG Mono (NextREG 0x09)

VHDL ref: `zxnext.vhd` line 5186

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| NR-20  | Bits [7:5] of NextREG 0x09: per-PSG mono          | `nr_09_psg_mono[2:0]`                                     |
| NR-21  | Bit 7: PSG2 mono, Bit 6: PSG1 mono, Bit 5: PSG0 mono | Each bit independently controls one PSG               |

### 6.4 DAC NextREG Mirrors

VHDL ref: `zxnext.vhd` lines 4852-4854, `soundrive.vhd`

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| NR-30  | NextREG 0x2C: write to Soundrive chB (left)       | `nr_left_we_i` triggers chB write                          |
| NR-31  | NextREG 0x2D: write to Soundrive chA+chD (mono)   | `nr_mono_we_i` triggers both chA and chD writes            |
| NR-32  | NextREG 0x2E: write to Soundrive chC (right)       | `nr_right_we_i` triggers chC write                         |
| NR-33  | NR 0x2C / NR 0x2E read returns `pi_audio_L/R(9 downto 2)`     | High 8 bits of latest 10-bit Pi I2S sample, not regs_[] shadow. skip — `emulator.cpp:1722-1739` only registers WRITE handlers; reads fall through to regs_[] (see G112) |
| NR-34  | NR 0x2D read returns `nr_2d_i2s_sample & "000000"`            | Low 2 bits latched on the previous NR 0x2C / 0x2E read. skip — same handler-missing prerequisite as NR-33 (see G112) |

### 6.5 Pi I2S Control (NextREG 0xA2)

VHDL ref: `zxnext.vhd` lines 1341, 2593-2600, 5642, 6127, 6780

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| NR-40  | NR 0xA2 store: 8-bit register write              | `nr_a2_pi_i2s_ctl <= nr_wr_dat` on NR 0xA2 strobe. skip — no NR 0xA2 handler; falls through to generic shadow store (see G113) |
| NR-41  | NR 0xA2 fan-out to Pi I2S control signals        | b7=enL, b6=enR, b4=inout, b3=muteL, b2=muteR, b0=ear (drives I2S Mixer gate). skip — fan-out not wired (see G113) |
| NR-42  | NR 0xA2 read: `b[7:6]'0'b[4:2]'1'b[0]`           | bit 5 reads as 0, bit 1 reads as 1 (fixed pattern). skip — read returns raw shadow byte (see G113) |
| NR-43  | Mixer gates Pi I2S adder by pi_i2s_en[L/R] AND NOT mute[L/R] | audio_mixer.vhd:50-60 — control fan-out from NR 0xA2 must reach the Mixer input gates. skip — Mixer ignores the gate signals (see G73, distinct from G113 NR-handler) |

## 7. I/O Port Wiring

### 7.1 AY Ports

VHDL ref: `zxnext.vhd` lines 2647-2649, 2771-2773

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| IO-01  | Port FFFD: `A[15:14]="11"`, A[2]=1, A[0]=1      | AY register select (write) / read data                    |
| IO-02  | Port BFFD: `A[15:14]="10"`, A[2]=1, A[0]=1      | AY register write                                          |
| IO-03  | Port BFF5: BFFD with A[3]=0                       | Register query mode (`psg_d_o_reg_i`)                      |
| ~~IO-04~~ | ~~FFFD read latched on falling CPU clock edge~~ | **RETIRED 2026-09-24 (GH #201)** — formalises the Wave F demotion already recorded at `test/audio/audio_port_dispatch_test.cpp:489-501`. `port_fffd_dat <= psg_dat` on `falling_edge(i_CLK_CPU)` is at `zxnext.vhd:6407-6411` (the earlier comment's `:2771-2773` is `port_fffd_rd`, a different signal). The latch reloads on EVERY CPU falling edge, unconditionally, and a Z80 `IN` holds IORQ for ≥ 3 T-states at the same clock, so it has been reloaded with the settled `psg_dat` long before the CPU samples the bus. `psg_dat` itself only changes on an `OUT` from a previous instruction. The one-CPU-cycle pipeline stage is therefore unobservable at the instruction granularity jnext's CPU models — a modelling-granularity decision, not a coverage gap. IO-01/IO-02/IO-03/IO-05 cover the observable 0xFFFD/0xBFFD/0xBFF5 paths. No `check()` row exists. |
| IO-05  | BFFD readable as FFFD on +3 timing                | `port_fffd_rd` includes `port_bffd and machine_timing_p3` |

### 7.2 DAC Port Enable

VHDL ref: `zxnext.vhd` lines 2775-2778

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| IO-10  | DAC writes require `dac_hw_en=1`                   | Gated by `nr_08_dac_en`                                    |
| IO-11  | Multiple port mappings can map to same channel     | E.g., chA can be written by ports 0x1F, 0xF1, 0x3F, 0xFB, 0xDF |
| IO-12  | Port FD conflict: F1 and F9 in mode 2             | `port_fd_conflict_wr` prevents AY false triggers           |
| IO-13  | NR 0x84 b1 gates Soundrive Mode 1 port 0x1F (chA)             | `port_dac_sd1_ABCD_1f0f4f5f_io_en <= internal_port_enable(17)`. Implemented: `audio_port_dispatch_test.cpp` IO-13, the write is dropped with b1 clear and lands on chA with it set |
| IO-14  | NR 0x84 b3 gates Profi Covox port 0x3F (chA)                  | `port_dac_stereo_AD_3f5f_io_en <= internal_port_enable(19)`. Implemented: IO-14 (0x3F is Profi-only; 0x5F is also reachable through b1) |
| IO-15  | NR 0x84 b4 gates Covox ports (0x0F chB / 0x4F chC)            | `port_dac_stereo_BC_0f4f_io_en <= internal_port_enable(20)`. Implemented: IO-15, both writes dropped with b1 and b4 clear, both land with b4 alone |
| IO-16  | NR 0x84 b6 gates GS Covox port (0xB3 chB+chC)                 | `port_dac_mono_BC_b3_io_en <= internal_port_enable(22)`. Implemented: IO-16 |
| IO-17  | NR 0x84 b7 gates SpecDrum port (0xDF chA+chD)                 | `port_dac_mono_AD_df_io_en <= internal_port_enable(23)`. Implemented: IO-17 |

## 8. Debugger audio introspection

Suite `test/debug/audio_debug_test.cpp` (`audio_debug_test`, GH #529). It tests what the debugger
backend reports about the audio chain, not the chain itself (sections 1-7 do that). The rows of this
section are the ones this suite asserts today: `ANO` (the note helper), `AIZ` (the debugger's audio
features are invisible to the Z80) and `ADS` (`Debugger::audio_state()`). Later sub-issues of the
same epic add rows to the same suite under their own prefixes, each documented here with the work
that implements it.

### 8.1 Note helper (`src/debug/audio_math.h`)

VHDL ref: `ym2149.vhd` lines 306-325 (tone divider), 334-350 and 402-462 (envelope rate and shape),
371-391 (shape table); `turbosound.vhd` line 164 (I_SEL_L = 1). A4 = 440 Hz equal temperament.
PSG clock 1 750 000 Hz unless stated; frequencies to 1e-3 Hz, cents to 0.005.

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| ANO-01 | Tone period 0 behaves as period 1                | 109375.000 Hz, equal to period 1; not a note               |
| ANO-02 | Tone period 1                                    | 109375.000 Hz; above MIDI 127, not a note                  |
| ANO-03 | Tone period 8                                    | 13671.875 Hz is MIDI 128: not valid (upper bound)          |
| ANO-04 | Tone period 9                                    | 12152.778 Hz is F#9 +45.17, the highest valid note         |
| ANO-05 | Tone period 252                                  | 434.028 Hz is A4 -23.66                                    |
| ANO-06 | Tone period 253                                  | 432.312 Hz is A4 -30.52                                    |
| ANO-07 | Tone period 239                                  | 457.636 Hz is A#4 -31.96 (sharps only)                     |
| ANO-08 | Tone period 428                                  | 255.549 Hz is C4 -40.68 (scientific octave)                |
| ANO-09 | Tone period 4095                                 | 26.709 Hz is G#0 +49.50                                    |
| ANO-10 | A frequency exactly half way between two notes   | 452.893 Hz is A#4 -50.00: the tie goes to the upper note   |
| ANO-11 | The bottom of the range                          | 8.176 Hz is C-1 +0.00 (MIDI 0), valid; 0.6 semitone below it, 0, negative, NaN and infinite frequencies are not |
| ANO-12 | Envelope shapes 8 and 12, period 100             | 68.359 Hz, C#2 -23.55 (clock / (256 * period))             |
| ANO-13 | Envelope shapes 10 and 14, period 100            | 34.180 Hz, C#1 -23.55 (clock / (512 * period))             |
| ANO-14 | Envelope period 0 and 1, shapes 8 and 10         | Saw 6835.938 Hz, triangle 3417.969 Hz; period 0 equals period 1 |
| ANO-15 | Shapes 0-7, 9, 11, 13, 15                        | No repeat frequency (0 Hz)                                 |
| ANO-16 | Envelope glyph of each of the 16 shapes          | The table at `ym2149.vhd:371-391`, four ASCII characters   |
| ANO-17 | The same period at the Next's and the 128K's PSG clock | 1.75 MHz is 23.00 cents flat of 1.7734 MHz           |

### 8.2 Invisible to the Z80

Oracle: `src/audio/audio_mute.h` (the debugger's mutes and views have no hardware analogue, so the
machine's saved state and video must not depend on them) and the hardware, which sums every chip
(`turbosound.vhd:184-337`) with no mute. Fixture: one Next with TurboSound on, all three AY chips and
the DAC and beeper making sound, written through real ports and NextREGs.

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AIZ-01 | Mute AY1, run 50 frames; load the same starting state, run 50 frames unmuted | State bytes and framebuffer hash are equal |
| AIZ-02 | The same for muting the DAC and for muting the beeper | State bytes and framebuffer hash are equal                |
| AIZ-03 | Positive control: one extra `OUT 0xBFFD` in one run | State bytes differ (the comparison can fail)             |
| AIZ-04 | Mute AY1, load a state saved unmuted             | Before any tick the audible sum is the loaded hardware sum; the state saves back byte for byte; one PSG tick later the audible sum is the unmuted sum minus AY1's share |
| AIZ-05 | 1000 `audio_state()` calls between two instructions of a frame | The saved state equals the run without the calls; no NextREG read ran (the trace logs none; a control read does log) |
| AIZ-06 | Sound, `reset_ay_only()`, save before any PSG tick | `pcm_l`/`pcm_r` equal a fresh TurboSound's; the sounding state differs (`turbosound.vhd:118-138`) |
| AIZ-07 | A parked CPU looping over every audio write site (chip select, R13, a plain DAC port, 0xDF, a NextREG mirror, 0xFE with EAR toggling, a dropped 0xB3); `set_audio_log_enabled()` on in one run, off in the other | State bytes and framebuffer hash are equal over 50 frames: the write log only reads. Positive control: the run with the log on holds entries of all five targets |
| AIZ-08 | A parked CPU looping over every audio write site (as AIZ-07); an AudioWrite subscription on ANY with a Log action on in one run, off in the other | State bytes and framebuffer hash are equal over 50 frames: the event only reads. Positive control: the run with it on delivered all five targets and a dropped write |
| AIZ-09 | Each of the 13 channel mute bits, and ALL, for 50 frames, on versus off (14 runs) | State bytes and framebuffer hash are equal in every run (the hardware sums every channel: `turbosound.vhd:186-334`, `soundrive.vhd:112-113`) |
| AIZ-10 | A parked CPU looping `OUT (C),A` to port 0x00B3 with NR 0x84 bit 6 off (thousands of dropped writes a frame); `clear_audio_drops()` in one run only | State bytes and framebuffer hash are equal: the dropped-write counters are outside `describe_state()`. The counters do move (a probe machine counts more than 1000) |

### 8.3 `Debugger::audio_state()`

Driven by real port writes (0xFFFD/0xBFFD, 0xFE, the DAC ports) and NextREG writes on a ZXN_ISSUE2
machine; PSG time by ticking TurboSound. Oracles as cited per row.

| ID     | Drive                                            | Expected                                                   |
|--------|--------------------------------------------------|------------------------------------------------------------|
| ADS-01 | none                                             | `psg_clock_hz` 1750000 (`zxnext_top_issue2.vhd:1209,1217`)  |
| ADS-02 | R7 = every value 0x00-0x3F                       | Tone enabled = bit k clear, noise enabled = bit k+3 clear (`ym2149.vhd:467-469`) |
| ADS-03 | R0-R5 distinct                                   | Periods 0xF34, 0x256, 0x378 (`:302-304`, R1/R3/R5 bits 3:0) |
| ADS-04 | R8=0x0A, R9=0x1F, R10=0xE5                       | Volumes 10, 15, 5; envelope on B only (`:476-520`)          |
| ADS-05 | R6 = 0xFF                                        | Noise period 31 (`:283`)                                    |
| ADS-06 | R11=0x34, R12=0x12, R13=0xFE                     | Period 0x1234, shape 0x0E (`:332`)                          |
| ADS-07 | Address 0x1D, then a data write                  | Address 0x1D, registers unchanged (`:173,188`)              |
| ADS-08 | TurboSound on; 0xFFFD = 0xBE, 0xDD, 0x9F         | Active chip 1 pan 01, 2 pan 10, 0 pan 00; pans end [0,1,2] (`turbosound.vhd:129-136`) |
| ADS-09 | TurboSound off; 0xFFFD = 0xBE                    | Active chip and pan unchanged (`:129`)                      |
| ADS-10 | Select AY2, TurboSound off, then on              | In-mix [F,F,T], then [T,T,T] (`:197,252,307`)               |
| ADS-11 | NR 0x09 = 0x40                                   | Mono [F,T,F] (`zxnext.vhd:5747`)                            |
| ADS-12 | NR 0x06 = 0x03                                   | psg_mode 3, held in reset (`:5731,6989`)                    |
| ADS-13 | NR 0x06 = 0x02                                   | psg_mode 2, not held, YM curve (`:6999`)                    |
| ADS-14 | NR 0x06 = 0x01, then 0x00                        | psg_mode 1, 0; never held                                   |
| ADS-15 | NR 0x08 = 0x22, then 0x00                        | TurboSound on and ACB, then off and ABC (`:5739-5742`)      |
| ADS-16 | Mid-ramp, then R13 = 0x08 and 0x0C, no tick      | Step 31 falling, then 0 rising: the pending reload (`ym2149.vhd:393-401`) |
| ADS-17 | R11=2, R13=8, ticks counted from the write; samples all eight divider phases agree on | Level 28 falling after 40 ticks, 14 after 264, 30 after 520, 0 after 1000: the forced first step (`ym2149.vhd:340-342,393-409`) |
| ADS-18 | The same with R13=10                             | 14 falling after 264, 1 rising after 520, 6 rising after 600, 31 holding after 1000 |
| ADS-19 | The same with R13=13                             | 3 rising after 40, 13 rising after 200, 31 holding after 520 and 1000 |
| ADS-20 | The same with R13=14                             | 17 rising after 264, 30 falling after 520, 25 falling after 600, 0 holding after 1000 |
| ADS-31 | Shape 13, R11=2, 520 and 1000 ticks | 31 holding (`ym2149.vhd:422-443`) |
| ADS-32 | Shapes 0 and 9, 1000 ticks | 0 holding |
| ADS-33 | 40 ticks | Shapes 12 and 13 rising, shape 8 falling (motion only; the same with or without the forced first step) |
| ADS-21 | AY and YM curves; fixed volume 7, 0 (also 0x00 in YM); envelope step 28; a gated channel | Levels 0x22/0x13, 0, 0xD7/0xA1; the gated channel still reports its level while its output is 0 (`:476-537`) |
| ADS-22 | NR 0x08 bit 3, NR 0x84; ports 0x1F/0x0F/0x4F/0x5F | Channels 0x11, 0x22, 0x33, 0x44; enabled (`zxnext.vhd:2745,2967-2980`) |
| ADS-23 | Then NR 0x08 bit 3 off                           | Disabled, all channels 0x80 (`soundrive.vhd:72-101`)        |
| ADS-24 | NR 0x84 = 0xA5, 0xFE; expansion bus on, NR 0x88 = 0x0F | Port families 0xA4 and AY ports on; ports off; 0x0E and on (`zxnext.vhd:2708-2709,2744-2751`) |
| ADS-25 | Port 0xFE = 0x10 with tape EAR high; 0x08 with it low | EAR + tape; MIC only                                   |
| ADS-26 | NR 0x06 bit 6 x NR 0x08 bit 4                    | The two flags as written; excluded is their AND (`:5724,5739,7114`) |
| ADS-27 | NR 0xA2 = 0x81, 0x41, 0x01                       | (left, right, EAR) = (T,F,T), (F,T,T), (F,F,F) (`:2593-2598,2677-2690`) |
| ADS-28 | Gains and mute masks set on the mixer            | Host fields equal; muted on the muted chip's channels only  |
| ADS-29 | `--silent` and a default machine                 | Synthesis off / on                                          |
| ADS-30 | Distinct bytes in all 16 registers of each chip  | `regs` equals the bytes written and `ay_registers(c)`       |

### 8.4 Dropped writes

Every discarded AY, DAC and NR 0x2C-0x2E write is counted under the first VHDL reason that applies
(decode before gate), read through `Debugger::audio_state().drops`. Fixture: a bare Next (ZXN_ISSUE2).
Oracles: `zxnext.vhd:2744-2751,2963-2965,2974-2980,3024,3034-3041,3088-3094,5412-5414,6989,7046`,
`turbosound.vhd:121,129-136,141`, `ym2149.vhd:170,184,188`, `soundrive.vhd:72-101`, `nextreg.txt:816-823`.
Applied = the register or channel took the byte and no counter moved; dropped = it did not and exactly
one counter moved by 1. Every row asserts the whole `drops` array, so a write counted under the wrong
reason fails as well. Writes are `emu.port().out()` (source Debugger) unless the row says otherwise.

| ID     | Drive                                            | Expected                                                   |
|--------|--------------------------------------------------|------------------------------------------------------------|
| DRP-01 | 0xFFFD with NR 0x84 bit 0 off; then also NR 0x06 = 11; then both released | `AyPortDisabled` 1, address unchanged; port off AND held: `AyPortDisabled` 2 (decode first); released: applied (`zxnext.vhd:2744,2963`) |
| DRP-02 | 0xBFFD with the AY ports off, then on            | `AyPortDisabled` 1, R8 unchanged; then R8 = 0x0F, no count (`:2964`) |
| DRP-03 | The same through 0xBFF5                          | The same (`:2965`)                                          |
| DRP-04 | NR 0x06 = 11: 0xFFFD = 0x08 and 0xFE (TurboSound off); released | `AyHeldInReset` 2 (reset beats the pattern reason); then applied (`turbosound.vhd:121`, `ym2149.vhd:170`) |
| DRP-05 | Select 0x15, held in reset, 0xBFFD; released, select 5, 0xBFFD = 0x2A | `AyHeldInReset` 1 (not `AyAddressHigh`); then R5 = 0x2A (`ym2149.vhd:184`) |
| DRP-06 | 0xFFFD = 0xFE with TurboSound off, then on       | `AyChipSelectTurboOff` 1, chip 0 stays active; then chip 1 active, pan 3, no count (`turbosound.vhd:129-136`) |
| DRP-07 | 0xFFFD = 0x40 (TurboSound off and on), 0x80 (on), 0x98 (off); then 0x08 | `AySelectIgnored` 4, `AyChipSelectTurboOff` 0; address 0x08 at the end only (`turbosound.vhd:141`) |
| DRP-08 | Select 0x15; 0xBFFD = 0x11, 0xBFF5 = 0x22; select 5, 0xBFFD = 0x33 | `AyAddressHigh` 2, R5 unchanged; then R5 = 0x33, no count (`ym2149.vhd:188`) |
| DRP-09 | Port 0x1F (chA, b1): family off, DAC off, both off, both on | `DacFamilyDisabled`; `DacDisabled`; `DacFamilyDisabled` (precedence); applied (`zxnext.vhd:2745,2977,3091`) |
| DRP-10 | Port 0x0F (chB, b1 or b4), plus each bit alone   | Drops only when both bits are off; each alone applies (`:2748,2978`) |
| DRP-11 | Port 0x4F (chC, b1 or b4)                        | As DRP-10 (`:2979`)                                         |
| DRP-12 | Port 0x5F (chD, b1 or b3)                        | Drops only when both bits are off (`:2747,2980`)            |
| DRP-13 | Port 0x3F (chA, b3)                              | As DRP-09 (`:2747,2977`)                                    |
| DRP-14 | Port 0x7FF1 (chA, b2): b2 off, DAC on; b2 on, DAC off; both on; both off | Family drop AND the byte reaches port 0x7FFD; `DacDisabled` and 0x7FFD unchanged; applied; family first (`:3024,3034`) |
| DRP-15 | Port 0xF3 (chB, b2)                              | As DRP-09 (`:2746,2978`)                                    |
| DRP-16 | Port 0x7FF9 (chC, b2)                            | As DRP-14 for channel C                                     |
| DRP-17 | Port 0xFB: b2 = b5 = 0; b2; b5 without b2; neither decode with the DAC off; decoded, DAC off | Family drop; D only; A and D; family drop (not `DacDisabled`); `DacDisabled` (`:2749,2974,2977,2980`) |
| DRP-18 | Port 0xB3 (chB and chC, b6)                      | As DRP-09, both channels set when applied (`:2750,2975`)    |
| DRP-19 | Port 0xDF (chA and chD, b7)                      | As DRP-09, A and D set when applied (`:2751,2974`)          |
| DRP-20 | NR 0x2C = 0x44 with NR 0x08 bit 3 off, then on   | `DacMirrorDisabled` 1, port 0x002C, B = 0x80; then B = 0x44 (`zxnext.vhd:5412,7046`, `soundrive.vhd:72-94`) |
| DRP-21 | NR 0x2D, the same                                | Port 0x002D; then A = D = 0x44 (`soundrive.vhd:85-89,98-101`) |
| DRP-22 | NR 0x2E, the same                                | Port 0x002E; then C = 0x44                                  |
| DRP-23 | A CPU `OUT (C),A` to 0x12B3 with b6 off          | `DacFamilyDisabled` 1 with the 16-bit port, the byte, the OUT's PC, source Cpu and the stamper's frame tag |
| DRP-24 | A CPU `NEXTREG 0x2D,0x11` with the DAC off       | `DacMirrorDisabled`, port 0x002D, PC of the NEXTREG, source Cpu (`zxnext.vhd:5413`) |
| DRP-25 | A DMA byte to port 0x00DF with b7 off            | `DacFamilyDisabled`, source Dma, port 0x00DF, the RAM byte  |
| DRP-26 | A Copper `MOVE 0x2D,0x66` with the DAC off       | `DacMirrorDisabled`, source Copper, value 0x66              |
| DRP-27 | A debugger `port_out` and `nextreg_write` that are discarded | Source Debugger                                 |
| DRP-28 | 0xDFFD (NR 0x82 b2 = 1) with the AY ports on, then off | No counter moves, the AY address is unchanged, the paging register takes the byte (`zxnext.vhd:3088`) |
| DRP-29 | Drops under three reasons; `clear_audio_drops()` | Ok; every counter and `last_*` field at its default; `state_revision()` advanced |
| DRP-30 | Drops; soft reset; state save and load; cold boot | The counters equal the original after each step             |
| DRP-31 | Rewind on, a CPU loop of dropped OUTs, run 10 frames, pause, `step_back(1)` | The machine's cycle moves back and the count is unchanged (no counting in a replay) |
| DRP-32 | Two drops of one reason                          | Count 2, the last port and value are the second write's     |

### 8.5 Per-channel mute (`AudioChannelMute`)

Suite `audio_debug_test`, GH #533. The mute has no hardware analogue, so the oracle is the VHDL mix
with the muted channel's INPUT read as silent: 0 for an AY output, 0x80 for a DAC channel. The AY
fixture programs nine distinct DC levels (volumes 1-9, R7 = 0x3F; `ym2149.vhd:467-520`) through real
ports. The test-local oracle `expect_chip()` is a copy of `turbosound.vhd:186-199` with the pan gates
(`:323-329`) that reads the chips' own outputs and shares no code with TurboSound; the expected pcm is
the sum over the chips (`:334`). The DAC fixture opens the DAC (NR 0x08 bit 3, NR 0x84) at 0x40/0x50/0x60/0x70
with the AY silent; the expected sample is `4 * ((pair << 2) - 1024)` (`audio_mixer.vhd:86,99`, I2S idle
0x200, so the mix rests at 1536) with a muted channel counting 0x80 (`soundrive.vhd:74-77`).

| ID | Drive | Expected |
|----|-------|----------|
| ACM-01 | Mute chip 0 channel A (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-02 | Mute chip 0 channel B (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-03 | Mute chip 0 channel C (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-04 | Mute chip 1 channel A (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-05 | Mute chip 1 channel B (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-06 | Mute chip 1 channel C (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-07 | Mute chip 2 channel A (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-08 | Mute chip 2 channel B (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-09 | Mute chip 2 channel C (`ay_bit`): tick; then clear it, tick | Muted: `pcm_left/right` and all three `chip_pcm` pairs equal the oracle with only that input zeroed, and differ from the unmuted mix; cleared: the unmuted values (`turbosound.vhd:186-199,334`) |
| ACM-10 | Mute DAC A: sample; then clear it, sample | The muted side reads that channel at 0x80 (-768 left, right -768); the other side is unchanged; clearing restores -1792 / -768 (`soundrive.vhd:74-77`, `audio_mixer.vhd:86,99`) |
| ACM-11 | Mute DAC B: sample; then clear it, sample | The muted side reads that channel at 0x80 (-1024 left, right -768); the other side is unchanged; clearing restores -1792 / -768 (`soundrive.vhd:74-77`, `audio_mixer.vhd:86,99`) |
| ACM-12 | Mute DAC C: sample; then clear it, sample | The muted side reads that channel at 0x80 (-1792 left, right -256); the other side is unchanged; clearing restores -1792 / -768 (`soundrive.vhd:74-77`, `audio_mixer.vhd:86,99`) |
| ACM-13 | Mute DAC D: sample; then clear it, sample | The muted side reads that channel at 0x80 (-1792 left, right -512); the other side is unchanged; clearing restores -1792 / -768 (`soundrive.vhd:74-77`, `audio_mixer.vhd:86,99`) |
| ACM-14 | DAC gain -6 dB: DAC_ALL, then DAC_A alone | All four muted: (0, 0); A alone: each side `4 * lround(-192 * g)` (an 0xD0 pair), so the muted arm scales like the unmuted one |
| ACM-15 | ACB; mute chip 0 channel C | Removed from BOTH sides (`turbosound.vhd:186,189`) |
| ACM-16 | Chip 1 mono (NR 0x09 bit 6); mute channel A | Removed from both sides, which are equal (`:186-192`) |
| ACM-17 | Chip 2 pan left only; mute channel B | Left drops, right stays 0 (the pan gate follows the mux, `:323-329`) |
| ACM-18 | `AUDIO_MUTE` = AY1 plus channel bits of chips 0 and 1; then `AUDIO_MUTE` = 0 | A muted chip is 0 whatever its channel bits; unmuting it leaves its channel bit in force |
| ACM-19 | DAC B muted, EAR high; then the beeper muted | Beeper silent, B still at 0x80 |
| ACM-20 | The beeper muted, DAC B muted, the source mask set again; then cleared | The Mixer's `mute_mask()` is the source mask only (BEEPER, then NONE) and `Dac::channel_mute()` stays 0x2 through both; B reads 0x80 throughout, the beeper returns when its mute is cleared |
| ACM-21 | `set_audio_channel_mute_mask(0xFFFF)` | Stored 0x1FFF; TurboSound 0x1FF; DAC 0xF |
| ACM-22 | Mask set; soft reset; separately an in-place `init()` | Mask, both copies and the audible effect unchanged |
| ACM-23 | Mask set; `emulator_cold_boot()` | Mask and both copies (TurboSound, Dac) kept |
| ACM-24 | Save with no mute; set a mask; load | The mask is unchanged |
| ACM-25 | Channel mask plus `AUDIO_MUTE` = AY2 | `host.channel_mute_mask` as set; `muted` is its channel bit OR its chip's source bit |
| ACM-26 | A written while muted, then unmuted; a state load while muted | The DAC's audible copy follows: the write shows once unmuted, the load refreshes it, `channel()` keeps the register value (`soundrive.vhd:74-77`) |

The Qt side (`debugger_audio_panel_test` DAP-31..37, `debugger_panels_test` QATR-04), the backend
(`debugger_backend_test` REV-47) and the script (`script_parse_test` PARSE-ACM-01 and EVAL-ACM-01,
`script_events_test` SCRIPT-EV-MUT-ACM) rows are documented in their own suites' headers and in the
traceability matrix.

### 8.6 Audio write log

Every decoded AY, DAC, NR 0x2C-0x2E and beeper write, logged through `Debugger::audio_log_entries()`
(GH #531). "The ring is allocated only while the log is on" (3 MB at the default size) has no row: no API shows the release, and adding one for a memory-only property is not worth it, so it is checked by review. Fixture: a bare Next (ZXN_ISSUE2). Oracles: `ym2149.vhd:186,188,207-210`,
`turbosound.vhd:129-141`, `zxnext.vhd:2965,2974-2980,5412-5414`, `soundrive.vhd:85-101`; the
timestamps against the event system's own. CPU rows run a guest program; a write from the test
(`emu.port().out()`) is a Debugger-source write.

| ID     | Drive                                            | Expected                                                   |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AWL-01 | CPU: select R7, then write 0x38 through 0xBFFD, R7 preset 0x3F | One entry (the select is not logged): AyRegister, chip 0, reg 7, value 0x38, prev 0x3F, Cpu, the data OUT's PC, port 0xBFFD, flags 0 |
| AWL-02 | The same data write through 0xBFF5               | The same with port 0xBFF5 (`zxnext.vhd:2965`)               |
| AWL-03 | TurboSound on: 0xFFFD = 0xFE then 0xFD; off: 0xFFFD = 0xFF, then 0x05 | AyChipSelect (chip, prev) = (1, 0) then (2, 1); one Dropped AyChipSelectTurboOff; nothing for the plain select (`turbosound.vhd:129-141`) |
| AWL-04 | R13 = 0x08 twice, R12 = 0x08, a data write with the address at 0x1D | Both R13 entries flagged EnvelopeRestart (the second with an unchanged value); R12 flags 0; one Dropped AyAddressHigh entry, no restart flag (`ym2149.vhd:188,207-210`) |
| AWL-05 | Twelve DAC port cases, family and DAC on, channels preset | Per port the entries' chips, in order, are the decode's channels, each with its preset as prev and equal timestamps (`zxnext.vhd:2974-2980`) |
| AWL-06 | CPU NEXTREG 0x2C, 0x2D, 0x2E                     | DacMirror entries: chip 1; chips 0 then 3; chip 2; the NEXTREG's PC; prev the channel before (`soundrive.vhd:85-101`) |
| AWL-07 | OUT 0xFE: 0x00, 0x01, 0x10, 0x18, 0x18           | Beeper entries only for 0x10 (prev 0x00) and 0x18 (prev 0x10) |
| AWL-08 | Dropped: 0xBFFD with NR 0x84 b0 off; 0xB3 with b6 off; NR 0x2D with the DAC off | Three Dropped entries with their target, chip (0xFF for the DAC ones), register and reason; each counter moved by 1 |
| AWL-09 | A DMA byte to 0xDF; a Copper MOVE 0x2D; `port_out` 0x1F; a CPU OUT 0x1F | Sources Dma, Copper, Debugger, Cpu |
| AWL-10 | A CPU OUT 0xBFFD, a DMA byte to 0xDF, a CPU NEXTREG 0x2D, a Copper MOVE 0x2D, after two frames | The entry's cycle, frame, vc, hc and pc equal the delivered event's; tstate_in_frame = vc * tstates_per_line + hc / 2 |
| AWL-11 | No request; request; drop it; request again     | Nothing recorded and entries refused with no request; the ring is freed when the request goes; the seq is never reused |
| AWL-12 | Two clients (one an observer) request; one drops; the other detaches | The log is an OR of the requests: on after the first drops, off after the detach; an observer alone turns it on |
| AWL-13 | `audio_log_resize(4)`, 6 writes; resize(2); resize(0) and resize(MAX + 1) | The 4 newest, then the 2 newest; both bad sizes refused, content unchanged |
| AWL-14 | Rewind on, a CPU loop writing 0x1F; 10 frames; `step_back(1)` | The replay logged nothing; entries before the landing cycle kept, those from it on removed (an entry is stamped at the start of its instruction slot, which the landing re-runs); one Rewind marker last; no StateLoad marker for the ring restore |
| AWL-15 | The same machine, `rewind_to_frame(frame - 3)`   | The no-replay landing: the same truncation and one Rewind marker. The walk crosses every marker (Rewind, Reset and StateLoad): the rewind ring is this session's own history on one clock, and a state load does not clear it; AWL-23/24/27 pin the crossings, AWL-28 the one exception (a StateLoad marker at the landing cycle stays). A state loaded from another session has an unordered clock: a stated limit, best-effort, no row |
| AWL-16 | Two soft resets; a state save and load; a hard reset; a CPU write | One Reset marker for the two resets (the same marker at the same cycle is one row); one StateLoad; one Reset for the hard reset; the write after it is logged (the sink was re-installed) |
| AWL-17 | `audio_log_clear()`; destroy the Debugger with the log on | Ok, the ring empty, state_revision advanced, the next seq follows; after the destructor no tap is installed and a frame runs |
| AWL-18 | An AY write, a DAC fan-out, a dropped NR 0x2D, a soft reset, then `audio_log_export()` | The file's bytes equal a literal: the header and 5 LF lines (R7 resets to 0xFF, `ym2149.vhd:186`); export while off or to an unwritable path is refused |
| AWL-19 | `cfg.silent` (synthesis off); a CPU OUT 0x1F      | Logged: the tap does not need the mixer |
| AWL-20 | `AudioLogStream`, writes with a pump after each, `finish()` | True; the file is the header plus `audio_log_csv_append` of every entry in order; the stream's client is gone and the log off |
| AWL-21 | A ring of 4 and 6 writes between two pumps; a step back removing undrained writes | `finish()` false saying 2 audio writes were overwritten; and true for the truncation, whose file holds the rewind marker row |
| AWL-22 | `AudioLogStream::open` in a directory that does not exist | False, naming the path; `attach()` then refuses and the log stays off |
| AWL-23 | The same machine: `rewind_to_frame(frame - 1)`, then `rewind_to_frame(frame - 3)`; run 3 frames on | No write from the second landing's cycle on survives and the first Rewind marker is gone (the clock is one clock, so the walk crosses it); one Rewind marker last; running on logs no write twice and the cycles increase strictly |
| AWL-24 | 6 frames, soft reset (the clock is kept, `init()`), 3 frames, `rewind_to_frame(frame - 6)` (a landing before the reset) | Every write from the landing cycle on is removed together with the undone Reset marker; the writes before the landing are kept; one Rewind marker last |
| AWL-25 | A landing, then a soft reset, then a second soft reset | The reset is its own Reset row after the Rewind row, at the same cycle; the second reset (same marker, same cycle) is the same row |
| AWL-26 | A soft reset, one frame with the CPU parked, a second soft reset | Two Reset rows at two cycles: only the same marker at the same cycle is one row |
| AWL-27 | 10 frames, a checkpoint, 5 frames, load the checkpoint (the clock goes back), `rewind_to_frame(frame - 3)` (before the checkpoint); run 3 frames on | No write from the landing cycle on survives, the abandoned run before the load included; the undone StateLoad marker is gone; one Rewind marker last; running on logs no write twice |
| AWL-28 | The same with the landing exactly at the load point (`rewind_to_frame(frame + 1)` after the load, the snapshot taken at the save cycle) | The StateLoad marker at that cycle stays, just before the Rewind marker; no write from that cycle on survives |

### 8.7 Audio-write events (GH #532)

An `AudioWrite` event kind: a subscription on the decoded audio writes, filtered by target, chip, register, value/mask, applied or dropped, and source. Oracles: `ym2149.vhd:188,210`, `turbosound.vhd:129,141`, `zxnext.vhd:2974-2980,3088,3091,5413,6989`, `soundrive.vhd:85-101`, and the event system's delivery contract (the raising boundary; one instruction late for a CPU NextREG write and a Copper MOVE). Suite: `audio_debug_test`. AIZ-08 is in 8.2.

| ID | Drive | Expected |
|----|-------|----------|
| AEV-01 | Subscribe an AudioWrite filter with: no valid target (`0x20`); chip 4; a value outside its mask; a flag both required and forbidden; a register set with only DAC targets. Then the default filter | The five are `RefusedUnavailable`; the default is `Ok` |
| AEV-02 | AY chip 0 R7 subscription (Stop), log off; `LD BC,0xFFFD; LD A,7; OUT (C),A; LD B,0xBF; LD A,0x38; OUT (C),A` with R7 preset 0x3F (the data OUT at 0x800B) | Paused at 0x800D with `matched` = the id; the Event: AudioWrite, AyRegister, chip 0, reg 7, value 0x38, prev 0x3F, port 0xBFFD, source Cpu, pc 0x800B, flags 0, reason None; the plain select made no stop (`ym2149.vhd:188`) |
| AEV-03 | The same program, a chip 1 subscription | No stop (near miss: chip) |
| AEV-04 | The same program, register 8 | No stop (near miss: register) |
| AEV-05 | The same program, value/mask low nibble 8 | Stops at the 0x38 write |
| AEV-06 | The same with the value 0x37 | No stop (near miss: value/mask) |
| AEV-07 | Envelope-restart flag subscription; R13 = 8, R13 = 8 again, R12 = 8 | Two stops, one at each R13 OUT, the second with prev 0x08 (an unchanged value still restarts, `ym2149.vhd:188,210`); none at R12 |
| AEV-08 | DAC subscription; `LD A,0x11; OUT (0x1F),A` | Stop at 0x8004; Event Dac, chip 0, port 0x111F (`OUT (n),A` puts A on the high address byte), value 0x11, prev 0x80 |
| AEV-09 | The same subscription; `NEXTREG 0x2D,0x66` then `NOP` | Two events (chips 0 then 3, DacMirror, reg 0x2D, port 0x002D, pc 0x8000); the stop is one instruction late (0x8005), as NextRegWrite is (`soundrive.vhd:85-101`) |
| AEV-10 | The same subscription; an AY data write, an EAR change on 0xFE, a 0xFFFD chip-select command | No stop (near miss: target) |
| AEV-11 | Dropped-only subscription, NR 0x84 bit 6 off, `OUT (C),A` to 0x00B3 | Stop; Event Dac, chip 0xFF, flags Dropped, reason DacFamilyDisabled, port 0x00B3 (`zxnext.vhd:2974-2980`) |
| AEV-12 | The same subscription with the 0xB3 decode on (the write applies) | No stop (near miss: not dropped) |
| AEV-13 | Applied-only AY register subscription; NR 0x06 = 11 (held in reset), a write; NEXTREG 6,0, select 7, a write | No stop for the held write (AyHeldInReset, `zxnext.vhd:6989`); one stop for the applied one |
| AEV-14 | AY chip-select subscription, TurboSound on; the plain select 7, then the command 0xFE | One stop, at the command: chip 1, prev 0, value 0xFE, port 0xFFFD (`turbosound.vhd:129,141`) |
| AEV-15 | Targets ANY with regs {0}; OUT 0x1F (a Dac entry, reg 0); then an AY R0 write | No stop at the DAC write; one stop at R0 (a register set applies to the AY register and mirror targets only) |
| AEV-16 | Source-Dma subscription; a CPU OUT 0x1F; then a DMA byte to 0x00DF | No stop for the CPU write; the DMA write stops, with source Dma and two events (chips 0 and 3) |
| AEV-17 | Mirror subscription; a Copper MOVE 0x2D,0x66 | Two events, source Copper, chips 0 then 3, delivered at the boundary after the instruction during which the MOVE ran |
| AEV-18 | R7 subscription with `once`; two R7 writes | One stop; the subscription then reports `enabled == false`; the second write passes |
| AEV-19 | The same filter, Log action | One SES-06 line per write, no pause |
| AEV-20 | The same filter, Continue with a counting handler | Called once per write with value 0x38 and chip 0; no pause |
| AEV-21 | Log ON and subscription ANY; `port_out` 0xFFFD = 7 then 0xBFFD = 0x22 | The log holds one AyRegister entry with source Debugger and no AudioWrite event fired (a debugger write fires nothing) |
| AEV-22 | A one-entry latch ring (`shrink_ring_for_test(1)`); DAC subscription; `OUT (0xDF),A` (A and D) | One event delivered, chip 0, `overflowed` and `dropped == 1` |
| AEV-23 | Log ON, ANY subscription with a Continue handler, after two frames: a CPU OUT 0xBFFD and a DMA byte to 0x00DF | Each event's cycle, frame, vc, hc and pc equal its log entry's (one stamp function) |
| AEV-24 | Rewind on, a loop writing 0x1F; DAC subscription with a counting handler; 10 frames; `step_back(1)` | The count did not move during the replay; the landing is not a stop of the subscription |
| AEV-25 | `audio_tap_active()` before anything; subscribed; disabled; re-enabled; master off; master on; unsubscribed | false, true, false, true, false, true, false: the tap is on exactly while a live AudioWrite subscription exists |
| AEV-26 | DAC subscription; hard reset (`on_cold_boot_begin`, `emulator_cold_boot`, `on_cold_boot_done`); a CPU OUT 0x1F | The tap is on again on the rebuilt machine and the OUT stops |
| AEV-27 | The AEV-02 stop | `matched[0].addr` and the pause reason's addr are the port 0xBFFD |
| AEV-28 | Two subscriptions (AY chip 0, AY chip 1); a chip 0 write | `matched` holds exactly the chip 0 id (the drain applies the filter) |
| AEV-29 | A Copper MOVE 0x2D,0x5A with the DAC off, subscription ANY: `audio_target` | DacMirror (`zxnext.vhd:5413`) |
| AEV-30 | The same delivery: `port` | 0x002D |
| AEV-31 | The same delivery: `reg` | 0x2D |
| AEV-32 | The same delivery: `value` | 0x5A |
| AEV-33 | The same delivery: `source` | Copper |
| AEV-34 | The same delivery: `chip` | 0xFF (a dropped mirror write reached no channel) |
| AEV-35 | The same delivery: `audio_flags` | Dropped |
| AEV-36 | The same delivery: `drop_reason` | DacMirrorDisabled |
| AEV-37 | A CPU R13 write on chip 2 (R13 preset 0x04, write 0x0A) | prev 0x04, chip 2, flags EnvelopeRestart, reg 13; with AEV-29..36 the four packed bytes carry pairwise distinct values in each fixture, so a swap of any two kills a row |
| AEV-38 | Subscriptions with Dropped required and a channel: DAC and DAC|mirror (chip 1), DAC (0), mirror (3); then chip Any, an AY register target with chip 1, an AY chip-select target with chip 2, ANY with chip 1 | The first three are `RefusedUnavailable` (a dropped DAC write has chip 0xFF); the other four are accepted |
| AEV-39 | ANY subscription, log off; a soft reset and a state save and load, a frame after each | No AudioWrite event and no stop: the tap's Reset and StateLoad markers are not writes |
| AEV-40 | Mirror subscription with regs {0x2D}; `NEXTREG 0x2C,0x11`, `NEXTREG 0x2D,0x66`, `NOP` | Stops one instruction after NR 0x2D only (0x8009); two events, chips 0 and 3, reg 0x2D; none for 0x2C (`soundrive.vhd:85-101`) |

## Test Implementation Strategy

### Phase 1: Unit Tests (Internal State)

Write C++ unit tests that directly instantiate audio component classes and
verify:
- Register read/write behaviour for all AY registers
- Volume table lookups for both AY and YM modes
- Envelope state machine transitions for all 16 shapes
- Soundrive channel latching and DAC output arithmetic
- Mixer input scaling and summation

### Phase 2: I/O Integration Tests

Tests that exercise the full I/O path through the emulator core:
- Port FFFD/BFFD writes to select and write AY registers
- Port BFF5 for register query mode
- Soundrive port writes for all supported port mappings
- Port 0xFE writes for beeper output
- NextREG writes for configuration (0x06, 0x08, 0x09, 0x2C-0x2E)

### Phase 3: Functional Audio Tests

Z80 test programs (built with z88dk) that produce known audio patterns:
- Single-tone output on each AY channel
- Noise on each channel
- Envelope shapes (visual inspection of waveform or sample capture)
- Turbosound: different tones on PSG0/PSG1/PSG2 simultaneously
- Soundrive: write to all four DAC channels
- Stereo routing: verify L/R separation in captured audio

### Test Program Pattern

```z80
; Example: AY tone on Channel A, period=0x100, volume=15
    ld a, 0         ; Register 0 (Ch A fine tone)
    ld bc, 0xFFFD
    out (c), a      ; Select register
    ld a, 0x00      ; Fine period = 0
    ld bc, 0xBFFD
    out (c), a      ; Write value

    ld a, 1         ; Register 1 (Ch A coarse tone)
    ld bc, 0xFFFD
    out (c), a
    ld a, 0x01      ; Coarse period = 1 => total period = 0x100
    ld bc, 0xBFFD
    out (c), a

    ld a, 7         ; Register 7 (mixer)
    ld bc, 0xFFFD
    out (c), a
    ld a, 0x3E      ; Enable tone on Ch A only (bit 0=0, rest=1)
    ld bc, 0xBFFD
    out (c), a

    ld a, 8         ; Register 8 (Ch A volume)
    ld bc, 0xFFFD
    out (c), a
    ld a, 0x0F      ; Volume = 15 (max fixed)
    ld bc, 0xBFFD
    out (c), a
```

## Summary

| Component      | Test IDs    | Count |
|----------------|-------------|------:|
| YM2149 core    | AY-01..132  |   ~66 |
| Turbosound     | TS-01..52, TS-35..38, TS-62..67, TS-60/61 |   ~36 (+G115) |
| Soundrive DAC  | SD-01..23, SD-19   |   ~19 (+G111) |
| Beeper/Tape    | BP-01..23   |   ~14 |
| Audio Mixer    | MX-01..22, MX-23   |   ~15 (+G110) |
| NextREG Config | NR-01..32, NR-33/34, NR-40..42 | ~21 (+G112/G113) |
| I/O Port Wiring| IO-01..12, IO-13..17 |   ~14 (+G114) |
| **Total**      |             | **~175** |
