# Audio Subsystem Compliance Test Plan

VHDL-derived compliance test plan for the audio subsystem of the JNEXT ZX
Spectrum Next emulator. Covers: AY/YM2149, Turbosound (triple AY), Soundrive
4-channel DAC, Beeper/Tape, and the final audio mixer.

All behavioural specifications are derived exclusively from the FPGA VHDL
sources in `ZX_Spectrum_Next_FPGA/cores/zxnext/src/audio/`.

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
| AY-30  | Read R14 with R7 bit 6 = 0 (Port A input mode)  | Returns `port_a_i` (external input)                        |
| AY-31  | Read R14 with R7 bit 6 = 1 (Port A output mode) | Returns `reg(14) AND port_a_i`                             |
| AY-32  | Read R15 with R7 bit 7 = 0 (Port B input mode)  | Returns `port_b_i`                                         |
| AY-33  | Read R15 with R7 bit 7 = 1 (Port B output mode) | Returns `reg(15) AND port_b_i`                             |
| AY-34  | Port A/B inputs default to 0xFF (pullup)         | In turbosound wiring, port_a_i/port_b_i = all 1s          |

### 1.4 Clock Divider

VHDL ref: `ym2149.vhd` lines 260-279

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-40  | Divider reloads with `I_SEL_L=1` (AY compat)    | Reload value = `"0111"` (divide by 8)                      |
| AY-41  | Divider reloads with `I_SEL_L=0` (YM mode)      | Reload value = `"1111"` (divide by 16)                     |
| AY-42  | `ena_div` pulses once per divider cycle          | Tone generators clocked at this rate                       |
| AY-43  | `ena_div_noise` at half `ena_div` rate           | Noise generator runs at half the tone clock                |
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
| AY-63  | Noise output is poly17 bit 0                     | Single shared noise for all channels                       |
| AY-64  | Noise clocked at `ena_div_noise` rate            | Half the tone generator clock rate                         |

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
| AY-113 | 9     | 09  | `\___`            | C=1,At=0,Al=0,H=1: down once, hold at 0                 |
| AY-114 | 10    | 0A  | `\/\/` (triangle) | C=1,At=0,Al=1,H=0: down then up, repeat                 |
| AY-115 | 11    | 0B  | `\` then hold max | C=1,At=0,Al=1,H=1: down, then alt+hold at max           |
| AY-116 | 12    | 0C  | `////` (saw up)   | C=1,At=1,Al=0,H=0: repeat up cycles                     |
| AY-117 | 13    | 0D  | `/` then hold max | C=1,At=1,Al=0,H=1: up once, hold at max                 |
| AY-118 | 14    | 0E  | `/\/\` (triangle) | C=1,At=1,Al=1,H=0: up then down, repeat                 |
| AY-119 | 15    | 0F  | `/___`            | C=1,At=1,Al=1,H=1: up once, hold at 0                   |

**Envelope state machine details:**

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| AY-120 | Attack=0 (At bit): initial vol=31, direction=down| `env_vol="11111"`, `env_inc='0'`                           |
| AY-121 | Attack=1 (At bit): initial vol=0, direction=up   | `env_vol="00000"`, `env_inc='1'`                           |
| AY-122 | C=0: hold after first ramp regardless of Al/H    | Single ramp then stop                                      |
| AY-123 | C=1, H=1, Al=0: hold at end of first ramp        | Hold when reaching boundary                                |
| AY-124 | C=1, H=1, Al=1: hold at opposite boundary        | Alternate direction once, then hold                        |
| AY-125 | C=1, H=0, Al=1: triangle wave (continuous)       | Direction reverses at each boundary, no hold               |
| AY-126 | C=1, H=0, Al=0: sawtooth (continuous)            | One-direction hold then restart (no direction reversal)    |
| AY-127 | Envelope steps through 32 levels (0-31)          | 5-bit counter increments/decrements by 1                   |
| AY-128 | Envelope period counter reset on R13 write        | Counter and ena both reset                                 |

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
| TS-17  | Write routed to selected AY only                 | Only selected PSG gets `busctrl_we = 1`                    |
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

VHDL ref: `turbosound.vhd` lines 194-205, 249-260, 304-315

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-30  | Turbosound disabled: only selected PSG outputs    | Non-selected PSGs output zero                              |
| TS-31  | Turbosound enabled: all three PSGs output         | All PSGs contribute to the mix regardless of selection     |
| TS-32  | PSG0 active when `ay_select="11"` or ts enabled   | PSG0 zeroed only when not selected AND ts disabled         |
| TS-33  | PSG1 active when `ay_select="10"` or ts enabled   | Same logic for PSG1                                        |
| TS-34  | PSG2 active when `ay_select="01"` or ts enabled   | Same logic for PSG2                                        |

### 2.5 Panning and Final Sum

VHDL ref: `turbosound.vhd` lines 323-337

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| TS-40  | Pan "11": output to both L and R                  | Normal stereo                                              |
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
| SD-02  | Write channel A via port I/O (`chA_wr_i`)        | `chA` latches `cpu_d_i`                                    |
| SD-03  | Write channel B via port I/O (`chB_wr_i`)        | `chB` latches `cpu_d_i`                                    |
| SD-04  | Write channel C via port I/O (`chC_wr_i`)        | `chC` latches `cpu_d_i`                                    |
| SD-05  | Write channel D via port I/O (`chD_wr_i`)        | `chD` latches `cpu_d_i`                                    |
| SD-06  | NextREG 0x2D (mono) writes to chA AND chD        | Both channels A and D updated simultaneously               |
| SD-07  | NextREG 0x2C (left) writes to chB only           | Channel B updated                                          |
| SD-08  | NextREG 0x2E (right) writes to chC only          | Channel C updated                                          |
| SD-09  | Port I/O takes priority over NextREG              | If both fire same cycle, port I/O wins (checked first)     |

### 3.2 Port Mapping (from zxnext.vhd)

VHDL ref: `zxnext.vhd` lines 2429-2435, 2658-2664

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| SD-10  | Soundrive mode 1 ports: 0x1F(A), 0x0F(B), 0x4F(C), 0x5F(D) | `port_dac_sd1_ABCD_1f0f4f5f_io_en`          |
| SD-11  | Soundrive mode 2 ports: 0xF1(A), 0xF3(B), 0xF9(C), 0xFB(D) | `port_dac_sd2_ABCD_f1f3f9fb_io_en`          |
| SD-12  | Profi Covox: 0x3F(A), 0x5F(D)                    | `port_dac_stereo_AD_3f5f_io_en`                           |
| SD-13  | Covox: 0x0F(B), 0x4F(C)                          | `port_dac_stereo_BC_0f4f_io_en`                           |
| SD-14  | Pentagon/ATM mono: 0xFB(A+D)                      | `port_dac_mono_AD_fb_io_en` (disabled when mode 2 active) |
| SD-15  | GS Covox: 0xB3(B+C)                              | `port_dac_mono_BC_b3_io_en`                                |
| SD-16  | SpecDrum: 0xDF(A+D)                               | `port_dac_mono_AD_df_io_en`                                |
| SD-17  | DAC requires `nr_08_dac_en=1`                     | Soundrive held in reset when `nr_08_dac_en=0`             |
| SD-18  | Mono ports (FB, DF, B3) write to both A+D or B+C | Single port write updates two channels                     |

### 3.3 PCM Output

VHDL ref: `soundrive.vhd` lines 109-116

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| SD-20  | Left output = chA + chB (9-bit unsigned)          | `pcm_L_o = ('0' & chA) + ('0' & chB)`                     |
| SD-21  | Right output = chC + chD (9-bit unsigned)         | `pcm_R_o = ('0' & chC) + ('0' & chD)`                     |
| SD-22  | Max output: chA=0xFF, chB=0xFF => L=0x1FE        | No overflow, 9-bit result                                  |
| SD-23  | Reset output: L=0x100, R=0x100                    | 0x80 + 0x80 = 0x100 (DC midpoint)                         |

## 4. Beeper and Tape Audio

### 4.1 Port 0xFE Write

VHDL ref: `zxnext.vhd` lines 3591-3599

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| BP-01  | Port 0xFE write stores bits [4:0]                | `port_fe_reg <= cpu_do(4 downto 0)`                        |
| BP-02  | Bit 4 is the EAR output (speaker)                | `port_fe_ear <= port_fe_reg(4)`                            |
| BP-03  | Bit 3 is the MIC output                          | `port_fe_mic <= port_fe_reg(3)`                            |
| BP-04  | Bits [2:0] are the border colour                 | `port_fe_border <= port_fe_reg(2 downto 0)`                |
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

### 4.3 Port 0xFE Read

VHDL ref: `zxnext.vhd` lines 3453-3468

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| BP-20  | Port 0xFE read bit 6 = `EAR_in OR port_fe_ear`  | Bit 6 reflects tape input OR own speaker output            |
| BP-21  | Port 0xFE read bit 5 = 1 (always set)           | Fixed high bit                                              |
| BP-22  | Port 0xFE read bits [4:0] = keyboard columns    | From `i_KBD_COL`                                           |
| BP-23  | Port 0xFE read bit 7 = 1                        | Fixed high bit                                              |

## 5. Audio Mixer

### 5.1 Input Scaling

VHDL ref: `audio_mixer.vhd` lines 63-90

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-01  | EAR volume = 0x0200 (512) when active             | `ear_volume = "0001000000000"` when `ear_i=1, exc_i=0`    |
| MX-02  | MIC volume = 0x0080 (128) when active             | `mic_volume = "0000010000000"` when `mic_i=1, exc_i=0`    |
| MX-03  | EAR/MIC silenced when `exc_i=1`                   | Speaker-exclusive mode zeroes beeper in mix                |
| MX-04  | AY input: zero-extended 12-bit to 13-bit          | `ay_L = '0' & ay_L_i` (range 0-2295)                      |
| MX-05  | DAC input: 9-bit left-shifted by 2 + zero-padded  | `dac_L = "00" & dac_L_i & "00"` (range 0-2040)            |
| MX-06  | I2S input: zero-extended 10-bit to 13-bit         | `i2s_L = "000" & pi_i2s_L_i` (range 0-1023)               |

### 5.2 Final Mix

VHDL ref: `audio_mixer.vhd` lines 92-107

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-10  | Left output = ear + mic + ay_L + dac_L + i2s_L   | Simple addition, 13-bit result (range 0-5998)              |
| MX-11  | Right output = ear + mic + ay_R + dac_R + i2s_R   | Same formula for right channel                             |
| MX-12  | Reset zeroes both output channels                  | `pcm_L = 0`, `pcm_R = 0`                                  |
| MX-13  | EAR and MIC go to both L and R                     | Beeper is always mono in the mix                           |
| MX-14  | Max theoretical output = 5998                      | 512 + 128 + 2295 + 2040 + 1023 = 5998                     |
| MX-15  | No saturation/clipping in mixer                    | 13-bit output is wide enough; no overflow possible         |

### 5.3 Speaker-Exclusive Mode

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| MX-20  | `exc_i=1`: EAR and MIC contribute 0 to mix       | Beeper only goes to internal speaker, not line out         |
| MX-21  | `exc_i=0`: EAR and MIC contribute normally        | Default behaviour                                          |
| MX-22  | `exc_i` derived from NextREGs 0x06 bit 6 AND 0x08 bit 4 | `beep_spkr_excl = nr_06_internal_speaker_beep AND nr_08_internal_speaker_en` |

## 6. NextREG Configuration

### 6.1 PSG Mode (NextREG 0x06)

VHDL ref: `zxnext.vhd` lines 5163-5170, 6379

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| NR-01  | `nr_06_psg_mode[1:0]` from NextREG 0x06 bits [1:0] | Two-bit mode selector                                   |
| NR-02  | Mode "00": YM2149 mode                            | `aymode_i = 0` (YM volume table, full register readback)  |
| NR-03  | Mode "01": AY-8910 mode                           | `aymode_i = 1` (AY volume table, masked readback)         |
| NR-04  | Mode "10": YM2149 mode (bit 0 = 0)                | Same as "00" for AY chip behaviour                        |
| NR-05  | Mode "11": AY reset (silent)                       | `audio_ay_reset = 1`; all AY output zeroed                |
| NR-06  | `nr_06_internal_speaker_beep` from bit 6          | Controls speaker-exclusive mode                            |

### 6.2 Audio Config (NextREG 0x08)

VHDL ref: `zxnext.vhd` lines 5176-5182

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| NR-10  | Bit 5: PSG stereo mode (0=ABC, 1=ACB)            | `nr_08_psg_stereo_mode`                                    |
| NR-11  | Bit 4: Internal speaker enable                    | `nr_08_internal_speaker_en`                                |
| NR-12  | Bit 3: DAC enable                                  | `nr_08_dac_en`; when 0, Soundrive held in reset           |
| NR-13  | Bit 1: Turbosound enable                           | `nr_08_psg_turbosound_en`                                  |
| NR-14  | Bit 0: Keyboard Issue 2 mode                       | `nr_08_keyboard_issue2`; affects beeper signal             |

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

## 7. I/O Port Wiring

### 7.1 AY Ports

VHDL ref: `zxnext.vhd` lines 2647-2649, 2771-2773

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| IO-01  | Port FFFD: `A[15:14]="11"`, A[2]=1, A[0]=1      | AY register select (write) / read data                    |
| IO-02  | Port BFFD: `A[15:14]="10"`, A[2]=1, A[0]=1      | AY register write                                          |
| IO-03  | Port BFF5: BFFD with A[3]=0                       | Register query mode (`psg_d_o_reg_i`)                      |
| IO-04  | FFFD read latched on falling CPU clock edge       | `port_fffd_dat <= psg_dat` on `falling_edge(i_CLK_CPU)`   |
| IO-05  | BFFD readable as FFFD on +3 timing                | `port_fffd_rd` includes `port_bffd and machine_timing_p3` |

### 7.2 DAC Port Enable

VHDL ref: `zxnext.vhd` lines 2775-2778

| ID     | Test                                             | Verification                                               |
|--------|--------------------------------------------------|------------------------------------------------------------|
| IO-10  | DAC writes require `dac_hw_en=1`                   | Gated by `nr_08_dac_en`                                    |
| IO-11  | Multiple port mappings can map to same channel     | E.g., chA can be written by ports 0x1F, 0xF1, 0x3F, 0xFB, 0xDF |
| IO-12  | Port FD conflict: F1 and F9 in mode 2             | `port_fd_conflict_wr` prevents AY false triggers           |

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
| YM2149 core    | AY-01..128  |   ~62 |
| Turbosound     | TS-01..52   |   ~28 |
| Soundrive DAC  | SD-01..23   |   ~18 |
| Beeper/Tape    | BP-01..23   |   ~14 |
| Audio Mixer    | MX-01..22   |   ~14 |
| NextREG Config | NR-01..32   |   ~16 |
| I/O Port Wiring| IO-01..12   |    ~9 |
| **Total**      |             | **~161** |
