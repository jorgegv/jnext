# JNEXT — Pending Features & Known Issues

## NextZXOS Boot (v1.1 milestone)
- NextZXOS boots from SD card image (DivMMC config-page write-recording + soft-reset replay approach)

## File Formats
- Z80 snapshot format loading (`.z80`)
- DSK disk image loading + FDC emulation (`.dsk`)

## Debugger
- Source-level debugging with Z88DK `.LIS` files (breakpoints and disassembly at C source level)
- Scriptable debugger: T-state/scanline/frame event hooks via embedded scripting language
- Debugger window sticky positioning: attach and move with main emulator window in real time

## Audio
- DAC audio buzzing: investigate Soundrive DAC demo continuous buzz (emulator vs hardware vs demo code)
- TZX Direct Recording (DeciLoad 12k8 turbo): TZX 0x15 blocks not loading in real-time mode
- WAV DeciLoad loading: WAV files with DeciLoad 12k8 turbo encoding fail to load

## CI, Quality and Release
- GitHub Actions CI pipeline (Linux + macOS + Windows)
- Unit test plan per module; integration test plan between modules; functional test plan (~demos)
- CI golden-output visual regression tests
- General code refactor and cleanup; replace magic numbers with named constants
- Module-by-module VHDL source alignment review and documentation
- Performance profiling and optimization (400% speed bottleneck: ~75 FPS instead of ~200 FPS)
- Fedora and Debian/Ubuntu binary packages
- Static executable build (vendored Qt6 + SDL2 sources)

## Platform Ports
- Windows port
- macOS port

## Documentation
- Update README for repository and source code users
- DEVELOPMENT guide: architecture, subsystems, Mermaid diagrams, contribution workflow, issue templates
- USAGE document and man page for end users
