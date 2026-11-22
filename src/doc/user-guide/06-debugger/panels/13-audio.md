# Audio

![Audio panel](../../img/debugger-audio.png)

The **Sources** strip at the top lets you mute each AY chip, the DAC and the
beeper independently, whichever tab is showing; a checked box is audible. This
is a debugging aid on the output stage only: the Z80 cannot observe it, and it
is not part of a saved state or a rewind slot, so muting a chip to work out
which one is making a noise does not change the program's behaviour.

Each channel also has its own **M** (mute) and **S** (solo) buttons, described
under [Mute and solo](#mute-and-solo) below.

Below it are five tabs: **Chips**, **DAC & Beeper**, **Log**, **Tracker** and
**Scopes**. The last four are not available yet.

## Chips

The header reports whether TurboSound is enabled, whether the chips are in AY
or YM mode, whether the stereo layout is ABC or ACB, the PSG mode set in
NextREG 0x06 (YM, AY, YM ZXN-8950, or held in reset) and whether the AY ports
are open.

Each of the three chips has a block of its own:

- the chip header: **ACTIVE** marks the chip the AY ports reach, then the
  register address latch (`0x00` to `0x1F`; with bit 4 set the writes are
  ignored, as on the hardware), the pan (`L R`, `L -`, `- R`, `- -`), whether
  the chip is mono, and whether it reaches the mix;
- one row per channel: the tone period, the note and the cents off it, **T**
  and **N** (lit when the mixer register enables the tone or the noise on that
  channel), the volume or `Env` when the channel follows the envelope, and a
  level bar;
- the noise period, the envelope shape drawn as four characters (`\___`,
  `/___`, `\\\\`, `\/\/`, `\'''`, `////`, `/'''`, `/\/\`), the envelope period,
  its note (only for the shapes that repeat: 8, 10, 12 and 14) and the current
  step with its direction (`31 falling`, `0 rising`, `31 holding`).

Notes are named at the PSG clock the emulated machine runs at, 1.75 MHz on a
Next. A tuning table made for the 128K Spectrum (1.7734 MHz) therefore plays
about 23 cents flat here, and the panel shows that. The level bar is the level
the channel plays when its tone or noise gate is open, so it does not flicker
with the square wave. Every write to register 13 restarts the envelope, so the
step shown right after one is the start of the new shape.

The **Hex** box swaps the decoded view for the raw table: all 16 registers of
the three chips side by side, named.

The Chips tab is refreshed every frame while the Audio panel is visible and
Chips is the current tab; with another tab current, or the panel hidden behind
another one, the panel is refreshed at about 4 Hz with the rest of the window.

## DAC & Beeper

This tab holds the four DAC channels (A to D) with their M and S buttons. The
rest of the tab is not available yet.

## Mute and solo

Every one of the 13 channels (the 9 AY channels, 3 per chip, in the Chips tab,
and the 4 DAC channels in the DAC & Beeper tab) has an **M** and an **S**
button. A channel is heard only if both its Sources box and its M allow it: M
checked means the channel is muted, and the box and the button are independent
switches.

- **M** mutes or unmutes that channel. The channel keeps running, so unmuting
  it resumes mid-note; a muted DAC channel reads as silence, not as a click.
- **S** solos the channel: the other twelve channels are muted. The Sources
  strip is left alone, so a source you have switched off stays off (the
  beeper follows its own box). Clicking S again ends the solo and restores the
  mutes you had before it. Soloing a second channel moves the solo and still
  restores the first set of mutes. Any other change to the mutes (an M click,
  a script, another debugger client) ends the solo, so ending it never undoes
  that change.

Like the Sources boxes, channel mutes are a listening aid on the output stage
only: the Z80 cannot observe them, they survive a reset and a state load, and
they are not in a snapshot or a rewind slot. They change what you hear and what
is recorded from the mix (a WAV recording or a video capture). The buttons
follow the live mutes, so a mute set by a script shows on them.

A script reads and sets the channel mutes with `AUDIO_CHANNEL_MUTE`: bit 0 to 8
are the AY channels (bit `3 * chip + channel`, channel A = 0), bit 9 to 12 are
DAC A to D, and a set bit is a muted channel.

---
