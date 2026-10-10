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
| `audio` | Pi audio (editable: empty, a QEMU `-audiodev` driver, `none`, `wav:FILE`) | the Next's mixer (§3.7); a driver plays it straight to the host |

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
  (FIFOs, §3.8). The earlier manual-wiring flags (`--pi-uart-fifo`,
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
  ends close-on-exec in jnext, so no other child can keep it open — set
  atomically by `pipe2(O_CLOEXEC)` where it exists, Linux and FreeBSD). When jnext
  goes away however it goes — SIGKILL included, when no destructor runs — the
  kernel closes the write end, the script's `read` returns, and QEMU gets
  SIGTERM. Without it a killed jnext left QEMU running and holding the
  overlay's lock, so the next start failed.
- **Stopped when jnext exits:** `stop()` closes that pipe (the same SIGTERM
  path), waits up to three seconds, then SIGKILLs the process group QEMU and its
  watchdog run in, and removes the FIFO directory. A reaper thread logs a QEMU
  that exits on its own; the watchdog exits with QEMU's status, so it still sees
  that.
- **A failed start cleans up:** an overlay that start created is removed (one
  that was already there holds NextPi's saved state and is kept); `qemu.log`
  is kept, because the error points at it. No usable temporary directory for
  the FIFOs (`$TMPDIR` naming none) is such a failure, with an error that says
  so.
- **Child environment:** inherited, because QEMU's audio back-ends need the
  session's variables, with `LANG=C` and `LC_ALL=C` set in the child only, per
  the project rule: any `LANG` or `LC_ALL` jnext has is dropped first, so each
  appears exactly once (`PiQemu::child_environment`). Nothing parses QEMU's output: stdout and stderr go to
  `qemu.log`, and only exit statuses are consulted.
- **Child descriptors:** none of jnext's (the SD image, the FIFOs, sockets) —
  only stdin, stdout, stderr and the watchdog pipe. `POSIX_SPAWN_CLOEXEC_DEFAULT`
  on macOS, `posix_spawn_file_actions_addclosefrom_np` on glibc 2.34+, and
  elsewhere every other OPEN descriptor — listed from `/proc/self/fd` or
  `/dev/fd`, not every number up to a possibly huge `ulimit -n` — is marked
  close-on-exec before the spawn (`PiQemu::mark_close_on_exec_except`). The
  list leaves out the directory's own descriptor, closed by the time the list
  is acted on (`PiQemu::open_descriptors`). Only
  if neither list can be read does it walk the numbers, up to the descriptor
  limit capped at 65536 (and to 65536 when `sysconf` reports no limit, -1).

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
  anything is allocated or rounded, and a pax record's length is checked against
  what is left of its header (so `2^64-1` cannot wrap and reach the next one).
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

### 3.7 The Pi's sound into the Next's mixer (`audio/pi_audio.*`)

Added after #310, by agreement with the owner, in its own PR. On a real Next the
Pi's audio enters the FPGA over I2S and is summed into the mixer, gated by NR
0xA2 (`zxnext.vhd:2283-2290`, `:2358-2359`). jnext already modelled that side —
`I2s` and the mixer's 10-bit term (MX-06/07) — with nothing feeding it, which
is why MX-30 had been retired ("no Raspberry Pi to be the producer"). NextPi
under QEMU is one, so:

- **QEMU writes the Pi's sound to a FIFO.** With the default audio setting,
  `PiQemu` passes `-audiodev wav,...,path=<base>.audio,out.frequency=44100,
  out.channels=2,out.format=s16`: a 44-byte WAV header, then 16-bit stereo PCM
  at the mixer's own rate. Measured against real NextPi 1_93D: nothing is
  written while the Pi is silent, and while it plays the stream arrives in
  bursts averaging 44 100 frames/s.
- **`PiAudio` reads it** on a thread of its own into a lock-free single-producer
  single-consumer ring, so QEMU never blocks on a full pipe — not even while
  jnext is paused. The reader is opened before QEMU starts, because QEMU's open
  of its end blocks until a reader exists. It skips each writer's 44-byte WAV
  header and reassembles frames split across reads. A header is found two ways:
  after the EOF a departing writer leaves, and in-stream, at a frame boundary,
  by `RIFF`...`WAVE`, for a writer that follows the last with no read in
  between (bytes that may still be a header wait for the next read).
- **The emulator latches one frame per mixer output sample**
  (`Emulator::feed_pi_audio`, from `advance_audio` at each sample boundary),
  converted to the hardware's 10-bit offset binary (`PiAudio::to_i2s`, 0 → 0x200
  per `i2s.vhd:177-180`). From there the existing model applies unchanged: NR 0xA2
  enables, mutes and routes it, it is summed with the beeper, AY and DAC, and
  `--record` / `--wav-record` capture it.
- **Clock drift.** QEMU runs on the host's clock, the mixer on the emulated
  one. The consumer prebuffers 50 ms before playing, plays silence (0x200) on an
  underrun and prebuffers again, and trims a backlog beyond 300 ms back to
  100 ms by dropping the oldest frames, so latency stays near 100 ms. A pause
  longer than the ring (about 3 s) overflows it; the producer then drops new
  frames and flags it, and the consumer's next pop flushes the stale ring and
  prebuffers fresh audio, rather than replaying a moment from inside the pause.
