# JNEXT — Pending Features & Known Issues

## NextZXOS Boot (v1.1 milestone)
- NextZXOS boots from SD card image

## File Formats
- Z80 snapshot format loading (`.z80`)
- DSK disk image loading + FDC emulation (`.dsk`)

## Debugger
- Source-level debugging with Z88DK `.LIS` files (breakpoints and disassembly at C source level)
- Scriptable debugger: T-state/scanline/frame event hooks via embedded scripting language
- Debugger window sticky positioning: attach and move with main emulator window in real time

## Audio
- DAC audio buzzing: Soundrive DAC demo produces continuous buzz alongside expected tones
- TZX Direct Recording (DeciLoad 12k8 turbo): TZX 0x15 blocks not loading in real-time mode
- WAV DeciLoad loading: WAV files with DeciLoad 12k8 turbo encoding fail to load

## Platform Ports
- Windows port
- macOS port

## Known latent bugs
- `Emulator::prev_nmi_generate_n_` is constructor-init only; not reset in `reset()` / `init()`. Re-initialising a long-lived Emulator after a prior NMI can mask the next falling edge. Surfaced by NMI Phase 3 integration test 2026-04-24.
