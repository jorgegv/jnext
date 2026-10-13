# 2. Installing

Installing a package is the recommended way to get JNEXT. Download the one for
your system from the latest release:

**<https://github.com/jorgegv/jnext/releases/latest>**

## Trying a build before it is released

Every change that reaches the `main` branch is built automatically, and the
resulting packages can be downloaded and tried before the next release. These
are untested development builds, for people helping to check a fix.

1. Open <https://github.com/jorgegv/jnext/actions/workflows/ci.yml> and set
   **Branch** to `main`.
2. Open the newest run that has a green tick.
3. Scroll down to **Artifacts**, at the bottom of the run's Summary page.
4. Click the one for your system:

   | System | Artifact |
   |--------|----------|
   | Windows 10 / 11, 64-bit | `jnext-windows-x64-zip` |
   | Windows 7 / 8, 64-bit | `jnext-windows-x64-legacy-zip` |
   | Windows 7 / 8, 32-bit | `jnext-windows-x86-legacy-zip` |
   | macOS | `jnext-macos-dmg` |
   | Fedora / RHEL | `jnext-linux-rpm` |
   | Any Linux (Flatpak) | `jnext-linux-flatpak` |
   | Source code | `jnext-source-zip` |

   The `jnext-windows-*-sdl-zip` artifacts are reduced builds without the Qt
   interface; you do not need them.
5. You must be **signed in to GitHub** (a free account is enough) to download.
6. The download is a zip that wraps the package. Unzip it, then install as
   described in the pages that follow; the Windows and macOS security prompts
   behave the same way.
7. Builds are kept for 14 days. Please report problems on the issue tracker and
   include the run's address.

There is no Ubuntu or Debian package here; use the Flatpak.
