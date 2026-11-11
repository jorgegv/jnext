# JNEXT — Feature List

One line per feature. Details are in the user guide (`doc/user-guide`) and the developer guide (`doc/developer-guide`).

## Machine emulation
- ZX Spectrum 48K, 128K, +2A/+3, Pentagon 128 and ZX Spectrum Next machine types
- Pentagon personality on the Next (`--machine pentagon`): 128K ROM and paging, uncontended 50 Hz timing, Covox on 0xFB
- **Native NextZXOS cold boot** through the authentic FPGA chain (bootrom → TBBLUE.FW → NextZXOS) from an SD card image
- FUSE-based Z80 core with all standard opcodes (100% pass rate on FUSE test suite)
- All 26 Z80N extended opcodes (NEXTREG, MUL, LDIRX, barrel shifts, etc.)
- Accurate memory contention for 48K, 128K, and +3 timing models
- IM1/IM2 interrupt controller with all 14 Next interrupt levels
- Z80 CTC, UART, DMA, SPI, I2C/RTC peripherals (VHDL-verified)
- Host USB gamepads (hot-plug) mapped to the Next's two joystick connectors (Kempston/Sinclair/Cursor/MD via NR 0x05)
- Per-connector joystick source: a chosen controller, the host cursor keys + Space, or None (`--joy1-source`/`--joy2-source`, Input menu, Preferences)
- Remembered controller identity per connector, so identical pads are told apart (`--joy1-device`/`--joy2-device`)
- DivMMC with 8KB SRAM, automap, and SD card image mounting
- Live SD-card change without a reset: File > Insert SD Card Image / Eject SD Card, or `--delayed-sdcard-insert-frames N FILE` headless
- Floating bus emulation (48K/128K modes)
- Pentagon-512 / Pentagon-1024 paging modes via NextREG NR 0x8F

## Video
- ULA: standard 48K pixel+attribute, Timex hi-colour (8×1 attributes), Timex hi-res (512×192)
- Layer 2: 256×192, 320×256, and 640×256 resolutions at 8-bit colour
- Hardware sprites: 128 sprites, 16×16 pixels, 8-bit/4-bit colour, scaling ×1/×2/×4/×8
- Composite sprites (anchor + relative for larger objects)
- Tilemap: 40×32 and 80×32 modes, 4bpp/1bpp patterns, hardware scroll
- Tilemap per-scanline switching of map, tile definitions, default attribute, transparency index and clip window
- LoRes 128×96: 8-bit colour and Radastan 4-bit modes, X/Y hardware scroll, palette offset
- Copper co-processor: WAIT/MOVE instructions, per-scanline register writes
- 6-mode layer compositor (SLU/LSU/SUL/LUS/USL/ULS priority)
- Per-scanline border colour updates (authentic tape loading stripes)
- Nirvana/BIFROST-class multicolour: per-scanline, column-accurate attribute racing (FUSE-verified)
- Runtime NR 0x03 machine-timing changes re-derive the full video timing at the frame edge, incl. Pentagon timing

## Palettes
- 8 palettes (ULA/Layer2/Sprite/Tilemap × first/second), 256 entries each
- 9-bit RGB colour (512 possible colours), full NextREG palette control
- Authentic default ULA colours from the boot chain, on every machine type and for `--load`ed programs
- Bright magenta no longer collides with the default transparency colour

## Audio
- AY-3-8910 / YM2149 × 3 (TurboSound), with tone, noise, envelope, and stereo panning
- 4-channel 8-bit DAC (Soundrive/Specdrum/Covox)
- Beeper (EAR/MIC)
- SDL audio output at 44100 Hz stereo
- Direct 44.1 kHz stereo PCM WAV recording (`--wav-record`), including headless runs and sibling-NEX chaining
- Timestamped physical DAC write tracing (`--dac-trace` CSV)
- Master host gain (`--audio-gain-db`) plus per-subsystem gains for beeper, AY #0/#1/#2 and DAC (`--audio-gain-beeper-db`, `--audio-gain-ay0-db`/`ay1`/`ay2`, `--audio-gain-dac-db`; also in Preferences)

