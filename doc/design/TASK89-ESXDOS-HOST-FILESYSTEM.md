# Task 89 — Feasibility: full esxdos/NextZXOS emulation redirected to a host filesystem

**Status:** investigation only. No emulator code was written.
**Date:** 2026-07-18
**Branch:** `task89-esxdos-hostfs` (off `main` @ `895c1958`)

**Baseline assumption (per task statement):** Tasks 84 (#29, extended/self-streaming NEX
loading) and 85 (#30, esxdos syscall tracing) are assumed **implemented and working**.
Places where this design needs a specific detail of their interface are marked
**DEPENDENCY-TO-CONFIRM**, not as risks that they might not exist.

Every claim below is tagged:

- **PROVEN** — verified by reading the cited file, or by running a command whose output is quoted.
- **INFERRED** — reasoned from proven facts; plausible but not directly verified.
- **ASSUMED** — taken on faith; no evidence gathered.

---

## 0. Verdict up front

**Feasible, but NOT at the layer the task statement's framing suggests, and not cheaply.**

- Intercepting `RST $08` is **NOT sufficient** to mount a host directory as the SD card.
  It works for a *directly loaded NEX program* and for *dot commands*, and does **not** work
  for NextZXOS's own Browser/loader. Evidence in §2 — this is the single most important
  finding in this document.
- A genuine "host directory as SD card" requires a **synthetic FAT32 block device** at the
  DivMMC/SPI layer, which is a materially bigger and riskier piece of work.
- The two layers are **complementary, not alternatives**, and they serve different users.
- The **recommended first phase is the RST $08 layer only**, scoped honestly as
  "host directory for NEX programs and dot commands", *not* advertised as SD-card
  replacement. See §7.

---

## 1. How jnext intercepts esxdos today — PROVEN

### 1.1 The hook site

`src/cpu/z80_cpu.cpp:655-672`:

```cpp
if (pc == 0x0008 && on_esxdos_call) {
    uint16_t sp = z80.sp.w;
    uint16_t ret_addr = ... mem_.read(sp) | (mem_.read(sp + 1) << 8);
    uint8_t defb = mem_.read(ret_addr);
    ...
    if (on_esxdos_call(defb, r)) {
        r.PC = ret_addr + 1;
        r.SP = sp + 2;
        ...
        return 50;   // coarse T-state approximation
    }
}
```

**PROVEN properties of the hook:**

- It fires on **PC reaching `$0008`**, i.e. after the `RST $08` has already pushed its
  return address. The return address points at the `DEFB` hook code — matching the calling
  convention defined at
  `/home/jorgegv/src/spectrum/tbblue/src/asm/dot_commands/esxapi.def:11-14`:
  ```
  macro callesx,hook_code
          rst     $8
          defb    hook_code
  endm
  ```
- Returning `false` from the callback falls through to **whatever code is at `$0008`** —
  i.e. real firmware behaviour is preserved for unhandled codes
  (`src/core/emulator.cpp:1027`, `default: return false;`).
- The hook is installed **only** when `cfg.esxdos_stub` is set (`src/core/emulator.cpp:895`).
- Timing is a hardcoded 50 T-states (`z80_cpu.cpp:668`), explicitly commented as
  "a coarse average". **INFERRED:** any timing-sensitive guest would see wrong T-states;
  irrelevant for file I/O, relevant if this layer ever services `M_TAPEIN`/`M_TAPEOUT`.

### 1.2 What the stub services today — PROVEN

`src/core/emulator.cpp:885-1030`. The complete serviced set is **7 codes**:

| Code | Name | jnext behaviour | Source line |
|---|---|---|---|
| `$88` | `M_DOSVERSION` | reports `BC='NX'`, `DE=$0194` | `emulator.cpp:930-935` |
| `$8F` | `M_EXECCMD` | only `RUN <sibling>.nex` | `emulator.cpp:936-957` |
| `$9A` | `F_OPEN` | **one** in-memory file | `emulator.cpp:958-980` |
| `$9B` | `F_CLOSE` | always succeeds | `emulator.cpp:981-984` |
| `$9D` | `F_READ` | from the in-memory buffer | `emulator.cpp:985-1004` |
| `$9E` | `F_WRITE` | appends to the in-memory buffer | `emulator.cpp:1005-1023` |
| `$9F` | `F_SEEK` | **hardcoded failure**, `A=5` carry set | `emulator.cpp:1024-1026` |

Names/codes cross-checked against the oracle files, not recalled:
`esxapi.def:47-71` and
`/home/jorgegv/src/spectrum/z88dk-original-2.3/lib/target/zx/def/esxdos.def`
(`__ESX_F_OPEN = 0x9a` … `__ESX_F_GETFREE = 0xb1`).

**PROVEN:** there is **no host filesystem access whatsoever**. `esxdos_stub_file_` is a
`std::vector<uint8_t>` (`src/core/emulator.h:803`) — a single anonymous scratch buffer, with
a single handle (`esxdos_stub_handle_ = 1`, `emulator.h:805`). There is exactly one open
file at a time and no directory concept at all.

**PROVEN:** the only host-path machinery that exists is `resolve_sibling()`
(`emulator.cpp:914-927`), and it is used **only** by `M_EXECCMD`, not by `F_OPEN`. It
rejects absolute paths, any parent path, and any extension other than `.nex`:

```cpp
if (target.empty() || target.is_absolute() || target.has_parent_path() ||
    extension != ".nex")
    return std::string{};
```

---

## 2. THE CRUX — which layer? RST $08 or the block device?

### 2.1 The question

If NextZXOS's own file operations bypass `RST $08` and hit the SD block device directly,
then intercepting `RST $08` does **not** give a host-directory-as-SD-card.

### 2.2 What I established — PROVEN

**(a) `RST $08` is a DivMMC automap trigger address, enabled by default.**

Hardware decode is in `zxnext.vhd`, not `divmmc.vhd`
(`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd:2847-2884`):
the eight matched addresses are exactly the Z80 RST vectors `$0000..$0038`, and `$0008`
is bit 1 of NR `$B8`. Reset default `NR $B8 = $83` (`zxnext.vhd:5087-5090`) has bit 1 set,
so the `RST $08` trap is armed out of reset. The paging effect is
`divmmc.vhd:94-95` (`rom_en` / `ram_en`).

jnext models this faithfully — `src/peripheral/divmmc.cpp:399-421` reproduces the same
eight-address table and the valid/timing split, with the VHDL lines cited inline.

*Consequence:* the byte fetched **at** `$0008` comes from the DivMMC ROM, not the Spectrum
ROM. jnext's hook at `z80_cpu.cpp:655` fires **before** `on_m1_prefetch` runs the automap
(`z80_cpu.cpp:676`), so jnext intercepts the call **without** the DivMMC ROM ever being
paged in. That is correct for a stub, and is why the stub works with no firmware present.

**(b) The real `RST $08` handler lives in the DivMMC ROM.**

Verified by direct inspection of the ROM binary:

```
$ xxd -s 0x0008 -l 8 /home/jorgegv/src/spectrum/tbblue/machines/next/enNxtmmc.rom
00000008: c312 05e1 f5c3 6433
```

`C3 12 05` = `JP $0512`. **PROVEN.**

**(c) NextZXOS carries its OWN SD/SPI block driver, independent of the DivMMC ROM.**
*This is the decisive evidence.*

Scanning all three Next ROMs for the SPI port opcodes (`$E7` = chip select, `$EB` = data —
port assignment confirmed at `src/peripheral/spi.h:31-32`):

| ROM | `OUT ($E7),A` | `IN A,($EB)` | `OUT ($EB),A` |
|---|---|---|---|
| `enNxtmmc.rom` (DivMMC, 8 KB) | 3 | 8 | 3 |
| `enNextZX.rom` (NextZXOS, 64 KB) | 2 | **16** | 4 |
| `enNextMf.rom` (Multiface, 8 KB) | 1 | 0 | 0 |

`enNextZX.rom` contains a full complement of SD/SPI I/O of its own. Moreover the same
MMC `send_command` routine appears in **both** ROMs — first 60 bytes identical:

```
enNxtmmc.rom @0x1F10: 26002e00555d06ff4f3efe28023efdd3e7dbeb790eebed797ced797ded797a...
enNextZX.rom @0x98D6: 26002e00555d06ff4f3efe28023efdd3e7dbeb790eebed797ced797ded797a...
```

decoding as `LD H,0 / LD L,0 / LD D,L / LD E,L / LD B,$FF / LD C,A / LD A,$FE / JR Z,+2 /
LD A,$FD / OUT ($E7),A / IN A,($EB) / LD A,C / LD C,$EB / OUT (C),A / …` — select card,
then push the 6-byte MMC command frame.

**Correction to an over-strong claim:** the two copies are **not** byte-identical
throughout. They diverge after ~60 bytes at a `CALL` (`CD 3D 1F` vs `CD 25 19`) — different
absolute targets. **INFERRED:** this is the *same routine relocated to a different base
address*, which is exactly what an independently linked second copy looks like. I did not
disassemble both fully to prove functional equivalence.

`enNextZX.rom` also contains the FAT path literals `"c:/nextzxos/autoexec.1st"` (offset
`$a057`), `"c:/nextzxos/autoexec.bas"` (`$a073`), `C:/NEXTZXOS/METADATA/` (`$a956`), while
`enNxtmmc.rom` contains none. **INFERRED:** the FAT filesystem logic lives in the NextZXOS
ROM.

**(d) Dot commands DO go through `RST $08`.** PROVEN from source:
`/home/jorgegv/src/spectrum/tbblue/src/asm/nexload/nexload.asm:650` — `rst $08 : db F_READ`
(with `F_READ equ $9d` at `nexload.asm:109`). Likewise `browse.asm` uses `callesx` for
`m_dosversion`, `m_gethandle`, `m_errh`, `f_close`.

**(e) But `.browse` is only a shim.** PROVEN: `browse.asm:242,263,272,296,343,366` reach the
*real* browser via `callp3d ide_browser,7`, and `callp3d` expands
(`.../dot_commands/macros.def:24-29`) to `callesx m_p3dos` — i.e. `RST $08 / DEFB $94`
dispatching into NextZXOS ROM entry point `IDE_BROWSER $01BA`
(`.../dot_commands/nextzxos.def:10-68`, which also lists `IDE_SECTOR_READ $00AC` and
`IDE_SECTOR_WRITE $00AF`).

### 2.3 The answer to question 2

**Interception must happen at BOTH layers, and for "host directory as the SD card" the
block layer is the load-bearing one.**

Reasoning, in order of evidential strength:

1. **PROVEN:** NextZXOS has its own SPI driver and its own FAT paths in ROM (2.2c). It does
   not need `RST $08` to reach the card.
2. **INFERRED (strong):** NextZXOS's in-ROM Browser and loader therefore call their internal
   IDEDOS/+3DOS routines directly. `M_P3DOS` ($94, `esxapi.def:45`) exists precisely so that
   *external* code can reach entry points that internal code reaches with a plain `CALL`.
   A `RST $08` hook sees **none** of that traffic.
3. **PROVEN:** once NextZXOS is booted and the user navigates its Browser, the file I/O is
   NextZXOS ROM code → its own SPI driver → ports `$E7`/`$EB` →
   `src/peripheral/sd_card.cpp`. An `RST $08` hook is not on that path at all.

So:

- **RST $08 layer** — serves: a directly-loaded NEX program calling `F_OPEN`/`F_READ`;
  dot commands; z88dk `esxdos_*` library calls. Does **not** serve NextZXOS's own Browser.
- **Block layer (synthetic FAT32 volume backed by a host directory)** — serves
  *everything*, including the Browser, dot-command loading, `autoexec.bas`, and the
  DivMMC ROM's own driver, because it sits below all of them at the one true chokepoint.

**NOT PROVEN, flagged honestly:** I did not disassemble the Browser inside `enNextZX.rom`
to observe it calling internal routines rather than `RST $08`. Point 2 above is inference
from architecture plus the duplicated-driver fact. It is strong, but it is inference. If
someone wants certainty before committing to the block-device design, the cheap experiment
is: boot NextZXOS in jnext with Task 85's `esxdos` trace channel at TRACE, navigate the
Browser, and observe whether any `RST $08` traffic appears. **DEPENDENCY-TO-CONFIRM:** this
experiment needs #30's tracing to install its hook independently of `--esxdos-stub`, which
issue #30 states it does. *This experiment is the single highest-value next action and
should precede any implementation commitment.*

---

## 3. Realistic call surface for the RST $08 layer

Full published surface, from `esxapi.def:29-71` (33 codes). Classified by whether a
host-directory feature actually needs it:

### Load-bearing — must implement

| Code | Name | Why |
|---|---|---|
| `$9A` | `F_OPEN` | every file access starts here; needs real modes (`esxapi.def:78-84`) |
| `$9B` | `F_CLOSE` | handle lifecycle |
| `$9D` | `F_READ` | the point of the exercise |
| `$9F` | `F_SEEK` | currently hardcoded to fail; `esx_seek_set/fwd/bwd` = 0/1/2 (`esxapi.def:145-147`) |
| `$A3` | `F_OPENDIR` | any file browser or "list the directory" feature |
| `$A4` | `F_READDIR` | ditto; entry format incl. attributes (`esxapi.def:117-123`) |
| `$AC` | `F_STAT` | size/attribute queries before load |
| `$88` | `M_DOSVERSION` | programs gate on this; already stubbed |
| `$93` | `M_GETERR` | error-message retrieval; programs print it |

### Needed for write support

`$9E F_WRITE`, `$9C F_SYNC`, `$A2 F_FTRUNCATE`, `$AD F_UNLINK`, `$AA F_MKDIR`,
`$AB F_RMDIR`, `$B0 F_RENAME`, `$AE F_TRUNCATE`.

### Needed for a plausible-looking volume

`$A8 F_GETCWD`, `$A9 F_CHDIR`, `$89 M_GETSETDRV`, `$B1 F_GETFREE`, `$8E M_GETDATE`,
`$A0 F_FGETPOS`, `$A1 F_FSTAT`.

### Rarely used / can return `ENOSYS` ($14, `esxapi.def:181`)

`$85 DISK_FILEMAP`, `$86 DISK_STRMSTART`, `$87 DISK_STRMEND`, `$8B M_TAPEIN`,
`$8C M_TAPEOUT`, `$91 M_SETCAPS`, `$92 M_DRVAPI`, `$AF F_CHMOD`, `$A5 F_TELLDIR`,
`$A6 F_SEEKDIR`, `$A7 F_REWINDDIR`, `$8D M_GETHANDLE`, `$90 M_AUTOLOAD`, `$95 M_ERRH`.

### The one that decides scope

**`$94 M_P3DOS`** (`esxapi.def:45`). It is a *bridge into arbitrary NextZXOS ROM entry
points* (`nextzxos.def:10-68`), not a filesystem call. Servicing it properly means emulating
IDEDOS/+3DOS entry points including `IDE_SECTOR_READ $00AC` / `IDE_SECTOR_WRITE $00AF` —
i.e. raw sector access, which a host **directory** cannot answer at all. **INFERRED:** any
program using `M_P3DOS` for sector I/O is fundamentally incompatible with the RST $08
host-directory approach, and is an argument for the block-device design.

`$AC F_STAT` note: the z88dk oracle gives `__ESX_F_STAT = 0xac`, matching `esxapi.def:65`.
No name in this document was written from memory.

---

## 4. Where the hook gets installed

**PROVEN, the hook site is already boot-phase-agnostic**: `z80_cpu.cpp:655` triggers purely
on `pc == 0x0008`, with no boot-state condition. It fires whenever the guest executes
`RST $08`, whether that is 3 seconds into a NextZXOS boot or immediately after a NEX loads.

Two clean install points exist:

1. **After a NEX loads, before execution.** Clean and already exercised: `--esxdos-stub`
   plus `--load foo.nex`. **INFERRED:** this is the right point for the first phase — no
   firmware is involved, nothing else is competing for `$0008`.
2. **After NextZXOS has booted.** There is *no clean signal* for "NextZXOS has finished
   booting" in jnext today — I found none. **NOT PROVEN / open problem.** Candidate
   heuristics (PC entering a known NextZXOS range; first `M_DOSVERSION` from a dot command)
   are all fragile. And per §2 this install point buys little anyway, because the Browser's
   own I/O never reaches `$0008`.

**Conflict hazard — PROVEN by construction:** because the jnext hook fires *before*
`on_m1_prefetch` runs the DivMMC automap (`z80_cpu.cpp:655` vs `:676`), a hook that returns
`true` while real firmware is present **shadows the real handler entirely**. With NextZXOS
booted and the DivMMC ROM live, servicing a call host-side and servicing it firmware-side
are mutually exclusive per call. Any per-call policy (which of the two wins) is a design
decision that does not exist today.

---

## 5. Security / sandboxing

`resolve_sibling()` (`emulator.cpp:914-927`) is the only precedent, and it is **much too
narrow to reuse**: it hardcodes `.nex`, forbids *any* parent path (so subdirectories are
impossible), and resolves relative to the loaded NEX rather than a mount root. A host-
directory feature needs a real path-confinement routine. Requirements:

1. **Canonical containment.** Resolve the guest path against the mount root, then
   `std::filesystem::weakly_canonical` and verify the result is still under the root.
   Rejecting `..` textually is insufficient — symlinks defeat it.
2. **Absolute-path rejection or remapping.** esxdos paths may be drive-qualified —
   `esx_drive_current = '*'` and `esx_drive_system = '$'` (`esxapi.def:129-130`), plus
   the `c:/...` form seen in the NextZXOS ROM strings. All must map inside the root.
3. **Symlinks.** Either refuse to traverse them, or canonicalise and re-check. Refusing is
   safer and simpler.
4. **Case-insensitivity.** FAT is case-insensitive; Linux hosts are not, macOS usually is.
   A guest asking for `GAME.NEX` must find `game.nex`. Needs an explicit case-folding
   lookup, and a documented policy when two host files differ only in case.
5. **8.3 vs LFN.** `F_OPENDIR` mode bits distinguish them —
   `esx_mode_short_only $00`, `esx_mode_lfn_only $10`, `esx_mode_lfn_and_short $18`
   (`esxapi.def:94-96`). A host directory has no 8.3 names; they must be **synthesised**
   (`~1` suffixing), stably across runs, or short-name mode must be refused. Programs that
   open by short name will otherwise fail.
6. **Write confinement.** Creation/rename/unlink must be re-checked against the root
   *after* resolution, not before.
7. **Reserved names.** On Windows hosts, guest names like `CON`/`AUX` must not reach the
   host API.

**ASSUMED:** the feature would be opt-in via an explicit `--hostfs DIR` style flag, and off
by default. Nothing about it should be reachable without the user naming a directory.

---

## 6. What breaks or gets weird

**File handles across rewind/save-state — PROVEN to be a real problem.**
`src/peripheral/sd_card.h:173-178` already documents the shape of it:

> `NOTE: SdCardDevice intentionally has NO save_state/load_state. The rewind snapshot ring
> currently skips the SD back end. If this class is ever serialised, the CMD18 stream state
> ... must be included so rewinding mid-stream doesn't corrupt the host view.`

An open host `fd`/`ifstream` is not snapshottable. The existing stub sidesteps this by
holding the whole file in a `std::vector` (`emulator.h:803`) and by explicitly resetting
handle state on restore (`emulator.cpp:5993-5999`: `restore_esxdos_stub_state` sets
`esxdos_stub_file_pos_ = 0` and both open flags to `false`). That is fine for one small
buffer; it does not scale. Options: (a) store only `(path, offset, mode)` per handle in the
snapshot and reopen lazily on restore — **INFERRED** to be the workable approach;
(b) declare rewind unsupported while handles are open. Note rewinding *across a write* is
not recoverable at all — the host side effect already happened.

Other hazards:

- **Write semantics.** Guest writes are immediate and irreversible on the host. Combined
  with rewind, the emulator's timeline and the host filesystem's diverge permanently.
  A read-only default mount is the honest first position.
- **Concurrent access.** If the host user edits the directory while the guest holds a
  handle, guest-visible state is undefined. Directory enumeration is a snapshot at
  `F_OPENDIR` time; host churn mid-enumeration will be inconsistent.
- **Timestamps / RTC.** `M_GETDATE $8E` (`esxapi.def:38`) and `F_READDIR` entries carry FAT
  timestamps. jnext already emulates a DS1307 RTC from host time (per the design plan's
  I2C section). **Open question, NOT RESOLVED:** whether directory timestamps come from the
  host file's mtime or from the emulated RTC — they can disagree, notably under
  `--rtc "YYYY-MM-DD HH:MM:SS"` which deliberately pins emulated time for deterministic
  screenshots. Must be decided explicitly.
- **FAT attributes.** `esx_attr_read_only/hidden/system/volume_label/directory/archive`
  = `$01/$02/$04/$08/$10/$20` (`esxapi.def:103-108`). POSIX has no direct equivalent for
  hidden/system/archive. Synthesis rules needed (e.g. leading-dot → hidden).
- **Free space.** `F_GETFREE $B1` (`esxapi.def:71`) on a host directory would report the
  host filesystem's free space — likely gigabytes, possibly overflowing what the guest
  expects from a FAT volume. Needs clamping.
- **Timing.** The hook's flat 50 T-states (`z80_cpu.cpp:668`) makes host I/O appear
  instantaneous. Programs with loading-progress animations driven by expected I/O latency
  will look wrong. Cosmetic.

---

## 7. Effort estimate and phased plan

Estimates are **INFERRED** engineering judgement, in agent-sessions of focused work, and
assume the project's mandatory discipline (dedicated branch/worktree, VHDL-or-oracle-derived
tests, independent review, full green triplet).

