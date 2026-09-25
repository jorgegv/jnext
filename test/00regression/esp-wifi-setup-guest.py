#!/usr/bin/env python3
"""Write the Z80 guest that walks the ZX Spectrum Next's OWN documented WiFi
setup session (GH #154).

Unlike `esp-loopback-peer.py` this script is NOT a peer: the sequence below
touches no socket, so there is nothing to listen on and nothing to race. It
only has to emit the guest binary, which is why it is a plain writer that exits
immediately rather than a server with a ready-file handshake.

WHAT IT IS A TEST OF. `tbblue/docs/extra-hw/wifi/WIFIand UARTReadME1st.txt` is
shipped with the ZX Spectrum Next distribution and walks a user, at a terminal,
through bringing the ESP-01 online:

    AT+CWMODE?    will give 1,2 or 3 (where STA=1, AP=2, Both=3)   (:232)
    AT+CWMODE=1   - to set it                                      (:238)
    AT+CWLAP      - to List Access Points...                       (:240)
    AT+CWJAP="wifinetwork","password" - To Join Access Point       (:242)
    AT+CIFSR will give the current IP address on the network.      (:247)
    AT+GMR will tell you the current versions...                   (:249)
    ...
    AT+CWQAP                                                       (:372)

Before GH #154 jnext answered ERROR to the FIRST line and to five of the first
six. This row is that document, executed. Its headline assertion is therefore
not any particular reply but that the whole session contains NO `ERROR` at all.

The guest program is byte-identical to `esp-loopback-peer.py`'s — the same
76-byte table walker, reused deliberately rather than forked, so there is one
Z80 program in the suite and not two to keep in step. Only the script table
differs. See that file for the annotated assembly listing.

Usage: esp-wifi-setup-guest.py <guest.bin> [script]

`script` selects which AT session to emit; it defaults to `wifi-setup`, the one
described above. The second, `cipdomain`, drives AT+CIPDOMAIN against three IP
LITERALS so that the row needs no DNS server and no network at all — see
SCRIPTS below.

There is deliberately ONE copy of the Z80 walker in the suite rather than a
file per session: the program is identical, only the table differs.
"""
import sys

# The table walker, byte-identical to esp-loopback-peer.py's GUEST_CODE. The
# annotated listing it was assembled from lives there and is the source of
# record for both rows.
GUEST_CODE = bytes([
    0xF3, 0x21, 0x00, 0x81, 0x7E, 0xB7, 0x28, 0x0A, 0xCD, 0x17, 0x80, 0x7E,
    0x23, 0xCD, 0x44, 0x80, 0x18, 0xF2, 0xCD, 0x30, 0x80, 0x18, 0xFB, 0x56,
    0x23, 0x7A, 0xB7, 0xC8, 0x5E, 0x23, 0x15, 0x01, 0x3B, 0x13, 0xED, 0x78,
    0xE6, 0x02, 0x20, 0xF7, 0x7B, 0x01, 0x3B, 0x13, 0xED, 0x79, 0x18, 0xE9,
    0x01, 0x3B, 0x13, 0xED, 0x78, 0xE6, 0x01, 0x28, 0xF7, 0x01, 0x3B, 0x14,
    0xED, 0x78, 0x01, 0xFE, 0xCA, 0xED, 0x79, 0xC9, 0x57, 0xCD, 0x30, 0x80,
    0xBA, 0x20, 0xFA, 0xC9,
])
SCRIPT_TABLE_ORG = 0x8100

# Each record is (AT line, sync byte). The sync byte is "the first byte of this
# reply that cannot occur in the bytes before it" — it is a SYNC POINT, not a
# parse, and the guest echoes everything it passes on the way to it.
#
# `K` (of `OK`) serves for every line but one. `AT+GMR` is the exception: its
# version block contains `SDK version:`, whose K comes first, so the walker
# syncs there and the rest of the block drains during the NEXT record's wait.
# That is harmless — the trace stays in order because the engine serialises a
# reply against the command in flight — and it is recorded here so a reader
# does not "fix" a sync byte that is deliberately early.
WIFI_SETUP = [
    (b'AT+CWMODE?\r\n',                          b'K'),   # readme:232
    (b'AT+CWMODE=1\r\n',                         b'K'),   # readme:238
    (b'AT+CWLAP\r\n',                            b'K'),   # readme:240
    (b'AT+CWJAP="wifinetwork","password"\r\n',   b'K'),   # readme:242
    (b'AT+CIFSR\r\n',                            b'K'),   # readme:247
    (b'AT+GMR\r\n',                              b'K'),   # readme:249
    (b'AT+CWQAP\r\n',                            b'K'),   # readme:372
    (b'AT+CIFSR\r\n',                            b'K'),   # the address is gone
]

# AT+CIPDOMAIN over IP LITERALS (GH #154). Literals are used on purpose: they
# take the synchronous fast path, so this row needs no DNS server, no peer and
# no network — and it still exercises the whole product path, because the
# ADDRESS POLICY is applied to a literal exactly as it is to a resolved name.
#
# That is what makes this worth a functional row at all. The unit suites prove
# the resolver and the engine; only a run of the real binary proves that
# `setup_esp()` actually handed the engine a resolver and wrapped it in the
# allowlist gate. An engine that was perfect and a wiring step that forgot it
# would pass every unit row and fail here.
#
# The sync byte for the two REFUSALS is `R` — the first one in `ERROR`, since
# `DNS Fail` contains none.
CIPDOMAIN = [
    # RFC1918 is deliberately reachable (design doc §8.1 item 4), so this one
    # answers with the address.
    (b'AT+CIPDOMAIN="192.168.100.238"\r\n', b'K'),
    # Loopback is denied by the DEFAULT policy. If the policy were not wired
    # into the resolver, this would answer with 127.0.0.1 instead.
    (b'AT+CIPDOMAIN="127.0.0.1"\r\n',       b'R'),
    # Cloud metadata — the address the transport is most careful never to
    # reach, and therefore the one a lookup must never disclose either.
    (b'AT+CIPDOMAIN="169.254.169.254"\r\n', b'R'),
]

SCRIPTS = {'wifi-setup': WIFI_SETUP, 'cipdomain': CIPDOMAIN}


def build_guest(script):
    table = b''
    for line, expect in script:
        assert len(line) < 256, line
        table += bytes([len(line)]) + line + expect
    table += b'\x00'                      # end of script
    pad = SCRIPT_TABLE_ORG - 0x8000 - len(GUEST_CODE)
    assert pad >= 0, 'guest code overruns the script table'
    return GUEST_CODE + bytes(pad) + table


def main(argv):
    if len(argv) not in (2, 3):
        sys.stderr.write(__doc__)
        return 2
    name = argv[2] if len(argv) == 3 else 'wifi-setup'
    if name not in SCRIPTS:
        sys.stderr.write('unknown script %r; known: %s\n'
                         % (name, ', '.join(sorted(SCRIPTS))))
        return 2
    with open(argv[1], 'wb') as f:
        f.write(build_guest(SCRIPTS[name]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
