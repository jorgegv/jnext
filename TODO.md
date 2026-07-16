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
- Windows port — self-contained MinGW cross-build works (`make package-win`; Qt6/SDL2/SDL3 DLLs + qwindows plugin bundled; native cert store for the SD download; 16 MB stack reserve). Verified end-to-end under wine (GUI, ROM load, SD-image download). Still pending: a smoke test on real Windows hardware, and the `release.yml` windows CI job (fedora:44 container) is UNVERIFIED on a real GitHub runner
- macOS port — CI config exists (`release.yml` macos job) but is UNVERIFIED; needs a real build/test on macOS

## Configuration
- Preferences dialog covers startup defaults + paths; keyboard-layout / redefinable-key remapping still pending (no runtime remap engine yet — tracked as the Phase 11 "redefinable keys" item)
