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
`sudo apt install qemu-system-arm qemu-utils`. Then either tick **Start
NextPi** under **Settings ▸ Preferences ▸ NextPi** and restart JNEXT, or start
it for one run with:

```
jnext --nextpi
```

The first time, JNEXT offers to download NextPi, the way it offers to download
the SD card image: about 6 GB, with a progress bar, into `~/.jnext/nextpi`. It
needs about 22 GB free while the image is unpacked, and about 15 GB afterwards.
Every later start skips all of that. If you say No, JNEXT starts without NextPi,
even with `--nextpi`, and asks again next time.

NextPi takes about a minute to boot. Then, at the NextZXOS command line:

```
.pisend -q
.pisend -c nextpi-play_speech "Hello from the Spectrum Next"
```

The first finds the Pi; the second asks it to speak. The Pi's sound goes through
the Next's own mixer, as on the real machine: it plays with the rest of the
Next's sound, a video or WAV recording of the session includes it, and NextREG
`0xA2` turns it on and off (NextPi's tools turn it on themselves). It arrives a
little late — about a tenth of a second — and at the 10-bit quality of the
Next's input.

QEMU runs in the background for as long as JNEXT does, and stops with it. A
soft or hard reset of the Next leaves the Pi running, as it would on the real
machine. `--no-nextpi` leaves the Pi off for one run even when the preference
is ticked.

## The NextPi settings

Everything except the on/off tick has a sensible default, and an empty field in
the Preferences tab means that default:

| Setting | Default | What it is |
|---|---|---|
| NextPi directory | `~/.jnext/nextpi` | where the download goes and NextPi runs from |
| NextPi release | `1_93D` | a release name on the NextPi mirror, or `latest` for the newest one there |
| QEMU | `qemu-system-arm` on your `PATH` | the QEMU to run; `qemu-img` is expected beside it |
| Pi audio | the Next's mixer | or `host` to play it on your computer's default output (`pa` on Linux, `coreaudio` on macOS), a QEMU audio driver to play it straight to your computer, `none` to mute it, or `wav:FILE` to record it on its own |

Changes take effect the next time JNEXT starts. Changing the release makes the
next start offer to download that one in place of the installed one. With
`latest`, JNEXT checks the mirror each time it starts; if it cannot reach it, it
carries on with the release it has.

The Pi's SD card image is never changed: everything NextPi writes goes to
`overlay.qcow2` in the NextPi directory. Delete that file to start again from a
pristine image. QEMU's own messages go to `qemu.log` there too, which is the
place to look if the Pi never answers.

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
- It works on Linux and macOS only.
