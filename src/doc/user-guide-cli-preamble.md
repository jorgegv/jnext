# 9.1 Command-line options

Every option JNEXT accepts is listed below, grouped the way the manual groups
them. This page, the **jnext(1)** man page and
[`USAGE.md`](https://github.com/jorgegv/jnext/blob/main/USAGE.md) are three
renderings of one source, so they cannot disagree with each other.

A bare filename is shorthand for `--load`, so `jnext game.tap` works. Anything
starting with `-` is treated as an option, never as a filename, and naming the
program both ways at once is rejected.

For a quick reminder without leaving the shell:

```console
$ jnext --help
$ jnext --version
$ man jnext
```

If you ever find the documentation and the program disagreeing, the program is
right — please [report it](https://github.com/jorgegv/jnext/issues).
