# 9.3 ZX Spectrum Next hardware documentation

JNEXT emulates the official ZX Spectrum Next FPGA core, and the VHDL source of
that core is the authority on hardware behaviour. For everything the VHDL does
not specify — file formats, firmware behaviour, host-side conventions — the
project keeps its external links in one place:
[`doc/REFERENCES.md`](https://github.com/jorgegv/jnext/blob/main/doc/REFERENCES.md).

It holds the Next hardware wiki (registers, ports, the boot sequence, the NEX
file format) and the TBBlue firmware sources. That file is the single place
those URLs live, so they are not repeated here.
