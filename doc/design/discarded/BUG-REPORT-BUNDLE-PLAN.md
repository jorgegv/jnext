# Bug-report bundle: one file to replay a failing session locally (GH #492)

**DISCARDED by the owner on 2026-11-20.** The replayable bundle is too much work for its benefit:
most of it exists only to keep NextZXOS content out of a replayable file. GH #492 was reformulated
as a non-replayable diagnostic report. This plan is kept for reference only; nothing in it is
scheduled.

Status: **APPROVED; owner decisions recorded (2026-11-20, section 17)** (planner of the 2026-11-17
autonomous run;
revision 1, 2026-11-20: the independent review's eight findings folded in; revision 2,
2026-11-20: round 2's five findings; revision 3, 2026-11-20: round 3's three findings, with
measurements of the real distribution; revision 4, 2026-11-20: round 4's false-positive finding,
measured, with the clean-corpus remedy; revision 5, 2026-11-20: round 5's two findings, the clean
corpus made code-only and the false-positive count re-measured per source; review round 6
approved; the owner answered every question on 2026-11-20; see the last section). Nothing in this
plan is started. When the work resumes, #492 is an epic (Q1), on the branch `epic492-bug-report-bundle`: the sub-issues
of the task list (titled `epic-492: ...`) branch off it and merge back into it, and the epic
reaches `main` in one merge (CLAUDE.md, merge protocol step 1).

The goal, from the issue: a bug reporter makes **one file** that lets a developer replay the
failing session locally, with the full debugger, rewind and every panel, with no network
connection to the reporter and no live session.

**Two hard rules shape every part of this design.**

1. **No NextZXOS content ever goes into a bundle**: not the SD image (standard or minimal), not
   a piece of it, and not the firmware a booted machine holds in RAM. A `.jns` of a NextZXOS
   session holds NextZXOS and DivMMC ROM content in RAM (NEXT-SNAPSHOT-FORMAT.md, non-goal N3;
   `warm_start_cache.h` says the same of the warm-start recording). A bundle is made to be
   attached to a GitHub issue, which is publishing. So a bundle of a firmware-booted session
   carries **no machine memory at all**: it starts from a state the developer's own jnext can
   rebuild from the developer's own card, and replays everything after it (section 3). Every
   content member is checked byte by byte against the distribution before it is written
   (section 5.3), and the residuals that no check can close are stated there as rules and put to
   the owner (Q14).
2. **The replay must be proved, not hoped for.** Every bundle carries state digests at regular
   checkpoints, and the replay compares them as it goes. A replay that drifts says so, at the
   first checkpoint that differs; it never shows a plausible session that is not the one
   recorded.

---

## Task list

One heading per sub-issue. The task lines under a heading are that sub-issue's checklist, with
stable IDs; the detail is in sections 10 to 15, and "Sub-issues and parallelization" below says
what can run at once.

### Phase B0: foundations

#### Sub-issue 1: host-input seam inventory and the session log core

- [ ] **B0.1** Seam inventory: every place host state enters the emulated machine (inputs, host actions, debugger mutations, runtime writes of machine state from frontend menus and Preferences, in-frame pulls, UART byte streams, card sectors, configuration), one recorder-observable seam per class; the table goes into the developer guide
- [ ] **B0.2** Session log core (`src/bundle/session_log.*`): anchor-relative cycle stamps, the event list, the value queues, the byte streams, seekable cursors, the memory cap
- [ ] **B0.3** Checkpoint digests: observable digest (CPU registers and all of RAM), framebuffer digest, full state-stream digest, every N frames; the recorder's cost measured with `make bench` (the rolling recording is on by default only if it is at most 1 %, Q2)

#### Sub-issue 2: the machine configuration a replay needs

- [ ] **B0.4** `EmulatorConfig` field table: every field classified machine / host / debug / excluded (with the reason); a completeness tripwire; serialise and restore the machine class by name; a machine-class field changed after `init` is logged as an event

#### Sub-issue 3: the warm start's read set

- [ ] **B0.6** Warm-start read set: the recording keeps the number, hash and bytes of every sector its boot read first, and the SHA-256 of its recorded state stream, for a bundle to name; a row proves host-class settings do not change the recording

### Phase B1: recording

#### Sub-issue 4: inputs and host actions

- [ ] **B1.1** Inputs at their seams: keyboard matrix and extended keys, both joystick connectors, the Kempston mouse (deltas, buttons, wheel), each with the cycle it was applied at
- [ ] **B1.2** Host actions and runtime machine writes: soft reset, NMI buttons (Multiface, DivMMC), drive button, tape deck (insert, play, stop, rewind), SD insert and eject, POK trainers, the `--delayed-*` actions, ESP associate and disassociate; the CPU speed menu and the Preferences apply (both write NR 0x07), the tape fast-load and auto-play toggles
- [ ] **B1.3** Debugger mutations: every verb behind `Debugger::Impl::note_mutation()` logged with its arguments (memory, registers, NextREGs, port writes); every memory write's payload (loads and uploads such as a DZRP `CMD_WRITE_MEM` alike) goes into `debugger/writes.bin`, out of `events.jsonl`, under the content check
- [ ] **B1.4** Timeline rules: anchors (power-on, hard reset, program load, machine change) start a new segment; a rewind truncates the log at the landing cycle; a quick snapshot taken in the segment and loaded back is a jump to that cycle; any other snapshot load is a new anchor or ends the segment

#### Sub-issue 5: in-frame pulls and byte streams

- [ ] **B1.5** Value queues for in-frame pulls: the RTC (`I2cRtc::snapshot_time()` at every I2C START, and `current_time()` for `M_GETDATE`), the esxDOS stand-in's host calls (open, read, stat, directory listing, time); the extended-NEX read overlay is not a pull (it serves the program file) and is re-derived on replay from the program member or reference
- [ ] **B1.6** UART RX tee: every byte the ESP-01, the NextPi UART and the joystick-port UART (fifo, pty) delivered into an RX FIFO, with its cycle; the TX side kept as a running digest only
- [ ] **B1.7** Rewind fast-forward while recording is fed from the log (RX bytes, RTC values, host calls), so a rewind lands on the recorded timeline even in a network session

#### Sub-issue 6: SD-card capture and the clean corpus

