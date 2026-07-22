<!--
  The SmartScreen click-path below (More info -> Run anyway) has NOT been
  re-verified on current Windows the way the macOS page was re-verified on
  macOS 15 (GH #59) — it is carried over from when the page was written.
  Microsoft has changed this dialog before. Re-check it on real Windows
  hardware and fix the page, rather than adding a second, still-untested
  alternative next to it.
-->
# Windows

Download `jnext-*-windows-x64.zip`, unzip it anywhere, and run `jnext.exe`.
There is no installer: everything the program needs is in the folder, and
deleting the folder uninstalls it.

On first launch Windows SmartScreen may warn about an unrecognised publisher
and refuse to start the program. Click **More info**, then **Run anyway**. You
only do this once.

## Why the warning appears

The executable is not code-signed. Authenticode signing needs a certificate
bought from a commercial certificate authority and kept renewed, and JNEXT does
not have one — so Windows has no publisher name to show you, and says so. The
warning reports the *absence of a signature*; it does not mean anything was
found wrong with the download.

A signature would name the certificate holder who built the file, and would
stop Windows asking. It would not tell you the program is safe — signed
software can do anything unsigned software can. If who built it is the question
that matters to you, build it yourself: see
[Building from source](06-building-from-source.md), which produces a binary
SmartScreen never sees.