- **Replay.** RZX playback does not consume the stream (the Pi's output belongs
  to the live session) and holds the input silent; a rewind's replay never
  advances audio at all.
- **The rest value.** `I2s` resets to 0x200, offset-binary silence, on every
  machine with or without a Pi: the receiver resets its words to 0
  (`i2s_receive.vhd:129-130`) and `i2s.vhd:177-180` inverts the sign bit.
- The other settings keep sending the Pi's sound to the host or a file
  directly, bypassing the mixer: `host` (the platform's default output, `pa`
  on Linux and `coreaudio` with its buffer tuning on macOS), a QEMU driver
  name (`coreaudio`, `pa`, ..., `none`) or `wav:FILE`.

End to end with real QEMU, a headless run typed `nextpi-play_speech` into the
UART with NR 0xA0 = 0x30 and NR 0xA2 = 0xC0: the `--wav-record` file is silent
until the command and carries the speech after it.

### 3.8 Platforms

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
| PI-18 | the child runs with `LANG=C` / `LC_ALL=C` (each once); jnext's own locale is untouched |
| PI-19 | the child inherits none of jnext's descriptors (one held at fd 57 is not open in it, nor the watchdog's pipe at fd 3) |
| PI-20 | SIGKILLing the process running NextPi stops QEMU (the watchdog) |
| PI-21 | too little free space: refused with the amounts, nothing fetched |
| PI-22 | GNU `L` long names and POSIX ustar prefixes give the entry its full name |
| PI-23 | absurd long-name, pax header, entry and pax `size=` sizes each fail with their own "malformed" message, no exception |
| PI-24 | a failed upgrade leaves the installed release prepared and intact, no partial files |
| PI-25 | release names with `/`, spaces, a leading dot or URL syntax are refused before anything is asked or fetched |
| PI-26 | a failed start removes the overlay it created and keeps `qemu.log` |
| PI-27 | the start policy table (`nextpi::start_outcome`) |
| PI-28 | an install failing part-way (image or marker step) leaves no `release` marker |
| PI-29 | names in the mirror's listing are validated; `latest` never fetches an invalid one |
| PI-30 | `$TMPDIR` naming no directory: start fails with an error, no exception |
| PI-31 | a pax record whose length runs past its header is "malformed", not wrapped |
| PI-32 | a failed start keeps an overlay that was already there |
| PI-33 | `PiQemu::child_environment`: LANG / LC_ALL dropped, then each set to C once |
| PI-34 | QEMU's own exit status reaches jnext through the watchdog |
| PI-35 | every descriptor a start leaves open in jnext is close-on-exec |
| PI-36 | the replay gate during an RZX playback |
| PI-37 | the close-on-exec fallback marks open descriptors (fd 3 and the walk's last number included), spares the one kept and leaves 0-2 alone |
| PI-38 | whether NextPi starts (`nextpi::start_request`): CLI over Preferences, headless ignores them |
| PI-39 | a pax record after a valid one claiming more than is left (99, 2^64-1, 30 with a `\n` in the padding) is "malformed" |
| PI-40 | a pax record not ending on `\n` is "malformed" |
| PI-41 | a pax record whose length ends before its first space is "malformed" |
| PI-42 | a pax record with no space after its length is "malformed" |
| PI-43 | a `release` marker that cannot be removed stops the install before the image is put in place |
| PI-44 | the release-name length limit: 64 characters valid, 65 not |
| PI-45 | the fallback's number walk does the same; its limit is `sysconf`'s capped at 65536, and 65536 for -1 or 0 |
| PI-46 | the fallback reads `/proc/self/fd` where it exists, else `/dev/fd`, and walks the numbers only with no list |
| PI-47 | the open-descriptor list names open descriptors but not the directory's own, and leaves no descriptor open (reading it, or marking from it) |
| PI-48 | the Pi's sound reaches the mixer through QEMU's wav FIFO and `PiAudio`; NR 0xA2 = 0x00 silent, 0xC0 the stand-in's square wave, each channel on its own side at its exact level (left ±1024, right ±512) |
| PI-49 | neither a rewind replay nor an RZX playback consumes the stream; a replay produces no audio at all, an RZX playback outputs exactly 0 in both channels; the I2S input resets to 0x200 with or without a Pi; live again it is drawn at exactly one frame per mixer output sample |
| PI-50 | the warm-start recording boot gets no audio reader |
| PI-51 | when the audio FIFO cannot be created, `start()` fails with that error and starts no QEMU |
| PI-52 | the mixer's `-audiodev` value escapes a comma in the FIFO path |
| PI-53 | a QEMU driver setting (`none`) is passed to QEMU, and jnext makes no audio reader or FIFO |
| PI-54 | `host` gives QEMU the platform's default output; empty still means the mixer |

`test/audio/audio_test.cpp` adds **MX-41** (the stream retired MX-30 asked for; MX-30 itself stays retired, its ID not reused) and MX-31..40 for `PiAudio`
against a real FIFO: the stream frame for frame (header and frames split across
writes), the 10-bit mapping, a frame's level in the mix and the received count;
prebuffer and underrun (2205 frames, 50 ms); the latency trim (to 4410 frames,
100 ms) and its edge (13230 frames, 300 ms, kept; 13231 trimmed); the ring full
(131072 frames); a pause longer than the ring, flushed on resume; a writer reconnecting with a
fresh header; the open errors and
the FIFO's 0600 mode; the reader pausing, not spinning, while there is no
writer; a live writer's pause (EAGAIN) not ending the stream; one descriptor
on the FIFO, close-on-exec, released by `close()`; and a signal interrupting the
reader's `poll()` not ending it. The latency
and capacity rows assert the literal numbers, so changing a constant fails
them.

`main()` applying that policy is the functional regression row **nextpi-func**
(`test/00regression/scripts/`): through the real binary, `--nextpi` with no QEMU
on its `PATH` exits 1 with the install hint, and a declined download starts jnext
without NextPi (exit 0, nothing fetched). In a GUI session (Qt's offscreen
platform) whose preference enables NextPi, still with no QEMU, `--no-nextpi`
keeps it from even being tried, and without the flag the failure is logged and
jnext runs to its automatic exit (0) — the Preferences-only path. On the
offscreen platform the warning dialog is not shown, since nobody could dismiss
it; the log line before it carries the message. Its fifth fact is the sound end
to end: a stand-in QEMU on the `PATH` plays a square wave into the wav FIFO, a
6-byte program opens NR 0xA2, and `--wav-record` must carry the tone with each
channel at its own level. That is the one place `main()` handing PiQemu's reader
to the emulator (`cfg.pi_audio`) is tested; setting it to null fails the row.
Its sixth fact is the saved audio preference: a GUI session with `[nextpi]
audio=none` must start the stand-in QEMU with `-audiodev none`, not the mixer's
wav FIFO, which is the one test of `main()` copying that preference into the
start (`spec.audio = saved.nextpi_audio`); dropping that line fails the row.

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
"always exit" fails PI-27. Round 2, each failing exactly its row: dropping the
entry or the pax `size=` bound fails PI-23; not removing the marker first, or
writing it in place, fails PI-28; an unchecked listing fails PI-29; the throwing
`temp_directory_path` fails PI-30; the old pax length check fails PI-31;
removing an overlay the start did not create fails PI-32; not filtering
LANG/LC_ALL fails PI-33; the watchdog exiting 0 fails PI-34; an inheritable
write end fails PI-35; no RZX half in the gate fails PI-36; a fallback that
marks nothing fails PI-37; `asked_on_cli` true for `--no-nextpi` fails PI-38.
In **nextpi-func**, `start_outcome(true, …)` in `main()` fails fact 4 (exit 1)
and ignoring `--no-nextpi` fails fact 3. For the sound: not calling `feed_pi_audio`
fails PI-48/49, and calling it but not latching the frame into `I2s` fails
PI-48; feeding twice per output sample (the Pi at double speed) fails PI-49;
consuming during RZX playback fails PI-49, and so does an `I2s` reset value
other than 0x200; swapping the channels, feeding one from the other, or a one-step error in
either channel's 10-bit value fails PI-48; a
wrong rest value in either channel fails PI-49; keeping the reader in the
warm-start config fails PI-50; ignoring the audio FIFO's open error fails PI-51.
In `PiAudio`: not skipping the WAV header fails MX-31..34, MX-36, MX-41 and PI-49,
and skipping one byte too few fails MX-32/34/36/41 and PI-48 (MX-31 never looks at
sample values); prebuffering at `<=` fails MX-31; no trim fails MX-32; changing
the prebuffer by one frame fails MX-31, the target MX-32/36, and the maximum
latency MX-36, and trimming at `>=` fails MX-36; an off-by-one in the ring-full
check, not counting its drops, or a different capacity fails MX-33; not
flushing the ring after it overflowed fails MX-42; not resetting the
header skip or the half-frame carry when the writer goes fails MX-34; not
recognising a header in-stream, or taking a partial one for a frame, fails
MX-43; a FIFO
made 0666, or a regular file accepted, fails MX-35; no pause after a read that
finds no writer fails MX-37 on Linux (macOS's `poll()` waits anyway); treating
EAGAIN as the writer's end fails MX-38; no close-on-exec, a leaked descriptor
on re-open or close, or a stale one after `close()`, fails MX-39; giving up on a
`poll()` error (a signal's EINTR) fails MX-40; not escaping the FIFO path for
QEMU fails PI-52; ignoring a driver setting, or making the reader for one, fails
PI-53; not mapping `host` to the platform default fails PI-06 and PI-54.

## 5. Not done

- **No Pi emulation.** The Pi is real NextPi software under QEMU.
- **No separate gain for the Pi.** Its level is the hardware's share of the mix
  (a 10-bit input among the 13-bit sum); the master gain raises it with the rest.
- **No GPIO, I2C1 or SPI0 to the Pi.** Only the UART crosses the wire.
- **No live start/stop** from the running GUI; Preferences changes apply at the
  next launch.
