# Bug-report bundle: one file to replay a failing session locally (GH #492)

Status: **DRAFT for owner review** (2026-11-20, planner of the 2026-11-17 autonomous run).
Nothing in this plan is started and no sub-issue is filed. When the owner approves it, the
sub-issues of the task list are opened under #492. The plan assumes #492 becomes an epic, on
the branch `epic492-bug-report-bundle`: each sub-issue branches off it and merges back into it,
and the epic reaches `main` in one merge (CLAUDE.md, merge protocol step 1; see Q1).

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
   rebuild from the developer's own card, and replays everything after it (section 3).
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

- [ ] **B0.1** Seam inventory: every place host state enters the emulated machine (inputs, host actions, debugger mutations, in-frame pulls, UART byte streams, card sectors, configuration), one recorder-observable seam per class; the table goes into the developer guide
- [ ] **B0.2** Session log core (`src/bundle/session_log.*`): anchor-relative cycle stamps, the event list, the value queues, the byte streams, seekable cursors, the memory cap
- [ ] **B0.3** Checkpoint digests: observable digest (CPU registers and all of RAM), framebuffer digest, full state-stream digest, every N frames

#### Sub-issue 2: the machine configuration a replay needs

- [ ] **B0.4** `EmulatorConfig` field table: every field classified machine / host / debug / excluded (with the reason); a completeness tripwire; serialise and restore the machine class by name

#### Sub-issue 3: a reproducible warm start

- [ ] **B0.5** Warm-start recording with a pinned RTC (`kRecordingVersion` 2), so the recording is a function of the card, the machine and the jnext version only
- [ ] **B0.6** Warm-start read set: the recording keeps the number, hash and bytes of every sector its boot read first, and its state digest, for a bundle to name

### Phase B1: recording

#### Sub-issue 4: inputs and host actions

- [ ] **B1.1** Inputs at their seams: keyboard matrix and extended keys, both joystick connectors, the Kempston mouse (deltas, buttons, wheel), each with the cycle it was applied at
- [ ] **B1.2** Host actions: soft reset, NMI buttons (Multiface, DivMMC), drive button, tape deck (insert, play, stop, rewind), SD insert and eject, POK trainers, the `--delayed-*` actions, ESP associate and disassociate
- [ ] **B1.3** Debugger mutations: every verb behind `Debugger::Impl::note_mutation()` logged with its arguments (memory, registers, NextREGs, port writes, loads into memory)
- [ ] **B1.4** Timeline rules: anchors (power-on, hard reset, program load, machine change) start a new segment; a rewind truncates the log at the landing cycle; a quick snapshot taken in the segment and loaded back is a jump to that cycle; any other snapshot load is a new anchor or ends the segment

#### Sub-issue 5: in-frame pulls and byte streams

- [ ] **B1.5** Value queues for in-frame pulls: RTC reads (`i2c.cpp`), the esxDOS stand-in's host calls (open, read, stat, directory listing, time), the extended-NEX host reads
- [ ] **B1.6** UART RX tee: every byte the ESP-01, the NextPi UART and the joystick-port UART (fifo, pty) delivered into an RX FIFO, with its cycle; the TX side kept as a running digest only
- [ ] **B1.7** Rewind fast-forward while recording is fed from the log (RX bytes, RTC values, host calls), so a rewind lands on the recorded timeline even in a network session

#### Sub-issue 6: SD-card capture

- [ ] **B1.8** First-access tracking per card: the input-sector set (sectors whose first access after the anchor is a read) with each sector's hash, and an undo log (the pre-write bytes of a sector's first write)
- [ ] **B1.9** The pristine reference: the card's base identified as a known distribution image (the fixed-image recipe, the minimal-card recipe with its `dirs`), or as unknown; its sector-hash set, cached locally
- [ ] **B1.10** Card member: the input sectors not found anywhere in the pristine reference, as bytes; the rest as references; FAT path attribution of every included sector for the privacy review; the second card the same way

### Phase B2: the bundle file

#### Sub-issue 7: container writer and reader

- [ ] **B2.1** Container: a ZIP through `src/save/zip_archive.*`, `manifest.json` plus members (section 4), deflate, bounded sizes, a writer self-check that refuses a member holding a 512-byte block of the pristine reference
- [ ] **B2.2** Reader: validation, version rules (section 8), the privacy summary, `--bug-report-info FILE`

### Phase B3: replay

#### Sub-issue 8: the replay engine

- [ ] **B3.1** Rebuild the anchor: power-on, program load, warm start (regenerated and checked against its digest), a classic snapshot file; card assembly as a scratch journal bound by digest over the developer's base image (the #507 mechanism)
- [ ] **B3.2** Inject the log at its cycles with every live host injector off; replay devices for the value queues and the UART streams; per-sector read checks on the card
- [ ] **B3.3** Verification: checkpoint digests, first divergence reported with its frame and cycle, version-mismatch report, a distinct exit status headless
- [ ] **B3.4** End of the log: the machine pauses (GUI) or exits (headless), and continuing makes it live

