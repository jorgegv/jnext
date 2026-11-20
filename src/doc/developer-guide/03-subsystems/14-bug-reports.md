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
| `settings.json` | the command line (paths reduced to `<path>/name`), the settings file as `section.key` to value (no frontend reads it yet, so it reads "not read by this frontend"; the writer takes the map from its caller and blanks it), and the session switches | always |
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

The program file is the reporter's own and always ships, whatever any card holds: jnext
does not police it. A file edited on disk after it was loaded is not shipped as the one
that ran: its SHA-256 and current size are in the manifest, its bytes are not.

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
command line, the log tail, settings values and every name from outside. A settings value
is kept only when its type cannot name the host (numbers, booleans, words, key
combinations); paths, host lists, device names, the recent-files history and any key the
table does not know are written as `<blanked>`. The three members that carry free text
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

## Tests

`bug_report_test` (rows BR-01 to BR-34) builds reports in memory from a no-card session
and from a synthetic card with ROM files of a known pattern, and parses them with the
container's reader. The two regression rows `bug-report-nosd-func` and
`bug-report-card-func` run the real binary. The second one is the end-to-end check of the
rule, as far as a scan can see: it extracts every firmware file of the run's card clone and `nextboot.rom` into its
scratch directory (removed with it, never kept) and fails if any 32-byte window of them
occurs at any offset of any member, as raw bytes and as what the member decodes to (JSON hex strings, integer arrays and the integers of a `log()` array, and nested zips). The only member left out is the program, proved byte-equal to the NEX the row made, and only when it is the one program. The row first checks that NextZXOS really booted, then loads a `.jns` it saved after the boot and scans that report too. `test/00regression/bug-report-check.py`
does the scan with the standard library only.
