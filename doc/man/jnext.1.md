---
title: JNEXT
section: 1
header: jnext Manual
footer: jnext
...

<!-- SINGLE SOURCE for the jnext CLI/usage reference. It generates BOTH -->
<!-- doc/man/jnext.1 (roff, installed to $mandir/man1) and USAGE.md -->
<!-- (repo-root markdown rendering). Do not edit either output by hand: -->
<!-- edit this file and run `make docs` (needs pandoc; see BUILD.md). -->
<!-- Both outputs are committed, so building from source never needs the -->
<!-- doc toolchain. No version or date is embedded on purpose - it would -->
<!-- go stale every release and drag this file into sync-version.sh; -->
<!-- users get the version from `jnext --version`. -->
<!-- One comment per line on purpose: a blank line inside a single HTML -->
<!-- comment block comes out as a literal newline entity in USAGE.md. -->

# NAME

jnext - real-time ZX Spectrum Next emulator with an integrated debugger

# SYNOPSIS

**jnext** \[*options*] \[*file*]

# DESCRIPTION

**jnext** is a real-time, cross-platform ZX Spectrum Next emulator written in
C++17 and derived from the official VHDL sources of the ZX Spectrum Next FPGA
core. It emulates the Next itself as well as the ZX Spectrum 48K, 128K and
+2A/+3 machines, and carries a debugger that shows CPU, memory, video layers,
sprites, audio and NextREG state live.

*file* is a program to load, exactly equivalent to **\--load** *FILE*, so
`jnext game.tap` just works. Anything starting with `-` is treated as an
option, never as a filename. Name the program one way or the other, not both
at once: `jnext --load game.tap game.tap` is rejected, and so is naming two
files.

The examples below use the Qt6 GUI build (`build/gui-release/jnext`). The
SDL-only build (`build/sdl-release/jnext`) takes the same options minus the GUI and
debugger ones.

# OPTIONS

A numeric value is taken whole. Trailing characters (`5x`), a value outside the
option's range, or a negative count are a usage error: **jnext** prints a message
naming the option and the value, and exits with status 1.

## Machine and program

**\--machine** *TYPE*
:   Machine type: `48k`, `128k`, `plus3`, `pentagon`, `next` (default). A
    snapshot given to **\--load** (`.jns`, `.sna`, `.szx`, `.z80`) runs on the
    machine it was saved on instead, and **jnext** warns when that is not the
    one given here.

**\--load** *FILE*
:   Load a program. The format is detected from the extension: `.nex`, `.jns`,
    `.sna`, `.szx`, `.z80`, `.tap`, `.tzx`, `.wav`, `.rzx`. (`.rzx` is accepted
    here and plays back, as **\--rzx-play**.) A `.zip` archive holding one of
    them loads that program, and a NEX in one also gets its directory in the
    archive (see the `.zip` entry of **LOADING PROGRAMS**).

    `.jns` is jnext's own whole-machine snapshot (see **JNEXT SNAPSHOTS**
    below). Loading one restores the machine it was taken on, including the
    machine *type*: a `.jns` taken on a Next loads as a Next even if
    **\--machine** says otherwise. A `.sna`, `.szx` or `.z80` does the same: it
    runs on the machine it was saved on (the file says which: a `.sna` by its
    size, an `.szx` by its machine ID, a `.z80` by its hardware mode), even over
    **\--machine**, which is then warned about. The machine stays selected,
    in the GUI's **Machine > Machine Type** too. A snapshot from a machine
    **jnext** does not emulate (a Timex, a Scorpion, a Pentagon 512 or 1024), or
    taken with the TR-DOS, Interface 1, MGT or Multiface ROM paged in, is
    refused, with the reason in the log, before anything is changed; at startup
    **jnext** then exits non-zero. A 16K, +2, +2A or +3e snapshot runs as the
    48K, 128K, +3 or +3, and an NTSC 48K `.szx` as a 48K at 60 Hz.

    A NEX file with bytes after the banks its header declares is an *extended*
    NEX. If its header asks for the file to be kept open (any non-zero
    `file_handle`: 1 to 0x3FFF hands the program the handle in `BC`, 0x4000 and
    above writes it at that address), the program can read the extra bytes from
    its own file. If the header says `file_handle` 0, jnext
    does what the Next's own loader does: it loads only the declared banks,
    ignores the trailing bytes and starts the program. More than 16 KB of
    ignored bytes also logs a warning.

    The debugger's source map and symbols attach from beside the program:
    *NAME*`.sld` (or *NAME*`.sld.txt`), an sjasmplus source map whose labels join
    the symbols (a label assembled for a page names its address only while that
    page is mapped), beside any program, *NAME*`.Memory.txt` (or `Memory.txt`), NextBuild /
    Boriel ZX Basic symbols, beside a `.nex`, and *NAME*`.map`, a z88dk map
    (`-m`), beside a `.nex`, `.tap` or `.sna`: its symbols join the others (not
    when a `Memory.txt` exists; a symbol of a `BANK_n` or `PAGE_n` section names
    its address only while its page is mapped, the others take the page mapped at
    their address when the program is loaded, from tape too), and with `-debug` also its source lines, which
    attach when there is no SLD. z88dk's **-create-app** also
    writes the section binaries (*NAME*`_CODE.bin`, ...) that the map is checked
    against; without them it attaches unverified. Beside a tape (`.tap`, `.tzx`,
    `.wav`), whose program is not in memory yet, a map that carries a binary
    identity attaches at once and each memory space it covers is checked the first
    time the program runs its code there; a space that does not match drops the map
    and the symbols that came with it, with the reason in the log. A map given with **\--map** or
    loaded by hand is kept over them. z88dk's `--list` listings beside the map
    (*NAME*`.lis`, *FILE*`.lis`) are read too when they match the map, and add
    assembly lines and sdcc's lines on z88dk 2.4; a listing that does not match
    is skipped with a note in the log. A map built for another binary, or a
    file over 64 MiB, is refused, with the reason in the log.

**\--nex-args** *LINE*
:   Argument line for a NEX **V1.3** program. *LINE* is placed, verbatim and
    zero-terminated, in the CLI buffer the file's header declares (its address
    and size). When the program starts, `DE` holds that address plus the size -
    one past the buffer - which is where the reference V1.3 loader leaves it
    (the V1.3 header comment says the address itself). A line as long as
    or longer than that buffer is truncated to the buffer's size with no
    terminator - which is what the reference V1.3 loader does. Quote *LINE* to
    pass more than one word: `jnext --load game.nex --nex-args "level 3"`.
    Only V1.3 declares a CLI buffer, so the option is inert - with a warning -
    for a `V1.0`-`V1.2` file, or a V1.3 file whose header declares no buffer.

**\--experimental-nex-v1.3**
:   Allow loading NEX **V1.3** files. NEX V1.3 is an experimental format and
    is **not supported in any way**; without this flag, loading a NEX file
    whose header version exceeds `V1.2` is refused with an error naming the
    version found (exit status 1). In the GUI, selecting such a file shows a
    warning dialog instead, with Cancel as the default. `V1.0`-`V1.2` files
    are unaffected either way.

**\--sdcard** *FILE*
:   Mount SD-card image *FILE* (`.img`). Optional; see **SD CARD AND ROMS**.
    A ZIP archive is not an image and is refused (exit status 1): unzip it
    first. The WebAssembly build extracts the single `.img` inside one.

**\--sdcard-sha256** *HEX*
:   The SHA-256 of the **\--sdcard** image, as the caller has already checked
    it. The warm-start recording is keyed on it instead of on the image's size
    and modification time. **jnext** does not hash the image: *HEX* must
    describe the file as it is. The browser build uses this option, because its
    page rewrites the card into memory on every visit (so the time changes)
    and has already checked the hash. *HEX* is 64 hexadecimal digits, in either
    case. Requires **\--sdcard**. A recording taken after **jnext** has written
    to the card in this run is used, but not cached.

**\--sdcard-download-confirm**
:   Skip the download prompt for the default-location image and proceed.

**\--sdcard-download-force**
:   Force re-download and re-patch of the default-location image, to recover a
    corrupted one. Also discards a half-finished download. Ignored when
    **\--sdcard** is given: an explicit path always wins.

**\--sdcard-upgrade** *MODE*
:   What to do when the default-location card is of an older NextZXOS
    distribution than the one jnext downloads now. *MODE* is **ask** (the
    default in the GUI: forget any "do not ask again" and ask), **yes** (upgrade
    without asking: your files are carried over to the new card and the old
    card is kept as a backup in `previous/`; refused when the card has no
    original image to compare with, and then **fresh** is the way), **fresh**
    (a new card with nothing carried over; the old card is kept as the backup),
    **no** (keep the card for this run, no question) or **never** (keep it and
    do not offer this distribution again). Without the option, the GUI asks
    (once, unless "Keep my card (don't ask again)" was chosen) and **\--headless** keeps the card and
    logs one line. Refused together with **\--sdcard**: an explicit card is
    never offered an upgrade. See **SD CARD AND ROMS**.

**\--sdcard-readonly**
:   Open the SD image read-only, so the host file is never modified. The
    emulated machine sees a write-protected card: writes are rejected with the
    SD write-error token rather than silently discarded. Use it when a run must
    not disturb an image other runs share.

**\--sdcard-writes** *FILE*
:   Keep the writes the emulated machine makes to the SD card in *FILE*, not in
    the image: the image is opened read-only and never changes, and *FILE*'s
    sectors are laid over it whenever the card is mounted, so the writes
    survive the next run. *FILE* belongs to one image, identified by its
    **\--sdcard-sha256** digest or else by its size and modification time: a
    *FILE* made against another image is refused (exit status 1), and so is a
    file that is not a **jnext** SD-writes file. It is created at the first
    write and holds only the sectors the machine wrote (516 bytes each) and the
    one file jnext's esxDOS stand-in keeps for a directly loaded NEX (see
    **esxDOS calls from a directly loaded NEX**), stored when the program
    closes it and handed back to the stand-in at the next start, whatever the
    program: it belongs to the card, not to the NEX that wrote it.
    Excludes **\--sdcard-readonly**; with **\--sdcard-file-add** the copy goes into
    *FILE* and the image is not written. Do not change the image with another
    program while *FILE* has writes for it (hdfmonkey, mtools, a loop mount, a
    copy over it): *FILE* would no longer fit the image, and the card the machine
    sees would be damaged. Use the SD card file manager (File menu), which writes
    into *FILE*. JNEXT warns once, with a message box and in its log, when the
    image of a card with a writes file changes (or is replaced, deleted or resized) while
    it runs, and it refuses *FILE* at the next start ("was made for another
    image, or the image was changed by another program since"). The same applies
    to **\--sdcard2-writes**. A recording of
    the warm start is keyed on the image and on *FILE*'s sectors, so it is
    taken again after the machine has written one; the stand-in's file is not
    card state and does not change it. Files read from the image by
    the host (the ROMs, a snapshot's card identity) do not see the writes. See
    **SD CARD AND ROMS**.

**\--sdcard-writes-reset**
:   Discard the writes kept in the **\--sdcard-writes** *FILE* before the card
    is mounted, the stand-in's file included, so the run starts on the image as
    it is. Requires
    **\--sdcard-writes**. A file that is not a **jnext** SD-writes file is
    refused, never removed.

**\--no-sdcard**
:   Run a directly loaded NEX with no SD card at all: no NextZXOS, no ROMs and
    no warm start. An emulator convenience, like the esxDOS stand-in: a real
    Next always has its ROMs. The NEX starts on the synthetic machine
    **\--load** has always assembled when there is no firmware, and every
    esxDOS call it makes is answered by the stand-in: it implies
    **\--esxdos-stub**, so the NEX's own directory is served, unless
    **\--esxdos-stub-root** names another. For a program that uses no ROM
    routine and no NextZXOS service beyond the esxDOS file calls. **The ROM area
    is empty**: the first time the program executes code from a ROM (an IM 1
    interrupt at \$0038, **RST \$10**, a call into the ROM) **jnext** logs an
    error naming the address and the likely cause, e.g. `executed ROM code at
    $0038 (IM 1); --no-sdcard has no ROM`, and the program will hang or show
    garbage. DivMMC and the Multiface are held off, the NMI buttons are
    refused, File > Insert SD Card Image..., Eject SD Card and their second-card
    twins are disabled and the status bar shows **SD: none**. A hard reset runs the boot ROM, which
    stops at its no-card screen as on hardware; the program is not loaded
    again. Requires **\--load** of a **.nex** (directly or in a **.zip**) on
    the Next (a machine saved in Preferences other than the Next is refused
    too: give **\--machine next**), and excludes every **\--sdcard** and
    **\--sdcard2** option, **\--warm-start-regenerate**,
    **\--delayed-sdcard-insert-frames**, **\--delayed-sdcard2-insert-frames**
    and **\--delayed-nmi**. In a browser page it is `card: { source: 'none' }`.

**\--sdcard2** *FILE*
:   Mount SD-card image *FILE* (`.img`) in the **second SD card slot**, which
    NextZXOS sees as the next drive after the first card's partitions (D: for a
    first card with one). There is no default and no download: the slot is empty
    unless this is given. ROMs are read from the first card only. A *FILE* that
    is not a readable regular file, a ZIP archive, or the same file as
    **\--sdcard**, is refused (exit status 1). See **A SECOND SD CARD**.

**\--sdcard2-readonly**
:   Open the second card's image read-only, as **\--sdcard-readonly** does for
    the first. Requires **\--sdcard2**.

**\--sdcard2-writes** *FILE*
:   Keep the writes the emulated machine makes to the second card in *FILE*
    instead of its image, as **\--sdcard-writes** does for the first card; *FILE*
    belongs to that image, identified by its size and modification time.
    Requires **\--sdcard2**; excludes **\--sdcard2-readonly**.

**\--sdcard2-writes-reset**
:   Discard the writes kept in the **\--sdcard2-writes** *FILE* before the card
    is mounted. Requires **\--sdcard2-writes**.

**\--sdcard-file-add** *FILE*
:   Copy host *FILE* — a file, or a directory with everything in it — into the
    SD-card image and **exit without starting emulation**. Without
    **\--sdcard-file-dest** it lands in the root of the card under its own
    name. Give it once per run. The image written is the one **\--sdcard**
    names, or the default-location image when **\--sdcard** is omitted (your own
    card). With **\--sdcard-writes** the copy goes into that file instead
    and the image is not written. See **PUTTING A FILE ON THE CARD**.

**\--sdcard-file-dest** *PATH*
:   Optional. The path on the card that **\--sdcard-file-add**'s *FILE*
    becomes, from the root of the card (`/NEXTZXOS/DRV-A.DSK`). The leading `/`
    is optional, directories are separated with `/`, and any missing directory
    in the path is created. For a directory, `/` copies its contents straight
    into the root of the card.

**\--sdcard-file-force**
:   Let **\--sdcard-file-add** replace files that are already there. Without it
    an existing destination file is refused and nothing is written.

**\--sdcard-file-extract** *PATH*
:   Copy the file, or the directory with everything in it, at *PATH* on the SD
    card (`/GAMES/game.nex`; long or 8.3 names, any letter case) into the
    **current directory** under its own name, and **exit without starting
    emulation**. A local file or directory of that name is refused and left as
    it is. Give it once per run. It reads the card **\--sdcard** names, or the
    default-location image when **\--sdcard** is omitted, as a run with the same
    options would see it (with **\--sdcard-writes**, through that file). The image
    is never written; the writes file keeps its contents (as for any run given it,
    it may be compacted). See **GETTING YOUR FILES OUT**.

**\--sdcard-writes-flatten** *OUT*
:   Write *OUT*, a plain image of the card with the **\--sdcard-writes** *FILE*
    laid over it (the card as the machine sees it), and **exit without starting
    emulation**. Any FAT tool (mtools, a loop mount, 7-Zip) can open *OUT*.
    Requires **\--sdcard-writes**. An *OUT* that already exists is refused.
    See **GETTING YOUR FILES OUT**.

**\--sdcard-go-back**
:   Go back to the previous SD card that an upgrade kept in `previous/` next to the
    default card, and **exit without starting emulation**. It is a swap: the card
    that was in use becomes the backup, your **\--sdcard-writes** files go with
    their cards, and the distribution you left is not offered again. Exits 0 when
    done; 2 when there is no previous card; 3 when it failed and nothing was
    changed; 4 when the card is in use by another jnext. Refused together with
    **\--sdcard**, another SD card command, **\--load** or **\--inject**.

**\--sdcard-delete-backup**
:   Delete the previous SD card an upgrade kept in `previous/`, giving its disk
    space back, and **exit without starting emulation**. Nothing else is touched.
    It exits with the statuses of **\--sdcard-go-back**, and is refused in the
    same cases.

**\--sdcard-export-changes** *DIR*
:   Copy the files you added or changed on the card, as the machine sees it
    (the card plus the **\--sdcard-writes** file, when one is given), into
    *DIR* as ordinary files, write the list `CHANGES.txt` beside them, and
    **exit without starting emulation**. *DIR* must be new or empty. What it
    compares the card against, in this order: the card as jnext made it (the
    default card, when its original image is still on disk and matches), the
    image named by **\--sdcard-export-base**, the card itself when there is a
    **\--sdcard-writes** file (so only that file's changes are exported); with
    none of these it refuses. See **GETTING YOUR FILES OUT**.

**\--sdcard-export-base** *FILE*
:   With **\--sdcard-export-changes**: the image the card was made from, to
    compare against. A raw distribution image is accepted as it is.

**\--sdcard-import-changes** *DIR*
:   Put the files of a **\--sdcard-export-changes** directory *DIR* onto the
    card (or, with **\--sdcard-writes**, into that file) and **exit without
    starting emulation**. *DIR* must hold the `CHANGES.txt` the export wrote.
    A file already on the card is replaced, a read-only one too; nothing is
    ever deleted; the attributes (read-only, hidden, system) are restored. All
    or nothing, as **\--sdcard-file-add**. See **GETTING YOUR FILES OUT**.

**\--warm-start-regenerate**
:   Discard the cached warm-start recording and take a fresh one on the next
    **\--load** of a `.nex` file.

    A **\--load** of a `.nex` on the ZX Spectrum Next runs the program on a
    RECORDING of a real firmware boot, not on the machine **jnext** assembles.
    This is the behaviour, not an option: on the first load with a given SD
    image **jnext** cold-boots it (`nextboot.rom` -> `TBBLUE.FW` -> NextZXOS,
    a few seconds) and drives NextZXOS to its Command Line, where it is
    executing a typed statement, as it is when `.nexload` starts a program;
    it stores the resulting machine state under
    `~/.jnext/warm-start/`, and applies the program on top of it — which is
    what `.nexload` does on real hardware. Later loads restore that state
    instead of booting. If no recording can be made or used — no SD image, a
    card with no firmware on it, a recording that will not load back —
    **jnext** says so on standard error and falls back to the machine it has
    always assembled.

    The recording is keyed by the SD image's size and modification time (or by
    the **\--sdcard-sha256** digest), the machine type, the state format and the
    recording procedure, and it is also dropped when **jnext**
    itself writes to the image, so it invalidates itself whenever any of those
    change. The image's contents are not read: an image replaced or edited from
    outside with the same size and the same modification time (`cp -p`,
    `touch -r`) is served the old recording. Use this option to force the issue
    in that case, and after replacing `TBBLUE.FW` or NextZXOS on the card. It applies to `.nex` files on the Next only: the
    other machine types run no firmware, and snapshots and tapes are loaded by
    mechanisms a firmware-resident machine does not have.

    The recording is made on your machine from the image you mounted, and is
    never shipped with **jnext**: it contains NextZXOS and DivMMC ROM content.

**\--set** *SECTION.KEY=VALUE*
:   Set one setting of `~/.jnext/jnext.conf` for this run. Every Preferences
    setting can be given this way: the key is the file's `[section]` and key
    name joined with a dot, the value is what the file holds, and the keys are
    listed under **Settings for \--set** below. Repeat it for more
    (`--set audio.gain_db=-6 --set startup.silent=false`). The split is at the
    first `=`, so a value may contain one.

    A value that is not accepted, or a key that does not exist, is a usage
    error naming both, and **jnext** exits with status 1. A key the running
    frontend has no use for (a window setting under **\--headless**, say) is
    reported with a warning and ignored, so one command line can serve all
    three frontends.

    **Precedence.** The command line wins over the saved `jnext.conf`, which
    wins over the built-in default; a flag and **\--set** are the same thing,
    and the later of two settings of one key wins whichever way it is spelled
    (`--cpu-speed 28 --set startup.cpu_speed=0` starts at 3.5 MHz). Nothing
    given on the command line is ever written to `jnext.conf`. A change made in
    the window during the run takes over from then on (a **Preferences** OK
    or Apply, a menu, a file dialog that moves the last directory), and
    **Preferences** shows the saved values, not the command line's.

    A boolean is `true` or `false`. It also reaches what a flag cannot: `--set
    startup.silent=false` plays sound for one run although the file says muted,
    `--set startup.tape_fast_load=true` forces fast loading over a saved
    real-time setting, and `--set esp.allowed_hosts=` (empty) allows any host
    over a saved list.

**\--cpu-speed** *MHZ*
:   The CPU speed the machine starts at: `3.5`, `7`, `14` or `28`, exactly. The
    same as `--set startup.cpu_speed=0..3`, and like it a *starting* value: the
    speed is programmed through NextREG 0x07 when the machine has started, as
    **Preferences** does. NextZXOS then sets its own speed while it boots, as
    the hardware's firmware does, so on the Next machine a program that waits
    for the firmware sees the firmware's speed, not this one. On the other
    machines it holds. Works in the Qt window, the SDL-only build and
    **\--headless**.

**\--scale** *N*
:   Window scale 1 to 3 for this run, over the saved **Preferences** value. The
    same as `--set startup.window_scale`. Qt window only: elsewhere **jnext**
    warns and ignores it.

**\--crt** *EFFECT*
:   CRT effect for this run: `off`, `scanlines`, `soft`, `tv` or `pc`, the
    choices of the **Preferences** CRT effect pop-up. The same as setting
    `startup.crt_filter`, `startup.crt_shader` and `startup.crt_shader_style`
    together. `soft`, `tv` and `pc` need the GPU shader; a build without it
    refuses them (exit status 1) and offers `off` and `scanlines`. The Qt window
    and the browser build: elsewhere **jnext** warns and ignores it.

**\--speed** *PERCENT*
:   Emulator throttle: 50 = half, 100 = normal, 200 = 2x, 400 = 4x. Clamped to
    10..1000. Works in the GUI and the SDL-only build; **\--headless** runs
    uncapped, so there it has no effect, and **jnext** warns.

**\--fast-boot** *SECONDS*
:   Run the first SECONDS of a plain Next start at maximum speed with the sound
    off, 0 to 60 (default 15, 0 = off). The firmware splash and the NextZXOS
    start-up pass in a few seconds instead of about twelve. It ends at once on
    the first key the guest sees (Shift is a good one: it does nothing else),
    when the time is up (counted as 50 frames per second), when a debugger stop
    happens, or when a load or machine change resets the machine; the key is
    delivered as usual. Only the Next machine, and never when a program is
    loaded at start (**\--load**, **\--inject**, **\--rzx-play**), when the command
    line schedules work on the emulated timeline (**\--delayed-screenshot**,
    **\--delayed-automatic-exit**, **\--delayed-pok-frames**, **\--script**) or
    when a recording runs. It applies to the launch only: a later hard reset
    boots at normal speed. It never changes the emulated machine. To reach the
    firmware menu, press SPACE while the splash shows, or set it to 0.
    `--set startup.fast_boot_hide_screen=true` keeps the window black until fast
    boot ends, so the first picture is the screen it ends on; screenshots,
    recordings and the debugger still see the real screen.
    **\--headless** runs uncapped already, so there it has no effect, and
    **jnext** warns. The same as `--set startup.fast_boot_seconds`.

**\--when-slow-prefer** *WHAT*
:   What to sacrifice on a host that cannot emulate in real time: `audio`
    (default) or `video`. It changes nothing on a host with headroom.

    `audio` keeps the sound card fed by emulating two frames in one frame slot
    when it runs low. The first frame of such a pair is overwritten before it
    can be painted, so the picture advances in an uneven, lurching way while
    the sound stays clean.

    `video` never drops a frame for the sound card. The extra frame runs in the
    next slot instead, so it gets its own present, and the machine simply runs
    slower than real time. The sound queue then runs down to its floor and the
    last sample level is held to bridge the gap, which is heard as stutter and
    a dropped pitch. This is how FUSE-style emulators degrade.

    Interactive frontends only; inert under **\--headless**, which is uncapped
    and has no audio device. The GUI equivalent is *When the host is too slow*
    under **Settings > Preferences > Startup**, which applies live.

**\--joy1-source** *SRC*
:   Host source for Joy 1 (port 0x1F): `sdl` (autodetected gamepad, default),
    `keys` (host arrow keys, Space = fire) or `none` (nothing drives it).

**\--joy2-source** *SRC*
:   Host source for Joy 2 (port 0x37): `sdl` (default), `keys` or `none`. Only
    one connector may use `keys`. Interactive (SDL/Qt) frontends only; inert
    under **\--headless**.

**\--joy1-device** *ID*
:   Physical controller for Joy 1 (port 0x1F), by the id jnext logs when it
    connects it (`Joystick 1 connected: 'name' [id]`): 32 hex digits, optionally
    followed by `#N` to tell identical pads apart. Used while the connector's
    source is `sdl`; if that controller is not connected, jnext logs it and
    uses the first free one. An empty ID chooses Automatic (the first free
    controller) over a saved controller, the same as `--set input.joy1_device=`.
    Wins over the saved choice for this run.

**\--joy2-device** *ID*
:   Physical controller for Joy 2 (port 0x37); same form as **\--joy1-device**,
    an empty ID included.
    The two connectors cannot name the same controller. Interactive (SDL/Qt)
    frontends only; inert under **\--headless**.

**\--mouse-release** *METHOD*
:   How a captured mouse is released: `ctrl-alt` (default; Alt+Cmd in the Qt GUI
    on macOS), `middle-click` or `esc`. The methods are exclusive: only the one
    chosen releases. `ctrl-alt` is not consumed (Alt still types its compounds);
    the middle click and Esc are not passed to the program. Esc is the ZX BREAK
    key whenever the mouse is not captured, and `middle-click` needs a 3-button
    mouse. Wins over the saved GUI preference for this run. Interactive (SDL/Qt)
    frontends only; inert under **\--headless**. The GUI equivalent is *Release
    captured mouse with* under **Settings > Preferences > Input**, which applies
    live.

**\--touch-keyboard** *MODE*
:   Browser build only: when the on-screen Spectrum keyboard is shown. `auto`
    (default) shows it on a phone or tablet (a device whose main pointer is a
    finger), `on` always, `off` never. A page can also show or hide it while it
    runs (see **The touch keyboard**). Accepted, with a warning, and ignored by
    every other build.

**\--joy-uart-rx** *FILE*
:   Attach *FILE* as a serial byte source on the joystick-port UART RX pin — the
    "PC" end of a joy-port serial cable, as used by a dezogif / DeZog serial rig.

    The bytes do not reach the guest through either UART header. They arrive
    through the NR 0x0B I/O-mode multiplexer, which the guest controls, so the
    guest hears them ONLY while all three of these hold:

    * bits 7 and 5 of NR 0x0B are both set (`joy_iomode_uart_en`), i.e. pin 7 is
      in one of the two UART modes. Modes `00` (static) and `01` (CTC-toggled)
      carry no UART and receive nothing;
    * bit 4 selects the connector this cable is plugged into — see
      **\--joy-uart-connector**;
    * bit 0 picks the channel that receives it: 0 = UART 0, 1 = UART 1. While the
      cable owns a channel, the module on that channel's header (the ESP-01 on
      UART 0, the Pi header on UART 1) can neither be heard nor be spoken to,
      which is what the hardware does.

    Delivery is paced at the receiving channel's current baud rate, so a stream
    longer than the 512-byte RX FIFO does not overrun a guest that is draining
    it. Bytes sent while the guest is not listening are LOST rather than queued,
    exactly as on the wire — use **\--joy-uart-rx-delay-frames** to schedule
    them. If the whole stream is lost, jnext says so with a warning rather than
    finishing quietly.

    An unreadable or empty *FILE* is refused at startup.

