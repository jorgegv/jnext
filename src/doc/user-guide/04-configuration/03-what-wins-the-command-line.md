# What wins: the command line

**Command-line options always beat saved settings.** For any given setting:

1. If you passed it on the command line, that value is used.
2. Otherwise the saved preference is used.
3. Otherwise the built-in default is used.

So if your preferences say "Next" but you run `jnext --machine 48k`, you get a
48K — for that run only, with nothing overwritten. This makes it safe to keep
comfortable defaults for everyday use and still override them for a one-off,
and it is why scripts should pass explicit options rather than depend on
whatever a particular machine happens to have saved.

Headless runs ignore the configuration file completely, so an automated test
produces the same result on any machine.

The full list of options is in the **jnext(1)** man page, also available as
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
