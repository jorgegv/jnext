# 8.6 Debugging from the page

A page can serve the machine it runs to a debugger. The player speaks the same three
protocols as the desktop emulator: **DZRP** (DeZog's, [6.10](../06-debugger/functions/10-remote-debugging-with-dezog.md)),
**ZRCP** (ZEsarUX's text protocol, also DeZog's, [6.12](../06-debugger/functions/12-remote-debugging-over-zrcp.md))
and the **GDB remote protocol** of `z88dk-gdb` ([6.11](../06-debugger/functions/11-debugging-with-z88dk-gdb.md)).

A page cannot listen on a TCP port, so the desktop options `--dzrp-port`, `--zrcp-port`
and `--gdb-port` are refused in a page. The page gets a **channel** instead: bytes in, bytes
out. A channel is reached in one of two ways:

- **from your own JavaScript** (`transport: 'channel'`, the default), for a page or a
  webview host that carries the bytes to the debugger itself;
- **over a WebSocket** (`transport: 'ws'`), where the player dials a bridge that turns
  the WebSocket into a TCP port, so DeZog or `z88dk-gdb` connect to it as to any server.

The bridge is `jnext-debug-bridge.mjs`, a node script. It is in `jnext-VERSION-web.zip` and in
JNEXT's source tree (`tools/web-debug-bridge/`).

## The option

```js
jnext.mount(el, {
  machine: '48k',
  file: 'game.nex',
  debug: { protocol: 'dzrp', transport: 'ws', url: 'ws://127.0.0.1:12000' }
});
```

`debug` is one entry, or an array of up to three, one for each protocol:

| key | what it is |
|---|---|
| `protocol` | Required: `'dzrp'`, `'zrcp'` or `'gdb'`. A protocol may be given once. |
| `transport` | `'channel'` (default) or `'ws'`. |
| `url` | Required with `'ws'`, and refused with `'channel'`: a string that starts with `ws://` or `wss://`. |

Any other key, or a mistake in an entry, makes `mount()` reject, and `onError` gets the
reason; the reason says `option 'debug'` and the entry. The player's `argv` shows one
`--debug-channel PROTOCOL` for each entry; `transport` and `url` are the page's business and
never reach the emulator. The same option is on the command line of the browser build as
`--debug-channel PROTOCOL`; the desktop build refuses it.

The machine serves the debugger while the page runs. The servers are off unless the page
asks for them.

## The channel from JavaScript

`player.debugChannel(protocol)` returns the channel of an entry whose transport is
`'channel'`. It throws for a protocol the page did not ask for, and for one served over a
WebSocket. It can be fetched as soon as `mount()` resolves, but the machine only exists after
the visitor's click.

```js
const ch = player.debugChannel('dzrp');
ch.onData  = (bytes) => { /* what the machine says: a Uint8Array, send it to the client */ };
ch.onClose = () => { /* the machine ended the session */ };
ch.open();                       // a client arrived
ch.send(new Uint8Array([...]));  // what the client said
ch.close();                      // the client left
```

- `open()` throws when a client is already open, and throws `jnext: the machine is not running`
  before the machine starts.
- `send(bytes)` takes a `Uint8Array` or an `ArrayBuffer`. It throws when no client is open,
  and throws a `RangeError` when the input is full (64 MiB waiting).
- `close()` ends the session from the client's side; no `onClose` follows it.
- `onClose` is called once when the machine ends the session (ZRCP `quit`, `destroy()`, or the
  machine exiting by itself). Every open session gets it once when the player is destroyed or
  exits.
- `onData` and `onClose` are called from a microtask, after the player's own call has
  returned, never from inside it, so a handler may call the channel again.
- `isOpen` says whether a session is open. One session is open at a time for each protocol.

## The WebSocket link and the bridge

Run the bridge on your own computer:

```
node jnext-debug-bridge.mjs --ws-port 12000 --tcp-port 11000
```

It needs node 18 or later and nothing else. It prints one line when both ports are listening,
`jnext-debug-bridge: page on ws://127.0.0.1:12000, clients on 127.0.0.1:11000`, and a line each
time a page links and unlinks. Port `0` lets the system pick, and the line says which.

The page dials `ws://127.0.0.1:12000`; the debugger connects to port `11000`. For DeZog:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "JNEXT in the browser",
    "remoteType": "cspect",
    "cspect": { "hostname": "localhost", "port": 11000 }
}
```

Use `remoteType` `"dzrp"` where your DeZog has it, `"zrcp"` with a bridge for a `zrcp` page, and
`z88dk-gdb -h 127.0.0.1 -p 11000` for a `gdb` page; the settings DeZog and `z88dk-gdb` need
about your program are theirs, as in chapter 6. One bridge serves one protocol: run one
bridge, on its own two ports, for each protocol the page serves.

The bridge's rules:

- **One page and one client.** A second page that dials gets HTTP 409. A second TCP client is
  closed. A client that hangs up and dials again at once is not a second client: it waits
  for the session before it to end: up to 300 ms for a client that has not left yet, and up to 3 s for the page's `closed` once it has. A client that connects while no page is
  linked is closed at once.
- **Only a local page may link.** The bridge accepts a page whose `Origin` is `http://` or
  `https://` with the host `localhost` or `127.0.0.1` and any port, and answers HTTP 403 to every
  other origin: without that, any web site you visit could dial the bridge and read the debug
  traffic. `--origin ORIGIN` allows one more origin (repeatable; the whole origin, for example
  `https://my.example`), and `--any-origin` turns the check off. A client that sends no `Origin`
  (a program, not a browser) is accepted.