**Phase 0 — settle the crux experimentally. ~0.5 session.**
Boot NextZXOS with #30's `esxdos` trace at TRACE, drive the Browser, record whether any
`RST $08` traffic appears. This converts §2.3's inference into a fact and decides whether
Phases 2/3 are needed at all. **Do this before committing to anything else.**

**Phase 1 — real host-backed files at the RST $08 layer. ~2-3 sessions. The smallest
genuinely useful phase.**
Replace the single in-memory buffer with a small handle table over real host files under a
`--hostfs DIR` root: `F_OPEN`, `F_CLOSE`, `F_READ`, `F_WRITE`, a **working** `F_SEEK`
(removing the hardcoded failure at `emulator.cpp:1024`), `F_FGETPOS`, `F_FSTAT`, `F_STAT`.
Plus the path-confinement routine from §5. Read-only by default; writes behind a second
flag. **Directly useful on its own** — it is also exactly the machinery #29 needs for
26 MB self-streaming NEX payloads, so it is shared work rather than speculative work.

**Phase 2 — directory surface. ~2 sessions.**
`F_OPENDIR`/`F_READDIR`/`F_GETCWD`/`F_CHDIR`/`F_GETFREE`/`M_GETDATE`, plus 8.3 synthesis
and the case-folding lookup. This is where a dot command like `.ls` starts working.

