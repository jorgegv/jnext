#!/usr/bin/env node
// jnext-debug-bridge: lets a debugger that speaks TCP (DeZog, z88dk-gdb, telnet) reach the
// emulator in a browser page, whose `debug` option serves DZRP, ZRCP or GDB over a WebSocket.
//
//   node jnext-debug-bridge.mjs --ws-port N --tcp-port M [--host 127.0.0.1]
//                               [--origin ORIGIN]... [--any-origin]
//
//   the page   dials ws://HOST:N  (jnext.mount(el, {debug: {protocol, transport: 'ws', url}}))
//   the client connects to HOST:M (DeZog: remote type dzrp, zrcp or gdb; port M)
//
// Plain node (18 or later), the built-in modules only: no npm dependency. ASCII only.
//
// THE LINK (what the page speaks; an IDE can serve it directly):
//   text message `open`    a client arrived;
//   text message `close`   either side ended the session (from the page: the emulator ended it);
//   text message `closed`  the page's answer to the bridge's `close`: the end is confirmed, and everything the
//                          page sent for that session is behind it, so the next client may be admitted;
//   binary messages        the bytes of the session, in order, both ways;
//   any other text message is ignored.
//
// ONE page at a time (a second link is refused with HTTP 409) and ONE TCP client at a time. A client that
// dials while another is connected waits for it to end (300 ms) and is then closed; one that dials after a
// client left waits for the page to confirm the end of that session with `closed` (up to 3 s, then admitted
// anyway); one while no page is linked is closed at once. The debug protocols have no
// authentication, so this listens on the loopback address by default, and the WebSocket accepts only a
// page whose Origin is http(s)://localhost or http(s)://127.0.0.1 (any port): without that, any web site
// the user visits could dial this port and read the debug traffic. `--origin ORIGIN` adds one origin
// (repeatable); `--any-origin` turns the check off. A client that sends no Origin header (not a browser)
// is accepted: a web page always sends one.
//
// `TcpRelay` is the half a webview host needs (a VS Code extension host, say): a TCP listener whose
// events `open`, `data` and `close` and whose methods `send`, `end` and `release` are the channel's
// messages, so the host forwards them to and from the page (postMessage). After the client's `close` the host
// calls `release()` once the page has confirmed the end of that session (see TcpRelay).
import { EventEmitter } from 'node:events';
import crypto from 'node:crypto';
import http from 'node:http';
import net from 'node:net';
import { fileURLToPath } from 'node:url';

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const MAX_MESSAGE = 64 * 1024 * 1024;
const GRACE_MS = 300;
const HOLD_GRACE_MS = 3000;   // how long a redial waits for the page to confirm the end of the session before it

