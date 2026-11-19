# Running a NEX With No SD Card (`--no-sdcard`)

> Status: **implemented** (all frontends; the browser as `card: { source: 'none' }`).
> Issue: GH #540.
> Tests: `test/core/no_sdcard_test.cpp` (NOSD-ROM-01..07, NOSD-DEV-01..04),
> `test/gui/no_sdcard_ui_test.cpp` (NOSDUI-01..03),
> `test/platform/web_api_test.cpp` (WAPI-CARD-07),
> `test/browser/card.mjs` (BCARD-45..47), regression row `no-sdcard-func`.

## 1. Use case

Many NEX games need nothing from NextZXOS but the esxDOS file calls
(`F_OPEN`, `F_READ`, `F_CLOSE`, ...). They install their own IM 2 interrupts
and never call a ROM routine. JNEXT already answers those calls itself (the
esxDOS stand-in, `--esxdos-stub` / `--esxdos-stub-root`, intercepted at
`$0008` before any ROM byte is fetched), so for these programs the ROMs and
NextZXOS do nothing.

They still cost a lot, and the browser shows it most. Before this change every
visitor to a page with a 250 KB game first had to get `sn-emulator-26.11.zip`
(67 MB) from specnext.com, or download it and choose it by hand. Embedded on a
host like itch.io, where the player sits in a cross-site iframe, the cached
card may not survive between visits in some browsers, so the visitor can be
asked again. On the desktop, the first run of such a game downloads the same
image.

The core already had the machine this needs: `init_for_load_from_file()`
falls back to the synthetic machine when there is no SD image, and
`Emulator::init` loads no ROM without one. Only `main.cpp`, which refused to
start without a card, and the page layer, which always obtained one, stood in
the way.

## 2. Behaviour

`--no-sdcard` (no argument), or `card: { source: 'none' }` in a page
(`data-card-source="none"` on the widget):

| situation | what happens |
|-----------|--------------|
| `--no-sdcard --load game.nex` | no card is provisioned (no download, no prompt); the warm start is skipped with an info line, not the "no SD image is mounted" error; the NEX starts on the synthetic machine; `--esxdos-stub` is implied, so the NEX's own directory is served unless `--esxdos-stub-root` names another |
| the program fetches an opcode from a ROM (IM 1 at `$0038`, `RST $10`, a call into the ROM) | the first time only, an error is logged, e.g. `executed ROM code at $0038 (IM 1); --no-sdcard has no ROM`; in a page it is also reported through `onError` (the widget's status line). The program then runs whatever the empty ROM holds |
| RAM the program mapped over `$0000-$3FFF` (NR 0x50 / 0x51) | not a ROM: nothing is reported |
| DivMMC and Multiface | held off whatever NR 0x83 says: their ROMs come from the card |
| F9 / F10 (the NMI buttons), the debugger's NMI buttons | refused, with a warning in the log |
| hard reset (F1, File > Reset, a program's NR 0x02) | the boot ROM runs, finds no card and stops at its own "Error initializing SD card!" screen, as hardware does. The program is not loaded again; that is a separate feature |
| File > Insert SD Card Image... / Eject SD Card | disabled; the status bar shows **SD: none** |

Refused at start, each with its own message (exit status 1):

| combination | message |
|-------------|---------|
| any `--sdcard*` option, `--warm-start-regenerate`, `--delayed-sdcard-insert-frames` | `--no-sdcard cannot be combined with X: there is no SD card.` |
| `--delayed-nmi` | `--no-sdcard cannot be combined with --delayed-nmi: there is no ROM to service an NMI.` |
| a machine other than the Next | `--no-sdcard needs the Next: the other machines run from ROMs on the SD card.` |
| no `--load`, or a file that is not a `.nex` (directly or in a `.zip`) | `--no-sdcard needs --load of a .nex (directly, or in a .zip): with no SD card there are no ROMs to run anything else.` |

In a page, `card: { source: 'none' }` is refused at `mount()` without `file`,
with `keepWrites` (there is no card to keep the writes of) and with `dirs`
(there is no card to add them to). The existing
message for an unknown source keeps its wording (`source must be 'auto',
'fetch' or 'pick'`): pages and tests match it.

This is an **emulator convenience**, like the esxDOS stand-in: a real Next
always has its ROMs. A program that needs one (a z88dk program with the default
IM 1 start-up, one that prints with `RST $10` or reads the ROM font) does not
work in this mode, and the report says why.

## 3. Design

**Where the mode lives.** `EmulatorConfig::no_sdcard`, set by `main.cpp` from
the option, which it validates first (section 2). With it, the SD provisioning
block is skipped and Preferences' card path is not used. The browser's
`web_api::build_argv` turns `card.source == 'none'` into `--no-sdcard`, and
`jnext_api.js` obtains no card (`obtainCard` resolves to none, `$card` is
omitted).

**The ROM report.** `Emulator::rom_fetch_watch_step_()` runs from the one
`cpu_.execute()` site that `run_frame()`, the debugger's Step and
`execute_single_instruction()` share, behind one bool (`rom_fetch_watch_`),
armed by `init()` in this mode only and cleared by the first report. It asks
what supplied the opcode, `Mmu::last_m1()` (GH #339), not what the address
is, so RAM mapped at `$0000` is not reported and the boot ROM (`BootRom`) is
not either. The kinds reported are `Rom`, `AltRom`, `DivMmcRom` and
`MultifaceRom`. A slot that fetched nothing (an INT, an NMI, the esxDOS
stand-in) only records its kind, which names the cause of the next fetch:
`$0038` after an accepted INT is IM 1, `$0066` after an NMI is the NMI. Any
other address in `$0000-$0038` with the restart pattern is named `RST $nn`;
the rest is "a call or jump into the ROM". `take_rom_fetch_report()` hands the
message over once: `web_controller::after_tick()` raises it as an `error`
event, which `jnext_api.js` delivers through `onError`.

**DivMMC and Multiface.** Both are gated by NR 0x83 bits 0 / 1 in one place,
`propagate_effective_port_enables()`. The mode ANDs both gates with false
there, so automap never pages, port `$E3` is not decoded and the Multiface
stays in reset, whatever the program writes.

**NMI.** `on_hotkey_f9_mf_nmi()` and `on_hotkey_f10_divmmc_nmi()` refuse
before strobing the NMI source. Holding the Multiface off is not enough on its
own: `NmiSource` would still assert /NMI and the CPU would go to `$0066`
(NOSD-DEV-02 fails without the refusal).

**Card changes.** `sd_card_change_refusal()` refuses in this mode, so the
File menu, `--delayed-sdcard-insert-frames` and any later frontend share one
answer. `MainWindow::set_emulator()` disables the two actions and shows the
status cell, which is hidden otherwise, so the status bar of every other
session is unchanged.

**Hard reset.** A cold boot drops the load (`load_file` empty). The boot ROM
was installed only with a card image; the mode now counts as that gate, so
the boot ROM runs and stops at its own no-card screen instead of the CPU
running an empty ROM from `$0000`.

## 4. Not in scope

- Re-loading the program on a hard reset.
- Serving a zip's other members to the esxDOS stand-in (GH #541): today a page
  unpacks the game's data files itself.
- ROM *data* reads (the ROM font at `$3C00`): only opcode fetches are reported.
