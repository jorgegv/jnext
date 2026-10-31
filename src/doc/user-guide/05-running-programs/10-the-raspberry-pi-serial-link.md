# 5.10 NextPi on the Raspberry Pi

**NextPi** is the software that runs on the Raspberry Pi of the Spectrum Next,
the small computer on its GPIO header. The Next talks to it over its second
UART, and NextZXOS comes with the tools that use it: `.pisend`, `.piget`,
`.piput`, the NextPi UI, and programs that hand speech, music and tape
streaming to the Pi.

JNEXT does not emulate the Pi. It does the next best thing: it runs a real
NextPi under **QEMU** and connects it to the Next's UART, so those tools talk to
the genuine article.

## Turning it on

You need **QEMU** installed — on macOS `brew install qemu`; on Fedora
`sudo dnf install qemu-system-arm qemu-img`; on Debian or Ubuntu
`sudo apt install qemu-system-arm qemu-utils`; on Windows install QEMU for
Windows and either add its folder to your `PATH` or set the QEMU binary under
**Settings ▸ Preferences ▸ NextPi**. On Windows the Pi is reached over a local
(loopback) socket, which may raise a firewall prompt the first time. Then either tick **Start
NextPi** under **Settings ▸ Preferences ▸ NextPi** and restart JNEXT (the tick
starts NextPi when JNEXT starts the ZX Next), or start it for one run, with any
machine, with:

```
jnext --nextpi
```

JNEXT checks for QEMU (and, before the first start, `qemu-img`) first, and
stops with that install hint if it is missing, before offering or downloading
anything.

The first time, JNEXT offers to download NextPi, the way it offers to download
the SD card image: about 6 GB, with a progress bar, into `~/.jnext/nextpi`. It
needs about 22 GB free while the image is unpacked, and about 15 GB afterwards.
Every later start skips all of that. If you say No, JNEXT starts without NextPi,
even with `--nextpi`, and asks again next time.

If the download stops (network trouble, Cancel, closing JNEXT), the part already
fetched is kept as `NextPi-<release>.tar.gz.part` in the NextPi directory, and the
next start continues from there after asking; delete that file to start over.
A download that finished but could not be put in place is used as it is on the next
start.

If no data arrives for a few seconds, the progress dialog says the download has stalled and for
how long; after 30 seconds without data JNEXT stops waiting and says why. Cancel works at any
time.

Checking the mirror (the list of releases, the checksum) shows the same progress dialog, stall
notice included, when it takes more than a second. Without the dialog (`--headless`, or a build
with no Qt window) a check that takes more than a second prints one line on the terminal and in
the log, naming what it is doing and the address (`Fetching the NextPi checksum...` and the
address of the checksum), and any download that gets no data for five seconds prints the same
stalled notice as the dialog; a quick check prints nothing. When any of these fails, JNEXT says which
address it was fetching, what went wrong in plain words (with the error code after it) and what
it does next, and in the graphical version the message is in a dialog whose text can be selected
and copied: try the address in a web browser to see whether the mirror can be reached at all.

### If you already have NextPi, or prepare it by hand

Put `NextPi-<release>.tar.gz` from the mirror (and, if you have it, its `.md5`)
in the NextPi directory (`~/.jnext/nextpi`; on Windows `%USERPROFILE%\.jnext\nextpi`).
JNEXT checks it against the MD5 and unpacks it without downloading, then deletes
the archive.

To prepare the directory by hand instead: `nextpi.img` is the `.img` inside the
archive, renamed; `boot/kernel.img` and `boot/bcm2708-rpi-zero.dtb` are copied
from the image's first (FAT) partition; and a file `release` holds the release
name, which must be the NextPi release set in Preferences (default `1_93D`).

When JNEXT cannot use what is there, the question and the log say which file is
missing or which check failed. A run started from an icon shows no log: add
`--log-file FILE` to its shortcut to keep one.

NextPi takes about a minute to boot. Then, at the NextZXOS command line:

```
.pisend -q
.pisend -c nextpi-play_speech "Hello from the Spectrum Next"
```

