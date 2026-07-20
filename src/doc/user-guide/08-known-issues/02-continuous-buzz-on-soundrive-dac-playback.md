# 8.2 Continuous buzz on Soundrive/DAC playback

**What you see.** Software that plays samples through the Soundrive/Specdrum
8-bit DAC produces a steady background buzz underneath the expected sound.

**What is known.** It persists with interrupts disabled, at 28 MHz, and with a
tightly timed pure-assembly playback loop — and it reproduces in ZEsarUX as
well as JNEXT. So it is not yet clear whether both emulators share a fault,
the test program itself is at fault, or driving a DAC from Z80 code with no
hardware sample clock is inherently prone to it.

**Impact.** Low. Very little Next software uses the DAC; AY/TurboSound and the
beeper are unaffected.

Tracked as [issue #38](https://github.com/jorgegv/jnext/issues/38).
