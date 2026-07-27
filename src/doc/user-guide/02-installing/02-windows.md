<!--
  These routes are derived from documented Windows behaviour rather than
  observed on our own artifact: the maintainer has no Windows machine, and
  decided (2026-07-22) to publish them as they stand and let real users report
  what actually happens, rather than hold the page hedged indefinitely.

  That is a deliberate trade, not an oversight — so if a report comes in, treat
  it as the verification that was deferred, and FIX the route in place rather
  than adding a second, still-untested alternative beside it. The macOS page's
  single instruction silently stopped working on macOS 15, which is how GH #55
  arose; this page is one Microsoft dialog change away from the same.

  The weakest claim, if you are triaging a report: that unblocking the zip
  BEFORE extraction suppresses the prompt for the extracted jnext.exe (route
  2). It is a synthesis of two documented behaviours, not a documented
  click-path, and it also depends on which tool the user extracts with.
-->
# Windows

There are three Windows packages. All three are the same emulator, with the
same full GUI and debugger — pick the one that matches your Windows:

| Package                          | Runs on                        |
|----------------------------------|--------------------------------|
| `jnext-*-windows-x64.zip`        | Windows 10 (1703) or later     |
| `jnext-*-windows-x64-legacy.zip` | Windows 7 SP1 or later         |
| `jnext-*-windows-x86-legacy.zip` | Windows 7 SP1 or later, 32-bit |

The Windows 7/8 packages are built against Qt 5 instead of Qt 6, which is what
lets them run there — Qt 6 needs APIs that only exist from Windows 10 onwards.
The 32-bit package is the one to take on a 32-bit Windows of any version.

Unzip the one you picked anywhere, and run `jnext.exe`. There is no installer:
everything the program needs is in the folder, and deleting the folder
uninstalls it.

The first time you run it, Windows SmartScreen may say it *prevented an
unrecognised app from starting* and show no publisher name. That is expected,
and the next section explains why. Getting past it takes one of the routes
below; you only do it once.

## Why the warning appears

JNEXT is not code-signed, and that is a deliberate choice. Authenticode signing
requires a certificate bought from a commercial certificate authority and
renewed every year — and the kind that stops SmartScreen complaining from the
very first release is dearer still and arrives on a hardware token. This is a
hobby project, and paying for that would not make the emulator any better.

So Windows has no publisher to name and no reputation to look up, and tells you
so. The warning reports the *absence of a signature* — it does not mean
anything was found wrong with the download. It will keep appearing, because
signing is not planned.

A signature would name the certificate holder who built the file. It would not
tell you the program is safe — signed software can do anything unsigned
software can — so if who built it is the question that matters to you, route 3
answers it properly.

## Route 1 — Run anyway at the prompt

1. Run `jnext.exe`. SmartScreen blocks it and offers only a **Don't run**
   button.
2. Click **More info**. A line appears naming the app, with its publisher given
   as *Unknown publisher*.
3. Click **Run anyway**.

This is the route most people will use, and the one to try first.

## Route 2 — unblock the download first

When your browser downloads a file, Windows tags it with the *Mark of the Web*
— an NTFS alternate data stream named `Zone.Identifier` recording that it came
from the internet. That tag is what puts JNEXT in front of SmartScreen.
Clearing it from the **zip, before you extract it**, is the tidier route:

In Explorer, right-click the downloaded `.zip`, choose **Properties**, and on
the **General** tab tick **Unblock** at the bottom, then **OK**. If there is no
**Unblock** box, the file carries no tag and there is nothing to do.

The same thing from PowerShell:

```powershell
Unblock-File .\jnext-*-windows-*.zip
```

`Unblock-File` does exactly what that checkbox does.

Do it **before extracting**, because whether the tag reaches the files inside
depends on which tool you extract with: Explorer's own zip handler and WinRAR
copy it onto every extracted file, while 7-Zip only does so when that option is
turned on. Unblocking the zip first sidesteps the question. If you have already
extracted, apply the same **Properties → Unblock** or `Unblock-File` to
`jnext.exe` itself.

This is you deciding to trust one particular download, which is why it names
one file. Do **not** switch SmartScreen off, and do not add Microsoft Defender
exclusions to avoid the prompt: this route covers a single download and leaves
everything else on your PC checked exactly as it was.

> **If this does not work for you, please say so.** JNEXT is developed on Linux
> and these steps follow Microsoft's documented behaviour rather than a test on
> a Windows machine here. If the prompt still appears after unblocking, please
> [open an issue](https://github.com/jorgegv/jnext/issues) saying which Windows
> version and which unzip tool you used. Route 1 works regardless.

## Route 3 — build it yourself

The Mark of the Web comes from downloading, not from running, so an executable
you build on your own machine never carries one and there is no SmartScreen
step at all. See [Building from source](06-building-from-source.md) — note that
the Windows executable is cross-compiled from Linux with MinGW rather than
built on Windows.
