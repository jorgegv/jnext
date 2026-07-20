# 7.1 Headless mode

`--headless` runs the emulator with no window and no audio device, as fast as
the host allows:

```console
$ jnext --headless --machine next --load myprog.nex \
      --delayed-screenshot shot.png \
      --delayed-screenshot-frames 150 \
      --delayed-automatic-exit-frames 200
```

That is the whole shape of an automated run: load something, capture at a
known point, exit at a known point.
