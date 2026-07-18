# DRAFT GitHub feature issue — NOT FILED

> This is a **draft only**. It has deliberately **not** been filed on GitHub.
> The user reviews it and decides whether to file it, and in what form.
>
> Template: `.github/ISSUE_TEMPLATE/feature_request.yml` (title prefix `feat: `,
> label `enhancement`; note #29/#30 carry the label `feature`).
> Full evidence: [TASK89-ESXDOS-HOST-FILESYSTEM.md](TASK89-ESXDOS-HOST-FILESYSTEM.md).

---

**Title:** `feat: mount a host directory for esxdos file access from NEX programs and dot commands`

**Labels:** enhancement
**Milestone:** (suggest v1.1 — this is post-v1.0 scope)

---

### Problem / motivation

Getting a file to a program running under jnext means putting it inside an SD-card image.
Every iteration of a Next program that reads data files is: rebuild, `mcopy` into the `.img`,
boot, navigate. There is no way to point the emulator at a host directory and have the guest
see it.

What exists today is a stub, not file access. With `--esxdos-stub`, `RST $08` is intercepted
(`src/cpu/z80_cpu.cpp:655-672`) and exactly seven codes are serviced
(`src/core/emulator.cpp:885-1030`): `M_DOSVERSION`, `M_EXECCMD` (only `RUN <sibling>.nex`),
`F_OPEN`, `F_CLOSE`, `F_READ`, `F_WRITE`, and `F_SEEK` — the last **hardcoded to fail**
(`emulator.cpp:1024-1026`). The "file" is a single anonymous in-memory `std::vector<uint8_t>`
(`src/core/emulator.h:803`) with one handle. Nothing touches the host filesystem. There is no
directory concept at all.

### Proposed solution

A `--hostfs DIR` mount that backs the esxdos `RST $08` file calls with real host files,
confined to `DIR`.

**Phase 1 — real files.** Replace the single in-memory buffer with a handle table over host
files: `F_OPEN`, `F_CLOSE`, `F_READ`, `F_WRITE`, a genuinely working `F_SEEK`, `F_FGETPOS`,
`F_FSTAT`, `F_STAT`. Read-only by default; writes behind a second explicit flag.

**Phase 2 — directories.** `F_OPENDIR`, `F_READDIR`, `F_GETCWD`, `F_CHDIR`, `F_GETFREE`,
`M_GETDATE`, including 8.3 short-name synthesis and case-insensitive lookup (FAT is
case-insensitive, Linux is not).

Call codes above are taken from the authoritative definitions in
`tbblue/src/asm/dot_commands/esxapi.def:29-71` and z88dk's `esxdos.def`, not from memory.

**Sandboxing** (`resolve_sibling()` at `emulator.cpp:914-927` is the precedent but far too
narrow — it hardcodes `.nex` and forbids any subdirectory): canonicalise each guest path
against the mount root and re-verify containment after resolution, refuse symlink traversal,
and map the esxdos drive qualifiers `'*'` / `'$'` (`esxapi.def:129-130`) inside the root.

### Alternatives considered

**Intercepting at the DivMMC/SD block layer instead** — presenting a synthetic FAT32 volume
backed by a host directory, so NextZXOS's own driver sees a normal card. This is the *only*
design that would make the NextZXOS Browser see host files, and it is deliberately **not**
proposed here: it means synthesising boot sector, FATs and directory entries on the fly, and
jnext's FAT sensitivity is already on record (the canonical test image had to be re-clustered
because tbblue's FatFs correctly rejected an under-clustered volume — `roms/nextzxos-1gb-fat32fix.img`).
A subtly wrong synthetic volume produces boot failures indistinguishable from emulator bugs,
in a boot path that was expensive to get working. Estimated 6-10 sessions at high risk versus
~5 for Phases 1-2. Worth revisiting only if the smaller feature proves insufficient in practice.

**Keeping the `mcopy`-into-the-image workflow** — works today, and remains the only way to get
files to the NextZXOS Browser. It is simply slow to iterate against.

### Additional context

**Scope boundary — please read before assuming what this does.** Intercepting `RST $08` does
**not** turn a host directory into the SD card. NextZXOS carries its own SD/SPI block driver
in ROM, independent of the DivMMC ROM: scanning the shipped ROMs for the SPI port opcodes
(ports `$E7`/`$EB`, per `src/peripheral/spi.h:31-32`) gives `enNextZX.rom` 2× `OUT ($E7),A`,
16× `IN A,($EB)`, 4× `OUT ($EB),A`, and the same MMC `send_command` routine appears in both
`enNxtmmc.rom` (@`$1F10`) and `enNextZX.rom` (@`$98D6`) — first 60 bytes identical, then
diverging at a `CALL` consistent with relocation. `enNextZX.rom` also carries the FAT path
literals `"c:/nextzxos/autoexec.1st"` and `C:/NEXTZXOS/METADATA/`, which `enNxtmmc.rom` does not.

So this feature serves **directly-loaded NEX programs and dot commands** — dot commands do use
`RST $08` (`tbblue/src/asm/nexload/nexload.asm:650`, `rst $08 : db F_READ`). It does **not**
serve the NextZXOS Browser, whose I/O never reaches `$0008`. Booting still requires an SD image.

**Verification step before implementation starts:** boot NextZXOS with the `esxdos` trace
channel from #30 at TRACE, drive the Browser, and confirm no `RST $08` traffic appears. The
claim above that the Browser bypasses `RST $08` is *inference* from the duplicated driver —
strong, but the Browser inside `enNextZX.rom` was not disassembled. This ~half-session
experiment settles it and should gate the work.

**Known hazards to design around:**

- **Rewind/save-state.** An open host `fd` is not snapshottable. `src/peripheral/sd_card.h:173-178`
  already documents the same class of problem for the SD back end. Likely fix: snapshot only
  `(path, offset, mode)` per handle and reopen lazily. Rewinding across a *write* is not
  recoverable — the host side effect already happened. An argument for read-only by default.
- **Hook shadows real firmware.** The hook fires before the DivMMC automap runs
  (`z80_cpu.cpp:655` vs `:676`), so with firmware present, servicing a call host-side and
  letting the real handler run are mutually exclusive. A per-call policy is needed.
- **`M_P3DOS` ($94, `esxapi.def:45`)** is a bridge into arbitrary NextZXOS ROM entry points
  including `IDE_SECTOR_READ $00AC` (`nextzxos.def:10-68`) — raw sector access a host directory
  cannot answer. Programs relying on it are out of scope at this layer.
- **Timestamps.** `M_GETDATE` and `F_READDIR` entries carry FAT timestamps; host mtime and the
  emulated RTC disagree, notably under `--rtc` which pins emulated time for deterministic
  screenshots. Needs an explicit decision.
- **`F_GETFREE`** would report host free space — gigabytes; needs clamping to something a
  FAT-expecting guest tolerates.

Builds on #29 (extended NEX loading needs exactly this host-backed `F_READ`/`F_SEEK` machinery
for self-streaming payloads) and #30 (tracing is the instrument for the verification step above).
