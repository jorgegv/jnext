# NextPi on the Raspberry Pi UART — Design

**Status:** implemented on branch `pi-uart` (PR #310).
**Last updated:** 2026-10-07.
**Audience:** jnext maintainers, and users who run NextPi software against jnext.

> **Authority.** Hardware behaviour is cited from the ZX Spectrum Next FPGA VHDL
> (`cores/zxnext/src/zxnext.vhd`) through the citations jnext already carries for the
> registers involved (NR 0xA0 in `emulator.h`, the UART muxes in `uart.h`). Guest
> behaviour is cited from the software that consumes the link: NextPi's `.pisend`
> 2.63 dot command, as shipped in `/DOT` on the official SD card image, read by
> disassembly. Where something is inferred rather than cited, it says so.

---

## 1. Use case

A real ZX Spectrum Next can carry a Raspberry Pi on its GPIO header, running
**NextPi** (a DietPi-based distribution). The Next talks to it over **UART 1** — a
plain 115200-baud serial console on which NextPi's *Supervisor* answers with a
`SUP>` prompt — and NextZXOS ships the tools that use it: `.pisend` (send a
command, upload a file), `.piget`/`.piput`, the NextPi UI, and programs that
offload audio (`nextpi-play_speech`, `nextpi-play_midiLQ`, SID/MOD players, TZX
streaming) to the Pi.

None of that can be developed or tried on jnext today. UART 1 with nothing
attached loops its transmitter back into its own receiver, so `.pisend -q`
"discovers" a Pi that is the guest talking to itself, and every command is lost.

The Pi side **can** run on a desktop: NextPi releases are SD-card images for a Pi
Zero, and QEMU's `raspi0` machine boots them, with the Pi's UART (`ttyAMA0`, the
Supervisor's console) as a QEMU chardev.

This feature puts the two together. jnext does **not** emulate the Pi. With
`--nextpi` it downloads NextPi on first use, starts QEMU on it, and connects
the Pi's console to UART 1 — the same way it uses ffmpeg when that is installed:
an external tool jnext drives, not code it contains.

```
 Z80 guest ── UART 1 ── NR 0xA0 GPIO mux ── PiUartDevice ── FIFO pair ── QEMU pipe chardev ── raspi0 ── NextPi
                                                     (jnext provisions NextPi, creates the FIFOs, spawns QEMU)
```

## 2. Functionality

### 2.1 One enabler, defaults for the rest

| Command line | Effect |
|------|--------|
| `--nextpi` | Start NextPi for this run. |
| `--no-nextpi` | Do not start it this run, whatever the saved preference says. |

Everything else has a default, and lives in the `[nextpi]` section of
`~/.jnext/jnext.conf`, edited under **Settings > Preferences > NextPi**:

| Key | Control | Default |
|-----|---------|---------|
| `enabled` | "Start NextPi (under QEMU) with jnext" tick | `false` |
| `dir` | NextPi directory | `~/.jnext/nextpi` (`$JNEXT_CONFIG_DIR/nextpi`) |
| `release` | NextPi release (editable: a release name, or `latest`) | `1_93D` |
| `qemu_binary` | QEMU | `qemu-system-arm` on `PATH`; `qemu-img` beside it |
| `audio` | Pi audio (editable: a QEMU `-audiodev` driver, `none`, `wav:FILE`) | `coreaudio` on macOS, `pa` elsewhere |

An empty value means the default, so a config file never pins a home directory.
`--nextpi`/`--no-nextpi` override `enabled` the way `--esp`/`--no-esp` override
`[esp] enabled`, and as for the ESP the saved preference is read only by a GUI
session. Changes made in Preferences take effect at the next launch: QEMU and the
download happen before the main window exists, and the Pi deliberately outlives
cold boots (§3.5), so there is nothing to toggle live. Apply logs that, as it
does for the ESP.

### 2.2 First use

The first start that needs the Pi finds no NextPi and offers to download it — the
SD card image's flow: a Yes/No question, a progress bar for the ~6 GB download,
then a busy indicator while the archive's MD5 is checked and the ~15 GB image
unpacked (a terminal session gets the same steps as prompts and a percentage).
Every later start finds the directory ready, touches no network and starts QEMU
at once. NextPi then takes about a minute to reach `SUP>`, after which:

```
.pisend -q
.pisend -c nextpi-play_speech "Hello from the Spectrum Next"
```

### 2.3 Choices made, and the alternatives

- **No CLI for the settings, only the enabler** — per review: jnext gives a
  feature one switch and defaults the rest. The other settings are a
  Preferences tab and a config section.
- **A pinned release, with `latest` as an opt-in.** The default `1_93D` changes
  only when jnext does, so what a user runs is reproducible; `latest` follows
  the mirror's newest release, checked at each start, falling back to the
  installed one when the mirror cannot be reached.
- **jnext downloads and prepares NextPi itself**, rather than relying on a
  separately prepared directory: the extraction needs no external tool (zlib for
  the `.tar.gz`, jnext's own FAT32 reader for the boot partition, OpenSSL's MD5 —
  all already linked), so the only external requirement is QEMU itself.
- **The archive is deleted once unpacked** (6 GB saved); the image is kept (15 GB)
  because QEMU boots from it, through a copy-on-write overlay.
- **A Pi asked for with `--nextpi` that cannot start is an error; one enabled in
  Preferences is not.** The second is reported in a dialog and jnext starts
  without it, so a broken QEMU install cannot lock a user out of jnext.
  Declining the first-use download is neither: it is the user's choice, and
  jnext starts without the Pi either way. The policy is one pure function,
  `nextpi::start_outcome`, which `main()` applies.
- **Not in this PR:** a live start/stop from the running GUI, a progress bar for
  the unpack (it is a busy indicator; the download has the bar), and Windows
  (FIFOs, §3.7). The earlier manual-wiring flags (`--pi-uart-fifo`,
  `--pi-uart-pty`) were dropped in review.

## 3. Design

### 3.1 A `UartDevice` on UART 1

`UartDevice` (`peripheral/uart_device.h`) already names the Pi as UART 1's module,
and the ESP-01 already occupies UART 0 through the same seam. Attaching there gives
the link, without new UART code:

- guest → Pi at byte boundaries, after the guest's own baud has timed the byte
  (`UartDevice::receive`);
- Pi → guest paced into the 512-byte RX FIFO at UART 1's **current** programmed
  baud (`UartDevice::tick`, gated per instruction by `tick_wanted()`), so a burst
  from the Pi cannot overrun the FIFO;
- no loopback while attached;
- the joystick-connector mux's isolation of UART 1 (NR 0x0B, `zxnext.vhd:3340-3341`
  for RX, `:3526-3531` for TX), which `Uart::attach_device` and
  `UartChannel::deliver_tx_byte` already apply to any attached device.

### 3.2 The host half is reused, not copied

The joystick cable (`peripheral/joy_uart_link.*`, GH #252) already solved the host
side: non-blocking FIFO/pty endpoints that survive a peer coming and going, bounded
queues with backpressure, pacing off the receiving channel's baud, peer-loss
discard, a one-shot fault report, and an *inert* replay gate. `PiUartDevice` owns a
`JoyUartLink` and forwards to it; its `connector` is meaningless here and fixed at
0. The only addition to the link is `rx_pending()`, which the device mirrors into
its tick gate so an idle link costs the per-instruction path one `bool` load.

The joystick cable is *not* a `UartDevice` because it has no channel of its own
(its header explains why). The Pi has exactly one, so it is.

### 3.3 The NR 0xA0 GPIO gate

On the board, the Pi's UART pins (GPIO 14 and 15) reach UART 1 only through the
NR 0xA0 "Pi peripheral enable" mux (`zxnext.vhd:2278-2281`, reset `0x00` at
`:5080`). jnext already stores the register and names bit 5 `pi_uart_rxtx` and bit
4 `pi_uart_en`: one connects UART 1 to GPIO 14/15, the other chooses which way
round RX and TX are wired — for a Pi, or crossed for a Pi HAT.

The link is **connected only while both bits are set** (`(nr_a0 & 0x30) == 0x30`):

- with the UART not on the GPIO pins, neither side hears the other;
- with it on the pins but wired the other way round, the Next's TX drives the Pi's
  own TX pin and its RX listens to the Pi's RX pin, so again neither side hears
  the other.

*Inferred, and why it is safe:* which of bits 5 and 4 is the enable and which the
orientation does not change the gate — it requires both under either reading.
That `11` is the *Pi* orientation is cited from the consumer: `.pisend` 2.63 writes
`NEXTREG 0xA0,0x30` immediately before every exchange with the Pi (routine at
`$264F`, alongside its NR 0xA2 I2S set-up), and it works on real hardware.

When the gate is closed, Pi bytes are clocked out at UART 1's baud and dropped
(`JoyUartLink::dropped`), and guest bytes are dropped before the link
(`PiUartDevice::tx_disconnected`) — counted, not queued, because on hardware they
are gone. The first such loss is logged once at info level, because the common
case is benign (NextPi's boot log arriving before any program has set NR 0xA0),
and the uncommon one — a program that never sets it — is otherwise a silent no-op.

### 3.4 Lifetime, resets and replay

- **Owned by `Emulator`**, declared after `uart_`, so it dies first and the RX sink
  capturing `&uart_` cannot outlive the UART. A cold boot (placement-new at the same
  address) rebuilds it from the config, re-opening the endpoint — exactly the
  joystick cable's posture.
- **A soft reset does not rebuild it**: the Pi on the far end never sees a
  Next-side reset. NR 0xA0 does reset to 0x00, so the guest must route UART 1 to
  the Pi again, as on hardware.
- **Replay:** the device is held inert while a rewind re-executes frames or an RZX
  is played back — a re-sent byte would reach the Pi twice, and a re-read one
  would be stolen from the resumed timeline. Nothing is serialised.
- **The RZX recording machine** does not open the link (cleared in the recording
  boot config alongside the joystick cable's endpoints).

### 3.5 Launching NextPi under QEMU (`core/pi_qemu.*`)

`PiQemu` starts `qemu-system-arm -M raspi0` with the directory's kernel, device
tree and overlay, the kernel command line nextpi-sandbox established (the Pi USB
driver's FIQ path off, because it hangs USB enumeration under QEMU; QEMU's USB
sound card made ALSA's default, which is what NextPi's players open), and the
Pi's console UART on a QEMU **`pipe` chardev**. That chardev is a pair of FIFOs,
`<base>.in` (QEMU reads) and `<base>.out` (QEMU writes), which jnext creates in a
private temporary directory and the Emulator opens as the link's far end
(`JoyUartEndpoint::open_fifo_paths`). No pty, TCP port or `socat`.

- **Owned by `main()`, not by `Emulator`.** A hard reset rebuilds the Emulator;
  the Pi must not reboot with it (NextPi takes a minute to boot, and a real Pi
  does not see a Next reset). The rebuilt Emulator re-opens the same FIFO paths,
  and QEMU, which holds both ends read-write, never sees an end-of-file.
- **Started before the machine boots**, after every quick argument check (so a
  typo never triggers a download), and before `Emulator::init()` opens the FIFOs.
  QEMU exiting within 300 ms is reported as a start failure.
- **The overlay** (`overlay.qcow2`, 16 GB copy-on-write via `qemu-img`) exists
  because QEMU's raspi SD card must be a power-of-two size and NextPi's image is
  not; it also keeps the image read-only.
- **Never orphaned — the watchdog.** QEMU runs under a small `/bin/sh` script
  whose fd 3 is the read end of a pipe only jnext holds the write end of (both
  ends close-on-exec in jnext, so no other child can keep it open). When jnext
  goes away however it goes — SIGKILL included, when no destructor runs — the
  kernel closes the write end, the script's `read` returns, and QEMU gets
  SIGTERM. Without it a killed jnext left QEMU running and holding the
  overlay's lock, so the next start failed.
- **Stopped when jnext exits:** `stop()` closes that pipe (the same SIGTERM
  path), waits up to three seconds, then SIGKILLs the process group QEMU and its
  watchdog run in, and removes the FIFO directory. A reaper thread logs a QEMU
  that exits on its own; the watchdog exits with QEMU's status, so it still sees
  that.
- **A failed start cleans up:** an overlay that start created is removed;
  `qemu.log` is kept, because the error points at it.
- **Child environment:** inherited, because QEMU's audio back-ends need the
  session's variables, with `LANG=C` and `LC_ALL=C` set in the child only, per
  the project rule. Nothing parses QEMU's output: stdout and stderr go to
  `qemu.log`, and only exit statuses are consulted.
- **Child descriptors:** none of jnext's (the SD image, the FIFOs, sockets) —
  only stdin, stdout, stderr and the watchdog pipe. `POSIX_SPAWN_CLOEXEC_DEFAULT`
  on macOS, `posix_spawn_file_actions_addclosefrom_np` on glibc 2.34+, and
  elsewhere every other descriptor is marked close-on-exec before the spawn.

### 3.6 Provisioning NextPi (`core/nextpi_provisioner.*`)

`nextpi::provision` mirrors `sdcard::provision_sd_card` and uses its seams
(`DownloadFn`, `ConfirmFn`, `ProgressFn`, `BusyFn`), so `main()` wires the same
GUI dialogs (`SdcardGuiProvisioner`, now with configurable wording) or terminal
prompts to both.

- **Ready check:** the image, both boot files and a `release` marker. Matching
  the wanted release → done, no network.
- **Release names are validated** before they reach a file name or a URL:
  1-64 letters, digits, `.`, `_` and `-`, not starting with `.` or `-` — or
  `latest`. Names read from the mirror's listing go through the same check.
- **`latest`:** the mirror's index page is fetched and its `NextPi-<name>.tar.gz`
  links compared with `sort -V` ordering (`1_100` after `1_93D`).
- **Download:** the `.md5` first (tiny, and a release name the mirror lacks fails
  here, not after 6 GB), then the archive with progress. An archive left by an
  interrupted run is reused if its MD5 matches. A free-space check (~22 GB)
  precedes it.
- **Unpack:** the image is streamed out of the `.tar.gz` (zlib) by a small tar
  reader that handles what NextPi's GNU-format archive uses: the image is
  15 082 717 184 bytes, past the octal field's 8 GB, so its size is in GNU
  base-256 form; pax `path`/`size` records, GNU `L` long names and POSIX ustar
  `prefix` fields are handled too. A corrupt or hostile archive is an error, not
  an exception: name/pax records are capped at 1 MB and entries at 1 TB before
  anything is allocated or rounded.
  `kernel.img` and `bcm2708-rpi-zero.dtb` are copied out of the image's first
  FAT32 partition with the lenient `fat32_read_tree` (the boot partition is
  under the FAT32 cluster minimum, which that reader tolerates). On the real
  1_93D release they come out byte-identical to `mtools`' extraction.
- **Install:** unpacked into `*.part` names first, so a failure while
  downloading or unpacking leaves any previous release untouched and usable.
  The swap itself cannot be atomic (a directory of files cannot be replaced in
  one step) but it is crash-safe: the `release` marker is removed first and
  written last (to a temporary name, renamed over), so an interrupted install is
  re-done at the next start rather than mistaken for a complete one. A release
  change discards the old `overlay.qcow2`, which only makes sense over the image
  it was made on. The archive and its `.md5` are deleted.
- **Declining** with nothing installed → no Pi; with an older release installed
  → that one is used, with a warning.
- **Test seam:** `$JNEXT_NEXTPI_MIRROR` replaces the mirror URL (as
  `$JNEXT_SDCARD_DISTRO_URL` does for the SD image); `file://` URLs work, which
  is how the real release was provisioned offline during development.

### 3.7 Platforms

POSIX only: the link is a FIFO pair. On Windows `--nextpi` is refused and the
provisioner returns "not supported on Windows"; the code compiles there.

## 4. Testing

`test/uart/uart_integration_test.cpp`, group **PI** (plan:
`doc/testing/UART-I2C-TEST-PLAN-DESIGN.md`, Group 17). PI-01..05 drive the UART
link over real FIFOs; PI-06..09 run `PiQemu` against a shell-script stand-in for
QEMU that answers on the pipe chardev as QEMU does; PI-10..14 run the provisioner
offline against a fake mirror whose archive is built in the test (a GNU tar with a
base-256 size and a pax path, holding a tiny MBR disk with a hand-made FAT32 boot
partition); PI-15..27 cover what review found untested. No QEMU, network or
NextPi image is needed.

| Row | Proves |
|-----|--------|
| PI-01 | both directions over UART 1 with NR 0xA0 = 0x30; UART 0 untouched |
| PI-02 | the NR 0xA0 gate: 0x00, 0x10 and 0x20 carry nothing either way (counted); 0x30 does |
| PI-03 | no loopback on UART 1 while the Pi is attached |
| PI-04 | NR 0x0B's joystick mux isolates the Pi in both directions |
| PI-05 | a soft reset keeps the same device attached; NR 0xA0 resets to 0x00 |
| PI-06 | the QEMU command line: raspi0, kernel, dtb, overlay, pipe chardev, audio spec |
| PI-07 | refusals: incomplete directory, QEMU not installed, QEMU exiting at once — nothing left running |
| PI-08 | end to end with the stand-in: overlay and FIFOs created, guest reads `SUP> `, Pi hears the guest; stop is SIGTERM-prompt and removes the FIFOs |
| PI-09 | a rebuilt Emulator (hard reset) reaches the same running Pi through the same FIFOs |
| PI-10 | the mirror listing parse and release ordering |
| PI-11 | the tar reader: pax path, base-256 size, byte-exact output; a missing entry is an error |
| PI-12 | first use: asked once, MD5 + archive fetched, image and boot files installed, archive deleted; the next run asks and fetches nothing |
| PI-13 | declining installs nothing; an MD5 mismatch deletes the download and installs nothing |
| PI-14 | a release change replaces the install after asking and discards the overlay; `latest` picks the newest; offline it keeps the installed one with a warning |
| PI-15 | the replay gate: during a rewind/RZX replay nothing reaches the Pi and nothing is read from it; afterwards both flow |
| PI-16 | the warm-start recording boot gets no NextPi FIFOs |
| PI-17 | a QEMU ignoring SIGTERM is SIGKILLed with its watchdog after the grace period |
| PI-18 | the child runs with `LANG=C` / `LC_ALL=C`; jnext's own locale is untouched |
| PI-19 | the child inherits none of jnext's descriptors (one held at fd 57 is not open in it) |
| PI-20 | SIGKILLing the process running NextPi stops QEMU (the watchdog) |
| PI-21 | too little free space: refused with the amounts, nothing fetched |
| PI-22 | GNU `L` long names and POSIX ustar prefixes give the entry its full name |
| PI-23 | absurd long-name, pax and entry sizes fail as "malformed", no exception |
| PI-24 | a failed upgrade leaves the installed release prepared and intact, no partial files |
| PI-25 | release names with `/`, spaces, a leading dot or URL syntax are refused before anything is asked or fetched |
| PI-26 | a failed start removes the overlay it created and keeps `qemu.log` |
| PI-27 | the start policy table (`nextpi::start_outcome`) |

`main()` applying that policy is the functional regression row **nextpi-func**
(`test/00regression/scripts/`): through the real binary, `--nextpi` with no QEMU
on its `PATH` exits 1 with the install hint, a declined download starts jnext
without NextPi (exit 0, nothing fetched), and `--no-nextpi` is accepted.

Settings: `test/gui/app_config_test.cpp` AC-71..74 (`[nextpi]` defaults and
round-trip) and `test/gui/preferences_apply_test.cpp` PA-20a..e (the tab's
controls, their enable-follows-tick behaviour, Apply, and an untouched dialog
handing every field back unchanged).

Discrimination (all measured): removing `setup_pi_uart()` from `init()` fails
PI-01..05; removing the NR 0xA0 probe fails PI-02; in `PiQemu`, swapping the FIFO
ends fails PI-08/09, skipping the overlay fails PI-08, dropping the SIGTERM fails
PI-08; in the provisioner, reading sizes as octal only fails PI-11/12/14, keeping
the overlay on a release change fails PI-14, ignoring the MD5 fails PI-13, and
re-downloading a ready directory fails PI-12. For the review rows: no replay gate
fails PI-15; not clearing the warm-start FIFO fails PI-16; SIGKILLing only the
watchdog fails PI-17; no `LANG=C` fails PI-18; inherited descriptors fail PI-19;
no watchdog fails PI-08/20/26; no space check fails PI-21; ignoring `L` records
or the ustar prefix fails PI-22; no metadata bound makes the suite die (a 1 TB
allocation); no name validation fails PI-25; leaving the overlay fails PI-26;
"always exit" fails PI-27.

## 5. Not done

- **No Pi emulation.** The Pi is real NextPi software under QEMU.
- **No I2S audio.** NextPi's audio reaches the Next's mixer over I2S (NR 0xA2) on
  real hardware; under QEMU it plays on the host instead.
- **No GPIO, I2C1 or SPI0 to the Pi.** Only the UART crosses the wire.
- **No live start/stop** from the running GUI; Preferences changes apply at the
  next launch.
