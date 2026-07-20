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
- **JNEXT is beta.** Rough edges exist and are listed rather than hidden — see
  chapter 8, *Known issues*, and the issue tracker below.