#### Sub-issue 9: replay with the debugger and rewind

- [ ] **B3.5** Seekable replay cursors: a rewind or a step back during a replay re-feeds the log from the landing cycle; a debugger mutation during a replay warns that the rest may drift
- [ ] **B3.6** Landing points: replay to frame N, or to N seconds before the save point, and pause there with the rewind ring filled behind it

### Phase B4: user interface

#### Sub-issue 10: GUI

- [ ] **B4.1** File > Bug Report menu: Start Recording, Save Bug Report..., Stop Recording, Open Bug Report...; status-bar indicator
- [ ] **B4.2** Save dialog with the privacy review (what goes in, sizes, the card files, the network hosts, notes); open dialog with the summary and the mismatch warnings; replay progress
- [ ] **B4.3** Preferences > Bug Reports panel: rolling recording, memory cap, checkpoint interval, include the program, include the log

#### Sub-issue 11: CLI and configuration keys

- [ ] **B4.4** `--bug-report-record`, `--bug-report-play`, `--bug-report-info` and the option set of section 7; `bug_report.*` rows in `config_keys.cpp`; man page; `make cli-check`
- [ ] **B4.5** SDL-only frontend and `--headless` parity: record, save at exit, replay, the same exit statuses

### Phase B5: end-to-end proof and documentation

#### Sub-issue 12: determinism proof rows

- [ ] **B5.1** The probe program: a NEX (source in `demo/`) that folds every source of section 6 into its state, so a replay that loses one source diverges
- [ ] **B5.2** Functional rows: record and replay headless; record in the Qt GUI and the SDL GUI under Xvfb, replay headless; a NextZXOS session on the run's own card; a `--no-sdcard` session with no card at all; a network session against a loopback peer
- [ ] **B5.3** Oracle rows: RZX and `.jds` cross-checks on classic machines and for the keyboard; mutation rows, one per source, each of which must make the replay report divergence at the expected checkpoint
- [ ] **B5.4** Windows rows under wine (paths, the container) and the copyright and privacy rows (no pristine block in any member, no host path in the manifest)

#### Sub-issue 13: documentation

- [ ] **B5.5** User guide page "Reporting a bug with a bundle", man page sections, developer guide chapter (the seams, the log, the card capture, the replay), FEATURES.md

### Phase B6: optional, owner-gated

#### Sub-issue 14: keyframes (only if Q3 says yes)

- [ ] **B6.1** Local keyframe cache on the replay side (never shipped): a second open of the same bundle jumps to the nearest keyframe
- [ ] **B6.2** Shipped keyframes for sessions with no firmware in memory (`--no-sdcard`); firmware-excised keyframes only on the owner's say-so (Q3)

#### Sub-issue 15: the browser build (only if Q10 says yes)

- [ ] **B6.3** Record and replay in a web page: the minimal card by `cardSha256`, `dirs` and recipe; JavaScript save and open

---

## Sub-issues and parallelization

Each sub-issue branches off `epic492-bug-report-bundle` and merges back into it. "Depends on"
means the branch cannot be finished before those are merged into the epic branch.

