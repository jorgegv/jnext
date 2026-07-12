# JNEXT — Credits

## Libraries and third-party software

| Library | License | Used for |
|---------|---------|----------|
| [SDL2](https://www.libsdl.org/) | zlib | Cross-platform multimedia (audio + input) |
| [Qt6](https://www.qt.io/) | LGPLv3 | GUI framework (optional: `ENABLE_QT_UI`) |
| [libcurl](https://curl.se/libcurl/) | curl | HTTP(S) download of the NextZXOS distribution SD-card image |
| [OpenSSL](https://www.openssl.org/) (libcrypto) | Apache-2.0 | SHA-256 integrity hash of the cached SD-card image |
| [FatFs](http://elm-chan.org/fsw/ff/) | BSD-1 | Host-side FAT32 read/format of the SD image (vendored in `src/third_party/fatfs/`) |
| [spdlog](https://github.com/gabime/spdlog) | MIT | Fast C++ logging (git submodule, `third_party/spdlog/`) |
| FUSE Z80 core | GPLv2 | Z80 CPU core, adapted from the [FUSE](http://fuse-emulator.sourceforge.net/) emulator (`third_party/fuse-z80/`) |
| [ZOT](https://github.com/antirez/zot) | MIT | TZX/TAP tape player library by antirez (vendored in `third_party/zot/`) |
| zlib | zlib | Compressed snapshot (SZX) and RZX blocks |
| libpng | libpng | PNG screenshots |

## References and acknowledgments

- **[ZX Spectrum Next FPGA core](https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA/-/tree/master)**
  — the official VHDL sources, which serve as the authoritative hardware
  specification for this emulator.
- **[TBBlue firmware sources](https://gitlab.com/thesmog358/tbblue)** — the
  official firmware sources, invaluable for tracing and debugging boot-time
  behaviour.
- **[FUSE](http://fuse-emulator.sourceforge.net/)** — the Z80 CPU core is
  adapted from FUSE, as is its Z80 opcode test suite.
- **[ZEsarUX](https://github.com/chernandezba/zesarux)** — multi-system Sinclair
  emulator with Next support, used as a behavioural reference during
  development.
- **[CSpect](https://mdf200.itch.io/cspect)** — another Next-capable emulator,
  also used as a behavioural reference during development.
- **[ZX Next Wiki](https://wiki.specnext.dev/Main_Page)** — the reference point
  for Next development.
