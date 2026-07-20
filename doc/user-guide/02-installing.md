# 2. Installing

Installing a package is the recommended way to get JNEXT. Download the one for
your system from the latest release:

**<https://github.com/jorgegv/jnext/releases/latest>**

## Linux

| Distribution | Command |
|--------------|---------|
| Fedora / RHEL | `sudo dnf install ./jnext-*.x86_64.rpm` |
| Ubuntu 24.04 | `sudo apt install ./jnext_*_ubuntu24.04_amd64.deb` |
| Ubuntu 26.04 | `sudo apt install ./jnext_*_ubuntu26.04_amd64.deb` |
| Any distribution | `flatpak install ./jnext-*-x86_64.flatpak` |

The RPM and DEB packages put a `jnext` command on your `PATH` and add JNEXT to
your desktop's application menu. Run it with:

```sh
jnext
```

The Flatpak is launched from the application menu too, or from a terminal with
its application ID:

```sh
flatpak run io.github.zxjogv.jnext
```

If your distribution is not listed, the Flatpak is the one to use — it carries
its own dependencies and works anywhere Flatpak does.

> **Flatpak performance note.** The Flatpak build runs about a third slower at
> high turbo speeds than the RPM and DEB packages. Normal (100 %) speed is
> unaffected. If you routinely fast-forward, prefer the native package.

## Windows

Download `jnext-*-windows-x64.zip`, unzip it anywhere, and run `jnext.exe`.
There is no installer: everything the program needs is in the folder, and
deleting the folder uninstalls it.

On first launch Windows SmartScreen may warn about an unrecognised publisher —
the executable is not yet code-signed. Click **More info**, then **Run anyway**.

## macOS

Download `jnext-*-Darwin.dmg`, open it, and drag **jnext** to Applications.

On first launch macOS Gatekeeper may refuse to open it, because the app is not
yet signed or notarised. Right-click the app, choose **Open**, and confirm
**Open** in the dialog. Alternatively, allow it under **System Settings >
Privacy & Security**.

## Optional: video recording

JNEXT can record a session to an MP4 (chapter 5.5). That feature — and only
that feature — needs **ffmpeg** installed and on your `PATH`. Everything else
works without it.

```sh
sudo dnf install ffmpeg      # Fedora
sudo apt install ffmpeg      # Debian / Ubuntu
```

## Checking it worked

```sh
jnext --version
```

That prints the version and exits. You are ready for [chapter
3](03-first-run.md).

## Building from source

If you would rather build it yourself, see
[BUILD.md](../../BUILD.md) — prerequisites, build options, and how to produce
the packages above.