- [ ] **B1.8** First-access tracking per card: the input-sector set (sectors whose first access after the anchor is a read) with each sector's hash, and an in-memory undo log (the pre-write bytes of an input sector at its first write)
- [ ] **B1.9** The distribution index: the pristine reference of the card's base (the fixed-image recipe, the minimal-card recipe with its `dirs`), its sector-hash set, a byte-granular window index of the contents of every distribution file with the padding exclusion E1 (windows of period 4 or less, and nothing else), distribution files in decodable formats indexed decoded as well, the file table and the system paths; one rolling hash shared by the index build and the member scan; cached locally, built from the distribution even when the session had no card
- [ ] **B1.10** Card member: per-sector and per-file classification (section 5.3.3: pristine, system, distribution-derived, free space, non-filesystem, FSInfo, structure, user file); bytes only where the class allows, references for pristine sectors, hashes only for the rest; FAT path attribution for the privacy review; the second card the same way
- [ ] **B1.11** The clean corpus (5.3.1, Q18): run-time probes and library sweeps for the pinned z88dk, built from jnext's own sources by `make bug-report-clean-corpus`, linking **code and z88dk's own run-time data only** (third-party data such as the ROM fonts excluded by name, the build failing if a map names one); a licence check of every linked file recorded in this design before anything is committed (no proprietary content: PULL-REQUEST-PROTOCOL rule 3 and the owner's rule); committed as binaries, shipped in the packages, intersected with the index when it is built; its drop counts re-measured and re-pinned (BRB-FILE-40) once built

### Phase B2: the bundle file

#### Sub-issue 7: container writer and reader

- [ ] **B2.1** Container: a ZIP through `src/save/zip_archive.*`, `manifest.json` plus members (section 4), deflate, bounded sizes; the writer's content check over content **as the guest saw it** (section 5.3.2: RX sequences per UART, host files rebuilt per file, debugger payloads, the program decoded, inflated and run-length expanded), scanned at every byte offset against the distribution index; a classic `.szx` re-serialised to the chunks the loader parses, `RAMP` pages uncompressed; programs in formats with no decoder shipped as references only
- [ ] **B2.2** Reader: validation, version rules (section 8), the privacy summary, `--bug-report-info FILE`

### Phase B3: replay

#### Sub-issue 8: the replay engine

- [ ] **B3.1** Rebuild the anchor: power-on, program load, warm start (regenerated on its read set and checked against its digest), a classic snapshot file; the base is the pristine rebuild at its own path, never the developer's working card; references resolved from it; card assembly as a scratch journal bound by digest over that base (the #507 mechanism)
- [ ] **B3.2** Inject the log at its cycles with every live host injector off; replay devices for the value queues and the UART streams; per-sector read checks on the card
- [ ] **B3.3** Verification: checkpoint digests, first divergence reported with its frame and cycle, version-mismatch report, a distinct exit status headless
- [ ] **B3.4** End of the log: the machine pauses (GUI) or exits (headless), and continuing makes it live

#### Sub-issue 9: replay with the debugger and rewind

- [ ] **B3.5** Seekable replay cursors: a rewind or a step back during a replay re-feeds the log from the landing cycle; a debugger mutation during a replay warns that the rest may drift
- [ ] **B3.6** Landing points: replay to frame N, or to N seconds before the save point, and pause there with the rewind ring filled behind it

### Phase B4: user interface

#### Sub-issue 10: GUI menu and dialogs

- [ ] **B4.1** File > Bug Report menu (menu only, no hotkey: Q12): Start Recording, Save Bug Report... (proposing `<name>.YYYYMMDDhhmm.jnext-bug.zip`, Q5), Stop Recording, Open Bug Report...; status-bar indicator
- [ ] **B4.2** Save dialog with the privacy review (what goes in, sizes, the card files, the network hosts, notes, the card and host files waiting for the reporter's tick (Q14)); open dialog with the summary and the mismatch warnings; replay progress

#### Sub-issue 11: Preferences panel, configuration keys and CLI

- [ ] **B4.3** Preferences > Bug Reports panel (rolling recording, memory cap, checkpoint interval, include the program, include the log) and, in the same change, its `bug_report.*` rows in `config_keys.cpp` with their dedicated flags (project rule)
- [ ] **B4.4** `--bug-report-record FILE|DIR` (the pattern `<name>.YYYYMMDDhhmm.jnext-bug.zip` is Q5's; the directory form, which then uses that default name, is the planner's interpretation, for the owner to confirm), `--bug-report-play`, `--bug-report-info` and the rest of the option set of section 7; man page; `make cli-check`
- [ ] **B4.5** SDL-only frontend and `--headless` parity: record, save at exit, replay, the same exit statuses

### Phase B5: end-to-end proof and documentation

#### Sub-issue 12: determinism proof rows

- [ ] **B5.1** The probe program: a NEX (source in `demo/`) that folds every source of section 6 into its state, so a replay that loses one source diverges
- [ ] **B5.2** Functional rows: record and replay headless; record in the Qt GUI and the SDL GUI under Xvfb, replay headless; a NextZXOS session on the run's own card; a `--no-sdcard` session with no card at all; a network session against a loopback peer
- [ ] **B5.3** Oracle rows: RZX and `.jds` cross-checks (two recordings of one scripted, RTC-pinned input, with no typist); mutation rows, one per source, each of which must make the replay report divergence at the expected checkpoint; capture rows, one per source, replaying with the live source different or absent
- [ ] **B5.4** Windows rows under wine (paths, the container) and the copyright and privacy rows (the byte check finds no distribution run in any member, no host path in the manifest, no debug-level log line)

#### Sub-issue 13: documentation

- [ ] **B5.5** User guide page "Reporting a bug with a bundle", man page sections, developer guide chapter (the seams, the log, the card capture, the replay), FEATURES.md

### Phase B6: later (B6.3 by Q10; B6.1 and B6.2 by the planner's scheduling, for the owner to confirm; not filed: #492 is parked)

#### Sub-issue 14: keyframes (later: the planner's scheduling, for the owner to confirm; Q3 decided: local cache, and shipped keyframes for `--no-sdcard` only)

- [ ] **B6.1** Local keyframe cache on the replay side (never shipped): a second open of the same bundle jumps to the nearest keyframe
- [ ] **B6.2** Shipped keyframes for sessions with no firmware in memory (`--no-sdcard`), under the same content check as every member (RAM can hold files the stand-in served) and the keyframes tick (Q14); no firmware-excised keyframes (Q3)

#### Sub-issue 15: the browser build (later; Q10 decided: not in v1)

- [ ] **B6.3** Record and replay in a web page: the minimal card by `cardSha256`, `dirs` and recipe; JavaScript save and open

---

## Sub-issues and parallelization

Each sub-issue branches off `epic492-bug-report-bundle` and merges back into it. "Depends on"
means the branch cannot be finished before those are merged into the epic branch.

| Sub-issue | Tasks | Depends on | Can run in parallel with |
|---|---|---|---|
| 1 Seams and session log | B0.1, B0.2, B0.3 | none | 2, 3 |
| 2 Machine configuration | B0.4 | none | 1, 3, 4, 5, 6 |
| 3 Warm-start read set | B0.6 | none | 1, 2, 4, 5 |
| 4 Inputs and host actions | B1.1, B1.2, B1.3, B1.4 | 1 | 2, 3, 5, 6 |
| 5 In-frame pulls and streams | B1.5, B1.6, B1.7 | 1 | 2, 3, 4, 6 |
| 6 SD-card capture and the clean corpus | B1.8, B1.9, B1.10, B1.11 | 1, 3 (B1.9 needs neither) | 2, 4, 5 |
| 7 Container | B2.1, B2.2 | 2, 4, 5, 6 (the writer can start on 1's log) | none |
| 8 Replay engine | B3.1, B3.2, B3.3, B3.4 | 7 | 10, 11 |
| 9 Replay with debugger and rewind | B3.5, B3.6 | 8 | 10, 11, 12, 15 |
| 10 GUI menu and dialogs | B4.1, B4.2 | 7 (the replay dialog needs 8) | 8, 9, 11, 12, 14, 15 |
| 11 Preferences, keys and CLI | B4.3, B4.4, B4.5 | 7 (the replay options need 8) | 8, 9, 10, 14 |
| 12 Proof rows | B5.1, B5.2, B5.3, B5.4 | 8, 11 (B5.1, the probe program, needs nothing) | 9, 10, 14, 15 |
| 13 Documentation | B5.5 | 9, 10, 11, 12 | 14, 15 |
| 14 Keyframes (later) | B6.1, B6.2 | 9 | 10, 11, 12, 13, 15 |
| 15 Browser (later) | B6.3 | 8, 11 | 9, 10, 12, 13, 14 |

The parallel column lists no sub-issue that is a direct or indirect dependency or dependant of
the row. Work that can start early: B1.9 (the pristine sector-hash set) needs neither 1 nor 3;
the B5.1 probe program can be written on day one and is useful to every sub-issue's own rows;
the GUI and CLI parts that do not open a bundle (sub-issues 10 and 11) need only 7, and their
replay dialog and replay options are finished after 8. The Preferences panel and its
`config_keys.cpp` rows are one sub-issue (11), so they land in the same change.

**Critical path:** 1 -> 4 / 5 / 6 -> 7 -> 8 -> 9 -> 13. Sub-issues 2 and 3 run beside 1 and must
land before 7 and 6 respectively. The host runs at most two builds or test runs at a time, so
at most two branches are in active build and test together.

---

## 1. Sources and evidence

Read for this plan, at `main` 288f97668 (v1.3.27):

- The issue (#492, no comments) and the issues it leans on: #507 (`--sdcard-writes` with
  `--sdcard-file-add`), #508 (the browser's gzipped card cache), #509 (`dirs` on the minimal
  card), #458 (the minimal card), #505 (the second card), #396 (no whole-image digest).
- Code: `src/core/rzx_recorder.*`, `src/script/recorder.h`, `src/debug/inspect.h`
  (`InputState`), `src/debug/rewind_buffer.h`, `src/debug/debugger_inspect.cpp`
  (`note_mutation`), `src/core/warm_start_cache.h`, `src/core/emulator.cpp` (`init`,
  `prepare_warm_start`, `run_frame`'s render skip), `src/core/emulator_config.h`,
  `src/platform/config_keys.h`, `src/peripheral/sd_write_journal.h`, `src/peripheral/sd_card.h`,
  `src/core/sdcard_provisioner.h`, `src/core/minimal_card.h`, `src/core/sd_snapshot_identity.h`,
  `src/save/jns_container.h`, `src/save/zip_archive.h`, `src/core/fat32_image.h`,
  `src/peripheral/i2c.cpp`, `src/peripheral/esp_uart_adapter.h`,
  `src/esp01/include/esp01/esp_threaded.h`, `src/input/mouse.h`,
  `src/input/phantom_typist.h`, `src/platform/audio_pacing.h`,
  `src/platform/render_policy.h`, `tools/make-minimal-card/`; for revision 1 also
  `Emulator::warm_start_boot_config()` and the comment above its call in `emulator.cpp`,
  `test/00regression/scripts/warm-start-chans-func.sh` (rows E and G), `src/core/szx_loader.cpp`,
  `src/gui/main_window.cpp` (`on_cpu_speed`, `apply_startup_config`, the tape toggles),
  `src/esp01/src/esp_at.cpp` (`ATE1` echo), `src/input/gamepad_host.cpp`
  (`SDL_AttachVirtualJoystick`), `third_party/fatfs/ffconf.h` (`FF_FS_NORTC`).
- Documents: the man page (`doc/man/jnext.1.md`: SD-card options, `--rtc`, Recording and
  playback, `--record-script`, `--fast-boot`, EXIT STATUS), `doc/design/NEXT-SNAPSHOT-FORMAT.md`
  (sections 2, 8, 11), `test/bench/` baselines and `doc/design/TASK27-FINAL-OPTIMIZATION-REPORT.md`
  (headless speed).

Measured or read facts this plan rests on are cited where used. Nothing here was run on the
host: the 1-minute load was above the run's admission threshold (15.3 against 12) and no
measurement in this plan needs a fresh run to make a design decision. The numbers that do need
one (the recorder's overhead for Q2, the replay speed, the content check's cost) are tasks
with their own rows; the speed figures quoted in section 3 are historical (Task 27, July 2026)
until the replay-speed row re-measures them.

---

## 2. What exists today, and what each piece leaves open

| Piece | What it captures | What it leaves non-deterministic or unusable for a bundle |
|---|---|---|
| `.jns` snapshot | The whole machine at a frame boundary (G1), provenance, card identity (size, MBR, volume serial), ROM digests | On a booted Next it **contains NextZXOS and DivMMC ROM content in RAM** (N3): it cannot be shipped. Assumes the mounted card is in the state it was left (no content check since #396). |
| `.szx` / `.sna` / `.z80` | Classic-machine RAM and registers, no ROM | No Next state. Usable as an anchor on 48K, 128K, +3 and Pentagon only. |
| `--rzx-record` / `--rzx-play` | Snapshot plus the values the program read from ports, per instruction-count frame | **Refused on the Next** (man page: no snapshot an RZX can carry describes a Next and keeps the value log aligned). Values without ports, so no source tells you which input a value came from. |
| `--record-script` (`.jds`) | Keyboard matrix, extended keys, both joystick connectors, as level changes at frame edges; captures; a header with machine, program, RTC, card | **No mouse** (`InputState` has none), no RTC values, no network, no host files, no card content. A change made while paused mid-frame cannot be placed (a WARNING). Samples the matrix, so keys jnext types itself (a tape's `LOAD ""`) are recorded as if typed by hand. A cold boot restarts it. |
| Program loaders | The program file as read (the tape's digest of the bytes read, GH #402), loader options | Host paths. A tape or NEX from the distribution's own `/GAMES` is distribution content. |
| Saved configuration (`jnext.conf`, `config_keys.cpp`) | Every Preferences setting, one table | Mixes host settings (scale, CRT, gains, window) with machine ones. Many machine settings are CLI-only and live in `EmulatorConfig`, not in the file (`--rtc`, `--sdcard-writes`, `--esxdos-stub*`, `--no-sdcard`, `--nex-args`, `--inject*`, `--joy-uart-*`, `--magic-port`). |
| Rewind (`rewind_buffer.*`) | Full state per frame in a ring; a rewind restores the nearest slot and re-executes to the target | Proves jnext re-executes a frame identically from a restored state; that is the determinism this plan builds on. But the fast-forward runs with the ESP **inert** (`esp_uart_adapter.h`: a live network cannot be re-executed), and RTC reads during it read the live host clock again, so a rewind across ESP traffic or an RTC read does not land on the recorded timeline. |
| SD card, default mode | The image is opened read-write and guest writes go into it | The image the user has is the distribution image **plus every session's writes**, so no digest names it. |
| SD card, `--sdcard-writes` (#507) | A journal of the sectors the guest wrote (516 bytes each), bound to the image by size and mtime or by SHA-256; image opened read-only | The journal holds every write since it was created, read or not; it can hold copies of distribution files the guest copied. It is the right **replay-side** mechanism (section 5.5), not something to ship as it is. |
| Minimal card (#458, #509) | A 40 MiB card built byte-for-byte from the pinned distribution zip: `Pins` (`zip_sha256`, `image_sha256`, `card_sha256`, `recipe`), optional `dirs` | Deterministic by construction: exactly the "sha256 plus a recipe" the issue asks for. #508's gzipped Cache Storage is browser-only storage of the same card and changes nothing for a bundle. |
| Warm start | A recording of a real boot, keyed on card size and mtime (or digest), machine, format and procedure versions | Already reproducible across clocks: `warm_start_boot_config()` pins the recorder's RTC to 2026-07-10 08:55:00 (b93c03f79), and `warm-start-chans-func` row E proves three recordings under different clocks are byte-identical, row G that a warm-loaded program reads the user's clock. The recorder copies the rest of the live `config_` on purpose (audio gains and CPU speed included), so whether host-class fields change the recording is not yet proved (B0.6). It holds firmware: the bundle names it, never ships it. |
| RTC | `I2cRtc::snapshot_time()` latches `std::localtime(std::time(nullptr))` at every I2C START (`I2cRtc::start()`); `current_time()` answers the esxDOS `M_GETDATE` call | Host local time and time zone, read in the middle of a frame. |
| `--rtc` | A frozen clock | A frozen clock does not advance, so it changes what the reporter sees. Fine as a pin, not as the default for recording. |
| ESP-01 | AT engine; the threaded wrapper releases bytes to the guest at the wire's cadence | Which bytes exist at a given cycle depends on the network and on the worker thread (`esp_threaded.h`). SNTP and ping use host time. Not serialised (simplification 5 of the ESP design). |
| Mouse, joysticks | Host deltas and buttons into `KempstonMouse`; host pads into the connectors | Arrive between frames from host events: non-deterministic in time, deterministic once applied. |
| GUI menus and Preferences | `MainWindow::on_cpu_speed` writes NR 0x07 directly; `apply_startup_config(..., program_cpu_speed)` does too; the tape fast-load and auto-play toggles call `set_fast_load` / `set_tape_auto_play` | Guest-visible machine writes made by the host at a moment the host chooses, outside `note_mutation()` and outside any CLI option. |
| `.szx` loader | after the `ZXST` file header, it parses `Z80R`, `SPCR`, `RAMP` (zlib-compressed when flag bit 0 is set) and `AY`, and ignores `SCLD` on purpose (GH #323) | Skips every other chunk silently (`szx_loader.cpp`, "skipping unknown chunk"), so a file can carry a custom-ROM or interface-EEPROM chunk jnext never reads. |

Two facts make the rest possible:

- **Emulation is single-threaded and host input lands between instructions.** The only threads
  are the Pi audio reader, the QEMU reapers, the ESP worker and a download dialog
  (`git grep std::thread`). The ESP worker never writes into the guest: its bytes reach the RX
  FIFO from the caller's thread inside `tick()` (`esp_threaded.h`). So every way host state
  enters the machine happens at an instruction boundary, at a cycle that can be recorded.
- **There is no guest-visible randomness.** The only RNG in `src/` names a scratch directory
  (`program_archive.cpp:159`). A hard reset clears RAM (`Emulator::init` with
  `preserve_memory == false` calls `ram_.reset()`), so a power-on is a clean starting point given
  the ROMs and the cards.

---

## 3. Architecture

A bundle is **an anchor, a log and the media to rebuild both**.

```
  anchor (rebuildable by the developer)      log (everything the host put in after it)
  ------------------------------------      ---------------------------------------------
  power-on | program load | warm start      events      (cycle, kind, args)      inputs, actions, debugger edits
  | classic snapshot file                    queues      (cycle, value)           RTC reads, esxDOS host calls
  + machine configuration (B0.4)             streams     (cycle, bytes)           UART RX: ESP, NextPi, joy-UART
  + cards: base identity + input sectors     checkpoints (frame, cycle, digests)  every N frames
```

**The anchor** is a machine state the developer's jnext can rebuild without being given it:

| Anchor | Rebuilt from | Contains firmware? | Shipped as |
|---|---|---|---|
| `power-on` | configuration + the cards' sectors (ROMs come from the card) + the embedded `nextboot.rom` | Built locally | Identity only |
| `load` | power-on, or the warm start for a `.nex` on the Next, + the program file and its loader options | Built locally | Identity + the program (Q4) |
| `warm-start` | a fresh warm-start recording on the replay card (the recorder's RTC is already pinned), checked against its digest (B0.6) | Built locally | Identity + digest |
| `snapshot` | power-on + a `.sna` / `.szx` / `.z80` file (classic machines) | A `.szx` can (custom-ROM and interface chunks) | `.sna` and `.z80`: the file, under the content check; `.szx`: re-serialised with only the chunks jnext reads, then checked |

A `.jns` is never an anchor in v1 (Q3): on any machine it can hold DivMMC firmware, and on a
booted Next it holds NextZXOS. Loading one while recording ends the segment, and the recorder
says so.

**The log** is a timeline of everything the host put into the machine, each entry stamped with
the master-clock cycle (28 MHz, counted from the anchor) at which it took effect, and the frame
it fell in. It is captured **at the seams where host state enters the machine** (B0.1), not by
sampling machine state: the phantom typist and the auto-type queue are machine-internal and
re-run by themselves in a replay, so sampling the keyboard matrix (the `.jds` recorder's way)
would apply their keys twice. Three shapes, by how the source behaves:

- **Events**, pushed by the host between instructions: key, joystick, mouse, a button, a tape
  deck action, an SD insert, a POK, a debugger edit. Replay applies each at its cycle.
- **Value queues**, pulled by the guest in the middle of a frame: an RTC read, an esxDOS
  stand-in call. The replay answers the N-th pull with the N-th recorded value, and checks that
  it happens at the recorded cycle.
- **Byte streams**, pushed into a FIFO at an exact cadence: the UART RX side. The replay
  delivers each byte at its recorded cycle. The TX side is the guest's own output, so it is
  kept as a running digest only: a replay that sends something else has diverged.

**Checkpoints** every N frames (default 50) carry three digests: the *observable* digest (Z80
registers and all 2 MiB of RAM: concepts every jnext version shares), the *framebuffer* digest
(the composed picture), and the *full* digest (the `save_state` stream, comparable only between
identical state formats). zlib's CRC-32 is already linked and costs about a millisecond per
2 MiB; the end of the log also carries a SHA-256 of the full state.

**The replay** rebuilds the anchor, turns every live host injector off (keyboard, pads, mouse,
scripts, `--delayed-*`, POK trainers, the ESP, the NextPi, the joy-UART), replaces each pulled
source with a replay device that answers from the log, injects the events at their cycles, and
checks the checkpoints. It runs **normal frames**, never the rewind's `replay_mode_`, which
skips the render and with it the sprite side-effect pass (`run_frame`: the `if (!replay_mode_)`
block around `render_framebuffer_now()` and `run_sprite_side_effects()`); a replay that skipped
it would lose port 0x303B's collision flags.

**Why no memory snapshot, and what it costs.** The replay of a long session takes time: the Next
at 28 MHz ran 284 frames/s headless in the Task 27 final report (5.7x real time), the 48K
371 frames/s in the July baseline (historical figures; the replay-speed row of B5.2 re-measures
them). A ten-minute NextZXOS session replays in under two minutes; an
hour in about ten. That is the price of rule 1. Two things keep it low: a session's segment
starts again at every hard reset and program load (B1.4), so most bundles are short; and the
replay side keeps local keyframes, so a second open of the same bundle is quick (B6.1, never
shipped).

---

## 4. Contents

The file is a ZIP (the `.jns` container's writer and reader, `src/save/zip_archive.*`), named
**`<name>.YYYYMMDDhhmm.jnext-bug.zip`**. The owner decided the pattern and that `YYYYMMDDhhmm` is
the current date and time at save (Q5). The details that follow are **the planner's
interpretation, for the owner to confirm** when the work resumes, not owner decisions: `<name>` is
the loaded program's base name without its extension (`jnext` when no program is loaded), reduced
to `A-Z a-z 0-9 . _ -` (anything else becomes `_`); the time is **local** time (the manifest's
`created` stays UTC); and a second save in the same minute adds `-2`, `-3`, ... after the time
(`game.202611200551-2.jnext-bug.zip`) instead of overwriting. The `.zip` ending is kept because
GitHub accepts `.zip` attachments and refuses unknown extensions. A bundle is recognised by its
`manifest.json`, not by the name or the extension. `--load` of a `.zip` today means a
program archive (`program_archive.cpp`), so the bundle check runs first.

| Member | Format | What |
|---|---|---|
| `manifest.json` | JSON | `kind: "jnext-bug-report"`, `format_version`; producer (jnext version, `git describe`, platform, build type, PGO or not); `created` (UTC); machine; the state-format versions (warm-start `kFormatVersion` and `kRecordingVersion`, the `.jns` `state_model_revision`, the state-stream length); `nextboot.rom` SHA-256; ROM digests (`jns::` ROM identity); anchor; segment length in frames and cycles; checkpoint interval; card identities (section 5); the warnings the recorder raised; the privacy summary (section 9) |
| `config.json` | JSON | The machine class of `EmulatorConfig`, field by name (B0.4); paths reduced to bundle members |
| `program/<name>` | bytes | The program as the loader read it, unless excluded (Q4) or found in the distribution (then a reference: distribution, path, SHA-256); under the content check. The extended-NEX read overlay is re-derived from it on replay, never shipped separately |
| `events.jsonl` | JSON lines | One event per line: `{"c": cycle, "f": frame, "k": kind, ...}`; no content payloads (a memory write names its offset and length in `debugger/writes.bin`) |
| `queues/<source>.jsonl` | JSON lines | `rtc`, `hostfs` (call, arguments, result; file bytes in `queues/hostfs-data.bin`, one record per read, under the content check) |
| `debugger/writes.bin` | bytes | the payload of every debugger memory write (loads, DZRP and GDB uploads, pokes), back to back; each checked as one contiguous block |
| `streams/<uart>.bin` | binary | RX records: cycle (u64 LE), byte; a header naming the UART and the device (the content check runs on the payload sequence, not on these records) |
| `checkpoints.bin` | binary | frame (u32), cycle (u64), observable, framebuffer and full CRC-32s, TX digests |
| `card0/sectors.bin`, `card1/...` | binary | The input sectors shipped as bytes, as raw 512-byte blocks back to back (no per-record header, so a sector sits at a multiple of 512); `card0/index.json`: the base identity, the input-sector set with hashes, which are bytes (by position in `sectors.bin`), references (pristine sector number) or hashes only, and the FAT path of each |
| `screen.png` | PNG | The picture at the save point, for triage |
| `notes.txt` | text | What the reporter wrote |
| `log.txt` | text | Optional, off by default, info level and above only (debug and trace lines can carry instruction traces and byte dumps of firmware), redacted (section 9), under the content check |

Low-rate members are JSON so a developer can read a bundle with `unzip` and `jq` (the `.jns`
goal G5); high-rate members are binary with a documented layout. A member that is not declared
in the manifest, a declared one that is missing, or a CRC mismatch refuses the bundle.

---

## 5. The SD card, and the content guarantee

### 5.1 What has to be captured

For a replay to see the same bytes, only one set of sectors matters: the **input sectors**,
those whose first access after the anchor is a read. A sector the session wrote before reading
is the session's own output, which the replay regenerates. A sector never read cannot influence
the guest. So the card is captured as (B1.8):

- the input-sector set, each with the SHA-256 of the 512 bytes the guest read;
- an **undo log**: when an input sector is first written, its pre-write bytes (the bytes the
  guest read), so they can be recovered at save time in the default read-write mode, where the
  image itself has moved on. A sector whose first access is a write needs no pre-image. The undo
  log lives in memory only, counts toward the memory cap (Q13), and is never persisted or
  shipped: it only feeds the classification below;
- nothing else. The tracking is a bit per sector (256 KiB for a 1 GiB card) plus a hash per input
  sector; a NextZXOS boot reads a few thousand sectors.

### 5.2 How the card is named without being shipped

The base is identified as one of:

| Base | Identity | The replay base is |
|---|---|---|
| The default card (`cspect-next-1gb-fixed.img`) | distribution zip name, URL, `zip_sha256`, `image_sha256` of the raw `cspect-next-1gb.img`, `kFixedImageRecipe`, and the SHA-256 of the pristine fixed image | the **pristine rebuild**: the fixed image remade from the raw image by its recipe, at its own path (5.5) |
| The minimal card (#458) | `card_sha256`, `recipe`, the canonical `dirs` list (#509) | the builder's output (`make-minimal-card --dirs LIST`, or the same builder in jnext), at its own path |
| An image with `--sdcard-sha256` or a cached digest | its SHA-256 and size | the developer's own copy (`--bug-report-card FILE`), opened read-only |
| Anything else (an **unknown base**) | SHA-256 and size, computed at save time (about 1 s per GB, with progress) | the developer's own copy, or nothing (5.4) |

The developer's **working** default card is never the replay base: it carries the developer's
own sessions' writes, so its FAT, directories and settings differ from the reporter's.

### 5.3 What goes in, and what never does

**What a block check can and cannot prove.** Deduplicating whole 512-byte sectors against the
pristine image only proves that no *identical* distribution sector is shipped. It does not see
a distribution file edited by one byte, the last sector of a copied file whose slack past EOF
holds other bytes, content shifted by an insertion, or a copy at an unaligned offset in another
member. Revision 0 of this plan claimed more than that; the mechanism below replaces it, and
the end of this section states what it still cannot see.

#### 5.3.1 The distribution index

Built once per distribution (B1.9) and cached under `~/.jnext/bug-report/index/<image sha256>/`
(hashes only, not invertible, so not an image; never shipped, never committed):

- the **pristine sector-hash set**, every sector of the pristine image, used for references
  only. On the save side it is hashed from a rebuild of the pristine image made from the raw
  image the provisioner keeps in `~/.jnext/sdcard/`: the cached replay base when there is one,
  otherwise a per-run scratch copy deleted as soon as it is hashed (the image rule allows per-run
  scratch copies);
- the **window index**: a 64-bit hash of every 64-byte window starting at a multiple of 32 bytes
  in the *contents* of every distribution file (the files' bytes, not the image's FAT or
  directory sectors: about 160 MB of content, some 5 million entries, about 40 MiB). A
  distribution file in a format the check decodes (`.z80`, `.szx`, `.zip`, TAP and TZX data
  blocks) is indexed twice, as stored and as decoded, so a decoded member meets a decoded
  original. One rolling hash function, defined once in `src/bundle/`, builds the index and scans
  the members, so the two sides cannot disagree;
- the **file table**: path, size and SHA-256 of every distribution file, and the system paths
  (the distribution's top-level files and directories, read from the distribution itself:
  `/TBBLUE.FW`, `/MACHINES`, `/NEXTZXOS`, `/SYS`, `/DOT`, `/RPi` and the rest).

**Padding exclusion (E1), and only near-padding.** Padding is not expression, and matching it
would make every directory sector, FAT sector and zero-padded file look like a distribution copy
(round 2's finding). So exactly one criterion removes windows from the index, and it is chosen so
that it provably removes nothing but fill:

- **E1, short period with at most two exceptions:** a 64-byte window that equals a repetition of
  a pattern of period 4 or less (`w[i] == P[i mod p]` for some p in 1..4) in every position but at
  most two: 0x00, 0xFF and 0xE5 fills, `AA 55` and other short fills, and such a fill with one or
  two stray bytes (a length byte in a zeroed TAP header, a sparse table of a program's own). Such
  a window carries at most 4 pattern bytes and two more. A 512-byte sector is **trivial** when
  every aligned window in it is excluded by E1.

Two exceptions is the most the proof below supports, and it is chosen for that reason as well as
for the measurements: revision 3 allowed none, revision 4 one (every TAP false positive was a
fill with exactly one stray byte), and revision 5 two, because one licence-clean program
(`test07joystick`, a table of its own; see below) shares a window with exactly two stray bytes
with `TBBLUE.TBU`. Revision 2 had a second criterion, E2 (a window in more than 16 distribution
files counted as "boilerplate"), and a looser E1 (fewer than 8 distinct values). Both stay gone:
round 3 measured that the distribution repeats the 48K BASIC ROM inside some two dozen other ROM
files, so E2 dropped 413 of the 511 windows of `48.rom` and 177 of `enNextZX.rom` (firmware, not
boilerplate), and "fewer than 8 distinct values" also matches 1-bit artwork, fonts and sparse
tables, which are expression.

Excluded windows are not in the index; a trivial sector never counts as "a distribution sector"
in the classification below (as a reference it is still fine: a reference ships nothing).

**The floor, restated.** The member side is scanned at every byte offset, so: *any run of 95 bytes
or more that a checked byte sequence shares with a distribution file is found, unless every
aligned 64-byte window inside that run is excluded by E1 or by the clean corpus (below).* For
E1 alone, a shared run that escapes carries little:

- two consecutive aligned windows overlap in 32 bytes, holding at most 4 exception positions
  (two per window), so the overlap has an exception-free stretch of at least ceil(28 / 5) = 6
  bytes, which is at least p + q - gcd(p, q) (at most 6 for p, q <= 4) and at least one full
  cycle of each pattern; by Fine and Wilf both windows then follow the same phase-aligned pattern
  of period gcd(p, q), and by induction every aligned window of the run follows one pattern of
  period at most 4. With three exceptions the stretch could be 4 bytes, and the step fails: that
  is why E1 stops at two;
- the run is therefore that pattern, plus fewer than 32 bytes before the first aligned window
  and fewer than 32 after the last (62 bytes), plus at most two exception bytes per aligned
  window, and no 32-byte block holds more than two (the window containing it allows only two).

**An escaping run of L bytes holds at most 62 + 2 x (floor((L - 64) / 32) + 1) bytes that are
not one short repeating fill, plus the fill's pattern of at most 4 bytes**: two stray bytes in
every 32 of the run's interior, at most: at most two arbitrary bytes per 32. Sparse tables of
that density exist (`test07joystick`'s `_row0`, below), and beyond those bytes they are fill.
The floor bounds **verbatim** copies of the **indexed** distribution only; 5.3.5 says what that
leaves.

**Coverage, measured** (rounds 3 to 5 and these revisions, 2026-11-20, on the local default card
of `sn-emulator-26.11`: the 250 files under `/TBBLUE.FW`, `/TBBLUE.TBU`, `/machines`,
`/nextzxos`, `/sys` and `/dot`, 9.2 MB, read through `mtype` into memory only; nothing extracted
to disk, the scripts deleted afterwards):

| File | Windows (stride 32) | Indexed, revision 2 (E1 < 8 values, E2 > 16 files) | Indexed, revision 3 (E1 period <= 4) | Indexed, revision 5 (E1 with up to two exceptions) |
|---|---|---|---|---|
| all 250 files | 287 300 | 142 603 (49.6 %) | 182 331 (63.5 %) | 174 685 (60.8 %) |
| `machines/next/48.rom` | 511 | 62 (12.1 %) | 476 (93.2 %); the 35 excluded are the 0xFF fill at 0x3880-0x3CFF | 476 (93.2 %) |
| `machines/next/128.rom` | 1023 | 607 | 1021 (99.8 %) | 1021 (99.8 %) |
| `machines/next/plus3.rom` | 2047 | 1359 | 1788 (87.3 %) | 1781 (87.0 %) |
| `machines/next/enNextZX.rom` (NextZXOS) | 2047 | 1606 | 1799 (87.9 %) | 1798 (87.8 %) |
| `machines/next/enNxtmmc.rom` (DivMMC) | 255 | 254 | 254 (99.6 %) | 254 (99.6 %) |
| `machines/next/enNextMf.rom` (Multiface) | 255 | 255 | 255 (100 %) | 255 (100 %) |
| `TBBLUE.FW` | 19 871 | 16 908 | 19 343 (97.3 %) | 19 317 (97.2 %) |
| The 48K character set (0x3D00-0x3FFF) | 23 | 3 | 23 (100 %) | 23 (100 %) |

(Revision 4's one-exception E1 measured 179 023, 62.3 %, with the same ROM counts but `plus3.rom`
1783 and `TBBLUE.FW` 19 329.)

The largest number of files sharing one window was 155 (E2 would have excluded it), which is the
measured reason E2 cannot stay. Under every criterion of revisions 3 to 5, every one of the 250
files has at least one indexed window, so a verbatim copy of any of them is caught; five are
smaller than 95 bytes and are covered by the classification instead (system path, size and
name), not by the byte check. That cover holds under the system paths, and for a copy elsewhere
only while the copy keeps its name and size: a distribution file under 95 bytes, renamed and
copied outside the system paths, is a user file the byte check cannot see. It is at most 94
bytes, and on every base it ships only when ticked (Q14). A system-path file of 95 bytes or more
made entirely of E1-excluded windows would have no indexed window either; none exists in 26.11
(measured), and BRB-FILE-40 names any that appears and leaves it to the classification, like the
short files. Low indexed fractions (`TBBLUE.TBU` about half, some dot commands near 30-40 %, the
`/nextzxos/*.z80` files below 4 % as stored) are the files' own fills and zero padding; the `.z80`
files are also indexed decoded.

**The NEX header is checked by layout, not scanned.** The first 512 bytes of a NEX are the header
the NEX format defines: magic and version, counts, flags, the bank bitmap (one 0/1 byte per bank),
addresses, the loading-screen and copper flags, and reserved bytes. The writer checks it field by
field against the format (`nex_loader.cpp` reads the same fields; the format page is in
`doc/REFERENCES.md`): every field within the values the format defines, every reserved byte zero.
A header that passes holds nothing but those fields, so no window lying wholly inside it is
scanned; **every byte after the 512-byte header is scanned**: the palette, the loading screens,
the copper block, any gap before `banks_offset` (V1.3), the banks and the extended payload after
them. A header that fails is scanned like any other bytes. Residual: the header's free-valued
fields (addresses, counts, flags, the border colour, the bank bitmap's 0/1 bytes) carry a few tens
of bytes the check does not read; they cannot hold a 95-byte run. This replaces matching the header against distribution NEX headers, which revisions
3 and 4 did, and which two licence-clean programs failed (below).

**False positives on real user programs, measured (rounds 4 and 5, then these revisions).** The
same check run the other way, against licence-clean programs, refused almost all of them. Index:
every file of the local default card (1393 files, 121.3 MB, about 2.5 million indexed windows; it
is the working card, so a few files may not be distribution files). Corpus, all built with z88dk:
the tracked programs under `demo/` as `.nex` and their `.tap` builds, and the committed dapr-nexlib
test programs (`test/00regression/nex/test*.nex`): 62 files, but only **43 distinct contents
(SHA-256) from 27 distinct sources**, because the dapr programs exist two or three times byte for
byte (`test/00regression/nex/`, `demo/dapr-nexlib+tests/test00alltests/` and each test's
`build/`) and a demo's `.nex` and `.tap` are builds of one source. A 63rd file, `demo/esxdos_probe`,
is an untracked artefact of 2026-05-15 with no source in the repository; it is reported, not
counted. Round 5 showed that leaving out only the program's own *file* lets its byte-identical twin
clean it; every count below leaves out **every build of the program's source** (same stem, every
output type, every copy), which is what a reporter's program meets: it has no twin in the corpus.

| Index (all with the clean corpus, leave-one-source-out) | Refused, of 27 sources (43 contents) |
|---|---|
| revision 3's index, no clean corpus | all, by file (38 of 38 demo `.nex`, 14 of 15 `.tap`, 9 of 9 dapr) |
| revision 4: E1 with one exception + clean corpus (by file: 0, inflated by twins) | 3: `test01l2empty`, `test02layer2`, `test07joystick` |
| + NEX header checked by layout, not scanned | 1: `test07joystick` |
| revision 5: + E1 with up to two exceptions | **0** |

The three revision-4 survivors, one window each, identified through the dapr `.map` files:

- `test01l2empty`: its NEX header from byte 0 (a window with 14 exceptions), matching the header of
  `/demos/tilemap/tm.nex`; closed by the layout check of the header;
- `test02layer2`: its NEX header at byte 28, inside the bank bitmap (4 exceptions), matching the
  header of `/apps/audio/NextSID/NextSIDplayer.nex`; closed the same way;
- `test07joystick`: bank 2, `$8FF1`, `_row0+6` (section `data_compiler` of its `main.c`: the
  program's own table), a window with exactly two stray bytes in a fill, matching `TBBLUE.TBU` at
  offset 140 224; closed by E1's second exception.

What the shared runs of the other programs are, mapped the same way (`test01l2empty`,
`test05print`): a typical 16 KiB demo had 42 hit offsets in 4 runs, the longest 126 bytes, 31-34
of them against system-path files, and they were the NEX header z88dk's appmake writes (now
checked by layout); **z88dk's newlib run-time data**, `__thrd_id` (section `data_threads`) and
`__stdio_block` (section `bss_fcntl`) of `zxn_crt.asm.m4`, matched in `TBBLUE.TBU`,
`/dot/NextPiUI`, `/apps/audio/NextDAW-demo` and `/apps/audio/NXModPlayer`: distribution programs
built with the same z88dk run-time at the same default origin; coinciding user data (a tileset
palette); and, in the TAPs, fills with one stray byte. The z88dk 2.4 library archives themselves
(32 Spectrum-relevant `.lib` files, 31.6 MB, scanned at every offset) share only 568 windows with
the index and explain none of the refusals: object files hold code with unresolved addresses and
no run-time data, so the clean corpus has to be **linked** builds.

**The remedy: a clean corpus of linked z88dk builds (Q18).** An index window that also occurs, at
any offset, in a file of the **clean corpus** is dropped from the index. The clean corpus is a set
of binaries built by the pinned z88dk release (2.4 today; a release bump adds its builds, older
ones stay) from **jnext's own licence-clean sources** only:

- the tracked `demo/` programs, as `.nex`, `.tap` and `.bin`;
- **run-time probes**: a minimal program for each target (`+zxn`, `+zx`), C library (`sdcc_ix`,
  `sdcc_iy`, `sccz80` with newlib, and the classic library), startup and output type (`nex`,
  `tap`, `dot`) z88dk offers, at the default origin;
- **library sweeps**: generated programs that call every public **function** of each of those
  libraries at least once, split over as many programs as needed to fit. They exist for library
  code only one program uses, which a leave-one-source-out over 27 sources cannot show.

**Code only: no third-party data, ever.** z88dk's libraries carry data of other authors, and some
of it is the very content rule 1 protects. Measured (round 5, and again here): z88dk 2.4's
`libsrc/_DEVELOPMENT/font/font_8x8/font_8x8_zx_system.bin`, the data behind `_font_8x8_zx_system`,
is 768 bytes **byte-identical to `48.rom[0x3D00:0x4000]`**, the 48K ROM character set; its windows
occur in 24 of the distribution's ROM files, `enNextZX.rom` and `48.rom` included (23 windows each).
Beside it sit the system fonts of other machines' ROMs (`_font_8x8_bbc_system`,
`_font_8x8_c64_system`, `_font_8x8_cpc_system`, `_font_8x8_msx_system`, `_font_8x8_sam_system`,
`_font_8x8_vga_rom`, their `_font_8x10_*` siblings and their `_zx7_` compressed forms). A sweep that
linked them would put the 48K character set into the clean corpus, drop its windows from the index
(readmitting a piece of the card, and deciding Q17 behind the owner's back), and commit and package
proprietary fonts. So:

- the probes and sweeps link **code and z88dk's own run-time data only**. Every library data object
  of third-party origin is excluded **by name**: every symbol defined under `libsrc/_DEVELOPMENT/font/`
  and the classic library's font sources (`_font_*`, `_zx7_font_*`, the FZX fonts), and any other
  ROM-derived or vendor table B1.11 finds; the sweep generator calls functions, never takes the
  address of a data symbol, and fails the build if a linked binary's map names an excluded symbol;
- **B1.11 records a licence check of every file linked into every clean binary** (z88dk's LICENSE
  covers program output "unless otherwise stated in the appropriate file", so each source file's own
  terms are read), and writes the result into this design before anything is committed. A file whose
  terms are not GPLv3-compatible, or not clearly z88dk's own, is excluded by name like the fonts;
- **committing or packaging proprietary data of any kind is forbidden**: by PULL-REQUEST-PROTOCOL.md
  rule 3 ("Any test fixture is license-compatible with the project (GPLv3). No proprietary
  content.") and by the owner's absolute rule (no NextZXOS image, firmware or distribution content,
  in any form). The clean corpus is fixture-class content and both rules bind it;
- BRB-FILE-42 proves the result: the clean corpus shares no 64-byte window with any
  `/machines/**/*.rom` file, and none with the 48K character set.

The clean binaries are built by a maintainer target (`make bug-report-clean-corpus`), committed
(z88dk output of our own sources, like the committed test NEX files), shipped in the packages, and
intersected with the index on the reporter's machine when the index is built. No hash and no byte
of the distribution is committed; Q18 has the alternatives.

**What the remedy ships, and the line it draws.** A window is dropped only if those exact 64 bytes
occur in a clean build: code z88dk generated from jnext's own sources, z88dk's own library code and
z88dk's own run-time data, never third-party data (above). Such a window was in the index because
the same bytes also occur on the distribution card, inside a distribution program that links the
same z88dk run-time (`TBBLUE.TBU`, the dot commands, applications). So the remedy does ship byte
strings that also occur on the card. Each is z88dk's own code or run-time data, of z88dk's
authorship and licence, which the reporter's own program contains because the reporter linked it;
none is NextZXOS's or another vendor's. That is the line the remedy draws, and the owner accepted it
(Q18, option (a)); option (d) was the one that ships nothing that also occurs on the card.

**Drop counts, measured** (E1 with two exceptions; clean corpus = today's demo and dapr builds,
**before the probes and sweeps exist**: these counts are re-measured when B1.11 builds them, and the
re-measured counts replace them in this table and in BRB-FILE-40's pins). Of the 250 system-path
files, 234 lose no window. The others:

| File | Windows dropped |
|---|---|
| every `/machines/**/*.rom` | 0 |
| `/TBBLUE.FW` | 1 |
| `/TBBLUE.TBU` | 19 |
| `/dot/NextPiUI` | 11 |
| `/dot/zed` | 4 |
| `/dot/DZX7`, `/dot/NDAWPLAY` | 3 each |
| `/dot/CONFIG`, `/dot/MV`, `/dot/NBNGET`, `/dot/PIGET`, `/dot/SPUI` | 2 each |
| `/dot/FIND`, `/dot/LS`, `/dot/NXMOD`, `/dot/PIPUT`, `/dot/QE` | 1 each |

(Revision 4 quoted 307 for `TBBLUE.TBU` and 12 for `/dot/NextPiUI` with one-exception E1: most of
those windows are fills with two stray bytes, which E1 now excludes on its own.)

**Firmware that links the same run-time.** `TBBLUE.TBU`, the dot commands and several applications
are z88dk programs, so their z88dk run-time and library portions lose those windows. A copy of
exactly those portions passes and ships z88dk's code; the firmware's own code keeps its windows
(every file above keeps almost all of them). The bound changes accordingly: a shared run escapes the
check only if every aligned window in it is E1-excluded or occurs in a clean build. Each such 64-byte
piece exists in licence-clean code; it is not proved that the whole run occurs contiguously in one
clean build, and the design does not claim it.

**What the remedy does not cover.** A program built with a z88dk version or library the clean
corpus lacks, or with another toolchain that shares run-time with distribution programs, can still
be refused; the refusal names the member, and the program then travels as a SHA-256 reference (the
developer asks for it). Extending the clean corpus is the remedy, and BRB-FILE-41 shows when it is
needed.

#### 5.3.2 The rule: the check sees content as the guest saw it

**Every byte of content reaches the check in the order its source produced it, uncompressed and
unframed.** The check never runs on a member's storage layout (records, framing, JSON, a
compressed inner format), because the floor's proof needs the shared run to be contiguous in the
scanned bytes. Per source:

| Content | Reconstructed for the check as | Stored in the bundle as |
|---|---|---|
| Card sectors | each shipped sector's 512 bytes, slack included; and each user file's shipped sectors concatenated in file order | `card0/sectors.bin`, raw 512-byte blocks |
| UART RX | the RX byte sequence of each UART, payload only, in delivery order | `streams/<uart>.bin`, (cycle, byte) records |
| Stand-in host files | each served host file rebuilt from all its reads, by file and offset, merged (a NextZXOS file read in 32-byte `F_READ`s is checked as the whole file) | `queues/hostfs-data.bin`, one record per read |
| Debugger memory writes | each write's payload as one contiguous block (every memory verb, a DZRP `CMD_WRITE_MEM` upload included, not only "loads"); `events.jsonl` keeps only the verb, the address, the length and the offset into the payload member | `debugger/writes.bin` |
| The program | decoded by its loader's format: a `.zip` program archive inflated member by member; a `.z80` run-length expanded (a v1 file as one block for the whole 48K, `decompress_v1`; v2 and v3 page by page); a `.szx` re-serialised with its `RAMP` pages uncompressed (flag bit 0 clear) and checked as pages; TAP blocks and TZX standard, turbo and pure-data blocks as their payload bytes (the blocks with no decoder are the next row); NEX: the 512-byte header checked by layout and not scanned (5.3.1), every byte after it (palette, loading screens, copper, any gap before `banks_offset`, banks, extended payload) as stored; `.sna` as stored | the file, re-serialised for `.szx` and `.z80` (5.3.4) |
| Programs in formats with no decoder | WAV, CSW, TZX direct-recording and generalised-data blocks encode bytes as pulses, which the check cannot see | never shipped: a SHA-256 reference only |
| Keyframes (B6.2) | every RAM page | the keyframe member |
| `log.txt`, `notes.txt`, `config.json` | their text | as is |

An archive in the program is shipped only if every member passes; a member in a format with no
decoder makes the whole archive a reference. `screen.png` is exempt (a rendering of the screen,
not content bytes). RTC values, checkpoints and the card index carry no content.

#### 5.3.3 Classification of card sectors

Every input sector is attributed through the partition table and the FAT (`fat32_parse_dir` gives
the clusters). One class per sector, applied file by file where a file is involved:

| Class | Rule | Shipped as |
|---|---|---|
| Pristine | the sector's bytes are in the pristine sector-hash set (any sector number) | a reference |
| System | the file is under a system path, on **every** base | hashes only, never bytes (Q15 for settings files) |
| Distribution-derived | a non-trivial sector of the file equals a sector of a distribution file's contents, or its size and name match a distribution file, or it is an archive or image format (`.zip`, `.gz`, `.tar`, `.img`, `.dsk`, `.rom`), or a guest-saved snapshot (`.snx`, `.sna`, `.szx`, `.z80`: made by the NMI menu or a program, they hold firmware-era RAM) | hashes only |
| Free space | a sector in no cluster chain (free clusters can hold a deleted older NextZXOS) | a reference when pristine, otherwise hashes only, on every base |
| Non-filesystem | the MBR, the partition's reserved area (boot sector, the backup boot sector, the **backup FSInfo** beside it in the backup boot area, typically partition sector 7, and the rest of the reserved sectors), the gap before the partition, anything outside it | a reference when pristine, otherwise hashes only, on every base; one exception below |
| FSInfo | the partition's primary FSInfo sector (the one the boot sector's `BPB_FSInfo` names), which changes with every write (free count, next free) | bytes, only if its layout checks: the three signatures in place and every reserved byte zero, so it can hold nothing but two counts; otherwise hashes only. The backup FSInfo stays Non-filesystem |
| Structure | FAT sectors, directory sectors | bytes, after the byte check |
| User file | none of the above | bytes, after the byte check, and only as Q14 allows (by default: only files the reporter ticks) |

**Slack.** A user file's last sector is checked whole. When a hit lies entirely in the slack past
EOF, the slack is shipped zeroed (a zeroed slack is trivial, so it cannot itself hit) and the
sector's read check is masked to the bytes before EOF; the replay may then diverge if the guest
copies the slack, and the digests say so. Any other hit makes the file hashes only.

**A hit in a non-card member** refuses that member, and the review names it (a host file served
by the stand-in that is a NextZXOS file, a program that embeds a ROM, an RX stream that downloaded
a distribution file, a debugger upload).

**The ROM character set.** The distribution's `/MACHINES/NEXT/48.rom` and `128.rom` are indexed,
and classic programs often copy the ROM character set (768 bytes at 0x3D00) into their own code or
RAM. Under E1 all 23 of its aligned windows are indexed (measured above), so any copy of 95 bytes
or more of it is found, whole or partial: such a program, a classic snapshot of it and its
keyframes are refused, the program becomes a SHA-256 reference and the developer needs it from the
reporter. That is the correct outcome under the rule, by the criterion and not by accident, and it
has a real cost on classic-machine bundles; the owner decided to keep the character set indexed
(Q17).

#### 5.3.4 Members that are re-derived instead of shipped, and the no-index case

The extended-NEX read overlay serves the program file's own bytes to the guest through the SD
device (`SdCardDevice::set_read_overlay`, set in `load_nex()`); it is rebuilt on replay from the
program member or reference. A classic `.szx` anchor is re-serialised with only the chunks the
loader parses (`Z80R`, `SPCR`, `RAMP`, `AY`, after the `ZXST` file header); `SCLD` is dropped too,
because the loader deliberately ignores it (Timex and SE snapshots are refused, GH #323), as is
any custom-ROM or interface chunk, and each drop is recorded. A `.z80` anchor is re-serialised
the same way: a v2 or v3 file keeps only pages 3-10, the RAM banks the loader applies
(`z80_loader.h`: "Pages 0-2 (ROM) and 11 (Multiface ROM) are skipped", `if (page_num < 3 ||
page_num > 10) continue;`), so the 48K ROM, the Interface 1 / DISCiPLE / +D ROM, the 128K ROM and
a Multiface ROM another emulator stored never ship; a v1 file has no ROM pages. Each drop is
recorded.

**With no index** (a `--no-sdcard` user who never provisioned a card, a deleted raw image), the
writer offers the provisioner's download. Without the index it writes only what carries no
content: `manifest.json`, `config.json`, `notes.txt`, `events.jsonl` (no payloads, now that they
live in `debugger/writes.bin`), `queues/rtc.jsonl`, the stand-in's call log without file data,
`checkpoints.bin`, the card index (hashes only) and `screen.png`; and it says the bundle is
unlikely to replay. It never writes unchecked content.

#### 5.3.5 What no check can close (the residuals, Q14)

Stated as rules, not examples:

1. **The floor bounds only verbatim copies of the indexed distribution.** A copy altered at
   least once every 94 bytes shares no 95-byte run with the original and passes. That covers:
   - **another NextZXOS version**: after any insertion every absolute `JP`, `CALL` and `LD nn`
     target moves, so relocated Z80 code shares mostly short runs: most of another version
     escapes, not a little of it;
   - a systematic patch, an interleaving, a byte-wise transform;
   - a text re-encoding (hex, base64, Intel HEX);
   - any compression or encryption the check does not decode.
2. **Ticked or default-shipped user files.** A firmware copy outside the system paths, in a
   format the classification does not flag, is a user file. On a known (distribution) base,
   revision 1 shipped user files **by default without a tick**, so a NextZXOS of another version
   copied onto the default card to try it out would have passed silently; on an unknown base, a
   ticked one passes the same way.
3. **Content outside the index.** Anything copyrighted that is not in the distribution jnext
   indexes (a commercial game, an older distribution, a Multiface 128 or +D ROM) is invisible to
   the check. Rule 1 is about NextZXOS; the program question is Q4.
4. **Channels that are not files.** Residual 1 applies to every content channel that ships, not
   only to card files. Besides card user files and stand-in host files, four channels carry
   content: **the RX streams** (a NextZXOS update, or another version, downloaded over the ESP),
   **debugger memory-write payloads** (a DeZog or GDB upload), **the program** (Q4) and **shipped
   keyframes** (`--no-sdcard` only, Q3). Each passes the byte check, and each can carry an altered
   copy the check cannot see.
5. **Pieces of the clean corpus.** A copy made only of 64-byte pieces that also occur in z88dk
   builds of jnext's own sources (the run-time and library portions of z88dk-built distribution
   programs such as `TBBLUE.TBU` and the dot commands) passes. Each piece is z88dk's own code or
   run-time data (the clean corpus links no third-party data: no ROM font, no vendor table), not
   NextZXOS's or another vendor's; it is still a byte string that also occurs on the card, which is
   the line the owner accepted (Q18); a long run of them is not proved to exist contiguously in
   one clean build (5.3.1).

The classification removes the common cases (system paths, archives, snapshots and
distribution-derived files are hashes only on every base). Q14's recommended option puts every
content channel under the reporter's explicit choice: one tick per user file and per served host
file, one "network data" tick for the RX streams, one "debugger uploads" tick, the program's own
tick (Q4), and one "keyframes" tick; nothing is ticked by default except the program. Only
shipping no content at all (Q14's option (z)) closes residuals 1, 2, 4 and 5 completely (with no
program shipped, no clean-corpus piece ships either); Q18's option (d) is the program-only
equivalent.

The review lists every file whose bytes are included ("these 14 files of yours are in the
bundle"), every file waiting for a tick, and every file the developer will need from the reporter
instead.

### 5.4 Replaying without the image

- **A `--no-sdcard` session needs no card at all**: the NEX runs on the synthetic machine with
  the esxDOS stand-in, whose host calls are in the queues. Self-contained, subject to the byte
  check on what the stand-in served.
- **A classic machine needs only its ROM files**, by SHA-256, from any card that holds them (any
  distribution with the same `48.rom`). When the session read no card sector after the anchor
  (DivMMC never touched the card), the replay mounts a placeholder of the same size, and any
  read of it is reported as "this session needs the card" rather than served.
- **A session on a distribution card needs the pristine rebuild**, which the developer's jnext
  makes from the same distribution it already downloads, plus the bundle's sectors. Every input
  sector is checked on read, so a mismatch names the sector, its path and both hashes.
- **System and distribution-derived files that differ from pristine** (the user replaced a file
  under `/NEXTZXOS`, or NextZXOS rewrote a settings file there) are hashes only, so the developer
  cannot replay past their first read without the reporter's copy; the review says so before
  the bundle is written, and names them. The one exception is `/MACHINES/NEXT/config.ini`, which
  the owner allowlisted alone (Q15) under its testable criterion.
- **Unticked user files and an unknown base**: the developer needs the reporter's copy of every
  hashes-only file the session read (an unticked user file, or anything on an unknown card that
  is not pristine); the bundle lists them by path.

### 5.5 Assembling the card for a replay

1. **The base.** For a distribution card, the pristine rebuild is made once and cached at
   `~/.jnext/sdcard/bug-report-bases/<pristine sha256>.img` (1 GiB for the default card, 40 MiB
   for the minimal one), under `~/.jnext/sdcard/` as the project's image rule requires (owner
   decision, 2026-11-20: comply, no exception), verified against the SHA-256 the bundle records,
   and opened read-only. Its path
   is never the developer's working card. The rebuild must be byte-reproducible: FatFs is built
   with `FF_FS_NORTC 1` (no timestamps from the host) and recipe 3 sets the volume serial from
   the raw image's hash, so it should be; BRB-CARD-20 rebuilds it twice and compares, and
   compares with the save side's recorded digest.
2. **The journal.** A scratch journal (the `sd_write_journal` format, version 1, bound by
   SHA-256: `Binding::by_digest()`) at a per-replay path in jnext's per-run scratch directory.
   Into it go the shipped sectors, and every reference resolved from the base (pristine sector M
   written at sector N; a reference to the same number needs no record). The journal registry is
   process-wide and keyed by image path (`sd_write_journal::for_image`); because the base path is
   the cache path above, never a path the GUI mounts as a working card, a replay cannot collide
   with the developer's own card. The replay binds with `reset` so a previous replay's
   registration of the same base is replaced, and one replay runs at a time per process.
3. **Writes.** The guest's writes during the replay append to the scratch journal; the base is
   never written; the journal is deleted after the replay unless the developer asks to keep it.

### 5.6 The warm start and the second card

A `.nex` load on the Next starts from the warm start, recorded in an earlier run. Its boot read
sectors before the anchor, so B0.6 makes the recording keep its own input sectors (number, hash
and bytes: local storage, never published) and the SHA-256 of its state stream. The bundle
carries those sectors under the rules of 5.3. Replay order: build a journal of the warm start's
read set over the base, regenerate the warm start on it (the recorder's RTC is already pinned,
section 2), compare the stream digest and refuse on a mismatch; then replace the journal with the
anchor-time input sectors and start the session. The two sets usually coincide, since a write to
the card invalidates the warm-start cache.

The second card (#505) is captured and assembled the same way (`card1/`).

---

## 6. Determinism, source by source

Every source has two kinds of row (section 15): a **mutation** row (alter the logged entry, the
replay must diverge: the replay consumes the log) and a **capture** row (replay with the live
source different or absent, the replay must still match: the recorder captured it). The last
column names the capture row's condition.

| Source | Enters the machine | Captured as | Replay | Capture row: replay with |
|---|---|---|---|---|
| Keyboard, extended keys | frontend key handlers, debugger input verbs, scripts, `--delayed-keypress` | event at the seam, level form, with cycle | applied at the cycle | no live input (injectors off); the record side driven by `xdotool` in the Qt and SDL GUIs |
| Phantom typist, auto-type | machine-internal, reacting to port 0xFE reads | not logged (re-runs by itself) | re-runs | BRB-IN-08: a TAP load replays without double keys |
| Joysticks (both connectors, MD 6-button, membrane stick) | joystick dispatcher, 12-bit connector state | event | applied | no pad attached; the record side drives the SDL gamepad seam through an SDL virtual joystick (`SDL_AttachVirtualJoystick`, already used by `gamepad_host.cpp`) |
| Mouse | `KempstonMouse::inject_delta`, `set_buttons`, `set_wheel` | event per call | same calls | no live pointer; the record side moves it with `xdotool` |
| Host input timing | between frames, or at the resume point while paused mid-frame | the cycle of application | the same cycle | BRB-IN-06: the `.jds` WARNING case becomes exact |
| Audio-driven pacing, `--speed`, `--when-slow-prefer` | whole frames only (`audio_pacing.h`, `render_policy.h`) | not logged: wall clock only | n/a | GUI-record, headless-replay rows |
| Render skip of superseded frames | sprite side effects still run (`run_frame`) | not logged | n/a | same rows |
| `--silent`, fast boot (`audio_off_`) | PSG and mixer synthesis skipped | recorded as machine configuration until a row proves it guest-invisible | honoured | BRB-CFG-05 decides |
| CPU speed from the GUI | `MainWindow::on_cpu_speed` and the Preferences apply write NR 0x07 directly | event (runtime machine write) | the same write at the cycle | a headless replay, which has no menu; the record side clicks the menu |
| Tape fast load and auto-play toggles | `set_fast_load`, `set_tape_auto_play` from the menu and the Preferences apply | event (machine-class field changed after `init`) | applied | headless replay with the opposite startup setting |
| RTC | `I2cRtc::snapshot_time()` at every I2C START; `current_time()` for `M_GETDATE` | value queue (the broken-down time) | answered from the queue | a host clock that differs: a test-only clock-offset seam on the replay side (`--rtc` cannot be given with `--bug-report-play`) |
| `--rtc` pin | frozen | configuration | honoured | BRB-CFG row |
| Warm start | boot under the recorder's pinned RTC (b93c03f79) | identity, read set, stream digest (B0.6) | regenerated, digest checked | different host-class settings on the replay side (B0.6 row) |
| ESP-01 (AT replies, echo, sockets, SNTP, ping, timeouts) | RX bytes from `tick()` | byte stream; TX digest | replay UART device; ESP off | no network peer (the row's loopback peer stopped) |
| NextPi UART | `pi_uart_device` | byte stream | replay device | no QEMU |
| NextPi I2S into EAR | the Pi audio thread | **not in v1**: flagged, the replay warns from the first sample (Q11) | n/a | BRB-STR-09 (the warning) |
| Joystick-port UART (`--joy-uart-rx` file) | deterministic from the file and its delay | the file is a member, under the content check | honoured | the host file deleted |
| Joystick-port UART (fifo, pty) | external process | byte stream | replay device | no writer on the fifo |
| esxDOS stand-in | host file reads, listings, `stat`, file times (host clock) | value queue with the bytes | replay host FS; never writes the host, even with `--esxdos-stub-writable` (writes are digest-checked only) | the served file changed, and in a second row deleted |
| Extended-NEX read overlay | the program file's bytes through the SD device | not logged: re-derived from the program member or reference | rebuilt by `load_nex()` | n/a (deterministic from the program) |
| SD card sectors | SPI reads | input-sector set + undo log | scratch journal, per-read hash check | a base that differs in sectors the session did not read |
| Tape | the file (digest of the bytes read) and deck actions | member + events | honoured | the host tape file deleted |
| Host actions (reset, NMI, drive button, SD insert, POK, ESP associate) | GUI, CLI `--delayed-*` | event | applied; their CLI options off | headless replay with none of the CLI options |
| Debugger edits (memory, registers, NextREGs, ports, loads) | `note_mutation()` call sites | event with the verb's arguments; every memory payload in `debugger/writes.bin` | re-issued | no debugger client attached |
| Remote debug clients (DZRP, GDB, ZRCP) | through the `Debugger` facade | as debugger edits (B0.1 verifies every server path reaches the facade) | re-issued | no client connected |
| Rewind while recording | restore + fast-forward within one frame (the ring holds a slot per frame) | the log is truncated at the landing; the fast-forward is fed from the log (B1.7) | n/a | BRB-TL rows |
| Quick snapshot taken and loaded in the segment | restore | a jump to the recorded cycle | replays to it | BRB-TL row |
| Any other snapshot, `.jns`, machine change, program load | re-initialises | ends the segment (a new anchor) | n/a | BRB-TL rows |

---

## 7. Recording: GUI and CLI

### 7.1 GUI

- **File > Bug Report** (Qt; the SDL frontend gets the same actions on its host keys):
  **Start Recording** (from now: a new segment from the current state is impossible without a
  memory snapshot, so it performs a hard reset or reloads the current program, and says so
  before doing it), **Save Bug Report...** (the rolling segment, at any time; the file dialog
  proposes `<name>.YYYYMMDDhhmm.jnext-bug.zip`, section 4; "in the last directory used" is the
  planner's interpretation, for the owner to confirm), **Stop
  Recording**, **Open Bug Report...**. Menu only: there is no hotkey (owner decision Q12).
- **Rolling recording** (Preferences, Q2): the recorder runs from every anchor, in memory, and
  nothing is written until the reporter saves. `--fast-boot` is off today "when a recording
  runs" (man page); the rolling recorder is exempt, because fast boot is deterministic (frames
  and the first key) and recorded as configuration, so a default-on recorder does not silently
  turn fast boot off for everyone. "Save Bug Report..." is then available at any
  time, which is the issue's "from a rolling buffer, like rewind".
- **The save dialog** shows the privacy review before writing (section 9): duration, program
  (included / reference / excluded), card files whose sectors are included (paths, sizes),
  network hosts with byte counts, host files the stand-in served, the warnings, a notes field,
  "include the log" (off), the total size. Saving does not stop the recording.
- **Status bar**: an indicator while recording, with the segment's length; a warning state when
  the memory cap is near.
- **Preferences > Bug Reports**: rolling recording on or off, memory cap (MiB), checkpoint
  interval (frames), include the program by default, include the log by default.

### 7.2 CLI and configuration keys

Every GUI setting has a CLI way to set it (project rule): the `bug_report.*` rows of
`config_keys.cpp`, reachable through `--set`, plus dedicated flags where they are wanted.

| Option | Effect | Key |
|---|---|---|
| `--bug-report-record FILE\|DIR` | record from the start of the run (or from the `--load`), write the bundle at exit: to FILE as given, or, when the argument is an existing directory or ends in `/`, to `DIR/<name>.YYYYMMDDhhmm.jnext-bug.zip` (section 4; the directory form is the planner's interpretation, for the owner to confirm) | none (per run) |
| `--bug-report-rolling` / `--no-bug-report-rolling` | the rolling recording, this run | `bug_report.rolling` |
| `--bug-report-memory-mb N` | memory cap; at the cap the recording stops with a warning | `bug_report.memory_mb` |
| `--bug-report-checkpoint-frames N` | checkpoint interval | `bug_report.checkpoint_frames` |
| `--bug-report-exclude-program` | the program as a SHA-256 reference only | `bug_report.include_program` |
| `--bug-report-include-log` | add the redacted log | `bug_report.include_log` |
| `--bug-report-notes TEXT` | `notes.txt` | none |
| `--bug-report-play FILE` | replay (also `--load FILE` and a bare file name, by content) | none |
| `--bug-report-card FILE` | the developer's base image for card 0 (`--bug-report-card2` for card 1) | none |
| `--bug-report-stop-frame N` / `--bug-report-stop-before SECS` | where the replay pauses (GUI) or ends (headless) | none |
| `--bug-report-strict` | a version mismatch refuses instead of warning | none |
| `--bug-report-info FILE` | print the summary and the privacy review, exit | none |

Headless: `--bug-report-record FILE` with `--delayed-automatic-exit-frames N` is the
scriptable "save at frame N" the rows use. A replay exits 0 when every checkpoint matched to the
end, and a distinct documented status (under EXIT STATUS) when it diverged.

Exclusions, enforced in `cli_options.h`:

- `--bug-report-record` with `--bug-report-play`, `--rzx-record` or `--rzx-play` (the anchors
  conflict). The RZX oracle (section 15) therefore uses two recordings, not one.
- `--bug-report-play` with a second program (`--load` of another file: `--load bundle.zip` is
  itself a play), `--inject`, `--script`, `--delayed-keypress`, `--delayed-nmi`,
  `--delayed-pok-frames`, `--delayed-sdcard*-insert-frames`, `--pok-trainers` (the bundle brings
  its own inputs).
- `--bug-report-play` with any machine-class option (`--machine`, `--rtc`, `--sdcard*`,
  `--esxdos-stub*`, `--joy-uart-*`, `--fast-boot`, `--cpu-speed`, `--esp*`, `--nextpi*` and the
  rest of B0.4's machine class): refused, naming the option, because the bundle brings the
  machine. Replay-side options are accepted: `--bug-report-card*`, `--headless`, display and
  audio settings, `--rewind-buffer-size`, `--trace`, `--coverage`, the debug servers, maps and
  source maps, `--log-*`.

---

## 8. Replay and version mismatch

**Opening a bundle** (GUI) shows its summary: recorded with which jnext, machine, duration,
notes, the card bases it needs and whether they are present, the warnings, and any version
mismatch. **Replay** then rebuilds the anchor, runs at full speed with a progress bar to the
chosen landing point (the end, frame N, or N seconds before the save point), with the rewind
ring sized to cover the time before it, and pauses in the debugger. From there everything
works: stepping, breakpoints, watchpoints, every panel, rewind and step back (the cursors seek,
B3.5). A debugger edit during a replay warns that the rest of the log may not apply. Running
past the end of the log makes the machine live: the developer's own input takes over.

**Version rules** (B3.3). The honest rule first: **a bundle whose anchor ran firmware replays
faithfully only under the reporter's exact jnext version.** A `.nex` bundle on the Next anchors
on the warm start, and the developer's jnext regenerates it; its digest is of the full state
stream (B0.6), which a different version changes as soon as `kFormatVersion` or the stream length
moves, or any emulation change touches the ~760-frame boot. A power-on bundle re-runs the
firmware boot inside the replay, so the same changes show as an early divergence. The manifest
carries `git describe`, so the remedy is to build that version (`git checkout <describe>`, then
`make gui-release-non-pgo`), and the refusal prints exactly that.

| Case | Behaviour |
|---|---|
| `format_version` newer than the reader | refused |
| Same jnext version and state format | full digests compared; any mismatch is a divergence |
| Different version, firmware anchor (warm start, or power-on on the Next) | refused with the reporter's `git describe` and the build command; `--bug-report-cross-version` replays anyway (regenerated anchor, observable and picture digests only, the first divergence expected early) |
| Different version, no firmware in the anchor (`--no-sdcard`, a classic snapshot or tape on a classic machine) | warned; observable and framebuffer digests compared, the full digest skipped; `--bug-report-strict` refuses |
| Divergence | a banner with the frame and cycle of the first differing checkpoint and which digest differed (state or picture only); the replay goes on; headless exits with the divergence status |
| A card sector read that differs from its recorded hash | reported with its number, FAT path and both hashes (a card divergence, not an emulator one) |
| A warm start whose digest differs under the same version | refused, naming both digests and the warm-start versions (a card or host-setting difference, B0.6) |
| A queue pulled at another cycle, a queue running dry, a stream byte due at a cycle that never comes | divergence, naming the source |

A divergence under a newer jnext is information, not only an error: "the replay diverges at
frame 4123, picture only" on a fixed version is the fix showing, which is what
`--bug-report-cross-version` is for.

---

## 9. Size and privacy

**Size.** Deflate on every member. Typical contents: events a few KiB to a few MiB per hour
(mouse movement is the bulk); checkpoints about 30 bytes each (one a second at the default);
the card's included sectors (the user's own data that was read, usually small); the program; the
network streams (9 bytes per byte uncompressed, compressing well). The writer states the size in
the save dialog and warns above 25 MiB, GitHub's attachment limit. The in-memory recording has a
cap (Q13); when it is reached the recording stops and the status bar says so, rather than
dropping the head of a log that cannot be replayed without it.

**What personal data could end up inside, and what the design does about it.**

| Data | Where it would come from | Treatment |
|---|---|---|
| Host paths and the user name | program path, card path, `--esxdos-stub-root`, source directories, the log | never written: members by base name, the manifest holds no host path; the log, when included, is info level only and has `$HOME` and absolute paths replaced |
| File names on the card | directory sectors among the input sectors | listed in the review by path; the reporter sees them |
| The user's files on the card | input sectors not in the distribution | listed with sizes; included only because the session read them and the reporter ticked them (Q14) |
| Keystrokes | the input log | inherent; the review says how many key events and over how long |
| Passwords typed into the guest | e.g. a WiFi password typed into a NextZXOS dialog, sent to the emulated ESP | the TX side is never stored (digest only), but the keystrokes are, and with `ATE1` the AT engine echoes TX bytes into RX (`esp_at.cpp`, the command-mode echo), so the password can sit in the RX stream too. The echo comes from the threaded worker, so it cannot be regenerated on replay and must stay in the stream; the review flags an `AT+CWJAP` (or any AT command) seen while echo was on, and says the keystrokes and the stream both hold what was typed |
| Network content | ESP RX streams | review lists hosts and byte counts |
| Host files served by the stand-in | value queues | listed by bundle-relative name and size |
| Local time and time zone | RTC values | inherent; the review shows the first and last RTC value |
| Machine identity | platform string, build type | kept (it is what makes a report bisectable); no host name, no MAC, no user id |

`--bug-report-info` prints the same review, so a developer (or the reporter, before attaching)
can check a file without opening jnext's GUI.

---

## 10. Phase B0: foundations

### B0.1 Seam inventory

Classify every path by which host state reaches emulated state: frontend input handlers (Qt,
SDL, browser), `Debugger` verbs (`debugger_input.cpp`, every `note_mutation()` call site, 20 today),
the remote servers, the script engine, the CLI `--delayed-*` actions, POK trainers, GUI actions
(reset, NMI, drive, tape deck, SD insert and eject, machine change, load), **runtime writes of
machine state or machine-class configuration from frontend menus and Preferences** (the CPU
speed menu and the Preferences apply write NR 0x07 directly, `main_window.cpp` `on_cpu_speed`
and `apply_startup_config`; the tape fast-load and auto-play toggles), the pulled sources
(the `i2c.cpp` RTC, the esxDOS stand-in), the extended-NEX read overlay (classified as
re-derived, not logged), the UART devices, the SD card's read and write paths, and
configuration read at `init`. Each class gets one seam the recorder
observes and the replay drives; where two frontends reach the machine by different calls, they
are routed through one. The table goes into the developer guide.

Tests: `bundle_seam_test`, rows BRB-SEAM-01.. : each seam reports through the recorder
interface (a fake recorder counts calls) for each entry point the inventory lists; a mutation
that bypasses one seam (calling `Keyboard` directly from a frontend handler) fails its row.

### B0.2 Session log core

`src/bundle/session_log.*` (a new library, `jnext_bundle`, linking `jnext_save` for the
container): the anchor-relative cycle, the event vector, one queue per pulled source, one
stream per UART, each with a cursor that can seek to "the first entry after cycle C" (for rewind
and for the replay's step back). A memory cap counted over all of them.

Tests: `bundle_log_test`, BRB-LOG-01.. : append and iterate in cycle order; seek to every
boundary (before the first, between equal cycles, after the last); a queue consumed in order;
a pull at the wrong cycle reported; the cap stops the log and reports it; encode and decode
round trip through the member formats of section 4 with byte-exact members.

### B0.3 Checkpoint digests

At every Nth frame boundary: CRC-32 of the Z80 registers plus RAM, CRC-32 of the framebuffer,
CRC-32 of the `save_state` stream (the rewind's stream, already produced per frame when rewind is
on). Overhead to be measured on `build/gui-release-non-pgo` with `make bench`, with and without the
recorder (Q2's condition).

Tests: BRB-LOG rows for the digest: a one-byte RAM change, a register change and a pixel change
each move exactly the digest they should; the full digest equals a CRC of `save_state`'s bytes.

## 11. Phase B0, continued: configuration and the warm start's read set

### B0.4 The machine configuration

A field table over `EmulatorConfig` (one row per member: name, class, encoder), in the same
single-source spirit as `config_keys.cpp`: class `machine` (machine type, CPU speed, TurboSound,
tape fast load and auto-play, fast boot, `silent` until proved guest-invisible, RTC pin, cards and
their modes, `no_sdcard`, the esxDOS stand-in options, NEX arguments and v1.3 permission, the
inject options, ESP on and its schedule, NextPi on, joy-UART, magic port), `host` (scale, CRT,
gains, paths of screenshots), `debug` (trace, coverage, ports, maps: replayed as the developer
chooses), or `excluded` with a reason. A tripwire row pins the member count, so a new field
fails until it is classified (a count pin, authorised for new rows). **A machine-class field
changed after `init`** (the tape toggles today; any future live toggle) goes through the seam
of B0.1 and is logged as an event, so a field cannot change behind the recorder's back.

Tests: `bundle_config_test`, BRB-CFG-01.. : every machine-class field round trips; a host field
is absent from `config.json`; a path field is reduced to a member name; an unknown machine field
in a newer bundle refuses; the tripwire; a machine-class field set after `init` without the
seam fails its row; BRB-CFG-05: one run with sound and one `--silent` from the same anchor and
inputs, compared by digest, decides whether `silent` stays a machine field.

### The warm start is already reproducible across clocks (no task)

Revision 0 had a task here to pin the recorder's RTC. It is not needed: the pin exists.
`Emulator::warm_start_boot_config()` sets `rtc_fixed` to 2026-07-10 08:55:00
(`kWarmStartRtcYear`), the comment above its call in `emulator.cpp` explains why (the NextZXOS
menu's work depended on the RTC minute, so the same card recorded different machines at different
times of day), and b93c03f79 landed it with `warm-start-chans-func` row E (three recordings under
different clocks are byte-identical) and row G (a warm-loaded program reads the user's clock, not
the pin). The recording is therefore a function of the card, the machine configuration and the
jnext version; `kRecordingVersion` stays 1.

### B0.6 The warm start's read set

The recording keeps, in its cache file, every sector its boot read first (number, SHA-256,
bytes) and the SHA-256 of the recorded state stream (the full stream, so B3.3's version rule
follows from it). A bundle whose anchor is the warm start names the warm start's identity and
digest and folds these sectors into its own input sectors (replay order in 5.6).

One property is not yet proved: the recorder copies the live `config_` apart from the fields
`warm_start_boot_config()` clears, deliberately including audio gains and CPU speed. The bundle
replays with the reporter's machine-class fields, but the developer's host-class fields (audio
gains, display) are the developer's own; if any of them changes the recorded state, the
regenerated digest differs on every developer machine.

Tests: BRB-WS-04: the read set of a recording on a synthetic card equals the sectors the boot
read (counted by a test hook on the SD device); BRB-WS-05: a card edited in one read-set sector
regenerates to a different digest, and the replay's refusal names it; BRB-WS-06: two recordings
of the same card whose configurations differ in every host-class field of B0.4 (gains, scale,
CRT, screenshot paths) have identical streams. If BRB-WS-06 fails for a field, that field moves
to the machine class (it then describes the machine, and the bundle carries it), or
`warm_start_boot_config()` normalises it for the recording; the sub-issue decides per field, with
the evidence.

## 12. Phase B1: recording

### B1.1 to B1.4 Inputs, actions, debugger edits, timeline (sub-issue 4)

At the seams of B0.1, each event gets its cycle and frame. Level form for keys and connectors;
call form for the mouse (deltas are not levels). Host actions are logged as named events with
their arguments (a tape insert names its member; an SD insert names the card). Debugger edits
log the verb and its arguments, so the replay re-issues the same call.

Timeline rules (B1.4): a hard reset, a program load, a machine change start a new segment (the
recorder keeps the latest one, Q13); a rewind truncates the log at the landing cycle, which is
exact because the rewound state is the recorded state at that cycle (and B1.7 makes that true
for every source); a quick snapshot taken in the segment carries its cycle in memory, and loading
it back is a jump the replay reproduces by replaying to that cycle; any other snapshot load ends
the segment.

Exactness of the rewind rule rests on the ring holding a slot per frame, so the rewind's
fast-forward never crosses a frame end (where `replay_mode_` would skip the sprite side-effect
pass). The recorder asserts it: a fast-forward that crosses a frame boundary while recording
ends the segment with a warning instead of truncating (BRB-TL-06), so a future sparser ring is
caught rather than trusted.

Tests: `bundle_record_test`, BRB-IN-01.. (one row per input kind, the recorded cycle equals the
cycle the machine saw the change; a change while paused mid-frame is stamped with the resume
cycle, BRB-IN-06; a TAP load's typist keys are not logged, BRB-IN-08), BRB-ACT-01.. (one per
action, the CPU speed menu, the Preferences apply and both tape toggles included), BRB-DBG-01.. (one per mutating verb: the logged arguments re-issued on a second machine
give the same digest), BRB-TL-01.. (a rewind truncates; a re-done input after the rewind is the
only one logged; a quick-snapshot jump; a `.jns` load ends the segment with its warning).

### B1.5 to B1.7 Pulls and streams (sub-issue 5)

The RTC seam covers both host-time paths of `I2cRtc` (`snapshot_time()`, which latches the
registers a guest I2C read sees, and the wall-clock read the esxDOS stand-in's `M_GETDATE`
answers from); each returns the broken-down time it read and logs it; the esxDOS stand-in logs
each call with its arguments and its result (the bytes of a read, a listing, a `stat`), so a host
file edited during the session is captured as the guest saw it, and the bytes go through the
content check. The extended-NEX read overlay is not logged: it serves the program file, which
the replay has as a member or reference.
The UART tee sits between each `UartDevice` and the RX FIFO, so the ESP, the Pi and the joy-UART
are one mechanism. B1.7: while recording, the rewind's fast-forward consumes the log's queues
and streams instead of the live sources (today it leaves the ESP inert and reads the live RTC).

Tests: BRB-PULL-01.. (an RTC read logged and replayed; two reads in one frame keep their order;
an esxDOS read of a file changed between two opens replays both contents), BRB-STR-01.. (an AT
exchange against the inline engine replays byte-exactly; a TX difference is detected by the
digest; a fifo joy-UART session replays; BRB-STR-09: an I2S sample routed to EAR raises the
not-replayable warning), BRB-TL-05 (a rewind across RX bytes lands on the recorded digest).

### B1.8 to B1.10 SD-card capture (sub-issue 6)

As section 5. All rows use synthetic FAT32 images made at run time (`fatfs_format.cpp`), never
a NextZXOS image.

Tests: `bundle_card_test`, BRB-CARD-01.. : first access read vs write classified per sector; the
undo log restores the anchor content in read-write mode and in journal mode, and holds no
pre-image for a write-first sector; a sector present in the synthetic "pristine" image (at
another number too) becomes a reference, and the replay resolves it into the journal at the new
number; each class of 5.3 on a synthetic card (a modified system file on a distribution base is
hashes only; a copy of a distribution file under another directory with one byte changed is
hashes only by the distribution-derived rule; the same copy renamed and shifted by 7 bytes is
caught by the byte check; a `.zip` and a `.snx` are hashes only; a user file is bytes when
ticked, hashes only when not, on a distribution base and on an unknown base alike (Q14's
default); a free-space sector and an MBR, reserved or outside-partition sector are references
when pristine and hashes only when not; an FSInfo sector with its signatures and zero reserved
bytes ships, one with a non-zero reserved byte is hashes only); low-information exclusion
(BRB-CARD-30..): a directory sector, a FAT sector of a card with free clusters, a user file
zero-padded to its cluster, and a zeroed slack all pass the byte check; a 95-byte firmware run
planted inside a zero-padded region is still found; E1's boundaries (a window of period 4 is
excluded, of period 5 is indexed; a period-4 fill with two stray bytes is excluded, with three
stray bytes is indexed; a two-value bitmap of period 8 is indexed); a window present in every
synthetic distribution file is still indexed (there is no E2); a window present in a synthetic
clean-corpus file is dropped, and the same window absent from it is not; a trivial sector equal to a pristine sector does not make its file
distribution-derived; slack: a copied file's last sector with distribution bytes in its slack
ships with the slack zeroed; no index: the writer writes exactly the content-free members of
5.3.4 and nothing else; the second card; BRB-CARD-20: the pristine rebuild made twice is byte-identical
and equals the digest the save side recorded; BRB-CARD-21: a replay base path never equals a
working card path, and a second replay of the same base replaces the first registration. The
index for the default card is built and cached by the regression master's provisioning at run
time (it is never committed); the unit rows use synthetic "distributions" made with
`fatfs_format.cpp`.

## 13. Phase B2: the bundle file (sub-issue 7)

Writer and reader as section 4, through `zip_archive`. The reader validates the manifest against
the members, applies the version rules of section 8 and builds the privacy summary that the save
dialog, the open dialog and `--bug-report-info` share.

Tests: `bundle_file_test`, BRB-FILE-01.. : the default name (the section 4 interpretation, for the
owner to confirm), through a test clock seam (a fixed local time): `game.nex` saved at 2026-11-20 05:51 is `game.202611200551.jnext-bug.zip`, no program
gives `jnext.202611200551.jnext-bug.zip`, a name with spaces and non-ASCII characters is reduced
to the allowed set, a second save in the same minute gets `-2`, an existing file is never
overwritten, and a bundle renamed to anything is still recognised by its manifest while a plain
`.zip` named `*.jnext-bug.zip` without one is not; a written bundle is read back member for member; a
missing, an extra or a corrupt member refuses; `--bug-report-info` prints the review (a golden
text for a synthetic bundle); the manifest holds no absolute path (scanned); a foreign reader
(`unzip -t` and `jq` in a functional row) opens it, as the `.jns` rows do.

The content-check rows (BRB-FILE-20..) plant a 95-byte run of a synthetic distribution file into
the **payload the guest saw**, never into a member's raw storage, at every offset 0..511 of the
payload, and require that each is found: in the RX bytes of an ESP stream (so the run is spread
over 95 nine-byte records); in a host file served by the stand-in in 32-byte `F_READ`s; in a
debugger `CMD_WRITE_MEM` payload; in a `.szx` `RAMP` page stored zlib-compressed; in a `.z80`
page stored run-length encoded; in a member of a `.zip` program archive; in a TAP data block; in
the log. A 94-byte run may pass (the documented floor), and a run made only of excluded windows
passes. A WAV program and a TZX with a direct-recording block are shipped as references only. A
`.szx` with a custom-ROM chunk and an `SCLD` chunk is written without either, its `RAMP` pages
uncompressed. A v3 `.z80` with pages 0 and 11 is written with pages 3-10 only, and a v1 `.z80` is
checked as one decompressed 48K block.

**The real-index row** (BRB-FILE-40, in `bug-report-privacy-func`, so it runs where the regression
master's card exists). It builds the distribution index **at run time** from the run's own card,
copies every system-path file verbatim (a) into a user file on a scratch card, (b) into a host file
served by the stand-in and read in 32-byte `F_READ`s, (c) into an ESP RX stream, and requires
every copy of every file of 95 bytes or more to be found; it also plants a random 95-byte slice of
each system-path file at a random offset of each of the three carriers (the seed printed, so a
failure reproduces), which tests the floor theorem itself on real data; it prints the indexed
fraction of each system-path file, names every system-path file of 95 bytes or more made only of
E1-excluded windows (left to the classification, like the files under 95 bytes, not a failure),
and fails if any other file of 95 bytes or more has no indexed window, or if the clean corpus
drops from **any** system-path file a number of windows other than the count this design pins for
it (5.3.1's drop table: 0 for every ROM, 1 for `/TBBLUE.FW`, 19 for `/TBBLUE.TBU`, 11 for
`/dot/NextPiUI`, the listed dot commands as listed, 0 for the other 234), so a probe or sweep that
drops windows the demos did not is a reviewed change to this table, never a silent one. Every byte it
handles stays in the run's scratch directory or under `~/.jnext/sdcard/`, and is deleted with the
row's directories; it commits nothing and writes no ROM or firmware byte anywhere else. The
synthetic BRB-CARD-30 rows cannot find what this row finds (round 3: E2's flaw only shows on the
real distribution).

**The false-positive row** (BRB-FILE-41, in the same functional row, against the same run-time
index). It builds the tracked `demo/` programs from source with the pinned z88dk (`.nex` and
`.tap`) and takes the committed dapr test programs, **deduplicates them by SHA-256**, and checks
each distinct program against the index with the clean corpus built **leave-one-source-out**: the
committed probes and sweeps plus the builds of every *other* source, never any build of the
program's own source (same stem and source directory, every output type, every byte-identical
copy), which is what a reporter's program meets. It pins the number of refused sources at **0 of
27** (43 distinct contents; revision 5's measurement), and prints each refusal's member, offsets,
symbol where a map exists, and matched distribution files, so a z88dk bump or a new demo that needs
more probes fails loudly instead of quietly shipping no program.

**The clean-corpus row** (BRB-FILE-42, same functional row). It hashes every 64-byte window, at
every offset, of every clean-corpus binary, and requires that none equals a window of any
`/machines/**/*.rom` file (at any offset) or of the 48K character set (`48.rom` 0x3D00-0x3FFF), and
that no clean binary's map names a symbol on the excluded-data list (5.3.1). It fails, naming the
binary, the offset and the symbol, before a font or other third-party table can reach a commit.

## 14. Phase B3: replay (sub-issues 8 and 9)

As sections 3 and 8. The replay devices are the replay halves of the B0.1 seams; the card
assembly is section 5.5; the landing point uses the rewind ring so the developer can step back
from it.

Tests: `bundle_replay_test`, BRB-RPL-01.. : a recorded synthetic session (in-process, the
no-card NEX path) replays with every checkpoint equal; each refusal of section 8 (newer format,
warm-start digest, card sector hash, dry queue); the version-mismatch path skips the full digest;
the end of the log hands over to live input. BRB-DBG-10.. : a rewind during a replay re-feeds the
log (the digest after the rewind and a second run forward equals the recorded one); a debugger
edit during a replay raises the warning.

## 15. Phase B4 and B5: interface, proof, documentation

### B4 (sub-issues 10 and 11)

As section 7. Qt rows in the existing offscreen Qt suites (the menu actions, the dialogs' review
contents against a synthetic bundle, the Preferences rows); `config_keys.cpp` rows with
`app_config_test`'s AC rows; `make cli-check` for the flag table and the man page.

### B5.1 to B5.4 The proof (sub-issue 12)

**How a replay's determinism is proven.** A replay that matches its own recording is the weakest
evidence there is (an engine that ignored every event would match a session with no input, and a
recorder that missed a source would match whenever the live source happened to answer the same
way), so the proof has five legs:

1. **The probe program makes every source matter, every checkpoint interval.** A NEX (source in
   `demo/`) whose main loop reads the keyboard, both joysticks and the mouse every frame, and the
   RTC, an AT exchange with the ESP, a file through the esxDOS stand-in and a card sector through
   NextZXOS (where there is a card) at least once every 25 frames, and folds every value into a
   running checksum in RAM and on screen. With checkpoints every 50 frames, every source is read
   inside every checkpoint interval, so "divergence at the checkpoint after the altered entry"
   is guaranteed by construction, not hoped for; BRB-PROBE-01 checks the probe's own schedule
   from a trace of its port and esxDOS accesses.
2. **Mutation rows, one per source** (the replay consumes the log). For each row of section 6:
   record a probe session, alter that one source in the saved bundle (drop an event, change an
   RTC value, flip an RX byte, flip one included sector byte, change one machine field), replay,
   and require a divergence report at the checkpoint after the altered entry, naming the source
   where the source is named.
3. **Capture rows, one per source** (the recorder captured it). Record a probe session, then
   replay with that source's live side **different or absent**, as section 6's last column says:
   a host clock offset by a day through a test-only clock seam; the served host file changed,
   and in a second row deleted; the loopback network peer stopped; no fifo writer; no QEMU; no
   pad, with the record side's pad driven through an SDL virtual joystick so the gamepad seam
   itself is exercised; a base differing in sectors the session never read. The replay must
   match every checkpoint. An unlogged source passes the mutation row only by luck and fails this
   one, because its live answer is now different.
4. **Independent oracles.** RZX: two recordings of one scripted input on the 48K and the 128K,
   one with `--rzx-record`, one with `--bug-report-record`, each with the same `--script` keys,
   the same `--rtc` pin and a classic snapshot as the start (no tape, so no typist), replayed
   separately; the screenshots at the same frames are identical. This proves that, given the
   same deterministic input, the bundle's replay reaches the frames an established mechanism
   (RZX, read by libspectrum in the existing RZX rows) reaches. `.jds`: the same construction
   with `--record-script`, `--rtc` on both the recording and the replay (`.jds` does not replay
   RTC values), and no typist (the `.jds` recorder samples the matrix and would double its keys).
5. **Across frontends and platforms.** Record in the Qt GUI and in the SDL GUI under Xvfb
   (`xvfb-run -d`, `-noreset`, `xdotool` keys and menu clicks, the existing pattern), replay
   headless; the digests match. Container and path rows run under wine (`make unit-test-win`).

Functional rows (`functional_tests.conf`): `bug-report-headless-func`, `bug-report-qt-func`,
`bug-report-sdl-func`, `bug-report-nextzxos-func` (`private-sd`: a NextZXOS session on the run's
own clone, replayed with the base found by digest), `bug-report-nocard-func`,
`bug-report-net-func` (the ESP against a loopback peer the row starts),
`bug-report-rzx-oracle-func`, `bug-report-jds-oracle-func`, `bug-report-mutation-func` (the
mutation table), `bug-report-capture-func` (the capture table), `bug-report-privacy-func` (the
byte check over every member of the NextZXOS bundle finds no distribution run; no host path; no
debug-level log line), `bug-report-replay-speed-func` (re-measures the replay speed of section 3
on `build/gui-release-non-pgo`).
Bundles a row makes live in its `$TMP_DIR` and die with it. **No bundle of a NextZXOS session is
ever committed**; a committed fixture bundle is allowed only for a `--no-sdcard` session of a
licence-clean demo.

### B5.5 Documentation (sub-issue 13)

A user guide page (how to record, save, what goes in, how to attach to an issue), the man page
options and EXIT STATUS, the developer guide chapter (the seams, the log, the card capture, the
replay, the proof), FEATURES.md, and the ChangeLog line at landing.

---

## 16. Risks

| Risk | Mitigation |
|---|---|
| A seam the inventory missed: a source that changes the machine without being logged | B0.1's rows per entry point; the probe program and the mutation rows catch the effect even when the seam is unknown (the replay diverges) |
| Long replays of long NextZXOS sessions | segments restart at every reset and load; local keyframes (B6.1); landing points |
| The distribution index cannot be built on the reporter's machine (raw image deleted, never provisioned) | the provisioner offers to fetch the distribution; without the index the writer ships no content member at all (5.3) and the review says so |
| Recording overhead on slow hosts | Q2's measured condition; the rolling recording can be turned off |
| A new guest-visible effect of `audio_off_` | BRB-CFG-05 decides; until then it is a machine field |
| Copyright leak through a member other than the card (a program from the distribution, a host file that is a NextZXOS file, a debugger load, a log line) | the byte check over every content member (B2.1), floor 95 bytes |
| Firmware the index does not hold, or a copy altered at least once every 94 bytes (another NextZXOS version, relocated or patched code, re-encodings, compression) | system paths, archives, snapshots and distribution-derived files hashes only on every base; every user file and served host file only when ticked (Q14 default); the residuals stated as rules (5.3.5) |
| The index matches padding and every card bundle becomes hashes only | the padding exclusion E1 (period 4 or less) and trivial sectors (5.3.1), with the BRB-CARD-30 rows |
| An exclusion criterion drops firmware from the index | E1 provably removes only short-period fills with at most two stray bytes per window (5.3.1's bound); the clean corpus links code and z88dk run-time data only and drops only windows that occur in its builds (measured: 0 of every ROM, 1 of `TBBLUE.FW`, every system-path file pinned); BRB-FILE-40 checks every system-path file against the real index at run time |
| The check refuses licence-clean programs, so bundles lose the program | the clean corpus, E1 with two exceptions and the NEX header layout check (Q18), measured to 0 of 27 sources leave-one-source-out; BRB-FILE-41 pins it |
| The clean corpus carries third-party data (z88dk's `font_8x8_zx_system` is the 48K character set) | code-only sweeps with third-party data excluded by name, a per-file licence check before commit, BRB-FILE-42 (no window shared with any ROM or the character set) |
| Content hidden by a member's storage layout (records, JSON, compression) | the check runs on content as the guest saw it (5.3.2), with rows planted into the payloads (BRB-FILE-20) |
| The pristine rebuild is not byte-reproducible on some host | BRB-CARD-20 on every platform the unit suites run on; the save side records the digest, so a difference refuses instead of drifting |
| The developer must hold the reporter's jnext version for firmware bundles | the refusal prints `git describe` and the build command; `--bug-report-cross-version` for the deliberate case |

---

## 17. Owner decisions (2026-11-20)

The owner answered every question on 2026-11-20 (about 05:50). Numbers are kept from the drafts
(Q6 was withdrawn in revision 1, Q8 replaced by Q14). Q14 to Q18 were read together, since they
jointly decide how often a bundle carries the reporter's program and files.

| # | Question | Decision |
|---|---|---|
| Q1 | Epic shape | **Yes**: #492 is an epic, branch `epic492-bug-report-bundle`, with its sub-issues (13, plus the two B6 ones). Scheduling sub-issue 14 (keyframes) after the v1 sub-issues is the planner's interpretation, for the owner to confirm; sub-issue 15 is later by Q10. |
| Q2 | Rolling recording on by default | **On, if the measured cost is at most 1 %** (`make bench`, B0.3), off otherwise; exempt from the fast-boot "off when recording" rule. |
| Q3 | Machine memory in bundles | **As proposed**: no machine memory for any firmware session and no `.jns` anchor; the local replay-side keyframe cache (B6.1); shipped keyframes only for `--no-sdcard` sessions, under the byte check and the keyframes tick (B6.2). (Q3 did not say when; marking B6.1 and B6.2 "later" is the planner's interpretation, for the owner to confirm.) |
| Q4 | The program file | **Included by default** (a distribution file is a reference), after the byte check, listed in the review, with an untick that keeps only its SHA-256. |
| Q5 | File name | **Changed**: `<name>.YYYYMMDDhhmm.jnext-bug.zip`, the current date and time at save; still recognised by content. The `<name>` rule, the allowed character set, local time, the `-2` suffix, `--bug-report-record` taking a directory and the save dialog's "last directory used" are the planner's interpretation, for the owner to confirm (section 4). |
| Q6 | (withdrawn in revision 1) | The warm-start recorder's RTC was already pinned (b93c03f79). |
| Q7 | Default card mode | **Keep the in-place default**; capture through the in-memory undo log. |
| Q8 | (replaced by Q14) | |
| Q9 | Version policy on replay | **As proposed**: firmware bundles refuse under another version, printing the reporter's `git describe` and the build command; `--bug-report-cross-version` forces; bundles with no firmware in the anchor warn and replay, `--bug-report-strict` refuses. |
| Q10 | The browser build | **Not in v1** (B6.3 later). |
| Q11 | NextPi I2S routed to EAR | **Not replayable; warn** from the first sample. |
| Q12 | A hotkey for Save Bug Report | **Menu only.** |
| Q13 | Memory cap and segments | **64 MiB** (the undo log counts toward it), **latest segment only**; at the cap the recording stops with a warning. |
| Q14 | The copyright residuals | **Option (a)**: every content channel ships only when the reporter ticks it (one tick per user file on every base and per served host file, one "network data", one "debugger uploads", one "keyframes"); nothing ticked by default except the program (Q4). |
| Q15 | Settings files under system paths | **Allowlist `/MACHINES/NEXT/config.ini` only**, under the testable criterion (under 4 KiB, printable ASCII with TAB, CR and LF, `key=value` or comment lines, passes the byte check); every other system-path file hashes only. |
| Q16 | Disk for the replay base | **Cache it** at `~/.jnext/sdcard/bug-report-bases/<sha256>.img`, **with the forget option** (`--bug-report-forget-bases` and a Preferences button). |
| Q17 | The classic ROMs' character set | **Keep it indexed (strict)**; the clean corpus may never drop its windows (BRB-FILE-42). |
| Q18 | False positives on z88dk programs | **Option (a)**: E1 with up to two stray bytes, the NEX header checked by layout, and a code-only clean corpus of linked z88dk builds of jnext's own sources (third-party data excluded by name, licence check per file before commit), committed, shipped and intersected on the reporter's machine. The owner accepts the line it draws: it ships byte strings of z88dk's own code that also occur in z88dk-built distribution programs. The package cost is measured by B1.11 and recorded here. |

The option texts the owner chose from are in the revision history of this file (revision 5 and
the round-6 notes, commits 5f1de86ff and 360798539).

---

## 18. Decisions taken in this plan

| Decision | Why | Rejected |
|---|---|---|
| An own log in the bundle, not RZX | RZX cannot describe a Next (the man page explains why jnext refuses it) | extending RZX: its value log has no ports and its snapshots no Next state |
| Not the `.jds` script as the log | frame-granular, no mouse, no pulls or streams, a text DSL for people | extending `.jds`; a `.jds` export of the input part stays possible later |
| Capture at the host seams | machine-internal typists re-run in a replay; sampling the matrix would apply their keys twice | `InputState` sampling per frame (the `.jds` recorder's way) |
| Cycle stamps from the anchor | places a change made while paused mid-frame exactly, and orders pulls inside a frame | frame numbers |
| No memory in a firmware-session bundle | rule 1: RAM holds NextZXOS and DivMMC firmware | shipping a `.jns`; excising ROM pages only (NextZXOS code in RAM banks and DivMMC RAM would remain) |
| The card as input sectors with per-read hashes; bytes only for files that pass the per-file classification and the byte check | exact for the guest, minimal in size; the check sees modified, shifted and unaligned copies down to 95 bytes | exact whole-sector deduplication alone (revision 0: misses modified files, slack and shifted content); a whole-image digest only; shipping the #507 journal; a file-level diff (not sector-exact) |
| System paths hashes only on every base | a user's replaced NextZXOS file is still NextZXOS | the rule for unknown bases only (revision 0) |
| A byte-granular window index of distribution files, stride 32, window 64 | a fixed, provable floor (95 bytes) over every member at every offset, about 40 MiB of index | an aligned 512-byte check (revision 0: blind to every unaligned member); a full suffix index (far larger for a lower floor nobody needs) |
| The check runs on content as the guest saw it, per source, decoded | the floor needs the shared run contiguous in the scanned bytes; records, per-read framing, JSON and inner compression break that | scanning members' storage bytes (revision 1: blind to RX records, small `F_READ`s, JSON payloads, `.szx`/`.z80`/zip compression) |
| E1 allows two stray bytes per window (revision 5) | the proof supports two and no more (the overlap keeps a 6-byte clean stretch); one licence-clean program shares a two-stray-byte fill with `TBBLUE.TBU` | one (revision 4: `test07joystick` refused); three (the proof fails) |
| The NEX header checked by layout, not scanned (revision 5) | it is fields the format defines; two licence-clean programs matched distribution NEX headers | matching headers through the clean corpus (it cannot cover every header) |
| The clean corpus links code and z88dk run-time data only; third-party data excluded by name; licence check per file | z88dk's `font_8x8_zx_system` is byte-identical to the 48K ROM character set, and the library holds other vendors' ROM fonts | linking every public symbol (revision 4's wording) |
| Leave-one-source-out, deduplicated by SHA-256, for the false-positive pin | byte-identical twins cleaned their own copies (round 5: 0 by file, 3 sources by source) | leave-one-file-out (revision 4) |
| E1 allowed one stray byte per window (revision 4, superseded by two) | every TAP false positive was a fill with exactly one different byte; the bound stays provable (one stray byte per 32 bytes at most) | no exception (revision 3: 14 of 15 TAPs refused) |
| A clean corpus of linked z88dk builds of jnext's own sources, intersected on the reporter's machine | the shared runs are z88dk's NEX header and run-time data, present in z88dk-built distribution programs; a dropped window is by construction 64 bytes of a clean build | the z88dk library archives (measured: unlinked objects explain none of the refusals); E2-style frequency rules (round 3); refusing every z88dk program |
| One padding criterion, E1 (no E2): short-period fills | provably removes only fills (an escaping run holds at most 62 non-fill bytes plus the fill's pattern); without it every directory, FAT and padded sector would hit | no exclusion (revision 1); E2, "in more than 16 files" (revision 2: measured to drop 413 of 511 `48.rom` windows, because the distribution repeats the 48K ROM); "fewer than 8 distinct values" (revision 2: matches fonts, 1-bit artwork and sparse tables) |
| Distribution files in decodable formats indexed as stored and decoded | the check decodes members, so it must meet decoded originals | indexing stored bytes only |
| `.z80` re-serialised with pages 3-10 only | the loader skips pages 0-2 and 11, which can hold the 48K, Interface 1 / +D, 128K and Multiface ROMs | shipping the file verbatim (revision 2) |
| Every content channel under a tick by default (Q14 (a)) | the check cannot see altered copies in any channel, so shipping is the reporter's choice for each | ticks for files only (revision 2) |
| Free space and non-filesystem sectors: reference or hashes only | free clusters and the pre-partition gap can hold old firmware on a real Next card | no class (revision 1) |
| FSInfo shipped only when its layout checks | it changes with every write, so hashes only would stop most replays at mount; a layout check proves it holds only two counts | hashes only for the whole reserved area |
| Every user and served host file only when ticked, on every base (Q14 default) | the check cannot see altered copies, so shipping is the reporter's explicit choice | user files by default on a known base (revision 1) |
| Programs in formats with no decoder are references only | the check cannot see bytes encoded as pulses | shipping them unchecked |
| The replay base is the pristine rebuild at its own cached path under `~/.jnext/sdcard/bug-report-bases/` | the developer's working card differs in FAT, directories and settings; references need pristine sectors; a separate path cannot collide in the journal registry; images live only under `~/.jnext/sdcard/` | the developer's working card (revision 0); a fresh 1 GiB copy per replay; a cache outside `~/.jnext/sdcard/` (revision 1, against the image rule) |
| The extended-NEX overlay re-derived from the program | it serves the program's own bytes, so shipping them in a queue would bypass the program's reference | logging overlay reads as a value queue (revision 0) |
| `.szx` re-serialised to the chunks jnext reads | the loader skips unknown chunks silently, which can carry a ROM or interface EEPROM | shipping the file verbatim |
| The warm start's existing RTC pin (b93c03f79) is relied on, not re-done | rows E and G already prove it | a `kRecordingVersion` bump (revision 0: would re-record every user's cache for nothing) |
| CRC-32 checkpoint digests, SHA-256 at the end | already linked, about 1 ms per 2 MiB; a divergence persists over many checkpoints | SHA-256 per checkpoint (several times the cost for no gain in detection) |
| ZIP container, JSON for low-rate members | the `.jns` precedent: readable with `unzip` and `jq`, CRC per member | a new binary container |
| Rewind while recording truncates the log, and the fast-forward is fed from the log | the rewound state is the recorded one, so nothing needs storing; feeding the fast-forward makes it true for the ESP and the RTC too | storing the abandoned branch |
| The replay runs normal frames, never `replay_mode_` | `replay_mode_` skips the render and the sprite side-effect pass | reusing the rewind's fast-forward mode for speed |
| Firmware bundles need the reporter's version; the refusal prints the build command | the warm start is regenerated by the developer's jnext and digested over the full stream | promising cross-version replay of firmware sessions (revision 0's Q9) |
| Machine-class options refused with `--bug-report-play` | the bundle brings the machine; an override would replay another machine | silently overriding them |

---

## 19. Revision log

**Revision 1 (2026-11-20), after the independent review (REJECT, eight findings), each checked
against the code first:**

1. The warm start was not "unreproducible": `warm_start_boot_config()` pins the recorder's RTC
   since b93c03f79 (rows E and G). B0.5, the `kRecordingVersion` bump and Q6 are gone; §2, §6
   and §18 corrected; B0.6 gains BRB-WS-06 (host-class settings do not change the recording).
2. The copyright mechanism is rebuilt (§5.3): a distribution index with a byte-granular window
   check over every content member (floor 95 bytes, slack handled), per-file classification with
   the system-path rule on every base, archives and guest snapshots hashes only, the `.szx`
   chunk filter, the extended-NEX overlay re-derived, no content member without an index,
   debugger loads and the log under the check; the residual is stated and put to the owner (Q14,
   Q15).
3. The replay base is the pristine rebuild at its own cached path (§5.5), references resolved
   from it, BRB-CARD-20 (byte-identical rebuild) and BRB-CARD-21 (no registry collision).
4. GUI runtime writes (CPU speed menu and Preferences apply on NR 0x07, the tape toggles) are a
   seam class in B0.1, B1.2, B0.4 and §6.
5. The version rule is stated as it is: firmware bundles need the reporter's version (§8, Q9).
6. The RZX and `.jds` oracles are two recordings of one scripted, RTC-pinned input with no typist
   (§15), consistent with §7.2's exclusions.
7. Every source has a capture row (live source different or absent) besides its mutation row,
   and the probe NEX reads every source inside every checkpoint interval (§6, §15).
8. The Preferences panel and its `config_keys.cpp` rows are one sub-issue (11).

Also folded in: the RTC latches at every I2C START; the undo log is memory-only and counted in
the cap; the rolling recorder does not disable fast boot (Q2); the `ATE1` echo puts typed AT
commands in the RX stream (§9); the log is info level only and under the check; replay-side and
machine-class options with `--bug-report-play` (§7.2); the stand-in never writes the host on
replay; the speed figures are historical; the rewind rule's per-frame-slot assumption is asserted
(BRB-TL-06); the warm-start replay order (§5.6).

**Revision 2 (2026-11-20), after review round 2 (REJECT, five findings), each checked against
the code first (`szx_loader.cpp`: `RAMP` flag bit 0 means zlib, `SCLD` deliberately ignored):**

1. R2-1: the byte check runs on content as the guest saw it, reconstructed per source and
   decoded (5.3.2): RX sequences per UART, host files rebuilt from all their reads, debugger
   memory payloads moved out of `events.jsonl` into `debugger/writes.bin`, `.szx` `RAMP` pages
   uncompressed, `.z80` expanded, zip program archives inflated, formats with no decoder
   references only. BRB-FILE-20 plants the runs into the payloads.
2. R2-2: low-information exclusion E1 (under 8 distinct values) and E2 (in more than 16
   distribution files), trivial sectors, the floor restated to match, the BRB-CARD-30 rows
   (directory, FAT, zero-padded file and zeroed slack pass; a planted run in padding is found;
   both boundaries), and the classic ROM character set false positive stated, with Q17.
3. R2-3: free-space and non-filesystem classes (reference when pristine, otherwise hashes only,
   on every base), and FSInfo shipped only when its layout checks.
4. R2-4: the residuals stated as rules (5.3.5): the floor bounds only verbatim copies, a copy
   altered once every 94 bytes passes (relocation, patches, re-encodings, compression), and
   default-shipped user files on a known base were a hole. Q14 now covers every base and
   recommends the strict option (a).
5. R2-5: the pristine rebuild is cached under `~/.jnext/sdcard/bug-report-bases/` (owner
   decision: comply with the image rule); the save side uses that cache or a per-run scratch
   copy; Q16 updated.

Also folded in: the chunk list (`Z80R`, `SPCR`, `RAMP`, `AY` after the `ZXST` header; `SCLD`
dropped with its reason); exactly what a bundle holds with no index (5.3.4); Q15's cost, with a
testable `config.ini`-only allowlist as the default; the shared rolling hash pinned in B1.9.

**Revision 3 (2026-11-20), after review round 3 (REJECT, three findings), each checked against
the code first (`z80_loader.h`: pages 0-2 and 11 skipped, `decompress_v1` for v1) and the first
re-measured:**

1. R3-1: E2 is dropped and E1 tightened to windows of period 4 or less, which provably removes
   only fills (an escaping shared run holds at most 66 non-fill bytes; revision 4 tightens this to 62). Measured on the local
   `sn-emulator-26.11` card (250 system-path files, read into memory only, nothing kept): revision
   2's criteria left 62 of 511 `48.rom` windows and 49.6 % overall; revision 3's leave 476
   (93.2 %, the rest the 0xFF fill) and 63.5 % overall, every file with at least one indexed
   window, all 23 character-set windows indexed. Distribution files in decodable formats are
   indexed decoded too. New row BRB-FILE-40 checks every system-path file against the real index
   built at run time, copied into a user file, a served host file and an RX stream. Q17's premise
   restated.
2. R3-2: a `.z80` anchor is re-serialised with pages 3-10 only; the v1 wording corrected (one
   block for the whole 48K); a BRB-FILE case added.
3. R3-3: 5.3.5 gains residual 4 (the RX streams, debugger uploads, the program and shipped
   keyframes); Q14 (a) puts each under a tick ("network data", "debugger uploads", "keyframes";
   the program keeps Q4's default tick), and (z) drops them all.

Also: the backup FSInfo sector is named (Non-filesystem, beside the backup boot sector); the TZX
block types with a decoder are named in the program row.

**Revision 4 (2026-11-20), after review round 4 (REJECT, one finding), measured before
writing:**

1. R4-1: false positives measured in memory (card files through `mtype`, the corpus read from
   the repository; no extraction, scratch deleted): revision 3's index refused 38 of 38 tracked
   `demo/` `.nex`, 14 of 15 `.tap` and 9 of 9 dapr programs. The shared runs are the z88dk NEX
   header, z88dk newlib run-time data (`__thrd_id`, `__stdio_block` of `zxn_crt.asm.m4`, shared
   with `TBBLUE.TBU`, `/dot/NextPiUI` and z88dk-built applications), coinciding palette data, and,
   in the TAPs, fills with one stray byte. The z88dk library archives explain none of them. Remedy
   (Q18 (a)): E1 allows one stray byte, and windows that occur in a clean corpus of linked z88dk
   builds of jnext's own sources are dropped; measured leave-one-out: 0 refused in all three sets,
   at a firmware cost of 0 `48.rom` and `enNextZX.rom` windows, 1 `TBBLUE.FW` window, 307 of
   `TBBLUE.TBU` and 12 of `/dot/NextPiUI`. Why it cannot readmit firmware, and what it means for
   firmware that links the same run-time, are in 5.3.1; residual 5 added. New task B1.11, new row
   BRB-FILE-41 (refusal count pinned at 0), new Q18; Q4 now says it depends on Q18.
2. The bound is stated exactly: at most 62 non-fill bytes plus the fill's pattern (revision 3's 66
   was loose), plus, with the one-stray-byte E1, one byte per aligned window.

Also: BRB-FILE-40 plants random 95-byte slices with a printed seed, names all-fill system files
instead of failing on them, and checks that the clean corpus drops no ROM or `TBBLUE.FW` windows
beyond the recorded counts; 5.3.1 says what covers a distribution file under 95 bytes copied
outside the system paths (only its name and size, so a renamed one is a ticked user file).

**Revision 5 (2026-11-20), after review round 5 (REJECT, two findings), measured before
writing (in memory: card files through `mtype`, programs and z88dk files read from disk; nothing
extracted, scratch deleted):**

1. R5-1: confirmed that z88dk 2.4's `font_8x8_zx_system.bin` equals `48.rom[0x3D00:0x4000]`; its
   windows occur in 24 distribution ROM files. The clean corpus now links code and z88dk's own
   run-time data only, every third-party data object excluded by name (the `font/` tree, `_font_*`,
   `_zx7_font_*`, FZX fonts, any ROM-derived or vendor table B1.11 finds); B1.11 records a licence
   check of every linked file in this design before anything is committed; committing or packaging
   proprietary data is stated as forbidden (PULL-REQUEST-PROTOCOL rule 3, the owner's rule); the
   drop count of **every** system-path file is pinned in BRB-FILE-40 (5.3.1's table: 0 for 234 files
   and every ROM, 1 for `TBBLUE.FW`, 19 for `TBBLUE.TBU`, 11 for `/dot/NextPiUI`, a few dot commands);
   new row BRB-FILE-42 (the clean corpus shares no window with any ROM or the character set); the
   costs are marked as measured before the probes and sweeps exist, to be re-measured by B1.11; the
   doc states plainly that the remedy ships byte strings of z88dk's own code that also occur on the
   card, and Q18 puts that line to the owner. Q17 can no longer be decided by the clean corpus.
2. R5-2: re-measured leave-one-source-out, deduplicated by SHA-256: 62 files are 43 distinct
   contents from 27 sources; revision 4's remedy left 3 sources refused (`test01l2empty` and
   `test02layer2` on their NEX headers, `test07joystick` on a two-stray-byte fill in its own
   `_row0` table). Closed by checking the NEX header by layout instead of scanning it, and by E1
   allowing two exceptions (the most the proof supports, re-proved in 5.3.1): 0 of 27. BRB-FILE-41
   now leaves out every build of the source and pins 0 of 27.

Also: Q18 names the package cost as not yet measured, and says that option (c) brings the refusals
back for every reporter without z88dk, most non-developer users; the coverage table gains revision
5's column (60.8 % overall, every ROM count unchanged or within 7 windows).

**Round 6 (2026-11-20): APPROVED.** Its wording notes folded in: every byte after the NEX header
is scanned, and the header's free fields are a residual of a few tens of bytes; the bound reads
"at most two arbitrary bytes per 32"; option (z) also closes residual 5, and Q18 (d) is its
program-only equivalent; a reading note asks for Q14 to Q18 to be read together.

**Owner decisions (2026-11-20).** Every question answered; section 17 now records them. Q5
changed the file name to `<name>.YYYYMMDDhhmm.jnext-bug.zip` with the current date and time at
save, applied to section 4, the save dialog, the CLI and the tests (BRB-FILE-01's name rows).
Q12's "menu only" removes the hotkey mention from section 7.1. Every other answer took the
recommended default.

The following were filled in by the planner, not decided by the owner, and are flagged in the
text as **the planner's interpretation, for the owner to confirm** (review round 7, R7-1): the
`<name>` rule (program base name, `jnext` with no program), the allowed character set, local time,
the `-2` suffix for a second save in the same minute, `--bug-report-record` accepting a directory,
the save dialog proposing the last directory used, and scheduling sub-issue 14 (keyframes) after
the v1 sub-issues.

**Parked (2026-11-20).** The owner parked #492 ("too much work for the little reward"): no
sub-issues are filed, the issue leaves v1.4, and this design and its decisions are kept for a
later restart.