**Phase 3 — synthetic FAT32 block device. ~6-10 sessions, high risk.**
A read-only FAT32 view of a host directory presented at the SPI/`SdCardDevice` layer, so
NextZXOS's own driver sees a normal card. This is the only design that makes the **Browser**
work. It means synthesising boot sector, FATs, and directory entries on the fly and mapping
sector reads onto host file offsets — and jnext's FAT sensitivity is already documented
(the canonical test image had to be re-clustered because tbblue's FatFs correctly rejected
an under-clustered volume — see CLAUDE.md, `roms/nextzxos-1gb-fat32fix.img`). Getting this
subtly wrong produces boot failures that look like emulator bugs. Writes would be a further
large increment and are **not** recommended.

**Total for a credible "host directory as SD card": Phases 0-3, ~11-16 sessions.**
For "host files usable from NEX programs and dot commands": Phases 0-2, ~5 sessions.

---

## 8. What would make this NOT worth doing

- **If Phase 0 shows the Browser never touches `RST $08`** (the expected outcome), then
  Phases 1-2 must be *marketed accurately*. Shipping them under the banner "mount a host
  directory as the SD card" would be a false claim; users would try it with the NextZXOS
  Browser and find nothing. If the project is unwilling to ship the honestly-scoped
  smaller feature, stop here.
