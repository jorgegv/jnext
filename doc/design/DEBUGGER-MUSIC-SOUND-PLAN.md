# Music and sound panels in the debugger: execution plan (epic #224)

Status: **DRAFT for owner review** (2026-11-17; owner answers to §10 folded in 2026-11-18).
Nothing in this plan is started. Once the owner approves it, #224 gets **one sub-issue per
scope item, eleven in all** (decision 7), on the epic branch `epic224-audio-debugger`. Each
sub-issue carries its tasks below as its checklist, branches off the epic branch, and merges
back into it. **The epic branch reaches `main` twice** (owner, 2026-11-18, §10 Q1): once when
Phase 1 (sub-issues 1-5) is done, and again when Phases 2 and 3 are done. Between the two it
continues on the same branch, with `main` merged into it first. Each merge into `main` follows
the epic rule: the batch gate on the epic branch's tip and a green CI run on the pushed epic
branch (CLAUDE.md, merge protocol steps 1 and 2).

The scope is the owner's. It comes from #224's "UPDATE 2026/11/17: scope (approved by
jorgegv, 2026-11-17)" block and its Decisions 1-11 (10 and 11 added 2026-11-18), and this plan
does not reopen it:

- **Phase 1, see and stop.** (1) A decoded chip view with notes and cents, envelope glyph,
  note and position, and a hex toggle. (2) Routing and "why is it silent": DAC values,
  enables, port families, beeper and speaker bits, Pi I2S, host gains and mutes, and a
  dropped-write counter. (3) An audio write log, recorded only while something needs it, with
  CSV export from the GUI and `--audio-log FILE`. (4) Breakpoints on decoded audio writes: the
  `AudioWrite` event kind, a `.jds` `ay_write` event and an AY register accessor, and an
  "Audio write" type in the GUI. (5) Solo and mute for the 9 AY and the 4 DAC channels.
- **Phase 2, see over time.** (6) A tracker view. (7) Player timing. (8) Oscilloscopes and
  meters. (9) A DAC sample-stream view next to the CTC and DMA that pace it.
- **Phase 3, take it elsewhere.** (10) A `.psg` register stream per AY chip. (11) Per-source
  WAV stems that sum to the mix.
- **Decisions.** (1) The `debugger.h` additions form one bundle, specified here (§4.4).
  (2) Notes use the emulated clock only: 28 MHz master, 1.75 MHz PSG, A4 = 440 Hz. (3) The
  decoded view, meters and scopes refresh at display rate while visible; everything else stays
  at about 4 Hz. (4) `AUDIO_MUTE` stays as it is; a separate 13-bit `AUDIO_CHANNEL_MUTE` is
  added, and Solo is GUI-only. (5) Mutes and solos are debugger state, scriptable, with no
  `--set` keys; the CLI flags are the log and the exports. (6) Audio becomes a tab with
  sub-tabs: Chips, DAC & Beeper, Log, Tracker, Scopes. (7) Epic branch, this document first,
  one sub-issue per item. (8) Phase 3 is part of this epic. (9) The `.psg` writer is checked
  against Fuse's recording of the same 128K program; this document decides whether a pinned
  ZXTune is also provisioned (§7.2: it is not). (10) Phase 1 merges into `main` on its own
  when it is done; Phases 2 and 3 follow on the epic branch. (11) The `.jds` accessor is
  `ay[c, r]`, matching `phys[page, offset]`, with no grammar change.
- **Out of scope** (from the issue): player-variable decoding, FFT, piano roll, VGM and YM
  export, editing registers from the panel, beeper pitch decoding, audio over DZRP, GDB or
  ZRCP, Pi I2S stream analysis, and the SDL frontend (the CLI items 3, 10 and 11 still work in
  every frontend).

---

## Task list