**\--joy-uart-rx-delay-frames** *N*
:   Hold the **\--joy-uart-rx** stream for *N* complete frames before its first
    byte arrives (default 0, i.e. from the first instruction). This is how a byte
    is made to land while the program is already running — the asynchronous-break
    case — rather than being spent before the program has set NR 0x0B up.
    Requires **\--joy-uart-rx**.

**\--joy-uart-connector** *N*
:   Which joystick socket the joy-port serial cable is in: `1` or `2`
    (default `2`, the connector a real rig uses). NR 0x0B bit 4 selects the
    connector the machine reads, so a guest that selects the other one hears
    nothing at all. Applies to **\--joy-uart-rx**, **\--joy-uart-fifo** and
    **\--joy-uart-pty**, and requires one of them.

**\--joy-uart-fifo** *PATH*
:   Attach a LIVE, BIDIRECTIONAL serial cable on the joystick-port UART, as a
    pair of FIFOs named from *PATH*: *PATH*`.rx` carries host to Next and
    *PATH*`.tx` carries Next to host. Both names are from the guest's point of
    view, the same way round as **\--joy-uart-rx**. Either is created if it does
    not exist; a path that exists and is not a FIFO is refused.

    This is the option to use for a debugger on the host — DeZog driving
    dezogif_ng — because it carries a conversation. **\--joy-uart-rx** is a
    RECORDING: it reads its file whole at startup, so pointing it at a FIFO
    waits for the writer to close and then replays what it got. The two are
    mutually exclusive: one cable, one socket.

    The guest side is gated by NR 0x0B exactly as **\--joy-uart-rx** is, with
    one asymmetry that is the hardware's: RECEIVE is connector-selected (bit 4
    picks one socket's pin to listen on), TRANSMIT is not — the Next presents
    joystick pin 7 to both sockets in turn, so a cable in the socket bit 4 does
    not select can still hear the machine while being unable to be heard by it.

    NOTHING BLOCKS. With no peer attached, received bytes are simply absent and
    transmitted bytes wait; a peer that attaches later gets what was queued. A
    peer that goes away mid-session ends the conversation: what was queued is
    discarded rather than handed to whatever connects next, and the cable
    re-opens for a peer that reconnects. Receive is paced at the receiving
    channel's current baud, so a large host message cannot overrun the 512-byte
    RX FIFO.

    A rewind or an RZX playback holds the cable INERT — a re-executed frame
    neither re-transmits (the peer cannot un-receive a byte) nor consumes host
    bytes the resumed timeline still needs. Nothing about a live cable is
    saved in a snapshot. Rewinding does not rewind the program on the other
    end of the cable, so a debugger session will not survive one.

    POSIX only. Refused in the browser build, which has no named pipes.

**\--joy-uart-pty**
:   As **\--joy-uart-fifo**, but allocate a pseudo-terminal instead and log the
    slave device path at startup. This is the form a serial client opens —
    DeZog's serial remote wants one serial device, not a pair of pipes — and it
    is put in raw mode so a binary protocol passes through untranslated. POSIX
    only. Refused in the browser build, which has no terminals.