// ---------------------------------------------------------------------------------------------
// TcpRelay: one TCP client at a time.
//   listen(port, host) -> Promise<the bound port>
//   events: 'open' (a client arrived), 'data' (Uint8Array from it), 'close' (it left),
//           'drain' (the client's write buffer emptied, or the client was retired),
//           'rejected' (a connection was closed: another client is open, or `accept` said no)
//   send(bytes) to the client (false when its write buffer is full: wait for 'drain'), end() to end it
//   (no 'close' follows: the caller did it), release(), close().
//   options.accept: () => boolean, asked for each new connection (default: always).
//
// THE END OF A SESSION IS CONFIRMED BY THE HOST. When the client leaves ('close'), the host tells the page,
// and the page may still have bytes for that client in flight (its last reply, its own close). So the relay
// admits no new client until the host calls `release()`, which it does when the page has confirmed the end:
// everything the page sent before the confirmation then belongs to the old session and is dropped or
// delivered to the old client, never to the new one. A host that never confirms is not stuck: the relay
// releases itself after HOLD_GRACE_MS. A client that dials again meanwhile waits (up to HOLD_GRACE_MS plus a
// second); one that arrives while a client is still open and no end was seen waits GRACE_MS, because node may
// deliver a redial before the old client's FIN. end() (the host ended the session) does not hold.
export class TcpRelay extends EventEmitter {
  constructor(options = {}) {
    super();
    this.accept = options.accept || (() => true);
    this.client = null;
    this.waiting = null;     // a connection that arrived while a client was open or the end unconfirmed
    this.waitTimer = null;
    this.held = false;       // a session the client ended is not yet confirmed by the host: admit no one
    this.holdTimer = null;
    this.server = net.createServer((sock) => this.#connection(sock));
  }

  listen(port = 0, host = '127.0.0.1') {
    return new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(port, host, () => {
        this.server.off('error', reject);
        resolve(this.server.address().port);
      });
    });
  }

  get hasClient() { return this.client !== null; }

  // Bytes for the client. False when its write buffer is full: the caller stops its source until 'drain'
  // (emitted when the buffer empties, and also when the client is retired, which releases the caller).
  send(bytes) {
    if (!this.client) return true;
    return this.client.write(Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength));
  }

  // The host confirms the end of the session the client ended: the next client may come in.
  release() {
    clearTimeout(this.holdTimer);
    this.held = false;
    this.#promote();
  }

  // Stop and restart reading from the client (the caller's sink is full).
  pauseClient() { if (this.client) this.client.pause(); }
  resumeClient() { if (this.client) this.client.resume(); }

  // Ends the client's session from our side: a FIN after what was written (graceful, never a reset that
  // could cost the client the reply it is still reading). The socket is dropped 2 s after the FIN has
  // gone out, i.e. once every queued byte has been handed to the kernel: never while bytes are still
  // queued for a client that has not read them yet (a reply larger than the kernel buffers).
  end() {
    const c = this.client;
    if (!c) return;
    this.client = null;
    c.end();
    c.once('finish', () => setTimeout(() => c.destroy(), 2000).unref());
    this.emit('drain');
    this.#promote();
  }

  close() {
    this.end();
    clearTimeout(this.holdTimer);
    this.server.close();
  }

  #armWaiter(ms) {
    clearTimeout(this.waitTimer);
    this.waitTimer = setTimeout(() => {
      const w = this.waiting;
      this.waiting = null;
      if (!w) return;
      this.emit('rejected', w.remoteAddress);
      w.destroy();
    }, ms);
    this.waitTimer.unref();
  }

  #connection(sock) {
    sock.on('error', () => {});
    if (!this.client && !this.held) return this.#admit(sock);
    if (this.waiting) {
      this.emit('rejected', sock.remoteAddress);
      sock.destroy();
      return;
    }
    this.waiting = sock;
    this.#armWaiter(this.held ? HOLD_GRACE_MS + 1000 : GRACE_MS);
  }

  #promote() {
    const w = this.waiting;
    if (!w || this.client || this.held) return;
    this.waiting = null;
    clearTimeout(this.waitTimer);
    if (!w.destroyed) this.#admit(w);
  }

  #admit(sock) {
    if (!this.accept()) {
      this.emit('rejected', sock.remoteAddress);
      sock.destroy();
      return;
    }
    this.client = sock;
    sock.on('data', (d) => { if (this.client === sock) this.emit('data', new Uint8Array(d.buffer, d.byteOffset, d.length)); });
    sock.on('drain', () => { if (this.client === sock) this.emit('drain'); });
    // The client's FIN is the end of its session; the socket's own 'close' covers a reset.
    const gone = () => {
      if (this.client !== sock) return;   // ended by end(), or already seen
      this.client = null;
      this.held = true;
      clearTimeout(this.holdTimer);
      this.holdTimer = setTimeout(() => this.release(), HOLD_GRACE_MS);   // a host that never confirms
      this.holdTimer.unref();
      // A redial that arrived before this FIN is waiting on the short deadline: it waits for the host now.
      if (this.waiting) this.#armWaiter(HOLD_GRACE_MS + 1000);
      this.emit('close');
      this.emit('drain');
      sock.destroy();
    };
    sock.on('end', gone);
    sock.on('close', gone);
    this.emit('open');
  }
}

// ---------------------------------------------------------------------------------------------
// RFC 6455, the server side, hand-written: the handshake, masked client frames with 7, 16 and 64-bit
// lengths, continuation frames, ping/pong and close.

