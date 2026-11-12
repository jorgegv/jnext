# esxdos-write-probe: what real NextZXOS answers to the esxDOS calls that change files

`nextzxos.txt` is a RECORDING, not a computed expectation: the answers of real
NextZXOS 2.09 (M_DOSVERSION in its header) to F_UNLINK, F_RENAME, F_MKDIR,
F_RMDIR, F_TRUNCATE, F_FTRUNCATE, F_CHMOD and M_GETERR, and to the read calls
that observe their effects, one line per call. It is the oracle of the jnext
host-folder write calls (there is no VHDL for this: NextZXOS is software).

- `[rw]`: the probe on a writable card.
- `[ro]`: the same card mounted `--sdcard-readonly` (jnext refuses every block
  write with the SD data-response token 0x0D, as a write-protected card does).

The header names the image (file name and sha256 of the master image the
measurement was made on), the sha256 of both generated probe NEX files, the
jnext version and the date. The image itself is NEVER stored in the repository.

## Files

- `test/00regression/esxdos-write-probe.py`: generates the two probe NEX files
  (`nex --mode rw|ro OUT.nex`), annotates a raw run with each step's description
  (`annotate`), and judges a run against the recording (`compare`; exit 0 only when
  every step matches or is a declared divergence, listed in the generator's
  `DIVERGE` table with its reason).
- `test/00regression/esxdos-write-probe-measure.sh`: re-measures: runs both NEX
  files under NextZXOS on a private scratch copy of the SD master and writes the
  raw transcripts and a fresh `recording.txt`.

## Re-measuring

    bash test/00regression/esxdos-write-probe-measure.sh build/jnext ~/tmp/measure
    cp ~/tmp/measure/recording.txt test/fixtures/esxdos-write-probe/nextzxos.txt

Changing the generator changes the NEX files, so the recording must be
re-measured (the `esxdos-write-func` row refuses a generator whose NEX sha256
differs from the recording's header). Three measurement runs were identical
byte for byte; the transcript contains no listing order and no timestamp, and
every handle number is masked.

## Reading a line

    S0123 F1 A18 ; [unlink] F_UNLINK read-only file

`F` is the carry, `A` the A register in hex. `A**` is a masked A: on success
NextZXOS leaves whatever A held (junk), and handle numbers differ between
NextZXOS and jnext. Extra fields: F_STAT prints attribute and size (little-endian
hex; the `ro` probe size only), F_READ the bytes read, M_GETERR the message bytes
(bit 7 set on the last one), F_READDIR the attribute and the name.