**\--nextpi**
:   Start **NextPi** — the software that runs on the Raspberry Pi of the
    Spectrum Next, on its GPIO header — under QEMU
    (`qemu-system-arm`, machine `raspi0`), connected to UART 1, the UART wired
    to that header. NextZXOS's own Pi tools (`.pisend`, the NextPi UI) then talk
    to a real NextPi. jnext does not emulate the Pi: QEMU must be installed, and
    jnext starts it, wires it in and stops it when it exits.

    jnext first checks that QEMU (and, until the first start has made the Pi's
    overlay, `qemu-img`) is installed, and stops with the install hint if not,
    before anything is offered or downloaded.

    The first run that needs it offers to download NextPi from its mirror
    (<https://zx.xalior.com/NextPi2/>) into `~/.jnext/nextpi` — about 6 GB,
    with about 22 GB free needed while it is unpacked — with a progress bar,
    exactly as the SD-card image is provisioned; later runs start at once.
    An archive `NextPi-<release>.tar.gz` already in that directory is checked
    against its `.md5` beside it (fetched only if absent) and unpacked without
    downloading; a directory that cannot be used is reported with the missing
    file, in the question and in the log. An interrupted download (Cancel, a
    dropped connection, or 30 seconds without data) is kept as
    `NextPi-<release>.tar.gz.part` and continues on the next run.
    NextPi then takes about a minute to reach its Supervisor prompt (`SUP>`).
    The release, the directory, the QEMU binary, where the Pi's sound goes and
    any extra QEMU arguments are set under **Settings > Preferences > NextPi** (the `[nextpi]` section
    of `~/.jnext/jnext.conf`) or, for one run, with **\--nextpi-dir**,
    **\--nextpi-release**, **\--nextpi-qemu**, **\--nextpi-audio** and
    **\--nextpi-qemu-args**, which work in a headless session too; their
    defaults are release `1_93D`, `~/.jnext/nextpi`, `qemu-system-arm` on
    `PATH`, and the Next's mixer.

    On Linux and macOS the Pi's sound reaches the Next's mixer over I2S, as on
    the real machine: it is mixed with the beeper, the AY chips and the DAC, so
    **\--record** and **\--wav-record** include it, and NR 0xA2 enables and
    mutes it (NextPi's tools enable it themselves). It is 10-bit, as the
    hardware's input is, and arrives about 100 ms late. Rewinds and RZX playback
    hold it silent. With NR 0xA2 bit 0 set it goes to the EAR input instead
    (port 0xFE bit 6), as a tape signal: how a Next loads a tape the Pi plays to
    it.

    The guest reaches the Pi only while NR 0xA0 bits 5:4 are `11` — UART 1 on
    GPIO 14/15, wired for a Pi — which NextPi's tools set before they talk to
    it. Until then the Pi's bytes are lost on the wire and the guest's never
    reach it, as on the real machine; the first such loss is logged once. While
    NR 0x0B gives UART 1 to the joystick connector, the Pi cannot be heard
    either. Host bytes are paced at UART 1's current baud; a rewind or an RZX
    playback holds the link inert, and nothing about it is saved in a snapshot.
    A soft or hard reset leaves the Pi running. A Pi asked for with this option
    that cannot start (no QEMU, a failed download) is an error; declining the
    download is not — jnext then starts without the Pi.
    On Linux and macOS the link is a pair of FIFOs; on Windows (QEMU for
    Windows, its folder on `PATH` or set in Preferences) it is a loopback TCP
    socket QEMU listens on.
    **\--nextpi** starts the Pi with any machine (the header and UART 1 are
    there in every personality); the saved preference starts it only when jnext
    starts the ZX Next. Changing the machine while jnext runs neither starts
    nor stops it.

**\--no-nextpi**
:   Do not start NextPi this run, overriding a saved preference that
    enables it.

**\--nextpi-dir** *DIR*
:   The NextPi directory for this run (default `~/.jnext/nextpi`), over the
    value saved under **Settings > Preferences > NextPi**. An empty value means
    the default. It is used when NextPi starts, so it goes with **\--nextpi**
    (or the saved preference, when it starts NextPi); given when NextPi is not enabled
    for the run, this and the four options below are ignored, with a warning
    naming each.

**\--nextpi-release** *NAME*
:   The NextPi release for this run: a release name on the mirror, or `latest`
    for the newest one listed there (default `1_93D`). Over the saved value; an
    empty value means the default.

**\--nextpi-qemu** *PROGRAM*
:   The QEMU program for this run (default `qemu-system-arm` on `PATH`; with a
    path, `qemu-img` is expected beside it). Over the saved value; an empty
    value means the default.

**\--nextpi-audio** *SPEC*
:   Where the Pi's sound goes for this run: empty for the Next's mixer (the
    default), `host`, a QEMU `-audiodev` driver such as `pa` or `none`, or
    `wav:`*FILE*. Over the saved value.

**\--nextpi-qemu-args** *"ARGS"*
:   Extra QEMU arguments for this run, added after jnext's own. The text is
    split into arguments on spaces; `'...'` and `"..."` keep spaces in one
    argument; a backslash is an ordinary character; nothing is expanded and no
    shell is involved. Over the saved `qemu_args`. An unterminated quote stops
    NextPi from starting, with a message (an error when **\--nextpi** asked
    for it). For example, a second serial port for the Pi, on `ttyS0`:
    `--nextpi-qemu-args "-serial telnet:127.0.0.1:2357,server,nowait"`.

**\--tape-realtime**
:   Real-time tape loading, at the speed of an actual tape, instead of fast
    load. It sets the one fast/real-time setting (the GUI's **Tape > Fast
    Load** toggle) for this run only, overriding the saved preference without
    changing it; **Tape > Open Tape File**, **File > Load NEX File**, **\--load** of a `.tap` or
    `.tzx`, and the tape restored by a `.jns` all follow that setting. The
    SDL-only build, which has no menu and no saved preference, is controlled by
    this option alone.

**\--tape-auto-play**, **\--no-tape-auto-play**
:   Turn automatic tape play/stop on (the default) or off for this run,
    overriding the saved GUI preference. While on, the tape deck stops in the
    silent gap between two blocks when no program is reading the tape, and
    resumes when one does, so tapes whose BASIC loader is slow between blocks
    load in real time. It does nothing in fast load mode. A `.wav` has no blocks and is never paused. Off, the deck runs
    straight through, as a real one does (a TZX "stop the tape if in 48K mode"
    block now stops only on a 48K machine, as the TZX specification says,
    whatever this setting is).

**\--tape-save** *FILE*
:   Save to tape, appending to *FILE*; its extension picks the format. A
    `.tzx` or `.wav` records the machine's tape output (the MIC jack), so a
    program's own saver is captured as well as BASIC `SAVE`: a `.tzx` holds it
    as standard (0x10), turbo (0x11) or raw pulse (0x13) blocks, a `.wav` as
    8-bit mono 44100 Hz audio. Any other name (`.tap`) captures only blocks
    SAVEd through the 48K ROM routine. ROM saves are instant in all three: a
    trap at 0x04C2 (with the 48K ROM paged at slot 0) takes the block without
    running the routine. An existing file must be of the same format. Also
    **Tape > Start Saving...** in the window. Cannot be combined with RZX
    recording or playback: the trap skips the ROM routine, which a recording
    cannot replay.

**\--esxdos-stub**
:   Answer a few `RST $08` esxDOS calls for any program, for the whole session:
    the version query, one in-memory file and `run sibling.nex` chaining. Every
    other call goes to the code at `$0008`. For programs run without NextZXOS;
    a directly loaded NEX gets a fuller set automatically (see **LOADING
    PROGRAMS**). With NextZXOS booted it answers in front of NextZXOS's own
    esxDOS for those calls, and NextZXOS's file commands stop working.

    With a NEX to load (**\--load**, or a bare file name) and no
    **\--esxdos-stub-root**, the directory the NEX is in is served to it as if
    it had been given as *DIR* (below): read-only unless
    **\--esxdos-stub-writable** is also given, and in place of the in-memory
    file. To keep the in-memory file, load the NEX without **\--esxdos-stub**.
    A NEX loaded from a `.zip` that holds its data files is served its
    directory in the archive with no flag at all (see the `.zip` entry of
    **LOADING PROGRAMS**).

**\--esxdos-stub-root** *DIR*
:   Serve the host directory *DIR* to the guest through the esxDOS file and
    directory calls, instead of the single in-memory file. Implies
    **\--esxdos-stub**. Read-only unless **\--esxdos-stub-writable** is also
    given. *DIR* replaces the default of **\--esxdos-stub**, the directory of
    the loaded NEX, and the directory of a NEX loaded from a `.zip` that holds
    its data files.

    **Which programs see *DIR*, and which do not.** A NEX loaded with
    **\--load** sees it, and so do dot commands: those reach the filesystem
    through `RST $08`, which is what jnext intercepts. **NextZXOS does not see
    it** — not its Browser, not its file selector, not BASIC's `LOAD`, not its
    loader. NextZXOS carries its own SD-card driver and its own FAT code in
    ROM and reaches the card directly, without ever executing an `RST $08`, so
    there is no point at which jnext could answer for it. That boundary is
    permanent and deliberate; it is not a limitation that a later version
    lifts. **To put a file where NextZXOS can see it, copy it into the SD-card
    image.**

    Inside *DIR*, paths behave as FAT paths: `/` and `\` both separate
    components, a leading separator is the root of *DIR* rather than the host's
    root, lookup ignores case, a component can also be given by its 8.3 short
    name, and the drive letters `*:`, `$:` and `c:` all
    mean *DIR*. A path that would leave *DIR*, and any symbolic link, is
    refused; symbolic links are not listed either. Because `\` is a separator,
    a host file whose name contains one cannot be reached — FAT cannot name
    such a file either. Where two host files differ only in case and the guest's spelling
    matches neither exactly, the first in byte order is taken.

    Directory entries carry the host file's modification time. `M_GETDATE`
    answers from the emulated clock instead, so it follows **\--rtc**.

    **Do not combine it with a booted NextZXOS.** It implies
    **\--esxdos-stub** and inherits its limitation: the file calls are then
    answered in front of NextZXOS's own esxDOS, and NextZXOS's file commands
    stop working — `.ls` reports *No such file or dir*. This option is for
    programs jnext loads itself.

**\--esxdos-stub-writable**
:   Allow the guest to create, truncate, write, delete and rename files, make
    and remove directories, and set or clear the read-only bit of files under
    **\--esxdos-stub-root**, or in the directory of the loaded NEX when no
    root is given (for a NEX from a `.zip`, in the extracted copy, never in the
    archive): the program can delete any file there. Off by default, and
    deliberately a separate flag: a guest write changes a real file on the host
    immediately, and rewinding the emulator cannot undo it. Reads are fully
    rewindable; writes, deletions and renames are not.

**\--rtc** *"YYYY-MM-DD HH:MM:SS"*
:   Pin the RTC to a fixed date and time (a frozen clock) instead of following
    the host clock, which makes boot screenshots deterministic. The ISO
    `YYYY-MM-DDTHH:MM:SS` form is also accepted.

**\--silent**
:   Disable all sound output (beeper, the three AY/YM chips, DAC/Covox/
    Specdrum). No audio device is opened and the emulator skips PSG and mixer
    sample synthesis entirely, which can measurably speed up CPU-bound runs.
    Tape loading (EAR input) is unaffected.

**\--inject** *FILE*
:   Load a raw binary into RAM.

**\--inject-org** *ADDR*
:   Load address for **\--inject** (hex, `0x` prefix optional, default `8000`). Requires
    **\--inject**.

**\--inject-pc** *ADDR*
:   Entry point for **\--inject** (hex, `0x` prefix optional, default: same as **\--inject-org**).
    Requires **\--inject**.

**\--inject-delay** *N*
:   Wait *N* frames before injecting (default 0). Use around 100 if the binary
    calls ROM routines that need the system variables set up first. Requires
    **\--inject**.

## Networking (ESP-01 WiFi)

**\--esp**
:   Enable the emulated ESP-01 WiFi module on UART 0. **Off by default** - see
    **NETWORKING** below for what turning it on gives the running program.

**\--no-esp**
:   Force the ESP off for this run, overriding a saved GUI preference that
    enables it. Given both, whichever comes last on the command line wins.

**\--esp-allow** *HOST*
:   Only let the running program connect to *HOST*. Repeatable: give the option
    once per host. *HOST* is a single name - a comma is **rejected** rather
    than taken as a separator or as part of the name, because a name that
    silently matched nothing would look like a restriction that is in force
    when it is not. Matching is exact and case-insensitive, with no wildcards,
    and an IP address must be listed as itself. With no **\--esp-allow** at all
    the program may name any host (subject to the always-refused addresses in
    **NETWORKING**). Requires **\--esp**.

**\--esp-listen-address** *ADDR*
:   Bind the running program's `AT+CIPSERVER` to *ADDR*, default `127.0.0.1`.
    *ADDR* is a numeric IP address, never a name - an address resolved through
    DNS could change under you. The default means a program that opens a server
    is reachable only from this machine; widening it (`0.0.0.0`) exposes that
    program to your network, which is why it has to be asked for. Nothing
    listens until the program itself sends `AT+CIPSERVER`. Requires **\--esp**.

**\--esp-ip-address** *ADDR*
:   Report *ADDR* as the module's own station address instead of the default
    `192.168.1.50`, so a program that recognises its own address can be run
    against the one it expects. It is what `AT+CIFSR` and `AT+CIPSTA?` answer,
    and nothing more: **nothing binds it, nothing routes through it**, and the
    emulated Next cannot be reached at it - that is
    **\--esp-listen-address**, which is a real socket. *ADDR* is a numeric IP
    address, never a name, and `0.0.0.0` is refused because that is what the
    module reports while it has no network. Requires **\--esp**.

**\--esp-ip-address-after** *ADDR*
:   Report *ADDR* instead, once the outage ends. Defaults to the pre-outage
    address, which is what a short outage really does; give this when the
    address should come back **different** - a DHCP lease that expired during a
    long outage, or a router that reassigned. It is what makes a program's
    stale-address handling testable at all: across an outage that returns the
    *same* address, a program that caches its address at start-up and one that
    re-reads it are indistinguishable. Requires
    **\--esp-delayed-associate-frames**, and takes the same values as
    **\--esp-ip-address**.

**\--esp-delayed-disassociate-frames** *N*
:   Take the emulated module off its WiFi network after *N* emulated frames, as
    if the access point had gone away. From then on `AT+CIFSR` reports
    `+CIFSR:STAIP,"0.0.0.0"` instead of the module's address, which is what a
    real ESP-01 with no association reports and what lets a program detect that
    its address has gone. Nothing else changes: connections already open keep
    running, new ones still open, and no unsolicited `WIFI DISCONNECT` is sent -
    what a real module does to traffic during an outage has not been measured,
    and jnext does not invent it. Requires **\--esp**.

**\--esp-delayed-associate-frames** *N*
:   Put it back on the network after *N* emulated frames, so `AT+CIFSR` reports
    the module's address again - the same address as before, since a short
    outage does not normally change it. Requires
    **\--esp-delayed-disassociate-frames**, and *N* must be greater than its
    value: an outage that ends before it starts is refused rather than run.

## Recording and playback

**\--record** *FILE*
:   Record video and audio to an MP4. Requires **ffmpeg**(1) on the PATH.
    If the recording cannot be produced (encoder failure, no usable
    output), **jnext** reports the error and exits non-zero. If the
    recording cannot even be started (no **ffmpeg** on the PATH), **jnext**
    exits non-zero immediately, without running the emulation.

**\--wav-record** *FILE*
:   Record the mixed stereo output to a 44.1 kHz, 16-bit PCM WAV. Works
    headless and requires neither an audio device nor ffmpeg.

**\--wav-record-stems** *DIR*
:   Record the mix as seven 44.1 kHz, 16-bit stereo WAV files in *DIR*, one per
    source: `ay0.wav`, `ay1.wav`, `ay2.wav` (each AY chip after its mode and
    pan, silent while the chip is not in the mix), `dac.wav` (left is A+B,
    right is C+D), `beeper.wav` (EAR and MIC; a playing tape and Pi audio sent
    to EAR are inside it, as the Next mixes them as one term), `pi.wav` (the Pi
    I2S audio) and `mix.wav`. The stems are taken before the host gains and
    the debugger mutes, so `mix.wav` equals **\--wav-record** at 0 dB, and the
    six source files add up to `mix.wav` exactly while every source is steady
    within an output sample and otherwise within 3 counts per sample (the
    rounding of the seven files). *DIR* is created if missing and files of
    those names in it are overwritten. If *DIR* or a file cannot be created,
    **jnext** exits non-zero without running the emulation; a write error
    that shows later exits non-zero too. Cannot be combined with **\--silent**.

**\--dac-trace** *FILE*
:   Record `segment,tstate,channel,value` rows for physical DAC writes. A cold
    boot starts a new segment.

**\--audio-log** *FILE*
:   Record the decoded audio writes to a CSV, one row per write, with the frame,
    line, hc and T-state it happened at. Rows cover AY register writes, the
    TurboSound chip-select and pan commands (and every discarded 0xFFFD write),
    DAC channel writes (one row per channel written), the NextREG 0x2C-0x2E DAC
    mirror writes and beeper EAR/MIC changes, and every write the hardware
    discarded with its reason. Plain AY register selects, border-only writes to
    port 0xFE and the frames a rewind replays are not recorded. The first line
    is the header `frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port`.
    `frame`, `line`, `hc`, `tstate`, `cycle`, `chip`, `reg` and `flags` are
    decimal; `pc` and `port` are `0x%04X`; `value` and `prev` are `0x%02X`.
    `source` is `cpu`, `dma`, `copper` or `debugger` (writes made by the
    debugger or a script carry `debugger`), `target` is `ay`, `ay_select`,
    `dac`, `dac_mirror` or `beeper`, and `chip` is empty for a discarded DAC
    write that reached no channel. A reset, a state load or a rewind adds a row
    with `marker` in `source` and `reset`, `load` or `rewind` in `target`. After a
    `rewind` row the following rows may carry earlier cycles: the file is
    append-only and the rows before the marker are kept. It works in every
    frontend, headless, with **\--silent** and in a browser page.
    An unwritable *FILE* is refused before the machine starts. A write error, or
    writes lost because more than 65536 arrived between two services of the loop,
    makes the run exit non-zero.

**\--psg-record** *FILE*
:   Record each AY chip's register stream as `.psg`, in the layout Fuse writes
    for its AY log: a 16-byte header, then for every frame in which a register
    from R0 to R13 was written the register/value pairs in register order, with
    frame separators between the frames. *FILE* takes AY#0; AY#1 and AY#2 go to
    *FILE* with `-ay1` or `-ay2` before the extension (`tune.psg` gives
    `tune-ay1.psg`), created at that chip's first write and aligned to the start
    of the recording. When a register is written more than once in a frame the
    last value is kept. R14 and R15 are not recorded. A reset or a state load
    keeps the elapsed time, and a rewind keeps what was written and carries on
    after it. It
    works in every frontend, headless, with **\--silent** and in a browser page.
    An unwritable *FILE* is refused before the machine starts. A write error
    makes the run exit non-zero. The same recording starts from the Chips tab of
    the debugger's Audio panel.

**\--audio-gain-db** *DB*
:   Set master host audio gain from -24 dB to +24 dB (default 0 dB). The gain is
    applied after the emulated hardware mix; overflow saturates at the 16-bit
    PCM limits.

**\--audio-gain-beeper-db** *DB*
:   Set host gain for beeper EAR/MIC/tape-EAR audio from -24 dB to +24 dB.

**\--audio-gain-ay0-db** *DB*
:   Set host gain for TurboSound AY chip 0 from -24 dB to +24 dB.

**\--audio-gain-ay1-db** *DB*
:   Set host gain for TurboSound AY chip 1 from -24 dB to +24 dB.

**\--audio-gain-ay2-db** *DB*
:   Set host gain for TurboSound AY chip 2 from -24 dB to +24 dB.

**\--audio-gain-dac-db** *DB*
:   Set host gain for Specdrum, Soundrive and Covox DAC audio from -24 dB to
    +24 dB.

**\--rzx-play** *FILE*
:   Play back an RZX recording from the start of the run. Works the same in the
    GUI, the SDL-only build, under **\--headless** and in a browser page. A recording that fails
    to load is logged, and **jnext** then exits non-zero. The machine starts
    exactly as it does for **\--load** *FILE*, so the two spellings replay
    identically, and so do **File > Load NEX File...** and **File > Play RZX
    Recording** in the GUI. The recording brings its own snapshot of the machine, so it cannot be combined with
    **\--rzx-record**, with **\--load** or **\--inject** of another program,
    or with a second RZX file. It plays on the machine it was recorded on, whatever
    machine is configured: a recording **jnext** makes names its machine, and
    one from another emulator is judged by its embedded snapshot (an SNA's
    size, an SZX's machine ID, a `.z80`'s hardware mode). A recording that
    names none — one **jnext** 1.0.0 or earlier made with an SNA snapshot, or one whose
    snapshot is of a machine **jnext** does not emulate — plays on the
    configured machine. An explicit **\--machine** given with the recording
    wins, and **jnext** warns when it disagrees with the recording, which then
    goes out of step; a recording played from the GUI's menus always gets its
    own machine. The recording's machine stays selected afterwards, in the
    GUI's **Machine > Machine Type** too, as if chosen there.

**\--rzx-record** *FILE*
:   Record input to an RZX file from the start of the run — or, with **\--load**
    or **\--inject**, from the moment that program is in the machine, so the
    snapshot the file carries is the program, and playback replays it. The
    file is written when **jnext** exits (or, in the GUI, at **File > Stop RZX
    Recording**). Works the same in the GUI, the SDL-only build and under
    **\--headless**. A browser page refuses it and records from JavaScript (see
    THE BROWSER BUILD). While recording, a tape loads in real time: a fast load
    skips the ROM loader, which a recording cannot replay. The snapshot is an
    SZX on the 128K, Pentagon and +3 and a 48K SNA on the 48K. The file names the machine
    it was recorded on, which **\--rzx-play** then uses.

    **Recording is not available on a ZX Spectrum Next, which is jnext's default
    machine, so this needs an explicit \--machine 48k, 128k, pentagon or plus3.** An RZX
    carries a snapshot of the machine it starts from plus a log of the values the
    program read from ports — and the log records the values without the ports
    they came from. So no snapshot an RZX can carry both describes a Next (none
    of SNA, SZX or Z80 holds its extra RAM, NextREGs, Layer 2, tilemap, sprites
    or Copper) and keeps the value log aligned with what the restored machine
    goes on to read. jnext refuses rather than write a file that looks fine and
    replays wrong. **\--rzx-play** still plays a Next recording made by an
    earlier jnext, with a warning that it may not replay faithfully.
    A *FILE* that cannot be written is refused before the machine starts, and
    a recording that cannot be saved when it is written is logged; either way
    **jnext** exits non-zero. A reset ends the recording: a hard reset (the
    Reset button, F1, or the program's own), loading another program from the
    GUI, changing the machine type, or F4 writes the file there, and nothing
    after it is recorded — a recording cannot replay a reset.

**\--rewind-buffer-size** *N*
:   Frame-snapshot ring buffer for backwards execution. Opt-in; default 0 =
    off.

**\--trace**
:   Enable the per-instruction trace log (a 10K-entry ring). Also enabled
    implicitly by **\--rewind-buffer-size** *N* with *N* > 0, and toggleable in
    the debugger.

**\--coverage**
:   Record memory coverage from the start: for every byte of every memory
    (each RAM page, ROM image, DivMMC and Multiface window, Layer 2 mapping, ...)
    whether the program executed it (first opcode byte of an instruction), read it
    or wrote it. Toggleable in the debugger (Debug > Coverage), where the
    Disassembly, Memory and Source views show it. With the SDL frontend or
    **\--headless** it records but there is no viewer; it logs one summary line at
    exit. Costs a little speed while on (3-8% in release builds); no measurable cost while off.

## Headless and automation

**\--headless**
:   Run with no display and no audio device, at maximum speed.

**\--benchmark** *N*
:   Headless only. Run exactly *N* frames uncapped, print one machine-parseable
    `BENCH` line (wall seconds, fps, T-states/s, T-states/frame, CPU speed,
    host core, build type) plus a human summary to stdout, then exit. Used by
    `make bench` (`test/bench/bench.sh`).

**\--benchmark-label** *NAME*
:   Workload label printed verbatim in the `BENCH` line (default: the loaded
    file's basename, or `boot-<machine>`). No whitespace, since the `BENCH`
    line is space-delimited. Requires **\--benchmark**.

**\--delayed-screenshot** *FILE*
:   Save a screenshot after a delay. The format is taken from *FILE*'s
    extension: `.scr` writes the raw ULA screen memory, anything else writes a
    PNG of the composited picture. See **SCREENSHOT FORMATS** below. The
    capture is taken from the next frame the machine renders, so if it comes
    due while the machine is paused - by the debugger, or by a
    **\--magic-breakpoint** - it waits until the machine runs again; it is
    never written from the frame that was on screen before the pause. One
    still waiting when the exit comes is an error (see
    **\--delayed-automatic-exit**).

**\--delayed-screenshot-time** *N*
:   Delay in emulated seconds (default 10; see *Emulated seconds* under
    HEADLESS MODE). Requires **\--delayed-screenshot**.

**\--delayed-screenshot-frames** *N*
:   Delay in frames. Overrides **\--delayed-screenshot-time**. Requires
    **\--delayed-screenshot**.

**\--delayed-screenshot-layers** *LIST*
:   Layers to compose into the screenshot: a comma-separated list of `ula`,
    `layer2`, `sprites`, `tiles`, `all` (default `all`). Requires
    **\--delayed-screenshot**, and is rejected when that screenshot is a
    `.scr`, which has no layers to choose from.

**\--delayed-automatic-exit** *N*
:   Exit the emulator after *N* emulated seconds (see *Emulated seconds*
    under HEADLESS MODE). The exit always fires, but work the
    command line deferred to a later frame and that has not happened by then
    is an error, and **jnext** exits non-zero: a **\--load** still waiting out
    its boot delay (100 frames for `.tzx` and `.wav`), an **\--inject** with
    **\--inject-delay**, an **\--rzx-record** waiting for that load, a
    **\--delayed-keypress**, **\--delayed-nmi** or **\--delayed-screenshot**
    still to come, a **\--joy-uart-rx** stream still held by
    **\--joy-uart-rx-delay-frames**, and an edge of the
    **\--esp-delayed-disassociate-frames** /
    **\--esp-delayed-associate-frames** outage not yet reached. Give the exit
    headroom over the last of them.

**\--delayed-automatic-exit-frames** *N*
:   Exit after *N* frames. Overrides **\--delayed-automatic-exit**, and keeps
    its rule on work still to come.

**\--delayed-snapshot** *FILE*
:   Headless only: requires **\--headless**. Save a snapshot after a delay in
    frames. The format is chosen by the extension of *FILE*: `.jns`, `.szx`,
    `.nex`, anything else `.sna`. A snapshot is only ever taken at a frame
    boundary,
    so if the debugger has paused the machine part-way through a frame — a
    magic breakpoint, say — the frame in flight is completed first and the
    snapshot is written from the boundary that follows it. That pause never
    refuses the capture; the saved machine is then up to one frame past the
    point the debugger stopped at. A format that cannot represent the current
    machine *is* refused — it writes nothing, says why, and exits non-zero
    (see **JNEXT SNAPSHOTS**).

**\--delayed-snapshot-frames** *N*
:   Delay in frames for **\--delayed-snapshot** (default 0). Requires
    **\--delayed-snapshot**.

**\--snapshot-compression** *STATE*
:   Whether `.jns` snapshots are written deflated. `on` (the default) deflates
    every member; `off` stores them instead, so the file is readable with
    `unzip -p` and a hex editor at roughly five times the size. A debugging
    aid; it changes nothing about what the file contains, and jnext reads both
    forms. Any other *STATE* is an error.

**\--quick-snapshot-keep** *N*|`unlimited`
:   How many quick snapshots **File > Quick Save Snapshot** keeps in
    `~/.jnext/snapshots/`: after each save the oldest beyond *N* are deleted
    (default 20; *N* is 1 to 1000000). Only files named exactly like Quick
    Save's own (`jnext-YYYYMMDD-HHMMSS.jns`, optional `-NN`) are ever deleted;
    anything else in the folder is left alone. `unlimited` keeps everything.
    Overrides the `[snapshot] quick_keep` setting for the run; the SDL-only
    build, which reads no configuration file, takes only this option.

**\--snapshot-mode** *MODE*
:   How much a `.jns` load refuses. One setting with three positions, because
    they are one axis and asking for two of them at once is not a thing to
    want.

    `normal` (the default) warns about a mismatched state model revision or
    mismatched ROM digests and refuses a different SD card.

    `strict` turns those provenance warnings into refusals: a snapshot written
    by a jnext whose state model this build does not know, or one whose
    recorded ROM digests differ from the ROMs now loaded. A missing tape file
    is **not** covered — that always warns and never refuses, because a
    machine restores perfectly well without the tape it was loading from. The
    same goes for a tape that has changed or whose position cannot be restored.

    `force` restores even though the mounted SD card is not the one the
    snapshot was taken on, printing a warning that names both cards. This is
    the setting that lets you create exactly the silently-wrong case the
    identity check exists to prevent, which is why it is spelled out in full.

    Any other *MODE* is an error.

**\--delayed-keypress** *SECS* *KEY*
:   Press *KEY* after *SECS* emulated seconds (see *Emulated seconds* under
    HEADLESS MODE). Headless only (requires **\--headless**), repeatable.

**\--delayed-keypress-frames** *N* *KEY*
:   Press *KEY* after *N* emulated frames. This is the frames-unit spelling of
    **\--delayed-keypress**, not an override of it: both forms queue into the
    same list, so giving both schedules two keypresses. Requires
    **\--headless**.

**\--delayed-nmi** *SECS* *BUTTON*
:   Press an NMI *BUTTON* after *SECS* emulated seconds (see *Emulated
    seconds* under HEADLESS MODE). Headless only (requires **\--headless**),
    repeatable.
    *BUTTON* is case-insensitive and names which button to press, spelled as
    the label on a real Next's case. Of its three buttons, two raise an NMI:
    `nmi` (aliases `mf`, `m1`) is the **NMI** button, wired to the Multiface;
    `drive` (alias `divmmc`) is the **DRIVE** button, wired to the DivMMC.
    **RESET** is not an NMI button and is not accepted here.
    The press goes through the same path as the
    host F9 / F10 hotkeys, so it is subject to the same enable gates —
    NextREG 0x06 bit 3 for the Multiface, bit 4 plus NextREG 0x83 bit 0 for
    the DivMMC — and a press with its gate closed does nothing, exactly as on
    hardware. One press generates one NMI, not a repeating one.

**\--delayed-nmi-frames** *N* *BUTTON*
:   Press *BUTTON* after *N* emulated frames. This is the frames-unit spelling
    of **\--delayed-nmi**, not an override of it: both forms queue into the
    same list, so giving both schedules two presses. Requires **\--headless**.

**\--delayed-sdcard-insert-frames** *N* *FILE*
:   Insert SD-card image *FILE* after *N* emulated frames, exactly as
    **File > Insert SD Card Image** does (see **SD CARD AND ROMS**). The
    card keeps **\--sdcard-readonly**. The run exits non-zero if the insert
    is refused - while an RZX recording is made or played, while a directly
    loaded NEX keeps its own file open, when *FILE* is not a readable file, or
    when another insert is due in the same frame - or if the automatic exit
    comes first. An empty *FILE* is rejected. Repeatable. Requires
    **\--headless**.

**\--delayed-sdcard2-insert-frames** *N* *FILE*
:   The same for the **second SD card** slot: insert *FILE* there after *N*
    emulated frames, as **File > Insert Second SD Card Image** does (see
    **A SECOND SD CARD**). The card keeps **\--sdcard2-readonly**. The run exits
    non-zero under the same refusals as **\--delayed-sdcard-insert-frames**, and
    when *FILE* is the image the first slot has mounted. NextZXOS finds its drives
    at start-up, so to see the card use a hard reset (the environment variable
    `JNEXT_DELAYED_RESET_FRAMES`, or NextZXOS's own). Repeatable. Requires
    **\--headless**.

**\--delayed-pok-frames** *N* *FILE*
:   Apply trainers from the POK file *FILE* after *N* emulated frames, once the
    program has loaded. This is how a `.pok` file is used in the runs that have
    no **Machine > POK Trainers...** window: **\--headless** and the SDL-only
    build's window (see **POK TRAINERS** under LOADING PROGRAMS). The trainers
    are the ones **\--pok-trainers** names, which this requires. *N* counts
    frames from the start of the run (50 a second on a 50 Hz machine), and a tape
    takes many; the trainers are written through the debugger's memory path,
    exactly as the window's **Apply** writes them. Given once. The file and the
    list are checked before the machine starts, and a run that fails the check
    exits 1 without starting: *FILE* unreadable, larger than 1 MiB or without
    a trainer, a listed trainer the file does not have or whose lines are
    malformed, a value missing or given where none is open. Later, the run
    exits non-zero if a trainer cannot be applied - an address that is ROM in
    the current memory map, a bank the machine does not have, an RZX recording
    or playback - and then the trainers before it stay applied; and if the
    automatic exit comes first. The Qt window refuses the option; with
    **\--headless** every build accepts it.

**\--pok-trainers** *LIST*
:   The trainers **\--delayed-pok-frames** applies: their numbers in the POK
    file (1 is the first `N` line), comma-separated, no blanks, each *T* or
    *T*`=`*VALUE*. A trainer that leaves a value open (a `256` in the file,
    "Lives" without saying how many) needs *T*`=`*VALUE*, *VALUE* 0 to 255,
    which goes everywhere the trainer leaves one open; any other trainer takes
    none. `1,3=9,7` applies trainers 1, 3 (with 9) and 7, in that order. A
    trainer whose bytes memory already holds is left as it is. Requires
    **\--delayed-pok-frames**.

**\--compositor-trace** *FILE*
:   Dump a per-pixel compositor trace (CSV) for one frame.

**\--compositor-trace-frame** *N*
:   Target frame for **\--compositor-trace** (default 250). Requires
    **\--compositor-trace**.

*KEY* is case-insensitive and is one of: a single character (`a`-`z`, `0`-`9`,
`.`, `,`, `;`, `:`), one of the symbolic names `ENTER`, `RETURN`, `SPACE`,
`UP`, `DOWN`, `LEFT`, `RIGHT`, or a compound `sym+`*char* / `caps+`*char*
(for example `sym+m` is `.`).

## Debugging

**\--magic-breakpoint**
:   Enable the magic-breakpoint opcodes (`ED FF`, `FD 00` and `DD 01`).

**\--persistent-breakpoints**
:   Keep breakpoints and watchpoints armed while the debugger window is
    **closed**. Without this option — the default — closing the window
    disarms them, and nothing stops. With it, a hit pauses the machine and
    reopens the debugger window at the breakpoint. With the window already
    open, behaviour is unchanged. The breakpoint check then runs on every
    instruction for the whole run, which is what "persistent" costs; it is
    accepted but has no effect in **\--headless**, in the SDL-only build and
    in builds without the debugger, since only the debugger can set a
    breakpoint.

**\--debugger**
:   Start the program under the debugger: the machine starts **paused**, where
    the program starts, instead of running. The stop is, for a **NEX** file,
    its entry point (a NEX whose entry is 0 is a load-only file: the CPU is
    parked, and that is where it stops); for a snapshot (**SNA**, **Z80**,
    **SZX**, **JNS**) or an **RZX** recording, the PC it restores; for
    **\--inject**, the injected program's PC; with no file, power-on, before
    the first instruction (PC 0). It stops right after the program is loaded,
    before it has run an instruction. A tape (**TAP**, **TZX**, **WAV**) stops at the
    first instruction fetched from memory the tape loaded, after its last block has
    loaded (the program it started; in a **TAP** or **TZX**, in fast or real-time
    mode alike); a tape that cannot carry code (BASIC and a SCREEN$ only) stops at
    the first instruction after its last block. A BASIC program that runs code from
    a REM stops at the end of its last block, not in the REM, and a tape that loads
    code which never runs is never stopped (use **\--break-at**). A **WAV** has no block
    structure to tell a code tape from a BASIC one, so it always takes the code rule: a
    BASIC-only WAV is never stopped, and a recording with noise in its trailing silence
    can stop late (the end of the tape is taken as the last signal edge). A trailing
    pure-data or direct-recording TZX block counts as data in real time only (fast load
    never plays it). The stop is logged
    (`debugger: stopped at $8000 (the NEX entry)`).

    In the Qt GUI the debugger window opens, paused. In **\--headless** and the
    SDL-only build there is no window, so the run waits for a remote debugger
    and serves DZRP on port 11000 (DeZog's `cspect` remote) unless a protocol
    port is given: with **\--zrcp-port** or **\--gdb-port** only those are
    served, and an explicit **\--dzrp-port** is used as it is. The same
    applies to a Qt build made without the debugger. A paused headless run
    waits for the debugger without using the CPU, and a debugger that attaches
    finds the machine already paused. Stops in these two frontends pause and
    wait for the remote instead of ending the run with exit code 3: under
    **\--script**, a `stop` or a failed `assert` pauses, and a script's own
    `exit` still exits. Not available in the browser.

**\--break-at** *ADDR|SYMBOL*
:   Implies **\--debugger**, and replaces its default stop: run to *ADDR*, stop
    there once, and stay paused. It takes the forms the debugger's Breakpoints
    panel takes for an Execute breakpoint: a hexadecimal address (`8000`,
    `$8000` or `0x8000`; there is no decimal form, so `32768` is not an
    address), a symbol of the program's own map or of **\--map**, `@symbol`
    (always the symbol, even when its name reads as hex), or a source line
    `file:line` of a loaded source map. A symbol or line keeps the memory it
    was assembled for: it stops only while that memory supplies the code. The
    stop is armed when the program is loaded, before it has run, so it also
    catches code a tape runs while it loads. It fires once and never appears in
    the Breakpoints panel. A value that is not an address, a symbol or a
    source line is refused when the program is loaded, before it has run an
    instruction, with exit code 1. Another client's pause before the stop does not cancel
    it: DeZog attaching (which pauses the machine) while a tape loads, then continuing,
    still ends at the stop. Not available in the browser.

**\--dzrp-port** *PORT*
:   Serve the DeZog Remote Protocol (DZRP) on TCP *PORT*, so DeZog - or any
    other DZRP client - can debug the running machine; see **REMOTE DEBUGGING
    (DEZOG)**. Off unless given. *PORT* `0` binds a free port the system
    chooses, and the log says which (`dzrp: listening on 127.0.0.1:40123`).
    One client at a time: a second connection is closed at once. A port that
    cannot be bound (already in use, say) is a startup error. Works in every
    frontend, **\--headless** included.

**\--gdb-port** *PORT*
:   Serve the GDB remote serial protocol on TCP *PORT*, so `z88dk-gdb` can
    debug the running machine; see **REMOTE DEBUGGING (Z88DK-GDB)**. Off unless
    given. *PORT* `0` binds a free port the system chooses, and the log says
    which (`gdb: listening on 127.0.0.1:40125`). One client at a time. A port
    that cannot be bound is a startup error. May be given together with
    **\--dzrp-port**: each server has its own port and its own client. Works
    in every frontend, **\--headless** included.

**\--zrcp-port** *PORT*
:   Serve the ZEsarUX remote command protocol (ZRCP) on TCP *PORT*, so DeZog
    (its `zrcp` remote) - or `telnet`, or any other ZRCP client - can debug the
    running machine; see **REMOTE DEBUGGING (ZRCP)**. Off unless given; `10000`
    is the port those clients assume. *PORT* `0` binds a free port the system
    chooses, and the log says which (`zrcp: listening on 127.0.0.1:40123`).
    One client at a time: a second connection is told so and closed. A port
    that cannot be bound is a startup error. Works in every frontend,
    **\--headless** included, and alongside **\--dzrp-port** and
    **\--gdb-port**.

**\--debug-channel** *PROTOCOL*
:   Browser page only: serve the debugger protocol *PROTOCOL* (`dzrp`, `zrcp`
    or `gdb`) to the page's debug channel, the `debug` option of `jnext.mount`.
    May be given once for each protocol. Every other build refuses it with
    exit status 1, because a server that was asked for and cannot be reached is
    a startup error: use **\--dzrp-port**, **\--zrcp-port** or **\--gdb-port**
    there.

**\--debug-listen-address** *ADDR*
:   Bind address for the debugger protocol servers (**\--dzrp-port**,
    **\--gdb-port**, **\--zrcp-port**), default
    `127.0.0.1`. *ADDR* is a numeric IP address, never a name - an address
    resolved through DNS could change under you - and anything else is refused
    at startup. The default means only this machine can reach the debugger; a
    non-loopback address (`0.0.0.0`) exposes it to your network, and none of
    the debugger protocols has any authentication. Refused unless a server port
    is given too: an address for servers that are all off would configure
    nothing.

**\--script** *FILE*
:   Load a debugger script (`.jds`); see **SCRIPTING**. Repeatable: the
    scripts load, and their rules run, in the order given. A script that
    cannot be read, or that has any error, is reported as
    *file*:*line*:*column*: *message* and jnext exits 1 before the machine
    runs - nothing of it runs partially. With **\--load**, a script that names a source
    line (`on execute main.c:19`), an `@symbol` that **\--map** does not know,
    or a z88dk **\--map** symbol of a section that names no page waits, with the scripts after it, until the
    program is in (it is still read and parsed at once); an error then also
    exits 1. Its rules start at the load: an `on frame` *N* or a **\--script-key**
    due before it never fires (a `.tzx` or `.wav` loads about 100 frames in), so
    give such rules in an earlier script that names neither.
    In **\--headless** and the SDL-only
    build a script decides the exit status: `exit` *N* exits *N*, a `stop` or
    a failed `assert` exits 3, a run-time error exits 1. In the Qt GUI a script
    never ends the program: `stop` and `exit` pause the machine instead.

**\--script-key** *FRAME* *N*
:   Deliver script host key *N* (`1` to `8`) at emulated frame *FRAME*
    (**\--headless** only; repeatable): the script's `on hostkey` *N* rules run
    at the end of frame *FRAME*, where `on frame` *FRAME* runs. Needs a
    **\--script**, or a **\--record-script** (key `8` is the recorder's
    capture).

**\--record-script** *FILE*
:   Record the session as a replay script; see **Recording a session** under
    **SCRIPTING**. Every input change is written as the frame it landed on,
    each Alt + 8 (Command+8 on macOS) (or **\--script-key** *FRAME* `8`) captures the screen, and
    *FILE* is written when jnext exits. Works in every frontend.

**\--map** *FILE*
:   Load a symbol table, a z88dk `.map` or an sjasmplus `--sym` or `--exp`
    file, told apart by its contents (z88dk's assembler also writes a `.sym`,
    in the map format), so a script can name an address as `@symbol` (an
    sjasmplus label is `@main.loop`), and the debugger shows the names (the
    same table **Map > Load MAP** fills). A z88dk map's symbols take their pages
    as its source lines do: a banked one only while its page is mapped, the rest
    in the page mapped at their address when the program is loaded (a map read
    before **\--load** takes them then). A file that cannot be loaded, or
    holds no symbols, is a startup error. More than 64 MiB (of a file, or of what
    a pipe such as `--map <(cmd)` delivers) is refused, and the error names the
    limit.

**\--source-map** *FILE*
:   Attach a source map, an sjasmplus SLD or a z88dk `-debug` `.map` (told apart
    by its contents), to the program **\--load** loads, as if loaded by hand with
    **Debug > Source > Load Source Map...**. A z88dk map gives source lines only
    (and reads the `--list` listings beside it); its symbols stay with **\--map**. Requires **\--load**, unless *FILE* is the
    map of a z88dk dot command (`-subtype=dot`, `dotx`; built with `-debug`): that map is attached
    at once, before any script loads, and stays pending until the guest runs the
    command, when it is checked (the command is loaded into DivMMC RAM by
    NextZXOS, which jnext does not know until it runs; see the User Guide). Any
    other map without **\--load** is refused (exit status 1). An RZX replay (**\--rzx-play** *R*, or
    **\--load** *R*`.rzx`) is the same case: a dot command's map attaches pending, any other map is
    refused at start-up naming RZX (exit status 1). A map whose binary
    identity does not match the program, or that cannot be loaded, is a startup
    error (exit status 1). With a tape program (`.tap`, `.tzx`, `.wav`) the map
    attaches at once and is checked the first time the program runs its code; a
    mismatch then drops it, and the run exits with status 1.

**\--source-dir** *DIR*
:   Also search *DIR* for the source files a source map names, in the Source tab
    of the debugger (the Qt window with the debugger; elsewhere a warning, and
    ignored). One directory per option, repeat it for more; the list separator
    (`:`, `;` on Windows) is refused. The Source tab tries a file beside the
    source map and in its parent directories first, then these directories, the
    longest matching trailing part of the recorded name first and the
    directories in the order given. For this run only, like **\--set**
    `paths.source_dirs`, whose list it appends to (a later **\--set** of that
    key replaces what came before). A list given on the command line REPLACES
    the saved one for the run; directories added by **Locate File...** still
    apply. A relative *DIR* is taken from the working directory. The saved list
    is edited under **Preferences > Paths**, and **Locate File...** in the
    Source tab adds a directory.

**\--annotations** *FILE*
:   Load a debugger annotations file (`.jdp`): the user's labels and comments,
    each keyed by a memory (the page or ROM that supplies the address, written
    as the debugger shows it: `P0E`, `ROM1`, `DIVRAM2`) and a 16-bit address. A
    user label is shown by the debugger views, named by a script's `@name` and
    by GDB `monitor sym`, over the toolchain's own symbols, and only while its
    memory supplies the address. The file is plain UTF-8 text: the line
    `jnext-annotations 1`, then `label SPACE $ADDR NAME` and `comment SPACE
    $ADDR TEXT` records (in TEXT `\\` is a backslash and `\n` a line break);
    lines starting with `#` and blank lines are ignored. It is loaded before the
    scripts, replaces nothing the program or **\--map** loaded, and is kept
    across resets, state loads and rewinds. In the Qt debugger the labels and
    comments are added from the Disassembly and Source right-click menus, and
    **Map > Open Annotations...**, **Save Annotations** and **Save Annotations
    As...** read and write the file (Save writes back to *FILE*). A file that cannot be read, or does
    not parse (the message names the line), is a startup error (exit status 1).

**\--import-skoolkit** *FILE*
:   At start, after **\--annotations**, read *FILE*, a skoolkit control file (your
    own, or one made by `skool2ctl.py -k -h` from a disassembly), into the
    debugger's labels and comments. Taken: the `@ ADDR label=NAME` lines (a
    leading `*` is dropped) and the text of the `B`, `C`, `S`, `T` and `W`
    sub-blocks, with their `.` and `:` continuation lines, as the instruction
    comment at that address. Counted in the log, not imported: block titles
    (sna2skool writes its own), `D` descriptions and the other directives. The
    memory of a file is its `# jnext space TAG` first line, else its name
    (`TAG.ctl` or `WWWW-TAG.ctl`, as **\--export-skoolkit** writes them), else
    the memory that supplies the address when the file is read, which is
    before **\--load** runs (the page mapped at start, the ROM at `$0000`). An
    imported label or comment replaces the one at the same address; all the files of
    the option are read first, so a failure changes nothing. Repeatable. A file that
    cannot be read, or that skoolkit would only warn about (an address that is not
    decimal, `$hex` or `%binary`, or above 65535; an unknown directive; a label name
    that is not a name; one name at two addresses), is a startup error (exit status
    1) naming the line.

**\--import-ghidra** *FILE*
:   As **\--import-skoolkit** for a Ghidra symbol file: lines `NAME ADDRESS l`
    (`l` or `f` or anything else, ignored), the address `0xHHHH` or `TAG:0xHHHH`
    with a jnext memory tag, as **\--export-ghidra** writes them. A blank line
    is skipped. A `#` line, an address prefix that is not a jnext memory tag
    (`ram:0x8000`), an expression, or a name that is not a name is a startup error
    (exit status 1). Repeatable. There are no comments in the format.

**\--export-skoolkit** *DIR*
:   When jnext exits, write the debugger's labels and the code/data map that
    **\--coverage** recorded into *DIR* as skoolkit control files (*DIR* is
    created). One `WWWW-TAG.ctl` and `WWWW-TAG.bin` pair per memory and
    window: *TAG* names the memory as the debugger does (`P0A`, `ROM0`,
    `DIVRAM3`), *WWWW* is the hex base of the 8 KB window (16 KB for ROM) the
    labels sit in, and the `.bin` holds that window's bytes, so
    `sna2skool.py -o 0xWWWW WWWW-TAG.bin` picks its control file by itself and
    `sna2skool.py -c DIR` combines them. Unpaged labels are in `0000-0CPU.ctl`.
    Executed bytes become `c` blocks, read or written ones `b`, the rest `u`;
    without **\--coverage** each window is one `c` block. skoolkit has no Z80N
    support, so Z80N instructions are written as `DEFB` sub-blocks. sna2skool
    drops without a word a label that is not at the start of a line it writes,
    so every window file also splits (`B`/`C` sub-blocks) at each unpaged label
    that falls in it, and the labels survive `sna2skool.py -c DIR`. A previous
    export's files in *DIR* are replaced; any other file there is never
    touched, and a `.ctl` or `.bin` of the same name that is not jnext's makes
    the export fail. A failed export is logged and jnext exits with status 1.

**\--export-ghidra** *DIR*
:   When jnext exits, write the debugger's labels into *DIR* (created) as symbol
    files for Ghidra's `ImportSymbolsScript.py`, one line `NAME ADDRESS l` per
    label: `TAG.ghidra.txt` for each memory (`P0A:0x8000`, the address in the
    overlay address space *TAG* that you create for that page),
    `CPU.ghidra.txt` for the unpaged labels (`0x8000`) and `view.ghidra.txt` for
    the memory as mapped when jnext exits (`0x8000`, no overlay needed). A previous
    export's `CPU.ghidra.txt`, `view.ghidra.txt` and `TAG.ghidra.txt` files in
    *DIR* are replaced or removed; any other file there, any other
    `*.ghidra.txt` included, is never touched. A failed export is
    logged and jnext exits with status 1.

**\--bug-report** *PATH*
:   When jnext exits, write a bug report to *PATH*: one zip file with the state of
    the machine and of jnext, for reading, not for replaying. If *PATH* is an
    existing directory, or ends in a slash (a backslash on Windows), the report is
    written inside it, named *PROGRAM*`.YYYYMMDDhhmm.jnext-bug.zip` (local time;
    *PROGRAM* is the last program loaded, `jnext` if none, and `-2`, `-3`... is
    added when the name is taken). Otherwise *PATH* is the file. A failed write is
    logged and jnext exits with status 1. See **BUG REPORTS**.

**\--magic-port** *PORT*
:   Enable the magic debug port at *PORT* (hex, `0x` prefix optional, for example `0x00FF`).

**\--magic-port-mode** *MODE*
:   Magic-port output mode: `hex` (default), `dec`, `ascii`, `line`. Requires
    **\--magic-port**.

**\--profile**
:   Enable the CPU T-state profiler. It allocates an mmap'd histogram and
    accumulates one entry per executed instruction; on exit the histogram is
    written to **\--profile-output**.

**\--profile-output** *FILE*
:   Output path for **\--profile** (default `profile.dat`). Requires
    **\--profile**.

**\--log-level** *SPEC*
:   Set per-subsystem log levels; see **LOGGING**.

**\--log-file** *FILE*
:   Write the log to *FILE* instead of the console. *FILE* is truncated at the
    start of every run, and jnext exits non-zero if it cannot be opened rather
    than quietly logging to the console instead. See **LOGGING**.

## Misc

**\--help**, **-h**
:   Print the built-in help and exit.

**\--version**, **-V**
:   Print the version and exit.

## Settings for \--set

Each entry below is a key of `~/.jnext/jnext.conf`, written `section.key`, with
the values it takes. Where the setting has a flag of its own, the entry names
it: the flag and **\--set** are the same setting, and the later one given wins.
The entries say which frontends use the key: the Qt window, the SDL-only build
(both interactive) or **\--headless**. Booleans are `true` or `false`.

`startup.machine_type` *48k|128k|plus3|pentagon|next*
:   The machine type; **\--machine**. All frontends.

`startup.cpu_speed` *0..3*
:   The CPU speed at start: 0 is 3.5 MHz, 1 is 7, 2 is 14, 3 is 28;
    **\--cpu-speed** takes the MHz. A starting value: NextZXOS sets its own
    speed while it boots. All frontends.

`startup.emulator_speed_percent` *10..1000*
:   The emulation speed as a percentage of real time; **\--speed**. Where
    **\--speed** clamps a value outside the range, **\--set** refuses it. The
    Qt window and the SDL-only build.

`startup.window_scale` *1..3*
:   The window scale; **\--scale**. The Qt window.

`startup.crt_filter` *true|false*
:   The software scanlines effect; **\--crt scanlines**. The Qt window and the
    browser build.

`startup.crt_shader` *true|false*
:   The GPU CRT shader; **\--crt**. The Qt window and the browser build.

`startup.crt_shader_style` *0..2*
:   The shader's look: 0 is `tv`, 1 is `pc`, 2 is `soft`; **\--crt**. The Qt
    window and the browser build.

`startup.silent` *true|false*
:   Start with the sound muted; **\--silent**. All frontends.

`startup.tape_fast_load` *true|false*
:   Tape fast-load; `false` is real time, which is **\--tape-realtime**. All
    frontends.

`startup.tape_auto_play` *true|false*
:   Automatic tape play and stop; **\--tape-auto-play** and
    **\--no-tape-auto-play**. All frontends.

`startup.fast_boot_seconds` *0..60*
:   Seconds of a plain Next start run at maximum speed, 0 = off;
    **\--fast-boot**. The Qt window and the SDL-only build.

`startup.fast_boot_hide_screen` *true|false*
:   Keep the window black while fast boot runs; any key, a pause or a
    debugger stop shows the screen at once. Default false. The Qt window and the
    SDL-only build.

`startup.when_slow_prefer` *audio|video*
:   What a host too slow for real time sacrifices; **\--when-slow-prefer**. The
    Qt window and the SDL-only build.

`audio.gain_db`, `audio.gain_beeper_db`, `audio.gain_ay0_db`, `audio.gain_ay1_db`, `audio.gain_ay2_db`, `audio.gain_dac_db` *-24..24*
:   The master, beeper, AY #0, #1, #2 and DAC gains in dB; **\--audio-gain-db**,
    **\--audio-gain-beeper-db**, **\--audio-gain-ay0-db**,
    **\--audio-gain-ay1-db**, **\--audio-gain-ay2-db** and
    **\--audio-gain-dac-db**. All frontends.

`paths.last_load_dir` *PATH*
:   The directory the Load dialogs open in. The Qt window.

`paths.sd_card_path` *FILE*
:   The SD card image; **\--sdcard**. Empty means none given. All frontends.

`paths.sd_card2_path` *FILE*
:   The image in the second SD card slot; **\--sdcard2** gives the same. Empty
    means no second card. All frontends.

`paths.screenshot_dir` *PATH*
:   The directory the Save Screenshot dialog opens in. The Qt window.

`paths.source_dirs` *DIRS*
:   The directories the debugger's Source tab also searches for source files,
    separated by `:` (`;` on Windows); **\--source-dir** appends one. Empty
    means none. Given on the command line, the list REPLACES the saved one for
    that run. A relative directory is taken from the working directory. The Qt
    window with the debugger.

`sdcard_manager.card` *FILE*
:   The SD card image the SD card file manager last showed. On opening, the
    window shows that card if it is in a slot, and otherwise the first card
    that is. The Qt window.

`sdcard_manager.card_dir` *PATH*
:   The folder of that card the manager last showed, with every byte outside
    printable ASCII and every `%` written as `%XX`. The window opens in it only
    on the remembered card and only if it exists on the card now; otherwise it
    opens at the card's root. The Qt window.

`sdcard_manager.host_dir` *PATH*
:   The host folder the manager's right-hand pane last showed. If it no longer
    exists, the nearest existing parent is used, then the Load directory, then
    the home directory. The Qt window.

`screenshot.quick_dir` *PATH*
:   The Quick Screenshot directory; empty means `~/.jnext/screenshots`. The Qt
    window.

`screenshot.quick_format` *png|scr*
:   The Quick Screenshot format. The Qt window.

`snapshot.quick_keep` *1..1000000|unlimited*
:   How many quick snapshots Quick Save keeps; **\--quick-snapshot-keep**. The
    Qt window and the SDL-only build.

`input.joy1_source`, `input.joy2_source` *sdl|keys|none*
:   The host source for Joy 1 and Joy 2; **\--joy1-source** and
    **\--joy2-source**. The Qt window and the SDL-only build.

`input.joy1_device`, `input.joy2_device` *ID*
:   The controller assigned to Joy 1 and Joy 2 (32 hex digits, optionally
    `#N`); empty is Automatic, which **\--joy1-device** and **\--joy2-device**
    also take as an empty ID. The Qt window and the SDL-only build.

`input.mouse_release` *ctrl-alt|middle-click|esc*
:   How a captured mouse is released; **\--mouse-release**. The Qt window and
    the SDL-only build.

`debugger_keys.`*ACTION* *COMBO*
:   The key combination of a debugger action, like the **Preferences** Debugger
    Keys tab: *ACTION* is `run`, `pause`, `step_into`, `step_over`, `step_out`,
    `step_back`, `frame_back`, `run_to_cursor`, `run_to_eof`, `run_to_eosl`,
    `trace_toggle`, `trace_export`, `source_step_into`, `source_step_over`,
    `source_step_out`, `source_step_back`, `source_reverse_continue`, `follow`,
    `go_back`, `go_forward` or `run_to_interrupt`, and `none` unbinds it. A combination
    that is not legal there, or that another action already holds (unless the
    same command line frees it), is refused with exit status 1 before the
    window opens. The Qt window with the debugger.

`source.colour_scheme` *auto|light|dark|borland*
:   The colour scheme of the debugger's Source tab, like the **Preferences**
    Debugger tab: `auto` (the default) is light or dark after the application,
    `borland` is Borland C++ 3.1 (yellow on blue). The Qt window with the
    debugger.

`esp.enabled` *true|false*
:   The emulated ESP-01 WiFi module; **\--esp** and **\--no-esp**. All
    frontends.

`esp.allowed_hosts` *HOST,HOST,...*
:   The hosts the ESP may connect to, comma-separated; empty means any;
    **\--esp-allow** gives one host per option and is appended to this list.
    All frontends.

`nextpi.enabled` *true|false*
:   NextPi under QEMU on UART 1; **\--nextpi** and **\--no-nextpi**. All
    frontends.

`nextpi.dir`, `nextpi.release`, `nextpi.qemu_binary`, `nextpi.audio`, `nextpi.qemu_args` *TEXT*
:   The NextPi directory, release, QEMU binary, Pi audio and extra QEMU
    arguments; **\--nextpi-dir**, **\--nextpi-release**, **\--nextpi-qemu**,
    **\--nextpi-audio** and **\--nextpi-qemu-args**. Surrounding blanks are
    removed. All frontends.

`debugger.attached` *true|false*
:   Whether the debugger window follows the emulator window. The window opens
    as told for this run; the file keeps its value until the **Attach to
    Emulator Window** menu item is toggled. The Qt window with the debugger.

`debugger.physical_addresses` *true|false*
:   Whether the debugger names an address's memory by where it sits in the
    Next's memory (`$060000`, `BRAM7+$0000`) instead of by page and ROM name
    (`P0E`, `ROM1`, `DIVROM`). The window opens as told for this run; the file
    keeps its value until the **Physical Addresses** item of the **Debug** menu
    is toggled. The Qt window with the debugger.

# MACHINES

`48k`
:   ZX Spectrum 48K - the original rubber-key Spectrum.

`128k`
:   ZX Spectrum 128K - AY sound and memory paging.

`plus3`
:   ZX Spectrum +2A/+3 - extended paging.

`pentagon`
:   The Next's Pentagon 128 personality, as the TBBlue firmware's own Pentagon
    entry runs it: the 128K ROM (`128.rom`) and paging, Pentagon timing (no
    contention, a 448 x 320 frame, always 50 Hz), the Covox on port 0xFB, and
    Pentagon-512/1024 paging through NextREG 0x8F. Not the original Russian
    clone's own hardware.

`next`
:   ZX Spectrum Next (Issue 2) - full Next hardware and extended features.

Like the Next firmware's own 48K, 128K, +3 and Pentagon personalities, the four
standalone machines start with the Next-only ports off: Timex port 0xFF, the
zxnDMA's port 0x6B, the Layer 2 and sprite ports, the CTC and 0xEFF7 (and 0xDFFD,
except on the Pentagon). The 48K also has 0x7FFD paging and the AY off; 0x1FFD
and the +3 floating bus are on only on the +3. Software turns any of them back
on through NextREG 0x82-0x85, as on the hardware.

