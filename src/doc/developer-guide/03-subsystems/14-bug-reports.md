# 3.11 Bug reports

A bug report is one zip file holding the state of the machine and of jnext at the
moment it was written, for a developer to read. It is not a session to replay: nothing
in it can start a machine again. The writer is **`src/platform/bug_report.{h,cpp}`**
(namespace `bug_report`, in `jnext_platform`, Qt-free); the three loop owners call it
at exit through `src/platform/bug_report_at_exit.h`, beside the reverse-engineering
exports, when `--bug-report PATH` was given.

## What is in it

A report is recognised by its `manifest.json` (`kind` is `jnext-bug-report`), never by
its file name. Every other member is listed in `manifest.members` with its size, and a
member not listed there is never written. Member names follow the container's grammar
(`save::zip`: lower-case, `[a-z0-9._/-]`).

| Member | Content | Shipped |
|---|---|---|
| `manifest.json` | `format_version` (1), `created_utc`, `producer` (version, platform, OS, architecture, frontend, toolkit), `machine`, `memory` (`included`, or `withheld` with a code and a reason), `cards`, `programs`, `members`, `warnings` | always |
| `settings.json` | the command line (paths reduced to `<path>/name`), the settings file as `section.key` to value (the Qt frontend reads it through `AppConfig::file_for_report()` and the writer blanks what could name the host; the SDL and headless frontends read none and it says "not read by this frontend"), and the session switches | always |
| `machine.json` | registers, the 256 NextREGs with names, the paging map, video (raster, ULA, clip windows, 128 sprites, Copper state, eight palettes), interrupts, DivMMC, Multiface, audio | always; the Copper program only with memory |
| `state/<subsystem>.json` | the `.jns` per-subsystem state, filtered by key class (below) | always, filtered |
| `screen/composite.png`, `screen/<view>.png` | the picture, and each of the seven layer views | always |
| `log.txt` | the last 500 info-and-above log lines, redacted | always |
| `debugger.json` | run state, PC symbol and source, breakpoints, call stack, the last 256 trace entries | always; opcode bytes, disassembly and stack bytes only with memory |
| `program/<name>` | a program file loaded since memory was last cleared, as it is now on disk | unless it is a jnext snapshot (a `.jns`, or an `.rzx` whose embedded snapshot is not a classic one: code `snapshot`), changed on disk since it was loaded, unreadable, or larger than 32 MiB (then its SHA-256 and size only) |
| `memory/ram.bin`, `cpu.bin`, `patterns.bin`, `cpu-disasm.txt` | every logical RAM page, the CPU's 64 KB view, sprite pattern RAM, a linear disassembly | only with memory |

The writer describes the machine where it stands. It never advances to a frame
boundary (the `.jns` writer does): a report written at a breakpoint shows that
instruction. `Emulator::state_documents()` is `save_jns()`'s walk of
`visit_jns_subsystems` kept as text per subsystem.

## When memory ships

A report carries no NextZXOS, ROM or other distribution byte. Memory, and everything
derived from memory bytes, ships only when no firmware can have run:

- the session is `--no-sdcard`: the machine holds no ROM, DivMMC or Multiface bytes at
  all, so nothing was copied into RAM from any of them; and
- since RAM was last cleared the boot ROM has not run, and no state has been loaded.

That is `Emulator::ram_withheld()`. It reads a latch, `ram_withheld_`, which is set at
three places and never per instruction:

