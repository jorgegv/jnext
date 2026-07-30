# External References

Authoritative external sources jnext development depends on. Keep every
external URL the project relies on here rather than scattered across docs and
code comments, so there is one place to check when a link rots or a spec moves.

> The **VHDL source is the authority** for hardware behaviour, not any web page
> (see [CLAUDE.md](../CLAUDE.md)). Where a wiki page and the VHDL disagree, the
> VHDL wins. These references are for things the VHDL does not specify: file
> formats, firmware behaviour, and host-side conventions.

## ZX Spectrum Next hardware and firmware

| Reference                                                                          | URL                                                                         |
|------------------------------------------------------------------------------------|-----------------------------------------------------------------------------|
| Next hardware wiki — registers, ports, file formats (start here)                   | <https://wiki.specnext.dev/>                                                |
| NextREG Machine ID register                                                        | <https://wiki.specnext.dev/Machine_ID_Register>                             |
| Boot sequence (authoritative)                                                      | <https://wiki.specnext.dev/Boot_Sequence>                                   |
| TBBlue firmware source (GPLv3) — `TBBLUE.FW` / `TBBLUE.TBU`, the IPL + boot module | <https://gitlab.com/thesmog358/tbblue/-/tree/master>                        |
| TBBlue machine-config boot files (`config.ini` / `menu.ini` / `menu.def`)          | <https://gitlab.com/thesmog358/tbblue/-/blob/master/docs/config/config.txt> |

## File formats

| Reference                                                                    | URL                                                      |
|------------------------------------------------------------------------------|----------------------------------------------------------|
| NEX file format — header layout, bank bitmap, file-handle field              | <https://wiki.specnext.dev/NEX_file_format>              |
| Alternative NEX file formats — the V1.3 header spec; also appended-payload / self-streaming conventions | <https://wiki.specnext.dev/Alternative_NEX_file_formats> |
| nexload2.asm — Ped7g's reference V1.3 loader, the behavioural oracle for V1.3 (the distro's nexload.asm cannot parse V1.3) | <https://github.com/ped7g/ZXSpectrumNextMisc/tree/master/nexload2> |
| sjasmplus `SAVENEX` — the V1.3 writer; `crc32c/crc32c.cpp` is the CRC-32C the spec's checksum field means | <https://github.com/z00m128/sjasmplus>                    |
| Atic Atac Next — freeware extended-NEX compatibility oracle                  | <https://9bitcolor.itch.io/atic-atac-next>                |

**NEX header, offset 140 (`file_handle`)** — worth recording inline because it
is easy to get wrong and jnext did:

| Value             | Loader behaviour                                              |
|-------------------|---------------------------------------------------------------|
| `0`               | close the file after loading                                  |
| `1`               | keep it open, pass the handle in `BC` (the recommended value) |
| `0x4000`–`0xFFFF` | keep it open, write the handle to that memory address         |

## ESP-01 consumer software (used to verify the emulated module)

| Reference                                                                    | URL                                                      |
|------------------------------------------------------------------------------|----------------------------------------------------------|
| NextSync — Jari Komppa's dot command + Python server (the download page)      | <https://solhsa.com/specnext.html#NEXTSYNC>              |
| NextSync 1.2 release — `nextsync12.zip`, the canonical binaries verified in [NEXTSYNC-VERIFICATION.md](testing/NEXTSYNC-VERIFICATION.md) | <https://github.com/Threetwosevensixseven/specnext/releases/tag/nextsync_v1.2> |
| NextSync sources (Unlicense) — `nextsync.c`, `uart.s`, the AT sequence read rather than reverse-engineered | <https://github.com/jclauzel/ZX-Next-Unite/tree/main/nextsync> |

## Emulators used as oracles / comparison

| Reference                                                         | URL                                      |
|-------------------------------------------------------------------|------------------------------------------|
| FUSE — Z80 core (vendored, GPLv2-or-later) and behavioural oracle | <https://fuse-emulator.sourceforge.net/> |


## Adding to this file

Add the URL here **and** link to this file rather than pasting the URL again
elsewhere. If a page's content is load-bearing for a decision, quote the
essential part inline (as with the file-handle table above) so the project
still has the fact if the page disappears.
