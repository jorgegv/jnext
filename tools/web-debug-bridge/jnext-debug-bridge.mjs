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
//   binary messages        the bytes of the session, in order, both ways;
//   any other text message is ignored.
//
// ONE page at a time (a second link is refused with HTTP 409) and ONE TCP client at a time (a second
// client, or one while no page is linked, is closed at once). The debug protocols have no
// authentication, so this listens on the loopback address by default, and the WebSocket accepts only a
// page whose Origin is http(s)://localhost or http(s)://127.0.0.1 (any port): without that, any web site
// the user visits could dial this port and read the debug traffic. `--origin ORIGIN` adds one origin
// (repeatable); `--any-origin` turns the check off. A client that sends no Origin header (not a browser)
// is accepted: a web page always sends one.
//
// `TcpRelay` is the half a webview host needs (a VS Code extension host, say): a TCP listener whose
// events `open`, `data` and `close` and whose methods `send` and `end` are exactly the channel's
// messages, so the host only forwards them to and from the page (postMessage).
import { EventEmitter } from 'node:events';
import crypto from 'node:crypto';
import http from 'node:http';
import net from 'node:net';
import { fileURLToPath } from 'node:url';

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const MAX_MESSAGE = 64 * 1024 * 1024;

// ---------------------------------------------------------------------------------------------
// TcpRelay: one TCP client at a time.
//   listen(port, host) -> Promise<the bound port>
//   events: 'open' (a client arrived), 'data' (Uint8Array from it), 'close' (it left),
//           'rejected' (a connection was closed at once: another client is open, or `accept` said no)
//   send(bytes) to the client, end() to end it (no 'close' follows: the caller did it), close().
//   options.accept: () => boolean, asked for each new connection (default: always).
export class TcpRelay extends EventEmitter {
  constructor(options = {}) {
    super();
    this.accept = options.accept || (() => true);
    this.client = null;
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

  send(bytes) {
    if (this.client) this.client.write(Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength));
  }

  // Ends the client's session from our side: a FIN after what was written (graceful, never a reset that
  // could cost the client the reply it is still reading); the socket is dropped anyway after 2 s.
  end() {
    const c = this.client;
    if (!c) return;
    this.client = null;
    c.end();
    setTimeout(() => c.destroy(), 2000).unref();
  }

  close() {
    this.end();
    this.server.close();
  }

  #connection(sock) {
    if (this.client || !this.accept()) {
      this.emit('rejected', sock.remoteAddress);
      sock.destroy();
      return;
    }
    this.client = sock;
    sock.on('data', (d) => { if (this.client === sock) this.emit('data', new Uint8Array(d.buffer, d.byteOffset, d.length)); });
    sock.on('error', () => {});
    // The client's FIN is the end of its session: a client that hangs up and dials again at once has
    // its new connection judged after this one is retired, not refused as a second client (the socket's
    // own 'close' event can come after the next connection).
    const gone = () => {
      if (this.client !== sock) return;   // ended by end(), or already seen
      this.client = null;
      this.emit('close');
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

// A WebSocket connection over an upgraded socket. Events: 'text' (string), 'binary' (Uint8Array),
// 'close'. A protocol violation closes with 1002 and then 'close'.
export class WsConnection extends EventEmitter {
  constructor(socket, head) {
    super();
    this.socket = socket;
    this.buf = head && head.length ? Buffer.from(head) : Buffer.alloc(0);
    this.fragOpcode = 0;
    this.frag = [];
    this.fragLen = 0;
    this.closed = false;
    socket.on('data', (d) => { this.buf = Buffer.concat([this.buf, d]); this.#parse(); });
    socket.on('error', () => {});
    socket.on('close', () => this.#finish());
    if (this.buf.length) this.#parse();
  }

  sendText(text) { this.#write(1, Buffer.from(text, 'utf8')); }
  sendBinary(bytes) { this.#write(2, Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength)); }

  close(code = 1000) {
    if (this.closed) return;
    const p = Buffer.alloc(2);
    p.writeUInt16BE(code, 0);
    this.#write(8, p);
    this.socket.end();
  }

  #write(opcode, payload) {
    if (this.closed || this.socket.destroyed) return;
    this.socket.write(encodeFrame(opcode, payload));
  }

  #finish() {
    if (this.closed) return;
    this.closed = true;
    this.emit('close');
  }

  #fail(code) {
    this.close(code);
    this.socket.destroy();
    this.#finish();
  }

  #parse() {
    for (;;) {
      const b = this.buf;
      if (b.length < 2) return;
      const fin = (b[0] & 0x80) !== 0;
      const opcode = b[0] & 0x0f;
      const masked = (b[1] & 0x80) !== 0;
      let len = b[1] & 0x7f;
      let off = 2;
      if ((b[0] & 0x70) !== 0 || !masked) return this.#fail(1002);   // reserved bits; clients must mask
      if (len === 126) {
        if (b.length < 4) return;
        len = b.readUInt16BE(2);
        off = 4;
      } else if (len === 127) {
        if (b.length < 10) return;
        const big = b.readBigUInt64BE(2);
        if (big > BigInt(MAX_MESSAGE)) return this.#fail(1009);
        len = Number(big);
        off = 10;
      }
      if (len > MAX_MESSAGE) return this.#fail(1009);
      if (b.length < off + 4 + len) return;
      const mask = b.subarray(off, off + 4);
      const payload = Buffer.alloc(len);
      for (let i = 0; i < len; i++) payload[i] = b[off + 4 + i] ^ mask[i & 3];
      this.buf = b.subarray(off + 4 + len);
      if (opcode >= 8) {   // control frames: never fragmented, at most 125 bytes
        if (!fin || len > 125) return this.#fail(1002);
        if (opcode === 8) { this.close(1000); this.socket.destroy(); this.#finish(); return; }
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
        if (op === 1) this.emit('text', whole.toString('utf8'));
        else this.emit('binary', new Uint8Array(whole.buffer, whole.byteOffset, whole.length));
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
    `jnext-debug-bridge: a client was closed at once (${pages.page ? 'one is already connected' : 'no page is linked'})\n`));
  relay.on('open', () => pages.page && pages.page.sendText('open'));
  relay.on('data', (d) => pages.page && pages.page.sendBinary(d));
  relay.on('close', () => pages.page && pages.page.sendText('close'));
  pages.on('link', (ws) => {
    process.stdout.write('jnext-debug-bridge: page linked\n');
    ws.on('text', (t) => { if (t === 'close') relay.end(); });
    ws.on('binary', (b) => relay.send(b));
  });
  pages.on('unlink', () => {
    relay.end();
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
