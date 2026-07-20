# 7.3 Exit codes, and the capture that was never taken

`jnext` exits 0 on success and non-zero on error. One case deserves calling
out, because it is the one that silently ruins a CI pipeline everywhere else:

> **A screenshot that was requested and never taken is an error.** If the
> automatic exit fires before the capture comes due, JNEXT logs an error and
> exits non-zero. It never writes nothing and reports success, and it never
> writes a stale frame in place of the one you asked for.

You can see it directly:

```console
$ jnext --headless --load myprog.nex \
      --delayed-screenshot never.png --delayed-screenshot-frames 5000 \
      --delayed-automatic-exit-frames 100
...
[platform] [error] --delayed-screenshot: NO screenshot was written to
  'never.png' (layers: all); --delayed-automatic-exit fired 4899 frame(s)
  before the capture was due. Exiting non-zero.
$ echo $?
1
```

Without that guarantee, a script whose program hangs early gets no PNG, a zero
exit status, and — if the comparison step is written defensively — a green
build. The contract means a broken run fails loudly at the emulator, before
your test logic ever has to be clever about it.

Give the exit bound headroom over the capture point (the example above uses
`frames + 50`) so a slightly slower boot does not trip this.
