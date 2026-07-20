# Linux

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
