# 5.11 POK trainers

A **`.pok` file** is a list of cheats for one game: "Infinite lives", "Infinite
ammo", "Start on level 5". Each entry (a *trainer*) is a few bytes to write into
the running game's memory. The format is the one World of Spectrum made
standard, and the files are found next to a game's tape or snapshot in most
collections. JNEXT opens one, shows its trainers, applies the ones you pick to
the machine that is running, undoes them again, and lets you save the result as
a snapshot.

A `.pok` file changes the program that is running. It does not start one: load
the game first (see [5. Running programs](index.md)), reach the point you want,
then open the trainers.

## Using one

1. Load and start the game.
2. Choose **Machine ▸ POK Trainers…**, or **File ▸ Load NEX File…** (Alt+O) and
   pick the `.pok` file (the file dialog lists *POK Trainers (\*.pok)*). A
   `.pok` opened with File ▸ Load does **not** reset the machine: it opens the
   trainers and leaves the game exactly where it was.
3. Pick a trainer in the list and press **Apply**. Its **Status** reads
   *applied*, and the game now behaves differently: lives stop going down,
   say.
4. Press **Remove** to undo it.
5. **Close** the window and carry on playing. To keep the state, save it with
   **File ▸ Save Snapshot…** ([5.9 Snapshots](09-snapshots.md)): the snapshot
   holds the changed bytes, so loading it later gives you the game with the
   trainer already in it.

**Machine ▸ POK Trainers…** with no file open asks for one; with a file open it
shows the trainers again. **Open POK File…** in the window opens another file.
While the window is open the game keeps running behind it, but the main window
takes no keys until you close it.

### What the Status column says

| Status | It means |
|---|---|
| *not applied* | Memory does not hold this trainer's bytes. **Apply** is available. |
| *applied* | You applied it and its bytes are still in memory. **Remove** puts back what it replaced. |
| *applied (found in memory)* | You did not apply it in this session, but memory already holds all of its bytes, for example after loading a snapshot taken with it applied. **Remove** needs the file to give the original bytes (see below). |
| *invalid - line N: …* | The trainer has a malformed line, and the reason names the line. It cannot be applied. |

The status is worked out from memory, never remembered. The list is refreshed
when you press **Apply** or **Remove**, select a row, or the machine is rebuilt.
If the game has overwritten a poked byte (a level change that resets the lives
counter, say), the next refresh shows *not applied*, and **Remove** says so
instead of writing. Press **Apply** again to poke it back: **Remove** then
undoes the whole trainer, putting every byte the game had not touched back to
what it was before the first **Apply**, and leaving a byte the game itself
changed as the game has it.

### Trainers that ask for a value