The machine type can also be switched at runtime from the GUI
(**Machine > Machine Type**); switching resets the machine.

# SD CARD AND ROMS

jnext takes its ROMs from an SD-card image, exactly like real ZX Spectrum Next
hardware. Where each one comes from:

- **The FPGA boot ROM** (`nextboot.rom`, 8 KB) is *silicon-baked*: embedded in
  the jnext binary at link time. No flag, no lookup; it mirrors the on-FPGA
  flash IPL of the real machine.
- **The ROMs jnext loads itself** - 48K / 128K / +3 BASIC, DivMMC firmware,
  Multiface firmware and the Alt ROM - are read from the SD-card image at the
  canonical TBBlue paths under `/MACHINES/NEXT/` (`48.rom`, `128.rom` (also the Pentagon's),
  `plus3.rom`, `enNxtmmc.rom`, `enNextMf.rom`, `enAltZX.rom`). That is the
  whole list of files jnext reads from the card itself. It reads them from a
  FAT12, FAT16 or FAT32 card, with a partition table or without one, and finds
  the volume the way the Next firmware does (**\--sdcard-file-add** still needs
  a FAT32 card).
- **NextZXOS is not one of them.** It is loaded by the firmware, not by jnext:
  the machine boots `/TBBLUE.FW` from the card through the emulated SD
  interface and that firmware streams `/MACHINES/NEXT/enNextZX.rom` into
  memory itself, exactly as on real hardware.

So even a **\--machine** `48k` run needs an SD-card image: that is where
`48.rom` lives.

Point at an image with **\--sdcard** *FILE*, or give no **\--sdcard** at all:
jnext then falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (a
FAT32-patched image), offering to download the canonical distribution image
(kept alongside it as `cspect-next-1gb.img`) and produce that patched copy on
first run. **\--sdcard-download-confirm** skips the prompt, which is useful in
scripts; **\--sdcard-download-force** re-downloads and re-patches a corrupted
cached image. An interrupted download (Cancel, a dropped connection, or 30
seconds without data) is kept (`sn-emulator.zip.part`) and continues on the next
run. A download that fails, or whose file is no zip, is retried up to three times,
after 2, 4 and 8 seconds, each retry noted on the terminal and in the download window; Cancel is not retried, nor is a refusal by the server (HTTP 4xx, except 408 and 429).
An explicit **\--sdcard** always wins over both.

When the default card is of an older NextZXOS distribution than the one jnext
downloads now, jnext tells you a newer one is available, and in the GUI asks
what to do. **Upgrade (keep my files)** downloads the new image, carries the files you
saved (and the contents of any **\--sdcard-writes** file) over to it, and keeps the old
card, untouched, as a backup in `previous/` next to the card; where NextZXOS itself changed
a file you also changed, the new version wins, and a report lists what was carried and
what was replaced. A card that cannot be compared with the one it came from (its original
image is gone) cannot have its files carried over: **\--sdcard-upgrade fresh** then starts
a new card and keeps the old one. A failed download or patch changes nothing. **Not now**
goes on using the old card and asks again next time; **Keep my card (don't ask again)**
stops the offer for this distribution (a later one asks once more). A terminal or scripted run (**\--headless**,
**\--sdcard-file-add**) never asks: it keeps the card and logs one line. The
choice can be made without the GUI with **\--sdcard-upgrade**. A card you name
yourself, with **\--sdcard** or in Preferences, is never offered an upgrade.
Each distribution's card has its own FAT32 volume serial.

Note that jnext opens the image **read-write** by default, and writes the
emulated machine makes to the SD card are persisted to the file. Two runs
sharing one image are therefore not independent: a run that boots NextZXOS
mutates the image the next run reads. For a reproducible run, either pass
**\--sdcard-readonly**, or give it a private copy (`cp --reflink=auto` is
instant and costs no space on a copy-on-write filesystem). A third way is
**\--sdcard-writes** *FILE*: the image is then never written, and the
machine's writes are kept in *FILE* and laid over it on every run.

The mounted image is also the SD card the emulated machine sees at runtime,
through the SPI/DivMMC path: NextZXOS reads its files from it.

## A SECOND SD CARD

The Next board has two SD sockets. **\--sdcard2** *FILE* puts a second card in
the second one. NextZXOS maps every FAT partition of every card to a drive
letter, first card first: with one partition on the first card, the second
card's first partition is **D:**. Type `CD "d:"` to enter it (`.nexload d:/x.nex`
does not work: `:` separates BASIC statements, so quote the path or `CD` first).
NextZXOS finds its drives at start-up, so the card must be in the slot before
the machine boots or hard-resets; there is no card-detect line.

- The second card is a plain image of its own (a partition table and a FAT
  partition, for example a copy of a real card made with `dd`, or one built with
  `sfdisk` and `mformat`). It must not be the first card's file.
- ROMs and the firmware always come from the first card: the second one is never
  read for them, and is not downloaded or upgraded.
- It follows the first card's rules: **\--sdcard2-readonly**,
  **\--sdcard2-writes** and **\--sdcard2-writes-reset** work as their first-card
  twins. A soft reset keeps the card as it is; a hard reset power-cycles it.
- **\--sdcard-file-add** works on any FAT32 image, so `jnext --sdcard card2.img
  --sdcard-file-add FILE` puts a file on a second-card image.
- A recording of the warm start is keyed on the second card as well (its size
  and modification time, and the contents of its **\--sdcard2-writes** file), so
  changing it takes a new recording. Rewind carries
  its state.
- The card can also come from **Preferences > Paths > Second SD card image**
  (`paths.sd_card2_path`, reachable with **\--set**) when **\--sdcard2** is not
  given. A saved path that no longer exists is logged and leaves the slot empty;
  only an explicit **\--sdcard2**, or **\--set** `paths.sd_card2_path=`, refuses
  to start.
- The slot is optional: with nothing in it the machine behaves exactly as it did
  before the slot existed.
- One image never goes in both slots: **\--sdcard2**, a Preferences path, **File >
  Insert** and the delayed inserts all refuse the file the other slot has mounted
  (two cards writing one FAT would corrupt it).
- The slot can be changed while the machine runs (see **CHANGING THE CARD WHILE
  RUNNING**).

## CHANGING THE CARD WHILE RUNNING

**File > Insert SD Card Image...** swaps the card in the running machine, and
**File > Eject SD Card** takes it out; nothing is reset. The Next has no
card-detect line, so the guest is not told: use NextZXOS's **REMOUNT**, which
asks you to *Remove/insert SD and press Y* - change the card, then press Y.

**File > Insert Second SD Card Image...** and **File > Eject Second SD Card** do
the same for the second slot, which NextZXOS sees as the next drive (see **A SECOND
SD CARD**): its drive appears at the next hard reset or REMOUNT. A hard reset after
an eject of the second card keeps the slot empty, and after an insert keeps the new
card: unlike the first card, its path is not kept for the ROMs. The inserted card is
write-protected exactly when **\--sdcard2-readonly** was given. These two menu items
have no keyboard mnemonic.

- The ROMs are not re-read: they were loaded at power-on, as on hardware. A
  later hard reset, or loading a program, reads them from the inserted card.
  After an eject, those put the last inserted card back (the first card only).
- Refused while an RZX recording is made or played, and while a directly
  loaded NEX keeps its own file open. The rewind history is discarded.
- A `.jns` snapshot records the card it was taken on. Once a *different*
  card is in (another FAT32 volume, partition table or size; a copy of the
  same card is the same card) it refuses to load: insert the original card
  again, or pass **\--snapshot-mode** `force`. The second SD card, when one was
  mounted, is recorded and checked the same way (see **JNEXT SNAPSHOTS**).

**File > SD Card File Manager...** (and the SD card button on the toolbar) opens a
window that copies, moves, renames and deletes files and folders on the card of the
running machine, with a folder of the host beside it. It does not pause or reset the
machine and asks no question before a change: the change is on the card at once and the
machine sees it when it next reads that folder (NextZXOS keeps the part of the folder
it last read in memory and brings it up to date when it reads the folder again after
reading something else). Do not change a file a running program has open or is
writing. With **\--sdcard-writes** the changes go into *FILE* and the image is not
touched. The window is modeless, there is one of it, and it is inert while an upgrade
or a go back works on the card. A card item that changed since the window listed it
(the machine can change the card meanwhile, also while a question is open) is not touched: that item
fails, the run stops there and the items after it are not attempted.

Other programs that write an image while jnext runs it behave as the window does
when the card has no writes file, under the same rules; with a writes file they
must never be used (see **\--sdcard-writes**). Replacing, deleting or resizing the image
file while it is mounted is never safe: the machine keeps the old file, and jnext
warns once when it sees it.

## PUTTING A FILE ON THE CARD

**\--sdcard-file-add** copies a host file, or a whole directory, into the
image and exits. No emulator starts, nothing boots; it is a file-copy command
that happens to live in the emulator binary, so getting programs onto the card
needs no `mtools` and no loopback mount.

    jnext --sdcard-file-add game.dsk --sdcard-file-dest /NEXTZXOS/DRV-A.DSK
    jnext --sdcard-file-add demo.nex --sdcard-file-dest /DEMOS/demo.nex
    jnext --sdcard-file-add demo.nex
    jnext --sdcard-file-add mygames
    jnext --sdcard-file-add mygames --sdcard-file-dest /GAMES/MINE

**\--sdcard-file-dest** is the path on the card that the source *becomes*. It is
optional: without it the source goes into the root of the card under its own
name, so the third example writes `/demo.nex` and the fourth `/mygames`. A
directory given with **\--sdcard-file-dest** `/` has its contents copied
straight into the root instead. Missing directories along the path are
created, so the second example works on a card with no `/DEMOS`. Names may be
long: an 8.3-clean uppercase name such as `DRV-A.DSK` is written as a plain
short entry, and anything else gets VFAT long-name entries plus a generated
short name, exactly as any other FAT32 driver would write it.

One run copies one source, so the pairing is never in doubt: giving
**\--sdcard-file-add** or **\--sdcard-file-dest** twice is refused (exit 1),
and the two may come in either order. To copy several things at once, put them
in a directory and add that.

A destination file that already exists is **refused**, and the file on the card
is left alone — overwriting `DRV-A.DSK` would destroy a disk image. Pass
**\--sdcard-file-force** to replace it deliberately.

A directory is copied recursively, in name order:

- A directory that already exists on the card is **merged into**, never
  replaced; the files in it follow the rule above, each one.
- **Symbolic links are followed**: FAT has no links, so the card gets what a
  link points at. A link that leads back into its own parent directories is
  refused, as is a link to nothing.
- **Empty directories** are created.
- Refused, before anything is written: a device, FIFO or socket; a file or
  directory that cannot be read; a file of 4 GiB or more; a name FAT cannot
  hold (non-ASCII, or one of `" * : < > ? \ |`, or ending in a dot or a space);
  and two names in one directory that differ only in case, which FAT would make
  one file; and a destination that is another entry's 8.3 alias (`/LONGFI~1.TXT`
  for a file stored as `Long File Name.txt`), which would write into that entry.
  Nothing is renamed or silently skipped — hidden files included.

A file or a directory, the copy is **all or nothing**. Every refusal is decided
before the first byte is written, so a refused copy — including one that does
not fit, counting the directories it would have to make — leaves the card
exactly as it was. If the copy fails part-way — the card fills up, or a file
shrinks while it is read — everything this run created, files and directories,
is removed again before jnext exits with an error. Two things cannot be undone:
a file **\--sdcard-file-force** had already replaced (the message says how
many), and the extra cluster an existing directory grew by to hold a new entry,
since FAT never shrinks a directory. The free-space check counts a file that
**\--sdcard-file-force** will replace at its full size, so a forced copy onto a
nearly full card can be refused even though it would just fit.

The copy goes into the image **\--sdcard** names. With no **\--sdcard** it goes
into the default-location image, your own card, which every run without
**\--sdcard** boots. It does not make
a backup: these are gigabyte files, and `cp --reflink=auto` is instant on a
copy-on-write filesystem.

With **\--sdcard-writes** *FILE* the copy goes into *FILE* instead, and the
image is opened read-only and never written, so a card shared by other runs
stays as it is. The next run
that is given the same image and *FILE* boots a card that has the file:

    jnext --sdcard-file-add DOTEST --sdcard-file-dest /DOT/DOTEST --sdcard-writes dev.jsdw
    jnext --sdcard-writes dev.jsdw

The rules are those of a copy into the image. A second add of the same
destination is refused (exit 4) and the card kept in *FILE* is left as it was; with
**\--sdcard-file-force** it is replaced. The first replacement adds the new
copy's sectors to *FILE*; replacing it again by a file of the same size does not
make *FILE* grow further. **\--sdcard-writes-reset** starts again from the
image. Adds accumulate in *FILE*, together with anything a run of the machine
wrote and the file the esxDOS stand-in keeps. *FILE* holds a copy of everything
added, so for a big tree a private copy of the image is the better tool. A
*FILE* that holds a rewritten partition table or boot sector of the card is
refused (exit 5) before anything is written, because the card the machine sees
is then not the one the copy would be laid out for; **\--sdcard-writes-reset**
starts again. The warm-start recording is taken again by the next **\--load**,
as after any write to the card.

The image must be a spec-valid FAT32 (at least 65525 clusters) — the same thing
the Next's own firmware requires. The image jnext provisions for itself already
is one; a hand-made under-clustered image is refused, and
`tools/fix-sdcard-image.sh` re-clusters one, applying the same fixes jnext's own
provisioning does (a default `config.ini`, and the 24.11 distribution's 16 KB
`zx81.rom` doubled so the firmware boot menu can load it).

The exit status says which of the things that can go wrong did; see
**EXIT STATUS**.

*DRV-A.DSK* is not an arbitrary example: NextZXOS auto-mounts any `.DSK` or
`.P3D` found at `/NEXTZXOS/DRV-`*X*`.DSK` (and `CPM-`*X*`.`*ext*) as drive *X*,
so copying one there is all it takes to have a +3 disk available at the next
boot.

## GETTING YOUR FILES OUT

Four commands, like **\--sdcard-file-add** terminal commands that start no emulation, get
your files off a card or into one:

    jnext --sdcard CARD --sdcard-writes dev.jsdw --sdcard-writes-flatten flat.img
    jnext --sdcard CARD --sdcard-export-base ORIGINAL --sdcard-export-changes ~/my-files
    jnext --sdcard new.img --sdcard-import-changes ~/my-files
    jnext --sdcard-file-extract /DEMOS/game.nex

**\--sdcard-writes-flatten** makes one plain image from a card and its writes
file. **\--sdcard-export-changes** copies every file you added or changed, as the
machine sees the card, into a directory, and writes `CHANGES.txt` there. Each
line of it is five TAB-separated fields: a kind (`A` an added file, `C` a
changed file, `D` an added directory, `R` a file you removed: listed, never
imported), the attribute letters among `R`, `H`, `S` in that order (or `-`),
the size in bytes, the path on the card (absolute, bytes outside printable
ASCII and `%` written `%XX`), and the file's path inside the directory (`-` for
`D` and `R`). The first line is `jnext-changes 1`; lines starting with `#` are
comments; lines are sorted by card path. Delete a line to leave that file out of
an import.

**\--sdcard-import-changes** is the inverse: it writes every `A` and `C` line's
file to its card path (making `D` lines' directories), replaces what is there,
restores the attributes, and reports which paths were written and which
replaced. It never applies an `R` line and never copies `CHANGES.txt`. A
directory without a valid `CHANGES.txt`, a listed file that is missing or has
another size, and a card path jnext cannot write are each refused before
anything is written. The three commands above take the exit statuses of
**\--sdcard-file-add** (see **EXIT STATUS**).

**\--sdcard-file-extract** takes one file or directory off the card, by name, and
puts it in the current directory. Write the card path with `/` or `\` between the
names (the leading one is optional); each name is matched against the long name or
the 8.3 name, ASCII letters in any case. The local entry is named after what the
card stores (`/GAMES/WOTEF.NEX` for a file stored as `WotEF.nex` gives
`./WotEF.nex`, and the 8.3 alias `/LONGFI~1.TXT` of a file stored as `Long File Name.txt` gives
`./Long File Name.txt`), never the
spelling typed. A name the host cannot hold is written with `%XX` in place of
each byte outside printable ASCII, each `%`, each of `<>:"/\|?*`, a trailing dot
or space, and a Windows device name's first letter (`CON.TXT` becomes
`%43ON.TXT`), on every system, as **\--sdcard-export-changes** writes them; each
such name is listed on standard output. A `.` or `..` component, a trailing dot or
space and a character outside printable ASCII in the card path are refused: give
the 8.3 name, or extract the parent directory (the only way left when the 8.3 name
itself holds such a character). A file or directory of that name
that already exists in the current directory, a symbolic link included, is
refused and never touched: nothing is overwritten, and nothing is merged into an
existing directory. It is all or nothing: every problem is found before anything
is created, and a failure while copying removes what the run created. Hidden,
system and read-only files are copied; dates and attributes are not. One jnext
may use a card at a time, so a card another jnext has open is refused (exit 5).
On Git Bash, which rewrites a path that starts with `/`, write `//DIR/NAME` or
`\DIR\NAME`. It exits with the statuses of **\--sdcard-file-add** (see **EXIT
STATUS**).

Like **\--sdcard-file-add**, **\--sdcard-export-changes**, **\--sdcard-import-changes** and
the upgrade's carrying over of your files use the first FAT32 (LBA) partition of the
card. The machine itself also reads FAT12, FAT16 and unpartitioned cards, so on such
a card of your own these commands cannot reach the volume it boots.
**\--sdcard-file-extract** reads every card the machine mounts: FAT12, FAT16 or FAT32,
partitioned (whatever the partition type, over a FAT boot record) or not. A FAT32
card with fewer than 65525 clusters is refused (exit 5): the Next's firmware
refuses it too; `tools/fix-sdcard-image.sh` re-clusters one.