- **Loopback.** The bridge listens on `127.0.0.1` for both ports; `--host ADDR` changes that.
- **A client that stops reading** keeps its own socket open, and the bytes still queued for it,
  until it disconnects or the bridge exits. The slot and the page link are freed when the
  session ends. The bridge does not drop a stalled client by a timeout.
- **Backpressure.** A side that is slower than the other holds the other back, in both
  directions, instead of the bridge buffering without limit.

The player keeps the link open for as long as it runs. If the bridge is not there, or goes
away, the player dials again once a second, and tells the browser console, once for each time
the link goes down, `the debug link to URL is down`. A session open when the link drops ends
(the client is gone). Nothing is sent to `onError`: a missing bridge is not a failure a visitor
should see.

### The link's messages

The messages are the whole protocol between the player and a bridge, so any program can
play the bridge's part (an IDE extension, say):

| message | meaning |
|---|---|
| text `open` | A client arrived. |
| text `close` | Either side ended the session. From the page it means the machine ended it. |
| text `closed` | The page's answer to the bridge's `close`: the end is confirmed, and everything the page sent for that session came before this message, so the bridge may admit the next client. |
| binary | The bytes of the session, in order, in both directions. |

Any other text message is ignored (the player says so once in the console).

## A webview host

Where the page runs in a webview of an editor, the editor's own code can be the bridge, without
a WebSocket: use `transport: 'channel'` and carry the bytes with the editor's message passing.
`jnext-debug-bridge.mjs` exports the TCP half as `TcpRelay`, an `EventEmitter` with
`listen(port, host)` (resolves to the bound port), the events `open`, `data` and `close`, and
the methods `send(bytes)`, `end()` and `release()`.

**Confirm the end of every session.** When the client leaves (`close`), the page may still have bytes
for it in flight: its last answer. If the next client were admitted at once, that answer would reach it.
So after a `close` the relay admits no new client until the host calls `release()`, which the host does
when the page has confirmed that it closed its channel, on the same message path as the page's bytes, so
that the confirmation comes behind them. A host that never calls `release()` is not stuck: the relay
releases itself after 3 s.

```js
// in the extension host (node)
import { TcpRelay } from './jnext-debug-bridge.mjs';
const relay = new TcpRelay();
relay.on('open',  () => panel.webview.postMessage({ t: 'open' }));
relay.on('data',  (b) => panel.webview.postMessage({ t: 'data', b: Array.from(b) }));
relay.on('close', () => panel.webview.postMessage({ t: 'close' }));
panel.webview.onDidReceiveMessage((m) => {
  if (m.t === 'data') relay.send(new Uint8Array(m.b));
  else if (m.t === 'close') relay.end();          // the machine ended the session
  else if (m.t === 'closed') relay.release();     // the page confirmed the end of the client's session
});
await relay.listen(11000);
```

```js
// in the page (the webview)
const ch = player.debugChannel('dzrp');
ch.onData  = (b) => vscode.postMessage({ t: 'data', b: Array.from(b) });
ch.onClose = () => vscode.postMessage({ t: 'close' });
window.addEventListener('message', (e) => {
  const m = e.data;
  if (m.t === 'open') ch.open();
  else if (m.t === 'data') { if (ch.isOpen) ch.send(new Uint8Array(m.b)); }   // a just-ended session
  else if (m.t === 'close') {
    if (ch.isOpen) ch.close();
    vscode.postMessage({ t: 'closed' });          // behind every `data` posted before
  }
});
```

## What to expect

- **The machine is served on the frame loop.** The player answers the debugger once for each
  frame of the page (60 times a second on most screens), and answers every command that has
  already arrived in that frame while the machine is paused. A client that sends one command
  and waits for the answer gets one round trip for each frame, about 17 ms at 60 Hz; a client
  that sends several commands in one go gets them all answered together.
- **When the frame loop stops, so does the debugger.** `player.pause()` and a tab that is
  hidden (the browser stops the page's frames) hold the debugger's commands until the player
  runs again. They are not lost.
- **Connecting a DZRP or GDB client pauses the machine**, as on the desktop, and a ZRCP client
  attaches when it connects; they release the machine when they disconnect.
- A page served over `https://` that dials `ws://127.0.0.1` may be refused or questioned by the
  browser (a secure page reaching a local address); this was not tested. A page on
  `http://localhost` is the case JNEXT's own tests use.

## Security

None of the three protocols has any authentication: whoever reaches the TCP port controls the
machine and can read and write all of its memory.

- Keep the bridge on the loopback address, its default. **Do not give `--host` an address of
  your network** unless you trust that network: the bridge then accepts a debugger from any
  computer that can reach it, and any program there can link as the page, because a client with no
  `Origin` is accepted.
- The TCP port speaks plain text lines in ZRCP. A web page can make your browser send text to
  `127.0.0.1` on the port number of a ZRCP bridge (an HTTP request is text, and ZRCP does not refuse
  lines it does not know); browsers block some ports and ask before reaching local addresses, but
  that is the browser's protection, not the bridge's. The same holds for the desktop's
  `--zrcp-port`. Run a ZRCP bridge only while you are debugging.
- Only the machine of the page is exposed: the bridge reads nothing from your disk.
