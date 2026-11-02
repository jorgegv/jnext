# Disassemblies in skoolkit and Ghidra

jnext can hand what the debugger knows (its labels, your comments, and which bytes the program really
executed, read or wrote) to two reverse-engineering tools, and read labels and comments back from
them: [SkoolKit](https://skoolkit.ca/) (a Z80 disassembler driven by *control files*) and
[Ghidra](https://ghidra-sre.org/) (through its *ImportSymbolsScript.py* symbol import).

## The workflow

1. Run the program with **`--coverage`** (or **Debug ▸ Coverage ▸ Enable Coverage**, see
   [Memory coverage](15-memory-coverage.md)), so jnext learns which bytes are code and which are data.
2. Export: **Map ▸ Export ▸ skoolkit Control Files…** or **Ghidra Symbols…**, which asks for a
   directory; or `--export-skoolkit DIR` / `--export-ghidra DIR` on the command line, written when jnext
   exits. The directory is created.
3. Open the result in the tool, and, if you want to keep the names you gave there, bring them back with
   **Map ▸ Import ▸ …** or `--import-skoolkit FILE` / `--import-ghidra FILE`.

An export writes **what the Disassembly shows**: your own labels over the loaded files' labels, your
comments, and (for skoolkit) the code and data map. Without coverage the control files still carry the
labels and comments; each window is then one code block.

A report box tells how many files, labels and comments were written, and anything you should know
(a name skipped, a memory whose bytes cannot be read). A directory you exported to before has its
own earlier files replaced, and nothing else in it is ever touched. A file of the same name that is
not jnext's makes the export fail before it writes anything.

## SkoolKit

The Next has 224 pages of 8 KB, and a control file describes one 64 KB view. So the export writes
**one control file and one image per memory window**:

| File | Holds |
|---|---|
| `C000-P10.ctl` | the labels, comments and code/data map of the page `P10` (RAM page `0x10`), seen at `$C000` |
| `C000-P10.bin` | that window's bytes, as the debugger reads them now |
| `0000-ROM0.ctl` / `.bin` | ROM 0, a 16 KB window |
| `0000-0CPU.ctl` | the labels that belong to no particular memory (a `--map` file's, for example) |

The names are the debugger's ([Which memory](16-user-labels-and-comments.md)): `P0A`, `ROM1`, `DIVRAM2`…
The first line of each file, `# jnext space P10`, is a comment skoolkit ignores; jnext reads it back.

- **One page:** `sna2skool.py -o 0xC000 C000-P10.bin` finds `C000-P10.ctl` by itself.
- **A 48K or 128K snapshot** (`--delayed-snapshot`): `sna2skool.py -c DIR snapshot.sna`
  reads every control file of the directory, in name order, which is address order. Pass only the
  memories the snapshot holds: on a 48K machine they are all of them, on a 128K machine the pages that were
  mapped.

In the code/data map, bytes the program executed are code (`c`), bytes it only read or wrote are data
(`b`) and the rest is unused (`u`). skoolkit knows only the plain Z80: the Next's own instructions
(`NEXTREG`, `MUL`, `PUSH nn`…) are written as `DEFB`, so the code after them stays aligned. A label
that falls inside an instruction or a data run gets a split of its own, so skoolkit does not drop it
without a word (it does that to any label that is not at the start of a line).

A page you labelled in two windows (for example `$4000` and `$C000`) gets a file for each; the
labels of one window do not appear in the other's.

## Ghidra

Ghidra's `ImportSymbolsScript.py` reads lines of `NAME ADDRESS l`. Since Ghidra 12.1 the script is
part of the **Jython extension, which is not installed by default**: install it first (**File ▸ Install
Extensions…**, tick *Jython*), then run the script from the Script Manager and pick the file.

| File | Lines | Use |
|---|---|---|
| `view.ghidra.txt` | `main 0x8000 l` | the labels as the debugger shows them **now**; for a flat 64 KB image, no setup |
| `P04.ghidra.txt` | `main P04:0x8000 l` | the labels of one memory; needs an *overlay* named `P04` |
| `CPU.ghidra.txt` | `rst8 0x0008 l` | the labels that belong to no particular memory |

To use a per-memory file, create in the program's memory map an **overlay block** named exactly as the
memory (`P04`) over the range the page was mapped at, then import its file. The script stops at the
first line whose overlay does not exist, which is why there is a file per memory and not one big file:
import only the files whose overlays you made. An overlay covers the window you made; a page you
labelled in two windows has both addresses in its one file.

Ghidra's symbol import carries no comments and no code/data map.

## Import

**Map ▸ Import ▸ skoolkit Control Files…** (several files at once) or **Ghidra Symbols…** read labels
and comments into the same store as your own labels ([Your own labels and comments](16-user-labels-and-comments.md)),
so **Save Annotations** keeps them. An imported label replaces your label at the same address in the
same memory; the same for a comment. All the files are checked first: if any of them is wrong, the
message names the file and line and **nothing** is imported.

- **From skoolkit** jnext takes the `@ ADDRESS label=NAME` lines and the comment text of the
  instruction sub-blocks (`B`, `C`, `S`, `T`, `W`, with their `.` lines). The block titles
  (sna2skool makes its own), the descriptions and every other directive are counted in the report and
  not imported. A control file that `skool2ctl.py -k -h` made from your edited disassembly imports
  fine.
- **Which memory.** The file's `# jnext space` line, else its name (`P04.ctl`, `8000-P04.ctl`); a
  Ghidra line may carry `P04:`. A file with none of these (a control file of your own, say) puts each
  entry in **the memory that supplies that address at that moment**, which with
  `--import-skoolkit` on the command line is before the program is loaded.
- **Strict reading.** jnext refuses what skoolkit would only warn about and then ignore: an address
  that is not decimal, `$hex` or `%binary`, or is above 65535; a parameter skoolkit cannot read; an
  unknown directive; a label that is not a name; one name at two addresses.

## Command line

| Option | |
|---|---|
| `--export-skoolkit DIR` / `--export-ghidra DIR` | write at exit; a failure exits with status 1 |
| `--import-skoolkit FILE` / `--import-ghidra FILE` | read at start, after `--annotations`; repeat for several files; a bad file exits with status 1 |
| `--coverage` | record the code/data map |

See also the [command line reference](../../09-reference/01-command-line-options.md).
