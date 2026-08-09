# JNEXT — Feature List

## Machine emulation
- ZX Spectrum 48K, 128K, +2A/+3, and ZX Spectrum Next machine types
- **Native NextZXOS cold boot** through the authentic FPGA chain (bootrom → TBBLUE.FW → NextZXOS welcome + main menu) from an SD card image
- FUSE-based Z80 core with all standard opcodes (100% pass rate on FUSE test suite)
- All 26 Z80N extended opcodes (NEXTREG, MUL, LDIRX, barrel shifts, etc.)
- Accurate memory contention for 48K, 128K, and +3 timing models
- IM1/IM2 interrupt controller with all 14 Next interrupt levels
- Z80 CTC, UART, DMA, SPI, I2C/RTC peripherals (VHDL-verified)
- Host USB gamepads (up to 2, hot-plug) mapped to the Next's two joystick connectors; mode via NR 0x05 (Kempston/Sinclair/Cursor/MD). Either connector can instead be driven by the host cursor keys + Space (fire), selectable per connector (`--joy1-source`/`--joy2-source`, Input menu, Preferences; persisted)
- DivMMC with 8KB SRAM, automap, and SD card image mounting
- Floating bus emulation (48K/128K modes)
- Pentagon-512 / Pentagon-1024 paging modes via NextREG NR 0x8F (Next FPGA feature)

## Video
- ULA: standard 48K pixel+attribute, Timex hi-colour (8×1 attributes), Timex hi-res (512×192)
- Layer 2: 256×192, 320×256, and 640×256 resolutions at 8-bit colour
- Hardware sprites: 128 sprites, 16×16 pixels, 8-bit/4-bit colour, scaling ×1/×2/×4/×8
- Composite sprites (anchor + relative for larger objects)
- Tilemap: 40×32 and 80×32 modes, 4bpp/1bpp patterns, hardware scroll,
  and per-scanline map/tile-definition/default-attribute switching
- LoRes 128×96: 8-bit colour and Radastan 4-bit modes, X/Y hardware scroll, palette offset
- Copper co-processor: WAIT/MOVE instructions, per-scanline register writes
- 6-mode layer compositor (SLU/LSU/SUL/LUS/USL/ULS priority)
- Per-scanline border colour updates (authentic tape loading stripes)
- Nirvana/BIFROST-class multicolour: per-scanline, column-accurate attribute
  racing (FUSE-verified pixel-identical rendering)
- Runtime NR 0x03 machine-timing changes re-derive the full video timing at
  the frame edge (line/frame length, INT position), incl. Pentagon timing

## Palettes
- 8 palettes (ULA/Layer2/Sprite/Tilemap × first/second), 256 entries each
- 9-bit RGB colour (512 possible colours), full NextREG palette control

## Audio
- AY-3-8910 / YM2149 × 3 (TurboSound), with tone, noise, envelope, and stereo panning
- 4-channel 8-bit DAC (Soundrive/Specdrum/Covox)
- Beeper (EAR/MIC)
- SDL audio output at 44100 Hz stereo
- Direct 44.1 kHz stereo PCM WAV recording (`--wav-record`), including headless runs and sibling-NEX chaining
- Timestamped physical DAC write tracing (`--dac-trace` CSV)
- Master host gain (`--audio-gain-db`) plus per-subsystem gains for beeper, AY #0/#1/#2 and DAC (`--audio-gain-beeper-db`, `--audio-gain-ay0-db`/`ay1`/`ay2`, `--audio-gain-dac-db`; also in Preferences, -24 dB to +24 dB, persisted and live-applied)

