# esxDOS calls that change files on the host folder (GH #457)

Design and use case, written before the code (feature flow), kept as the record of
what was decided and why. The behaviour as built is in the man page, the user guide
(section "Host files for a directly loaded program") and the developer guide
(subsystem "A host directory"); this file carries the reasoning and the decisions.

## Use case

A program developed against a host folder (`--esxdos-stub --load game.nex
--esxdos-stub-writable`): a game that deletes or renames its save slots, makes its
data directory, truncates a log, protects a file. Written in C with z88dk
(`esx_f_unlink`, `esx_f_rename`, `esx_f_mkdir`, `esx_f_rmdir`, `esx_f_truncate`,
`esx_f_ftruncate`, `esx_f_chmod`, `esx_m_geterr`), or in assembly, it works on the
Next and must work in the emulator, with the same answers, including the error
codes a program branches on. Until now every one of those calls failed on the host
folder (`esx_enonsense`) although it works on the real machine.

Shrek's WebAssembly port carried an implementation. Nothing is copied from it:
every answer here is jnext's own measurement.

## The calls

| Call | Hook | Registers (z88dk `asm_esx_f_*.asm`) |
|---|---|---|
| `F_UNLINK` | `$AD` | A=drive, IX=filespec (wildcards) |
| `F_RENAME` | `$B0` | A=drive, IX=source, DE=destination |
| `F_MKDIR` / `F_RMDIR` | `$AA` / `$AB` | A=drive, IX=path |
| `F_TRUNCATE` | `$AE` | A=drive, IX=filespec, BCDE=new size |
| `F_FTRUNCATE` | `$A2` | A=handle, BCDE=new size |
| `F_CHMOD` | `$AF` | A=drive, IX=filespec, B=attribute values, C=attributes to change |
| `M_GETERR` | `$93` | A=code (0: IX=custom message), B=1, DE=32-byte buffer |

All exit Fc=0 on success, Fc=1 and the code in A on error.

## The oracle: a probe, not VHDL

There is no VHDL for this: NextZXOS is software. The oracle is real NextZXOS 2.09
booted in jnext, running a probe NEX started with `.nexload`, with every answer
recorded. Three runs were identical byte for byte.

- `test/00regression/esxdos-write-probe.py` generates the probe: a table of calls
  and an interpreter that prints one line per call through the magic port. The
  same program runs against NextZXOS and against jnext's host folder.
- `test/fixtures/esxdos-write-probe/nextzxos.txt` is the recording, with the image
  sha256, `M_DOSVERSION` (proving 2.09) and the sha256 of both probe NEX files in
  its header. `esxdos-write-probe-measure.sh` re-measures.
- Nothing in the transcript depends on a listing order or a timestamp: state is read
  back by `F_STAT` of named entries (and by `F_READDIR` for directory attributes).
  `A` on success is masked: NextZXOS leaves junk there.
- The write-protected answers come from the same card mounted
  `--sdcard-readonly`, which refuses every block write like a protected card.
- No SD image is ever committed: the measure script makes a scratch copy and deletes
  it.

## What was measured, and what it decided

Where the recording differed from the hypotheses the work started from:

- A path above the root (`..`) is `esx_enotdir` when more path follows, for every
  call (the existing escape rows moved from `esx_epath` to it, owner-approved on
  exactly this evidence). A bare `..` is what a missing last name is: `esx_enoent`
  for lookups, `esx_einval` for a create, `esx_enotdir` for `F_OPENDIR`/`F_CHDIR`.
  `F_STAT` of a bare `..` is `esx_enoent` too (the owner approved changing the older
  row that pinned `esx_epath`, which left the enum).
- A write-protected volume answers `esx_edevicebusy` (31), after the checks that
  do not write. `F_TRUNCATE` there answers success and loses the write; jnext
  refuses with 31 (declared: a write that did not happen is not an answer to copy).
- `F_MKDIR` of the root is `esx_einval`, not `esx_eexist`.
- `F_TRUNCATE` accepts a wildcard and truncates the FIRST match.
- `F_CHMOD`: `A_WRITE` is the inverse of read-only, `A_READ` inert, hidden and
  system are set together, the archive bit is storable, directories' attributes
  show in `F_READDIR`. jnext serves `A_WRITE`, `A_READ`, `A_ARCH` set; the rest is
  `esx_enosys` (refused, not faked: `F_STAT` would otherwise contradict the answer).
- `F_RENAME` with an empty destination moves into the current directory; a renamed
  current directory keeps its old name in `F_GETCWD` on a card.
- `" < > |` are `esx_einval` in every call; a trailing dot is dropped; a trailing
  space is kept by NextZXOS and refused by jnext (a host may strip it).
- 8.3 aliases are fixed at creation, a new name takes the lowest free tail, a renamed
  file gets a fresh one.
- `M_GETERR`: Fc=0, A=$02 for every code; the texts are in `esxdos_trace.h`.

## Declared divergences (each a tag in the generator's `DIVERGE` table)

- `cwd-follows-rename`: a renamed current directory shows its new name in `F_GETCWD`.
- `trailing-space`: a created name ending in a space is `esx_einval` (a host may strip it).
- `chmod-unserved`, `chmod-dir`: `F_CHMOD` changes the host cannot store are `esx_enosys`.
- `alias-create`: creating through the 8.3 alias of an existing long-named file
  rewrites that FAT entry and loses the long name; jnext truncates the file it names
  and keeps its host name.
- `open-mode-no-rw`: an `F_OPEN` mode with no read and no write bit (`$04`, `$08`,
  `$0C`) is `esx_einval` here (existing behaviour); NextZXOS opens or creates.
- `ro-truncate`: `F_TRUNCATE` on a write-protected root is refused with
  `esx_edevicebusy`; NextZXOS answers success and loses the write (owner-kept).
- `ro-open-write`: `F_OPEN` for writing or creating on a write-protected root is
  `esx_erdonly`; NextZXOS opens a writable file for writing and refuses a create
  with `esx_edevicebusy` (owner-kept).

## Hazards the calls create, fixed with them

- Unstable 8.3 aliases: renumbering on every listing makes a delete by short name hit
  another file once a neighbour is gone. Aliases are now kept per directory.
- Host-only aliases: a case-insensitive host, an NTFS short name or a normalised
  spelling reached a different file than the listing shows. A name counts only if the
  listing holds it. A name already held in a rename's target directory is judged by
  the listing too (any case, and 8.3 aliases both ways), and a renamed directory
  keeps the aliases of what is inside it.

## Residuals

- The NEX a kept-open NEX reads through the extended-NEX bridge is not an open file of
  the host folder and is not protected from `F_UNLINK`/`F_RENAME`.
- Names with bytes >= `$80` pass to the host byte for byte (as `F_OPEN` create does).
- Rewind cannot undo a host change; a handle whose file is gone is not reopened.
