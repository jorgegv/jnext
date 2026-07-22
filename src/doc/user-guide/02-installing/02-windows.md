<!--
  NONE of the routes below has been checked on real Windows hardware (GH #59) —
  they are derived from documented Windows behaviour, not observed on our own
  artifact. Microsoft has changed the SmartScreen dialog before, and route 2's
  behaviour depends on which tool the user extracts the zip with. The macOS
  page's single instruction silently stopped working on macOS 15, which is how
  GH #55 arose; do not let this page repeat it. Re-check each route on real
  hardware and FIX it, rather than adding a second, still-untested alternative
  beside it. Specifically unconfirmed: whether unblocking the zip before
  extraction suppresses the SmartScreen prompt for the extracted jnext.exe.
-->
# Windows

Download `jnext-*-windows-x64.zip`, unzip it anywhere, and run `jnext.exe`.
There is no installer: everything the program needs is in the folder, and
deleting the folder uninstalls it.

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
Unblock-File .\jnext-*-windows-x64.zip
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

> **Not confirmed on real hardware yet.** Nobody has verified on a real Windows
> machine that unblocking the zip first stops the prompt appearing for the
> extracted `jnext.exe`. If you try it, please report what happened on
> [issue&nbsp;#59](https://github.com/jorgegv/jnext/issues/59). Route 1 works
> regardless.

## Route 3 — build it yourself

The Mark of the Web comes from downloading, not from running, so an executable
you build on your own machine never carries one and there is no SmartScreen
step at all. See [Building from source](06-building-from-source.md) — note that
the Windows executable is cross-compiled from Linux with MinGW rather than
built on Windows.