## Networking (emulated ESP-01 Wi-Fi)
- Guest programs can make **real outbound TCP and UDP connections** through an emulated ESP-01 on UART 0, driven by the same AT commands real Next software already speaks (`AT+CIPSTART`, `AT+CIPSEND`, `+IPD` receive framing). Verified against the **real NXtel BBS**: NXtel launched from the SD card through the NextZXOS Browser reaches ONLINE over the live internet
- **UDP**, including `AT+CIPSTART="UDP",…` with its optional local port. Verified with the real **`newt`** dot command reading the clock from **`pool.ntp.org` over the live internet** (`.newt sntp 0 pool.ntp.org`)
- **Off by default.** Nothing is reachable until you pass `--esp`; `--no-esp` forces it off again even when a saved GUI preference turns it on
- **Optional host allowlist**: repeat `--esp-allow HOST` to restrict the guest to named hosts. Without it any host is allowed, and the run says so at startup
- **Settings > Preferences > Network** makes both settings permanent, so a program you run often needs no flags; the change takes effect at the next machine start
- **Always refused, allowlist or not**: loopback, link-local, cloud-metadata (`169.254.169.254` and friends), unspecified and multicast addresses. Your own **LAN stays reachable** — RFC1918 is deliberately allowed, so a guest can talk to a machine on your network
- Every connection opened, refused or failed is **logged and never silent**, and a **GUI status cell** shows the current state with refusals in red
- **Server mode**: a guest can listen for incoming connections (`AT+CIPMUX=1` + `AT+CIPSERVER=1,<port>`), which is what a debug stub on the emulated Next needs — the debugger on the PC only dials out. Bound to **loopback by default**; `--esp-listen-address` widens it deliberately, a bind failure answers `ERROR` and never falls back, and nothing listens until the guest asks. Up to four inbound connections, multiplexed `+IPD,<id>,<len>:` framing for the sessions that asked for it and the unmultiplexed form for everyone else. **`AT+CIPCLOSE=<id>`** drops one named connection and frees its slot, so a peer that stops answering instead of disconnecting cannot occupy the module for the rest of the session. **`AT+CIPSTO`** is the server idle timeout, 0-7200 s and 180 by default, matching a real ESP-01: a client that connects and then says nothing is hung up on and the guest is told `<id>,CLOSED`
- **A schedulable WiFi outage**: `--esp-delayed-disassociate-frames N` takes the module off its network after N frames and `--esp-delayed-associate-frames N` puts it back, so a program that has to notice the Next dropping off the WiFi can be tested headlessly. `AT+CIFSR` then reports `STAIP,"0.0.0.0"`, exactly as a real unassociated module does; the address comes back unchanged, and nothing else changes — what a real module does to traffic during an outage has not been measured, and jnext does not invent it
- No TLS and no transparent mode (`AT+CIPMODE`) — no evidenced consumer for either
- Downloads through it are paced at the UART's real baud rate, so software that reprograms the link speed behaves as it does on hardware