## Networking (emulated ESP-01 Wi-Fi)
- Real outbound TCP and UDP connections from guest programs through an emulated ESP-01 on UART 0, using the standard AT commands (user guide: Networking)
- Verified against the real NXtel BBS (via the NextZXOS Browser) and the `newt` dot command reading `pool.ntp.org`
- Off by default: `--esp` enables it, `--no-esp` forces it off over a saved preference
- Optional host allowlist: repeatable `--esp-allow HOST`
- Settings > Preferences > Network makes the Wi-Fi settings permanent
- Always refused: loopback, link-local, cloud-metadata, unspecified and multicast addresses; LAN (RFC1918) stays reachable
- Every connection opened, refused or failed is logged, with a GUI status cell showing the state
- Server mode: guests can listen for inbound connections (`AT+CIPMUX`, `AT+CIPSERVER`), loopback by default, `--esp-listen-address` widens it
- Server housekeeping: up to four inbound connections, `AT+CIPCLOSE=<id>` drops one, `AT+CIPSTO` sets the idle timeout
- The Next's documented WiFi setup session works (`AT+CWMODE`, `AT+CWLAP`, `AT+CWJAP`, `AT+CWQAP`)
- `AT+CIPSTATUS` lists live connections; the query forms of `AT+CIPMUX`, `AT+CIPSERVER` and `AT+UART_CUR`/`_DEF` answer
- `AT+CIPDOMAIN` resolves a name without connecting, under the same allowlist and address policy
- `AT+PING` pings in process on an unprivileged ICMP socket (works in the Flatpak), under the same rules
- SNTP against a real time server (`AT+CIPSNTPCFG`, `AT+CIPSNTPTIME?`), deliberately ignoring `--rtc`
- Configurable station address: `--esp-ip-address ADDR`
- Station address that changes across an outage: `--esp-ip-address-after ADDR`
- Schedulable WiFi outage: `--esp-delayed-disassociate-frames N`, `--esp-delayed-associate-frames N`
- No TLS and no transparent mode (`AT+CIPMODE`)
- Downloads paced at the UART's real baud rate

