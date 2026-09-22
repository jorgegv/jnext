# How faithful it is

JNEXT is built from the official ZX Spectrum Next FPGA sources — the VHDL that
describes the actual hardware. Where a machine's behaviour is ambiguous or
undocumented, the answer is taken from that source rather than guessed at, and
pinned there by a test. In practice this means the emulator's answer is the
silicon's answer.

Two honest caveats:

- **Extreme cycle-exactness is not a goal.** "Good enough to develop games on"
  is. A handful of demos that depend on sub-scanline timing may not be
  pixel-perfect.
- **JNEXT 1.0 is not finished.** Fixes are still pending and many features
  are on the roadmap. It works well enough for its author, and hopefully for
  you; the 1.0 release is meant to bring more users and more bug reports. Rough
  edges are listed rather than hidden — see chapter 8, *Known issues* — and
  bugs go to the issue tracker, <https://github.com/jorgegv/jnext/issues>.