The first finds the Pi; the second asks it to speak. On Linux and macOS the Pi's
sound goes through the Next's own mixer, as on the real machine: it plays with the rest of the
Next's sound, a video or WAV recording of the session includes it, and NextREG
`0xA2` turns it on and off (NextPi's tools turn it on themselves). It arrives a
little late — about a tenth of a second — and at the 10-bit quality of the
Next's input. With bit 0 of NextREG `0xA2` set, the Pi's sound goes to the EAR
input instead, the tape signal port `0xFE` bit 6 reads, which is how a Next loads
a tape the Pi plays to it.

QEMU runs in the background for as long as JNEXT does, and stops with it. A
soft or hard reset of the Next leaves the Pi running, as it would on the real
machine. Changing the machine neither starts nor stops it. `--no-nextpi`
leaves the Pi off for one run even when the preference is ticked.

## The NextPi settings

Everything except the on/off tick has a sensible default, and an empty field in
the Preferences tab means that default:

| Setting | Default | What it is |
|---|---|---|
| NextPi directory | `~/.jnext/nextpi` | where the download goes and NextPi runs from |
| NextPi release | `1_93D` | a release name on the NextPi mirror, or `latest` for the newest one there |
| QEMU | `qemu-system-arm` on your `PATH` | the QEMU to run; `qemu-img` is expected beside it |
| Pi audio | the Next's mixer | or `host` to play it on your computer's default output (`pa` on Linux, `coreaudio` on macOS, `dsound` on Windows), a QEMU audio driver to play it straight to your computer, `none` to mute it, or `wav:FILE` to record it on its own |
| Extra QEMU arguments | none | added after JNEXT's own when NextPi starts |

Each setting also has an optional command-line flag, for one run and over the
saved value, in a headless session too: `--nextpi-dir DIR`, `--nextpi-release
NAME`, `--nextpi-qemu PROGRAM`, `--nextpi-audio SPEC` and `--nextpi-qemu-args
"ARGS"`. An empty value means the default. They go with `--nextpi` (or a saved
preference that starts NextPi), which alone starts NextPi with the defaults.
Given when NextPi is not enabled for the run, a flag is ignored and JNEXT warns
in its log, naming it.

Changes made in Preferences take effect the next time JNEXT starts. Changing the release makes the
next start offer to download that one in place of the installed one. With
`latest`, JNEXT checks the mirror each time it starts; if it cannot reach it, it
carries on with the release it has, and says so, with the address it tried.

The Pi's SD card image is never changed: everything NextPi writes goes to
`overlay.qcow2` in the NextPi directory. Delete that file to start again from a
pristine image. QEMU's own messages go to `qemu.log` there too, which is the
place to look if the Pi never answers.

### Extra QEMU arguments

The box under *Extra QEMU arguments* (or `--nextpi-qemu-args "..."`) is added to the end of the QEMU command
line when NextPi starts. It is split into arguments on spaces, so one box
holding `-serial telnet:127.0.0.1:2357,server,nowait` is two arguments,
`-serial` and the address. Single or double quotes keep spaces inside one
argument (`-name 'my pi'`); there are no escapes, so a backslash is an ordinary
character and a Windows path can be pasted as it is. Nothing is expanded
(`$HOME`, `~` and `*` stay as typed) and no shell is involved. A quote that is
never closed stops NextPi from starting, with a message naming where it opens.
When NextPi starts, the log shows how the box was split: `NextPi: 2 extra QEMU
arguments, after JNEXT's own: "-serial" "telnet:127.0.0.1:2357,server,nowait"`.

The arguments come after JNEXT's own, so a `-serial` of yours is the Pi's second
serial port (its mini UART, `ttyS0`) and the NextPi console stays where JNEXT
connected it. That makes a second telnet console possible:
`-serial telnet:127.0.0.1:2357,server,nowait`. Write the address as shown:
`telnet::2357` listens on every network interface, and without `nowait` QEMU
waits for a client to connect before it boots the Pi. NextPi as shipped runs
nothing on `ttyS0`, so the port accepts a connection but stays silent until you
set up a login (a `getty`) on it inside NextPi.

JNEXT does not check the arguments. One that conflicts with JNEXT's own (a
second `-M` or `-kernel`, a `-chardev` with the id `pi`) can stop NextPi, and
QEMU's reason is in `qemu.log`. Like every NextPi setting, the box is read only
by sessions with a window, not by `--headless` (use `--nextpi-qemu-args` there).

## When nothing arrives

On a real Next the Pi's UART pins only reach the Next once a program sets
NextREG `0xA0` to `0x30`, and JNEXT follows that: until then the Pi's bytes are
lost and the Next's never reach it. NextPi's own tools set it themselves; a
program of your own has to do the same. The first time traffic is lost this
way, JNEXT says so once in its log. Everything the Pi prints while it boots is
lost the same way, which is harmless: `.pisend -q` starts a fresh conversation.

The Pi also cannot be heard while NextREG `0x0B` gives the same UART to the
joystick port ([5.7](07-the-joystick-port-serial-cable.md)).

## What it does not do

- It is not saved in snapshots. Rewinding and RZX playback do not resend
  anything to the Pi, and the Pi is not rewound either.
- Only the serial link and the sound cross over, not the Pi's other GPIO pins.