One heading per sub-issue, i.e. per scope item 1-11 (decision 7). The task lines under a
heading are that sub-issue's checklist, one line per task, with stable IDs; the detail is in §5
to §7. Shared groundwork goes with the first item that needs it: A1.1 (note math and the core
suite), A1.2 (the mute fix and the invisibility harness) and A1.3 (`audio_state()`) are in
sub-issue 1, because the decoded chip view is their first consumer and A1.3's own rows already
use the harness. Each sub-issue updates the user guide, developer guide and man page for what it
changes (a new flag's man page entry has to land with it, or `make cli-check` fails). The three
doc tasks A1.13, A2.8 and A3.5 are the closing consolidation pass of their phase. They are
**epic-level tasks**, tracked on #224 itself and done on the epic branch before the phase's merge
into `main`, so that no sub-issue has to wait for the whole phase (§ "Sub-issues and
parallelization"). When sub-issues 1-5 and A1.13 are merged into the epic branch, Phase 1 is
complete and the epic branch merges into `main` (decision 10); sub-issues 6-11 then continue on
it.

### Phase A1: see and stop

#### Sub-issue 1: decoded chip view (scope item 1)

- [ ] **A1.1** New core suite `audio_debug_test`, plus a published pure helper for PSG tone and envelope frequency, note name and cents
- [ ] **A1.2** Take the debugger mutes out of machine state: the AY chip mute leaks into the serialised `pcm_l`/`pcm_r`; add the "invisible to the Z80" harness
- [ ] **A1.3** `audio_state()` backend query: chips, envelope position, select and pan, routing, host gains and mutes (INS-22)
- [ ] **A1.4** Audio tab with sub-tabs, and the Chips sub-tab: decoded view, hex toggle, live level bars, display-rate refresh while visible

#### Sub-issue 2: routing and "why is it silent" (scope item 2)

- [ ] **A1.5** Dropped-write classification at the AY, DAC and NR 0x2C-0x2E sites, with always-on counters and `clear_audio_drops`
- [ ] **A1.6** DAC & Beeper sub-tab: DAC A-D, enables, port families, beeper and speaker, Pi I2S, gains and mutes, dropped-write table

#### Sub-issue 3: audio write log (scope item 3)

- [ ] **A1.7** Audio write tap and ring log in the backend (INS-23), CSV export, `--audio-log FILE`
- [ ] **A1.8** Log sub-tab: filters, envelope-restart flag, double-click to Disassembly, Export CSV, collected only while visible

#### Sub-issue 4: breakpoints on decoded audio writes (scope item 4)

- [ ] **A1.9** `AudioWrite` event kind: latched at the tap, cheap filters (target, chip, register, value/mask, flags), delivered at the boundary
- [ ] **A1.10** "Audio write" breakpoints in the GUI: a `BreakpointModel` type and the Add dialog fields
- [ ] **A1.11** DSL: the `ay_write` event with payload `CHIP REG VALUE PREV` and an `ay[c, r]` accessor

#### Sub-issue 5: solo and mute per channel (scope item 5)

- [ ] **A1.12** Per-channel mute and solo: the 13-bit host mask in TurboSound and the Dac, backend verbs, `AUDIO_CHANNEL_MUTE` in the DSL, Solo and Mute controls

#### Epic-level task before the Phase 1 merge into `main`

- [ ] **A1.13** Phase 1 doc pass: user guide Audio page, developer guide 3.4, 3.9 and 3.10.2, scripting quick reference, FEATURES.md, banner recount

### Phase A2: see over time

#### Sub-issue 6: tracker view (scope item 6)

- [ ] **A2.1** Frame grouping helper (pure): per-frame final registers, double writes, first and last write, write count, PC range
- [ ] **A2.2** Tracker sub-tab: one row per frame, 3 chips side by side, changes highlighted, empty and double frames marked, jump back via rewind

#### Sub-issue 7: player timing (scope item 7)

- [ ] **A2.3** Player timing in the Tracker sub-tab: first and last write, count, PC range, min/avg/max over N frames, jitter strip

#### Sub-issue 8: oscilloscopes and meters (scope item 8)

- [ ] **A2.4** Source tap in the Mixer: per-source box-filtered samples at 44.1 kHz, only while requested (INS-24), bench A/B
- [ ] **A2.5** Scopes sub-tab: 9 AY channels, 4 DAC channels, beeper and L/R mix, edge-triggered, with meters, at display rate

#### Sub-issue 9: DAC sample-stream view (scope item 9)

- [ ] **A2.6** Read-only CTC and DMA queries (INS-25)
- [ ] **A2.7** Sample-stream view in DAC & Beeper: rate, intervals, jitter and gaps per writer, beside the CTC and DMA pacing

#### Epic-level task after sub-issues 6-9

- [ ] **A2.8** Phase 2 doc pass: user guide (tracker, timing, scopes, sample stream), developer guide 3.4 and 3.9, FEATURES.md, banner recount

### Phase A3: take it elsewhere

#### Sub-issue 10: `.psg` register-stream export (scope item 10)

- [ ] **A3.1** `.psg` writer, one stream per AY chip, fed from the write tap; byte rows against hand-built files
- [ ] **A3.2** Fuse reference: a 128K fixture program, a maintainer tool that records it in Fuse, and the committed recording compared byte for byte
- [ ] **A3.3** `.psg` export from the GUI (start/stop) and `--psg-record FILE`

#### Sub-issue 11: per-source WAV stems (scope item 11)

- [ ] **A3.4** Per-source WAV stems before host gain (AY0-2, DAC, beeper, Pi, mix), `--wav-record-stems DIR`, GUI start/stop

#### Epic-level task before the final merge into `main`

- [ ] **A3.5** Phase 3 doc pass, FEATURES.md, the epic's bench record and the ChangeLog draft

## Sub-issues and parallelization

The 11 sub-issues are the owner's (decision 7, confirmed 2026-11-18). "Depends on" is taken from
the tasks' Depends lines (§5 to §7), lifted to sub-issues; "Can run in parallel with" is every
other sub-issue that is neither a direct nor an indirect dependency of it, nor depends on it,
directly or indirectly. That relation is symmetric by construction (computed from the graph, not
by hand). The doc passes A1.13, A2.8 and A3.5 are epic-level and depend on every sub-issue of
their phase.

| Sub-issue | Tasks | Depends on | Can run in parallel with |
|---|---|---|---|
| 1. Decoded chip view | A1.1, A1.2, A1.3, A1.4 | none | none |
| 2. Routing and dropped writes | A1.5, A1.6 | 1 | 5, 8, 11 |
| 3. Audio write log | A1.7, A1.8 | 1, 2 | 5, 8, 11 |
| 4. Audio-write breakpoints and DSL | A1.9, A1.10, A1.11 | 3 | 5, 6, 7, 8, 9, 10, 11 |
| 5. Channel mute and solo | A1.12 | 1 | 2, 3, 4, 6, 7, 8, 9, 10, 11 |
| 6. Tracker view | A2.1, A2.2 | 3 | 4, 5, 8, 9, 11 |
| 7. Player timing | A2.3 | 6 | 4, 5, 8, 9, 10, 11 |
| 8. Oscilloscopes and meters | A2.4, A2.5 | 1 | 2, 3, 4, 5, 6, 7, 9, 10 |
| 9. DAC sample-stream view | A2.6, A2.7 | 3 | 4, 5, 6, 7, 8, 10, 11 |
| 10. `.psg` export | A3.1, A3.2, A3.3 | 6 | 4, 5, 7, 8, 9, 11 |
| 11. WAV stems | A3.4 | 8 | 2, 3, 4, 5, 6, 7, 9, 10 |

Where the edges come from: 2 needs `audio_state()` (A1.5 on A1.3); 3 needs the drop
classification its records carry (A1.7 on A1.5); 4 latches at the tap (A1.9 on A1.7); 5 needs
the audible copy and the Chips sub-tab (A1.12 on A1.2, A1.4); 6 groups log entries (A2.1 on
A1.7, A2.2 on A1.8); 7 uses the frame grouping (A2.3 on A2.1); 8 reads the hardware aggregate
and lives in the Audio tab (A2.4 on A1.2, A2.5 on A1.4); 9 reads DAC writes from the log (A2.7
on A1.7; A2.6 alone has no dependency); 10's writer uses the frame accumulator (A3.1 on A2.1); 11
uses the source tap (A3.4 on A2.4).

**Merge order is stricter than dependency** (decision 10). Phase 1 (sub-issues 1-5, then
A1.13) lands on `main` on its own, so sub-issues 6-11 may be *worked on* in parallel with
sub-issues 2-5 as the table allows, but they **merge into the epic branch only after the Phase 1
merge into `main`**. Sub-issues that touch the same file also merge one at a time, the second
rebased on the first: 5, 8 and 11 all edit `src/audio/mixer.{h,cpp}`; 2 and 5 both edit
`src/audio/turbosound.{h,cpp}` (A1.5's status returns, A1.12's channel mask); 2, 3 and 4 all edit
the audio handlers in `src/core/emulator.cpp`; every sub-issue with a Qt part edits
`audio_panel.cpp`.

**Critical path:** 1 -> 2 -> 3 -> 6 -> 10 (13 tasks: A1.1-A1.4, A1.5-A1.6, A1.7-A1.8,
A2.1-A2.2, A3.1-A3.3), with 1 -> 2 -> 3 -> 6 -> 7 as long in sub-issues but shorter in tasks.
For Phase 1 alone it is 1 -> 2 -> 3 -> 4, then A1.13 and the merge into `main`. Sub-issues 5 and
8 can start as soon as sub-issue 1 has merged into the epic branch, and 11 as soon as 8 has; A2.6
(the first half of 9) can start at once.

---

## 1. Sources and evidence

Every claim about code cites a path and a symbol. Line numbers are as of the tree named below;
the VHDL line numbers are from the current FPGA checkout, and several jnext comments cite older
ones (for example the soundrive reset at `zxnext.vhd:6436`, which is `:7046` now).

| Source | Where | State read |
|---|---|---|
| jnext | this repository | `main` @ `ad916bc6d` (v1.3.5) |
| FPGA VHDL | `ZX_Spectrum_Next_FPGA/cores/zxnext/src` | checkout HEAD `15e206d` |
| Fuse 1.6.0 (packaged) | `/usr/bin/fuse`, `fuse-emulator-1.6.0-16.fc45` | run under Xvfb, 2026-11-18 |
| Fuse 1.6.0 source | a local unpacked tarball, `psg.c`, `peripherals/ay.c`, `spectrum.c` | read |
| Scope evidence | `$R/gh224-scope-proposal.md`, evidence appendix | each citation used here re-checked |

Statements nobody has measured are marked **(unmeasured)**.

---

## 2. What exists today

### 2.1 The Audio tab and its backend

- `AudioPanel` (`src/debugger/audio_panel.cpp`) shows a 16 x 3 hex table, five source mute
  checkboxes (`apply_source_mutes()`) and three status lines. It is one tab of
  `DebuggerWindow`'s `tab_widget_` (`debugger_window.cpp`, `addTab(audio_panel_, tr("Audio"))`).
- While the machine runs, every panel redraws every `REFRESH_INTERVAL` = 12 frames
  (`debugger_manager.h:370`, `DebuggerManager` tick at `debugger_manager.cpp:1003-1008`).
- Backend INS-10 (`src/debug/debugger.h:636-649`): `ay_registers()`, `turbosound_enabled()`,
  `ay_mode()`, `stereo_mode()`, `audio_mute_mask()`, `set_audio_mute_mask()`. `AyChipMode`
  (`inspect.h`) has two values and cannot represent NR 0x06 = 11 (held in reset) or 10.
- `debugger.h` is frozen ("additions are by owner decision only", banner lines 12-14). Its
  DIRECT-VALUE QUERIES banner (`:311`) lists 52 queries that cannot refuse; every new one moves
  that count and the list.
- `.jds` reads and sets `AUDIO_MUTE` (`src/script/names.cpp`, `LIVE[]`), and can watch
  `io_write` and `nextreg` writes. It has no AY register accessor and no `CHIP` payload.
- The GUI breakpoint types are `Execute, Read, Write, ReadWrite, IoRead, IoWrite`
  (`breakpoint_model.h:49`; Add dialog `breakpoint_panel.cpp:233-241`).
- Older tools: `--wav-record` (post-gain mix, refused with `--silent`, `main.cpp:1792`),
  `--dac-trace` (`Dac::set_write_callback`, CSV header `segment,tstate,channel,value`,
  `dac_trace_recorder.cpp:27`), per-source host gains (`Mixer::set_*_gain_db`).

### 2.2 The audio core, as the taps will see it

- **TurboSound** (`src/audio/turbosound.{h,cpp}`): three `AyChip`s, `ay_select_`, `pan_[3]`,
  `reset_held_`, `enabled_`, `stereo_mode_`, `mono_mode_`. `tick()` recomputes the mix only when
  a chip output or a mix input changed (GH #440, `mix_dirty_`). Private: `ay_select_`, `pan_`,
  `active_ay_index()`.
- **AyChip** (`ay_chip.h`): the register file, the 5-bit `addr_`, and the generators. Private:
  envelope `env_vol_`, `env_inc_`, `env_hold_`, the volume tables. `selected_register()` returns
  `addr_ & 0x0F` and so hides bit 4, which the hardware uses to ignore data writes
  (`ym2149.vhd:188`). The decoded view needs the full 5-bit address.
- **Dac** (`dac.h`): `ch_[4]` is private; only the sums `pcm_left()` and `pcm_right()` are public.
- **Mixer** (`mixer.{h,cpp}`): `accumulate_impl_()` runs once per instruction slice and
  integrates the 13-bit mix (`mix()`), and `emit_sample_impl_()` emits one 44.1 kHz sample. The
  HALT batch replays both through `integrate_span()` (GH #452), and `--silent` skips them
  entirely (`audio_off_`, `emulator.cpp:181`). The debugger mute is already a one-branch gate
  in `mix()` (`if (mute_mask_ != AudioMute::NONE)`, `mixer.cpp:143`); that is the cost model
  the new taps follow.
- **Port sites** (`emulator.cpp`, "Audio port handlers", from `:5384`): the AY handlers
  (`:5395-5453`) and the DAC handlers (`:5466-5660`) are the places a write is applied or
  silently dropped. The NR 0x2C-0x2E handlers are at `:6124-6135`, and the beeper is set at
  `:5136-5137`.
- **Event stamping**: `set_latch_stamper()` (`emulator.cpp:15604`) derives frame, `vc`, `hc`
  and PC from one cycle value. The audio log reuses that arithmetic so its timestamps match
  the event system's.

### 2.3 Findings this epic fixes on its branches (CLAUDE.md: findings are fixed, not filed)

| # | Finding | Evidence | Fixed in |
|---|---|---|---|
| F1 | **The AY chip mute is in the machine state.** `compute_chip_stereo()` returns 0 for a muted chip (`turbosound.cpp:224`), `compute_stereo_mix()` sums that into `pcm_L_`/`pcm_R_`, and `describe_state()` serialises both (`d.u16("pcm_l", pcm_L_)`, `:251`). So a snapshot or rewind slot taken while muted differs from one taken unmuted, and a state loaded without the mute plays the muted aggregate until the next PSG tick (`chip_pcm_valid_ = false` path). `audio_mute.h` promises the mute is "NOT machine state". | read; the A1.2 row demonstrates it before the fix | A1.2 |
| F2 | `AyChip::selected_register()` drops address bit 4; a register select of 0x10-0x1F makes every later data write vanish (`ym2149.vhd:173,188`), and nothing shows it | read | A1.3 (5-bit `address` in the state), A1.5 (drop reason) |
| F3 | Stale VHDL line citations in the audio handlers' comments (`zxnext.vhd:2647/2648/6379/6389/6436`, now `:2963/2964/6989/6999/7046`) | grep against the current checkout | refreshed only where a task touches the line |

---

## 3. What the hardware does (the panels' oracle)

### 3.1 PSG clock and tone

- **PSG clock enable 1.75 MHz** = 28 MHz / 16: `CLK_28_PSG_EN <= clkdiv_3_0` with
  `clkdiv_3_0 = '1' when clk_28_div(3 downto 0) = "1111"` (`zxnext_top_issue2.vhd:1209,1217`),
  wired to every ym2149 (`zxnext.vhd:6995`). jnext ticks the PSG every 16 master cycles
  (`PSG_DIVISOR = 16`, `emulator.cpp:11897`) of `MASTER_CLOCK_HZ = 28'000'000`
  (`emulator_config.h:566`).
- **Internal divider**: `cnt_div` reloads with `(not I_SEL_L) & "111"` and `I_SEL_L = '1'`
  (`turbosound.vhd:164`), so `ena_div` pulses once per 8 PSG ticks and `ena_div_noise` once
  per 16 (`ym2149.vhd:258-277`).
- **Tone**: `tone_gen_comp = TP - 1`, or 0 when `TP(11:1) = 0`, so TP 0 and 1 behave the same;
  the output toggles every `comp + 1` divider ticks (`ym2149.vhd:306-325`). The full period is
  16 x max(TP,1) PSG ticks, so:

  **f_tone = psg_clock / (16 x max(TP, 1)) = 109 375 Hz / max(TP, 1)** at 1.75 MHz.

- **Noise**: comparator `R6(4:0) - 1`, or 0 when `R6(4:1) = 0`, stepped on `ena_div_noise`
  (`ym2149.vhd:283-298`).

### 3.2 Note name and cents (decision 2)

- `midi = 69 + 12 x log2(f / 440)`. The note is `round(midi)` with ties going to the upper
  note, so cents fall in [-50, +50); the name uses sharps and scientific octaves (A4 = MIDI 69,
  C4 = 60); `cents = 100 x (midi - note)`.
- Only MIDI 0..127 (8.18 Hz to 12.5 kHz) gets a name. Outside that range the view shows `-`,
  which happens with TP 1-8 (109 kHz down to 13.7 kHz, the usual "volume-only" PCM trick).
- The clock comes from the backend (`AudioState::psg_clock_hz`, filled from
  `MASTER_CLOCK_HZ / 16`), never from a literal in the panel.
- Worked values (computed by `python3` for this plan; they become A1.1's oracle table):

| TP | f (Hz) | note | cents |
|---|---|---|---|
| 1 (and 0) | 109 375.000 | out of range | `-` |
| 252 | 434.028 | A4 | -23.66 |
| 253 | 432.312 | A4 | -30.52 |
| 239 | 457.636 | A#4 | -31.96 |
| 428 | 255.549 | C4 | -40.68 |
| 4095 | 26.709 | G#0 | +49.50 |

  The usual 128K tables were computed for 1.7734 MHz (A4 = TP 251.9, so 252): on the Next they
  play 1200 x log2(1.75 / 1.7734) = **-23.0 cents** flat, which is the scope's "about 23 cents".

### 3.3 Envelope: shape, position and note

- `env_gen_comp = EP - 1`, or 0 when `EP(15:1) = 0`; `env_ena` pulses once per max(EP, 1)
  divider ticks (`ym2149.vhd:334-350`). Each pulse moves `env_vol` (5 bits) one step, wrapping,
  and the shape logic sets `env_hold` (`:352-420`). A write to R13 sets `env_reset`
  (`ym2149.vhd:209-211`), which reloads `env_vol` = 31 (attack 0) or 0 (attack 1) and clears
  hold (`:393-401`). **Every R13 write restarts the envelope, even with an unchanged value.**
- One envelope step lasts 8 x max(EP, 1) PSG ticks. The repeating shapes are those with C = 1
  and Hold = 0, which are 8, 10, 12 and 14:
  - sawtooth (8 `\\\\`, 12 `////`): 32 steps per cycle, so **f = psg_clock / (256 x max(EP,1))**;
  - triangle (10 `\/\/`, 14 `/\/\`): 64 steps, so **f = psg_clock / (512 x max(EP,1))**.
  - EP = 100: saw 68.36 Hz (C#2 -23.55 cents), triangle 34.18 Hz (C#1 -23.55 cents).
- Position = `env_vol` (step 0-31) and the motion: Holding when `env_hold`, otherwise Rising
  or Falling from `env_inc`. In AY mode the volume uses `env_vol(4:1)`, so 16 levels, but
  there are still 32 steps; the view shows the step, not the level.

### 3.4 TurboSound select and pan, mono, reset

- A 0xFFFD write is a select/pan command when `turbosound_en_i = '1'`, `d(7) = '1'` and
  `d(4:2) = "111"`: chip from `d(1:0)` (`10` = AY#1, `01` = AY#2, others AY#0) and pan from
  `d(6:5)` (`turbosound.vhd:129-136`). It is a register select only when `d(7:5) = "000"`
  (`:141`), and the chip latches all 5 low bits (`ym2149.vhd:173`).
- Only the selected chip reaches the mix unless TurboSound is on (`turbosound.vhd:197,252,307`).
- `audio_ay_reset` is held while NR 0x06 bits 1:0 = 11 (`zxnext.vhd:6989`); the mode is
  `aymode_i <= nr_06_psg_mode(0)` (`:6999`), so 10 ("ZXN-8950" in `nextreg.txt`) plays as YM.
- NR fields shown: NR 0x06 bit 6 (beep to speaker only), bits 1:0 (`zxnext.vhd:5724,5731`);
  NR 0x08 bit 5 stereo, bit 4 internal speaker, bit 3 DAC enable, bit 1 TurboSound
  (`:5739-5740`, read-back `:6494`); NR 0x09 bits 7:5 mono (`:5747`);
  `beep_spkr_excl = nr_06_internal_speaker_beep and nr_08_internal_speaker_en` (`:7114`), which
  zeroes EAR and MIC in the mixer (`audio_mixer.vhd:80-81`).

### 3.5 DAC decode, gates and the dropped-write reasons

- Port families: `port_dac_*_io_en <= internal_port_enable(17..23)`, i.e. NR 0x84 bits 1-7
  (`zxnext.vhd:2745-2751`, `nextreg.txt:816-823`), after the expansion-bus AND
  (`internal_port_enable`, `zxnext.vhd:2708-2709`; jnext `effective_internal_port_enable()`).
  The mono-AD 0xFB family is enabled only while Soundrive 2 is off (`:2749`).
- Channel decode `port_dac_A..D` (`zxnext.vhd:2967-2980`); write gate
  `port_dac_X_wr <= iowr and port_dac_X and dac_hw_en` (`:3091-3094`), with
  `dac_hw_en <= nr_08_dac_en` (`:2776`). Soundrive is held in reset while NR 0x08 bit 3 = 0
  (`reset_i => reset or not nr_08_dac_en`, `:7046`), and in reset it ignores port and NR mirror
  writes and holds 0x80 (`soundrive.vhd:72-101`).
- AY port decode: `port_fffd`/`port_bffd` require `port_ay_io_en = internal_port_enable(16)`,
  NR 0x84 bit 0 (`zxnext.vhd:2744,2963-2964`).
- NR 0x2C-0x2E reads return the Pi I2S sample, not the DAC (`nextreg.txt:413-431`), so the
  guest cannot read the DAC back and only the debugger can show it.

**Dropped-write reasons** (A1.5). Each reason is one VHDL term, and a write gets the first
reason that applies, in this order. Decode comes before the gate because an undecoded write is
not a write to that device at all.

| Reason | VHDL | jnext site today |
|---|---|---|
| `AyPortDisabled`: 0xFFFD/0xBFFD with NR 0x84 bit 0 off | `port_fffd`/`port_bffd` decode, `zxnext.vhd:2963-2964` | `emulator.cpp:5404,5427,5450` early returns |
| `AyHeldInReset`: NR 0x06 = 11 | `audio_ay_reset`, `zxnext.vhd:6989`; `turbosound.vhd:121`, `ym2149.vhd:184` | `TurboSound::reg_addr/reg_write` early returns |
| `AyChipSelectTurboOff`: a select/pan command pattern written with TurboSound off | `turbosound.vhd:129` needs `turbosound_en_i` | falls through `reg_addr()` and is ignored because `d(7:5) != 000` |
| `AySelectIgnored`: any other 0xFFFD value with `d(7:5) != 000` | `turbosound.vhd:141` | `reg_addr()` drops it silently |
| `AyAddressHigh`: a data write while the latched address has bit 4 set | `ym2149.vhd:188` | `AyChip::write_data`, `if (addr_ & 0x10) return;` |
| `DacFamilyDisabled`: a DAC-family low byte whose family is off in NR 0x84 (for 0xF1/0xF9 the write falls through to the paging decode, and the reason says so) | `port_dac_A..D`, `zxnext.vhd:2967-2980` | the DAC handlers' NR 0x84 tests and `decline_write()` |
| `DacDisabled`: decoded but NR 0x08 bit 3 = 0 | `dac_hw_en`, `zxnext.vhd:3091-3094` | `if (!dac_enabled_) return;` in every DAC handler |
| `DacMirrorDisabled`: NR 0x2C-0x2E with NR 0x08 bit 3 = 0 | soundrive reset, `zxnext.vhd:7046`, `soundrive.vhd:72` | `if (dac_enabled_)` at `emulator.cpp:6125-6133` |

A 0xDFFD write also matches the 0xFFFD pattern, and the VHDL gates the AY side off
(`port_fffd_wr <= ... and not port_dffd`, `zxnext.vhd:3088`). It is the paging port's own
address, so it is **not** counted (D6).

---

## 4. Architecture

### 4.1 One tap seam in the core, zero cost when off

```
 guest OUT / DMA / Copper / NR commit
        |  AY, DAC, NR 0x2C-0x2E, 0xFE handlers (emulator.cpp)
        |     drop classification -> AudioDropCounters (always on; one compare per write)
        |     if (audio_tap_active_)  -> AudioTapRecord  ---> ring log (INS-23)
        |                                               ---> AudioWrite latch (EVT)
        |                                               ---> .psg writer (A3.1)
        v
 TurboSound / Dac / Beeper  --tick-->  Mixer::accumulate_impl_()
                                          if (source_tap_) per-source accumulators
                                          emit_sample_impl_() -> scope ring (INS-24) / stems (A3.4)
```

- **Write sites.** Each audio write site does exactly one extra test,
  `if (__builtin_expect(audio_tap_active_, 0))`, on a plain `bool` member of `Emulator`. The
  bool is true while any consumer is live: the ring log, an armed `AudioWrite` subscription, or
  a `.psg` recording. These sites already run a `std::function` port handler per write, so the
  test is noise next to them, and it adds nothing to the per-instruction path.
- **Drop counters.** The DAC and NR mirror drops sit on the existing early-return branches,
  which run only when a write is discarded. The AY drops are decided inside `TurboSound` and
  `AyChip`, so `reg_addr()` and `reg_write()` return a small status enum instead of `void`
  (A1.5), and the handler adds one compare per AY port write (`if (st != Applied)`), never per
  instruction. The decision logic stays where it is, plus one test on the ignored-write branch
  of `reg_addr()`, with no copy in `emulator.cpp` that could drift. The counters are always on: the user gets the answer even if they open the panel after
  the problem happened. They are plain counters held by `Emulator`, outside `describe_state()`.
- **Mixer.** `accumulate_impl_()` gains `if (source_tap_)` with a null pointer by default:
  one well-predicted branch per instruction slice, the same cost class as the existing
  `mute_mask_` test the Task 47 work accepted. When the pointer is set, a fixed struct of
  per-source accumulators is fed. `integrate_span()` calls the same inlined body, so the HALT
  batch feeds the tap without extra code. `--silent` (`audio_off_`) skips the mixer, so there
  is nothing to tap; the GUI says so and the CLI refuses stems (as it refuses `--wav-record`).
- **Visibility (decision 3, the #498 rule).** Every consumer is a per-client request in the
  `Debugger`, with the effective state "any live request". This is the shape of
  `set_call_stack_enabled(ClientId, bool)` (`debugger.h:683`, GH #434). The Qt panel requests
  the log only while the Log or Tracker sub-tab is visible, and the scopes only while Scopes is
  visible. The CLI flags are requests from the loop owner's client. Closing the window or
  switching the sub-tab drops the request, and the tap goes quiet.
- **Proof of zero cost** (A1.7, A2.4): `make bench` A/B on `build/gui-release-non-pgo` with the
  taps off against the epic's base. Compare retired instructions and set the layout floor with
  a control build (verification-discipline: "Check the unit is invariant"). The cost with the
  taps on is measured and recorded, not assumed **(unmeasured today)**.

### 4.2 Invisible to the Z80

Nothing in this epic may change what the guest sees:

- The taps read state and never call a guest-visible accessor with side effects. They read
  `AyChip::read_register()` and `peek()`, never `reg_read()`.
- The ring, the counters, the scope ring, the writers and both mute masks live outside every
  `describe_state()`, so they are neither saved nor rewound, and neither reset nor load
  touches them.
- The mutes gate an **audible copy** of the output only. After A1.2, the serialised
  `pcm_L_/pcm_R_` are always the hardware sums (F1).
- **The invisibility row** (harness from A1.2, used by every task that adds a tap or a mask):
  run the same fixture twice from the same state for N frames, once with the feature on and
  once off, and compare `Debugger::save_state_bytes(AdvanceToBoundary)` and the framebuffer
  hash byte for byte. Each row's fixture writes the AY, a DAC and the beeper, so that the
  feature has something to observe. A positive control (a real guest write between the runs)
  shows that the comparison can see a difference at all.

### 4.3 Timestamps and frames

- A log entry's `frame`, `vc`, `hc` and `cycle` come from the stamper's arithmetic
  (`frame_num_ - 1`, `(cycle - frame_cycle_)` split by `master_cycles_per_line`), so a log row,
  an `io_write` event and a script's `FRAME` agree.
- **Frame grouping** (tracker, player timing and `.psg`) uses that same frame number. The ULA
  interrupt sits within one line of the frame wrap on every timing (`c_int_v` = 0 or 1, or 319
  = `c_max_vc` on Pentagon: `zxula_timing.vhd:163,199,233,265,293`), so an IM1 or IM2 player
  that writes after its interrupt falls in one frame (D8).
- CPU NextREG writes commit after the boundary drain (`events.h`, `NextRegWrite`), so a CPU
  write to NR 0x2C-0x2E is stamped at commit, one instruction after the `NEXTREG`. A Copper
  write is stamped at its own cycle. The source column says which.
- Writes made in rewind replay (`replay_mode_`) are not recorded. A rewind landing drops the
  log entries from the landing cycle on, markers included (a state load of this session keeps the
  clock, so its marker is crossed too, except one AT the landing cycle, which is the landing state
  and stays; a state loaded from another session has an unordered clock, a stated limit), and appends a
  `Rewind` marker; a reset and a state load append a marker too, and
  only the same marker twice at one cycle is one row (D9, amended by GH #531: an entry is stamped at
  the start of its instruction slot and a landing re-runs the slot that starts at its cycle, so an
  entry AT the landing cycle goes too; a soft reset and a rewind keep the clock, so the walk crosses
  their markers; a `Rewind` marker is always recorded, because a `--audio-log` stream may have
  written the removed rows and the rows after them are earlier in time).

### 4.4 The `debugger.h` bundle (decision 1)

Exact declarations. Types go in `inspect.h` (INS-22 to INS-25) and `events.h` (EVT); the mute
bit layout goes in `audio/audio_mute.h`, which `inspect.h` already includes, so the bit
assignment exists once (the INS-10 reuse rule, `inspect.h:432-437`). Each verb lands in the task
named in its comment, never as a stub. Every published header stays within the include budget
`test/lint-debug-headers.sh` enforces (preflight row 5: no `core/emulator.h`, no `src/platform/`,
no Qt or SDL, none of the internal headers it lists). That lint checks a **fixed list** of headers
(`HEADERS=(...)`, `lint-debug-headers.sh:79-84`), so a new published header is covered only once
it is added to that list.

**`src/audio/audio_mute.h`** (added beside `AudioMute`):

```cpp
/// Per-channel host mute (GH #224, decision 4): a jnext debugger facility like AudioMute,
/// with the same rules (output stage only, not machine state, untouched by reset and loads).
/// AY chip c channel k (A=0, B=1, C=2) is bit 3*c + k; DAC channel A..D is bit 9..12.
namespace AudioChannelMute {
enum : uint16_t {
    NONE    = 0x0000,
    AY_ALL  = 0x01FF,
    DAC_A   = 0x0200, DAC_B = 0x0400, DAC_C = 0x0800, DAC_D = 0x1000,
    DAC_ALL = 0x1E00,
    ALL     = 0x1FFF,
};
constexpr uint16_t ay_bit(int chip, int channel) { return uint16_t(1u << (3 * chip + channel)); }
}  // namespace AudioChannelMute
```

**`src/debug/events.h`**:

```cpp
enum class EventKind : uint8_t { /* ...existing, unchanged values..., */ Dma, AudioWrite, Count };

/// What an AudioWrite reached (or would have reached, when Dropped).
enum class AudioTarget : uint8_t { AyRegister = 0, AyChipSelect, Dac, DacMirror, Beeper, Count };
using AudioTargetMask = uint8_t;                         // bit = AudioTarget
constexpr AudioTargetMask audio_target_bit(AudioTarget t) { return AudioTargetMask(1u << uint8_t(t)); }

/// The reasons of DEBUGGER-MUSIC-SOUND-PLAN.md §3.5, in precedence order.
enum class AudioDropReason : uint8_t {
    None = 0, AyPortDisabled, AyHeldInReset, AyChipSelectTurboOff, AySelectIgnored,
    AyAddressHigh, DacFamilyDisabled, DacDisabled, DacMirrorDisabled, Count };
constexpr size_t AUDIO_DROP_REASON_COUNT = size_t(AudioDropReason::Count);

namespace AudioFlag { enum : uint8_t { EnvelopeRestart = 0x01, Dropped = 0x02 }; }

struct Event {            // added fields, meaningful for kind == AudioWrite only
    // existing: port, value, prev, reg, source, pc, frame, vc, hc, cycle
    AudioTarget     audio_target = AudioTarget::AyRegister;
    uint8_t         chip = 0;          // AY chip 0-2 | DAC channel 0-3 | NR 0x2C-0x2E
    uint8_t         audio_flags = 0;   // AudioFlag bits
    AudioDropReason drop_reason = AudioDropReason::None;
};

struct EventFilter {      // added fields, read only for kind == AudioWrite
    AudioTargetMask audio_targets = 0xFF;   // any
    uint8_t         chip = 0xFF;            // any
    // `regs` (existing RegSet) filters AY registers 0-15; empty = any
    uint8_t         value_mask = 0x00;      // (value & value_mask) == value_match
    uint8_t         value_match = 0x00;
    uint8_t         audio_flags_all = 0;    // every listed AudioFlag bit must be set
};
```

**`AudioWrite` latch packing** (`src/debug/event_table.h`, internal, not frozen). It follows the
existing convention: `kind` says which fields mean anything, `sub_kind` carries the kind's own
sub-enum as a raw byte (as `CopperEventKind` and `DmaEventKind` do), and `misc`, `misc2` and
`misc3` are per-kind bytes (`Reset`, `IntAck` and `Dma` already reuse them). No field is added
and each value has a whole byte:

| `LatchEntry` field | `AudioWrite` meaning | becomes `Event::` |
|---|---|---|
| `sub_kind` | `AudioTarget`, raw byte | `audio_target` |
| `addr` | the 16-bit port; 0x2C-0x2E for a NR mirror; 0 for the beeper | `port` |
| `reg` | AY: the 5-bit latched address; NR mirror: the NextREG number | `reg` |
| `value`, `prev` | the byte written, and the register or channel value before it | `value`, `prev` |
| `source` | `Cpu`, `Dma` or `Copper` (a debugger write is never latched) | `source` |
| `misc` | chip: AY 0-2, DAC channel 0-3, 0 otherwise | `chip` |
| `misc2` | `AudioFlag` bits | `audio_flags` |
| `misc3` | `AudioDropReason`, raw byte | `drop_reason` |

The header fields (`cycle`, `frame`, `pc`, `vc`, `hc`) come from the common stamper like every
other kind. A DAC write that fans out to two channels (0xB3, 0xDF, a mono-AD 0xFB, NR 0x2D)
latches one entry per channel.

**`src/debug/inspect.h`**:

```cpp
// INS-22 -- decoded audio state (GH #224)
constexpr size_t AY_CHANNELS_PER_CHIP = 3;
constexpr size_t DAC_CHANNEL_COUNT = 4;
constexpr size_t CTC_CHANNEL_COUNT = 4;

enum class EnvelopeMotion : uint8_t { Falling = 0, Rising, Holding };

struct AyChannelState {
    uint16_t tone_period = 0;     // 12 bits as written (R0/R1, R2/R3, R4/R5)
    bool     tone_enabled = false;  // R7 bit k == 0 (active low)
    bool     noise_enabled = false; // R7 bit k+3 == 0
    uint8_t  volume = 0;          // R8+k bits 3:0
    bool     uses_envelope = false; // R8+k bit 4
    uint8_t  amplitude = 0;       // 0-255: the AY/YM table level the channel plays at now
                                  // (fixed volume, or the envelope's current step)
    bool     muted = false;       // host: AUDIO_CHANNEL_MUTE bit or the chip's AUDIO_MUTE bit
};
struct AyEnvelopeState {
    uint16_t       period = 0;    // R11 | R12 << 8
    uint8_t        shape = 0;     // R13 bits 3:0
    uint8_t        step = 0;      // env_vol, 0-31
    EnvelopeMotion motion = EnvelopeMotion::Holding;
};
struct AyChipState {
    std::array<uint8_t, AY_REGISTER_COUNT> regs{};
    uint8_t address = 0;          // 5-bit latched address; >= 16 ignores data writes
    uint8_t pan = 3;              // bit 1 = left, bit 0 = right (turbosound.vhd:129-136)
    bool    mono = false;         // NR 0x09 bit 5 + chip
    bool    reaches_mix = false;  // selected, or TurboSound on
    std::array<AyChannelState, AY_CHANNELS_PER_CHIP> channels{};
    uint8_t noise_period = 0;     // R6 bits 4:0
    AyEnvelopeState envelope{};
};
struct DacState {
    std::array<uint8_t, DAC_CHANNEL_COUNT> channels{};  // A-D; 0x80 = silence
    bool    enabled = false;      // NR 0x08 bit 3
    uint8_t port_families = 0;    // effective NR 0x84, bits 7:1 as in the register
};
struct BeeperState {
    bool ear = false, mic = false, tape_ear = false;
    bool speaker_enabled = false;   // NR 0x08 bit 4
    bool beep_speaker_only = false; // NR 0x06 bit 6
    bool excluded = false;          // beep_spkr_excl: EAR/MIC removed from the mix
};
struct PiAudioState { bool left_enabled = false, right_enabled = false, to_ear = false; };
struct HostAudioState {
    float    output_gain_db = 0, beeper_gain_db = 0, dac_gain_db = 0;
    std::array<float, AY_CHIP_COUNT> ay_gain_db{};
    uint8_t  mute_mask = 0;          // AudioMute
    uint16_t channel_mute_mask = 0;  // AudioChannelMute
    bool     synthesis_on = true;    // false under --silent / audio disabled
};
struct AudioDropCounter {
    uint64_t    count = 0;
    uint16_t    last_port = 0;       // the 16-bit port, or 0x002C-0x002E for a NR mirror
    uint8_t     last_value = 0;
    uint16_t    last_pc = 0;
    uint32_t    last_frame = 0;
    EventSource last_source = EventSource::Cpu;
};
struct AudioState {
    uint32_t psg_clock_hz = 0;       // MASTER_CLOCK_HZ / 16
    std::array<AyChipState, AY_CHIP_COUNT> chips{};
    uint8_t    active_chip = 0;      // the chip 0xFFFD/0xBFFD reach
    bool       turbosound = false;
    uint8_t    psg_mode = 0;         // NR 0x06 bits 1:0 (00 YM, 01 AY, 10 plays as YM, 11 reset)
    bool       held_in_reset = false;
    bool       ay_ports_enabled = true;  // effective NR 0x84 bit 0
    StereoMode stereo = StereoMode::Abc;
    DacState dac{}; BeeperState beeper{}; PiAudioState pi{}; HostAudioState host{};
    std::array<AudioDropCounter, AUDIO_DROP_REASON_COUNT> drops{};  // [None] unused
};

// INS-23 -- the audio write log
struct AudioLogEntry {
    uint64_t        seq = 0;          // monotonic, never reused in a run
    uint64_t        cycle = 0;        // master cycle
    uint32_t        frame = 0;
    int16_t         vc = 0, hc = 0;   // the stamper's raw counters
    uint32_t        tstate_in_frame = 0;
    uint16_t        pc = 0;
    EventSource     source = EventSource::Cpu;
    AudioTarget     target = AudioTarget::AyRegister;
    uint8_t         chip = 0, reg = 0, value = 0, prev = 0, flags = 0;
    AudioDropReason drop_reason = AudioDropReason::None;
    uint16_t        port = 0;
    AudioMarker     marker = AudioMarker::None;   // Reset / StateLoad / Rewind (GH #531); other fields zero
};
constexpr size_t AUDIO_LOG_DEFAULT_ENTRIES = 65536;
constexpr size_t AUDIO_LOG_MAX_ENTRIES = size_t(1) << 22;

// INS-24 -- source taps
enum class AudioSource : uint8_t {
    Ay0A, Ay0B, Ay0C, Ay1A, Ay1B, Ay1C, Ay2A, Ay2B, Ay2C,
    DacA, DacB, DacC, DacD, Beeper, MixLeft, MixRight, Count };
constexpr size_t AUDIO_SOURCE_COUNT = size_t(AudioSource::Count);
struct AudioScopeBlock {
    uint32_t sample_rate = 0;                 // 44100
    uint64_t first_sample = 0;                // index of samples[*][0] since the tap started
    std::array<std::vector<int16_t>, AUDIO_SOURCE_COUNT> samples;  // equal lengths, oldest first
};

// INS-25 -- pacing engines
struct CtcChannelState {
    bool     counter_mode = false;   // control bit 6
    uint16_t prescaler = 16;         // 16 or 256 (bit 5)
    uint16_t time_constant = 256;    // 1-256 (0 written = 256)
    uint8_t  count = 0;              // current down-counter
    bool     running = false;
    bool     int_enabled = false;    // control bit 7, or NR 0xC5
    uint32_t zc_rate_millihz = 0;    // ZC/TO rate in milli-hertz; 0 = unknown (counter mode
                                     // fed by a stopped channel)
};
struct DmaState {
    bool     active = false;
    uint8_t  mode = 0;               // R4 bits 6:5: 00 byte, 01 continuous, 10 burst
    uint8_t  prescaler = 0;          // R2 port B; 0 = no pacing
    uint32_t rate_millihz = 0;       // 875 kHz / prescaler in milli-hertz; 0 when prescaler 0
    uint16_t src = 0, dst = 0, length = 0, counter = 0;
    bool     src_io = false, dst_io = false;
};
```

**`src/debug/debugger.h`** (audio section, after INS-10):

```cpp
/// INS-22 -- the decoded audio state, one coherent read (A1.3). DIRECT VALUE.
AudioState audio_state() const;
/// INS-22 -- the host channel mute (AudioChannelMute bits). Never in a snapshot or an RZX (A1.12).
uint16_t audio_channel_mute_mask() const;                                  // DIRECT VALUE
Result   set_audio_channel_mute_mask(ClientId by, uint16_t mask);          // MUTATE-logged
/// INS-22 -- zero every dropped-write counter (A1.5).
Result   clear_audio_drops(ClientId by);

/// INS-23 -- the write log (A1.7). `by`'s own request; on while any live client asks.
Result set_audio_log_enabled(ClientId by, bool enabled);  // RefusedUnavailable if not attached
bool   audio_log_enabled() const;                         // DIRECT VALUE, effective state
Result audio_log_clear(ClientId by);
Result audio_log_resize(size_t entries);                  // 1..AUDIO_LOG_MAX_ENTRIES
/// Entries with seq > since_seq, oldest first; RefusedUnavailable while off.
Expected<std::vector<AudioLogEntry>> audio_log_entries(uint64_t since_seq) const;
/// CSV, header in §5 A1.7; RefusedUnavailable on an I/O error.
Result audio_log_export(const std::string& path) const;

/// INS-24 -- the source tap (A2.4) and the two writers (A3.1, A3.4).
Result set_audio_scope_enabled(ClientId by, bool enabled);
bool   audio_scope_enabled() const;                       // DIRECT VALUE
Expected<AudioScopeBlock> audio_scope_samples(size_t count) const;  // RefusedUnavailable while off
Result psg_record_start(ClientId by, const std::string& path);  // §7, A3.3 naming rule
Result psg_record_stop(ClientId by);
bool   psg_recording() const;                             // DIRECT VALUE
Result stems_record_start(ClientId by, const std::string& dir);  // RefusedUnavailable under --silent
Result stems_record_stop(ClientId by);
bool   stems_recording() const;                           // DIRECT VALUE

/// INS-25 -- read-only pacing queries (A2.6).
std::array<CtcChannelState, CTC_CHANNEL_COUNT> ctc_state() const;  // DIRECT VALUE
DmaState dma_state() const;                                        // DIRECT VALUE
```

The DIRECT-VALUE banner grows by 8 queries, from 52 to 60, all in the video/audio bucket
(16 -> 24): `audio_state`, `audio_channel_mute_mask`, `audio_log_enabled`,
`audio_scope_enabled`, `psg_recording`, `stems_recording`, `ctc_state`, `dma_state`.
**The task that lands each query edits the banner and the bucket count**, and A1.13, A2.8 and
A3.5 re-check the total against the list, as the banner itself asks.
`b0-cap-traceability.md` gets INS-22 to INS-25 and the `AudioWrite` row of CAP-EVT; `debug_types_check.cpp` gets static asserts for the
`AudioChannelMute` layout and `AUDIO_DROP_REASON_COUNT`, and its `EventKind` asserts move from
14 to 15 kinds (A1.9). The pure helpers of A1.1 and A2.1 are **new published headers**
(`src/debug/audio_math.h`, `src/debug/audio_frames.h`). They include only standard headers plus,
for `audio_frames.h`, `debug/inspect.h` (for `AudioLogEntry`), and the task that creates each one
adds it to the lint's `HEADERS` list, so the lint actually checks it.

---

## 5. Phase A1: see and stop

Goal: the user sees what the chips play, why something is silent, and which write did it, and
can stop on that write and hear one channel alone. When sub-issues 1-5 are done, the epic
branch merges into `main` on its own (owner, 2026-11-18, §10 Q1), so Phase 1 must be complete
by itself: its docs (A1.13), its CLI flag (`--audio-log`), its FEATURES.md entry, and no
placeholder that points at unfinished work except the Tracker and Scopes sub-tabs, which say
"not available yet" in so many words.

### A1.1 Core suite and the note math

- **Design.** A new published header `src/debug/audio_math.h` (standard headers only), with pure
  functions: `double psg_tone_hz(uint16_t tp, uint32_t clock_hz)`,
  `double psg_envelope_hz(uint16_t ep, uint8_t shape, uint32_t clock_hz)` (0 for a non-repeating
  shape), `PsgNote psg_note(double hz)` returning `{bool valid; uint8_t midi; char name[4];
  int8_t octave; double cents;}`, and `const char* psg_envelope_glyph(uint8_t shape)` (the
  eight `ym2149.vhd:371-391` glyphs as ASCII: `\___`, `/___`, `\\\\`, `\/\/`, `\'''`, `////`,
  `/'''`, `/\/\`; no em dashes, no non-ASCII).
- **Files.** `src/debug/audio_math.{h,cpp}` and `src/debug/CMakeLists.txt` (in `jnext_debug`),
  `test/lint-debug-headers.sh` (`debug/audio_math.h` added to `HEADERS`),
  `test/debug/audio_debug_test.cpp`,
  `test/CMakeLists.txt`, `test/unit-tests.conf` (a core suite: `# gate: none`, `# os: all`, so
  it runs in `unit-test`, `unit-test-sdl`, `unit-test-win` and `unit-test-wasm`),
  `test/refresh-traceability-matrix.pl` (`%PLAN_DOC`, `:387`, `'audio_debug_test' => 'AUDIO'`),
  `doc/testing/AUDIO-TEST-PLAN-DESIGN.md` (a new section "Debugger audio introspection (GH #224)"
  holding every planned row of the epic). Read `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` first.
- **Tests.** `ANO-01..` rows: the §3.2 table (TP 0 = TP 1, 252, 253, 239, 428, 4095,
  out-of-range); a tie case (a frequency exactly half-way between two notes names the upper
  one, at -50 cents); envelope sawtooth and triangle frequency at EP 0, 1 and 100 (§3.3); a
  non-repeating shape returns 0; the eight glyphs. Each expected value comes from the formula with its VHDL citation, computed outside
  the code under test (the table above).
- **Acceptance.** The suite is declared with its exact count and green in all four unit
  gates; `make traceability-check` lists the planned epic rows as PLANNED; the header lint
  (preflight row 5) compiles `audio_math.h` and passes.
- **Depends on.** Nothing.

### A1.2 Mutes out of the machine state (F1) and the invisibility harness

- **Design.** `TurboSound` keeps two outputs. A new **hardware** aggregate `hw_pcm_L_/hw_pcm_R_`
  is computed with no mute and is what `describe_state()` serialises, under the same keys
  `pcm_l`/`pcm_r`, so the snapshot format does not change. The existing members and accessors
  (`pcm_L_/pcm_R_`, `pcm_left()`, `pcm_right()`, `chip_pcm()`) keep their meaning, **the audible
  output**: they apply the chip mute and, from A1.12, the channel mute, and they leave the
  declaration. The Mixer and every existing test therefore read exactly what they read today.
  While both masks are zero the two aggregates are equal, and the extra work is one branch in
  `compute_stereo_mix()`, which runs only when an output changed. After a state load the audible
  aggregate starts equal to the loaded hardware one, and the first PSG tick rebuilds it
  (`mix_dirty_` is already set by `load_state()`). Add a test helper
  `expect_invisible(fixture, frames, enable, disable)` in `audio_debug_test` implementing §4.2.
- **Files.** `src/audio/turbosound.{h,cpp}`, `src/audio/audio_mute.h` (comment),
  `test/debug/audio_debug_test.cpp`.
- **Tests.** `AIZ-01` (AY chip mute: state bytes equal with AY1 muted and unmuted over 50
  frames of a 3-chip fixture; **fails before the fix**), `AIZ-02` (DAC and beeper mutes),
  `AIZ-03` (the harness's positive control: one guest `OUT` between the runs makes it differ),
  `AIZ-04` (mute on, load a state saved unmuted: after one PSG tick `pcm_left()` excludes the
  muted chip, while the reloaded state bytes equal the saved ones). The existing `DAP-08..15`
  (which read `pcm_left()`) and `AYC-16` (`ay_cache_test`) pass unchanged: what is heard does
  not change, only what is saved.
- **Mutations.** Serialise the audible aggregate: AIZ-01 dies. Skip the rebuild on the first
  tick after a load: AIZ-04 dies.
- **Acceptance.** Muting never changes `save_state_bytes`; `audio_test`, `ay_cache_test` and
  `debugger_audio_panel_test` are unchanged and green.
- **Depends on.** A1.1.

### A1.3 `audio_state()` (INS-22)

- **Design.** Add const debug views to the audio classes (not published):
  `AyChip::DebugView{regs, addr, env_vol, env_inc, env_hold, amplitude[3]}`,
  `TurboSound::selected_index()`, `TurboSound::pan(i)`, and `Dac::channel(i)`. `Debugger::audio_state()`
  (`debugger_inspect.cpp`) fills the §4.4 struct from these, the NR shadows
  (`nextreg_peek`-equivalent reads with no side effects), `effective_internal_port_enable(0x84)`,
  the I2s enables, the Mixer gains and the two mute masks. `psg_clock_hz` comes from
  `MASTER_CLOCK_HZ / 16`. The envelope motion is Holding when `env_hold`, else Rising or Falling.
- **Files.** `src/audio/ay_chip.{h,cpp}`, `turbosound.h`, `dac.h`, `src/debug/inspect.h`,
  `debugger.h`, `debugger_inspect.cpp`, `debug_types_check.cpp`, `b0-cap-traceability.md`.
- **Tests.** `ADS-01..` rows driving **real port and NR writes** on an `Emulator` and reading
  `audio_state()`: R7 active-low decode for all eight tone/noise combinations of one channel;
  volume vs `uses_envelope`; a 5-bit address of 0x1D reported as 0x1D (F2); select and pan from
  0xFFFD commands 0xFF, 0xFE, 0xFD with pans 00-11 (`turbosound.vhd:129-136`); a select command
  with TurboSound off leaves `active_chip` unchanged; `reaches_mix`; mono bits; NR 0x06 = 11
  gives `held_in_reset` and `psg_mode` 3; NR 0x06 = 10 gives `psg_mode` 2 with
  `mode == Ym`; **the envelope step after N PSG ticks** for shapes 8, 10, 13 and 14 at EP 2,
  with the expected step derived by simulating `ym2149.vhd:258-277` and `:334-420`, divider
  phase included (the row's comment shows the derivation; the reviewer re-derives it);
  `amplitude` from the AY and YM tables; DAC A-D after writes; port families after NR 0x84 and
  NR 0x88 with the expansion bus on; beeper EAR/MIC/tape EAR; `excluded`; the host gains. Plus
  `AIZ-05`: 1000 `audio_state()` calls mid-frame leave the state bytes unchanged.
- **Acceptance.** Every field of `AudioState` is asserted by at least one row: against the VHDL
  or the register map, or against the host settings for the host fields (gains, mutes,
  `synthesis_on`); no field is computed from the panel.
- **Depends on.** A1.1.

### A1.4 Audio tab with sub-tabs, and the Chips sub-tab

- **Design.** `AudioPanel` becomes a container: the existing five source checkboxes in a strip
  at the top (visible from every sub-tab), then a `QTabWidget` with Chips, DAC & Beeper, Log,
  Tracker and Scopes (decision 6; Log, Tracker and Scopes appear as placeholders until their
  tasks land). The Chips sub-tab shows a header per chip (active, 5-bit address, L/R pan, mono,
  in mix, plus the global TurboSound, AY/YM/reset and ABC/ACB) and one row per channel (period,
  note and cents, T and N lit or dark, volume or "Env", a level bar from `amplitude`). Per
  chip it shows noise period, envelope glyph, period, the envelope note for shapes 8, 10, 12
  and 14, and step plus motion. A "Hex" toggle restores today's table. **Refresh** (decision
  3): while the Audio tab is current and the window visible, `DebuggerManager`'s tick calls
  `AudioPanel::refresh_live()` every frame, through one `wants_live_refresh()` predicate;
  every other panel keeps `REFRESH_INTERVAL`.
- **Files.** `src/debugger/audio_panel.{h,cpp}`, `debugger_manager.{h,cpp}`,
  `test/debugger/audio_panel_test.cpp`.
- **Tests.** `DAP-18..` in `?debugger_audio_panel_test` (it keeps its declared gate): the five
  sub-tabs exist in order; a TP of 252 on AY0 A shows "A4" and "-23.7" (the panel rounds to
  one decimal); T and N follow R7; "Env" with R8 bit 4; the glyph for each shape; the envelope
  note shown only for 8, 10, 12, 14; the hex toggle shows the same 48 bytes as
  `ay_registers()`; `wants_live_refresh()` is true only while the Audio tab is current and the
  window visible (switching tab or hiding the window clears it). DAP-01..17 keep passing; those
  that read labels moved into the Chips header are re-pointed to them, not rewritten.
- **Acceptance.** The panel reads only `audio_state()` and `ay_registers()`; the Qt panels
  still hold `Debugger*` and never `Emulator*` (the #276 backend rule; `git grep 'Emulator[*&]'
  src/debugger/` finds nothing outside comments); every DAP row green.
- **Depends on.** A1.3.

### A1.5 Dropped-write classification and counters

- **Design.** One private `Emulator::note_audio_drop(AudioDropReason, uint16_t port, uint8_t
  value)` called from each DAC and NR mirror early-return branch of §3.5. It records
  `debug_slot_pc_`, the frame, and the source (`slot_ran_dma()`, NextReg's write source for a
  mirror, `EventSource::Debugger` when `guest_access()` is false). The AY conditions are decided
  where they already are: `TurboSound::reg_addr()` returns `AyAddrStatus {Applied, HeldInReset,
  ChipSelectTurboOff, SelectIgnored}` and `TurboSound::reg_write()` returns `AyWriteStatus
  {Applied, HeldInReset, AddressHigh}` (from `AyChip::write_data()`'s `addr_ & 0x10` test), and
  the Emulator handler maps a non-`Applied` status to a reason. That is one compare per AY port
  write, and no copy of TurboSound's logic exists in `emulator.cpp` to drift from it (§4.1).
  The one new test is inside `TurboSound::reg_addr()`, on its already-ignored branch: it tells
  `ChipSelectTurboOff` from `SelectIgnored` by the command pattern. A
  dropped write also goes to the tap (A1.7) with `AudioFlag::Dropped` when the tap is active.
  `clear_audio_drops()` zeroes the counters, which are excluded from `describe_state()` and
  untouched by reset.
- **Files.** `src/audio/turbosound.{h,cpp}`, `ay_chip.{h,cpp}` (the status returns),
  `src/core/emulator.{h,cpp}`, `src/debug/debugger_inspect.cpp`, `debugger.h`,
  `test/audio/ay_cache_test.cpp` only if its reference model must mirror the new return type
  (its rows and expected values stay as they are).
- **Tests.** `DRP-01..` one row per reason, each driven by real writes and each straddling the
  condition (the same write counted with the enable off, applied and not counted with it on):
  NR 0x84 bit 0 off; NR 0x06 = 11; 0xFF to 0xFFFD with TurboSound off
  (`AyChipSelectTurboOff`); 0x40 to 0xFFFD (`AySelectIgnored`); select 0x15, then a data write
  (`AyAddressHigh`); each of the seven DAC families with its bit off (0x1F, 0xF3, 0x3F, 0x0F
  with b1 and b4 both off, 0xFB with b2 and b5 off, 0xB3, 0xDF), with the expected family taken
  from the decode table at `zxnext.vhd:2967-2980`; NR 0x08 bit 3 off (`DacDisabled`);
  NR 0x2D with the DAC off (`DacMirrorDisabled`); a DMA write counted with source Dma; a 0xDFFD
  write not counted (D6); 0xF1 with Soundrive 2 off counted as `DacFamilyDisabled` while the
  0x7FFD-family handler still runs; **precedence**: 0x1F with NR 0x84 bit 1 off **and** NR 0x08
  bit 3 off counts as `DacFamilyDisabled`, not `DacDisabled` (D7); a `.jds`-style
  `port_out()` to 0xFFFD with the port disabled counts with source `Debugger`; `last_pc` names
  the `OUT`. `AIZ-06`: counters survive a state load and a reset unchanged.
- **Mutations.** Swap the decode/gate precedence: the precedence row dies. Change TurboSound's
  select-command pattern (`d(4:2) = "111"`): the `AyChipSelectTurboOff` row and the existing
  `turbosound` rows of `audio_test` die. Return `Applied` for the address-high case: the
  `AyAddressHigh` row dies.
  Remove one site's call: its row dies.
- **Acceptance.** Every early return in the AY, DAC and NR mirror handlers either calls
  `note_audio_drop()` or carries a comment saying why it is not a drop (the 0xDFFD case).
- **Depends on.** A1.3.

### A1.6 DAC & Beeper sub-tab

- **Design.** Show DAC A-D as values and bars (0x80 centred), the DAC enable, the seven port
  families with their port lists, the beeper EAR/MIC/tape EAR, the speaker and beep-only bits
  and the resulting "EAR/MIC excluded from the mix", Pi I2S L/R and EAR routing, the host gains
  and both mute masks in force, and the **dropped-write table** (reason, count, last port,
  value, PC, frame, source), with a Clear button and double-click on a row to show its PC in
  the Disassembly. The Chips header shows the AY drop total and links to it.
- **Files.** `src/debugger/audio_panel.cpp` (or a new `audio_dac_view.{h,cpp}` if the file
  passes about 800 lines), `test/debugger/audio_panel_test.cpp`.
- **Tests.** `DAP-` rows: each family's lamp follows NR 0x84; a dropped write appears in the
  table with its reason text and PC; Clear empties it through `clear_audio_drops()`; the
  excluded line follows NR 0x06 bit 6 AND NR 0x08 bit 4.
- **Acceptance.** Every field of `DacState`, `BeeperState`, `PiAudioState`, `HostAudioState`
  and `drops` is visible.
- **Depends on.** A1.4, A1.5.

### A1.7 Write tap and ring log (INS-23), `--audio-log FILE`

- **Design.** An `AudioTapRecord` (internal, `src/audio/audio_tap.h`, standard headers only) built at each
  site while `audio_tap_active_`:
  - AY data writes: chip, the 5-bit address, value, previous register value (`read_register`),
    and `EnvelopeRestart` when the address is 13;
  - 0xFFFD writes **only** when they are a chip-select/pan command or dropped (a plain register
    select is folded into the next data write's decoded register; D10);
  - DAC channel writes: one record per channel written, so 0xB3, 0xDF and 0xFB fan out;
  - NR 0x2C-0x2E commits;
  - beeper: only when EAR or MIC changes (border-only 0xFE writes are not audio);
  - writes the debugger itself makes (`port_out()`, a `.jds` `out`, `nextreg_write()`; the sites
    see `guest_access()` false) are recorded with source `Debugger`, because they change the
    sound. They fire no `AudioWrite` event, by the §4.2a rule that a debugger write fires no
    event at all (`events.h`, `EventSource`), and they do reach the `.psg` stream, since the
    chip holds them.

  `Debugger::Impl` owns a ring (`AUDIO_LOG_DEFAULT_ENTRIES`, allocated on the first request,
  freed when the effective state goes off) and the per-client requests. The Emulator holds one
  `AudioTapSink*` the Debugger installs; `audio_tap_active_` is recomputed whenever a consumer
  changes. CSV export header:
  `frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port`, values in
  decimal except `pc`, `port`, `value` and `prev` in `0x..` hex; marker rows read `marker`.
  Written `"wb"` (LF on every host, as `dac_trace_recorder.cpp:22`).
  `--audio-log FILE`: the loop owner attaches as a client, requests the log, streams entries
  to the file at every pump, and closes it at exit. It works in every frontend, headless and
  `--silent` included (no mixer needed). `--dac-trace` stays.
- **Files.** `src/audio/audio_tap.h`, `src/core/emulator.{h,cpp}`, `src/debug/debugger_audio.cpp`
  (new), `debugger_impl.h`, `debugger.h`, `inspect.h`, `src/main.cpp` and the loop owners
  (`qt_app.cpp`, `sdl_app.cpp`, `headless_app.cpp`), `src/core/cli_options.h`,
  `doc/man/jnext.1.md` (+ `make docs-man`).
- **Tests.** `AWL-01..`: each target recorded with chip, register, value, prev and source (CPU
  `OUT`, DMA to a DAC port, Copper MOVE to NR 0x2D, CPU `NEXTREG`); timestamps equal those of
  an `io_write` event subscribed to the same write; R13 flagged with an unchanged value too;
  plain selects not logged; beeper logged on change only; ring overflow keeps the newest and
  `seq` stays monotonic; nothing recorded with no request, and recording again after a
  re-request; two clients, one drops its request, recording continues; replay mode records
  nothing; a rewind landing truncates from its cycle on and adds a Rewind marker; CSV bytes for a three-entry log against
  a hand-written expected file. `AIZ-07`: log on vs off. Regression row `audio-log-func`
  (headless, `--audio-log` over an AY+DAC fixture, CSV header and a known first data row,
  exit 0). `make cli-check` green.
- **Bench.** `make bench` A/B (§4.1) with the log off; the result goes in the PR.
- **Acceptance.** Taps off costs within the control-build floor in retired instructions; the
  log matches the event system's timestamps; `audio-log-func`, `make cli-check` and
  `make docs-check` green; `--audio-log` is in the man page.
- **Depends on.** A1.3 (the audio classes' debug views), A1.5 (the same write sites).

### A1.8 Log sub-tab

- **Design.** A virtualised table over `audio_log_entries(since_seq)`, appending at the 4 Hz
  refresh while running and fully on pause. Filters: chip, register, DAC channel, target,
  source, "envelope restarts only", "dropped only". Double-click a row to show its PC in the
  Disassembly (the same call the Breakpoints panel uses), and "Export CSV..." calls
  `audio_log_export()`. The sub-tab requests the log in `showEvent` while the window is
  visible and drops the request in `hideEvent`.
- **Files.** `src/debugger/audio_log_view.{h,cpp}` (new), `audio_panel.cpp`,
  `test/debugger/audio_panel_test.cpp`.
- **Tests.** `DAP-` rows: showing the sub-tab makes `audio_log_enabled()` true and hiding it
  makes it false (the #498 rule); the filter rows; the restart flag cell; double-click
  navigates; Export writes the file.
- **Acceptance.** The log is collected exactly while the Log sub-tab is visible in a visible
  window (or another client asks); every filter of the design narrows the table as its row
  says; DAP rows green.
- **Depends on.** A1.4, A1.7.

### A1.9 `AudioWrite` event kind

- **Design.** At the tap, for a guest write (`guest_access()` true) and when an `AudioWrite`
  subscription is armed (`DebugState::audio_events_armed()`, a new flag of the same shape as
  `DebugState::nextreg_events_armed()`, `debug_state.h:587`, which `NextReg::write()` tests at
  `src/port/nextreg.cpp:469-471`), build a `LatchEntry` with kind `AudioWrite` and the packing
  of §4.4 ("`AudioWrite` latch packing"), and let the pre-gated `event_table` filter decide
  whether to latch it. It is delivered at the raising instruction's boundary, or
  one instruction late for a CPU NR mirror (as `NextRegWrite`). The matching filter
  (`EventFilter::matches`) tests target mask, chip, `regs`, value/mask and flags with integer
  compares. `Event` carries the new fields. This covers "stop on AY c register r (optionally
  value/mask)", "on an envelope restart" (`audio_flags_all = EnvelopeRestart`), "on a DAC
  write" (`audio_targets = Dac|DacMirror`) and "on a dropped write"
  (`audio_flags_all = Dropped`).
- **Files.** `src/debug/events.h`, `event_table.{h,cpp}`, `debug_state.{h,cpp}`,
  `debugger_events.cpp`, `src/core/emulator.cpp`, and `src/debug/debug_types_check.cpp`, whose
  compile-time pins move with the new kind: `EVENT_KIND_COUNT == 14` (`:109`) becomes 15, the
  `kind_bit(EventKind::Dma) == (1u << 13)` assert (`:113`) gains a
  `kind_bit(EventKind::AudioWrite) == (1u << 14)` twin, and `Dma + 1 == EVENT_KIND_COUNT`
  (`:116`) becomes `AudioWrite + 1`. The event-kind table of
  `doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md` §4.3, which the first assert's message cites,
  gains the `AudioWrite` row in the same change; so does `b0-cap-traceability.md` (CAP-EVT).
- **Tests.** `AEV-01..`: each filter field straddled (match and non-match); a Stop lands after
  the writing instruction with `Event::pc` the writer; `once`; Log and Continue actions; ring
  overflow behaviour as other kinds; an AY write by DMA carries source Dma; a debugger
  `port_out()` to 0xBFFD fires nothing; each field of the latch packing round-trips into
  `Event` (one row per field, distinct values in all four so a swapped byte shows); `AIZ-08`:
  an armed Log-action subscription vs none. The build itself checks the new kind count
  (`debug_types_check.cpp`).
- **Mutations.** Swap `misc` and `misc3` in the packing: the round-trip row dies. Drop the
  `audio_events_armed()` pre-gate: nothing fails functionally (the filter still rejects), so
  the A1.7 bench A/B is what guards it, as for `nextreg_events_armed()`.
- **Acceptance.** The four stop cases of the design (AY c register r with optional value/mask,
  an envelope restart, a DAC write, a dropped write) each have a row that stops on the right
  instruction and a row that does not stop on a near miss; the build is green with 15 kinds.
- **Depends on.** A1.7.

### A1.10 "Audio write" breakpoints in the GUI

- **Design.** `BreakpointModel::Type` gains `AudioWrite`. The Add dialog shows, for that type:
  target (AY register / AY chip select / DAC / any), chip, register, an optional value and mask,
  "envelope restart only" and "dropped only". The row text reads, for example,
  `AY1 R13 restart` or `DAC dropped`. If #372 has landed by then, its condition and hit-count
  widgets apply here as they do to the other types.
- **Files.** `src/debugger/breakpoint_model.{h,cpp}`, `breakpoint_panel.cpp`,
  `test/debugger/panels_test.cpp`.
- **Tests.** `QBA-01..` in `?debugger_panels_test`: the dialog builds the right `EventFilter`
  for each field; the row text; a hit pauses and the panel highlights it; persistence follows
  whatever `persistent_bp_test` already covers for the other types (extended there if the type
  persists).
- **Acceptance.** Every field of the dialog maps to one `EventFilter` field (§4.4) and a
  breakpoint made in the dialog stops on the same write as the equivalent backend
  subscription; QBA rows green; the Breakpoints page of the user guide shows the new type.
- **Depends on.** A1.9.

### A1.11 DSL: `ay_write`, `CHIP`, `ay[c, r]`

- **Design.** In `src/script/`: the reserved word `ay_write` and the accessor stem `ay`. The
  event is `on ay_write [chip N] [reg N] [mask M value V] do ... end`, compiled to an
  `AudioWrite` subscription with `audio_targets = AyRegister`, with payload `CHIP REG VALUE PREV`
  (new payload name `CHIP`; `REG`, `VALUE`, `PREV` and `PC` exist). `ay[c, r]` reads chip c
  register r through `ay_registers()`. The spelling `ay[c, r]` is the owner's (2026-11-18,
  §10 Q2): it matches `phys[page, offset]` (`ast.h:79`) and the lexer's bracketed-stem table
  (`lexer.cpp:172-175`), so no grammar change is needed.
  `AUDIO_CHANNEL_MUTE` belongs to sub-issue 5 (A1.12).
- **Files.** `src/script/names.{h,cpp}`, the parser, evaluator and engine,
  `src/doc/user-guide/06-debugger/scripting/03-events.md`, `04-reading-the-machine.md`,
  `08-changing-the-machine.md`, `13-quick-reference.md`.
- **Tests.** `PARSE-AYW-01..` (`script_parse_test`): the event's grammar, each qualifier, and
  refusals (chip 3, reg 16); the reserved-word and names lists (the `PARSE-LV-NAMES` row's
  list grows by one name, as its own comment requires). `EVAL-AYR-01..` (`script_eval_test`):
  `ay[1, 13]` after a TurboSound write; out-of-range indices refused. `EVT-AYW-01..`
  (`script_events_test`): a script logs `CHIP REG VALUE PREV` for a real AY write and stops on
  `reg 13`.
- **Acceptance.** The scripting pages and the quick reference document the event, its
  qualifiers and payload and the accessor; `make docs-check` green; script rows green.
- **Depends on.** A1.9.

### A1.12 Per-channel mute and solo

- **Design.** `Emulator::set_audio_channel_mute_mask(uint16_t)` fans out to
  `TurboSound::set_channel_mute_mask(bits 0-8)`: the audible copy of A1.2 zeroes the muted
  channels' `a/b/c` inputs before the per-chip stereo math, so ACB, mono and pan stay right.
  It also goes to `Dac::set_channel_mute(bits 9-12)` (as built): `Dac` keeps an audible copy
  `aud_[4]` of its registers in which a muted channel reads 0x80 (its silence is the midpoint, as
  for `AudioMute::DAC`), updated on each write, on a mute change, on `reset()` and on `load_state()`;
  `pcm_left()/pcm_right()` sum that copy, the registers `ch_` (what `channel()` returns and what is
  serialised) never see the mute, and `Mixer::mix()` is unchanged, so an unmuted run runs the same
  code as before (measured, perf-attach: +0.00% instructions). A `.jns` load rebuilds the copy through
  its `load_state` round trip. TurboSound's muted mix is an out-of-line cold function behind one
  test, and its chip mute became AND gates so that test costs nothing net. The mask is not machine
  state: not reset, not serialised. The
  backend verbs are as in §4.4, with a MUTATE log line. In the DSL, `AUDIO_CHANNEL_MUTE` is a
  live name, readable and assignable (`set AUDIO_CHANNEL_MUTE = 0x1FE`), mirroring `AUDIO_MUTE`
  (`names.cpp` `LIVE[]`, `script_engine.cpp:794-796`). GUI: a Mute and a Solo button per channel row
  in Chips and per DAC channel in DAC & Beeper. Solo is GUI-only (decision 4): it sets the mask
  to everything except that channel and remembers the previous mask to restore on un-solo.
  `AudioChannelMute` is defined in `audio_mute.h`.
- **Files.** `src/audio/turbosound.{h,cpp}`, `dac.{h,cpp}`, `audio_mute.h`, `emulator_jns.cpp` (a comment),
  `src/platform/emulator_boot.h` (the cold boot carries the mask),
  `src/core/emulator.h`, `src/debug/debugger_inspect.cpp`, `debugger.h`, `debug_types_check.cpp`,
  `src/script/names.{h,cpp}`, `script_engine.cpp`, the scripting pages `04-reading-the-machine.md`,
  `08-changing-the-machine.md` and `13-quick-reference.md`, `src/debugger/audio_panel.cpp`.
- **Tests.** `ACM-01..`: for each of the 13 bits, the audible output loses exactly that
  channel's contribution, with each channel driven to a distinct level so an aggregate cannot
  hide a wrong bit (verification-discipline, "asserting on an aggregate"); ACB and mono with
  one channel muted; the DAC midpoint substitution; mask untouched by hard and soft reset;
  `set_audio_channel_mute_mask` advances `Debugger::state_revision()` by one, as `REV-08`
  (`debugger_backend_test`) pins for `set_audio_mute_mask`, with a new literal `REV-` row there.
  `AIZ-09`: each bit on vs off leaves the state bytes equal. `EVAL-ACM-01..`
  (`script_eval_test`): `AUDIO_CHANNEL_MUTE` reads and sets the mask, and the `PARSE-LV-NAMES`
  list grows by that name. `DAP-` rows: Solo on B of AY1 sets the mask to
  `ALL & ~ay_bit(1,1)`, and un-solo restores the earlier mask.
- **Mutations.** Map a bit to the wrong channel: its ACM row dies. Mute the DAC channel to 0
  instead of 0x80: the midpoint row dies. Serialise the mask: AIZ-09 dies. Let either setter
  overwrite the shared byte: an `ACM-` row that sets a source mute and then a channel mute
  (and the reverse order) dies. Return the internal bit from `mute_mask()`: an `ACM-` row
  comparing `mute_mask()` with the `AudioMute` value set dies.
- **Acceptance.** All 13 channels can be muted and soloed from the panel and from `.jds`, the
  serialised state never changes, and an unmuted run executes the same mixer branches as today
  (A1.7's bench method, run again here).
- **Depends on.** A1.2, A1.4.

### A1.13 Phase 1 doc pass

- **Design.** The closing pass over what sub-issues 1-5 changed (each of them already updated
  the pages it touched; this task makes them read as one manual). No issue numbers in the
  manuals (owner rule); versions or dates instead.
- **Files.** `src/doc/user-guide/06-debugger/panels/13-audio.md`, `08-breakpoints.md`;
  `src/doc/developer-guide/03-subsystems/04-audio.md`, `09-1-the-backend-api.md`,
  `09-2-event-delivery-and-mutation.md`, `10-2-the-qt-debugger.md`; the rendered
  `doc/user-guide/` and `doc/developer-guide/`; `doc/man/jnext.1.md` and its generated outputs
  (if `--audio-log`'s entry needs more than A1.7 gave it); `FEATURES.md`; the DIRECT-VALUE
  banner of `src/debug/debugger.h` (comment only).
- **Tests.** `make docs-check` (man page, user guide and developer guide renders match their
  sources), `make cli-check`, and the banner recount: the listed names, the bucket counts and
  the total agree with the declarations (52 + the queries landed so far).
- **Acceptance.** A reader of the Audio page finds: the five sub-tabs and what each shows;
  notes and cents, and why 128K tables play about 23 cents flat on the Next; the envelope glyph,
  note and position, and that every R13 write restarts the envelope; the dropped-write reasons
  as a "why is it silent" checklist with the register to change for each; the log, its
  filters and export; audio-write breakpoints; mute, solo and `AUDIO_CHANNEL_MUTE`. A reader
  of the developer guide finds the audible copy versus the serialised hardware aggregate, the
  tap seam and its cost rule, INS-22, INS-23 and the `AudioWrite` kind. FEATURES.md lists the
  Phase 1 panels. `make docs-check` green.
- **Depends on.** A1.1 to A1.12.

---

## 6. Phase A2: see over time

Goal: the user sees the tune frame by frame, where the player runs in the frame, the waveforms,
and whether a sample stream is paced.

### A2.1 Frame grouping helper

- **Design.** A published pure header `src/debug/audio_frames.h`. It defines
  `struct AudioFrame { uint32_t frame; std::array<std::array<int16_t,14>,3> final_value;`
  `std::array<uint16_t,3> written_mask, double_mask; uint32_t writes; first/last {vc, hc,`
  `tstate}; uint16_t pc_min, pc_max; }` (`final_value` -1 = not written) and a streaming
  `AudioFrameAccumulator` (feed an `AudioLogEntry`; a completed frame comes out when the frame
  number advances, plus `flush()`). It has three consumers: the tracker, player timing and the
  `.psg` writer (A3.1). Dropped writes and markers never count as writes; a marker flushes.
- **Files.** `src/debug/audio_frames.{h,cpp}` and `src/debug/CMakeLists.txt` (in
  `jnext_debug`); `test/lint-debug-headers.sh` (`debug/audio_frames.h` added to `HEADERS`);
  `test/debug/audio_debug_test.cpp`; `test/unit-tests.conf` (count).
- **Tests.** `AFG-01..` with hand-built entry lists: last value wins in `final_value`;
  `double_mask` set on a second write to one register; registers 14 and 15 ignored for
  `final_value` and counted in `writes`; empty frames between written ones; first and last
  timestamps; PC range; DMA writes counted; a marker flushes.
- **Acceptance.** Every field of `AudioFrame` is asserted on a frame where it holds and on one
  where it does not; the header lint compiles `audio_frames.h`; the core suite is green in all
  four unit gates.
- **Depends on.** A1.7.

### A2.2 Tracker sub-tab

- **Design.** One row per frame from the accumulator over `audio_log_entries`: per channel the
  note (when the period changed), volume nibble or `E`, noise period when the channel's noise is
  on, and per chip the envelope shape and period when written, in Vortex Tracker's column order,
  with the three chips side by side. Changed cells are highlighted. A frame with no AY write
  between written frames gets an "empty" marker, and a frame with any `double_mask` bit gets a
  "double" marker. Double-click a row to `rewind_to_frame(frame)` when the frame is inside
  `rewind_range()`; otherwise the action is greyed and the tooltip says why. The view requests
  the log while visible (as in A1.8).
- **Files.** `src/debugger/audio_tracker_view.{h,cpp}` (new), `audio_panel.cpp`,
  `src/debugger/CMakeLists.txt`, `test/debugger/audio_panel_test.cpp`, `test/unit-tests.conf`
  (count).
- **Tests.** `DAP-` rows: a scripted 4-frame write stream renders the expected cells; empty and
  double markers; rewind called with the row's frame; greyed outside the range; showing and
  hiding the sub-tab moves `audio_log_enabled()`.
- **Acceptance.** For a known write stream the grid equals a hand-written expected grid, cell
  for cell; a double-click lands the machine on that frame (`time().frame` after the rewind);
  the log is requested only while the sub-tab is visible.
- **Depends on.** A2.1, A1.8.

### A2.3 Player timing

- **Design.** A strip in the Tracker sub-tab with, per frame, first and last write as line and
  T-state, write count and PC range, plus min/avg/max over the last N frames (N = 50 by
  default, editable) and a jitter strip (first-write line per frame as a dot plot). The label
  points to #369's T-state counter and `--profile` for exact routine cost.
- **Files.** `src/debugger/audio_tracker_view.{h,cpp}` (the strip; or a small
  `audio_timing_strip.{h,cpp}` if the view grows past about 800 lines), the per-frame
  statistics in `src/debug/audio_frames.{h,cpp}` (pure, so the core suite tests them),
  `test/debug/audio_debug_test.cpp`, `test/debugger/audio_panel_test.cpp`.
- **Tests.** `AFG-` rows for the window statistics (min/avg/max of first-write line and count
  over N frames, on hand-built frames). `DAP-` rows over a fixture whose IM2 player writes at a
  known line, from the log's own stamps: min = max for a steady player; a deliberately late
  frame shows in max and in the strip.
- **Acceptance.** The strip's numbers equal the statistics computed by the core helper from the
  same frames; the late frame of the fixture is visible as such; rows green.
- **Depends on.** A2.1.

### A2.4 Source tap (INS-24)

- **Design.** `Mixer::SourceTap` holds per-source `int64` accumulators (9 AY channel outputs
  0-255, 4 DAC channels, the beeper term, the hardware mix L/R, and for A3.4 the per-chip
  post-pan L/R and the Pi I2S L/R) and is fed by `accumulate_impl_()` when non-null. Every
  term is the hardware one: the AY terms come from A1.2's unmuted aggregate through a new
  `TurboSound::hw_chip_pcm()`, the DAC from `Dac::channel()`, and the mix is their 13-bit sum
  with the `exc_i` gate but no host gain or mute. At
  `emit_sample_impl_()` it averages each source with the mixer's own rounding and pushes one
  frame of samples to its consumers: the scope ring (16 sources x 8192 samples, about 186 ms;
  allocated on enable) and the stems writer. Taps are pre-mute and pre-gain (D12). The
  `Debugger` holds the per-client requests.
- **Files.** `src/audio/mixer.{h,cpp}`, `src/core/emulator.cpp`, `src/debug/debugger_audio.cpp`,
  `debugger.h`, `inspect.h`.
- **Tests.** `SCP-01..`: a DC fixture gives constant samples equal to the hand-computed source
  level; a beeper square wave averages to the expected duty level per sample; AY channel A
  alone moves `Ay0A`; with HALT batching on and off (the switch `halt_batch_equiv_test` uses)
  the scope samples are identical (GH #452); nothing is collected while unrequested.
  `AIZ-10`: tap on vs off.
- **Bench.** `make bench` A/B with the tap off (one branch per slice) and on; both numbers
  recorded.
- **Acceptance.** With the tap off, retired instructions are within the control-build floor of
  the epic's base; with it on, the cost is recorded in the PR; the scope samples equal the
  hand-computed levels and do not change with HALT batching.
- **Depends on.** A1.2 (the hardware aggregate it reads).

### A2.5 Scopes sub-tab

- **Design.** A grid of 16 small traces (9 AY, 4 DAC, beeper, L and R) plus a meter per
  source. Each trace is rising-edge triggered on its own source, with level at mid-scale and
  holdoff of one display frame; with no trigger in the window it free-runs, and the trigger
  lamp shows it. Refresh is at display rate while visible (decision 3). Under `--silent` or with
  audio disabled the sub-tab shows "audio synthesis is off".
- **Files.** `src/debugger/audio_scope_view.{h,cpp}` (new), `audio_panel.cpp`,
  `debugger_manager.cpp` (the live-refresh predicate covers Scopes), `src/debugger/CMakeLists.txt`,
  `test/debugger/audio_panel_test.cpp`.
- **Tests.** `DAP-` rows: a square-wave fixture triggers at the same phase on consecutive
  refreshes; the request follows visibility; the synthesis-off placeholder.
- **Acceptance.** A steady tone gives a still trace at display rate; the source tap runs only
  while the sub-tab is visible; the cost of the open window with Scopes visible is measured
  (host instructions per frame, window closed vs open) and recorded in the PR.
- **Depends on.** A2.4, A1.4.

### A2.6 CTC and DMA queries (INS-25)

- **Design.** `CtcChannel::DebugView` and `Dma::DebugView` (const, not published) feed
  `ctc_state()` and `dma_state()`. Timer-mode rate = 28 MHz / (prescaler x TC). A counter-mode
  channel takes the previous channel's ZC/TO rate / TC; channel 0 is fed by channel 3
  (`i_clk_trg => ctc_zc_to(2 downto 0) & ctc_zc_to(3)`, `zxnext.vhd:4598`). The CTC runs at
  28 MHz (`i_CLK => i_CLK_28`, `:4586`), and the prescaler and edge logic are in
  `device/ctc_chan.vhd:146-150`. DMA rate = 875 kHz / prescaler: the DMA timer adds 8/4/2/1 per
  CPU clock at 3.5/7/14/28 MHz and is compared on bits 13:5 (`device/dma.vhd:249-254,424`), so
  28 MHz / 32 = 875 kHz at every speed.
- **Tests.** `ACT-01..`: rates for a timer at prescaler 16 and 256 with TC 1, 100 and 0
  (= 256); counter mode chained from a running timer; the ch3-to-ch0 chain; DMA prescaler 0 and
  55 at each CPU speed (the rate must not change with speed); `AIZ-11`: 1000 queries mid-run
  leave the state bytes equal.
- **Files.** `src/peripheral/ctc.h` (`CtcChannel::DebugView`), `src/peripheral/dma.h`
  (`Dma::DebugView`), `src/debug/inspect.h`, `debugger.h`, `debugger_inspect.cpp`,
  `debug_types_check.cpp`, `b0-cap-traceability.md`, `test/debug/audio_debug_test.cpp`.
- **Acceptance.** Every field of `CtcChannelState` and `DmaState` has a row with its VHDL
  citation; the DMA rate is the same at all four CPU speeds; the queries leave the machine
  untouched.
- **Depends on.** Nothing in this epic (it can run in parallel with A1).

### A2.7 Sample-stream view

- **Design.** A pure helper in `audio_frames.h`, `dac_stream_stats(entries, window)`, groups DAC
  writes by writer (CPU by PC, DMA, Copper) and reports rate (Hz), interval min/avg/max, jitter
  (standard deviation), and gaps (intervals above 2x the median, D13). The DAC & Beeper
  sub-tab shows it next to the CTC channels and the DMA state from A2.6, with the computed
  pacing rate beside the measured write rate.
- **Files.** `src/debug/audio_frames.{h,cpp}` (`dac_stream_stats`), `src/debugger/audio_panel.cpp`
  (or the DAC & Beeper view file of A1.6), `test/debug/audio_debug_test.cpp`,
  `test/debugger/audio_panel_test.cpp`.
- **Tests.** `DSS-01..` on hand-built streams: steady 15 625 Hz from one PC; a gap; two writers
  kept apart; DMA vs CPU. `DAP-` row: the view shows the CTC rate beside the measured one.
- **Acceptance.** For a CTC-paced DAC fixture the measured write rate and the CTC's computed
  rate agree to the CTC's own resolution, and a deliberately skipped sample shows as a gap.
- **Depends on.** A1.7, A2.6.

### A2.8 Phase 2 doc pass

- **Design.** The closing pass over what sub-issues 6-9 changed; no issue numbers in the
  manuals.
- **Files.** `src/doc/user-guide/06-debugger/panels/13-audio.md`;
  `src/doc/developer-guide/03-subsystems/04-audio.md`, `09-1-the-backend-api.md`,
  `10-2-the-qt-debugger.md`; the rendered `doc/user-guide/` and `doc/developer-guide/`;
  `FEATURES.md`; the DIRECT-VALUE banner (comment only).
- **Tests.** `make docs-check`; the banner recount.
- **Acceptance.** A reader of the Audio page finds how to read the tracker (empty and double
  frames, the jump back), what player timing measures and what it does not (exact cost is the
  T-state counter or `--profile`), how the scopes trigger and that they need audio synthesis
  on, and how to read the sample-stream view against the CTC and DMA pacing. The developer guide
  describes the source tap, its cost rule, INS-24 and INS-25. `make docs-check` green.
- **Depends on.** A2.1 to A2.7.

---

## 7. Phase A3: take it elsewhere

### 7.1 The `.psg` format as written (A3.1)

The writer reproduces Fuse 1.6.0's `psg.c`, which was read for this plan:

- Header: `"PSG\x1a"` and 12 zero bytes, **16 bytes** (`psg_start_recording()`).
- `empty_frame_count` starts at 1. At each frame end: if any of registers 0-13 was written,
  the writer emits the separators for `empty_frame_count` (`0xFE n` for each group of 4 frames,
  n <= 255, then single `0xFF`s), then `reg, value` pairs **in register order 0..13**, and
  resets the count to 1; otherwise it increments the count (`psg_frame()`,
  `write_frame_separator()`).
- On stop: flush the current frame, then emit the separators (`psg_stop_recording()`).
- Registers 14 and 15 are never written. No 0xFD end marker.
- The value is the raw byte the guest wrote (`ay.c`, `psg_write_register(current, b)`).
- **One deliberate difference** (D4): Fuse keeps the **first** value written to a register in
  a frame (`if( psg_registers_written[reg] ) return 0;`); jnext keeps the **last**, which is
  what the chip holds when the frame ends. For a player that writes each register at most once
  per frame (the common case, and the A3.2 fixture) the bytes are identical.
- Per chip: the writer feeds stream c from AY data writes to chip c that reached the chip
  (dropped writes are excluded; chip-select commands are not register writes). It uses the
  `AudioFrameAccumulator` (A2.1) per chip and the frame number of §4.3.

### 7.2 Decision 9: how the writer is checked

**Fuse can be driven non-interactively, but only through its GUI, and not reliably enough for
CI.** Evidence from 2026-11-18:

- Fuse 1.6.0 has **no** command-line or debugger command for AY logging; the man page lists
  only "File, AY Logging, Record..." and "Stop", and `--debugger-command` (the scripted-oracle
  route of `technique_fuse_headless_oracle`) has no PSG verb.
- Under Xvfb with `GDK_BACKEND=x11`, `xdotool` opened File > AY Logging > Record...: a click
  on the menubar, then keyboard Right and Return on the submenu (a click on the submenu item
  did nothing), then the "Fuse - Start AY Log" chooser took a typed path and `key --window
  <id> Return`. File > Exit flushed the file: `50 53 47 1a` plus 12 zero bytes, then
  `fe ff fe 61 ff` (the session's program wrote no AY: 1020 + 388 + 1 empty frames). It
  confirms the 16-byte header and the separator encoding of §7.1.
- Two of the four GUI steps failed on the first attempt for timing reasons, and the user's
  `~/.fuserc` auto-loaded a snapshot, so a clean `HOME` is required.

**Decision.** Use Fuse **once, by a maintainer**, to make a committed reference, and compare
against it in CI (A3.2). **No ZXTune is provisioned** (D3). Byte identity with Fuse's own
writer on the same program is stronger than any reader accepting the file: every player that
reads Fuse's `.psg` files reads ours. The one known divergence (D4) is pinned by a hand-built
row instead.

### A3.1 `.psg` writer

- **Design.** `src/audio/psg_writer.{h,cpp}`: a streaming writer per chip implementing §7.1,
  fed by the `Debugger` from the tap (the tap stays on while a recording runs). Files are
  opened `"wb"` at start, so an unwritable path is refused before anything runs (the
  `RzxRecorder::can_write` rule).
- **Tests.** `PSG-01..` against hand-built byte arrays: header; one write in frame 0 gives
  `FF r v`; a 3-frame gap gives `FF FF FF`; a 4-frame gap gives `FE 01`; 7 frames give
  `FE 01 FF FF FF`; 1021 empty frames give `FE FF FF`; several registers emitted in register
  order regardless of write order; **last value wins** (D4); R14/R15 excluded; a dropped write
  excluded; a chip-select command excluded; stop flushes and terminates; three chips go to
  three streams; `AIZ-12`: recording on vs off.
- **Files.** `src/audio/psg_writer.{h,cpp}`, `src/audio/CMakeLists.txt`,
  `src/debug/debugger_audio.cpp` (start/stop verbs), `debugger.h`, `test/debug/audio_debug_test.cpp`.
- **Acceptance.** Every `PSG-` row's expected bytes are written out by hand in the row, from
  §7.1 and Fuse's `psg.c`; the writer is fed only by the tap; an unwritable path is refused at
  start.
- **Depends on.** A2.1.

### A3.2 Fuse reference

- **Design.**
  - `test/audio/fixtures/psg_fixture.asm` and its assembled `psg_fixture.bin`, both committed
    (as `test/00regression/bin/ay_envelope_sweep.{asm,bin}` are), with the bytes listed in the
    `.asm` header so the `.bin` can be regenerated with no toolchain. It is a 128K program in
    our own source, origin 0x8000. It runs IM2 with its own vector table, so jnext needs no ROM.
    Its first frame of sound is a **sentinel**: one write of R7 = 0x3F followed by eight empty
    frames, which nothing else in the sequence repeats. After about 100 frames of silence it
    plays a fixed 300-frame sequence. That sequence writes each register at most once per frame
    right after `HALT`, varies 1 to 14 registers per frame, includes R13 writes with unchanged
    values, and has gaps of 1, 3, 4, 5 and 9 empty frames. Then it stops writing and loops on
    `HALT`.
  - `tools/fuse-psg-reference.sh`: a maintainer tool, never run by CI. It builds a `.tap`
    from the fixture (`z88dk-appmake +zx`), starts `/usr/bin/fuse --machine 128 --no-sound
    --auto-load` under its own `xvfb-run -d` with a scratch `HOME`, drives the menu with
    `xdotool` as in §7.2 (each step retried until the expected window appears, then verified),
    records, exits through File > Exit, and writes `test/audio/fixtures/psg_fixture.fuse.psg`.
    Every external command runs under `LANG=C` and `timeout --kill-after`.
  - Row `PSF-01`: inject the `.bin` at 0x8000 into an in-process 128K-timing machine, run 500
    frames with the writer on, then compare with the committed file after normalising both:
    cut everything before the sentinel frame and drop the trailing separator run. Cutting at the
    sentinel makes the comparison independent of when Record was clicked, even if the 128K
    ROM or the tape loader touched the AY first in the Fuse run. The remainder must be
    byte-identical.
  - The fixture and its Fuse output are our own program's register stream: license-clean, no
    ROM or SD image content.
- **Files.** `test/audio/fixtures/psg_fixture.{asm,bin}`, `test/audio/fixtures/psg_fixture.fuse.psg`,
  `tools/fuse-psg-reference.sh`, `test/debug/audio_debug_test.cpp`, `test/CMakeLists.txt` (the
  fixtures staged for the suite, re-staged on change, like the other fixture files), the
  section of `AUDIO-TEST-PLAN-DESIGN.md` naming Fuse 1.6.0 as `PSF-01`'s oracle.
- **Tests.** `PSF-01`, plus `PSF-02`: the normalisation itself on two hand-built files that
  differ only before the sentinel and in the trailing run, and that give different results
  when they differ after it. A mutation that changes the separator encoding or the register
  order fails `PSF-01` as well as the `PSG-` rows.
- **Acceptance.** The committed `.psg` was produced by `tools/fuse-psg-reference.sh` (the
  commit message says so, with the Fuse version); `PSF-01` is green in all four unit gates
  without Fuse installed; nothing in CI runs Fuse.
- **Depends on.** A3.1.

### A3.3 `.psg` export: GUI and `--psg-record FILE`

- **Design.** CLI `--psg-record FILE`: AY#0 goes to FILE; AY#1 and AY#2 go to FILE with `-ay1`
  / `-ay2` inserted before the extension, **created at that chip's first write** (D5). It works
  in every frontend, `--silent` included. GUI: "Record PSG..." and "Stop" in the Chips sub-tab,
  with a recording lamp.
- **Files.** `src/core/cli_options.h`, `src/main.cpp`, loop owners, `audio_panel.cpp`,
  `doc/man/jnext.1.md`.
- **Tests.** Regression row `psg-record-func`: `--headless --machine 128k --inject
  psg_fixture.bin --inject-org 8000 --inject-pc 8000 --inject-delay 100` (the injection and
  boot delay `audio-envelope-func.sh:63-64` already uses on the 128K boot) with `--psg-record`, then the file is compared to `psg_fixture.fuse.psg` after
  the same normalisation (the path through `main.cpp` that unit rows cannot reach), and no
  `-ay1` file exists. `DAP-` row for the GUI start/stop. `make cli-check`.
- **Acceptance.** `psg-record-func`, `make cli-check` and `make docs-check` green;
  `--psg-record` and its file naming are in the man page; the GUI and the CLI produce the same
  bytes for the same run.
- **Depends on.** A3.2.

### A3.4 Per-source WAV stems

- **Design.** On `emit_sample_impl_()` the source tap (A2.4) gives, per output sample, the
  averaged terms with the mixer's rounding and AC coupling (term minus its rest level: beeper 0,
  AY 0, DAC 1024, Pi 512; times 4), before host gain and before the mutes (D12). Files in DIR,
  all 44.1 kHz 16-bit stereo through `AudioRecorder`: `ay0.wav`, `ay1.wav`, `ay2.wav` (post-pan,
  post-mono, zero while the chip is not in the mix), `dac.wav` (L = A+B, R = C+D), `beeper.wav`,
  `pi.wav`, `mix.wav`. The Pi stem exists so that the stems sum to the mix (D14). CLI
  `--wav-record-stems DIR` creates DIR if missing; it is refused with `--silent` or audio
  disabled, as `--wav-record` is (`main.cpp:1792,2741`). GUI: "Record stems..." and "Stop" in
  the Scopes sub-tab.
- **Tests.** `STM-01..`:
  - with every term constant within each sample period, the stems sum exactly to `mix.wav`;
  - with arbitrary input, |sum of the 6 stems - mix| <= 3 LSB per sample. The bound is derived,
    not tuned: 7 independent roundings of at most 0.5 each;
  - at 0 dB and no mutes, `mix.wav` equals the `--wav-record` capture byte for byte;
  - with AY gain -6 dB and AY1 muted, the stems are unchanged (pre-gain, pre-mute);
  - with HALT batching on and off, the stems are identical;
  - a chip outside the mix gives a silent stem.

  Plus `AIZ-13`: stems on vs off. Regression row `wav-stems-func` (headless; seven valid WAV
  files; the `--silent` refusal message). `make cli-check`.
- **Files.** `src/audio/mixer.{h,cpp}` (the stem terms of the source tap),
  `src/audio/audio_recorder.{h,cpp}` (reused, one instance per stem), `src/debug/debugger_audio.cpp`,
  `debugger.h`, `src/core/cli_options.h`, `src/main.cpp` and the loop owners,
  `src/debugger/audio_panel.cpp`, `doc/man/jnext.1.md`, `test/debug/audio_debug_test.cpp`,
  `test/00regression/scripts/wav-stems-func.sh`, `test/00regression/functional_tests.conf`
  (row and `# expect:` counts).
- **Acceptance.** The sum identity holds exactly on the DC fixture and within the derived 3 LSB
  bound everywhere; `mix.wav` equals `--wav-record` at 0 dB; `wav-stems-func`, `make cli-check`
  and `make docs-check` green.
- **Depends on.** A2.4.

### A3.5 Phase 3 doc pass and close-out

- **Design.** The closing pass over sub-issues 10 and 11, and the epic's records.
- **Files.** `src/doc/user-guide/06-debugger/panels/13-audio.md` (and the recording page of the
  user guide, if the CLI flags are listed there); `src/doc/developer-guide/03-subsystems/04-audio.md`;
  the rendered guides; `doc/man/jnext.1.md` and outputs (if more than A3.3 and A3.4 gave it);
  `FEATURES.md`; this document (a bench record appended); the DIRECT-VALUE banner.
- **Tests.** `make docs-check`, `make cli-check`, the banner recount (final total 60).
- **Acceptance.** A reader of the user guide finds what each `.psg` and stem file contains, how
  to load a `.psg` into an AY player, that jnext keeps the last value written in a frame where
  Fuse keeps the first, and why the stems sum to the mix. The developer guide explains the
  writers and how to regenerate the Fuse reference. This document carries the bench record
  (retired instructions, taps off and on, from A1.7, A2.4 and A2.5), and the ChangeLog draft (§13)
  is handed to the owner. `make docs-check` green.
- **Depends on.** A3.1 to A3.4.

---

## 8. Tests at a glance

| Suite | Gate / OS | New prefixes | Holds |
|---|---|---|---|
| `audio_debug_test` (new) | core, `# gate: none`, `# os: all` (Linux Qt and SDL trees, Windows under wine, WASM under node) | `ANO` `AIZ` `ADS` `DRP` `AWL` `AEV` `ACM` `AFG` `SCP` `ACT` `DSS` `PSG` `PSF` `STM` | every backend, tap, writer and invisibility row |
| `?debugger_audio_panel_test` | unchanged gate | `DAP-18..` | the five sub-tabs, live refresh, visibility requests, Solo |
| `?debugger_panels_test` | unchanged gate | `QBA` | "Audio write" breakpoints |
| `?script_parse_test`, `?script_eval_test`, `?script_events_test` | unchanged | `PARSE-AYW` `EVAL-AYR` `EVAL-ACM` `EVT-AYW` | DSL |
| `cli_options_test` (via `make cli-check`) | unchanged | (table-driven) | `--audio-log`, `--psg-record`, `--wav-record-stems` |
| regression | Linux, plus `os=` per row | `audio-log-func`, `psg-record-func`, `wav-stems-func` | the `main.cpp` paths |

- `git grep` found no use of any of these prefixes, of `audio_debug_test`, of the three row
  names, or of `INS-22` to `INS-25` (2026-11-17). Every ID is a literal, unique across suites
  (`traceability-dup-ids.pl`).
- The planned rows live in `AUDIO-TEST-PLAN-DESIGN.md`'s new section with their oracle:
  the VHDL citation for decode rows, the Fuse reference for `PSF`, the property of §4.2 for
  `AIZ` and the host-only rows.
- No SKIP anywhere. The regression rows need only `python3` and the built binaries, which are
  already provisioned (`ci.yml` `test` job).
- Every task bumps its suite's `# expect:` and recounts after merging back into the epic branch
  (the auto-merge trap of identical counter edits).
- Mutations come from each task's diff, not its row list; each task lists the ones it must kill.
- Branch gates per CLAUDE.md: targeted suites in every configuration the change touches
  (`unit-test-sdl` and `unit-test-win` for any core or audio change; `unit-test-wasm` for the
  core suite), the affected regression rows via `make regression-rows`, and `make unit-test`
  before each review round. No PGO and no full regression on a branch; `make bench` only where
  a task says so.

---

## 9. Risks

| Risk | Consequence | Mitigation |
|---|---|---|
| The Mixer tap branch shows up in the per-instruction cost | a measurable slowdown with the debugger closed | null-pointer gate as `mute_mask_`; retired-instruction A/B with a control build (A1.7, A2.4); if over the floor, move the tap behind the `audio_off_` arm with a separate accumulate entry point |
| A1.2 changes what is serialised for a muted machine | old snapshots saved while muted carry a muted aggregate | a load rebuilds the audible copy; the stored aggregate is only used until the next PSG tick (existing `chip_pcm_valid_` path); stated in the developer guide |
| Fuse GUI automation is flaky | the reference cannot be regenerated easily | maintainer-only tool with retries and window checks; the committed file is the CI oracle; the steps are recorded in §7.2 |
| The fixture plays differently in Fuse and jnext (frame offset of about 1.5 lines on 128K) | `PSF-01` false failure | writes right after `HALT`, far from the boundary; normalisation of leading and trailing runs |
| `AudioWrite` adds an `EventKind` value | the compile-time pins in `debug_types_check.cpp:109,113,116` break the build | appended before `Count`, existing values unchanged; A1.9 moves the three asserts and the architecture doc's §4.3 table in the same change |
| A beeper engine floods the ring (tens of kHz of EAR toggles) | the log holds seconds, not minutes | per-target filter in the GUI; `audio_log_resize`; beeper logged on change only |
| #377 (dockable panels) lands mid-epic | the Audio tab's sub-tab layout must move | sub-tabs live inside `AudioPanel`, a single widget a dock can host as is |
| #372 lands mid-epic and reshapes the Add dialog | A1.10 conflicts | A1.10 reuses #372's widgets if present; merge order decided by the orchestrator |
| #498 introduces its own visibility mechanism | two ways of asking for data | A1.8 uses plain per-client requests that #498 can adopt; whichever lands second aligns to the first |
| Display-rate refresh of the Chips and Scopes views costs host CPU while the window is open | the window costs more than its 4 Hz refresh | only while visible (decision 3); measured with the window open in A2.5 |
| Stems of a long headless run fill the disk | a failed run late | `AudioRecorder` reports a write error; exit is non-zero, like `--wav-record` |

---

## 10. Owner questions (ANSWERED 2026-11-18)

No owner question is open. The owner answered both of the first draft's questions "yes":

1. **Q1. Phase 1 on `main` early? ANSWERED: yes.** When Phase 1 (sub-issues 1-5) is done, the
   epic branch merges into `main` on its own, then continues for Phases 2 and 3 and merges
   again at the end. Folded into the Status line, the task list and §5.
2. **Q2. DSL accessor `ay[c, r]` instead of the scope's `ay[c][r]`? ANSWERED: yes.**
   `ay[c, r]`, no grammar change. Folded into A1.11 and D11.

---

## 11. Decisions taken in this plan

| # | Decision | Why | Rejected alternative |
|---|---|---|---|
| D1 | One tap seam: a `bool` gate at each audio write site and a null-pointer gate in `Mixer::accumulate_impl_()`; consumers are per-client requests in the `Debugger` | zero cost when off, measured by retired instructions; one place to reason about | per-feature hooks at each site; a `std::function` callback per write (the `--dac-trace` shape), which costs an indirect call per write even for one consumer |
| D2 | The dropped-write counters are always on | DAC and mirror drops sit on cold early-return branches, AY drops cost one compare per AY port write (a returned status, A1.5); the user gets the answer even if they opened the panel late | counting only while the panel is visible (loses the evidence that matters most) |
| D3 | No ZXTune is provisioned | byte identity with Fuse's own writer on the same program implies acceptance by every reader of Fuse's files; ZXTune is a large C++ tree with heavy build dependencies, and its binary releases were not checked | a sha256-pinned ZXTune build under `~/.cache/jnext/tools` as an extra reader |
| D4 | `.psg` keeps the last value written to a register in a frame | it is the chip's state at the frame end, which a frame-based replayer reproduces; Fuse's first-value rule replays the wrong value when two writers touch one register in a frame | Fuse's first-value rule, for byte identity in every case |
| D5 | `--psg-record FILE`: AY#0 to FILE, AY#1/#2 to `-ay1`/`-ay2` files created on their first write | a 128K tune gives exactly the file named; TurboSound tunes get one file per chip | three files always (two silent files for a 128K tune); a prefix-only naming |
| D6 | A 0xDFFD write is not a dropped AY write | it is the paging port's own address; counting it would flood the counter in every program that pages | count every `port_fffd`-pattern write that `not port_dffd` suppresses |
| D7 | Drop precedence: decode before gate (family before DAC enable) | an undecoded write is not a write to that device in the VHDL (`port_dac_X` is the decode, `dac_hw_en` the gate) | jnext's current order (`dac_enabled_` tested first) |
| D8 | Frames for the tracker, timing and `.psg` are the stamper's frames | the ULA interrupt is within one line of the wrap on every timing; events, scripts and the log then agree | grouping by interrupt instants (a second frame notion for one feature) |
| D9 | No recording in rewind replay; a rewind removes the entries from the landing cycle on, markers included (a `StateLoad` marker at the landing cycle stays; a load from another session has an unordered clock, a stated limit), and adds a `Rewind` marker; resets and loads add a marker, the same marker twice at one cycle being one row (amended by GH #531) | the log then describes the machine's actual timeline | recording replayed writes twice; clearing the log on every rewind |
| D10 | Plain 0xFFFD register selects are not log entries; the selected register is folded into the next data write | a select makes no sound and doubles the entry count of every player | log every port write (the `io_write` event already offers that) |
| D11 | The DSL accessor is `ay[c, r]` (owner-confirmed, 2026-11-18) | matches `phys[page, offset]` and the lexer's bracketed-stem rule | `ay[c][r]` with chained brackets (a grammar change for one accessor) |
| D12 | The scope and stem taps are pre-mute and pre-gain | stems are for A/B against hardware and must not depend on listening aids; a muted channel's scope still shows what is muted | post-mute taps (a muted channel's stem goes silent) |
| D13 | A sample-stream gap is an interval above 2x the median | robust to the stream's own rate; parameter shown in the view | a fixed time threshold |
| D14 | The stems include `pi.wav` | the Pi I2S term is part of the 13-bit sum (`audio_mixer.vhd:99-100`); without it the stems do not sum to the mix | omit it (the sum identity then holds only with the Pi input idle) |
| D15 | The level bar shows the channel's amplitude (volume or envelope level), not the instantaneous output | the instantaneous output toggles with the tone square wave and flickers; the scope shows the waveform | bar = `output_a/b/c()` |
| D16 | The note helper and the frame grouping are published pure headers tested in the core suite | three consumers each (panel, tracker, writer); they run in all four unit gates instead of only the Qt one | logic inside the Qt panel |
| D17 | Per-channel solo and mute act on an audible copy in TurboSound and the Mixer | it keeps ACB, mono and pan exact and leaves the serialised hardware outputs alone (F1) | muting in the panel by rewriting AY volumes (machine state) |
| D18 | Player timing lives in the Tracker sub-tab and the sample-stream view in DAC & Beeper | decision 6 names five sub-tabs; both are views of data those sub-tabs already show | two extra sub-tabs |
| D19 | F1's fix adds a new serialised hardware aggregate under the existing `pcm_l`/`pcm_r` keys and keeps `pcm_left()`/`chip_pcm()` as the audible output | the snapshot format is unchanged, the Mixer is untouched, and `DAP-08..15` and `AYC-16` keep passing without edits | repurpose `pcm_left()` as the hardware value (rewrites the existing mute rows and the Mixer's read) |
| D20 | AY drops are reported by `TurboSound::reg_addr()`/`reg_write()` returning a status, mapped to a reason in the Emulator handler | the decision stays in the one place that makes it, plus one test on the ignored-write branch (to tell a select command with TurboSound off from any other ignored value), so it cannot drift; one compare per AY write | re-testing `reset_held()`, the address bit and the select pattern in `emulator.cpp` before each write (a copy of TurboSound's logic) |
| D21 | 23 tasks grouped into 11 sub-issues, one per scope item, and the three phase doc passes kept as epic-level tasks on #224; A1.1-A1.3 go to sub-issue 1 | decision 7 (one sub-issue per item) with a work breakdown fine enough to review; A1.1-A1.3 are first consumed by the decoded view; a doc pass inside a sub-issue would make that sub-issue depend on its whole phase and serialise it | one sub-issue per task (26, against decision 7); a separate groundwork sub-issue (a twelfth issue no scope item names); the doc passes inside each phase's last sub-issue (round 2 of this plan) |

---

## 12. Dependencies outside the epic

- **#498** (collect only while a panel needs it): this epic follows its rule from the start
  (§4.1) and needs none of its code.
- **#372** (GUI breakpoint options): independent; A1.10 adopts its widgets if it lands first.
- **#369** (T-state counter): referenced by A2.3 for exact routine cost; no dependency.
- **#377** (dockable panels): no dependency; see §9.
- No new build or runtime dependency. Fuse, Xvfb, `xdotool` and z88dk are needed only by the
  maintainer tool of A3.2, and only to regenerate a committed file.

---

## 13. ChangeLog draft (for the owner, at release time)

- User Features: none beyond what Developer Features says (the panels are debugger tools).
- Developer Features:
  - Debugger Audio tab: decoded AY chips with notes and cents, envelope, routing, and
    dropped-write reasons.
  - Audio write log, audio-write breakpoints, `.jds` `ay_write`, per-channel solo and mute.
  - Tracker view, player timing, oscilloscopes, DAC sample-stream view with CTC/DMA pacing.
  - `.psg` export per AY chip and per-source WAV stems, from the GUI and the command line.
- Bug Fixes: debugger AY mutes no longer change saved snapshots and rewind slots.
- Contributors: none known (the issue is the owner's).