export function acceptKey(key) {
  return crypto.createHash('sha1').update(key + GUID).digest('base64');
}

// One frame to put on the wire, unmasked (a server never masks).
export function encodeFrame(opcode, payload) {
  const n = payload.length;
  let head;
  if (n < 126) head = Buffer.from([0x80 | opcode, n]);
  else if (n < 65536) { head = Buffer.alloc(4); head[0] = 0x80 | opcode; head[1] = 126; head.writeUInt16BE(n, 2); }
  else { head = Buffer.alloc(10); head[0] = 0x80 | opcode; head[1] = 127; head.writeBigUInt64BE(BigInt(n), 2); }
  return Buffer.concat([head, payload]);
}

const UTF8 = new TextDecoder('utf-8', { fatal: true });
function validUtf8(buf) {
  try { UTF8.decode(buf); return true; } catch (e) { return false; }
}
// RFC 6455 section 7.4: the codes a peer may send in a close frame.
function validCloseCode(c) {
  return (c >= 1000 && c <= 1003) || (c >= 1007 && c <= 1014) || (c >= 3000 && c <= 4999);
}

// A WebSocket connection over an upgraded socket. Events: 'text' (string), 'binary' (Uint8Array),
// 'drain' (the write buffer emptied, or the link ended), 'close'. A protocol violation closes with the
// code the RFC names (1002, 1007 or 1009) and then 'close'. The link also ends on the socket's FIN
// with no close frame (a killed browser): half-open sockets would otherwise leave it for ever.
export class WsConnection extends EventEmitter {
  constructor(socket, head) {
    super();
    this.socket = socket;
    this.buf = Buffer.alloc(0);
    this.pending = [];       // chunks not yet joined into buf: a big frame is joined once, not per chunk
    this.pendingLen = 0;
    this.need = 0;           // bytes buf + pending must hold before parsing can go further
    this.fragOpcode = 0;
    this.frag = [];
    this.fragLen = 0;
    this.closed = false;
    socket.on('data', (d) => this.#onData(d));
    socket.on('error', () => {});
    socket.on('drain', () => this.emit('drain'));
    socket.on('end', () => { socket.end(); this.#finish(); });
    socket.on('close', () => this.#finish());
    if (head && head.length) this.#onData(Buffer.from(head));
  }

  sendText(text) { return this.#write(1, Buffer.from(text, 'utf8')); }
  // False when the write buffer is full: stop the source until 'drain'.
  sendBinary(bytes) { return this.#write(2, Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength)); }
  pause() { this.socket.pause(); }
  resume() { this.socket.resume(); }

  close(code = 1000) {
    if (this.closed) return;
    const p = Buffer.alloc(2);
    p.writeUInt16BE(code, 0);
    this.#write(8, p);
    this.socket.end();
    setTimeout(() => this.socket.destroy(), 1000).unref();
  }

  #write(opcode, payload) {
    if (this.closed || this.socket.destroyed) return true;
    return this.socket.write(encodeFrame(opcode, payload));
  }

  #finish() {
    if (this.closed) return;
    this.closed = true;
    this.pending = [];
    this.buf = Buffer.alloc(0);
    this.emit('close');
    this.emit('drain');
  }

  #fail(code) {
    this.close(code);
    this.#finish();
  }

  #onData(d) {
    if (this.closed) return;
    this.pending.push(d);
    this.pendingLen += d.length;
    if (this.buf.length + this.pendingLen < this.need) return;
    this.buf = Buffer.concat([this.buf, ...this.pending]);
    this.pending = [];
    this.pendingLen = 0;
    this.#parse();
  }

  #parse() {
    for (;;) {
      if (this.closed) return;
      const b = this.buf;
      this.need = 0;
      if (b.length < 2) { this.need = 2; return; }
      const fin = (b[0] & 0x80) !== 0;
      const opcode = b[0] & 0x0f;
      const masked = (b[1] & 0x80) !== 0;
      let len = b[1] & 0x7f;
      let off = 2;
      if ((b[0] & 0x70) !== 0 || !masked) return this.#fail(1002);   // reserved bits; clients must mask
      if (len === 126) {
        if (b.length < 4) { this.need = 4; return; }
        len = b.readUInt16BE(2);
        off = 4;
      } else if (len === 127) {
        if (b.length < 10) { this.need = 10; return; }
        const big = b.readBigUInt64BE(2);
        if (big > BigInt(MAX_MESSAGE)) return this.#fail(1009);
        len = Number(big);
        off = 10;
      }
      if (len > MAX_MESSAGE) return this.#fail(1009);
      if (b.length < off + 4 + len) { this.need = off + 4 + len; return; }
      const mask = b.subarray(off, off + 4);
      const payload = Buffer.alloc(len);
      for (let i = 0; i < len; i++) payload[i] = b[off + 4 + i] ^ mask[i & 3];
      this.buf = b.subarray(off + 4 + len);
      if (opcode >= 8) {   // control frames: never fragmented, at most 125 bytes
        if (!fin || len > 125) return this.#fail(1002);
        if (opcode === 8) {
          // A close payload is empty, or a valid code and a UTF-8 reason (a 1-byte payload is neither).
          if (len === 1) return this.#fail(1002);
          if (len >= 2 && !validCloseCode(payload.readUInt16BE(0))) return this.#fail(1002);
          if (len > 2 && !validUtf8(payload.subarray(2))) return this.#fail(1007);
          this.close(1000);
          this.#finish();
          return;
        }
        if (opcode === 9) this.#write(10, payload);
        else if (opcode !== 10) return this.#fail(1002);
        continue;
      }
      if (opcode === 0) {
        if (!this.fragOpcode) return this.#fail(1002);
      } else if (opcode === 1 || opcode === 2) {
        if (this.fragOpcode) return this.#fail(1002);
        this.fragOpcode = opcode;
      } else {
        return this.#fail(1002);
      }
      this.frag.push(payload);
      this.fragLen += len;
      if (this.fragLen > MAX_MESSAGE) return this.#fail(1009);
      if (fin) {
        const whole = Buffer.concat(this.frag);
        const op = this.fragOpcode;
        this.fragOpcode = 0;
        this.frag = [];
        this.fragLen = 0;
        if (op === 1) {
          if (!validUtf8(whole)) return this.#fail(1007);
          this.emit('text', whole.toString('utf8'));
        } else {
          this.emit('binary', new Uint8Array(whole.buffer, whole.byteOffset, whole.length));
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The origin check: localhost and 127.0.0.1, http or https, any port, plus the ones given.
export function originAllowed(origin, extra, anyOrigin) {
  if (anyOrigin || origin === undefined) return true;
  if (extra.includes(origin)) return true;
  return /^https?:\/\/(localhost|127\.0\.0\.1)(:[0-9]+)?$/.test(origin);
}

// The WebSocket half: one page. Returns { server, listen(port, host) -> Promise<port>, close() }.
// Events 'link' (WsConnection) and 'unlink'.
export class PageServer extends EventEmitter {
  constructor({ origins = [], anyOrigin = false } = {}) {
    super();
    this.page = null;
    this.server = http.createServer((req, res) => { res.writeHead(426, { 'Content-Type': 'text/plain' }); res.end('WebSocket only\n'); });
    this.server.on('upgrade', (req, socket, head) => {
      // First: node removed its own listener before emitting 'upgrade'. A peer that resets while a
      // refusal is being written would otherwise be an unhandled 'error' and end the process.
      socket.on('error', () => {});
      const refuse = (status, text) => {
        socket.end(`HTTP/1.1 ${status} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
      };
      const key = req.headers['sec-websocket-key'];
      if (req.method !== 'GET' || String(req.headers.upgrade).toLowerCase() !== 'websocket' || !key ||
          req.headers['sec-websocket-version'] !== '13') return refuse(400, 'Bad Request');
      if (!originAllowed(req.headers.origin, origins, anyOrigin)) return refuse(403, 'Forbidden');
      if (this.page) return refuse(409, 'Conflict');
      socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
                   `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`);
      const ws = new WsConnection(socket, head);
      this.page = ws;
      ws.on('close', () => { if (this.page === ws) { this.page = null; this.emit('unlink'); } });
      this.emit('link', ws);
    });
  }

  listen(port = 0, host = '127.0.0.1') {
    return new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(port, host, () => {
        this.server.off('error', reject);
        resolve(this.server.address().port);
      });
    });
  }

  close() {
    if (this.page) this.page.close();
    this.server.close();
  }
}

// ---------------------------------------------------------------------------------------------
// The CLI.
function usage() {
  return 'usage: node jnext-debug-bridge.mjs --ws-port N --tcp-port M [--host ADDR] [--origin ORIGIN]... [--any-origin]\n';
}

export async function main(argv) {
  const o = { wsPort: null, tcpPort: null, host: '127.0.0.1', origins: [], anyOrigin: false };
  const port = (s) => (/^[0-9]+$/.test(s) && Number(s) <= 65535 ? Number(s) : null);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--any-origin') o.anyOrigin = true;
    else if (a === '--ws-port' || a === '--tcp-port' || a === '--host' || a === '--origin') {
      const v = argv[++i];
      if (v === undefined) { process.stderr.write(`${a} needs a value\n${usage()}`); return 2; }
      if (a === '--host') o.host = v;
      else if (a === '--origin') o.origins.push(v);
      else {
        const n = port(v);
        if (n === null) { process.stderr.write(`${a}: not a port number: ${v}\n`); return 2; }
        if (a === '--ws-port') o.wsPort = n; else o.tcpPort = n;
      }
    } else { process.stderr.write(`unknown argument ${a}\n${usage()}`); return 2; }
  }
  if (o.wsPort === null || o.tcpPort === null) { process.stderr.write(usage()); return 2; }

  const pages = new PageServer({ origins: o.origins, anyOrigin: o.anyOrigin });
  const relay = new TcpRelay({ accept: () => pages.page !== null });
  relay.on('rejected', () => process.stderr.write(
    `jnext-debug-bridge: a client was closed (${pages.page ? 'one is already connected' : 'no page is linked'})\n`));
  relay.on('open', () => pages.page && pages.page.sendText('open'));
  // Backpressure: a sink that is full stops its source until it drains (or the session ends).
  relay.on('data', (d) => {
    const page = pages.page;
    if (page && !page.sendBinary(d)) { relay.pauseClient(); page.once('drain', () => relay.resumeClient()); }
  });
  // The client ended the session: tell the page; TcpRelay admits no new client until the page confirms with
  // `closed` (release() below), or HOLD_GRACE_MS passes. The page's own `close` (if the machine ended the
  // session first) and its last bytes for the old session reach us before that confirmation, so they cannot
  // end or feed the next client.
  relay.on('close', () => { if (pages.page) pages.page.sendText('close'); });
  pages.on('link', (ws) => {
    process.stdout.write('jnext-debug-bridge: page linked\n');
    ws.on('text', (t) => {
      if (t === 'close') relay.end();
      else if (t === 'closed') relay.release();
    });
    ws.on('binary', (b) => {
      if (!relay.send(b)) { ws.pause(); relay.once('drain', () => ws.resume()); }
    });
  });
  pages.on('unlink', () => {
    relay.end();
    relay.release();
    process.stdout.write('jnext-debug-bridge: page unlinked\n');
  });

  let wsPort, tcpPort;
  try {
    wsPort = await pages.listen(o.wsPort, o.host);
    tcpPort = await relay.listen(o.tcpPort, o.host);
  } catch (e) {
    process.stderr.write(`jnext-debug-bridge: cannot listen: ${e.message}\n`);
    return 1;
  }
  process.stdout.write(`jnext-debug-bridge: page on ws://${o.host}:${wsPort}, clients on ${o.host}:${tcpPort}\n`);
  const stop = () => { relay.close(); pages.close(); setTimeout(() => process.exit(0), 100).unref(); };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
  return null;   // keep running
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  main(process.argv.slice(2)).then((rc) => { if (rc !== null) process.exit(rc); });
}