- **If Phase 3 is a precondition for anyone to care.** It carries the worst risk/reward in
  this document: a subtly wrong synthetic FAT32 breaks boot in ways indistinguishable from
  emulator bugs, in a boot path that took a long, painful investigation to get working
  (bank-7 aliasing, 2026-07-10). Destabilising that to save users a `mcopy` is a bad trade.
- **If the existing workflow is good enough.** Users can already put files on the SD image
  and boot it. The real pain is *iteration speed* while developing — which Phase 1 largely
  solves for the CLI/CI case without touching the block layer at all.
- **If write support is expected.** Host writes are irreversible and interact badly with
  rewind (§6). A read-write host mount is a footgun; if the feature is only interesting
  with writes, the cost/benefit worsens sharply.
- **If `M_P3DOS` turns out to be common in target software.** Programs doing raw sector I/O
  through it cannot be served by a host directory at any layer short of Phase 3.

---

## 9. Explicitly NOT verified — reported loudly

1. **NextZXOS's Browser was not disassembled.** §2.3 point 2 is inference. Phase 0 exists
   to settle it.
2. **The DivMMC ROM hook dispatcher at `$33B4`** (reached via `CALL $33B4` from the `$0512`
   handler) was not traced. Whether each `F_*` hook is implemented in DivMMC RAM/ROM or
   forwarded into NextZXOS ROM is **not established**.
3. **The two SPI routines were not proven functionally equivalent** — only the first ~60
   bytes were compared, and they then diverge at a `CALL` (§2.2c).
4. **No NextZXOS or DivMMC ROM source exists** in the tbblue tree — only binaries under
   `machines/next/`. All ROM-internal claims here come from binary inspection.
5. **Whether NextZXOS rewrites NR `$B8`/`$B9`/`$BA`/`$BB`** at runtime to change the
   `RST $08` trap configuration was not investigated. If it disables the trap, jnext's hook
   still fires (it is unconditional on `pc == 0x0008`) — a **divergence from hardware** that
   nobody has evaluated.
6. **Nothing was built or run.** This document is source and binary inspection only; no
   `make worktree-bootstrap`, no tests executed. No claim here rests on emulator runtime
   behaviour.