Some trainers leave a number open: "Lives" without saying how many. **Apply**
then asks for it, offering the byte memory holds at the first such place (the
game's current lives). The number you give is written everywhere the trainer
leaves a value open, once for the trainer, not once per poke. **Cancel** applies
nothing. A trainer that asks for a value is never shown as *found in memory*,
because the number you chose cannot be known afterwards.

### What Remove puts back

**Remove** writes back exactly the bytes **Apply** replaced, at exactly the
places it wrote them, even if the game has paged other memory in since. That is
why it is exact, and why it is only available for a trainer you applied in this
session.

For a trainer *found in memory*, JNEXT has no record of what was there. A `.pok`
line can carry the original byte as its last number, "used to remove a POKE"
from a game that was already poked, and JNEXT uses that. Most `.pok` files do
not give it (the number is `0`), and then **Remove** is refused with a message
naming the line and the address.

### When the undo is forgotten

The file stays open for as long as the main window does, but what **Remove**
needs to undo a trainer is kept only while the machine you applied it to is
running. These forget it, though the changes stay in memory:

- **Machine ▸ Power Reset**, a machine-type change, **File ▸ Load NEX File…**
  of a program or snapshot, Quick Load, and a program that resets the machine
  itself. The row shows *applied (found in memory)* if memory still holds the
  bytes, *not applied* if not.
- A program loaded from a debugger, DZRP or ZRCP client (an IDE sending the
  program): the trainer's bytes may still be in memory, but what they replaced
  belonged to the old program, so the row shows *applied (found in memory)* or
  *not applied*, never *applied*.
- Opening another `.pok` file.

## Where a poke lands

Each line of a `.pok` carries a **bank** number as well as an address. JNEXT
follows the hardware:

| The line says | It writes to |
|---|---|
| bank **8-15** (the format's "ignore the bank", always used for 48K games) | the RAM the memory map shows at that address **at the moment you press Apply** |
| bank **0-7**, address **0xC000-0xFFFF** | that 16 KB bank, whatever is paged in there |
| bank **0-7**, address **below 0xC000** | the bank is ignored, as for 8-15: only the top 16 KB of the address space is banked, so this is the memory map again |

Banks 0-7 are the 128K RAM banks, so they work on the 128K, +3, Pentagon and the
Next. On a **48K** the only bank that exists at 0xC000 is bank 0; a line naming
another bank at 0xC000 or above is refused, with a message (below 0xC000 the bank
is ignored, as on every machine, and the line pokes the memory map). A trainer whose lines say
bank 8 writes into whatever bank the game has paged in at that moment, so apply
it while the game is running normally.

A trainer is applied **all or nothing**. If one of its pokes cannot land (an
address that is ROM in the current memory map, a bank the machine does not
have, a slot with nothing mapped) the trainer is refused with a message naming
the line, and none of its bytes are written.

Other emulators, Fuse for one, index inside the named bank for every address.
The two only differ for a bank number on a line below 0xC000, which is rare in
the files in circulation. In the one checked against the real game, the 128K
Starglider's "Infinite Missiles (Bank#0)", reading the bank number there would
miss: its first line (address 44796) is the byte the CPU sees at 0xAEFC, the `1`
of an `LD A,1` that sets the missile counter.

## Things that stop it

- **An RZX recording is playing or being made.** A change the recording does
  not contain would make its playback diverge, so **Apply** and **Remove** are
  refused with a message that says so. Stop the recording or the playback first.
- **Rewind.** Applying or removing a trainer is a change made from outside the
  program, like a memory edit in the debugger. Step Back across it is refused;
  Frame Back to before it undoes it. See [Backward execution
  (rewind)](../06-debugger/functions/07-backward-execution-rewind.md).

## A file with a malformed line

JNEXT reads the format strictly, so a damaged line costs only the trainer it
belongs to. That trainer is listed as *invalid - line N: …* with the reason (a
bank outside 0-15, an address outside 16384-65535, a value outside 0-256, a
line that does not have four numbers, a trainer that ends without its last
line), and every other trainer works. Lines that belong to no trainer are listed
below the list with their line numbers, twenty at most and then "and N more".
Blank lines, CRLF or LF line ends, names padded with spaces, and a missing final
`Y` line are all fine. A file with no trainer at all, or larger than 1 MiB, is
refused with a message.

A trainer's name is shown as UTF-8 text when the bytes are valid UTF-8 and as
Latin-1 otherwise, which covers both kinds of file in circulation.

## Without the window

The window is part of the Qt build. The SDL-only build's window and the headless
runs have no menus, and `--load game.pok` is refused. Two options apply a `.pok`
file there:

- `--delayed-pok-frames N FILE` applies trainers from `FILE` after `N` emulated
  frames, once the program has loaded. Frames are counted from the start of the
  run (a 50 Hz machine runs 50 a second).
- `--pok-trainers LIST` says which ones: their numbers in the file, 1 for the
  first `N` line, comma-separated, each `T` or `T=VALUE`. A trainer that asks
  for a value (a `256` in the file) needs `T=VALUE`, 0 to 255; any other trainer
  takes none. `1,3=9,7` applies trainer 1, trainer 3 with the value 9, and
  trainer 7, in that order.

```console
$ jnext --headless --machine 48k game.tap \
      --delayed-pok-frames 1200 game.pok --pok-trainers 1,3=9 \
      --delayed-snapshot trained.sna --delayed-snapshot-frames 1210 \
      --delayed-automatic-exit-frames 1300
```

The numbers are the order of the `N` lines in the file (`grep '^N' game.pok | cat -n`
numbers them), and the log names each trainer as it is applied. Pick the frame by
when the game has reached the point you want, and look with a
`--delayed-screenshot` at a guessed frame: a tape loaded with the fast loader is
ready after a few hundred frames (the 48K Starglider shows its menu at 150), one
loaded in real time (`--tape-realtime`) takes as many frames as it has seconds
of tape times 50 at 50 Hz. With a snapshot, `--delayed-pok-frames 0` applies the
trainers in the first frame, right after the snapshot has loaded.

Everything that can be checked is checked before the machine starts, and a
failure exits with status 1: a file that cannot be read, is larger than 1 MiB or
has no trainer; a number that names a trainer the file does not have or one
whose lines are malformed; a value missing for a trainer that asks for one, or
given to one that does not; either option without the other, or given twice
(one file per run). The Qt window refuses both options, since it has the
trainers window.

The trainers go in by the same path as the window's **Apply**: every write is a
`MUTATE` line in the log, a trainer is applied whole or not at all, a bank-3
line at 0xC000 or above on a 48K or an address that is ROM in the current memory map is refused,
and so is anything while an RZX is recorded or played. A refusal, or an
automatic exit that comes before the frame, makes the run exit non-zero once it
ends, and the trainers before the refused one stay applied. A trainer whose
bytes memory already holds, from a snapshot saved with it, is left as it is.

For anything the options do not do (undoing a trainer later, applying at several
moments, a condition), a [debugger script](../06-debugger/scripting/index.md)
does the writes in any build:

```
; "Infinite lives": Z  8 32771 0 53   (bank 8 = the memory map; value 0)
; Pick the frame by when the game has reached the point you want.
on frame 2000 once do
    set mem[32771] = 0
    save_snapshot "game-trained.sna"
end
on frame 2003 once do exit 0 end
```

```console
$ jnext --headless --machine 48k --load game.tap --script trainer.jds
```

For a line with a bank 0-7 at 0xC000 or above, write to the physical page
([Changing the machine](../06-debugger/scripting/08-changing-the-machine.md)):
page `2*bank + ((address >> 13) & 1)`, offset `address & 0x1FFF`. The line
`Z  3 49443 90 0` (bank 3, address 49443, value 90) becomes:

```
var bank = 3
var addr = 49443
var poke_value = 90
on frame 2000 once do
    set phys[2 * bank + ((addr >> 13) & 1), addr & 0x1FFF] = poke_value
    save_snapshot "game-trained.sna"
end
on frame 2003 once do exit 0 end
```

Every write is logged as a `MUTATE` line, in the window too: the trainers go
through the same write path as the debugger's own memory edits, which is also
why an RZX recording refuses them.
