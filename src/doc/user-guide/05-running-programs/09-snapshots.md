# 5.9 Snapshots

A snapshot freezes the whole machine to a file and brings it back later.

## Which format to use

JNEXT writes four, and the extension picks one:

| Extension | What it is | Use it when |
|---|---|---|
| `.jns` | **JNEXT's own snapshot.** The only one that can represent a ZX Spectrum Next. | You are on a Next — which is JNEXT's default machine. |
| `.sna` | The classic 48K/128K snapshot every Spectrum emulator reads. | You are on a 48K, 128K, Pentagon or +3 and want to hand the file to another emulator. |
| `.szx` | ZX-State: richer than `.sna` — it names the machine and carries the +3's own paging register. | Same, when `.sna` cannot describe the machine (see below). |
| `.nex` | Not a snapshot — a *program* file the Next's own loader runs. | You are producing something to run on real hardware. |

### What each format can describe, and when JNEXT refuses

A snapshot that cannot describe the machine in front of it is **refused**, with
an error saying why and which format to use instead. JNEXT never writes a file
that quietly misrepresents your machine.

| Machine | `.sna` | `.szx` | `.jns` |
|---|---|---|---|
| 48K | 48K form, exact | yes | yes |
| 128K | 128K form: all eight banks, the paging register | yes | yes |
| Pentagon | 128K form | yes, as a Pentagon 128 | yes |
| +3, ordinary paging | 128K form | yes | yes |
| +3, special paging or ROM 2/3 paged | **refused** — use `.szx` | yes | yes |
| any machine using extended paging (port 0xDFFD, or NextREG 0x8F's 512K/1024K modes) | **refused** — use `.jns` | **refused** — use `.jns` | yes |
| Next | **refused** — use `.jns` | **refused** — use `.jns` | yes |

`.sna` and `.szx` describe a machine the Next is not: no Layer 2, no sprites, no
tilemap, no Copper, no DivMMC, 128 KB of RAM where a Next has 768 KB or more.
That is why `.jns` exists.

The +3 row is the subtle one. An SNA has no byte for the +3's second paging
register, so two +3 states cannot be written at all: **special paging**, where
four RAM banks replace the whole address space including the ROM, and **ROM 2 or
ROM 3 paged**, which an SNA would bring back running a different ROM. `.szx` has
a field for that register, so it can hold both. An ordinary +3 — which is most
of the time — saves as a 128K SNA.

The extended-paging row is the one that can surprise you. A `.sna` records which
16 KB bank is at 0xC000 in a single byte that can only name banks 0-7. Port
0xDFFD — on by default on a Next and a Pentagon, and on the other machines
once software enables it in NextREG 0x82 — can put a bank *above* 7 there. The snapshot would then come back with a different bank
mapped and that bank's contents missing altogether, so JNEXT refuses instead.
`.szx` has no field for that register either; `.jns` is the format that carries
it.

One property of `.sna` worth knowing when you hand the file to another emulator:
the format carries no machine identifier, only a TR-DOS flag. FUSE reads a 128K
SNA as a *Pentagon* 128 — the right RAM and paging, a different timing model.
`.szx` names the machine, so use that if the timing matters.

## Saving

**File ▸ Save Snapshot…** (Alt+Shift+S), or headless:

```
jnext --headless --load game.nex \
      --delayed-snapshot state.jns --delayed-snapshot-frames 600
```

On a Next the file dialog offers `.jns` first and adds `.jns` if you type a
name with no extension; on a 48K, 128K, Pentagon or +3 it offers `.sna` first and adds
that. Type any of the four extensions and you get that format, unless the table
above says it is refused: the dialog then says why, and headless says why and
exits non-zero rather than leaving you a file you would have trusted.

A snapshot is only ever taken at a **frame boundary**. If the debugger has
stopped the machine part-way through a frame — which is exactly when you reach
for this menu item — JNEXT finishes that frame first and saves from the
boundary after it, and the status bar says so once. That pause never refuses
the save and never greys the menu item out — the only refusal is the format one
above; the consequence is that the restored machine is up to one frame (20 ms)
past where you stopped. If you need the precise instant, that
is what the rewind buffer is for.

A snapshot also keeps the changes a [POK trainer](11-pok-trainers.md) made to
the running game: apply the trainer, then save.

## Quick save and quick load

**File ▸ Quick Save Snapshot** (**Alt+Shift+F5**) and **File ▸ Quick Load Snapshot**
(**Alt+Shift+F9**) ask nothing. Quick Save writes a `.jns`, on every machine type,
to `~/.jnext/snapshots/` (or `$JNEXT_CONFIG_DIR/snapshots` when that variable is
set), named `jnext-YYYYMMDD-HHMMSS.jns`, and shows the path in the status bar.
Two saves in the same second get a `-02`, `-03` suffix, so nothing is
overwritten. Quick Load restores the **newest** `.jns` in that folder, judged by
modification time, the same way **File ▸ Load NEX File…** does: a full cold
boot, then the load. A `.jns` you save into that folder yourself counts too.

- An empty or missing folder only says "no snapshot" in the status bar; the
  machine is not touched.
- A newest file that cannot be loaded (damaged, or taken with a different SD
  card) is refused **before** anything is reset, so you keep your session.
- Quick Save keeps the **newest 20** of its own files and deletes older ones
  after each save. Change that with `--quick-snapshot-keep N` (or
  `unlimited`), with **Settings ▸ Preferences ▸ Paths ▸ Quick snapshots to
  keep**, or `quick_keep` under `[snapshot]` in `jnext.conf`; the command-line
  option wins for the run. Only files named exactly like Quick Save's own
  (`jnext-YYYYMMDD-HHMMSS.jns`, optional `-NN`) are ever deleted: a `.jns` you
  saved into the folder under another name is never touched. Older quick saves
  that are still there load through the ordinary load dialog.
- The SDL-only build has the same two keys, the same folder and the same
  `--quick-snapshot-keep` option (it reads no `jnext.conf`), and reports in the
  log instead of the status bar.
- Plain F5 and F9, and Alt+F5 and Alt+F9 on their own, are unchanged. Shift is
  the Spectrum's Caps Shift, so it is held down for the program while you press
  the chord.
- Only `.jns` is used: it is the one format that can hold a whole machine.
- Keys you type while a quick save or load is in progress are not lost: they are
  delivered afterwards, in the order you typed them, and a quick save waits for
  the keys you typed before it.

## Loading

Drop the file on the command line, or **File ▸ Load NEX File…** (Alt+O) — the
dialog lists `.jns` alongside everything else.

```
jnext state.jns
```

A `.jns` restores the machine **type** as well as its contents: one taken on a
Next comes back as a Next even if `--machine` says otherwise. You do not have
to remember what you saved. A `.sna`, `.szx` or `.z80` does the same: it runs on
the machine it was saved on (see below), even over `--machine`.

## Loading another emulator's snapshot

A `.sna`, `.szx` or `.z80` says which machine it holds, and JNEXT switches to
it, as it does for a `.jns`. The machine stays selected afterwards, so a later
reset starts it again; **Machine ▸ Machine Type** shows it. With `--machine` on
the command line, JNEXT warns that the snapshot's machine won.

| The file says | JNEXT runs |
|---|---|
| `.sna` of 49179 bytes | 48K |
| any longer `.sna` (the 128K form) | 128K |
| `.szx` 16K, 48K, NTSC 48K | 48K (NTSC at 60 Hz) |
| `.szx` 128K, +2, 128Ke | 128K |
| `.szx` +2A, +3, +3e | +3 |
| `.szx` Pentagon 128 | Pentagon |
| `.z80` v1, hardware 48K, 16K, Interface 1, SamRam, MGT | 48K |
| `.z80` hardware 128K, +2, 128K with Interface 1 or MGT | 128K |
| `.z80` hardware +3, +2A | +3 |
| `.z80` hardware Pentagon 128 | Pentagon |

The 128K, +3 and Pentagon snapshots bring the AY registers back, and a +3 its
ROM and paging. Anything else is refused, with the reason in the log and a
message in the GUI, and the machine you were running is left alone (started
with `--load`, JNEXT exits with an error): a Timex TC2048, TC2068 or TS2068, a
Scorpion, a ZX Spectrum SE, a Didaktik Kompakt, a Pentagon 512 or 1024, a
`.z80` of an unknown hardware mode or header length, and any snapshot taken
with the TR-DOS, Interface 1, MGT DISCiPLE or Multiface ROM paged in — JNEXT
emulates none of those ROMs, so the program would run against the wrong one.

## What travels, and what does not

Everything the emulated machine is: CPU, all of RAM, the MMU, every NextREG,
the video layers and their palettes, the Copper, the audio chips mid-envelope,
the DivMMC, the Multiface, the SD card's transfer state, the keyboard and
joystick.

Not in the file, deliberately:

- **Your debugger session.** Breakpoints, watches, the trace log and the rewind
  history are yours, not the machine's, and they stay where they are.
- **Your volume and mute settings.** Restoring somebody's save must not move
  your own knobs.
- **A recording in progress.** Video or RZX capture is something *you* are
  doing, not something the machine is.

## The SD card, and why a snapshot may complain

A `.jns` does **not** contain the SD card — a card image is a gigabyte, and it
holds NextZXOS, which is not ours to redistribute. It records the card's
*identity* instead, and checks it when you load.

- **A different card** — different size, different partitioning, different
  volume serial — is **refused**. Restoring a Next mid-way through reading a
  file, against a card where that file is somewhere else, is the kind of wrong
  that looks like a crash an hour later. `--snapshot-mode force` overrides
  it, and says what it is overriding. That includes a card you put in with
  **File > Insert SD Card Image** after saving: insert the original again to
  restore.
- **The same card, changed since, is not checked.** JNEXT assumes the card is as
  you left it. If you change it between saving and loading, outside JNEXT or by
  running other programs that write to it, loading the snapshot is at your own
  risk: NextZXOS keeps parts of the card's file system in memory, and a restored
  machine can write that stale picture back and corrupt files.
- The card's **volume label** is never compared. Several tools rewrite it, and
  a snapshot that refused over a renamed card would be a snapshot nobody reads
  the warnings of.

## Do not share a `.jns`

A snapshot contains **all of RAM**, and on a Next that includes the NextZXOS
and DivMMC ROM code paged into it. Those are not ours to redistribute and they
are not yours either. Keep your snapshots; do not post them.

If you want to hand somebody a program, hand them the `.nex`.

## The tape

The tape attached when you save is recorded by its full path, with a digest of
the file, and the deck's position in it: which block, and how far through it. Loading
the snapshot opens that tape again and puts it back **at the same place**, so
a program that was part-way through loading carries on loading. A tape that
came out of a `.zip` is taken from the archive again (JNEXT remembers which
archive and which member), even in a later session.

A snapshot never changes a setting. The tape comes back in whatever fast or
real-time mode and automatic play/stop setting **this session** has: the
`--tape-realtime`, `--tape-auto-play` and `--no-tape-auto-play` options, or the
Tape menu. The Tape menu and your saved preferences are left exactly as they
were. When the snapshot's tape was playing in the other mode, JNEXT converts it
at a block boundary, so that no block is cut or repeated: a real-time tape
saved part-way through a block and loaded in a fast session finishes that block
in real time and loads the rest fast.

The tape is **not** put back, and the load says so in the status bar, when
the file is no longer where it was recorded (or the archive is gone), when the
file has changed since the snapshot was saved, or when the snapshot was written
by a version that did not record the position. The machine still restores; open
the tape again with Tape > Open Tape File. Snapshots written by versions that recorded
only the file name look the tape up in the folder JNEXT was started from.

## Two flags for when something is wrong

`--snapshot-compression off` writes the file with nothing deflated. It is then
an ordinary ZIP you can open with `unzip -p` and read with a text editor — the
manifest, and one JSON document per subsystem. About five times the size, and
JNEXT reads both forms. `on` is the default.

`--snapshot-mode` says how much a load refuses, and it has three positions
because they are one dial rather than a set of switches:

- `normal` — the default, described above: warnings for a changed state model
  or changed ROMs, a refusal for a different card.
- `strict` — turns those warnings into refusals: a snapshot from a JNEXT whose
  state model this build does not know, or one whose recorded ROM digests
  differ from the ROMs now loaded. A missing tape file is never a refusal even
  here — a machine restores perfectly well without the tape it was reading.
- `force` — restores against a different SD card anyway, saying what it is
  overriding.
