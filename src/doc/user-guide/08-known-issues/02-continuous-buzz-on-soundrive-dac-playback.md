# 8.2 Continuous buzz on Soundrive/DAC playback — resolved

**This was not an emulator fault.** It was a bug in JNEXT's own bundled DAC
demo, and it is fixed.

**What was happening.** The demo addressed one of its four DAC channels through
port `0xDF`. That is the Specdrum *mono* port, which writes channels A **and** D
together rather than channel D alone — and the demo also had two of its channels
assigned to the wrong stereo sides. Both the left and right outputs therefore
carried exactly the same pair of summed tones. Since the second tone was the
third harmonic of the first, the sum came out as a hollow, reedy buzz instead of
the intended two-tone stereo.

**Why it looked like an emulator fault.** It reproduced identically in ZEsarUX,
and on real ZX Spectrum Next hardware. That is exactly what you would expect:
both emulators decode the DAC ports correctly, so both faithfully reproduced the
demo's own mistake, and so did the hardware.

If you write software that drives the Soundrive/Specdrum DAC, the mapping worth
double-checking is that ports `0x1F`/`0x0F` feed the **left** output and
`0x4F`/`0x5F` the **right**, while `0xDF` is a **mono** port that writes both
sides at once.

Closed as [issue #38](https://github.com/jorgegv/jnext/issues/38).
