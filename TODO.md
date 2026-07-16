# JNEXT — Pending Features & Known Issues

## File Formats
- DSK disk image loading + FDC emulation (`.dsk`)

## Debugger
- Source-level debugging with Z88DK `.LIS` files (breakpoints and disassembly at C source level)
- Scriptable debugger: T-state/scanline/frame event hooks via embedded scripting language
- Debugger window sticky positioning: attach and move with main emulator window in real time

## Audio
- DAC audio buzzing: Soundrive DAC demo produces continuous buzz alongside expected tones

## Platform Ports
- Windows port — packaging CI config exists (`packaging.yml` windows-latest leg) but is UNVERIFIED; needs a real build/test on Windows
- macOS port — packaging CI config exists (`packaging.yml` macos-latest leg) but is UNVERIFIED; needs a real build/test on macOS

## Configuration
- Preferences dialog covers startup defaults + paths; keyboard-layout / redefinable-key remapping still pending (no runtime remap engine yet — tracked as the Phase 11 "redefinable keys" item)