| Where | Effect |
|---|---|
| `Emulator::init()`, hard (it clears RAM) | `None` for `--no-sdcard`, otherwise `CardSession`; the list of loaded program files is cleared |
| the boot ROM install in `Emulator::init()` | `BootRomRan` (the boot ROM runs from PC 0 and may copy itself) |
| `Emulator::load_state()` | `StateRestored`, unless `DebugState::restoring_from_rewind()` (a rewind or step back restores this session's own ring, which a hard init recreates) |

A soft reset keeps the latch, as it keeps RAM. The first cause stays: the latch only
moves away from `None`. It fails safe: a restore path added later goes through
`load_state()` and withholds memory without anyone remembering to touch the report.

Every other session (a card, the classic machines) ships the machine state, the pictures,
the log and the debugger's state, but no memory. The manifest's `memory.withheld` has
the code (`card-session`, `boot-rom-ran`, `state-restored`) and a reason.

The program file is the reporter's own and ships whatever any card holds: jnext does not
police it. Four things keep its bytes out, each recorded with the SHA-256 (and size) in
the manifest and a code: it is a jnext snapshot (`snapshot`, below), it was edited on
disk after it was loaded and is not the one that ran (`changed-since-load`), it cannot be
read now (`unreadable`), or it is larger than a report member may be (`too-large`).

One kind of program file is never shipped: a jnext snapshot (`bug_report::holds_machine_snapshot()`). A `.jns` holds the whole memory, which after a NextZXOS boot is NextZXOS and DivMMC firmware, and an `.rzx` can carry one as its embedded snapshot. Its manifest entry is `included: false`, code `snapshot`, with SHA-256 and size. Classic `.sna`, `.szx` and `.z80` files are the reporter's own files like any other and ship.

## The state key classes

`JsonWriteDesc::content_keys()` lists the keys a declaration wrote through `bytes()`,
`fifo()` and `log()`, the arrays that can hold more than a register's value. The writer
classifies each `subsystem.key` with one table (`kKeyTable` in `bug_report.cpp`):

| Class | Treatment | Examples |
|---|---|---|
| State | always shipped | `mmu.slots`, `nextreg.regs`, the AY register files, `palette.*_rgb333`, `sprites.attributes` |
| Memory | only with memory | `copper.instructions` (Copper RAM), `membrane_stick.keymap` (loaded by the firmware) |
| Never | never shipped | `sdcard.cmd_buf`, `resp_buf`, `data_block` (card sectors), the UART FIFOs (serial traffic, which can include a WiFi password) |

**A key the table does not list is treated as Memory**, so a new declaration cannot leak
by default. `bug_report_test` row BR-15 diffs the table against the keys the walk emits,
both ways: **adding a `d.bytes()`, `d.fifo()` or `d.log()` declaration to a subsystem
fails that row until the key is classified.** Blobs (guest memory in `mem/`) never enter
the JSON, and `ram_window` writes a reference without bytes.

## Privacy

`bug_report::redact()` replaces every user name (from `USER`, `USERNAME`, `LOGNAME` and
the home directory's last component, three characters or more) with `<user>`, then every
absolute path (POSIX, `X:\` or `X:/`, UNC) with `<path>/<base name>`. It runs on the
command line, the log tail, settings values and every name from outside. Whether a settings value is kept is decided per key, in `config_value_rule()` (`bug_report.cpp`): a
`switch` over `config_keys::KeyId` with no `default`, so a key added to the table is a build error
(`-Werror=switch`) until someone classifies it, and `bug_report_test` BR-35 keeps the list of kept keys
explicit. Numbers, booleans and words are kept only as `config_keys`' own validator accepts them (the one `--set` and the settings file share), written in its canonical form, so a host or person name is refused by the key it sits in; `nextpi.release` only as empty, `latest` or a release tag; a debugger
key combination only when it parses; the debugger window's size and position only as `W,H` or `X,Y`.
Everything that can name the host is blanked whatever it holds: paths and path lists, host lists, device
ids and names, the recent-files history, the NextPi folder, QEMU binary, QEMU arguments and audio mode
(which can be `wav:FILE`), and any key the table does not know (its name is not echoed either). The log line of a refused `[debugger_keys]` entry (which quotes the hand-typed value in its reason) is
blanked after `bug_report::kConfigIssueTag`, the constant the emitter and the scrub share; `bug_report_ui_test`
BRUI-06 drives the real window with a bad `jnext.conf`. `redact()` also starts a path after a colon
(`wav:/abs/path`), except in a `scheme://` URL. The three members that carry free text
(`settings.json`, `log.txt`, `debugger.json`) are scrubbed a second time for the exact
user names and the home directory.

The SD card is named by its base name, size and SHA-256, with the identity the `.jns`
writer records (size, partition table digest, volume serial); never its bytes.

## The log tail

`Log::current_sink()` is one `dist_sink` holding the destination (console or `--log-file`)
and a process-wide ring of 500 lines at info level and above, so every logger still has
exactly one sink and the log-file rules do not change. `Log::tail()` returns the ring.
Debug and trace lines never enter it: they can carry byte dumps.

## The file name and the CLI

`bug_report::file_name()` builds `<name>.YYYYMMDDhhmm.jnext-bug.zip` (local time). `<name>`
is the last program file loaded since memory was cleared, without its extension, characters
outside `A-Za-z0-9._-` replaced by `_`, cut at 64, `jnext` with none; `-2`, `-3` follows the
time when the name is taken. `resolve_path()` is the `--bug-report PATH` rule: an existing
directory or a path ending in a separator names a directory (created when needed), anything
else is the file. A failed write logs an error and the loop owner exits 1.

## The Qt window

**File > Save Bug Report...** (`MainWindow::on_save_bug_report()`, after Quick Load Snapshot, no
shortcut) opens a save dialog in the home directory (nothing is remembered: that would be a new
persisted setting) proposing `bug_report::file_name()` for the loaded program, filtered to
`*.jnext-bug.zip`. `MainWindow::save_bug_report_to()` then calls `bug_report::write()` with the
command line, `frontend` `qt`, and the settings file from `AppConfig::file_for_report()` (every row of
`config_keys::KEYS` the file holds, plus whatever keys `[debugger_keys]` has). The status bar names the
file and whether memory is included; a failure shows a dialog (not when unattended). `QtApp::shutdown()`
passes the same map to `--bug-report`. There is no Preferences field, so no `config_keys` row: `--bug-report`
is a per-run action like `--delayed-screenshot`.

## The viewer

`tools/jnext-bug-view.py` shows a report: `jnext-bug-view.py REPORT [-o OUT.html] [--text [--memory]] [--no-open]`.
It is Python 3, standard library only (`zipfile`, `json`, `html`, `base64`, `argparse`, `webbrowser`), runs from a
clone as `python3 -I`, and needs no jnext build and no card. It is not packaged.

- **Output.** By default one self-contained HTML page (no JavaScript, no external resource; the pictures are
  embedded as `data:` PNGs) written to a temporary folder, or to `-o`, and opened with `webbrowser` unless
  `--no-open`. `--text` prints the summary and the registers as text instead, and with `--memory` the hex dump of
  the CPU view; the text is what a terminal and the regression rows read. Sections, as element ids: `summary`,
  `settings`, `machine`, `nextregs`, `paging`, `video`, `interrupts`, `pictures`, `log`, `debugger`, `memory`,
  `raw-state`. A section whose member is not in the report says so; without memory the `memory` section says "not
  included" and why, from the manifest.
- **It decodes nothing.** Register names, NextREG names, the paging map and the disassembly are what jnext wrote
  into the report; the viewer has no register table and no Z80 disassembler to keep in step.
- **Exit status.** 0 shown, 2 refused (with a message naming the file and the reason), 1 an I/O error.
- **Hostile input.** A report is an issue attachment. The viewer refuses a file that is not a zip, a zip with no
  `manifest.json`, a `kind` other than `jnext-bug-report`, a `format_version` above 1 ("newer"), a declared member
  that is missing, more than 4096 members, any member that declares more than 64 MiB and a total above 256 MiB (all
  checked from the central directory, before anything is inflated), and a member that cannot be read. A member is
  read at most up to its declared size. Every text goes through `html.escape`; a picture is embedded only if it
  starts with the PNG signature; JSON nested too deep, that is not JSON, or that has the wrong shape (a list where an object belongs) is shown as absent (the section says so), never a crash; `--text` writes every control character of the report (escape sequences, DEL, C1) as `\xNN`;
  nothing is extracted to disk.

## Tests

`bug_view_test` (BV-01 to 19) builds reports in memory and runs the viewer as `python3 -I` in a child with an explicit minimal environment (`LANG=C`, `PATH=/usr/bin:/bin`, no `HOME`): refusals, every section, names and values from the report, the pictures byte for byte, memory present and absent, escaping, text mode, and hostile structure. `bug_report_ui_test` (BRUI-01 to 06) drives the real menu action and file dialog of an offscreen window. `bug_report_test` (rows BR-01 to BR-35) builds reports in memory from a no-card session
and from a synthetic card with ROM files of a known pattern, and parses them with the
container's reader. The two regression rows `bug-report-nosd-func` and
`bug-report-card-func` run the real binary. The second one is the end-to-end check of the
rule, as far as a scan can see: it extracts every firmware file of the run's card clone and `nextboot.rom` into its
scratch directory (removed with it, never kept) and fails if any 32-byte window of them
occurs at any offset of any member, as raw bytes and as what the member decodes to (JSON hex strings, integer arrays and the integers of a `log()` array, and nested zips). The only member left out is the program, proved byte-equal to the NEX the row made, and only when it is the one program. The row first checks that NextZXOS really booted, then loads a `.jns` it saved after the boot and scans that report too. `test/00regression/bug-report-check.py`
does the scan with the standard library only.