## File format support
- NEX (v1.0/1.1/1.2): direct page loading, Layer 2 screen/palette from header; host-backed file/block streaming for extended self-streaming NEX applications; saving (V1.2, PC/SP/border/RAM banks — see class doc-comment for honest register limitations)
- NEX V1.3 loading (Ped7g's extended format, as written by sjasmplus): the three new loading screens (Layer 2 320x256x8bpp, Layer 2 640x256x4bpp, tilemode), copper code block started at load, optional CRC-32C integrity check, expansion-bus and CLI-buffer header fields, and the first-bank file offset used to skip block types jnext does not know. A screen kind the loader cannot size is now refused loudly instead of silently loading every bank from the wrong offset
- NEX V1.3 conformance gate: V1.3 is experimental/unsupported, so loading one needs an explicit opt-in — `--experimental-nex-v1.3` at the CLI (refused with an error without it), a Proceed/Cancel warning dialog in the GUI; V1.0–V1.2 load unchanged
- esxDOS sibling-NEX chaining (`--esxdos-stub`): esxDOS `run NAME.nex` chain-loads another NEX in the same directory (game selectors / multi-part NEX games without booting NextZXOS); plus in-memory esxDOS file I/O for a game's config/score file
- SNA: 48K and 128K snapshots with full register and paging restore; saving (48K)
- SZX: chunked format with zlib-compressed RAM pages; saving scoped to 48K/128K/+2A/+3 (full RAM, full CPU register set, classic paging, border) — refuses outright for Next/unsupported machines rather than truncating
- Z80: v1/v2/v3 snapshots, 48K and 128K, RLE-compressed and uncompressed pages, full register and paging restore
- Snapshot saving (`.sna`/`.szx`/`.nex`) via File > Save Snapshot... (Alt+Shift+S) or `--delayed-snapshot FILE` (headless)
- TAP: fast-load via ROM trap + real-time EAR bit simulation; instant `LOAD ""` autostart via FUSE-style phantom typist (48K/128K/+3 modes — triggers on first full keyboard scan, no fixed delay)
- TAP saving: BASIC `SAVE` through the 48K ROM SA-BYTES routine (trap at 0x04C2, gated on the 48K ROM identity) appends blocks to a `.tap` file (`--tape-save FILE`; FUSE-verified output; custom MIC-bit-banging savers not captured yet)
- TZX: full block support via ZOT library, fast-load + real-time playback (incl. Direct Recording 0x15 / DeciLoad 12k8 turbo loaders)
- WAV: RIFF/PCM EAR bit playback (8-bit/16-bit, mono/stereo) with sub-sample edge interpolation (DeciLoad-class turbo loaders work)
- RZX: playback and recording (IN replay, embedded SNA snapshot, zlib compressed)

## GUI (Qt 6)
- Native Qt 6 main window with menu bar, toolbar, and status bar
- Hi-DPI pixel-perfect rendering at integer scale (2×/3×/4×)
- True fullscreen with aspect-ratio letterbox
- CRT scanline filter overlay
- FPS counter, CPU speed, and machine mode in status bar
- Two distinct reset controls in menu and toolbar, as on real hardware: Power Reset (Alt+R/F1, cold boot) and Soft Reset (F4, back to NextZXOS without re-running the boot chain)
- Emulator speed control (0.5×/1×/2×/4×/custom %, or `--speed`)
- Selectable degradation policy for a host that cannot emulate in real time (`--when-slow-prefer audio|video`, or Preferences → Startup, applied live): keep the sound smooth and drop video frames, or show every frame and let the machine run slower than real time with the sound stuttering
- PNG screenshot (Alt+S, toolbar, `--delayed-screenshot`)
- Video recording to MP4 via FFmpeg pipe (`--record`)
- Direct audio recording to WAV (`--wav-record`, no FFmpeg required)
- Preferences dialog (Settings → Preferences…): configure startup defaults, input sources, live host audio gain and remembered paths; saved to `~/.jnext/jnext.conf` — CLI flags always override saved settings

## Distribution / packaging
- Native Linux packages via CMake CPack: TGZ, DEB, RPM; plus a Fedora `packaging/rpm/jnext.spec` and a Debian/Ubuntu `packaging/debian/` source package
- Flatpak manifest (`packaging/flatpak/`)
- Self-contained Windows builds: cross-compiled with Fedora MinGW, with every runtime DLL and the `qwindows` platform plugin bundled beside the exe — portable zips, no installer, no redistributables (verified under wine: GUI, ROM load, and SD-image download all work). Three published packages: `make package-win` (x64, full GUI + debugger on Qt6, **Windows 10 1703+**), `make package-win-qt5` (x64, Qt5, **Windows 7 SP1+**) and `make package-win32-qt5` (32-bit x86, Qt5, **Windows 7 SP1+**) — audited floors in `doc/design/WINDOWS-COMPAT-PLAN.md` (GH #108)
- SDL-only Windows variants (`make package-win-sdl` / `make package-win32-sdl`): no Qt, repo-internal validation legs — not published artifacts
- Freedesktop integration: `.desktop` launcher, AppStream metainfo, application icon (installed via `make`/`cmake --install`)
- GitHub Actions: CI (build + FUSE + unit + golden-screenshot regression against a provisioned SD image) and a release workflow that publishes packages on `v*` tags

## Debugger (Qt 6)
- Separate debugger window with full panel layout
- Panels: CPU registers, disassembly, memory hex editor, MMU, stack, call stack
- Panels: video layers (All layers / ULA / Layer2 / Sprites / Tilemap / Background per-scanline view), sprites, copper, NextREG, audio, watches, breakpoints
- Video panel "All layers" view: the real composite (same image as the emulator window) — renders through the live compositor, so NR 0x15 priority, blend modes and the NR 0x4A fallback colour (which belongs to no layer) are all visible
- Video panel "Background" view: the NR 0x4A fallback colour the compositor emits where every layer is transparent, shown per scanline (Copper gradients appear as bands)
- Audio panel per-source mute: AY #0/#1/#2, DAC and Beeper can each be silenced independently to isolate what a program is playing. Gates the output stages only — the AY chips keep running and their registers keep reading back, so muting is invisible to the Z80 and cannot change emulation
- PC/data/read/write breakpoints with watchpoints
- I/O port breakpoints: IO Read on an `IN`, IO Write on an `OUT`. A port address of `00`-`FF` matches any port with that low byte (`FE` catches every ULA access, whatever `OUT (254),A` left in the high byte); `0100` and up matches that exact 16-bit port (`243B`, not `253B`). The DMA's own port transfers are seen too
- Symbol table from Z88DK MAP files; inline symbol names in disassembly
- Trace log (circular buffer, export to file)
- Stepping: Step Into (F6), Step Over (F7), Step Out (F8), Run to EOF, Run to EOSL
- Backwards execution (rewind): frame snapshots ring buffer, Step Back (Shift+F7), Frame Back (Shift+F6), rewind slider
- Magic breakpoint: `ED FF` (ZEsarUX) / `DD 01` (CSpect) triggers debugger pause
- Magic debug port: configurable 16-bit port logs writes as hex/dec/ascii

## CLI
- `--machine`, `--load`, `--headless`, `--tape-realtime`, `--tape-save`, `--esxdos-stub`
- `--sdcard` (canonical source for all ROMs — DivMMC, NextZXOS, 48K/128K/+3, Multiface — at TBBlue paths under /MACHINES/NEXT/; optional, falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` — the patched image — offering to download the canonical distribution `cspect-next-1gb.img` and produce that patched copy, with a GUI download progress bar)
- `--sdcard-download-confirm`, `--sdcard-download-force` (auto-provision / force re-download of the fallback image)
- `--sdcard-readonly` (open the SD image read-only; the emulated machine sees a write-protected card and the host file is never modified, so a run cannot disturb an image other runs share)
- `--nex-args "LINE"` — argument line for a NEX V1.3 program, delivered in the CLI buffer its header declares with DE pointing at it; truncated to the header's own declared size, and warns rather than failing when the file cannot carry one
- `--inject` raw binary with `--inject-org`, `--inject-pc`, `--inject-delay`
- `--rewind-buffer-size`, `--speed`, `--record`, `--wav-record`, `--dac-trace`, master `--audio-gain-db`, subsystem `--audio-gain-beeper-db`/`--audio-gain-ay0-db`/`--audio-gain-ay1-db`/`--audio-gain-ay2-db`/`--audio-gain-dac-db`, `--rzx-play`, `--rzx-record`
- `--when-slow-prefer audio|video` (what to sacrifice when the host cannot emulate in real time; persisted as `[startup] when_slow_prefer`)
- `--magic-breakpoint`, `--magic-port`, `--magic-port-mode`
- `--esp` / `--no-esp` and the repeatable `--esp-allow HOST` (emulated ESP-01 Wi-Fi; off by default)
- `--delayed-screenshot-layers ula,layer2,sprites,tiles,all` — compose only the named layers into the screenshot (default all), for capturing each graphics layer in isolation
- `--log-level` per subsystem (cpu, video, audio, etc.)
- `--log-file FILE` (write the log to FILE instead of the console, truncated per run; jnext exits non-zero if FILE cannot be opened rather than quietly logging to the console it was redirected away from)
- `--rtc "YYYY-MM-DD HH:MM:SS"` — pin the DS1307 RTC to a fixed date/time (frozen clock; deterministic boot screenshots)
- `--version`
