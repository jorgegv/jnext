# 5.7 The joystick-port serial cable

A ZX Spectrum Next can be made to speak serial out of a **joystick socket**.
Writing to NextREG `0x0B` takes pin 7 away from the joystick logic and hands it
to one of the two UARTs, and a home-made DB9 cable then connects that pin —
and the socket's button-C pin coming back the other way — to a PC. It is how
[dezogif](https://github.com/jorgegv/dezogif_ng) and the **DeZog** debugger talk
to a real Next while a program is running on it.

JNEXT can be the Next end of that cable. There are two forms, and which one you
want depends on whether you need a conversation or a recording.

## A live cable, for a debugger

```
jnext --joy-uart-pty --load yourprogram.nex
```

JNEXT allocates a pseudo-terminal and prints the device to open:

```
joystick serial cable attached to joy 2 — pty /dev/pts/7; ...
```

Point your serial client at `/dev/pts/7` and you are connected. It is an
ordinary serial device in raw mode, so DeZog's `serial` remote, `picocom`,
`screen` and anything else that opens a tty will work with it. The device name
changes every run, so read it from that line rather than hard-coding it.

If you would rather wire it up from a shell script, use named pipes instead:

```
jnext --joy-uart-fifo /tmp/cable --load yourprogram.nex
```

That creates (or reuses) two FIFOs, named from the **guest's** point of view:

| Path            | Direction       |
|-----------------|-----------------|
| `/tmp/cable.rx` | host → Next     |
| `/tmp/cable.tx` | Next → host     |

```
printf 'ABC' > /tmp/cable.rx     # send to the program
cat /tmp/cable.tx                # read what it sends back
```

**Nothing ever blocks the emulator.** You can start JNEXT before the program on
the other end, stop that program and start it again, or never attach one at
all — the machine keeps running at full speed throughout. While no peer is
attached there is simply nothing to receive, and what the guest transmits waits
for one. If a peer that *was* attached goes away, whatever was still in flight
is discarded rather than delivered to whoever connects next, because half a
message is worse than none; a peer that reconnects starts cleanly from there.

## A recording, for an automated test

```
jnext --joy-uart-rx bytes.bin --joy-uart-rx-delay-frames 200 --load yourprogram.nex
```

This sends a fixed file one way only, on a schedule you choose. It is the right
tool for a repeatable test — nothing on the host has to be running, and the same
bytes arrive at the same moment on every run — and the wrong tool for a
debugger, which needs to hear an answer. The two cannot be combined: one cable,
one socket.

## What the guest has to do, and what happens if it does not

The bytes go through NextREG `0x0B`, which the *program* controls, so a cable is
only heard while the program has asked for it:

- **bits 7 and 5** must both be set — pin 7 in one of its two UART modes. The
  other two modes carry no UART at all;
- **bit 4** selects which socket the machine listens to: `0` = joy 1, `1` = joy
  2. Tell JNEXT which socket your cable is in with `--joy-uart-connector 1|2`
  (default `2`, which is what a real rig uses);
- **bit 0** picks the channel: `0` = UART 0, `1` = UART 1. While the cable owns
  a channel, the module on that channel's header — the emulated ESP-01 on UART
  0 — can neither be heard nor be spoken to, exactly as on hardware.

Bytes that arrive while the program is not listening are **lost**, not queued,
because that is what happens on a wire. (This is why the recording form has a
delay option: a stream with no delay is usually spent before the program has got
round to setting `0x0B` up.)

Receiving is paced at the baud rate the program itself programmed, so a large
message from the host cannot overrun the machine's 512-byte receive buffer.
Transmitting is not connector-selected: the Next presents pin 7 to both sockets
in turn, so a cable in the socket bit 4 does *not* select can still hear the
machine even though the machine cannot hear it. That is the hardware's
asymmetry, not JNEXT's.

## Three things to know before you rely on it

**Rewinding breaks a live session.** The debugger's rewind, and RZX playback,
re-execute instructions the program has already run. JNEXT holds the cable
completely still while that happens — it will not send a byte to your debugger
twice, and it will not swallow one you sent — but it cannot rewind the program
at the *other* end of the cable, which still believes everything it was told.
Expect to restart the session on the host side after a rewind.

**A hard reset re-plugs the cable.** **Machine > Power Reset** (F1) rebuilds the
whole machine, and the cable with it — which is right, because a power reset is
what a cable being pulled out and pushed back in looks like. Either way your own
end breaks and has to be reopened: with `--joy-uart-fifo` the pipes keep their
names but the descriptors you were holding stop working, and with
`--joy-uart-pty` the pseudo-terminal is a *new* device, so read the new path
from the startup line that the reset prints. A soft reset (the machine's own
NextREG 0x02 reset) does not touch the cable at all, which is also what the
hardware does.

**It is POSIX-only.** Neither form exists on Windows; JNEXT says so and stops
rather than starting a run with a cable that is not there.
