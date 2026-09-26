# 5.9 Snapshots

A snapshot freezes the whole machine to a file and brings it back later.

## Which format to use

JNEXT writes four, and the extension picks one:

| Extension | What it is | Use it when |
|---|---|---|
| `.jns` | **JNEXT's own snapshot.** The only one that can represent a ZX Spectrum Next. | You are on a Next — which is JNEXT's default machine. |
| `.sna` | The classic 48K/128K snapshot every Spectrum emulator reads. | You are on a 48K, 128K or +3 and want to hand the file to another emulator. |
| `.szx` | ZX-State: richer than `.sna`, still classic-only (48K/128K/+2A/+3). | Same, with more fidelity — it carries all eight RAM banks. |
| `.nex` | Not a snapshot — a *program* file the Next's own loader runs. | You are producing something to run on real hardware. |

`.sna` and `.szx` describe a machine the Next is not: no Layer 2, no sprites,
no tilemap, no Copper, no DivMMC, 128 KB of RAM where a Next has 768 KB or
more. That is why `.jns` exists — and **on a Next both of them are refused**,
each with an error naming the machines it can describe and pointing you at
`.jns`, rather than writing a file that misrepresents the machine you have. Ask
for either on a 48K, 128K or +3 and you get it.

One thing to know about the `.sna` you do get: JNEXT only ever writes the **48K
form** of SNA, so on a 128K or a +3 it holds the 64 KB the CPU can see at that
instant — the other five banks and the paging are not in it. `.szx` carries all
eight banks on those two machines, which is the extra fidelity the table above
means.

## Saving

**File ▸ Save Snapshot…** (Alt+Shift+S), or headless:

```
jnext --headless --load game.nex \
      --delayed-snapshot state.jns --delayed-snapshot-frames 600
```

On a Next the file dialog offers `.jns` first and adds `.jns` if you type a
name with no extension; on a 48K, 128K or +3 it offers `.sna` first and adds
that. Type any of the four extensions and you get that format — except `.sna`
or `.szx` on a Next, which are refused as above: the dialog says why, and
headless says why and exits non-zero rather than leaving you a file you would
have trusted.

A snapshot is only ever taken at a **frame boundary**. If the debugger has
stopped the machine part-way through a frame — which is exactly when you reach
for this menu item — JNEXT finishes that frame first and saves from the
boundary after it, and the status bar says so once. That pause never refuses
the save and never greys the menu item out — the only refusal is the format one
above; the consequence is that the restored machine is up to one frame (20 ms)
past where you stopped. If you need the precise instant, that
is what the rewind buffer is for.

## Loading

Drop the file on the command line, or **File ▸ Load NEX File…** (Alt+O) — the
dialog lists `.jns` alongside everything else.

```
jnext state.jns
```

A `.jns` restores the machine **type** as well as its contents: one taken on a
Next comes back as a Next even if `--machine` says otherwise, the same way a
`.sna` or `.szx` already behaves. You do not have to remember what you saved.

## What travels, and what does not

Everything the emulated machine is: CPU, all of RAM, the MMU, every NextREG,
the video layers and their palettes, the Copper, the audio chips mid-envelope,
the DivMMC, the Multiface, the SD card's transfer state, the keyboard and
joystick.

Not in the file, deliberately:

- **Your debugger session.** Breakpoints, watches, the trace log and the rewind
  history are yours, not the machine's, and they stay where they are.
- **Your volume and mute settings.** Restoring somebody's save must not move
  your own knobs.
- **A recording in progress.** Video or RZX capture is something *you* are
  doing, not something the machine is.

## The SD card, and why a snapshot may complain

A `.jns` does **not** contain the SD card — a card image is a gigabyte, and it
holds NextZXOS, which is not ours to redistribute. It records the card's
*identity* instead, and checks it when you load.

- **A different card** — different size, different partitioning, different
  volume serial — is **refused**. Restoring a Next mid-way through reading a
  file, against a card where that file is somewhere else, is the kind of wrong
  that looks like a crash an hour later. `--snapshot-mode force` overrides
  it, and says what it is overriding.
- **The same card, changed since** — which happens constantly, because JNEXT
  writes guest changes back to the card — **restores with one warning line**.
  That is normal, and usually irrelevant.
- The card's **volume label** is never compared. Several tools rewrite it, and
  a snapshot that refused over a renamed card would be a snapshot nobody reads
  the warnings of.

## Do not share a `.jns`

A snapshot contains **all of RAM**, and on a Next that includes the NextZXOS
and DivMMC ROM code paged into it. Those are not ours to redistribute and they
are not yours either. Keep your snapshots; do not post them.

If you want to hand somebody a program, hand them the `.nex`.

## Two flags for when something is wrong

`--snapshot-compression off` writes the file with nothing deflated. It is then
an ordinary ZIP you can open with `unzip -p` and read with a text editor — the
manifest, and one JSON document per subsystem. About five times the size, and
JNEXT reads both forms. `on` is the default.

`--snapshot-mode` says how much a load refuses, and it has three positions
because they are one dial rather than a set of switches:

- `normal` — the default, described above: warnings for a changed state model
  or changed ROMs, a refusal for a different card.
- `strict` — turns those warnings into refusals: a snapshot from a JNEXT whose
  state model this build does not know, or one whose recorded ROM digests
  differ from the ROMs now loaded. A missing tape file is never a refusal even
  here — a machine restores perfectly well without the tape it was reading.
- `force` — restores against a different SD card anyway, saying what it is
  overriding.