## File format support
- NEX (v1.0/1.1/1.2): direct page loading, Layer 2 screen/palette, host-backed streaming for self-streaming NEX applications
- NEX saving (V1.2: PC/SP/border/RAM banks)
- NEX V1.3 loading (Ped7g's extended format): new loading screens, copper block, optional CRC-32C check, extended header fields
- NEX V1.3 is gated behind an explicit opt-in: `--experimental-nex-v1.3` at the CLI, a warning dialog in the GUI
- `--nex-args "LINE"`: argument line for a NEX V1.3 program, delivered in its CLI buffer
- esxDOS answers for a directly loaded NEX: default drive, `run NAME.nex` chain-loading, in-memory file I/O, errors for other calls
- `--esxdos-stub` gives the same esxDOS answers to programs loaded any other way
- Host directory for a directly loaded program: `--esxdos-stub-root DIR`, or with `--esxdos-stub` the NEX's own directory by default, serves real host files through the esxDOS file and directory calls
- `--esxdos-stub-writable` allows guest writes to that host directory (read-only by default)
- The host directory is for NEX programs and dot commands only, not for a booted NextZXOS (user guide: Host files for a directly loaded program)
- Warm start (Next only, the default for `--load` of a `.nex`): the program runs on a cached recording of a real firmware boot
- `--warm-start-regenerate` forces a fresh warm-start recording after the card's firmware changes
- SNA: 48K and 128K snapshots with full register and paging restore
- SNA saving follows the machine (48K or 128K form); formats that cannot describe the state are refused with a pointer to `.szx` or `.jns`
- SZX: load (zlib-compressed RAM, AY registers) and save (full RAM, CPU, paging, border, AY registers) for 48K/128K/Pentagon 128/+2A/+3
- SZX loading runs the file on the machine its header names (incl. NTSC 48K at 60 Hz)
- Z80: v1/v2/v3 snapshots, 48K and 128K, compressed and uncompressed pages, full register and paging restore
- JNS (`.jns`): jnext's own whole-machine snapshot and the only format that can represent a Next (ZIP of named JSON subsystems; `--snapshot-compression off`)
- JNS restores the machine type it was saved as, whatever `--machine` says
- JNS records the SD card's identity and refuses a different card (`--snapshot-mode force` overrides, `strict` turns warnings into refusals)
- Snapshot saving (`.jns`/`.sna`/`.szx`/`.nex`) via File > Save Snapshot... (Alt+Shift+S) or `--delayed-snapshot FILE` headless
- A format that cannot represent the machine is refused with a reason, never written lossily
- TAP: fast-load via a ROM trap plus real-time EAR bit simulation
- Instant `LOAD ""` autostart on 48K/128K/+3 via a phantom typist
- Tape automatic play/stop (default on; Tape > Auto Play/Stop, `--no-tape-auto-play`) so slow BASIC loaders load in real time
- TAP saving: BASIC `SAVE` appends to a `.tap` file (`--tape-save FILE`)
- TZX and WAV saving: `--tape-save FILE.tzx|FILE.wav` or Tape > Start Saving records the MIC output, so custom savers are captured
- TZX: full block support via ZOT library, fast-load and real-time playback (incl. Direct Recording and DeciLoad turbo loaders)
- WAV: RIFF/PCM EAR bit playback (8/16-bit, mono/stereo) with sub-sample edge interpolation
- RZX playback in every frontend, on the machine the recording was made on
- RZX recording on 48K, 128K or +3 (refused on a Next)
- Programs inside `.zip` archives (`game.tap.zip`, `game.nex.zip`) load directly via `--load`, a bare filename or File > Load
- POK trainer files (WoS `.pok`): Machine > POK Trainers... applies and removes trainers on the running machine
- POK trainers from the command line: `--delayed-pok-frames N FILE` with `--pok-trainers`

## GUI (Qt 6)
- Native Qt 6 main window with menu bar, toolbar, and status bar
- Monochrome SVG toolbar icons that follow a light or dark theme live
- Light and dark mode: windows (debugger included) follow the desktop colour scheme live (`JNEXT_COLOR_SCHEME=dark|light` forces one)
- Hi-DPI pixel-perfect rendering at integer scale (2×/3×/4×)
- True fullscreen with aspect-ratio letterbox (macOS green button and ⌃⌘F too)
- CRT effect: off, Scanlines, or GPU Soft scanlines / TV / PC-monitor looks (View menu, Preferences, `--crt`)
- FPS counter, CPU speed, and machine mode in status bar
- Two reset controls as on hardware: Power Reset (Alt+R/F1, cold boot) and Soft Reset (F4)
- Emulator speed control (0.5×/1×/2×/4×/custom %, or `--speed`)
- Degradation policy for slow hosts: `--when-slow-prefer audio|video`, or Preferences → Startup
- Screenshots in PNG or `.SCR` (Alt+S, toolbar, `--delayed-screenshot`), format picked by extension
- Pause / resume from the emulator window (toolbar, Machine > Pause, Alt+U or the Pause key) with a status-bar indicator
- macOS host shortcuts use Command (⌘) instead of Alt
- Quick Screenshot (Alt+K): no dialog, timestamped name, configurable directory and format
- Quick Save (Alt+Shift+F5) / Quick Load (Alt+Shift+F9): one-key `.jns` snapshots (`--quick-snapshot-keep N|unlimited`)
- File > Recent Files (last ten) and File > Reload Last File (Alt+L)
- Video recording to MP4 via FFmpeg pipe (`--record`)
- Direct audio recording to WAV (`--wav-record`, no FFmpeg required)
- Preferences dialog (Settings → Preferences…, Alt+P) saved to `~/.jnext/jnext.conf`; CLI flags always override
- Every Preferences setting can be set for one run from the command line: `--set section.key=value`, `--cpu-speed`, `--scale`, `--crt`

## Distribution / packaging
- Native Linux packages via CMake CPack: TGZ, DEB, RPM, plus Fedora and Debian/Ubuntu source packaging
- Flatpak manifest (`packaging/flatpak/`)
- Self-contained portable Windows zips, cross-compiled with MinGW, no installer or redistributables
- Three published Windows packages: x64 Qt6 (Windows 10 1703+), x64 Qt5 and 32-bit Qt5 (Windows 7 SP1+)
- SDL-only Windows variants for repo-internal validation (not published)
- Freedesktop integration: `.desktop` launcher, AppStream metainfo, application icon
- GitHub Actions: CI (build, FUSE, unit, golden-screenshot regression) and a release workflow publishing packages on `v*` tags

## Debugger
- Separate debugger window with full panel layout
- Panels: CPU registers, disassembly, memory hex editor, MMU, stack, call stack
- Memory panel physical-page view: `Slot 0-7` show the page mapped in the slot, `Page...` shows any NR page
- Panels: video layers, sprites, copper, NextREG, audio, watches, breakpoints
- Video panel "All layers" view: the real composite, as in the emulator window
- Video panel "Background" view: the NR 0x4A fallback colour per scanline
- Video panel raster indicator: beam position in all four hardware counter domains, frame diagram and ULA fetch read-out
- Audio panel per-source mute: AY #0/#1/#2, DAC and Beeper silenced independently without affecting emulation
- PC/data/read/write breakpoints with watchpoints
- I/O port breakpoints: IO Read on `IN`, IO Write on `OUT`, by low byte or exact 16-bit port
- Breakpoint enable/disable per row, plus a master switch
- Symbol table from Z88DK MAP files and sjasmplus `--sym` / `--exp` files (`--map` or the Map menu), with inline symbol names in disassembly
- **Source-level debugging**: z88dk `-debug` maps and listings and sjasmplus SLD maps give C and assembly source lines, NextBuild / Boriel `Memory.txt` gives symbols (`--source-map FILE`) (user guide: The Source Debugger)
- Banked code matched by page, with identity checks against the linker's section binaries
- z88dk dot commands debugged from their map (`jnext --source-map prog.map`, also with RZX replay)
- Source tab: current line, statement stepping (Into, Over, Out, Back, Reverse Continue), search path for sources (`--source-dir`)
- Source breakpoints by `file:line` or symbol, with gutter click, right-click menu and syntax colouring in selectable schemes
- Source tab and Disassembly stay in sync, and the Call Stack shows each call's source line
- **C variables**: Variables tab shows parameters, locals, globals and statics with C types, structs, arrays, pointers and z88dk floats
- C watches, value editing, `cvar()` in scripts and ZRCP, GDB `monitor locals`/`globals`/`print`/`set var`
- Disassembly select & copy as assembly that pastes into a source file, or "Copy with Addresses" for bug reports
- Trace log (circular buffer, export to file)
- Stepping: Step Into (F6), Step Over (F7), Step Out (F8), Run to EOF, Run to EOSL
- Redefinable debugger keys: all seventeen debugger commands under Settings > Preferences > Debugger Keys
- Backwards execution (rewind): frame snapshot ring buffer, Step Back (Shift+F7), Frame Back (Shift+F6), rewind slider
- Rewind preserves the SD card's SPI protocol state, so streaming programs resume correctly
- Save Snapshot while paused mid-frame completes the frame and saves at the boundary
- Magic breakpoint: `ED FF` (ZEsarUX) / `DD 01` (CSpect) triggers debugger pause
- **Which memory an address is in**: every debugger view names the memory behind an address (`P0E`, `ROM0`, `DIVROM`, ...)
- Debug > Physical Addresses (`--set debugger.physical_addresses=true`) shows the byte's place in the Next's memory
- **Your own labels and comments**: right-click to add, rename or remove labels and comments in the Disassembly or Source tab
- Labels and comments saved in a `.jdp` project file (Map > Open / Save / Save As Annotations, `--annotations FILE`)
- **skoolkit and Ghidra export**: Map > Export or `--export-skoolkit DIR` / `--export-ghidra DIR` write labels, comments and the code/data map
- skoolkit and Ghidra import: Map > Import or `--import-skoolkit` / `--import-ghidra` read labels and comments back
- **Memory coverage**: Debug > Coverage or `--coverage` records executed, read and written bytes for every physical memory
- Coverage shown as Disassembly bars, Memory panel X | R | W underlines and Source tab tints
- Magic debug port: configurable 16-bit port logs writes as hex/dec/ascii
- **Remote debugging over DZRP (DeZog)**: `--dzrp-port N` in every frontend, loopback-only unless `--debug-listen-address` says otherwise
- **Remote debugging with z88dk-gdb**: `--gdb-port N` serves the GDB Remote Serial Protocol in every frontend
- **Debugger scripts (`.jds`)**: rules on execution, memory, ports, NextREG writes, frames, interrupts, Copper and DMA with logging, stop, assert and snapshot actions
- `--script FILE` loads a script (symbols from `--map FILE`); the exit status is the verdict in `--headless` and the SDL build
- Qt debugger Script tab, and Alt+1..Alt+8 script host keys (`--script-key FRAME N` headless)
- Debugger-script use cases: write guards, MMU consistency checks, interrupt-handler audit and MemPoint scripts in `test/scripts/dsl/`
- **Record and replay**: Script > Record Script... or `--record-script FILE` writes a session as a replay script with screen-capture checks
- **Remote debugging over ZRCP (DeZog's `zrcp` remote)**: `--zrcp-port N` serves ZEsarUX's remote command protocol in every frontend
- ZRCP breakpoint conditions, reverse-debugging history, code coverage, `smartload`, `load-binary` / `save-binary` and in-memory snapshots

## CLI
- `--machine`, `--load`, `--headless`, `--tape-realtime`, `--tape-save`, `--esxdos-stub`
- `--esxdos-stub-root DIR`, `--esxdos-stub-writable` (host directory for a loaded NEX / dot commands)
- `--sdcard` mounts an SD image, the source of all ROMs; falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img`, offering a download
- `--sdcard-download-confirm`, `--sdcard-download-force` (auto-provision / force re-download of the fallback image)
- `--sdcard-readonly` (write-protected card; the host file is never modified)
- `--sdcard-file-add FILE [--sdcard-file-dest PATH]` copies a host file or directory tree into the SD image and exits
- `--sdcard-file-force` overwrites an existing destination file
- `--warm-start-regenerate` (force a fresh warm-start recording)
- `--nex-args "LINE"` (argument line for a NEX V1.3 program)
- `--inject` raw binary with `--inject-org`, `--inject-pc`, `--inject-delay`
- `--rewind-buffer-size`, `--speed`, `--record`, `--wav-record`, `--dac-trace`, `--rzx-play`, `--rzx-record`
- `--audio-gain-db` master gain and per-subsystem `--audio-gain-beeper-db`/`--audio-gain-ay0-db`/`--audio-gain-ay1-db`/`--audio-gain-ay2-db`/`--audio-gain-dac-db`
- `--when-slow-prefer audio|video` (what to sacrifice when the host cannot keep real time)
- `--magic-breakpoint`, `--magic-port`, `--magic-port-mode`
- `--esp` / `--no-esp` and the repeatable `--esp-allow HOST` (emulated ESP-01 Wi-Fi; off by default)
- `--joy-uart-rx FILE`: feed a serial byte source into the joystick-port UART (`--joy-uart-connector 1|2`, `--joy-uart-rx-delay-frames N`)
- `--joy-uart-fifo PATH` / `--joy-uart-pty`: live bidirectional joystick-port UART cable for a host debugger such as DeZog (POSIX only)
- `--nextpi`: a real NextPi under QEMU on UART 1, with `--no-nextpi` to override a saved enable (user guide: NextPi on the Raspberry Pi)
- NextPi tuning: `--nextpi-dir`/`--nextpi-release`/`--nextpi-qemu`/`--nextpi-audio`/`--nextpi-qemu-args`, or Settings > Preferences > NextPi
- `--delayed-screenshot-layers ula,layer2,sprites,tiles,all` composes only the named layers into the screenshot
- `--log-level` per subsystem (cpu, video, audio, etc.)
- `--log-file FILE` writes the log to FILE; exits non-zero if it cannot be opened
- `--rtc "YYYY-MM-DD HH:MM:SS"` pins the DS1307 RTC for deterministic screenshots
- `--version`