# LOADING PROGRAMS

Use **\--load** *FILE*, or simply a bare filename (`jnext game.tap`). The two
are equivalent, so use one or the other: combining them
(`jnext --load game.tap game.tap`) is rejected, as is naming two files. The
format is detected from the extension:

`.nex`
:   v1.0/1.1/1.2; pages, Layer 2 screen and palette from the header. V1.3 is
    experimental and unsupported - it loads only with
    **\--experimental-nex-v1.3** (or the GUI warning's Proceed).

`.jns`
:   jnext's own whole-machine snapshot, including the machine type - see
    **JNEXT SNAPSHOTS**.

`.sna`
:   48K and 128K snapshots. Runs as the 48K (49179 bytes) or the 128K (longer).

`.szx`
:   Chunked snapshots, zlib-compressed pages. Runs on the machine its header
    names: 48K, 128K, +3 or Pentagon 128. Restores the AY registers.

`.z80`
:   v1/v2/v3, 48K and 128K, RLE-compressed or raw pages. Runs on the machine its
    hardware mode names (48K, 128K, +3 or Pentagon 128). Restores the AY
    registers and, for a +3, port 0x1FFD.

`.tap`
:   Fast load via a ROM trap; `LOAD ""` is auto-typed.

`.tzx`
:   Fast load or real-time. The tape player plays the standard, turbo, tone,
    pulse, pure-data and direct-recording blocks; a valid block it cannot play
    (CSW, generalised data, the deprecated C64, emulation-info and snapshot
    blocks, or an ID a later TZX revision adds) is skipped with a warning, and
    the blocks after it still load. A file that is not a well-formed TZX is
    refused.

`.wav`
:   RIFF/PCM EAR-bit playback (8/16-bit, mono/stereo).

`.rzx`
:   Played back as if **\--rzx-play** had been given.

`.zip`
:   An archive holding one program (`game.tap.zip`, `game.nex.zip`): the one
    file inside it with one of the extensions above is loaded, exactly as if
    it had been given on its own; screenshots, `.scr` and other files in the
    archive are ignored. An archive is recognised by its `.zip` extension, or,
    for a name with any other extension (or none), by its contents. When it
    holds several programs the command line refuses, listing them (unzip it and
    name the one you want); the window's **File > Load NEX File** asks which one. An
    archive that is encrypted, uses a method other than stored or deflate, is
    ZIP64 or multi-disk, is itself over 256 MiB, has a member over 256 MiB, or names a member with an
    absolute path or `..` is refused with the reason. The program is extracted
    to a temporary file under `~/.jnext/archive-load/` (or the
    **JNEXT_CONFIG_DIR** directory) that is removed when **jnext** exits. In
    the window, a `.pok` file in the archive (the one named like the program,
    else the only one) is offered under **Machine > POK Trainers...**.

    **A NEX with its data files.** When the program is a `.nex` and the
    directory it is in inside the archive (the archive's top level, or a folder
    such as `game/`) holds other files, that directory is extracted whole, with
    its subdirectories, and served to the NEX through the esxDOS stand-in, with
    no **\--esxdos-stub**: the program opens its files by name, as it would
    from a directory on disk (`game/G.NEX` with `game/data/L1.BIN` opens
    `data/L1.BIN`; case does not matter). The NEX is the program the archive
    loads when it is the only NEX and every other program in it lies in a
    subdirectory below its own, such as `data/sfx.wav`; a NEX and a `.tap` side
    by side, or two NEX in one directory, are several programs, refused on the
    command line and asked in the window. It is read-only unless
    **\--esxdos-stub-writable** is given, which lets the guest change the
    extracted copy, never the archive, and the changes are gone when **jnext**
    exits. An explicit **\--esxdos-stub-root** wins: the archive is still
    extracted, its files are not served. Files above the NEX's directory are
    not extracted. Before anything is written, the whole archive is checked: a
    member with an absolute path or `..` anywhere, two members whose paths
    differ only in case, a file that is also a directory, a name ending in a dot
    or a space, a path over 255 bytes, or files adding up to over 256 MiB refuse
    the load with the reason, and a member that cannot be read (bad CRC,
    encrypted) refuses it too, leaving nothing behind. A folder spelled in two
    cases in the archive (`data/` and `DATA/`) is one folder, written under the
    spelling that comes first, so the result is the same on every host. An archive whose NEX has
    nothing beside it extracts the NEX alone, as before, and gets no directory.

`.tap` and `.tzx` load in real time instead with **\--tape-realtime** (or by
unchecking **Tape > Fast Load** in the GUI).

RZX recording uses **\--rzx-record** *FILE*. The GUI's **File > Load NEX
File...** dialog accepts all of the above.

Raw binaries go straight into RAM with **\--inject** *FILE*
(**\--inject-org** load address, **\--inject-pc** entry point,
**\--inject-delay** a frame delay before injecting).

## esxDOS calls from a directly loaded NEX

On a real Next a NEX file is always started by NextZXOS, so the program can
use NextZXOS's esxDOS services (an `RST $08` followed by a call number). A NEX
loaded directly by jnext (**\--load**, a bare file name, or **File > Load NEX
File...**) is not started by NextZXOS, so jnext answers those calls itself -
ahead of the NextZXOS a warm start leaves resident (**\--warm-start-regenerate**),
exactly as **\--esxdos-stub** does with NextZXOS booted. This
starts when the NEX is loaded and stops at the next reset or soft reset. It
answers:

- the drive query (`M_GETSETDRV`): the only drive is `C:`, and asking for any
  other drive fails;
- the version query (`M_DOSVERSION`): NextZXOS 1.94, or 2.02 for a NEX that
  keeps its own file open (below);
- one file kept in memory: the program can create it, write it, and read it
  back under the same name, which is enough for a high-score or settings file.
  It is never written to a host file and is gone when jnext exits, unless
  **\--sdcard-writes** is given: the file is then kept in that file when the
  program closes it, and the next run starts with it;
- `run NAME.nex` (`M_EXECCMD`), which loads another NEX file from the same
  directory;
- the error message query (`M_GETERR`, with `B`=1): the text NextZXOS gives for
  an error code, copied to the buffer in `DE` with bit 7 set on its last
  character. `B`=0, which raises a BASIC error report and does not return on a
  Next, gets the error below;
- for a NEX whose header asks for its file to stay open: reading that file,
  including data appended after its banks (also through the block-streaming
  calls), and reading, never writing, other files in the same directory by
  name. Such a NEX gets the host files instead of the in-memory file
  (with **\--esxdos-stub** it gets its directory through the host directory
  below instead, which also lets it open several files, list the directory and
  see names in any case).

Every other call from `$80` to `$B1` returns an error (carry set, `A`=2),
including calls NextZXOS does implement, such as reading the current
directory, listing a directory or reading the date. A program that needs one of
those usually stops with its own error message. Codes above `$B1` are not
esxDOS calls and go to the code at `$0008`.

Without **\--esxdos-stub** or **\--esxdos-stub-root** this is not a filesystem.
Apart from the kept-open case above the program cannot see files on the host or
on the SD card, so a program that needs data files has to be copied to the SD
card with them and started from NextZXOS, or loaded with **\--esxdos-stub**,
which serves the directory the NEX is in (next section).

## A host directory for directly loaded programs

**\--esxdos-stub-root** *DIR* replaces the single in-memory file with the real
host directory *DIR*. `F_OPEN`, `F_CLOSE`, `F_READ`, `F_WRITE`, `F_SEEK`,
`F_FGETPOS`, `F_FSTAT`, `F_STAT` and `F_SYNC` then work on real files, and
`F_OPENDIR`, `F_READDIR`, `F_TELLDIR`, `F_SEEKDIR`, `F_REWINDDIR`, `F_GETCWD`,
`F_CHDIR`, `F_GETFREE` and `M_GETDATE` work as well. It is meant for developing
a program that reads data files: edit them in place and re-run, instead of
copying them into the SD-card image each time.

With **\--esxdos-stub-writable** it also changes the host files: `F_UNLINK`
(wildcards too), `F_RENAME`, `F_MKDIR`, `F_RMDIR`, `F_TRUNCATE`, `F_FTRUNCATE`
and `F_CHMOD`, and `M_GETERR` gives the text of an error code. Each answers as
NextZXOS 2.10 was measured to answer it (the probe and its recording are in the
source tree), including which error code a refusal carries. A write-protected
root (no **\--esxdos-stub-writable**) answers every call that would write with
`esx_edevicebusy`, after every check that does not write, and changes nothing.

**It does not make the host directory the SD card.** Only programs that reach
the filesystem through `RST $08` can see it — a directly loaded NEX, and dot
commands. NextZXOS cannot: its Browser, file selector, BASIC and loader use its
own in-ROM SD-card driver and its own FAT code, and never execute an `RST $08`
at all, so booting NextZXOS and looking for *DIR* in the Browser will show
nothing. This was measured, not assumed, and it is a permanent boundary rather
than an unfinished feature. Files that NextZXOS must see still go into the
SD-card image.

Some things it deliberately does not do. Writes need the separate
**\--esxdos-stub-writable** flag, because a guest write is a real and
irreversible change to a host file that rewinding cannot take back. A directory
listing is answered only where the answer is exactly the one NextZXOS gives:
the plain listing, which is also what NextZXOS returns for a sort/filter request
with nothing selected and for the wildcard `*.*` (NextZXOS matches a wildcard
against the 8.3 name, so `*.*` matches every entry). Sorting, filtering, every
other wildcard and the `+3DOS` header modes are refused rather than answered
approximately. "Exactly" is about which entries a listing returns: their order,
the spelling of their names (the host's case, where NextZXOS shows an 8.3 name
in capitals) and the read-only and hidden attribute bits follow the host files,
not a FAT directory. `M_P3DOS`, which lets a program
call NextZXOS ROM routines directly — including raw sector reads — is not
answered either: a host directory cannot supply sectors.

The block-streaming calls work on its files too, as streaming games use them:
`DISK_FILEMAP` on an open file, `DISK_STRMSTART` on the address it returns, the
data read from port `$EB`, `DISK_STRMEND`. The addresses are only handles for
that flow (one stream at a time); a program's own raw SD-card commands sent to
them are not served.

`F_GETCWD` answers in NextZXOS's form, `C:/` and then each directory's 8.3 name
followed by `/` (for example `C:/MYGAME~1/DATA/`), so a program that builds a
path as the current directory plus a name gets one that works. The error codes
for missing and mistyped paths are the ones NextZXOS returns.

Paths are confined to *DIR*. A path that climbs out of it is refused with
`esx_enotdir` (a bare `..` above the root, which NextZXOS treats as a missing
name), whichever separator it uses, a guest absolute path such as `/data/x.bin` means
`DIR/data/x.bin` and never the host's own `/data`, and symbolic links are
refused rather than followed.

What the write calls do, where it is not obvious. `F_UNLINK` deletes files only:
a directory is matched and left alone, with success, as on NextZXOS;
`F_RMDIR` removes an empty directory, refuses the current one, and calls a
non-empty one `esx_enospc`. A wildcard in `F_UNLINK` and `F_CHMOD` is matched as
NextZXOS matches it (the name and the extension, split at the last dot, each
matched alone, `?` for one character or none, `*` only as the last character of
either half, long or 8.3 name, any case); it works in the name only, never in a
directory, `F_TRUNCATE` takes the first file it matches and `F_RENAME`,
`F_MKDIR` and `F_RMDIR` refuse a wildcard. `F_RENAME` never replaces a file,
moves a source into an existing directory, and a directory it renames keeps its
open files and its place as the current directory. The read-only bit of a file
follows the host: a file without write permission is read-only for `F_STAT`,
`F_FSTAT`, `F_READDIR`, `F_OPEN` for writing, `F_UNLINK` and `F_TRUNCATE`, and
`F_CHMOD` with `A_WRITE` changes that permission. The archive bit of a file
answers as NextZXOS 2.10's does: set until an `F_CHMOD`, then clear after any
`F_CHMOD` of that file (set when the call itself cleared `A_ARCH`, which 2.10
reads that way), and set again by closing a handle opened for writing, by
`F_TRUNCATE`, and by creating the file anew; a rename keeps it. A host file has
no such bit, so JNEXT keeps it while it runs and **forgets it when it exits**
(on a card it would be in the directory entry), and a file changed on the host
reads as set again. An 8.3 short name
(`LONGNA~1.TXT`) keeps naming the same file after others are deleted, as on FAT,
and a name the host answers to that the directory listing does not hold (an NTFS
short name, a differently normalised spelling) is refused rather than reaching
another file.

Where it differs from NextZXOS, deliberately: `F_CHMOD` of the hidden and system
bits, and of a directory is refused with
`esx_enosys` (a host file has no such attributes; faking it would make
`F_STAT` contradict the answer); a name ending in a space is refused with
`esx_einval` (NextZXOS keeps the space, a host may strip it); a renamed current
directory shows its new name in `F_GETCWD`; `F_TRUNCATE` on a write-protected
root is refused where NextZXOS answers success and loses the write;
`F_OPEN` for writing on a write-protected root is `esx_erdonly`; an `F_OPEN` mode
with neither the read nor the write bit (`$04`, `$08`, `$0C`) is `esx_einval`
where NextZXOS opens or creates; and creating a file through the 8.3 short name
of an existing long-named file truncates that file and keeps its name, where a
FAT volume rewrites the entry and loses the long name.

A *DIR* that does not exist, or is not a directory, is an error at start-up:
jnext says so and exits non-zero rather than running with the option silently
inert.

**Without a *DIR*, the directory of the NEX.** **\--esxdos-stub** alone, with a
NEX to load (**\--load**, or a bare file name), serves the directory the NEX is
in, exactly as **\--esxdos-stub-root** with that directory would:
`jnext --esxdos-stub --load mygame/game.nex` lets the program open
`mygame/data.bin` by name. It is read-only unless **\--esxdos-stub-writable** is
given, and it replaces the single in-memory file (a program that saves a file
then gets an error instead of the in-memory copy; load it without
**\--esxdos-stub** to keep that). An explicit **\--esxdos-stub-root** takes
precedence. The directory is the one the path names (for a symbolic link, the
link's own directory). It lasts from the NEX load until the next reset, soft
reset or load: a NEX loaded later, through **File > Load NEX File...**, a recent
file, `run NAME.nex` or the debugger, gets its own directory. A NEX taken out
of a `.zip` that holds nothing beside it has no directory on disk and gets none
(one that holds its data files gets them, see the `.zip` entry above), and neither do a snapshot,
a tape or a program injected with **\--inject**. A `.jns` does not record it.

And it is not for use alongside a booted NextZXOS. Implying
**\--esxdos-stub** means it inherits the same shadowing: with NextZXOS running,
the file calls are answered before NextZXOS's own esxDOS sees them, so its file
commands break.

jnext answers only while ROM is paged in at `$0000`, as on a real Next, where
NextZXOS is reached through the DivMMC, which only takes over from the ROM. A
program that pages its own RAM in at `$0000` keeps its own `RST $08` code. That
rule also applies to **\--esxdos-stub** and to the kept-open file.

**\--esxdos-stub** answers a smaller set for programs loaded any other way (a
snapshot, a tape, **\--inject**) and for the whole session: the version query,
the in-memory file and `run NAME.nex` — or, with **\--esxdos-stub-root**, the
host directory described above. Every other call goes to the code at
`$0008` as usual. It is meant for programs run without NextZXOS: with
NextZXOS booted, it answers those calls in front of NextZXOS's own esxDOS, and
NextZXOS's file commands stop working.

## POK trainers

A `.pok` file (the World of Spectrum trainer format: named cheats, each a few
bytes to write) is not a program: it changes the one that is running, so
**\--load** refuses it. The Qt window opens it from **Machine > POK Trainers...**
or **File > Load NEX File**, lists its trainers, applies and removes them. In
**\--headless** and in the SDL-only window **\--delayed-pok-frames** *N* *FILE*
with **\--pok-trainers** *LIST* applies the chosen ones *N* frames in.

A line with a bank number 0 to 7 and an address of 0xC000 or above writes that
128K RAM bank (a 48K has bank 0 only); any other line, bank 8 to 15, which a 48K
game's file uses, or an address below 0xC000, writes the memory the memory map
shows at that address when the trainer is applied. A trainer is applied whole or
not at all. Keep the result with a snapshot (**File > Save
Snapshot...**, **\--delayed-snapshot**).

# JNEXT SNAPSHOTS

A `.jns` file is jnext's own whole-machine snapshot, and the only format that
can represent a ZX Spectrum Next: `.sna` and `.szx` describe a 48K/128K/Pentagon/+3
machine, which a Next is not, so asking for either on a Next is refused rather
than written lossily.

The other formats are written for the machines they can describe, and refused
for the states they cannot. A `.sna` follows the machine: a 48K gets the 48K
form (49179 bytes), a 128K and an ordinary +3 get the 128K form (all eight RAM
banks and the 0xC000 paging register). An SNA has no byte for the +3's second
paging register, so a +3 in *special paging*, or with ROM 2 or ROM 3 paged, is
refused with `.szx` named as the format that can hold it. An SNA names the bank
at 0xC000 in a field that reaches only banks 0-7, so a machine using **extended
paging** (port 0xDFFD, which is on by default on a Next and a Pentagon and
reachable on the other machines once software enables it in NextREG 0x82) is
refused too, pointing at `.jns` — `.szx` has no field for that register either.
An SNA also carries no machine identifier, so FUSE reads a 128K one as a
Pentagon 128 — the right RAM and paging, a different timing model. On
**\--machine** `pentagon` jnext writes that same 128K form, and an `.szx` as a
Pentagon 128 (machine ID 7).

Write one with **File > Save Snapshot...** (Alt+Shift+S), or headless with
**\--delayed-snapshot** naming a `.jns` file. Read one back with **\--load**, a
bare file name, or **File > Load NEX File...**, which lists `.jns` alongside
every other format. On a Next the save dialog offers `.jns` first and appends
it to a name typed without an extension; on a 48K, 128K, Pentagon or +3 it offers
`.sna` first and appends that instead.

**File > Quick Save Snapshot** (Alt+Shift+F5) and **Quick Load Snapshot**
(Alt+Shift+F9)
need no dialog. Quick Save writes a `.jns` named `jnext-YYYYMMDD-HHMMSS.jns`
(a `-02`, `-03` suffix if the name is taken, so nothing is overwritten) into
`~/.jnext/snapshots/` on every machine type, and names the file in the status
bar. Quick Load restores the *newest* `.jns` in that folder by modification
time, by the same route as **File > Load NEX File...**: a full cold boot, then
the load. An empty or missing folder only reports that there is no snapshot, and
a newest file that cannot be loaded is refused before anything is reset, so the
running session is kept. Older quick saves, and any `.jns` you put in the
folder yourself, are loaded through the ordinary load dialog; nothing is ever
deleted by hand-picking: Quick Save itself keeps only the newest 20 of its own
files (**\--quick-snapshot-keep** *N*|`unlimited`, or **Settings >
Preferences > Paths**, or `[snapshot] quick_keep`) and deletes only files named
exactly `jnext-YYYYMMDD-HHMMSS.jns` (with an optional `-NN`); anything else in
the folder is never touched. The folder is `$JNEXT_CONFIG_DIR/snapshots` when
that variable is set. The SDL-only build has the same two keys, the same folder
and the same option (it reads no configuration file), and reports in the log
instead of the status bar. Plain F5/F9 and Alt+F5/F9 are unchanged.

Loading a `.jns` restores the machine *type* as well as its contents, so one
taken on a Next comes back as a Next whatever **\--machine** says. A snapshot
is only ever taken at a frame boundary: if the debugger has stopped the machine
part-way through a frame, that frame is completed first and the restored
machine is up to one frame past where you stopped.

The attached tape is recorded by its path, with a digest and the deck's
position, and put back at that position on load (a tape that came out of a
`.zip` is re-extracted from the archive). A snapshot never changes a setting: the
tape comes back in the mode that **\--tape-realtime** (or the GUI) and
**\--tape-auto-play** / **\--no-tape-auto-play** give this session, converted
at a block boundary if the snapshot's tape was in the other mode. A tape that is
missing, has changed, or was recorded by a version that did not save the
position is not attached, with a warning.

The file does **not** contain the SD card - a card image is a gigabyte, and it
holds NextZXOS. It records the card's identity instead and checks it on load: a
different card is refused unless **\--snapshot-mode** `force`. The card's
contents are not checked: changing the card between saving and loading is at
your own risk. A second SD card (see **A SECOND SD CARD**) that was mounted when
the file was saved is recorded and checked the same way: a snapshot that had one is
refused when none is mounted, and refused when a different one is unless **\--snapshot-mode**
`force` (which does not override the missing card);
a snapshot saved without one, including every file written by an older **jnext**, is not
refused for one mounted now (a warning). **\--snapshot-mode** `strict` additionally turns the state-model and
ROM-digest warnings into refusals, and **\--snapshot-compression** `off` writes
the members stored rather than deflated, so the file can be read with
`unzip -p`.

It does contain **all of RAM**, which on a Next includes NextZXOS and DivMMC
ROM code paged into it. Those are not redistributable: keep your snapshots
rather than posting them, and hand somebody a `.nex` instead.

# BUG REPORTS

**\--bug-report** *PATH* saves, when jnext exits, one zip file that holds what a
developer needs to understand a failure: the state of the machine and of jnext at
that moment. In the Qt window **File > Save Bug Report...** saves one at once: it opens a
save dialog in your home folder that proposes the name below. It is a snapshot for
reading, not a session to replay. It holds:

- the jnext version, the host operating system and architecture, the frontend and
  the settings in use (the command line and, in the Qt frontend, the settings file),
  with host paths blanked;
- the machine, decoded: the CPU registers, every NextREG with its name, the paging
  map, the ULA, Layer 2, sprite, tilemap and Copper state, the palettes, the
  interrupt, DivMMC and Multiface flags, the audio chips;
- the screenshot, and each of the seven video layers on its own;
- the last 500 lines of the log at info level and above;
- the debugger's state: the run state, breakpoints, the call stack and the recent
  instruction trace;
- the SD card's name, size and SHA-256, never its bytes;
- the program file you loaded, byte for byte (a `.jns` snapshot, which holds the
  machine's whole memory, is the exception: only its SHA-256 and size are recorded). It
  is your file, and you are responsible for what you attach: jnext does not check it
  against anything;
- **memory, only when no firmware has run**: RAM, the CPU's 64 KB view, the sprite
  patterns, a disassembly, the bytes at the stack pointer and the opcode bytes of the
  trace. That is a `--no-sdcard` session whose memory has not seen the boot ROM run
  or a saved state loaded since it was cleared. In every other session, memory
  holds NextZXOS, ROM or DivMMC firmware from the card, so the report carries the
  machine state but none of its memory, and the manifest says why.

jnext never puts NextZXOS, ROM or any other distribution content in a report, and no
byte of the SD card. The program file you loaded is yours, goes in as it is and is your
responsibility. The card is named and identified by its SHA-256 only. The
serial (UART) FIFOs and the SD buffers of the machine state are left out, because
they can carry what the program or NextZXOS exchanged with the card or the
network.

Your user name and every absolute path on your computer are blanked in the file
(`<user>` and `<path>/name`). Look through a report before you attach it anyway.

To read a report, `tools/jnext-bug-view.py` in the jnext sources shows it as a page, or as text with
`--text`; it needs only Python 3, and refuses a file that is not a jnext bug report.

`manifest.json` lists every member of the file and what was left out and why. A file
is a report when it has that manifest, whatever its name.

# HEADLESS MODE, SCREENSHOTS AND RECORDING

**\--headless** runs the emulator with no window and no audio device, as fast
as the host allows: the mode built for scripting and CI.

Notes worth knowing:

- **\--delayed-screenshot-frames** is the deterministic one: it names one exact
  frame, whatever refresh rate the program selects.
  **\--delayed-automatic-exit-frames** is the same idea for the exit bound, and
  wins over **\--delayed-automatic-exit** when both are given.
- *Emulated seconds.* The seconds forms (**\--delayed-screenshot-time**,
  **\--delayed-automatic-exit**, **\--delayed-keypress**, **\--delayed-nmi**)
  count the machine's own time, not the host's: a frame run at 50 Hz counts 1/50
  s and one run at 60 Hz counts 1/60 s, and the delay comes due on the first
  frame at which the total reaches *N*. A machine that stays at 50 Hz therefore
  fires at frame *N* x 50 and one that stays at 60 Hz at frame *N* x 60; a
  program that switches the refresh rate (NextREG 0x05) is timed in its own
  seconds on each side of the switch. The host's speed does not matter.
- **\--rtc** makes boot screenshots reproducible by freezing the clock, so the
  NextZXOS date and time on screen never change between runs.
- A screenshot that was asked for and never taken is an error. If
  **\--delayed-automatic-exit** fires before the capture comes due, or after
  it came due while the machine was paused (a **\--magic-breakpoint**, say:
  nothing resumes a paused headless machine, so no frame is rendered for it),
  jnext logs an error and exits non-zero instead of silently writing nothing
  or writing the picture from before the pause. The same holds for a
  **\--load**, **\--inject** or keypress still to come.
- **\--delayed-screenshot-layers** isolates a layer. An excluded layer is
  composed as if its hardware enable bit were clear, so the remaining ones
  still follow the NR 0x15 priority order and the NR 0x4A fallback colour shows
  through where everything is transparent. Excluding `ula` also removes the
  border: the ULA is what draws it.

- Video recording (**\--record** *FILE*, or **File > Record MPEG4 Video**)
  pipes video and audio to ffmpeg, which must be installed.
- WAV recording (**\--wav-record** *FILE*) captures the mixed audio before the
  host playback device. It works in headless mode and continues through cold
  boots such as sibling-NEX chaining. It cannot be combined with
  **\--silent**, which disables mixer synthesis. If the emulator exits
  abnormally, the file may retain its initial zero-length data header even
  though PCM follows it.
- WAV stems (**\--wav-record-stems** *DIR*) write one file per source (each AY
  chip, the DAC, the beeper, the Pi and the mix) that add up to the mix. They
  follow the same rules as **\--wav-record**: headless works, cold boots
  continue the files, **\--silent** is refused, and SIGTERM or SIGINT finalise
  them. While they record, HALT-idle programs run without the HALT batching.
- DAC tracing (**\--dac-trace** *FILE*) records guest DAC writes before mixing.
  Channels are numbered 0-3 (A-D) and timestamps are CPU T-states.
- The audio write log (**\--audio-log** *FILE*) records the decoded AY, DAC and
  beeper writes with their timestamps, the same rows the debugger's Audio panel
  shows on its Log tab.
- The `.psg` recording (**\--psg-record** *FILE*) saves each AY chip's register
  writes, one file per chip in use, in the layout Fuse writes for its AY log.
- Master and per-subsystem host gains are available under **Settings >
  Preferences > Audio**, or for one run with the **\--audio-gain-** family.
  Beeper, each TurboSound AY chip and the DAC family can be balanced
  independently before the master gain. They change playback and recording
  volume without changing guest-visible audio state and survive cold boots.
  Each explicit CLI value overrides its corresponding saved preference.

## SCREENSHOT FORMATS

The output format is never a separate option: it is whatever the filename's
extension asks for, in **\--delayed-screenshot**, in **File > Save
Screenshot...** and in **File > Quick Screenshot**.

`.png`
:   The composited picture — every enabled layer, the border, the palette, the
    ULA scroll — exactly as the window shows it. The in-memory frame is
    640x256; the file is 640x512, each row written twice, so the pixels are
    square and the image is 4:3 without post-processing.

`.scr`
:   The raw ULA screen memory, the format Spectrum graphics tools read and
    write. No composition, no palette, no scroll: it is a copy of the bytes the
    ULA is fetching, from whichever screen bank is currently being displayed
    (bank 5, or bank 7 while the 128K shadow screen is selected by port 0x7FFD
    bit 3).

    **A `.SCR` can only describe the classic ULA layer.** Layer 2, the tilemap,
    the sprites, the LoRes modes and the Next palettes have no representation in
    it at all, and are not in the file — a Layer 2 game captured to `.scr`
    yields whatever happens to be sitting in the ULA screen memory, which is
    often blank. Capture those as `.png`.

    Size depends on the Timex screen mode in port 0xFF bits 2:0:

    | Mode | Size | Contents |
    |------|------|----------|
    | Standard | 6912 | 6144 pixel bytes, then 768 attribute bytes |
    | Standard, alt file (mode 1) | 6912 | the same, from the alternate screen at 0x6000/0x7800 |
    | Timex hi-colour (modes 2, 3) | 12288 | pixel plane, then the per-cell attribute plane |
    | Timex hi-res (modes 6, 7) | 12288 | the even pixel plane, then the odd one |

    The 12288-byte form is the de-facto Timex `.SCR` layout, and is exactly the
    two 6144-byte planes the ULA fetches. It does not record which of the two
    Timex modes produced it, nor the hi-res ink/paper colour held in port 0xFF
    bits 5:3, because the format has nowhere to put either. Writing 6912 bytes
    in those modes was rejected as worse: the final 768 bytes would be pixel
    data presented as attributes.

    While the shadow screen is selected the ULA is forced back to the standard
    mode by the hardware, so a shadow-screen capture is always 6912 bytes.

# NETWORKING (ESP-01 WiFi)

A real ZX Spectrum Next has a header for an ESP-01 WiFi module on UART 0.
**\--esp** emulates one, so software written for it - NXtel, nextsync, `newt`,
the NextZXOS `.HTTP` and `.zxdb-dl` dot commands - can reach the network through
your host.

Both **TCP and UDP** are emulated. UDP is what `newt` uses to read the time from
an internet SNTP server, so `.newt sntp 0 pool.ntp.org` works; the optional
local-port argument of `AT+CIPSTART="UDP",...` is honoured. UDP "modes" 1 and 2,
which let the far end of a link become whoever last sent a packet, are answered
`ERROR` rather than accepted and ignored. **Server mode** and **multiplexed
connections** are emulated too - see *Letting a program listen* below. TLS and
transparent mode are not.

## It is off by default, and that is the point

An enabled ESP gives the running program an **outbound TCP and UDP pipe out of
the emulator**, and the program can already read the SD-card image, which jnext
opens read-write. A program you did not write is a program you are trusting
when you turn this on. So jnext does not turn it on for you, and there is no
way for a loaded program to turn it on for itself.

When it *is* on, a windowed run shows an **ESP cell in the status bar** for as
long as the run lasts. Its presence means the guest can reach the network; it
names the host of the most recent connection, and shows an allowlist refusal or
a fault in red. There is no way to have the ESP enabled in a window without
that cell being there. **\--headless** builds no window and so has no status
bar: there the `esp01` log is the only report, which is why it reports every
connection at the default level.

An address the policy below refuses is reported as a *failure* rather than an
allowlist refusal, so it is not red; the reason (`address refused by policy: …`)
is in the cell's tooltip and in the log. See
[issue #161](https://github.com/jorgegv/jnext/issues/161).

## What is always refused, whatever you allow

These are refused by address, before any connection is attempted, and
**\--esp-allow** cannot re-enable them:

* loopback - `127.0.0.0/8` and `::1`, i.e. daemons on your own machine;
* link-local - `169.254.0.0/16` and `fe80::/10`;
* cloud metadata endpoints - `169.254.169.254`, `100.100.100.200` and
  `fd00:ec2::254`;
* the unspecified addresses `0.0.0.0/8` and `::`;
* multicast and reserved space - `224.0.0.0/4`, `240.0.0.0/4`,
  `255.255.255.255`, `ff00::/8`.

IPv6 spellings of an IPv4 address (`::ffff:127.0.0.1`, NAT64, 6to4) are
resolved to what they actually reach and judged on that, so they cannot be used
to get around the list.

**Your own LAN is reachable.** RFC1918 addresses (`192.168.x.x`, `10.x.x.x`,
`172.16-31.x.x`), carrier-grade NAT (`100.64.0.0/10`) and IPv6 unique-local
(`fc00::/7`) are deliberately *not* refused - reaching a machine on your own
network is the main thing people use this for, and nextsync in particular
connects to a PC on the LAN. The two cloud-metadata addresses above sit inside
those ranges and are carved back out.

## Narrowing it further

**\--esp-allow** restricts the program to hosts you name:

    jnext --esp --esp-allow nx.nxtel.org NXtel.nex

Every connection to anything else is refused, the program gets the same `ERROR`
it would get from a real module that could not connect, and the refusal is
reported in the log and in the status bar.

## Making it permanent

**Settings > Preferences > Network** holds the same two settings for people who
run one of these programs regularly: an enable checkbox and the allowed-host
list, one host per line (a comma separates too, matching the config file's own
format). Blank entries and repeats are dropped, and the list is greyed out
while the module is off, because it restricts nothing on its own.

The module is built when a machine starts, so this page cannot switch it on or
off underneath a running program: the change takes effect on the next hard
reset (**F1**, or **Machine > Power Reset**) and on the next launch. The CLI still
wins for a single run in both directions - **\--esp** over a saved *off*, and
**\--no-esp** over a saved *on*.

## Letting a program listen (server mode)

A program can also **accept** incoming connections: `AT+CIPMUX=1` followed by
`AT+CIPSERVER=1,<port>` makes jnext listen on that port and hand the program
each connection that arrives. This is what a debug stub running on the emulated
Next needs, because a debugger on your PC dials out and never listens.

Everything above is about *outbound* connections - which addresses the program
may dial. A listening socket points the other way: it exposes the **program** to
whatever can reach the port, and the allowlist has nothing to say about it,
because the program does not choose who connects. So the boundary is the bind
address instead:

* by default the port is bound to `127.0.0.1`, so only this machine can reach
  it - which is all the intended use needs;
* **\--esp-listen-address** *ADDR* widens that, and `0.0.0.0` means any machine
  that can reach yours can talk to the running program. There is no
  authentication of any kind in front of it;
* nothing listens until the program asks. There is no way to open a port from
  the command line, and no port survives a Power Reset;
* if the port cannot be bound - already in use, or an address that is not
  yours - the program is told `ERROR`. jnext never falls back to a different
  port or a wider address.

Up to four programs can be connected at once, numbered from 1; number 0 stays
reserved for the program's own outbound connection, so opening a server never
costs it the ability to dial out. `AT+CIPMUX` cannot be changed while anything
is connected.

A connected client that sends nothing for **180 seconds** is hung up on and the
program is told `<id>,CLOSED`, which is what a real ESP-01 does and the usual
reason an idle debug session dies. `AT+CIPSTO=<seconds>` changes the window
(0-7200, and **0 means never**), and — again like the real module — is refused
with `ERROR` unless a server is already running, so it belongs *after*
`AT+CIPSERVER=1,<port>`; the query form `AT+CIPSTO?` answers either way. jnext
forgets the setting on a reset.

Still not emulated: TLS and transparent mode
([issue #154](https://github.com/jorgegv/jnext/issues/154)).

## Making the WiFi go away

A program that has to notice the Next dropping off the network cannot be tested
against a module that is never off it. Two options schedule an outage, in
frames, so a headless run can execute that code path:

    jnext --headless --esp stub.nex \
        --esp-delayed-disassociate-frames 300 \
        --esp-delayed-associate-frames 900 \
        --delayed-automatic-exit-frames 1200

At frame 300 the module loses its association and `AT+CIFSR` starts reporting
`+CIFSR:STAIP,"0.0.0.0"`; at frame 900 it is back, with the same address it had
before - a short outage does not normally move it, whether the address is static
or a DHCP lease that outlived the gap. Both edges are reported in the log.

**The address report is all that changes.** Connections already open keep
running, new ones still open, `AT+CWJAP?` and `AT+CIPSTA?` still answer as they
did, and no `WIFI DISCONNECT` or `WIFI CONNECTED` is sent. That is a deliberate
boundary, not an oversight: what a real module does with traffic and with
unsolicited messages while its access point is down has not been measured on
hardware, and a bench built on an invented answer would only be measuring the
invention. What *is* measured is that a listening program survived a five-minute
outage on a real Next and never noticed
([issue #246](https://github.com/jorgegv/jnext/issues/246)).

## Nothing about your host leaks into the program

The module reports a fixed synthetic identity: SSID `JNextWifiHost`, a
`02:00:00:...` MAC and BSSID, and a `192.168.1.50` station address. None of it
is read from your machine, so a program cannot learn your real SSID, MAC or
local addresses by asking. The emulated module is not a radio and never scans.

**\--esp-ip-address** changes the station address it claims, and does not weaken
that: the value is one you typed, never one jnext discovered, and it is still
only a string in a reply. Nothing listens on it and no traffic goes near it.

# LOGGING

**\--log-level** *SPEC* sets per-subsystem log levels. A bare level sets every
subsystem; a *name*`=`*level* pair sets one. They can be mixed, and are applied
left to right:

    jnext --log-level warn                      # everything at warn
    jnext --log-level cpu=trace                 # just the CPU
    jnext --log-level warn,emulator=debug       # quiet, except the emulator

Levels are `trace`, `debug`, `info` (default), `warn`, `err`, `critical` and
`off`; `warning`, `error`, `fatal` and `none` are accepted as aliases.

Subsystems are `cpu`, `memory`, `ula`, `video`, `audio`, `port`, `nextreg`,
`dma`, `copper`, `uart`, `input`, `platform`, `emulator`, `sdcard`, `divmmc`,
`spi`, `ctc`, `i2c`, `multiface`, `esp01`, `esxdos` and `debugger`.

`debugger` is the debugger backend. At the default `info` level it reports every
change a debugger client makes to the machine - one `MUTATE` line per register,
memory, NextREG, port, palette or sprite write, naming the old value, the new one
and the client - plus each client attaching and detaching. Set it to `warn` to
silence that running commentary without silencing anything else.

`esp01` is the emulated ESP-01 WiFi module (**\--esp**). At the default `info`
level it reports only the connections opened, refused, failed and closed - the
events **NETWORKING** describes as never silent. A refusal or a failure is a
warning, so it survives **\--log-level** *warn* too. `debug` adds every AT command
received and every response emitted, and `trace` adds the raw byte traffic in
both directions. Turning it down to `off` silences the connection reports too,
which is a deliberate choice on the user's part and not something jnext undoes.

`esxdos` is a tracer rather
than a subsystem: at `trace` level every `RST $08` esxdos call is logged with
its arguments and its result, including calls jnext does not service. It does
not need **\--esxdos-stub** — the interesting case is a NextZXOS-expecting
program running without the stub, where those calls are otherwise invisible.

## Sending the log to a file

**\--log-file** *FILE* writes the log to *FILE* instead of the console:

    jnext game.nex --log-level nextreg=debug --log-file trace.log

It replaces the console rather than adding to it, so the trace is in one place
and nothing is doubled. *FILE* is truncated at the start of every run - a run's
log describes that run, and nothing in it belongs to a previous one. If it
cannot be opened, jnext says so and exits non-zero; it does not fall back to the
console, because a log you asked to be redirected and silently was not is a
trace you lose without noticing. A missing containing directory is created for
you, so `--log-file logs/run.log` works in a fresh checkout.

This exists so that capturing a log does not require getting shell redirection
right. jnext logs to standard error, so `> file` captures nothing and `2> file`
is needed - and PowerShell re-encodes redirected output from a native program.
**\--log-file** sidesteps both: the program writes where it was told to write.

A log file carries no colour, whatever `NO_COLOR` says.

# ENVIRONMENT

`NO_COLOR`
:   When set to any non-empty value, jnext emits no ANSI colour escapes on its
    log output, whether or not the console is a terminal - the convention
    described at <https://no-color.org/>. An empty value counts as unset, which
    is how a script re-enables colour for one child process. The value itself is
    never interpreted: `NO_COLOR=0` suppresses colour just as `NO_COLOR=1` does,
    because both are non-empty. It has no effect on **\--log-file** output,
    which is never coloured in the first place.

`JNEXT_COLOR_SCHEME`
:   **dark** or **light** forces the GUI's colour scheme (Qt 6 builds); **system**, empty or
    unset follows the desktop (see THE GUI). Any other value is ignored with a
    warning. Meant for screenshots and tests, where there is no desktop to
    ask (an Xvfb display), and for a desktop that reports the wrong thing. It
    applies to the windows only, never to the emulated screen.

# PROFILING

**\--profile** enables the CPU T-state profiler: one histogram entry per
executed instruction, written to **\--profile-output** (default `profile.dat`)
on exit. Join it against a z88dk `.map` file to get a per-function heatmap:

    jnext --headless game.nex --profile --profile-output game.dat \
        --delayed-automatic-exit 20
    tools/get-function-heatmap.pl -m game.map game.dat

# THE GUI

The Qt6 build gives a native window with menus, a toolbar and a status bar
(FPS, CPU speed, emulator speed, tape status, machine type, and a Paused
indicator while the machine is paused), Hi-DPI
pixel-perfect rendering at integer scale, and a CRT effect (scanlines, or an
optional GPU soft-scanlines, TV or PC-monitor look).

Menu shortcuts use **Alt** (Command on macOS), never Ctrl + a letter: Ctrl is the
Spectrum's Symbol Shift, so a Ctrl shortcut would eat a key the guest needs; the
only Ctrl chords are Ctrl+F5 and Ctrl+F6 (video recording), and on macOS
Control+Command+F (full screen) (see **KEYBOARD MAPPING** below).

**File**
:   Load a program (Alt+O - NEX/JNS/SNA/SZX/Z80/TAP/TZX/WAV/RZX; a .pok opens POK Trainers instead, without a reset), Recent Files (the last ten loaded, newest first) and Reload Last File (Alt+L; also in the debugger window's File menu), Insert SD Card
    Image / Eject SD Card and the same for the second SD card (see **CHANGING THE CARD WHILE RUNNING**), Record MPEG4 Video (Ctrl+F5) / Stop (Ctrl+F6), Play RZX / Record RZX / Stop
    RZX, Save Screenshot (Alt+S), Quick Screenshot (Alt+K), Save Snapshot
    (Alt+Shift+S), Quick Save Snapshot (Alt+Shift+F5), Quick Load Snapshot (Alt+Shift+F9)
    (see **JNEXT SNAPSHOTS** above), Save Bug Report... (see **BUG REPORTS**), Quit (Alt+Q or Alt+F4).

    **Save Screenshot...** asks for a name; the extension chooses the format
    (**SCREENSHOT FORMATS** above). **Quick Screenshot** asks nothing: it
    writes `jnext-YYYYMMDD-HHMMSS.png` (or `.scr`) into the quick-screenshot
    directory immediately and names the file it wrote in the status bar, so a
    sequence of captures costs one keypress each. The directory and the format
    are set under **Settings > Preferences > Paths**; the directory defaults to
    `~/.jnext/screenshots` and is created on first use. A name already in use
    gains a `-02`, `-03` suffix, so two captures in the same second cannot
    overwrite each other.

**Machine**
:   Power Reset (Alt+R - power off/on cold boot, full boot chain), Soft Reset
    (F4 - the front-panel reset button: back to NextZXOS without re-running
    the boot chain; does nothing while the firmware still holds config mode.
    After a direct **\--load** it resets into 48K BASIC and discards the
    loaded program, because that boot never ran the firmware and so has no
    NextZXOS to return to - use Power Reset there), Pause (Alt+U or the Pause key - freeze or
    resume the machine where it is, without opening the debugger; the sound
    goes quiet, a **Paused** cell appears in the status bar, and a Power Reset
    or a load made while paused stays paused),
    Machine Type (48K / 128K / +3 / Pentagon 128K / Next), CPU Speed (3.5 / 7 /
    14 / 28 MHz - the Next's own clock), Emulator Speed (0.5x / 1x / 2x / 4x /
    custom % - the host-side throttle), POK Trainers... (open a .pok trainer
    file and apply or remove its trainers - infinite lives and the like - on the
    running machine; a snapshot saved afterwards keeps them).

**Input**
:   Joy 1 Source (port 0x1F) and Joy 2 Source (port 0x37), each selecting what
    drives that connector: Automatic (the first free controller), any
    connected controller by name, Cursor Keys + Space, or None; Capture Mouse, which confines the host pointer so
    the Kempston mouse can move freely (by default Ctrl+Alt releases it, Alt+Cmd on macOS; see **\--mouse-release**).

**Tape**
:   Open Tape File (Alt+T), Eject Tape, Rewind, Fast Load (toggle), Auto Play/Stop
    (toggle); Start Saving...,
    which saves to a TAP, TZX or WAV file as **\--tape-save** does, and Stop
    Saving.

**Debug**
:   Magic Breakpoint (toggle), Debugger (Alt+D - the same entry as in
    **View**).

**View**
:   Scale 1x / 2x / 3x, Fullscreen (F11, or Control+Command+F (⌃⌘F) on macOS; letterboxed), the CRT effect (CRT Scanlines,
    CRT Soft Scanlines, CRT TV, CRT PC Monitor; one at a time, none checked = off; the last
    three only in builds with GPU support), Debugger (Alt+D).

**Settings**
:   Preferences... (Alt+P), which opens the settings dialog (Startup, Input,
    Audio, Network, NextPi and Paths tabs, plus Debugger and Debugger Keys in a
    build with the debugger compiled in). Its values are saved to the configuration
    file; command-line options always take precedence over them.

**Help**
:   About.

The toolbar has Power Reset, Soft Reset, Pause, Load, Screenshot, an NMI button
(Multiface NMI) and, in a debugger-enabled build, a Debug button (a magnifier over a chip) that opens the
debugger. The icons are monochrome, drawn in the window's text colour, so they
follow a light or dark theme; hovering a button shows its name.

The windows follow the desktop's light or dark preference, including a switch
made while jnext runs. On Linux jnext reads it from the XDG desktop portal
(the `org.freedesktop.appearance` `color-scheme` setting that GNOME, KDE and
others publish), because Qt's own detection misses it under GNOME; with no
portal it keeps Qt's own choice. On Windows and macOS Qt follows the system
setting itself. The Qt 5 builds (the legacy Windows packages) do not follow it.
**JNEXT_COLOR_SCHEME** overrides all of this (see ENVIRONMENT).

CPU Speed and Emulator Speed are different things: the first changes the clock
the emulated Z80 runs at, which is a real Next feature; the second changes how
fast the emulator runs relative to real time.

# KEYBOARD MAPPING

| PC key                | Spectrum key                       |
|-----------------------|------------------------------------|
| Letter/number keys    | The corresponding key              |
| Shift (left/right)    | Caps Shift                         |
| Ctrl (left/right)     | Symbol Shift                       |
| Backspace             | Delete (Caps Shift + 0)            |
| Arrow keys            | Cursor keys (Caps Shift + 5/6/7/8) |
| Enter                 | Enter                              |
| Space                 | Space                              |
| Esc                   | Break (Caps Shift + Space)         |
| Tab                   | Extend Mode (Caps Shift + Symbol Shift) |
| Caps Lock             | Caps Lock (Caps Shift + 2)         |
| Key left of `1`       | True Video (Caps Shift + 3)        |
| `\`                   | Inverse Video (Caps Shift + 4)     |
| Alt + key left of `1` | Inverse Video (Caps Shift + 4)     |
| Alt + E               | Edit (Caps Shift + 1)              |
| Alt + G               | Graph (Caps Shift + 9)             |
| Alt + C               | Caps Lock (Caps Shift + 2)         |
| Alt + 1 ... Alt + 8   | nothing - script host keys 1-8 (see **SCRIPTING**) |
| `'`                   | `"` (Symbol Shift + P)             |
| `;`                   | `;` (Symbol Shift + O)             |
| `.`                   | `.` (Symbol Shift + M)             |
| `,`                   | `,` (Symbol Shift + N)             |
| `/`                   | `/` (Symbol Shift + V)             |
| `-`                   | `-` (Symbol Shift + J)             |
| `=`                   | `=` (Symbol Shift + L)             |

Shift is Caps Shift and Ctrl is Symbol Shift, matching a real Spectrum Next
with a PC keyboard attached. Earlier releases had the two swapped. On a Mac, Symbol Shift is the Control
key too; Command is not a Spectrum key (see below).

Holding Shift while pressing a digit or one of the symbol keys above reaches
the guest as Caps Shift plus that key: Shift+1 is Edit, Shift+0 is Delete,
Shift+5 to Shift+8 are the four cursor keys, Shift+9 is Graph, and Shift+`;`
is Caps Shift + Symbol Shift + O. jnext keys off the physical key rather than
the character your host layout produces, the same way the Next's own PS/2
keymap treats the shift keys as an independent overlay. Earlier releases
dropped all nineteen of those keystrokes before the Spectrum saw them.

Esc is the Spectrum's Break key, not a fullscreen shortcut. Fullscreen is
toggled with F11 only (it used to also be exited with Esc). On macOS, which takes F11 for
Show Desktop, Control+Command+F (⌃⌘F) and the green title-bar button also toggle it.

The function keys are host controls, not Spectrum keys. F1 is Power Reset (the
same as Alt+R), F4 is Soft Reset, F9 fires the Multiface NMI, F10 fires the
DivMMC NMI, F2 cycles the window scale and F11 toggles fullscreen. With Alt
held F1 and F4 are not resets: Alt+F4 quits jnext, the same as File > Quit,
whatever else is held and whether or not the mouse is captured, and Alt+F1
does nothing. The SDL-only build does the same. With the
debugger open F9 belongs to the debugger (Pause / Break) instead. With Alt and
Shift held, F5 and F9 are the quick snapshot keys (Alt+Shift+F5 saves,
Alt+Shift+F9 loads) and reach neither the Multiface nor the guest; Alt+F5 and
Alt+F9 on their own are not claimed. Shift is the guest's Caps Shift, so it is
held down for the guest while you press the chord.

The Pause key (Pause/Break) pauses and resumes the machine in the GUI, the same
as Machine > Pause; keyboards without one, such as most Mac keyboards, use
Alt+U (Command+U on macOS). On a real Next that key only resets the PS/2 keyboard interface, so no
Spectrum key is lost. The SDL-only build has no pause.

Ctrl is left alone for the guest. No Ctrl+letter chord of any kind is a jnext
shortcut, so the Symbol Shift sequences NextBASIC needs constantly - Ctrl+O
`;`, Ctrl+D `STEP`, Ctrl+R `<`, Ctrl+T `>`, Ctrl+Q `<=`, Ctrl+S `|` - all reach
the program you are running, and so does Ctrl+Shift+S (Caps Shift + Symbol
Shift + S), which was the last one to move. Earlier releases bound those to
menu commands and the program never saw them. The only Ctrl chords jnext still
takes are Ctrl+F5 and Ctrl+F6 (start and stop video recording), and on macOS
Control+Command+F (full screen, a Command chord); function keys have no
Spectrum meaning to lose.

On macOS the two roles move one key over. The host shortcuts are Command
instead of Alt: every Alt+X below is Command+X (⌘X), Quit is Command+Q (Command+F4, the
Alt+F4 equivalent, quits too), Preferences is Command+, and the script host keys are Command+1
to Command+8; the menus show the Mac form. The Spectrum's Symbol Shift is the
physical Control key, as on a PC keyboard, Command is not a Spectrum key, and
Option stays the guest's Alt (Option+E/G/C are EDIT, GRAPH, CAPS LOCK). A
Command chord that jnext does not use never reaches the program. The menu-bar
mnemonics (Alt+F, ...) do not exist on macOS, and the recording shortcuts
Ctrl+F5 and Ctrl+F6 stay on the physical Control key there (Command+F5 is
VoiceOver's). The SDL-only build on
macOS follows the same rule for Command+1 to Command+8, Command+Shift+F5 and
Command+Shift+F9 and Command+F4 (quit); Option+F1 and
Option+F4 do nothing in either frontend.

Alt is the opposite: it is a host modifier, never a Spectrum key. jnext claims
Alt + Q/O/L/S/K/R/T/D/P/U, Alt+F4, Alt+Shift+S, Alt+Shift+F5 and Alt+Shift+F9 (menu shortcuts), Alt + F/M/I/A/B/V/N/H
(menu bar) and Alt + 1 to Alt + 8 (the script host keys), which leaves the
guest only Alt + E/G/C (EDIT, GRAPH, CAPS LOCK) and Alt + the key left of `1`
(INV VIDEO). Alt + 1 to Alt + 8 no longer type their digit into the program
(earlier releases let them through as plain 1-8); Alt + 9 and Alt + 0 still do. Real Next hardware instead maps Left
Alt to EXTEND MODE and Right Alt to GRAPH; jnext deliberately does not,
following the FUSE/ZEsarUX convention that puts EXTEND MODE on Tab.

USB gamepads are picked up automatically (hot-plug) and mapped to the Next's
two joystick ports, the first two controllers to Joy 1 and Joy 2 in the order
they connect; the joystick mode (Kempston / Sinclair / Cursor / MD) follows
NextREG 0x05, as on real hardware.

To choose which controller drives which port, use **Input > Joy 1 Source** and
**Joy 2 Source** (or **Settings > Preferences > Input**): each lists Automatic,
every connected controller by name (identical models appear as `name` and
`name #2`), Cursor Keys + Space and None. The choice applies at once and is
remembered by the controller's identity, so it survives unplugging and
restarting; a controller that is not connected is shown as `name (not
connected)` and the port uses the first free controller meanwhile, which jnext
logs. The same controller cannot drive both ports. With two identical pads the
numbering holds only while they are connected in the same order. Unplugging a
controller releases any button it held.

Either joystick port can instead be driven by the host cursor keys (arrows for
direction, Space for fire), which is useful for the many games that read the
Kempston joystick when you have no gamepad. Choose the source per connector in
the **Input** menu, in **Settings > Preferences > Input**, or with
**\--joy1-source** `keys` / **\--joy2-source** `keys`; the choice is remembered
across sessions. Only one connector may use the cursor keys at a time, and
while it does, the arrows and Space stop acting as ZX keys.

# THE DEBUGGER

The debugger opens in its own window (**Debug > Debugger** or
**View > Debugger** - the same entry in both menus - or Alt+D). It is
driven from the Qt6 UI, so it needs a GUI build (`make gui-release` or
`make gui-debug`); the SDL-only build has no way to open it.

- **CPU registers** - all Z80/Z80N registers, flags (S/Z/H/P/V/N/C), halt
  state, interrupt mode, active ULA screen; while paused, every register, flag,
  IFF and IM can be edited in place (double-click a value, click a flag), and
  **T-states** shows the time since the last stop (**Accumulate** adds stops up)
- **MMU panel** - the Next 8-slot MMU table (page numbers and type) and the
  128K bank mappings
- **Disassembly** - Z80 + Z80N, PC highlight, breakpoint gutter, follow-PC,
  run-to-cursor, symbol names from MAP files. Lines can be selected (drag,
  Shift+click, Shift plus the navigation keys, Ctrl+A) and copied with Ctrl+C
  as assembly only - no address column, no opcode bytes - or with the
  right-click menu's **Copy with Addresses** to keep both
- **Memory hex editor** - full 64K, hex and ASCII, inline editing, page/bank
  selector; find (hex with wildcards and a mask, or text), and save, load or
  fill a block, from its right-click menu
- **Stack** - SP-relative word view, SP row highlighted
- **Call stack** - CALL/RST/INT/RET tracking, with symbol resolution; a click on
  a row chooses the C frame the Variables tab shows
- **Variables** - with a z88dk `-debug` source map, the C variables of a call
  frame (chosen in a selector or in the Call stack) and the globals and statics,
  as a tree that opens onto struct members, array elements and what a pointer
  points at; the type, the value (or why there is none) and where it is; paused
  only. Double-click a value to edit it; right-click a row to watch it
- **Source** - with a source map (a z88dk `-debug` map or a sjasmplus SLD), the
  source line the CPU is on, or any file the map names (a File list, also
  **Debug > Source > Open Source File...**), with line numbers, a breakpoint
  column, a current-line arrow, coverage tint and the Disassembly's right-click
  menu; source Step Into, Over, Out, Back and Reverse, also in **Debug > Source**
- **Breakpoints** - execute, read, write, read/write and I/O watchpoints and
  Interrupt/NMI breakpoints, in one panel, with optional address ranges, I/O port
  masks, conditions (debugger-script expressions), hit counts and count-only
  breakpoints; **Breakpoints > Save as Script** / **Load from Script** keep them
  in a `.jds` file, and **Debug > Run to Next Interrupt** stops once at the next
  maskable interrupt.
  An I/O watchpoint address of **00**-**FF** matches any port with that low
  byte (**FE** catches every ULA access, whatever the high byte holds);
  **0100** and above matches that exact 16-bit port (**243B**, not **253B**).
  They are armed only while the debugger window is open; start jnext with
  **\--persistent-breakpoints** to keep them armed with it closed, in which
  case a hit reopens the window at the breakpoint. Each row has an **On**
  checkbox that suspends that one without deleting it, and the panel has a
  **Breakpoints enabled** master switch that suspends every breakpoint and
  watchpoint at once and restores each one's own state when ticked back; a
  suspended execute breakpoint is drawn in the gutter as a hollow ring rather
  than a filled dot
- **Watch expressions** - byte, word or long at arbitrary addresses, with
  custom labels; debugger-script expressions (registers, `mem16[@ptr]`,
  `phys[]`) evaluated live; and C variables pinned from the Variables tab (a local follows
  its function and says so when it is out of scope; read at each stop)
- **Video panels** - All layers (the real composite, through the live
  compositor), ULA (primary and shadow), Layer 2 (active and shadow), Sprites,
  Tilemap, Background (the NR 0x4A fallback colour); per-scanline view up to the
  current raster position, checkerboard for transparent pixels. The header
  gives the raster position in all four coordinate systems at once (raw
  `hc`/`vc`, the ULA's `hc_ula`/`vc_ula`, the Copper `cvc` that NR 0x1E/0x1F
  report, and the pixel being generated), whether the beam is in paper, border
  or blanking, and whether the ULA is fetching a bitmap byte, an attribute byte
  or nothing
- **Graphics** - the sprite patterns, the tile definitions and all eight
  palettes (RGB333 values and priority bit)
- **Symbols** - every known label, filterable; double-click one to show it in
  the Disassembly (also **Go > Symbols**)
- **Sprite viewer** - all 128 hardware sprites with their attribute table
- **Copper disassembly** - decoded WAIT/MOVE, with the current Copper PC
- **NextREG panel** - all 256 registers, named, editable inline
- **Audio panel** - AY register state for the 3 TurboSound chips, per-source
  mute
- **Trace log** - circular instruction trace buffer, exportable to a file. Off
  by default; enable with **\--trace**, the debugger's Enable Trace menu item,
  or implicitly by enabling rewind, which needs it for Step Back
- **Symbol table** - Z88DK MAP files (banked symbols per page), sjasmplus `--sym` / `--exp` files and
  sjasmplus SLD labels (per page); symbols appear inline in the disassembly and in watches
- **Backwards execution (rewind)** - frame-snapshot ring buffer, with Step
  Back, Frame Back and a rewind slider. Opt-in, because rewind takes a
  full-machine snapshot every frame: start jnext with
  **\--rewind-buffer-size** *N* (for example 500), or toggle it live from the
  debugger's Debug > Rewind > Enable Rewind menu, with no restart needed - that
  allocates 500 frames, or the last size set via Rewind Buffer Size...
  Unchecking the toggle pauses snapshotting but keeps the recorded history; set
  the buffer size to 0 to free the memory. The setting survives program loads
  and resets; the recorded history does not

| Key      | Action              |
|----------|---------------------|
| F2       | Enable Trace        |
| F3       | Export Trace        |
| F5       | Run / Continue      |
| F6       | Single Step (into)  |
| F7       | Step Over           |
| F8       | Step Out            |
| F9       | Pause / Break       |
| Ctrl+F6  | Source Step Into    |
| Ctrl+F7  | Source Step Over    |
| Ctrl+F8  | Source Step Out     |
| Shift+F6 | Frame Back          |
| Shift+F7 | Step Back           |
| F12      | Follow              |
| Alt+Left | Back                |
| Alt+Right | Forward            |

Every panel except the Disassembly is a dock: drag it to another edge, onto
another panel to make a tab, or out of the window to float it, and close it from
its title bar. **Window > Panels** brings a closed panel back and **Window >
Reset Layout** restores the default arrangement. The arrangement is remembered
(`layout` in the `[debugger]` section, see **FILES**).

Those are defaults. All twenty-one debugger commands - the fifteen above plus Run to
Cursor, Run to End of Frame, Run to End of Scan Line, Run to Next Interrupt, Source Step Back and Source
Reverse Continue, which ship unbound -
can be rebound under **Settings > Preferences > Debugger Keys** in the emulator
window, or by hand in the `[debugger_keys]` section of the configuration file
(`step_over=F10`). Only the commands you change are written there, so a default
that changes in a later release still reaches you.

The window may be resized below what the panels need: the panel area then
scrolls rather than clipping, and the control toolbar along the bottom stays
visible, with any buttons that no longer fit reachable from its overflow menu.
Its size is remembered between sessions and clamped to the screen it reopens
on.

# REMOTE DEBUGGING (DEZOG)

With **\--dzrp-port** *PORT*, jnext serves the DeZog Remote Protocol (DZRP,
version 2.2.0), the protocol DeZog - the Z80 debugger for Visual Studio Code -
uses to drive CSpect and real hardware. DeZog then debugs the program running
in jnext from your editor: source-level breakpoints, stepping, registers,
memory, the MMU slots, NextREGs and sprites.

In DeZog's `launch.json`, point a `cspect` remote at jnext's port:

```
"remoteType": "cspect",
"cspect": { "hostname": "localhost", "port": 11000 }
```

and start jnext with the same port, for example
`jnext --dzrp-port 11000 --load mygame.nex`. DeZog releases that have a
`dzrp` remote type can use it instead, with the same port.

- **Connecting pauses the machine**, and DeZog shows where it stopped.
  Disconnecting resumes it if the pause was DeZog's own and no other client -
  jnext's open debugger window, another remote debugger - is attached; if one
  is, the machine stays paused and the pause becomes that client's. A pause
  made from jnext's debugger window stays.
- **Breakpoints and watchpoints set from DeZog belong to that session**:
  they are separate from the ones set in jnext's debugger window, they stop
  the machine whether that window is open or not, and they are removed when
  DeZog disconnects - a DeZog that crashes cannot leave the machine stopped
  on them.
- **Either side may pause or resume.** jnext's debugger window and DeZog
  drive the same machine and whoever acted last wins. When something other
  than DeZog stops the machine while DeZog has it running - the debugger
  window, a magic breakpoint - DeZog is told it stopped, and why.
- **Saving and restoring the state** (DeZog's `-state save` / `-state
  restore`) keeps the state inside jnext for this session only; DeZog's
  file holds a reference to it. A state can only be saved at a frame
  boundary, so after a breakpoint hit the save is refused - pause the
  machine by hand first.
- **Not served**: the commands a hardware stub needs to patch breakpoints
  into memory (`CMD_SET_BREAKPOINTS`, `CMD_RESTORE_MEM`) and `CMD_EXEC_ASM`.
  They are answered empty, with a warning in the log, so the client never
  waits.

In **\--headless** mode a client that holds the machine paused holds its
frames too: nothing is emulated and jnext waits for the next command without
using the CPU. The automatic exit (**\--delayed-automatic-exit** and its
**-frames** form) is still a hard bound: while a client holds the machine
paused it is counted in wall time: the **-frames** form one frame per 20 ms, the
seconds form real seconds.

The server listens on `127.0.0.1` unless **\--debug-listen-address** says
otherwise. DZRP has no authentication: anyone who can reach the port controls
the machine and can read and write all of its memory.

# REMOTE DEBUGGING (Z88DK-GDB)

With **\--gdb-port** *PORT*, jnext serves the GDB remote serial protocol to
`z88dk-gdb`, z88dk's command-line debugger - a distribution's own gdb has no
Z80 support. Build the program with a map file (`-m` on the `zcc` line) and
connect to jnext's port:

```
jnext --gdb-port 3333 --load mygame.nex
z88dk-gdb -h 127.0.0.1 -p 3333 -x mygame.map
```

- **Connecting stops the machine**, and `quit` resumes it if the stop was
  the client's own and no other client (jnext's open debugger window, another
  remote debugger) is attached - if one is, the machine stays paused and the
  pause becomes that client's; a pause made from jnext's debugger window
  stays.
- **Served**: registers (AF BC DE HL, the alternate set, IX IY SP PC, and a
  T-state clock the client's profiler reads), memory as the Z80 sees it now
  (the 64 KB CPU view - a write that touches ROM is refused), breakpoints,
  `stepi`, `nexti`, `cont`, Ctrl-C and `quit`. Breakpoints belong to the
  session: they stop the machine whether jnext's debugger window is open or
  not, and they are removed when the client disconnects.
- **I, R, IFF1/IFF2 and IM are not in the register set** on purpose: the
  client's `set` writes the whole set at once, and zeroes every register it
  does not know.
  Read and write them with `monitor regs` and `monitor set`.
- **`monitor` commands** (z88dk-gdb versions that have `monitor`) reach the
  Next-specific state: `mmu`, `nextreg`, `page` (a physical 8 KB page),
  `in`/`out` (a real port access, which can change the device's state),
  `sym`, `src` (the source line and memory of an address), `time`,
  `reset soft`/`reset hard` and `bp`. `monitor help` lists them.
- **C variables** of a program built with a z88dk `-debug` map: `monitor
  locals` [*FRAME*] (the parameters and locals of a C frame, 0 is where the CPU
  is), `monitor globals` (every global, file static and static local, with the
  memory it lives in; two block statics of one name in a function share their
  path, are marked `(ambiguous path)` and are refused by `monitor print`) and `monitor print` [*FRAME*] *PATH* (one value:
  `p->x`, `*p`, `g_matrix[1][2]`, `fn::x`, `file.c::x`). They read
  the right module's file statics, z88dk's 48-bit floats, array counts and
  paged globals, which z88dk-gdb's own `info locals` / `print` can get wrong.
  A refusal (no map, an unknown name, a variable the compiler optimised out)
  prints its reason. `monitor set var` [*FRAME*] *PATH* `=` *VALUE* writes one
  (an integer, a `'c'` character, a float for a float variable, an address for a
  pointer; a value out of the type's range is refused, never truncated); a
  refusal prints `refused:` and its reason and the reply is `E01`. Only
  z88dk-gdb versions that have `monitor` can send them.
- **Either side may pause or resume**, as with DeZog: the client is told the
  machine stopped when something else stops it while the client has it
  running.

In **\--headless** mode a client holding the machine stopped holds its frames
too, exactly as described for DeZog above. The GDB protocol has no
authentication either.

# SCRIPTING

A debugger script (`.jds`, loaded with **\--script**) is a list of rules,
each an event and the actions to run when it happens:

```
# palette.jds - CI assertion: the exit status is the verdict
on execute @palette_init_done once do
    assert mem[0x9000] == 0xAA "sentinel missing in palette buffer"
    log "PASS palette init"
    exit 0
end
on write 0x4000..0x5AFF when VALUE == 0xB7 do
    stop "forbidden value ${VALUE:x2} written by ${PC:x4}"
end
```

```
jnext --headless --map game.map --script palette.jds --load game.nex \
      --delayed-automatic-exit-frames 600
```

The events are an instruction about to execute, a memory read or write, a
port read or write, a NextREG write, a frame, a scanline, a master-clock
cycle, an accepted interrupt, an NMI, a reset, a host key, a Copper
`MOVE`/`WAIT`/`HALT`, a DMA transfer's start, bytes and end, and any pause of
the machine. A rule's `when` condition is checked by the debugger itself, so a
rule body runs only for the accesses that match. A script observes the machine
without disturbing it; it changes the machine only through `set` and `out`,
and each such write is logged as a `MUTATE` line. Script output goes to the log
as `[jds F:`*frame* `C:`*cycle*`]` lines.

**C variables.** With a z88dk `-debug` map loaded (the source map carries the
program's C variable records), `cvar(`*PATH*`)` reads a C variable of the frame
the CPU is in, as a 32-bit integer: `on execute vars.c:41 when cvar(t) > 100`.
*PATH* is `name`, `function::name`, `file.c::name` or `file.c::function::name`,
optionally with a leading `*` and then `.member`, `->member` and `[index]`
steps. An integer or a pointer (its address) reads as itself and an unsigned
32-bit value keeps its bit pattern; a struct, an array, a float and a value
wider than 32 bits are refused, naming the type. The path is checked when the
script loads. Every other refusal (no C variables, an unknown name, a variable
out of scope or optimised out, a null pointer, an index past the end) is a
run-time error, never a zero: in a `when` it disables the rule and the run
exits 1, so put the rule on a line where the variable is in scope.
`set cvar(`*PATH*`) =` *value* writes one (frame 0): the value is a 32-bit
integer, a 4-byte integer takes its bit pattern, a smaller one must fit its
type (never truncated), and a float, a struct, an array, a register variable
and the other things a C variable cannot be written to (see the user guide) are
run-time errors that name the reason.

**Source lines.** With a source map loaded, `on execute main.c:19` (or
`on execute "dir with space/main.c:19"`) breaks on every place the line's code
starts, each in the memory it was built for; it takes no range and no `page`.
`on execute @name` with a banked label (an SLD label of a page) fires only in
that label's own page.

**Host keys.** In the emulator window (Qt or SDL) and in the debugger window,
Alt + 1 to Alt + 8 are the script host keys: Alt + *N* runs the loaded
scripts' `on hostkey` *N* rules, with or without the debugger window open.
The digit never reaches the guest, even when no script is loaded, so what
Alt + 1 does never depends on what is loaded. **\--script-key** is the same
key in **\--headless**. A debugger key binding can never use them.

**The Script tab.** In the Qt GUI the debugger window's Script tab lists the
loaded scripts' rules (the event with its filter, its state, its hit count),
the run's verdict (a stop or failed assert, an `exit`, a run-time error, the
verdicts not reached yet) and the script log. **Script > Load Script...**
loads one more script while the machine runs, **Reload Scripts** loads the
same files again and **Unload Scripts** removes them all; scripts given with
**\--script** appear there too. A script loaded from the menu registers at
once, and `FRAME` (and `on frame` *N*) count the machine's frames, not frames
since the load. In the GUI a script never ends the program: `stop`, a failed
`assert` and `exit` pause the machine and open the debugger.

**Keys.** `press` *KEY* holds a key down until `release` *KEY* (`press` *KEY*
`for` *N* is a pulse of *N* frames). *KEY* is a name **\--delayed-keypress**
takes, a single matrix position `row,col` (`0,0` is CAPS SHIFT, `7,1` SYMBOL
SHIFT), or one of the Next's extended keys as `ext:`*name* - `ext:right`,
`ext:left`, `ext:down`, `ext:up`, `ext:dot`, `ext:comma`, `ext:quote`,
`ext:semicolon`, `ext:extend`, `ext:capslock`, `ext:graph`, `ext:truevideo`,
`ext:invvideo`, `ext:break`, `ext:edit`, `ext:delete` (the keys NR 0xB0 / 0xB1
read; the PC's arrows, Backspace and Esc are among them). An extended key has
no `for` pulse.

**Recording a session.** **Script > Record Script...** in the debugger window
(or the Script tab's **Record...**, or **\--record-script** *FILE*) records
what you do into a replay script: every change of the keyboard, the Next
extended keys and both joysticks, as `press` / `release` / `joystick` at the
frame it landed on, and a capture at each **Alt + 8** (or **Script > Capture
Screen**). A capture is the ULA screen memory (*base*`-0001.scr` and a
`compare_scr` line) when only the ULA layer is on, and otherwise a PNG
(*base*`-0001.png`, and a `screenshot` of *base*`-0001-replay.png` to compare
it with). **Stop Recording** - or leaving jnext - writes the script. Its header
says what it was recorded on and the command that replays it:

```
jnext --headless --machine next --load game.nex --script session.jds \
      --delayed-automatic-exit-frames 1010
```

run in the directory holding the captures (the script names them relative to
it). The replay asserts the machine type and the
joystick mode (NR 0x05) it was recorded with, applies the same input at the
same frames, and ends with `exit 0`: a `.scr` that differs exits `3`. A cold
boot (a hard reset, or loading a program from the menu) restarts the recording,
because a replay starts at power-on too. What cannot be replayed exactly is
written into the script as a `WARNING` (and logged again when it replays): a
key changed while the machine was paused in the middle of a frame, input held
before the recording began, a rewind. Keys jnext types by itself (a tape's
`LOAD ""`, **\--delayed-keypress**) are recorded like any others, so replay
without the options that typed them.

**Exit status** in **\--headless** (and the SDL-only build): `0` for a clean run
or `exit 0`; *N* for `exit` *N* (0 to 255: any other value is a run-time
error); `3` for a `stop` or a failed `assert`; `1`
for a script that does not load or a run-time error (division by zero, say -
the rule is disabled and the run ends at the next frame). The first status a
run reaches is kept. When **\--delayed-automatic-exit** or
**\--delayed-automatic-exit-frames** ends a run before a script reached a
verdict it declared - a rule holding `exit` or `compare_scr` that never ran, or
a **\--script-key** not yet delivered - the run exits `3`
(`SCRIPT: N deferred actions never ran`). A status `2` never comes from a
script. A failure wins over a success at the same moment: when a `stop`, a
failed `assert` or `compare_scr`, or a run-time error happens at the same
event as an `exit 0` - in the same rule or in another one, before it or
after it - the run exits `3` (or `1` for the run-time error), and the log
says `SCRIPT EXIT 0 not taken`. A non-zero `exit` *N* is kept. An `exit` in an
`on stop` rule never changes the status: the pause that ran it already decided
it (a stop's `3`). An `exit`
that comes while a `compare_scr` is still waiting for its frame edge waits
with it, so `on write` *A* `do compare_scr` *F* *M* `exit 0 end` reports a
mismatch rather than exiting first.

The user guide's **Debugger scripting** chapter is the full reference: every
event with its payload, every action, the state names, and worked examples.

# REMOTE DEBUGGING (ZRCP)

With **\--zrcp-port** *PORT*, jnext serves the text protocol ZEsarUX offers on
its `--remoteprotocol-port` (ZRCP), speaking as ZEsarUX 12.0 does: a welcome,
then one command per line, each answered by its reply and a prompt -
`command> `, or `command@cpu-step> ` while the machine is held in cpu-step mode.
Try it by hand with `telnet localhost PORT` and `help`.

- **Served**: the session and information commands (`about`, `get-version`,
  `get-current-machine`, `help`, `ls`, `set-cr`, `quit` ...), inspection
  (`get-registers`, `set-register`, `read-memory`, `write-memory`, `hexdump`,
  `disassemble`, `get-memory-pages`, `get-tstates`, `evaluate`, the `tbblue-*`
  registers, sprites, patterns, palettes and clip windows), control
  (`enter-cpu-step`, `cpu-step`, `cpu-step-over`, `run`, `run` *n*,
  `hard-reset-cpu`, `reset-cpu`, `generate-nmi`) and breakpoints (the 100
  condition slots of `set-breakpoint`, `set-breakpointaction`,
  `enable-breakpoint`, `disable-breakpoint`, `enable-breakpoints`,
  `disable-breakpoints`, `get-breakpoints`, and the memory breakpoints of
  `set-membreakpoint`, `get-membreakpoints`, `clear-membreakpoints`), and
  history, coverage and loading (`cpu-history`, `cpu-code-coverage`,
  `extended-stack`, `smartload`, `load-binary`, `save-binary`,
  `snapshot-save`, `snapshot-load`). `help` *command* describes each one, with
  the divergences from ZEsarUX that a client can see in its replies. Numbers
  are decimal or hexadecimal with an `H` suffix (`8000H`); a malformed or
  out-of-range value is refused rather than read as something else.
- **`run` stops on any data sent**: while it runs, any line the client sends
  stops the machine and is discarded, not executed.
- **`hard-reset-cpu` is a cold boot** of the whole machine, complete before
  the reply; a paused machine stays paused.
- **Breakpoint conditions are ZEsarUX's** (`PC=8000H`, `SP>=65280`,
  `PC=PEEKW(SP-2) AND SP>=65280`, `A<>0 AND (HL&FFH)=5` ...), grouped the
  way ZEsarUX groups them; `help set-breakpoint` lists what is honoured and
  what is declined; `CVAR(`*path*`)`, a jnext extension, reads a C variable
  of the stopped program (`PC=8D3EH AND CVAR(a)=2`, `evaluate CVAR(p->x)`);
  `set-register CVAR(`*path*`)=`*value* writes one (`set-register CVAR(g_int)=10`;
  every refusal is `Error changing register`).
  A condition with no `PC=` first term fires only when it
  turns from false to true, as in ZEsarUX: one that stays true stops (or
  prints) once. A `PC=` breakpoint fires at every arrival at its address,
  except at the address a run resumes from.
  Breakpoints belong to the session: they start disabled, as in ZEsarUX, and
  are removed when the client disconnects.
- **History, coverage and call tracking are the machine's**: a client turns
  off only what it turned on, and its `cpu-history clear` / `set-max-size`
  change only its own view, never the trace jnext's Step Back reads.
- **`snapshot-save` / `snapshot-load` are in-memory snapshots of the
  session** (nothing is written to disk), saved with the machine paused while
  running, not at a breakpoint. **`smartload`, `load-binary` and
  `save-binary` read and write files on the host running jnext**, as
  ZEsarUX's do.
- **Declined**: `exit-emulator`, and the `run` options `verbose`,
  `no-stop-on-data` and `update-immediately`. ZEsarUX commands jnext does not
  serve answer `Error. Unsupported command in jnext:` and the name.

Connecting does not pause the machine; `enter-cpu-step` does. Disconnecting
(or `quit`) resumes it if the pause was the client's own and no other client
is attached; if one is, the machine stays paused and the pause becomes that
client's. One client at a time: a second connection is answered
`Error. Another ZRCP client is connected` and closed. In **\--headless** mode
a client that holds the machine paused holds its frames too, exactly as for
DZRP. ZRCP has no authentication either: the server listens on `127.0.0.1`
unless **\--debug-listen-address** says otherwise.

DeZog connects with its `zrcp` remote (`"remoteType": "zrcp"`,
`"zrcp": {"port": PORT}` in `launch.json`). Its default `resetOnLaunch: true`
starts each session with `hard-reset-cpu`, the cold boot above; keep
`skipInterrupt` at its default `false`, since jnext does not step over
interrupt routines (`set-debug-settings` bit 5 is refused, and DeZog shows
that once as a warning).

# MAGIC BREAKPOINT AND MAGIC PORT

The magic breakpoint uses the `ED FF` (ZEsarUX), `FD 00` (CSpect 2.19.9.1 and
later; the sjasmplus `break` instruction since 1.21.0, which needs `--zxnext=cspect`
or `OPT --zxnext=cspect`) and `DD 01` (older CSpect and sjasmplus) opcodes to
pause the debugger when enabled; execution then resumes right after the two
bytes. When disabled, and on real hardware, `ED FF` and `FD 00` are two-byte
NOPs, but `DD 01` is not: the `DD` prefix is ignored and `01 nn nn` executes as
`LD BC,nn`, consuming the two bytes that follow and overwriting `BC`. Enable it with
**\--magic-breakpoint** or **Debug > Magic Breakpoint**.

The magic debug port logs writes to a configurable port to stderr as hex,
decimal, ASCII or line-buffered text. Enable it with **\--magic-port** *PORT*
and **\--magic-port-mode** *MODE*.

# THE BROWSER BUILD

The same emulator, built for WebAssembly, runs inside a web page. The page loads
one script, `jnext-VERSION.js` (with `jnext-VERSION.wasm` beside it), which defines
one global, `jnext`; **jnext.mount**(*container*, *options*) puts a player in a DOM
element and returns a promise for it. Every option of **mount** is a command-line
flag or a **\--set** key, so what the page asks for is what the command line would
ask for; the player's `argv` property shows that command line. A browser page loads
`.nex`, `.tap`, `.tzx` and `.wav` programs, `.rzx` recordings and `.jns`, `.sna`, `.szx` and `.z80` snapshots; a
snapshot or a recording runs on the machine it was made on. A `.wav` plays in real time, as from a
cassette, and only while the machine runs: `pause()` or a hidden tab stops the tape
where it is.

NextZXOS, the operating system of the ZX Spectrum Next, is SpecNext's copyrighted
software. JNEXT does not ship, host or serve it, and neither does a page. The player
makes its SD card in the visitor's browser from the official distribution zip on
specnext.com, which the page cannot name or replace: when specnext.com allows the
page to read the file, the page downloads it; otherwise the player shows a link to
download it from specnext.com and a button to choose the downloaded file. Either way
the card the browser makes is kept in the browser's cache for that site, checked
against its SHA-256 on every visit, and the downloaded file itself is not kept. Nothing
is downloaded or built until the visitor clicks the player: the click is also what
starts the machine, because browsers allow sound only after a click. The page must be
a secure context: `https://`, `http://localhost` or `http://127.0.0.1`.

The CRT effect of the desktop window is available too, off by default: a page asks
for it with the option **\--crt** *EFFECT* (in `args`) or the three `startup.crt_*`
settings (in `set`), and gets the same five looks with the same parameters. It runs
on the page's WebGL, so it needs a browser with WebGL, and the soft scanlines show
their lines only when the picture is at least twice 640x512 device pixels, as on the
desktop. If the shader cannot run, the player logs one line starting `CRT effect:`
and carries on: with the software scanlines for `soft` (or when `scanlines` was also
asked), with the plain picture otherwise.

## JavaScript options

`card` *{source, dirs}*
:   Not a jnext setting: how the page gets NextZXOS's SD card. `source` is `'auto'`
    (the default: fetch the official file from specnext.com when it allows the page to
    read it, otherwise ask the visitor to download it and choose it), `'fetch'` (only
    fetch; a failure is an error), `'pick'` (always ask the visitor) or `'none'` (no
    SD card at all: the `file` NEX runs as with **\--no-sdcard**, nothing is fetched
    or asked, and a ROM fetch is also reported through `onError`; needs `file`,
    excludes `keepWrites`). The SD card
    reaches the emulator as **\--sdcard** and **\--sdcard-sha256**, set by the page
    layer; the page never names the card. `dirs` is an array of absolute paths in the
    distribution (`['/GAMES', '/APPS']`): the card then also holds each path with
    everything below it and the directories above it, case does not matter, and a
    malformed path, a path of the boot set, a duplicate or a nested one is refused. The
    default is none: the boot card only.

`poster` *URL*
:   Not a jnext setting: a picture, a screenshot of the program, shown in the player
    area before the click so the player is not black on load. The emulator replaces it
    when the visitor starts the player. Without it nothing is shown there.

`creditIn` *element*
:   Not a jnext setting: an element of the page, normally its controls bar, that holds the "JNext" link
    (www.jnext.es) at its end instead of a row under the player; in a flex row it sits at the right end.
    Without it the link goes under the player, aligned to its right edge.

`file` *URL | {url} | {data, name} | {card}*
:   **\--load**. The program to run (`.nex`, `.tap`, `.tzx`, `.wav`, a `.rzx` recording, or a `.jns`, `.sna`,
    `.szx` or `.z80` snapshot); without it the machine boots to the NextZXOS menu. A `.zip` is
    accepted when the program in it is a `.nex`: the NEX gets its directory in the archive (see
    the `.zip` entry of **LOADING PROGRAMS**), read-only unless `args: ['--esxdos-stub-writable']`
    is given, and what it writes is not kept by `keepWrites`. `player.load()` accepts the same.
    Any other `.zip` is refused. `{card: '/PATH/x.nex'}`
    runs a NEX that is on the SD card: its path lies inside one entry of `card.dirs`, the directory that holds
    it is copied out of the card with everything below it, and that directory is served to the program with
    **\--esxdos-stub** (read-only unless `args` has **\--esxdos-stub-writable**). It cannot be used with
    card source `'none'`, and `load()` refuses it.

`keepWrites` *boolean*
:   **\--sdcard-writes**, with a file the page manages: what the program writes
    to the card (saves, high scores) is kept in the visitor's browser
    (IndexedDB) and is there at the next visit. Off by default. The record is
    keyed by the card's SHA-256 and the page's path, so another card or another
    page has its own, and it holds only the sectors written, never the card.
    The page can clear it with `resetCard()`, or with `args:
    ['--sdcard-writes-reset']` for one start. The file a NEX loaded with `file`
    saves through esxDOS is kept too: JNEXT's own stand-in answers those calls
    (see **esxDOS calls from a directly loaded NEX**) and keeps one file, which
    belongs to the page's card and the file name, not to the program, so a
    changed NEX on the same page reads the old save, as on a real Next.
    Files written to a host directory a page names with
    `args: ['--esxdos-stub-root', ...]` live in the page's memory only.

`machine` *string*
:   **\--machine**.

`touchKeyboard` *string*
:   **\--touch-keyboard**: `'auto'` (default; the on-screen keyboard is shown on a
    phone or tablet), `'on'` or `'off'`.

`debug` *{protocol, transport, url} | array*
:   **\--debug-channel**, once for each entry: serve a debugger protocol to a client of the page.
    `protocol` is `'dzrp'` (DeZog's DZRP), `'zrcp'` (the ZEsarUX remote command protocol, also DeZog's)
    or `'gdb'` (the GDB remote protocol of `z88dk-gdb`); an array holds one entry for each protocol at
    most, up to three. `transport` is `'channel'` (the default: the page's own code is the client, through
    `player.debugChannel(protocol)`, see The player) or `'ws'`: the player dials the WebSocket `url`
    (`ws://` or `wss://`, required with `'ws'`, refused with `'channel'`) and carries the bytes of one client
    at a time over it, in the format of The debug link. A page cannot listen on TCP, so a client that is a
    program on the same computer (DeZog, `z88dk-gdb`) reaches the page through a bridge such as
    `tools/web-debug-bridge/jnext-debug-bridge.mjs`, which the web package ships. The machine is served on the
    frame loop: while the player is paused or the tab is hidden, a client waits. There is no authentication.

`muted` *boolean*
:   `startup.silent`: no audio device for the session.

`set` *{"section.key": value}*
:   One **\--set** *section.key=value* per entry, sorted by key.

`args` *array of strings*
:   Further command-line arguments, appended last, so they win over everything
    above.

`width`, `height` *number | CSS length*
:   Not a jnext setting: the box the player fills in the page.

`wasmUrl` *string*
:   Not a jnext setting: where the page serves the `.wasm` file from.

`onProgress`, `onReady`, `onError`, `onNotice` *function*
:   Not a jnext setting: functions of the page, called with `(phase, fraction)`
    while the SD card is fetched (`download`) or made from a file the visitor chose
    (`build`), when the page is waiting for that file (`choose`), while the first boot
    is recorded (`warm-start`) or restored (`restore`), when the program is in the
    machine and a frame has been shown, and with a message on every failure.
    `onNotice(code, message)` is for something that is not a failure: today the code
    `card-writes-reset`, when saves kept by `keepWrites` on an older NextZXOS card
    were reset.

## The player

`state` is `waiting`, `starting`, `running`, `paused`, `exited` or `destroyed`;
`ready` is a promise for the first ready. `pause()` and `resume()` stop and restart
the frame loop. `reset(kind)` restarts the program (`'program'`, the default; with no program it powers on again),
powers the machine on again (`'hard'`) or resets it (`'soft'`). `nmi()` presses the NMI button (the Multiface one, as F9 and the case button "NMI"), which opens NextZXOS's NMI Menu; it rejects, with the reason as the error message, when the machine is not running or the card source is `'none'` (no Multiface ROM). `load(file)` loads
another program while the machine runs. `saveSnapshot()` resolves to the bytes of a
`.jns` snapshot of the running machine (**\--snapshot-compression** applies), for the page
to offer as a download or give back to `load()` as `{data, name}`. `startRzxRecording()` starts
an RZX recording of the machine as it is now (not on a ZX Spectrum Next: give the player another
`machine`); `stopRzxRecording()` ends it and resolves to the file's bytes, for the page to offer as a
download or give back to `load()` as `{data, name}`. A reset or a `load()` ends the recording early, and
`stopRzxRecording()` still returns it, and until it is called no new recording can start; `destroy()` drops one that was not stopped. While a recording
plays, every port read comes from the file: the keys do nothing until it ends. `setVolume(v)` sets the master output
level, `0` to `1`. `destroy()` stops the machine and removes the player (it waits until the saves
of **keepWrites** are stored). `resetCard()` discards the saves kept by
`keepWrites` (the saved file included) and restarts the program; it needs that option, and resolves when
the program is running again. `showKeyboard(on)` shows (`true`) or hides (`false`) the
on-screen keyboard, whatever **\--touch-keyboard** says; the page's call wins, before or after the
start, and anything but a boolean throws. `keyboardVisible` tells whether it is shown.

`listFiles()` returns the files JNEXT has written for an output option (**\--wav-record**, **\--dac-trace**,
**\--tape-save**, **\--delayed-screenshot**, **\--compositor-trace**, **\--profile-output**, **\--export-skoolkit**,
**\--export-ghidra**, **\--bug-report**, **\--log-file**), in the order JNEXT began them, as an array of
`{path, size, complete}`: `path` is absolute, `size` the bytes now in the page's memory, `complete` true once
JNEXT has finished the file for good. It is empty before the click. `readFile(path)` resolves to a copy of the
bytes of a complete file listFiles() gives; it rejects (without `onError`) a path that is not a string (a
`TypeError`), a path JNEXT did not write, a file that is not complete yet, and anything in the player's own
locations. `stop()` ends the machine and keeps the player and its files: it resolves to the exit status once
every file is finished, and a stopped player cannot start again. The player's own locations are `/sdcard` (the SD
card), `/jnext` (what the player keeps) and `/media` (the programs it installs): neither member lists or returns a
file there, whoever wrote it and however the name is spelt (`..`, a relative name or a link included). What an export the page asked for contains is the page author's choice, as on the desktop (a **\--export-skoolkit** of ROM space holds the ROM
the machine ran), and offering it to visitors is the author's responsibility. After
`destroy()` all three throw.

`debugChannel(protocol)` returns the channel of the `debug` entry for `'dzrp'`, `'zrcp'` or `'gdb'` whose
`transport` is `'channel'`; it throws for a protocol the page did not ask for and for one served over
WebSocket. It can be fetched as soon as `mount()` resolves, but `open()` and `send()` throw until the
machine runs. `open()` says a client arrived (it throws when one is open); `send(bytes)` gives the
channel what the client said (a `Uint8Array` or an `ArrayBuffer`; it throws when no client is open, and
with a `RangeError` when the input is full); `close()` says the client left (no `onClose` follows).
`onData` is called with a `Uint8Array` of what the machine answered, and `onClose` when the machine
ended the session (ZRCP `quit`, `destroy()`, the machine exiting); both are called from a microtask, after the
player's own call has returned, never from inside it. `isOpen` tells whether a client is open.

## The debug link

A `debug` entry with `transport: 'ws'` keeps one WebSocket open to its `url` for as long as the player
runs, and redials once a second when it is lost. The link carries one client at a time: the text
message `open` says a client arrived, the text message `close` says either side ended it, and binary
messages carry the bytes in both directions, in order. When the bridge sends `close` (its client left),
the player answers with the text message `closed`, and the bridge admits no new client before it: what the
player sent for the old session is then all behind it. Any other text message is ignored. The player
tells the console once for each time the link goes down, and never through `onError`.

## The touch keyboard

The on-screen Spectrum Next keyboard is for devices with no keyboard. It sits at the bottom of the
player's box, which keeps the size the page gave it; the screen above it shrinks by the
keyboard's height. It is built on the first time it is shown, so a page that never shows it has
the DOM it would have without it.

Its keys are typed as the keys of a PC keyboard are: through the same path, so what the program sees
is what a physical keyboard gives. A key goes down when a finger lands on it and up when the finger
lifts; several fingers hold several keys, for chords and for games. Tapping a key never moves the
keyboard focus off the screen.

There are two layouts, chosen by the width of the box and followed when it changes: below 504 CSS
pixels the 40 keys of the 48K keyboard, in four rows (every other key of the Next is Caps Shift or
Symbol Shift plus a key, as on a 48K), and from 504 pixels on the Next's 58 keys, in five rows, with
the sixteen extra keys (Delete, Edit, Break, Graph, Extend, the cursor keys and the rest). Its height
is at most 45% of the box.

Caps Shift and Symbol Shift latch: a quick tap (under half a second, with no other key pressed
meanwhile) leaves the shift waiting, shown as pressed, and the next key is typed with it; a tap on a
waiting shift cancels it. A shift held longer, or held while another key is pressed, is an ordinary
hold and does not wait. Each key has an accessible name, and the shifts report `aria-pressed`.

## Not available in the browser

A page has no processes, sockets or terminal, so these are refused with a message
that says so: **\--nextpi**, **\--nextpi-dir**, **\--nextpi-release**,
**\--nextpi-qemu**, **\--nextpi-audio**, **\--nextpi-qemu-args**, **\--esp**,
**\--esp-allow**, **\--esp-listen-address**, **\--esp-delayed-disassociate-frames**,
**\--esp-delayed-associate-frames**, **\--esp-ip-address**,
**\--esp-ip-address-after**, **\--record**, **\--dzrp-port**, **\--gdb-port**,
**\--zrcp-port**, **\--debug-listen-address**, **\--debugger**, **\--break-at**,
**\--script**, **\--script-key**,
**\--record-script**, **\--map**, **\--annotations**, **\--import-skoolkit**,
**\--import-ghidra**, **\--sdcard2**, **\--sdcard2-readonly**, **\--sdcard2-writes**,
**\--sdcard2-writes-reset**, **\--joy-uart-pty**, **\--joy-uart-fifo**, **\--headless** (it would block the tab for ever)
and **\--rzx-record** (a page records from JavaScript, see The player). The debugger protocols are served to the page instead: see the debug option under JavaScript options. The settings of the refused flags are refused
when given as set entries too: `esp.enabled`, `esp.allowed_hosts`, `nextpi.enabled`, `nextpi.dir`,
`nextpi.release`, `nextpi.qemu_binary`, `nextpi.audio`, `nextpi.qemu_args`, `paths.sd_card2_path`.

# EXAMPLES

Boot NextZXOS / the TBBlue firmware (the default machine is `next`):

    jnext

The same, with an explicit SD-card image:

    jnext --sdcard ~/.jnext/sdcard/cspect-next-1gb-fixed.img

Run a program; a bare filename is shorthand for **\--load**:

    jnext game.nex
    jnext game.tap          # auto-types LOAD ""
    jnext game.sna

Run as a plain ZX Spectrum 48K (the 48K BASIC ROM comes from the SD image):

    jnext --machine 48k

Run at double speed:

    jnext --speed 200

Inject a raw binary at 0x8000 and run it:

    jnext --inject program.bin --inject-org 8000

Take a headless screenshot, for CI. Name the SD image explicitly: with no
**\--sdcard** and no cached image, provisioning prompts on stdin and declines
at EOF, so an unattended run would exit non-zero instead of capturing
anything (**\--sdcard-download-confirm** is the alternative).

    jnext --headless --machine 48k \
        --sdcard ~/.jnext/sdcard/cspect-next-1gb-fixed.img \
        --delayed-screenshot /tmp/test.png \
        --delayed-screenshot-frames 200 --delayed-automatic-exit 5

Apply trainers 1 and 3 (this one with the value 9) of a POK file 1200 frames
into a tape load, and keep the result as a snapshot:

    jnext --headless --machine 48k game.tap \
        --delayed-pok-frames 1200 game.pok --pok-trainers 1,3=9 \
        --delayed-snapshot trained.sna --delayed-snapshot-frames 1210 \
        --delayed-automatic-exit-frames 1300

Capture Layer 2 on its own, then the ULA and sprites together:

    jnext --headless game.nex --delayed-screenshot l2.png \
        --delayed-screenshot-layers layer2 \
        --delayed-screenshot-frames 200 --delayed-automatic-exit-frames 250
    jnext --headless game.nex --delayed-screenshot us.png \
        --delayed-screenshot-layers ula,sprites \
        --delayed-screenshot-frames 200 --delayed-automatic-exit-frames 250

# FILES

`~/.jnext/jnext.conf`
:   GUI configuration, in INI format. Written by **Settings > Preferences**.
    Host gains are stored under `[audio]` as `gain_db` (master),
    `gain_beeper_db`, `gain_ay0_db`, `gain_ay1_db`, `gain_ay2_db`, and
    `gain_dac_db`.
    The degradation policy is stored under `[startup]` as `when_slow_prefer`
    (`audio` or `video`, the persistent form of **\--when-slow-prefer**); an
    unrecognised value keeps the default rather than changing how the emulator
    degrades.
    The emulated ESP-01 is stored under `[esp]` as `enabled` (`true`/`false`,
    the persistent form of **\--esp**) and `allowed_hosts` (a comma-separated
    list, the persistent form of **\--esp-allow**), both edited under
    **Settings > Preferences > Network**. Whenever `enabled` is set the status
    bar carries the ESP cell **NETWORKING** describes, so neither a config file
    nor that page can put the guest on the network without saying so.
    **\--no-esp** overrides it for one run.
    NextPi is stored under `[nextpi]`, edited under **Settings >
    Preferences > NextPi**: `enabled` (`true`/`false`, the persistent form of
    **\--nextpi**; **\--no-nextpi** overrides it for one run), `dir` (the
    NextPi directory; empty means `~/.jnext/nextpi`), `release` (a NextPi
    release name on the mirror, or `latest` for the newest one listed there;
    empty means `1_93D`), `qemu_binary` (empty means `qemu-system-arm` on
    `PATH`) and `audio` (empty means the Next's mixer, so NextREG 0xA2
    controls it and `--record` captures it. `host` plays it on this
    computer's default output, `coreaudio` on macOS, `dsound` on Windows and
    `pa` elsewhere; otherwise a QEMU `-audiodev` driver such as `coreaudio`,
    `pa` or `none` to play it straight to the host, or `wav:`*FILE* to record
    it on its own). Changing the release makes the next start offer to
    download it, replacing the installed one.
    `qemu_args` holds extra QEMU arguments (empty means none; **\--nextpi-qemu-args** sets it for one run), added after
    jnext's own when NextPi starts. It is split into arguments on spaces;
    `'...'` and `"..."` keep spaces in one argument; a backslash is an ordinary
    character; nothing is expanded and no shell is involved. An unterminated
    quote stops NextPi from starting, with a message. Set it in Preferences
    or with **\--nextpi-qemu-args** rather than by hand: jnext writes the file
    value correctly itself. If you edit the file, remember that its INI reader
    (Qt's) has its own rules, applied before the text above: write the whole
    value in double quotes, write each `\` as `\\` and each `"` as `\"`;
    otherwise a backslash is an escape (`C:\pi\x.img` reads as `C:i.img`),
    a bare `"` is swallowed, a `;` ends an unquoted value, and the spaces
    beside a comma are lost.
    The joystick ports are stored under `[input]`: `joy1_source` and
    `joy2_source` (`sdl`, `keys` or `none`), `joy1_device` and `joy2_device`
    (the controller's id: 32 hex digits, optionally `#N`; empty means
    Automatic) and `joy1_device_name` and `joy2_device_name` (the name shown
    while that controller is not connected). **\--joy1-device** and
    **\--joy2-device** override the ids for one run. An id that is not valid is
    ignored, with its name. Also under `[input]`, `mouse_release` (`ctrl-alt`,
    `middle-click` or `esc`; **\--mouse-release** overrides it for one run).
    The quick screenshot is stored under `[screenshot]` as `quick_dir` (empty
    means `~/.jnext/screenshots`) and `quick_format` (`png` or `scr`), edited
    under **Settings > Preferences > Paths**; an unrecognised format keeps the
    default.
    The Quick Save retention is stored under `[snapshot]` as `quick_keep` (a
    number from 1, or `unlimited`; default 20), edited under **Settings >
    Preferences > Paths**; **\--quick-snapshot-keep** overrides it for one run,
    and an unrecognised value keeps the default.
    The debugger window's layout is stored under `[debugger]`: `size` (`W, H`),
    `position` (`X, Y`, written only while the window is detached),
    `attached` (`true`/`false`) and `layout` (the arrangement of its panels, one
    line of plain text; deleting it, or Window > Reset Layout, restores the
    default). An older `~/.jnext/Debugger.conf` is merged
    into this section once and removed. All section names are lowercase.
    CLI options always take precedence over saved values, and headless runs
    never read it. Every setting above can also be set for one run, without
    touching the file (the Recent Files history, the joystick controller names
    `joy1_device_name` and `joy2_device_name`, `[config] version` and the
    debugger window's `size`, `position` and `layout` are refused), with **\--set** *section.key=value* (see **Settings for
    \--set**).

`~/.jnext/screenshots/`
:   Where **File > Quick Screenshot** writes, unless `[screenshot] quick_dir`
    says otherwise. Created on the first capture.

`~/.jnext/snapshots/`
:   Where **File > Quick Save Snapshot** writes and **Quick Load Snapshot**
    reads (the newest `.jns` in it). Created on the first save. Quick Save keeps
    the newest 20 files it made (see **\--quick-snapshot-keep**).

`*.jnext-bug.zip`
:   A bug report (see **BUG REPORTS**), named `PROGRAM.YYYYMMDDhhmm.jnext-bug.zip`
    by **\--bug-report** *DIR*`/` and proposed under that name by **File > Save Bug
    Report...**. Where it goes is your choice; jnext keeps no folder of them.

`~/.jnext/warm-start/`
:   The recorded NextZXOS-resident machine a **\--load** of a `.nex` starts
    from, one file per machine type, around 100 KB each. Written on the first
    such load with a given SD image and re-recorded whenever the image's size or
    modification time (or **\--sdcard-sha256** digest) changes, or JNEXT writes to it, or the machine type, the
    state format or the recording procedure changes - see
    **\--warm-start-regenerate**. Deleting it costs one cold boot, nothing
    else.

`~/.jnext/sdcard/cspect-next-1gb-fixed.img`
:   The default SD-card image, used when **\--sdcard** is not given.

`~/.jnext/sdcard/cspect-next-1gb.img`
:   The canonical distribution image the patched one above is produced from.

`~/.jnext/sdcard/cspect-next-1gb-fixed.img.distro`, `cspect-next-1gb.img.distro`
:   The distribution (the zip file name, e.g. `sn-emulator-24.11.zip`) each of
    the two images came from. Without one, the image is taken to be 24.11.

`~/.jnext/sdcard/cspect-next-1gb-fixed.img.distro-declined`
:   The distribution whose upgrade offer you turned down with "Keep my card
    (don't ask again)" (or **\--sdcard-upgrade** `never`). Delete it, or use
    **\--sdcard-upgrade** `ask`, to be asked again.

`~/.jnext/sdcard/previous/`
:   The backup an upgrade leaves: the card, the original image and their
    sidecars as they were, the old contents of your writes files in `overlays/`,
    and `upgrade-report.txt`, the report of that upgrade.

`~/.jnext/sdcard/overlays.list`
:   The **\--sdcard-writes** files you have used with the default card, one path
    per line, so that an upgrade started from the desktop rebuilds them too.
    Files that are gone or no longer belong to the card are dropped from it.

`~/.jnext/nextpi/`
:   The NextPi directory: `nextpi.img` (the release's SD-card image,
    never written), `boot/` (the kernel and device tree QEMU boots, copied out
    of the image), `release` (which release it is; it must match the configured
    release), `overlay.qcow2` (everything
    NextPi writes to its card — delete it to start again from a pristine image)
    and `qemu.log` (QEMU's own messages). About 15 GB.

# EXIT STATUS

**jnext** exits 0 on success and non-zero on error. A command-line usage error
(an unknown option, or a value an option does not accept) exits 1. In particular it exits
non-zero when a **\--delayed-screenshot** was requested but never taken,
rather than silently writing nothing, and likewise when a **\--record**
recording fails to materialize as a usable output file, and when
**\--delayed-automatic-exit** fires before other deferred work (a **\--load**,
an **\--inject**, a keypress...) has happened — see that option.

It also exits non-zero when the program given to **\--load** (or as a bare
file name) fails to load, whatever its format, including an RZX recording
given to **\--load** or **\--rzx-play**. This holds in the GUI, the SDL-only
build and under **\--headless** alike. The error is logged, the machine keeps
running without the program, and the non-zero status is reported when
**jnext** exits.

It exits 1 when a **\--bug-report** file cannot be written, the way it does for a failed
**\--export-skoolkit**.

SIGTERM, or SIGINT (Ctrl-C in the terminal), ends **jnext** the way closing the
window does: recordings and captures are finished and no dialog is shown.
**jnext** then terminates by that same signal, so the shell sees 143 (SIGTERM)
or 130 (SIGINT), never a success. A second signal, SIGINT or SIGTERM in either order, ends it immediately. A signal
that was ignored when **jnext** started (**nohup**, a background job of a
non-interactive shell) stays ignored. This applies to the GUI, the SDL-only
build and **\--headless**; on Windows it is unchanged.

**\--sdcard-go-back** and **\--sdcard-delete-backup** exit 0 when done, 2 when there is
no previous SD card to act on, 3 when they failed with nothing changed, and 4 when
the default card is in use by another jnext (a usage error is 1, as above).

**\--sdcard-file-add** is a scriptable non-emulation mode, so it reports which
failure happened rather than a single non-zero status. **\--sdcard-writes-flatten**,
**\--sdcard-export-changes**, **\--sdcard-import-changes** and
**\--sdcard-file-extract** use the same table:

0
:   The file or directory was copied onto the card (for **\--sdcard-file-extract**,
    off the card into the current directory).

1
:   Command-line usage error — **\--sdcard-file-dest** or
    **\--sdcard-file-force** without **\--sdcard-file-add**, either of the
    first two given twice or with an empty value, or combined with
    **\--sdcard-readonly** or **\--load**; for the three commands above, a
    missing companion (**\--sdcard-writes-flatten** without **\--sdcard-writes**,
    **\--sdcard-export-base** without **\--sdcard-export-changes**), an empty
    value, a repeat, or two of the commands in one run. For **\--sdcard-file-extract**:
    given twice, with an empty value, with another SD card command, with **\--load**
    or **\--inject**, or with **\--no-sdcard**.

2
:   For **\--sdcard-file-extract**: the card path is malformed (empty, or with a `.`
    or `..` component, a trailing dot or space, a character outside printable
    ASCII) or names nothing on the card. For **\--sdcard-import-changes**: the
    directory has no `CHANGES.txt`, it is
    not a valid changes list, or a listed file is missing or not the size its
    line says. Otherwise the host file is missing, unreadable, shrank while being copied, or is 4 GiB
    or larger (which FAT32 cannot store); or, inside a directory, a device,
    FIFO or socket, an unreadable directory, a dangling symbolic link, or one
    that loops back.

3
:   For **\--sdcard-file-extract**: the current directory cannot be written, or the
    local file or directory cannot be created or written (the disk is full, a
    name or path too long); what the run created is removed again.
    Otherwise the destination path is unusable: malformed or non-ASCII, a name FAT
    forbids, a component that already exists as a file, or a destination that
    already exists as a directory (for a file) or as a file (for a directory).
    For a directory this includes a host name inside it that FAT cannot hold,
    and two names that differ only in case. A destination that is the 8.3 alias
    of a different entry on the card is refused the same way.
    For **\--sdcard-writes-flatten** and **\--sdcard-export-changes** it also
    covers: nothing to compare against (the card was not made by jnext in a way
    that can be rebuilt and no **\--sdcard-export-base** or **\--sdcard-writes**
    was given), a *DIR* that is not empty or an *OUT* that already exists, a
    writes file made for another image, an unreadable or unmountable base, and
    a copy that failed.

4
:   A destination file already exists and **\--sdcard-file-force** was not
    given. Nothing was written. For **\--sdcard-file-extract**: a local file,
    directory or symbolic link of that name exists in the current directory; it
    is left as it is.

5
:   The SD image is missing, is not an MBR-partitioned FAT32 image, holds a
    filesystem this build will not mount (an under-clustered one), or could not
    be written, or, with **\--sdcard-writes**, the writes file could not be
    written or holds a rewritten partition table or boot sector; or another
    jnext is using the image or the writes file (one card, one jnext). For
    **\--sdcard-export-changes** also: the card, seen through its writes file, could not be
    opened for reading. For **\--sdcard-file-extract**, any FAT12, FAT16 or FAT32
    card the Next's firmware mounts is accepted; the card is refused when it is
    not one of those, uses sectors other than 512 bytes, is truncated, or has a
    damaged directory structure (a directory that is reached twice, two names the
    host cannot tell apart, a file shorter than its size, a name that cannot be
    opened).

6
:   The card has no room — not enough free space, or a directory with no free
    slots.

# SEE ALSO

**ffmpeg**(1)

The project documentation: `README.md` for an overview and `BUILD.md` for
building from source.

Project home page: <https://github.com/jorgegv/jnext>

# BUGS

Report bugs at <https://github.com/jorgegv/jnext/issues>. Attach a bug report (**File > Save Bug
Report...**, or **\--bug-report** *DIR*`/`): see **BUG REPORTS**.

# AUTHORS

Main author: Jorge Gonzalez, aka ZXjogv <zx@jogv.es>.

Code contributors: dcrespo3d, jon263.

Testers and bug reporters: danboid, Duefectu, janko-jj, Utodev,
WoolyChewbakker.

If you are named here and would prefer not to be, or are not named and would
like to be, say so at the issue tracker below. `CREDITS.md` in the source
distribution carries the same list, plus the third-party libraries and the
reference projects jnext is built and verified against.

# COPYRIGHT

Copyright (C) ZXjogv <zx@jogv.es>. Licensed under the GNU General Public
License version 3 or later.