| Sub-issue | Tasks | Depends on | Can run in parallel with |
|---|---|---|---|
| 1 Seams and session log | B0.1, B0.2, B0.3 | none | 2, 3 |
| 2 Machine configuration | B0.4 | none | 1, 3, 4, 5, 6 |
| 3 Reproducible warm start | B0.5, B0.6 | none | 1, 2, 4, 5 |
| 4 Inputs and host actions | B1.1, B1.2, B1.3, B1.4 | 1 | 2, 3, 5, 6 |
| 5 In-frame pulls and streams | B1.5, B1.6, B1.7 | 1 | 2, 3, 4, 6 |
| 6 SD-card capture | B1.8, B1.9, B1.10 | 1, 3 (B1.9 needs neither) | 2, 4, 5 |
| 7 Container | B2.1, B2.2 | 2, 4, 5, 6 (the writer can start on 1's log) | none |
| 8 Replay engine | B3.1, B3.2, B3.3, B3.4 | 7 | 10, 11 |
| 9 Replay with debugger and rewind | B3.5, B3.6 | 8 | 10, 11, 12, 15 |
| 10 GUI | B4.1, B4.2, B4.3 | 7 (the replay dialog needs 8) | 8, 9, 11, 12, 14, 15 |
| 11 CLI and keys | B4.4, B4.5 | 7 (the replay options need 8) | 8, 9, 10, 14 |
| 12 Proof rows | B5.1, B5.2, B5.3, B5.4 | 8, 11 (B5.1, the probe program, needs nothing) | 9, 10, 14, 15 |
| 13 Documentation | B5.5 | 9, 10, 11, 12 | 14, 15 |
| 14 Keyframes (Q3) | B6.1, B6.2 | 9 | 10, 11, 12, 13, 15 |
| 15 Browser (Q10) | B6.3 | 8, 11 | 9, 10, 12, 13, 14 |

The parallel column lists no sub-issue that is a direct or indirect dependency or dependant of
the row. Work that can start early: B1.9 (the pristine sector-hash set) needs neither 1 nor 3;
the B5.1 probe program can be written on day one and is useful to every sub-issue's own rows;
the GUI and CLI parts that do not open a bundle (sub-issues 10 and 11) need only 7, and their
replay dialog and replay options are finished after 8.

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
  `src/platform/render_policy.h`, `tools/make-minimal-card/`.
- Documents: the man page (`doc/man/jnext.1.md`: SD-card options, `--rtc`, Recording and
  playback, `--record-script`, `--fast-boot`, EXIT STATUS), `doc/design/NEXT-SNAPSHOT-FORMAT.md`
  (sections 2, 8, 11), `test/bench/` baselines and `doc/design/TASK27-FINAL-OPTIMIZATION-REPORT.md`
  (headless speed).

Measured or read facts this plan rests on are cited where used. Nothing here was run on the
host: the 1-minute load was above the run's admission threshold (15.3 against 12) and no
measurement in this plan needs a fresh run to make a design decision. The two numbers that do
need one (the recorder's overhead for Q2, the replay speed) are tasks with their own rows.

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
| Warm start | A recording of a real boot, keyed on card size and mtime (or digest), machine, format and procedure versions | **Not reproducible elsewhere**: the boot reads the host RTC (`I2cRtc::snapshot_time()` in `i2c.cpp` latches `std::localtime(std::time(nullptr))` on every register read), so two recordings of the same card differ. The bundle must name it, not ship it (it holds firmware). |
| `--rtc` | A frozen clock | A frozen clock does not advance, so it changes what the reporter sees. Fine as a pin, not as the default for recording. |
| ESP-01 | AT engine; the threaded wrapper releases bytes to the guest at the wire's cadence | Which bytes exist at a given cycle depends on the network and on the worker thread (`esp_threaded.h`). SNTP and ping use host time. Not serialised (simplification 5 of the ESP design). |
| Mouse, joysticks | Host deltas and buttons into `KempstonMouse`; host pads into the connectors | Arrive between frames from host events: non-deterministic in time, deterministic once applied. |

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
| `warm-start` | a fresh warm-start recording on the replay card, checked against its digest (B0.5, B0.6) | Built locally | Identity + digest |
| `snapshot` | power-on + a `.sna` / `.szx` / `.z80` file (classic machines; they hold no ROM) | No | The file |

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
371 frames/s in the July baseline. A ten-minute NextZXOS session replays in under two minutes; an
hour in about ten. That is the price of rule 1. Two things keep it low: a session's segment
starts again at every hard reset and program load (B1.4), so most bundles are short; and the
replay side keeps local keyframes, so a second open of the same bundle is quick (B6.1, never
shipped).

---

## 4. Contents

The file is a ZIP (the `.jns` container's writer and reader, `src/save/zip_archive.*`), named
`<name>.jnext-bug.zip` (Q5: GitHub accepts `.zip` attachments and refuses unknown extensions)
and recognised by its `manifest.json`, not by the extension. `--load` of a `.zip` today means a
program archive (`program_archive.cpp`), so the bundle check runs first.

| Member | Format | What |
|---|---|---|
| `manifest.json` | JSON | `kind: "jnext-bug-report"`, `format_version`; producer (jnext version, `git describe`, platform, build type, PGO or not); `created` (UTC); machine; the state-format versions (warm-start `kFormatVersion` and `kRecordingVersion`, the `.jns` `state_model_revision`, the state-stream length); `nextboot.rom` SHA-256; ROM digests (`jns::` ROM identity); anchor; segment length in frames and cycles; checkpoint interval; card identities (section 5); the warnings the recorder raised; the privacy summary (section 9) |
| `config.json` | JSON | The machine class of `EmulatorConfig`, field by name (B0.4); paths reduced to bundle members |
| `program/<name>` | bytes | The program as the loader read it, unless excluded (Q4) or found in the distribution (then a reference: distribution, path, SHA-256) |
| `events.jsonl` | JSON lines | One event per line: `{"c": cycle, "f": frame, "k": kind, ...}` |
| `queues/<source>.jsonl` | JSON lines | `rtc`, `hostfs` (call, arguments, result; file bytes in `queues/hostfs-data.bin`) |
| `streams/<uart>.bin` | binary | RX records: cycle (u64 LE), byte; a header naming the UART and the device |
| `checkpoints.bin` | binary | frame (u32), cycle (u64), observable, framebuffer and full CRC-32s, TX digests |
| `card0/sectors.bin`, `card1/...` | binary | The included input sectors (section 5); `card0/index.json`: the base identity, the input-sector set with hashes, the references, the FAT paths |
| `screen.png` | PNG | The picture at the save point, for triage |
| `notes.txt` | text | What the reporter wrote |
| `log.txt` | text | Optional, off by default, redacted (section 9) |

Low-rate members are JSON so a developer can read a bundle with `unzip` and `jq` (the `.jns`
goal G5); high-rate members are binary with a documented layout. A member that is not declared
in the manifest, a declared one that is missing, or a CRC mismatch refuses the bundle.

---

## 5. The SD card

### 5.1 What has to be captured

For a replay to see the same bytes, only one set of sectors matters: the **input sectors**,
those whose first access after the anchor is a read. A sector the session wrote before reading
is the session's own output, which the replay regenerates. A sector never read cannot influence
the guest. So the card is captured as (B1.8):

- the input-sector set, each with the SHA-256 of the 512 bytes the guest read;
- an **undo log**: the pre-write bytes of each sector at its first write after the anchor, so the
  content at the anchor can be recovered at save time in the default read-write mode, where the
  image itself has moved on;
- nothing else. The tracking is a bit per sector (256 KiB for a 1 GiB card) plus a hash per input
  sector; a NextZXOS boot reads a few thousand sectors.

### 5.2 How the card is named without being shipped

The base is identified as one of:

| Base | Identity | The developer rebuilds it from |
|---|---|---|
| The default card (`cspect-next-1gb-fixed.img`) | distribution zip name, URL, `zip_sha256`, `image_sha256` of the raw `cspect-next-1gb.img`, `kFixedImageRecipe` | the distribution, through the provisioner (the same download jnext already offers) |
| The minimal card (#458) | `card_sha256`, `recipe`, the canonical `dirs` list (#509) | `make-minimal-card --dirs LIST` or the same builder in jnext |
| An image with `--sdcard-sha256` or a cached digest | its SHA-256 and size | the developer's own copy (`--bug-report-card FILE`) |
| Anything else | SHA-256 and size, computed at save time (about 1 s per GB, with progress) | the developer's own copy, or nothing (section 5.4) |

### 5.3 What goes in, and what never does

The **pristine reference** is the base as built by its recipe, before any session wrote to it.
At save time the recorder builds its sector-hash set once and caches it beside the image
(B1.9): for the default card, the fixed image rebuilt from the kept raw image into a scratch file
under `~/tmp` (deleted after); for the minimal card, the builder's output. Then each input sector
is:

- a **reference** when its bytes appear anywhere in the pristine reference (any sector, not only
  the same number: this also catches a distribution file the user copied elsewhere on the card);
- **bytes** in `card0/sectors.bin` otherwise: the user's own files, directory and FAT sectors.

The writer re-checks the finished bundle: no 512-byte-aligned block of **any** member may match
the pristine set (B2.1). The program file gets the same treatment as a whole: a NEX loaded from
the distribution's `/GAMES` is a reference, not a member.

Every included sector is attributed to a path through the FAT (`fat32_parse_dir` gives the
clusters) for the privacy review: the reporter sees "these 14 files of yours are in the bundle".

### 5.4 Replaying without the image

- **A `--no-sdcard` session needs no card at all**: the NEX runs on the synthetic machine with
  the esxDOS stand-in, whose host calls are in the queues. Fully self-contained.
- **A classic machine needs only its ROM files**, by SHA-256, from any card that holds them (any
  distribution with the same `48.rom`). When the session read no card sector after the anchor
  (DivMMC never touched the card), the replay mounts a placeholder of the same size, and any
  read of it is reported as "this session needs the card" rather than served.
- **A Next session needs the base image**, which every jnext user already has or can download,
  plus the bundle's sectors. Because every input sector is checked on read, a developer whose
  base differs only in sectors the session never read still replays exactly; one that differs
  in a sector that was read gets the sector number, its path and both hashes.
- **An unknown base** (a custom card): sectors that match the default distribution's pristine set
  are still references; sectors inside the distribution's system directories (`/TBBLUE.FW`,
  `/MACHINES`, `/NEXTZXOS`, `/SYS`, `/DOT`, and the rest of the distribution's top level) are
  hashes only, never bytes, because a different NextZXOS version would not dedupe (Q8). The
  developer needs that card from the reporter for those sectors, and the bundle says which.

### 5.5 Assembling the card for a replay

The replay writes the included sectors into a **scratch journal** (the `sd_write_journal`
format, version 1) bound by SHA-256 to the developer's base image, and mounts the base
**read-only with that journal over it**: exactly `--sdcard-writes`, the #507 mechanism.
Verified in `sd_write_journal.h`: a binding can be by digest (`Binding::by_digest()`), the
journal is laid over the image on every read, and the guest's writes during the replay append to
the scratch journal. The developer's image is never written, and the scratch journal is deleted
after the replay unless the developer asks to keep it.

### 5.6 The warm start and the second card

A `.nex` load on the Next starts from the warm start, recorded in an earlier run from the cache.
Its boot read sectors before the anchor, so B0.6 makes the recording keep its own input sectors
(number, hash and bytes: local storage, never published) and its state digest. The bundle
includes those sectors by the rules of 5.3, and the replay regenerates the warm start on the
replay card and refuses, naming both digests, if it differs. B0.5 makes that regeneration
reproducible by pinning the RTC during the recording procedure (Q6).

The second card (#505) is captured and assembled the same way (`card1/`).

---

## 6. Determinism, source by source

| Source | Enters the machine | Captured as | Replay | Proved by |
|---|---|---|---|---|
| Keyboard, extended keys | frontend key handlers, debugger input verbs, scripts, `--delayed-keypress` | event at the seam, level form, with cycle | applied at the cycle | BRB-IN rows; `.jds` oracle row |
| Phantom typist, auto-type | machine-internal, reacting to port 0xFE reads | not logged (re-runs by itself) | re-runs | BRB-IN-08 (a TAP load replays without double keys) |
| Joysticks (both connectors, MD 6-button, membrane stick) | joystick dispatcher, 12-bit connector state | event | applied | BRB-IN rows |
| Mouse | `KempstonMouse::inject_delta`, `set_buttons`, `set_wheel` | event per call | same calls | BRB-IN rows; probe NEX prints X, Y |
| Host input timing | between frames, or at the resume point while paused mid-frame | the cycle of application | the same cycle | the `.jds` WARNING case becomes exact (BRB-IN-06) |
| Audio-driven pacing, `--speed`, `--when-slow-prefer` | whole frames only (`audio_pacing.h`, `render_policy.h`) | not logged: wall clock only | n/a | GUI-record, headless-replay rows |
| Render skip of superseded frames | sprite side effects still run (`run_frame`) | not logged | n/a | same rows |
| `--silent`, fast boot (`audio_off_`) | PSG and mixer synthesis skipped | recorded as machine configuration until a row proves it guest-invisible | honoured | BRB-CFG-05 |
| RTC | `i2c.cpp` reads host local time per read | value queue (the broken-down time) | answered from the queue | BRB-PULL rows; probe NEX prints the time |
| `--rtc` pin | frozen | configuration | honoured | BRB-CFG row |
| Warm start | boot reads the RTC | B0.5 pins it during the recording | regenerated, digest checked | BRB-WS rows |
| ESP-01 (AT replies, sockets, SNTP, ping, timeouts) | RX bytes from `tick()` | byte stream; TX digest | replay UART device; ESP off | BRB-STR rows; loopback network row |
| NextPi UART | `pi_uart_device` | byte stream | replay device | BRB-STR row |
| NextPi I2S into EAR | the Pi audio thread | **not in v1**: flagged, the replay warns from the first sample (Q11) | n/a | BRB-STR-09 (the warning) |
| Joystick-port UART (`--joy-uart-rx` file) | deterministic from the file and its delay | the file is a member | honoured | BRB-STR row |
| Joystick-port UART (fifo, pty) | external process | byte stream | replay device | BRB-STR row |
| esxDOS stand-in, extended NEX host | host file reads, listings, `stat`, file times (host clock) | value queue with the bytes | replay host FS | BRB-PULL rows |
| SD card sectors | SPI reads | input-sector set + undo log | scratch journal, per-read hash check | BRB-CARD rows |
| Tape | the file (digest of the bytes read) and deck actions | member + events | honoured | BRB-IN row; RZX oracle row |
| Host actions (reset, NMI, drive button, SD insert, POK, ESP associate) | GUI, CLI `--delayed-*` | event | applied; their CLI options off | BRB-ACT rows |
| Debugger edits (memory, registers, NextREGs, ports, loads) | `note_mutation()` call sites | event with the verb's arguments | re-issued | BRB-DBG rows |
| Remote debug clients (DZRP, GDB, ZRCP) | through the `Debugger` facade | as debugger edits (B0.1 verifies every server path reaches the facade) | re-issued | BRB-DBG row |
| Rewind while recording | restore + fast-forward | the log is truncated at the landing; the fast-forward is fed from the log (B1.7) | n/a | BRB-TL rows |
| Quick snapshot taken and loaded in the segment | restore | a jump to the recorded cycle | replays to it | BRB-TL row |
| Any other snapshot, `.jns`, machine change, program load | re-initialises | ends the segment (a new anchor) | n/a | BRB-TL rows |

---

## 7. Recording: GUI and CLI

### 7.1 GUI

- **File > Bug Report** (Qt; the SDL frontend gets the same actions on its host keys):
  **Start Recording** (from now: a new segment from the current state is impossible without a
  memory snapshot, so it performs a hard reset or reloads the current program, and says so
  before doing it), **Save Bug Report...** (the rolling segment, at any time), **Stop
  Recording**, **Open Bug Report...**.
- **Rolling recording** (Preferences, Q2): the recorder runs from every anchor, in memory, and
  nothing is written until the reporter saves. "Save Bug Report..." is then available at any
  time, which is the issue's "from a rolling buffer, like rewind". A hotkey for it is Q12.
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
| `--bug-report-record FILE` | record from the start of the run (or from the `--load`), write FILE at exit | none (per run) |
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

Exclusions, enforced in `cli_options.h`: `--bug-report-record` with `--bug-report-play`,
`--rzx-record` or `--rzx-play` (the anchors conflict); `--bug-report-play` with `--load`,
`--inject`, `--script`, `--delayed-keypress`, `--delayed-nmi`, `--delayed-pok-frames`,
`--delayed-sdcard*-insert-frames`, `--pok-trainers` (the bundle brings its own inputs).

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

**Version rules** (B3.3):

| Case | Behaviour |
|---|---|
| `format_version` newer than the reader | refused |
| Same jnext version and state format | full digests compared; any mismatch is a divergence |
| Different jnext version | warned in the summary and the log; observable and framebuffer digests compared; the full digest skipped; `--bug-report-strict` refuses |
| Divergence | a banner with the frame and cycle of the first differing checkpoint and which digest differed (state or picture only); the replay goes on; headless exits with the divergence status |
| A card sector read that differs from its recorded hash | reported with its number, FAT path and both hashes (a card divergence, not an emulator one) |
| A warm start whose digest differs | refused, naming both digests and the warm-start versions |
| A queue pulled at another cycle, a queue running dry, a stream byte due at a cycle that never comes | divergence, naming the source |

A divergence under a newer jnext is information, not only an error: "the replay diverges at
frame 4123, picture only" on a fixed version is the fix showing.

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
| Host paths and the user name | program path, card path, `--esxdos-stub-root`, source directories, the log | never written: members by base name, the manifest holds no host path; the log, when included, has `$HOME` and absolute paths replaced |
| File names on the card | directory sectors among the input sectors | listed in the review by path; the reporter sees them |
| The user's files on the card | input sectors not in the distribution | listed with sizes; included only because the session read them |
| Keystrokes | the input log | inherent; the review says how many key events and over how long |
| Passwords typed into the guest | e.g. a WiFi password typed into a NextZXOS dialog, sent to the emulated ESP | the TX side is never stored (digest only); the keystrokes are, so the review says so |
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
(reset, NMI, drive, tape deck, SD insert and eject, machine change, load), the pulled sources
(`i2c.cpp` RTC, the esxDOS stand-in, the extended-NEX host), the UART devices, the SD card's
read and write paths, and configuration read at `init`. Each class gets one seam the recorder
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

## 11. Phase B0, continued: configuration and the warm start

### B0.4 The machine configuration

A field table over `EmulatorConfig` (one row per member: name, class, encoder), in the same
single-source spirit as `config_keys.cpp`: class `machine` (machine type, CPU speed, TurboSound,
tape fast load and auto-play, fast boot, `silent` until proved guest-invisible, RTC pin, cards and
their modes, `no_sdcard`, the esxDOS stand-in options, NEX arguments and v1.3 permission, the
inject options, ESP on and its schedule, NextPi on, joy-UART, magic port), `host` (scale, CRT,
gains, paths of screenshots), `debug` (trace, coverage, ports, maps: replayed as the developer
chooses), or `excluded` with a reason. A tripwire row pins the member count, so a new field
fails until it is classified (a count pin, authorised for new rows).

Tests: `bundle_config_test`, BRB-CFG-01.. : every machine-class field round trips; a host field
is absent from `config.json`; a path field is reduced to a member name; an unknown machine field
in a newer bundle refuses; the tripwire; BRB-CFG-05: one run with sound and one `--silent` from the
same anchor and inputs, compared by digest, decides whether `silent` stays a machine field.

### B0.5 A warm start that is the same everywhere

The warm-start procedure pins the RTC to a constant for the duration of the recording
(`kRecordingVersion` 1 -> 2, which re-records every cached warm start once). After the restore,
the RTC reads the live clock as now. With the RTC pinned, a warm start is a function of the card
content, the machine and the jnext version.

Tests: in `warm_start_test`: BRB-WS-01: two recordings of the same card at different host times
have identical state streams; BRB-WS-02: the version bump discards a version-1 cache; BRB-WS-03:
a guest RTC read after the restore returns the live clock (or the `--rtc` pin), not the recording's
constant.

### B0.6 The warm start's read set

The recording keeps, in its cache file, every sector its boot read first (number, SHA-256,
bytes) and a digest of the recorded state. A bundle whose anchor is the warm start names the
warm start's identity and digest and folds these sectors into its own input-sector set.

Tests: BRB-WS-04: the read set of a recording on a synthetic card equals the sectors the boot
read (counted by a test hook on the SD device); BRB-WS-05: a card edited in one read-set sector
regenerates to a different digest, and the replay's refusal names it.

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

Tests: `bundle_record_test`, BRB-IN-01.. (one row per input kind, the recorded cycle equals the
cycle the machine saw the change; a change while paused mid-frame is stamped with the resume
cycle, BRB-IN-06; a TAP load's typist keys are not logged, BRB-IN-08), BRB-ACT-01.. (one per
action), BRB-DBG-01.. (one per mutating verb: the logged arguments re-issued on a second machine
give the same digest), BRB-TL-01.. (a rewind truncates; a re-done input after the rewind is the
only one logged; a quick-snapshot jump; a `.jns` load ends the segment with its warning).

### B1.5 to B1.7 Pulls and streams (sub-issue 5)

The RTC seam covers both host-time paths of `I2cRtc` (`snapshot_time()`, which latches the
registers a guest I2C read sees, and the wall-clock read the esxDOS stand-in's `M_GETDATE`
answers from); each returns the broken-down time it read and logs it; the esxDOS stand-in and the
extended-NEX host log each call with its arguments and its result (the bytes of a read, a
listing, a `stat`), so a host file edited during the session is captured as the guest saw it.
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
undo log restores the anchor content in read-write mode and in journal mode; a sector present in
the synthetic "pristine" image (at another number too) becomes a reference; a user file's
sectors are bytes with their FAT path; an unknown base keeps system-directory sectors as hashes
only; the writer refuses a member containing a pristine block; the second card. The pristine
sector-hash set of the default card is built and cached by the regression master's provisioning
at run time (it is never committed).

## 13. Phase B2: the bundle file (sub-issue 7)

Writer and reader as section 4, through `zip_archive`. The reader validates the manifest against
the members, applies the version rules of section 8 and builds the privacy summary that the save
dialog, the open dialog and `--bug-report-info` share.

Tests: `bundle_file_test`, BRB-FILE-01.. : a written bundle is read back member for member; a
missing, an extra or a corrupt member refuses; `--bug-report-info` prints the review (a golden
text for a synthetic bundle); the manifest holds no absolute path (scanned); a foreign reader
(`unzip -t` and `jq` in a functional row) opens it, as the `.jns` rows do.

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
evidence there is (an engine that ignored every event would match a session with no input), so
the proof has four legs:

1. **The probe program makes every source matter.** A NEX (source in `demo/`) that reads the
   keyboard, both joysticks, the mouse, the RTC, an AT exchange with the ESP, a file through the
   esxDOS stand-in, a sector through NextZXOS where there is a card, and folds every value into a
   running checksum on screen and in RAM. Losing any source changes the digest.
2. **Mutation rows, one per source.** For each row of section 6: record a probe session, alter
   that one source in the saved bundle (drop an event, change an RTC value, flip an RX byte, flip
   one included sector byte, change one machine field), replay, and require a divergence report
   **at the checkpoint after the altered entry**, naming the right source where the source is
   named. An engine that ignored the source passes the unaltered replay and fails this row.
3. **Independent oracles.** On the 48K and 128K, the same session recorded with `--rzx-record`
   and as a bundle replays to identical screenshots by both mechanisms (RZX is an established
   format read by libspectrum, which the RZX rows already use). For the keyboard, the same
   session's `--record-script` replays to the same frames as the bundle.
4. **Across frontends and platforms.** Record in the Qt GUI and in the SDL GUI under Xvfb
   (`xvfb-run -d`, `-noreset`, `xdotool` keys, the existing pattern), replay headless; the
   digests match. Container and path rows run under wine (`make unit-test-win`).

Functional rows (`functional_tests.conf`): `bug-report-headless-func`, `bug-report-qt-func`,
`bug-report-sdl-func`, `bug-report-nextzxos-func` (`private-sd`: a NextZXOS session on the run's
own clone, replayed with the base found by digest), `bug-report-nocard-func`,
`bug-report-net-func` (the ESP against a loopback peer the row starts),
`bug-report-rzx-oracle-func`, `bug-report-mutation-func` (the mutation table),
`bug-report-privacy-func` (no pristine block in any member of the NextZXOS bundle; no host path).
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
| The pristine reference is not on the reporter's machine (raw image deleted) | the provisioner offers to fetch it; without it, the card is captured as an unknown base (5.4) and the review says so |
| Recording overhead on slow hosts | Q2's measured condition; the rolling recording can be turned off |
| A new guest-visible effect of `audio_off_` | BRB-CFG-05 decides; until then it is a machine field |
| Copyright leak through a member other than the card (a program from the distribution, a host file that is a NextZXOS file) | the writer's whole-bundle pristine-block check (B2.1) |

---

## 17. Open questions for the owner

Each with the default this plan assumes if the owner says nothing.

- **Q1. Epic shape.** #492 becomes the epic, branch `epic492-bug-report-bundle`, with the 13
  sub-issues (15 with B6). *Default: yes.*
- **Q2. Rolling recording on by default?** Without it, "save a bug report now" only works if the
  reporter started recording before the bug, which is rarely the case. *Default: on, if `make
  bench` shows at most 1% cost on the Next workloads; off otherwise.*
- **Q3. Memory in bundles.** v1 ships no machine memory for any firmware session, and no `.jns`
  anchor at all, so long sessions replay from their anchor. A firmware-excised keyframe (RAM pages
  matching card content replaced by references) can still carry modified NextZXOS code, so it is
  not offered. *Default: no shipped keyframes except for `--no-sdcard` sessions (B6.2); the local
  replay-side keyframe cache (B6.1) yes.*
- **Q4. The program file.** *Default: included (unless it is a distribution file, then a
  reference), listed in the review, with an untick that keeps only its SHA-256.*
- **Q5. File name.** *Default: `<name>.jnext-bug.zip`, recognised by content: GitHub accepts
  `.zip` attachments and refuses unknown extensions.*
- **Q6. Pinned RTC in the warm-start recording.** Re-records every cached warm start once
  (`kRecordingVersion` 2). *Default: yes, pinned to 2000-01-01 00:00:00.*
- **Q7. The default card mode.** Captured through the undo log, so the in-place default stays.
  The alternative, making `--sdcard-writes` the default, would change where every user's writes
  go. *Default: keep the in-place default.*
- **Q8. A card of unknown origin.** *Default: sectors in the distribution's system directories
  are hashes only, never bytes; the reporter is told which files the developer will need.*
- **Q9. A version mismatch on replay.** *Default: warn and replay, compare observable and picture
  digests; `--bug-report-strict` refuses.*
- **Q10. The browser build.** *Default: not in v1; the format is platform-neutral, and B6.3 adds
  it later.*
- **Q11. NextPi I2S routed to EAR.** *Default: not replayable in v1, flagged with a warning; the
  Pi's UART is covered.*
- **Q12. A hotkey for "Save Bug Report...".** Almost every Alt letter is taken. *Default: menu
  only in v1; a key only if the owner names one.*
- **Q13. Memory cap and segments.** *Default: 64 MiB; the recorder keeps only the latest segment
  (since the last hard reset, program load or machine change); at the cap it stops with a
  warning.*

---

## 18. Decisions taken in this plan

| Decision | Why | Rejected |
|---|---|---|
| An own log in the bundle, not RZX | RZX cannot describe a Next (the man page explains why jnext refuses it) | extending RZX: its value log has no ports and its snapshots no Next state |
| Not the `.jds` script as the log | frame-granular, no mouse, no pulls or streams, a text DSL for people | extending `.jds`; a `.jds` export of the input part stays possible later |
| Capture at the host seams | machine-internal typists re-run in a replay; sampling the matrix would apply their keys twice | `InputState` sampling per frame (the `.jds` recorder's way) |
| Cycle stamps from the anchor | places a change made while paused mid-frame exactly, and orders pulls inside a frame | frame numbers |
| No memory in a firmware-session bundle | rule 1: RAM holds NextZXOS and DivMMC firmware | shipping a `.jns`; excising ROM pages only (NextZXOS code in RAM banks and DivMMC RAM would remain) |
| The card as input sectors with per-read hashes, deduplicated against the pristine reference | exact for the guest, minimal in size, no distribution content | a whole-image digest only (the default card changes with every session, 1 s per GB); shipping the #507 journal (writes never read, possible copies of distribution files); a file-level diff (not sector-exact) |
| The replay card as a scratch journal by digest over a read-only base | reuses #507, never writes the developer's image | a full copy of the image per replay (1 GB) |
| A pinned RTC during the warm-start recording | makes the warm start reproducible on the developer's machine | shipping the warm start (firmware); replaying the warm-start boot inside every bundle (seconds per load while recording) |
| CRC-32 checkpoint digests, SHA-256 at the end | already linked, about 1 ms per 2 MiB; a divergence persists over many checkpoints | SHA-256 per checkpoint (several times the cost for no gain in detection) |
| ZIP container, JSON for low-rate members | the `.jns` precedent: readable with `unzip` and `jq`, CRC per member | a new binary container |
| Rewind while recording truncates the log, and the fast-forward is fed from the log | the rewound state is the recorded one, so nothing needs storing; feeding the fast-forward makes it true for the ESP and the RTC too | storing the abandoned branch |
| The replay runs normal frames, never `replay_mode_` | `replay_mode_` skips the render and the sprite side-effect pass | reusing the rewind's fast-forward mode for speed |
