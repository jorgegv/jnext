# Why JNEXT needs an SD-card image

A real ZX Spectrum Next keeps almost nothing in the machine itself. NextZXOS,
the 48K / 128K / +3 BASIC ROMs, the DivMMC and Multiface firmware — all of it
lives on the SD card, and the machine loads it from there at boot. JNEXT works
the same way: it boots from an *SD-card image*, a single file that stands in
for that card.

This is why even `--machine 48k` needs one. The 48K BASIC ROM is a file on the
card.
