// Browser rows BDBG-01..30: the debugger protocols (DZRP, ZRCP, GDB) served by a page, over the JS
// channel and over a WebSocket to the bridge (make wasm-browser-test, spec debug.mjs). Run by
// browser-smoke.sh on the SDK's node.
//
// The clients are the INDEPENDENT peers of the native regression rows, unchanged: dzrp-peer.py (on
// tools/cspect_dzrp, written against CSpect), zrcp-peer.py (ZEsarUX 12.0 transcripts and the committed
// fixture) and gdb-peer.py (RSP framing from the manual), each over a real TCP socket to either the
// reference bridge (tools/web-debug-bridge/jnext-debug-bridge.mjs, a child process) or a TcpRelay that
// stands in for a webview host (the channel API through page.exposeFunction). A row passes on the peer's
// own PASS line, which is the same oracle as the native dzrp-*-func, zrcp-*-func and gdb-*-func rows.
// BDBG-17 checks the bridge's WebSocket framing against FOREIGN clients: node's built-in WebSocket
// (undici) and the RFC 6455 section 1.3 handshake vector.
//
// Real servers, no context.route: Chromium applies its own rules to the WebSocket, so the harness's
// request allow-list is not in the way. The card is copyrighted: it lives in the scratch directory and in
// the page's storage only; tracing, HAR and video are never enabled.
import { Harness, waitFor, sleep } from './harness.mjs';
import { TcpRelay } from '../../tools/web-debug-bridge/jnext-debug-bridge.mjs';
import { spawn } from 'child_process';
import crypto from 'crypto';
import fs from 'fs';
import http from 'http';
import net from 'net';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256;
const BRIDGE = path.join(ROOT, 'tools/web-debug-bridge/jnext-debug-bridge.mjs');
const PEERS = path.join(ROOT, 'test/00regression');

const h = new Harness({});
const B = http.createServer((req, res) => {
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), nex: path.join(ROOT, 'test/00regression/nex') };
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream', 'Access-Control-Allow-Origin': '*' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`, BURL = `http://127.0.0.1:${B.address().port}`;
const IMAGE = { url: BURL + '/card.img', sha256: SHA };

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };
const CLEAN_ENV = { PATH: E.PATH, LANG: 'C', LC_ALL: 'C' };
const cleanup = [];   // child processes and pages to drop at the end of a row

// ---- the bridge (a child process on ports 0) ------------------------------------------------------
async function startBridge(extra = [], wsPort = 0) {
  const c = spawn(process.execPath, [BRIDGE, '--ws-port', String(wsPort), '--tcp-port', '0', ...extra], { env: CLEAN_ENV });
  const b = { child: c, out: '', err: '', wsPort: 0, tcpPort: 0, exited: false };
  c.stdout.on('data', (d) => { b.out += d; });
  c.stderr.on('data', (d) => { b.err += d; });
  c.on('exit', () => { b.exited = true; });
  cleanup.push(() => { if (!b.exited) c.kill('SIGKILL'); });
  b.wait = async (re, ms = 20000) => {
    const t0 = Date.now();
    while (!re.test(b.out)) {
      if (Date.now() - t0 > ms) throw new Error(`bridge: no ${re} in ${ms} ms: ${b.out} ${b.err}`);
      if (b.exited) throw new Error('bridge exited: ' + b.err);
      await sleep(20);
    }
  };
  b.kill = async () => { if (!b.exited) { c.kill('SIGKILL'); while (!b.exited) await sleep(10); } };
  await b.wait(/clients on/);
  const m = /ws:\/\/[^:]+:(\d+), clients on [^:]+:(\d+)/.exec(b.out);
  b.wsPort = Number(m[1]); b.tcpPort = Number(m[2]);
  b.url = `ws://127.0.0.1:${b.wsPort}`;
  return b;
}
const linked = (b, n = 1) => b.wait(new RegExp(`(page linked[\\s\\S]*){${n}}`), 20000);

// ---- a peer: one of the native regression clients, over TCP to `port` ------------------------------
function peer(script, scenario, port) {
  return new Promise((resolve) => {
    const c = spawn('python3', ['-I', path.join(PEERS, script), scenario, String(port)], { env: CLEAN_ENV });
    let out = '';
    c.stdout.on('data', (d) => { out += d; });
    c.stderr.on('data', (d) => { out += d; });
    const t = setTimeout(() => c.kill('SIGKILL'), 150000);
    c.on('close', (rc) => { clearTimeout(t); resolve({ rc, out: out.trim() }); });
  });
}
const peerOk = (r) => r.rc === 0 && /^PASS /m.test(r.out);
const must_peer = (r, what, ...bridges) => must(peerOk(r), `${what}: rc=${r.rc} ${r.out.slice(-400)}` +
  bridges.map((b) => ` [bridge: ${b.out.trim().replace(/\n/g, ' / ')} ${b.err.trim().replace(/\n/g, ' / ')}]`).join(''));

// ---- the page ----------------------------------------------------------------------------------------
async function newPage() {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx; page.msgs = []; page.console = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  page.on('console', (m) => page.console.push(m.text()));
  cleanup.push(() => ctx.close().catch(() => {}));
  return page;
}
async function open(page, players) {
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(IMAGE), players: JSON.stringify(players) });
  await page.goto(AURL + '/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length);
}
const rec = (page) => page.evaluate(() => {
  const r = window.apiTest.players[0];
  return { ready: r.ready, errors: r.errors, mountError: r.mountError, state: r.player ? r.player.state : null };
});
const click = (page) => page.click('#p0 .jnext-overlay', { timeout: 600000, noWaitAfter: true });
const waitReady = (page) => waitFor(page, () => window.apiTest.players[0].ready >= 1 || window.apiTest.players[0].errors.length > 0, null, 300000);
// A started page: opened, clicked, first frame shown.
async function started(debug, args = []) {
  const page = await newPage();
  await open(page, [{ machine: '48k', muted: true, debug, args }]);
  await click(page);
  await waitReady(page);
  const r = await rec(page);
  must(r.errors.length === 0 && r.state === 'running', 'page: ' + JSON.stringify(r));
  return page;
}
const ws = (protocol, b) => ({ protocol, transport: 'ws', url: b.url });
const MAGIC = ['--magic-breakpoint'];

// ---- the channel through a TcpRelay, as a webview host would use it -------------------------------
// The host (node) forwards: a TCP client arriving -> channel.open(), its bytes -> send(), its leaving ->
// close(); the page forwards onData and onClose back through exposed functions. The calls into the page
// go through one promise chain, so the order is the order of arrival.
// `delayMs` models a host that is far away (postMessage): every message of the page to the host (bytes, end,
// confirmation) is delayed by it, in order.
async function channelRelay(page, protocol, delayMs = 0) {
  const events = [];
  let chain = Promise.resolve();
  const q = (fn) => { chain = chain.then(fn).catch((e) => events.push({ kind: 'error', text: String(e.message || e) })); return chain; };
  const relay = new TcpRelay();
  await page.exposeFunction('__hostData', (b64) => {
    const b = Buffer.from(b64, 'base64');
    events.push({ kind: 'data', text: b.toString('latin1') });
    relay.send(b);
  });
  await page.exposeFunction('__hostClose', () => { events.push({ kind: 'close' }); relay.end(); });
  // The page's confirmation that it has closed its channel for the client that left: it travels behind every
  // byte the page posted before, so the host releases the relay (admits the next client) only after them.
  await page.exposeFunction('__hostClosed', () => relay.release());
  await page.evaluate(([p, delay]) => {
    const ch = window.player0.debugChannel(p);
    window.__post = (fn) => setTimeout(fn, delay);
    ch.onData = (u8) => {
      let s = '';
      for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192));
      const b64 = btoa(s);
      window.__post(() => window.__hostData(b64));
    };
    ch.onClose = () => window.__post(() => window.__hostClose());
  }, [protocol, delayMs]);
  relay.on('open', () => q(() => page.evaluate((p) => { window.player0.debugChannel(p).open(); }, protocol)));
  relay.on('data', (d) => q(() => page.evaluate(([p, b64]) => {
    const s = atob(b64), u8 = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
    window.player0.debugChannel(p).send(u8);
  }, [protocol, Buffer.from(d).toString('base64')])));
  relay.on('close', () => q(() => page.evaluate((p) => {
    try { window.player0.debugChannel(p).close(); } catch (e) { /* the machine ended it */ }
    window.__post(() => window.__hostClosed());
  }, protocol)));
  const port = await relay.listen(0);
  cleanup.push(() => relay.close());
  return { relay, port, events };
}

// ---- DZRP bytes for the in-page rows ---------------------------------------------------------------
const INIT = [5, 0, 0, 0, 1, 1, 2, 2, 0, 0x74, 0];   // len 5 (the payload), seq 1, CMD_INIT, 2.2.0 "t\0"
const inPage = (page, fn, arg) => page.evaluate(fn, arg);
// In the page: send `bytes` on a channel and resolve with the bytes that come back (until `n` arrived).
const EXCHANGE = `(async (a) => {
  const ch = window.player0.debugChannel(a.protocol), got = [];
  ch.onData = (u8) => got.push(...u8);
  ch.send(new Uint8Array(a.bytes));
  const t0 = Date.now();
  while (got.length < a.n && Date.now() - t0 < 15000) await new Promise((r) => setTimeout(r, 20));
  return got;
})`;
const exchange = (page, protocol, bytes, n) => page.evaluate(`${EXCHANGE}(${JSON.stringify({ protocol, bytes, n })})`);
const isInitReply = (got) => got.length >= 17 && got[4] === 1 && got.slice(5, 10).join() === '0,2,2,0,4' &&
                             String.fromCharCode(...got.slice(10, 17)) === 'jnext v';

// =================================================================================================
row('BDBG-01', 'DZRP over a WebSocket to the bridge: dzrp-peer.py init-regs PASS (INIT as 2.0.0 and 2.2.0, 270 seqs)', async () => {
  const b = await startBridge();
  const page = await started(ws('dzrp', b));
  await linked(b);
  const r = await peer('dzrp-peer.py', 'init-regs', b.tcpPort);
  must_peer(r, 'init-regs', b);
});

row('BDBG-02', 'DZRP over a WebSocket: dzrp-peer.py bp-continue PASS (breakpoint, continue, the stop notification, the bank byte)', async () => {
  const b = await startBridge();
  const page = await started(ws('dzrp', b));
  await linked(b);
  must_peer(await peer('dzrp-peer.py', 'bp-continue', b.tcpPort), 'bp-continue');
});

row('BDBG-03', 'DZRP over the JS channel through a TcpRelay (a webview host): dzrp-peer.py init-regs PASS', async () => {
  const page = await started({ protocol: 'dzrp' });
  const c = await channelRelay(page, 'dzrp');
  must_peer(await peer('dzrp-peer.py', 'init-regs', c.port), 'init-regs');
  must(!c.events.some((e) => e.kind === 'error'), JSON.stringify(c.events.filter((e) => e.kind === 'error')));
});

row('BDBG-04', 'DZRP over the JS channel: dzrp-peer.py bp-continue PASS', async () => {
  const page = await started({ protocol: 'dzrp' });
  const c = await channelRelay(page, 'dzrp');
  must_peer(await peer('dzrp-peer.py', 'bp-continue', c.port), 'bp-continue');
});

row('BDBG-05', 'ZRCP over a WebSocket: zrcp-peer.py smoke PASS (the ZEsarUX 12.0 welcome byte for byte, cpu-step, quit)', async () => {
  const b = await startBridge();
  const page = await started(ws('zrcp', b), MAGIC);
  await linked(b);
  must_peer(await peer('zrcp-peer.py', 'smoke', b.tcpPort), 'smoke');
});

row('BDBG-06', 'ZRCP over the JS channel: zrcp-peer.py smoke PASS; onClose fires exactly once, after the data that carries "Sayonara baby"', async () => {
  const page = await started({ protocol: 'zrcp' }, MAGIC);
  const c = await channelRelay(page, 'zrcp');
  must_peer(await peer('zrcp-peer.py', 'smoke', c.port), 'smoke');
  await sleep(300);
  const closes = c.events.filter((e) => e.kind === 'close').length;
  const last = c.events.map((e) => e.kind).lastIndexOf('close');
  const say = c.events.findIndex((e) => e.kind === 'data' && e.text.includes('Sayonara baby\n'));
  must(closes === 1, `onClose fired ${closes} times`);
  must(say >= 0 && say < last, `Sayonara at event ${say}, close at ${last}`);
});

row('BDBG-07', 'GDB over a WebSocket: gdb-peer.py handshake PASS (qSupported, target.xml, ?, g, m, s, monitor mmu, D)', async () => {
  const b = await startBridge();
  const page = await started(ws('gdb', b));
  await linked(b);
  must_peer(await peer('gdb-peer.py', 'handshake', b.tcpPort), 'handshake');
});

row('BDBG-08', 'GDB over the JS channel: gdb-peer.py handshake PASS', async () => {
  const page = await started({ protocol: 'gdb' });
  const c = await channelRelay(page, 'gdb');
  must_peer(await peer('gdb-peer.py', 'handshake', c.port), 'handshake');
});

row('BDBG-09', 'one page with all three protocols over three bridges: init-regs, smoke and handshake PASS in turn; the log has the three "listening on channel:0" lines', async () => {
  const bd = await startBridge(), bz = await startBridge(), bg = await startBridge();
  const page = await started([ws('gdb', bg), ws('dzrp', bd), ws('zrcp', bz)], MAGIC);
  await Promise.all([linked(bd), linked(bz), linked(bg)]);
  const log = page.console.join('\n');
  for (const p of ['dzrp', 'zrcp', 'gdb']) must(log.includes(`${p}: listening on channel:0`), `no "${p}: listening on channel:0" in the page log`);
  must_peer(await peer('dzrp-peer.py', 'init-regs', bd.tcpPort), 'init-regs');
  must_peer(await peer('zrcp-peer.py', 'smoke', bz.tcpPort), 'smoke');
  must_peer(await peer('gdb-peer.py', 'handshake', bg.tcpPort), 'handshake');
});

row('BDBG-10', 'player.argv carries --debug-channel dzrp for a dzrp page; a page with no debug has none, no "listening on channel" line, and debugChannel("dzrp") throws', async () => {
  const withDbg = await started({ protocol: 'dzrp' });
  const argv1 = await withDbg.evaluate(() => window.player0.argv.slice());
  const listen1 = withDbg.console.filter((l) => l.includes('dzrp: listening on channel:0')).length;
  const plain = await started(undefined);
  const argv2 = await plain.evaluate(() => window.player0.argv.slice());
  const throws = await plain.evaluate(() => { try { window.player0.debugChannel('dzrp'); return 'no throw'; } catch (e) { return e.message; } });
  const listen2 = plain.console.filter((l) => l.includes('listening on channel')).length;
  const i = argv1.indexOf('--debug-channel');
  must(i >= 0 && argv1[i + 1] === 'dzrp' && argv1.filter((a) => a === '--debug-channel').length === 1, 'argv ' + JSON.stringify(argv1));
  must(listen1 === 1, `dzrp page: ${listen1} "listening on channel" lines`);
  must(!argv2.includes('--debug-channel') && listen2 === 0, `plain page: argv ${JSON.stringify(argv2)}, ${listen2} lines`);
  must(throws === "jnext: the page has no debug entry for 'dzrp'", 'debugChannel: ' + throws);
});

row('BDBG-11', 'mount() rejects with the build_argv reason naming debug, and onError gets it: protocol x, ws without a url, a url with channel, a duplicate protocol', async () => {
  const cases = [
    [{ protocol: 'x' }, 'protocol must be'],
    [{ protocol: 'dzrp', transport: 'ws' }, 'needs a url'],
    [{ protocol: 'dzrp', transport: 'channel', url: 'ws://127.0.0.1:1' }, 'url is only for transport'],
    [[{ protocol: 'dzrp' }, { protocol: 'dzrp' }], 'given twice'],
  ];
  const bad = [];
  for (const [debug, want] of cases) {
    const page = await newPage();
    await open(page, [{ machine: '48k', muted: true, debug }]);
    const r = await rec(page);
    await page.ctx.close();
    if (!(r.mountError && r.mountError.includes("option 'debug'") && r.mountError.includes(want) &&
          r.errors.length === 1 && r.errors[0] === r.mountError)) bad.push(`${JSON.stringify(debug)} -> ${JSON.stringify(r)}`);
  }
  must(bad.length === 0, bad.join(' | '));
});

row('BDBG-12', 'misuse: debugChannel of a ws entry throws; open() before the click throws "not running"; send() before open() throws; open() twice throws; close() then open() admits a new session answered by CMD_INIT', async () => {
  const page = await newPage();
  await open(page, [{ machine: '48k', muted: true, debug: [{ protocol: 'dzrp' }, { protocol: 'gdb', transport: 'ws', url: 'ws://127.0.0.1:1' }] }]);
  const t = (fn) => page.evaluate(`(() => { try { (${fn})(); return 'no throw'; } catch (e) { return e.message; } })()`);
  const wsThrows = await t("() => window.player0.debugChannel('gdb')");
  const early = await t("() => window.player0.debugChannel('dzrp').open()");
  await click(page);
  await waitReady(page);
  const noOpen = await t("() => window.player0.debugChannel('dzrp').send(new Uint8Array([1]))");
  await page.evaluate(() => window.player0.debugChannel('dzrp').open());
  const twice = await t("() => window.player0.debugChannel('dzrp').open()");
  const first = await exchange(page, 'dzrp', INIT, 17);
  await page.evaluate(() => window.player0.debugChannel('dzrp').close());
  const closed = await t("() => window.player0.debugChannel('dzrp').close()");
  await page.evaluate(() => window.player0.debugChannel('dzrp').open());
  const second = await exchange(page, 'dzrp', INIT, 17);
  must(wsThrows === "jnext: the debug channel 'gdb' is served over WebSocket", 'ws entry: ' + wsThrows);
  must(early === 'jnext: the machine is not running', 'before the click: ' + early);
  must(noOpen === 'jnext: the debug channel is not open', 'send before open: ' + noOpen);
  must(twice === 'jnext: the debug channel is already open', 'open twice: ' + twice);
  must(closed === 'jnext: the debug channel is not open', 'close twice: ' + closed);
  must(isInitReply(first) && isInitReply(second), `INIT answers ${first.length} / ${second.length} bytes`);
});

row('BDBG-13', 'a page mounted while nothing listens on its ws port: the bridge started afterwards links within 3 s and init-regs PASSes; killed and restarted on the same port it relinks and init-regs PASSes again; the console says the link is down once for each episode, not for each redial', async () => {
  const probe = await startBridge();   // only to learn a free port
  const port = probe.wsPort;
  await probe.kill();
  const page = await started({ protocol: 'dzrp', transport: 'ws', url: `ws://127.0.0.1:${port}` });
  const down = () => page.console.filter((l) => /debug link .* is down/.test(l)).length;
  await sleep(3500);   // three or four failed dials: one console line for the whole episode
  must(down() === 1, `${down()} "debug link ... is down" lines in the first down episode`);
  const b1 = await startBridge([], port);
  const t0 = Date.now();
  await linked(b1);
  must(Date.now() - t0 < 3000, `linked after ${Date.now() - t0} ms`);
  must_peer(await peer('dzrp-peer.py', 'init-regs', b1.tcpPort), 'first init-regs', b1);
  await b1.kill();
  await sleep(3500);   // the link is down again: one more line, however many redials
  must(down() === 2, `${down()} "debug link ... is down" lines after the second down episode`);
  const b2 = await startBridge([], port);
  const t1 = Date.now();
  await linked(b2);
  must(Date.now() - t1 < 3000, `relinked after ${Date.now() - t1} ms`);
  must_peer(await peer('dzrp-peer.py', 'init-regs', b2.tcpPort), 'second init-regs');
});

// A raw upgrade request: resolves with the status line and the (open) socket.
const upgradeRequest = (port, origin, key = 'dGhlIHNhbXBsZSBub25jZQ==') =>
  `GET / HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
  `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n${origin ? `Origin: ${origin}\r\n` : ''}\r\n`;
function upgrade(port, origin, key) {
  return new Promise((resolve, reject) => {
    const s = net.connect(port, '127.0.0.1');
    let data = '';
    s.on('data', (d) => { data += d.toString('latin1'); if (data.includes('\r\n\r\n')) resolve({ status: data.split('\r\n')[0], head: data, socket: s, rx: [] }); });
    s.on('error', reject);
    s.on('connect', () => s.write(upgradeRequest(port, origin, key)));
  });
}
// A refused upgrade the client resets as soon as it has sent it.
const resetOnce = (port, origin) => new Promise((resolve) => {
  const s = net.connect(port, '127.0.0.1');
  s.on('error', () => {});
  s.on('close', resolve);
  s.on('connect', () => { s.write(upgradeRequest(port, origin)); s.resetAndDestroy(); });
});
// Retry an upgrade until it gets a 101 (or ms run out): the link a bridge frees is freed on an event.
async function upgradeOk(port, ms = 4000) {
  const t0 = Date.now();
  for (;;) {
    const u = await upgrade(port, 'http://localhost:1');
    if (/^HTTP\/1\.1 101/.test(u.status) || Date.now() - t0 > ms) return u;
    u.socket.destroy();
    await sleep(50);
  }
}

row('BDBG-14', 'bridge: a second TCP client during a session gets EOF at once and the first still answers; a page link from Origin http://evil.example gets HTTP 403, and --origin http://evil.example admits it; a second page gets 409', async () => {
  const b = await startBridge();
  const page = await started({ protocol: 'dzrp', transport: 'ws', url: b.url });
  await linked(b);
  const c1 = net.connect(b.tcpPort, '127.0.0.1');
  const rx1 = [];
  c1.on('data', (d) => rx1.push(...d));
  await new Promise((r) => c1.on('connect', r));
  c1.write(Buffer.from(INIT));
  const t0 = Date.now();
  while (rx1.length < 17 && Date.now() - t0 < 15000) await sleep(20);
  must(isInitReply(rx1), 'the first client got no INIT answer');
  const c2 = net.connect(b.tcpPort, '127.0.0.1');
  const eof = await new Promise((resolve) => {
    let got = 0;
    c2.on('data', (d) => { got += d.length; });
    c2.on('close', () => resolve(got));
    c2.on('error', () => {});
    setTimeout(() => resolve(-1), 5000);
  });
  rx1.length = 0;
  c1.write(Buffer.from([2, 0, 0, 0, 7, 15, 0x68, 0x69]));   // CMD_LOOPBACK "hi", seq 7
  const t1 = Date.now();
  while (rx1.length < 7 && Date.now() - t1 < 15000) await sleep(20);
  c1.destroy();
  const evil = await upgrade(b.wsPort, 'http://evil.example');
  evil.socket.destroy();
  const second = await upgrade(b.wsPort, 'http://localhost:1');
  second.socket.destroy();
  const b2 = await startBridge(['--origin', 'http://evil.example']);
  const ok = await upgrade(b2.wsPort, 'http://evil.example');
  ok.socket.destroy();
  must(eof === 0, `the second client: ${eof === -1 ? 'not closed' : eof + ' bytes before EOF'}`);
  must(rx1.join() === '3,0,0,0,7,104,105', 'the first client after the second: ' + rx1.join());
  must(/^HTTP\/1\.1 403/.test(evil.status), 'evil origin: ' + evil.status);
  must(/^HTTP\/1\.1 409/.test(second.status), 'second page: ' + second.status);
  must(/^HTTP\/1\.1 101/.test(ok.status), '--origin: ' + ok.status);
});

row('BDBG-15', 'player.pause(): a DZRP command sent while paused is not answered until resume(), then answered with the right seq', async () => {
  const page = await started({ protocol: 'dzrp' });
  await page.evaluate(() => window.player0.debugChannel('dzrp').open());
  must(isInitReply(await exchange(page, 'dzrp', INIT, 17)), 'INIT');
  const r = await page.evaluate(async () => {
    const ch = window.player0.debugChannel('dzrp'), got = [];
    ch.onData = (u8) => got.push(...u8);
    window.player0.pause();
    ch.send(new Uint8Array([2, 0, 0, 0, 9, 15, 0x68, 0x69]));   // CMD_LOOPBACK "hi", seq 9
    await new Promise((x) => setTimeout(x, 800));
    const whilePaused = got.length;
    window.player0.resume();
    const t0 = Date.now();
    while (got.length < 7 && Date.now() - t0 < 15000) await new Promise((x) => setTimeout(x, 20));
    return { whilePaused, got };
  });
  must(r.whilePaused === 0, `${r.whilePaused} bytes while paused`);
  must(r.got.join() === '3,0,0,0,9,104,105', 'after resume: ' + r.got.join());
});

row('BDBG-16', 'destroy() with a channel session open: onClose once; a ws page\'s TCP client gets EOF', async () => {
  const page = await started({ protocol: 'dzrp' });
  const n = await page.evaluate(async () => {
    const ch = window.player0.debugChannel('dzrp');
    let closes = 0;
    ch.onClose = () => { closes++; };
    ch.open();
    await window.player0.destroy();
    await new Promise((x) => setTimeout(x, 300));
    return closes;
  });
  must(n === 1, `onClose fired ${n} times`);
  const b = await startBridge();
  const p2 = await started({ protocol: 'dzrp', transport: 'ws', url: b.url });
  await linked(b);
  const c = net.connect(b.tcpPort, '127.0.0.1');
  const rx = [];
  c.on('data', (d) => rx.push(...d));
  await new Promise((r) => c.on('connect', r));
  c.write(Buffer.from(INIT));
  const t0 = Date.now();
  while (rx.length < 17 && Date.now() - t0 < 15000) await sleep(20);
  must(isInitReply(rx), 'no INIT answer before destroy');
  const eof = new Promise((resolve) => { c.on('close', () => resolve(true)); setTimeout(() => resolve(false), 10000); });
  await p2.evaluate(() => window.player0.destroy());
  must(await eof, 'the TCP client was not closed after destroy()');
});

// A masked client frame, as a FOREIGN writer: built here, never by the bridge's own code.
function maskedFrame(fin, opcode, payload) {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([(fin ? 0x80 : 0) | opcode, 0x80 | n])
    : n < 65536 ? Buffer.from([(fin ? 0x80 : 0) | opcode, 0x80 | 126, n >> 8, n & 255])
      : (() => { const b = Buffer.alloc(10); b[0] = (fin ? 0x80 : 0) | opcode; b[1] = 0x80 | 127; b.writeBigUInt64BE(BigInt(n), 2); return b; })();
  const mask = crypto.randomBytes(4);
  const body = Buffer.alloc(n);
  for (let i = 0; i < n; i++) body[i] = payload[i] ^ mask[i & 3];
  return Buffer.concat([head, mask, body]);
}

row('BDBG-17', 'bridge framing against FOREIGN clients: node\'s WebSocket sends binary messages of 1, 125, 126, 65535, 65536 and 70000 bytes and a TCP client reads them byte-identical, the reverse likewise; the RFC 6455 handshake vector; a fragmented message is reassembled; a ping is ponged', async () => {
  const b = await startBridge();
  // The page is node's own WebSocket (undici).
  const w = new WebSocket(b.url);
  w.binaryType = 'arraybuffer';
  const msgs = [];
  w.onmessage = (ev) => msgs.push(typeof ev.data === 'string' ? ev.data : Buffer.from(ev.data));
  await new Promise((resolve, reject) => { w.onopen = resolve; w.onerror = () => reject(new Error('WebSocket error: ' + b.err)); });
  const c = net.connect(b.tcpPort, '127.0.0.1');
  const rx = [];
  c.on('data', (d) => rx.push(d));
  await new Promise((r) => c.on('connect', r));
  const t0 = Date.now();
  while (!msgs.includes('open') && Date.now() - t0 < 5000) await sleep(10);
  must(msgs[0] === 'open', 'the page was not told `open`: ' + JSON.stringify(msgs.slice(0, 2)));
  const sizes = [1, 125, 126, 65535, 65536, 70000];
  const sent = sizes.map((n) => crypto.randomBytes(n));
  for (const s of sent) w.send(s);
  const want = Buffer.concat(sent);
  const t1 = Date.now();
  while (Buffer.concat(rx).length < want.length && Date.now() - t1 < 15000) await sleep(20);
  const got = Buffer.concat(rx);
  must(got.equals(want), `page to client: ${got.length} of ${want.length} bytes, equal ${got.equals(want)}`);
  // The other way: one big write, however the bridge chunks it.
  const back = crypto.randomBytes(200000);
  const before = msgs.length;
  c.write(back);
  const t2 = Date.now();
  const sum = () => Buffer.concat(msgs.slice(before).filter((m) => typeof m !== 'string')).length;
  while (sum() < back.length && Date.now() - t2 < 15000) await sleep(20);
  const gotBack = Buffer.concat(msgs.slice(before).filter((m) => typeof m !== 'string'));
  must(gotBack.equals(back), `client to page: ${gotBack.length} of ${back.length} bytes, equal ${gotBack.equals(back)}`);
  w.close();
  c.destroy();
  await sleep(200);

  // The RFC 6455 section 1.3 handshake vector, then a fragmented message and a ping, on a raw socket.
  const up = await upgrade(b.wsPort, 'http://localhost:1', 'dGhlIHNhbXBsZSBub25jZQ==');
  must(/^HTTP\/1\.1 101/.test(up.status) && /Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK\+xOo=\r\n/i.test(up.head),
    'the handshake: ' + up.head);
  const raw = up.socket;
  const rawRx = [];
  raw.on('data', (d) => rawRx.push(d));
  const c2 = net.connect(b.tcpPort, '127.0.0.1');
  const rx2 = [];
  c2.on('data', (d) => rx2.push(d));
  await new Promise((r) => c2.on('connect', r));
  await sleep(200);
  const parts = [Buffer.from('frag-'), Buffer.from('men'), Buffer.from('ted')];
  raw.write(Buffer.concat([maskedFrame(false, 2, parts[0]), maskedFrame(false, 0, parts[1]), maskedFrame(true, 0, parts[2])]));
  raw.write(maskedFrame(true, 9, Buffer.from('ping!')));
  const t3 = Date.now();
  while ((Buffer.concat(rx2).toString() !== 'frag-mented' || !Buffer.concat(rawRx).includes(Buffer.from([0x8a, 5, 0x70, 0x69, 0x6e, 0x67, 0x21]))) && Date.now() - t3 < 5000) await sleep(20);
  const frag = Buffer.concat(rx2).toString();
  const pong = Buffer.concat(rawRx).includes(Buffer.from([0x8a, 5, 0x70, 0x69, 0x6e, 0x67, 0x21]));
  raw.destroy();
  c2.destroy();
  must(frag === 'frag-mented', 'the fragmented message reached the client as ' + JSON.stringify(frag));
  must(pong, 'no pong for the ping');
});


row('BDBG-18', 'bridge: 40 refused upgrades (403) and 40 more refused with 409 beside a linked page, each reset by the client, leave the bridge alive; a good link still gets 101', async () => {
  const b = await startBridge();
  for (let i = 0; i < 40; i++) await resetOnce(b.wsPort, 'http://evil.example');
  const first = await upgradeOk(b.wsPort);
  must(/^HTTP\/1\.1 101/.test(first.status), 'the first good link: ' + first.status);
  for (let i = 0; i < 40; i++) await resetOnce(b.wsPort, 'http://localhost:1');
  await sleep(300);
  must(!b.exited, 'the bridge died: ' + b.err.slice(0, 300));
  first.socket.destroy();
  const again = await upgradeOk(b.wsPort);
  again.socket.destroy();
  must(/^HTTP\/1\.1 101/.test(again.status), 'a link after the resets: ' + again.status);
});

row('BDBG-19', 'bridge: a page link that ends in a FIN with no close frame frees the slot: the TCP client gets EOF and a new link gets 101', async () => {
  const b = await startBridge();
  const link = await upgradeOk(b.wsPort);
  must(/^HTTP\/1\.1 101/.test(link.status), 'link: ' + link.status);
  const c = net.connect(b.tcpPort, '127.0.0.1');
  const eof = new Promise((resolve) => { c.on('close', () => resolve(true)); c.on('error', () => {}); setTimeout(() => resolve(false), 4000); });
  await new Promise((r) => c.on('connect', r));
  await sleep(200);
  link.socket.end();   // FIN only: a killed browser
  must(await eof, 'the TCP client was held open after the page link ended');
  const again = await upgradeOk(b.wsPort);
  again.socket.destroy();
  must(/^HTTP\/1\.1 101/.test(again.status), 'a new link after the FIN: ' + again.status);
});

row('BDBG-20', 'the machine exits by itself with a session open: onClose fires exactly once (JS channel), and a ws page\'s TCP client gets EOF', async () => {
  const exitArgs = ['--delayed-automatic-exit-frames', '600'];
  const page = await started({ protocol: 'dzrp' }, exitArgs);
  const opened = await page.evaluate(() => {
    const ch = window.player0.debugChannel('dzrp');
    window.__closes = 0;
    ch.onClose = () => { window.__closes++; };
    ch.open();
    return ch.isOpen;
  });
  must(opened, 'open() did not open');
  await waitFor(page, () => window.player0.state === 'exited', null, 120000);
  await sleep(500);
  const closes = await page.evaluate(() => window.__closes);
  const stillOpen = await page.evaluate(() => window.player0.debugChannel('dzrp').isOpen);
  must(closes === 1 && !stillOpen, `onClose fired ${closes} times, isOpen ${stillOpen}`);
  const b = await startBridge();
  const p2 = await started({ protocol: 'dzrp', transport: 'ws', url: b.url }, exitArgs);
  await linked(b);
  const c = net.connect(b.tcpPort, '127.0.0.1');
  await new Promise((r) => c.on('connect', r));
  c.on('error', () => {});
  const eof = await new Promise((resolve) => { c.on('close', () => resolve(true)); setTimeout(() => resolve(false), 120000); });
  must(eof, 'the TCP client was not closed when the machine exited');
});

row('BDBG-21', 'the session ends right after a reply larger than the kernel buffers: ZRCP over ws, 100 x read-memory 0 65535 and quit in one write, the client reading 4 s late: every reply and "Sayonara baby" arrive before EOF', async () => {
  const b = await startBridge();
  const page = await started(ws('zrcp', b), MAGIC);
  await linked(b);
  const c = net.connect(b.tcpPort, '127.0.0.1');
  await new Promise((r) => c.on('connect', r));
  const N = 100;
  c.write('read-memory 0 65535\n'.repeat(N) + 'quit\n');
  await sleep(4000);   // nothing is read meanwhile
  const chunks = [];
  const done = new Promise((resolve) => { c.on('close', () => resolve(true)); setTimeout(() => resolve(false), 90000); });
  c.on('data', (d) => chunks.push(d));
  c.on('error', () => {});
  must(await done, 'no EOF');
  const text = Buffer.concat(chunks).toString('latin1');
  const prompts = text.split('command> ').length - 1;
  must(text.length >= N * 131070, `${text.length} bytes, expected at least ${N * 131070}`);
  must(prompts >= N + 1, `${prompts} prompts, expected at least ${N + 1}`);
  must(text.endsWith('Sayonara baby\n'), 'the stream does not end with Sayonara baby: ' + JSON.stringify(text.slice(-60)));
});

row('BDBG-22', 'backpressure: a TCP client that reads nothing makes the bridge stop reading the page link (the sender\'s bufferedAmount stays high); once it reads, all 48 MiB arrive byte-identical', async () => {
  const b = await startBridge();
  const w = new WebSocket(b.url);
  w.binaryType = 'arraybuffer';
  const msgs = [];
  w.onmessage = (ev) => msgs.push(ev.data);
  await new Promise((resolve, reject) => { w.onopen = resolve; w.onerror = () => reject(new Error('WebSocket error')); });
  const c = net.connect(b.tcpPort, '127.0.0.1');
  await new Promise((r) => c.on('connect', r));
  const MiB = 1 << 20, N = 48;
  const hash = crypto.createHash('sha1');
  for (let i = 0; i < N; i++) {
    const m = crypto.randomBytes(MiB);
    hash.update(m);
    w.send(m);
  }
  await sleep(1500);
  const buffered = w.bufferedAmount;
  const got = crypto.createHash('sha1');
  let total = 0;
  c.on('data', (d) => { got.update(d); total += d.length; });
  const t0 = Date.now();
  while (total < N * MiB && Date.now() - t0 < 60000) await sleep(50);
  w.close();
  c.destroy();
  must(buffered > 8 * MiB, `bufferedAmount ${buffered} after 1.5 s: the bridge read the link though the client did not read`);
  must(total === N * MiB && got.digest('hex') === hash.digest('hex'), `${total} of ${N * MiB} bytes, digests ${total === N * MiB ? 'differ' : 'n/a'}`);
});

row('BDBG-23', 'bridge framing, violations: a 1-byte close payload, close codes 1005 and 999, and a close reason that is not UTF-8 are refused (1002, 1002, 1002, 1007); a text message that is not UTF-8 is 1007; a close with code 3000 is answered 1000', async () => {
  const b = await startBridge();
  const closeCode = async (frame) => {
    const u = await upgradeOk(b.wsPort);
    must(/^HTTP\/1\.1 101/.test(u.status), 'link: ' + u.status);
    const rx = [];
    u.socket.on('data', (d) => rx.push(d));
    u.socket.write(frame);
    const t0 = Date.now();
    while (Buffer.concat(rx).length < 4 && Date.now() - t0 < 4000) await sleep(20);
    const r = Buffer.concat(rx);
    u.socket.destroy();
    return r.length >= 4 && r[0] === 0x88 ? r.readUInt16BE(2) : -1;
  };
  const got = [
    await closeCode(maskedFrame(true, 8, Buffer.from([0x03]))),
    await closeCode(maskedFrame(true, 8, Buffer.from([0x03, 0xed]))),
    await closeCode(maskedFrame(true, 8, Buffer.from([0x03, 0xe7]))),
    await closeCode(maskedFrame(true, 8, Buffer.from([0x03, 0xe8, 0xff]))),
    await closeCode(maskedFrame(true, 1, Buffer.from([0xff, 0xfe]))),
    await closeCode(maskedFrame(true, 8, Buffer.concat([Buffer.from([0x0b, 0xb8]), Buffer.from('bye')]))),
  ];
  must(got.join() === '1002,1002,1002,1007,1007,1000', 'close codes ' + got.join());
});


row('BDBG-24', 'the session ends in the same write as a 20 MiB reply (one binary frame and the close): the TCP client starts reading 3 s later and gets every byte, then EOF', async () => {
  const b = await startBridge();
  const link = await upgradeOk(b.wsPort);
  must(/^HTTP\/1\.1 101/.test(link.status), 'link: ' + link.status);
  link.socket.on('error', () => {});
  const c = net.connect(b.tcpPort, '127.0.0.1');
  await new Promise((r) => c.on('connect', r));
  c.pause();   // reads nothing yet
  await sleep(300);
  const payload = crypto.randomBytes(20 * 1024 * 1024);
  link.socket.write(Buffer.concat([maskedFrame(true, 2, payload), maskedFrame(true, 1, Buffer.from('close'))]));
  await sleep(3000);   // longer than the 2 s that used to cut a queued tail
  const got = [];
  c.on('data', (d) => got.push(d));
  const eof = await new Promise((resolve) => { c.on('close', () => resolve(true)); c.on('error', () => {}); c.resume(); setTimeout(() => resolve(false), 30000); });
  const all = Buffer.concat(got);
  link.socket.destroy();
  must(all.length === payload.length && all.equals(payload), `${all.length} of ${payload.length} bytes, identical ${all.equals(payload)}`);
  must(eof, 'no EOF after the bytes');
});

row('BDBG-25', 'backpressure towards the page: a page link that stops reading makes the bridge stop reading a TCP client that writes 80 MiB (its writes back up); once the page reads, every byte arrives byte-identical', async () => {
  const b = await startBridge();
  const link = await upgradeOk(b.wsPort);
  must(/^HTTP\/1\.1 101/.test(link.status), 'link: ' + link.status);
  link.socket.removeAllListeners('data');
  link.socket.on('error', () => {});
  link.socket.pause();   // the page reads nothing
  const c = net.connect(b.tcpPort, '127.0.0.1');
  c.on('error', () => {});
  await new Promise((r) => c.on('connect', r));
  const MiB = 1 << 20, N = 80;
  const hash = crypto.createHash('sha1');
  for (let i = 0; i < N; i++) {
    const m = crypto.randomBytes(MiB);
    hash.update(m);
    c.write(m);
  }
  await sleep(1500);
  const queued = c.writableLength;
  const chunks = [];
  link.socket.on('data', (d) => chunks.push(d));
  link.socket.resume();
  const got = crypto.createHash('sha1');
  let total = 0, done = 0;
  const t0 = Date.now();
  while (total < N * MiB && Date.now() - t0 < 90000) {
    await sleep(100);
    const buf = Buffer.concat(chunks);
    chunks.length = 0;
    // keep an incomplete tail for the next round
    let used = 0;
    for (let off = 0; off + 2 <= buf.length;) {
      let len = buf[off + 1] & 0x7f, h = 2;
      if (len === 126) { if (off + 4 > buf.length) break; len = buf.readUInt16BE(off + 2); h = 4; }
      else if (len === 127) { if (off + 10 > buf.length) break; len = Number(buf.readBigUInt64BE(off + 2)); h = 10; }
      if (off + h + len > buf.length) break;
      if ((buf[off] & 0x0f) === 2) { const p = buf.subarray(off + h, off + h + len); got.update(p); total += p.length; }
      off += h + len;
      used = off;
    }
    if (used < buf.length) chunks.push(buf.subarray(used));
    done++;
  }
  c.destroy();
  link.socket.destroy();
  must(queued > 32 * MiB, `the client's write queue was ${queued} bytes after 1.5 s: the bridge read it though the page did not`);
  must(total === N * MiB && got.digest('hex') === hash.digest('hex'), `${total} of ${N * MiB} bytes, digest ${total === N * MiB ? 'differs' : 'n/a'}`);
});


// A raw page link on the bridge `b`: what the bridge sent it, and a poll.
async function rawPage(b) {
  const link = await upgradeOk(b.wsPort);
  must(/^HTTP\/1\.1 101/.test(link.status), 'link: ' + link.status);
  link.socket.on('error', () => {});
  const rx = [];
  link.socket.on('data', (d) => rx.push(d));
  const frames = (text) => {
    const want = Buffer.concat([Buffer.from([0x81, text.length]), Buffer.from(text)]);
    let n = 0;
    for (let at = Buffer.concat(rx).indexOf(want); at >= 0; at = Buffer.concat(rx).indexOf(want, at + 1)) n++;
    return n;
  };
  const until = async (fn, what, ms = 8000) => { const t0 = Date.now(); while (!fn()) { if (Date.now() - t0 > ms) throw new Error('timeout: ' + what); await sleep(10); } };
  const say = (opcode, text) => link.socket.write(maskedFrame(true, opcode, Buffer.from(text)));
  return { link, frames, until, say };
}
const tcp = (b) => new Promise((resolve) => {
  const c = net.connect(b.tcpPort, '127.0.0.1');
  c.got = []; c.isGone = false;
  c.on('data', (d) => c.got.push(d));
  c.on('close', () => { c.isGone = true; });
  c.on('error', () => {});
  c.on('connect', () => resolve(c));
});

row('BDBG-26', 'bridge: a client that dials again after the old one left waits for the page\'s `closed`: the old session\'s late bytes and late `close` neither reach the new client nor end it, and the new client is admitted promptly once `closed` comes', async () => {
  const b = await startBridge();
  const p = await rawPage(b);
  const a = await tcp(b);
  await p.until(() => p.frames('open') === 1, 'the page told `open`');
  a.destroy();   // the client leaves
  const c = await tcp(b);   // and a new one dials at once
  await p.until(() => p.frames('close') === 1, 'the page told `close`');
  // The page now sends the late messages of the old session, before it confirms: bytes, and its own close.
  p.link.socket.write(Buffer.concat([maskedFrame(true, 2, Buffer.from('STALE')), maskedFrame(true, 1, Buffer.from('close'))]));
  await sleep(400);
  const early = Buffer.concat(c.got).toString();
  const earlyClosed = c.isGone;
  const earlyOpens = p.frames('open');
  const t0 = Date.now();
  p.say(1, 'closed');
  await p.until(() => p.frames('open') === 2, 'the page told `open` for the new client');
  const admitted = Date.now() - t0;
  p.say(2, 'FRESH');
  await p.until(() => Buffer.concat(c.got).length >= 5, 'FRESH');
  const late = Buffer.concat(c.got).toString();
  p.link.socket.destroy(); c.destroy();
  must(early === '' && !earlyClosed && earlyOpens === 1, `before the confirmation: ${JSON.stringify(early)}, closed ${earlyClosed}, opens ${earlyOpens}`);
  must(admitted < 1500, `admitted ${admitted} ms after \`closed\` (the 3 s timeout is not the way in)`);
  must(late === 'FRESH' && !c.isGone, `after it: ${JSON.stringify(late)}, closed ${c.isGone}`);
});

row('BDBG-27', 'bridge: a page that never sends `closed` does not wedge a redial: it is admitted after about 3 s, whether it dialled after the old client left or 50 ms before', async () => {
  for (const order of ['after', 'before']) {
    const b = await startBridge();
    const p = await rawPage(b);
    const a = await tcp(b);
    await p.until(() => p.frames('open') === 1, 'open');
    let c, t0;
    if (order === 'after') {
      a.destroy();
      t0 = Date.now();
      await sleep(50);
      c = await tcp(b);
    } else {
      c = await tcp(b);   // dials while the old client is still connected
      await sleep(50);
      a.destroy();
      t0 = Date.now();
    }
    await p.until(() => p.frames('open') === 2, `the page told \`open\` (${order})`, 8000);
    const dt = Date.now() - t0;
    p.link.socket.destroy(); c.destroy();
    must(dt > 2500 && dt < 5500 && !c.isGone, `${order}: admitted after ${dt} ms, closed ${c.isGone}`);
  }
});

row('BDBG-28', 'bridge: a client that dialled 50 ms BEFORE the old one\'s FIN keeps waiting for a `closed` that comes 500 ms late: admitted on it, not refused at 300 ms', async () => {
  const b = await startBridge();
  const p = await rawPage(b);
  const a = await tcp(b);
  await p.until(() => p.frames('open') === 1, 'open');
  const c = await tcp(b);   // dials first
  await sleep(50);
  a.destroy();              // then the old client's FIN
  await p.until(() => p.frames('close') === 1, 'the page told `close`');
  await sleep(500);         // the page is slow to confirm: longer than the 300 ms of a client that waits for a client
  const opensBefore = p.frames('open');
  const closedBefore = c.isGone;
  const t0 = Date.now();
  p.say(1, 'closed');
  await p.until(() => p.frames('open') === 2, 'the page told `open`');
  const admitted = Date.now() - t0;
  p.link.socket.destroy(); c.destroy();
  must(opensBefore === 1 && !closedBefore, `before \`closed\`: opens ${opensBefore}, closed ${closedBefore}`);
  must(admitted < 1500 && !c.isGone, `admitted ${admitted} ms after \`closed\`, closed ${c.isGone}`);
});

row('BDBG-29', 'the channel path: the old session\'s last reply reaches the host after the client left (the page\'s messages take 150 ms): the host releases the relay on the page\'s confirmation, and the client that dialled at once gets only its own answer', async () => {
  const page = await started({ protocol: 'dzrp' });
  const c = await channelRelay(page, 'dzrp', 150);
  const A = net.connect(c.port, '127.0.0.1');
  const rxA = [];
  A.on('data', (d) => rxA.push(...d));
  A.on('error', () => {});
  await new Promise((r) => A.on('connect', r));
  A.write(Buffer.from(INIT));
  const t0 = Date.now();
  while (rxA.length < 17 && Date.now() - t0 < 15000) await sleep(20);
  must(isInitReply(rxA), 'the first client got no INIT answer');
  A.write(Buffer.from([2, 0, 0, 0, 9, 15, 0x68, 0x69]));   // CMD_LOOPBACK "hi", seq 9
  await sleep(80);    // the machine answers on a tick; the answer is on its way to the host, 150 ms
  A.destroy();        // the client leaves while that answer is still in flight
  const B = net.connect(c.port, '127.0.0.1');
  const rxB = [];
  B.on('data', (d) => rxB.push(...d));
  B.on('error', () => {});
  await new Promise((r) => B.on('connect', r));
  B.write(Buffer.from(INIT));
  const t1 = Date.now();
  while (rxB.length < 17 && Date.now() - t1 < 15000) await sleep(20);
  await sleep(500);   // anything late would be here by now
  B.destroy();
  must(isInitReply(rxB) && rxB.length === 4 + (rxB[0] | (rxB[1] << 8)), `the redialled client got ${rxB.length} bytes: ${rxB.join(',')}`);
});


row('BDBG-30', 'the real player answers the bridge\'s `close` with `closed`: a client that dials again at once gets its CMD_INIT answered less than 1.5 s after the first one left (the 3 s fallback is not the way in)', async () => {
  const b = await startBridge();
  const page = await started({ protocol: 'dzrp', transport: 'ws', url: b.url });
  await linked(b);
  const A = net.connect(b.tcpPort, '127.0.0.1');
  const rxA = [];
  A.on('data', (d) => rxA.push(...d));
  A.on('error', () => {});
  await new Promise((r) => A.on('connect', r));
  A.write(Buffer.from(INIT));
  const t0 = Date.now();
  while (rxA.length < 17 && Date.now() - t0 < 15000) await sleep(20);
  must(isInitReply(rxA), 'the first client got no INIT answer');
  A.destroy();
  const left = Date.now();
  const B = net.connect(b.tcpPort, '127.0.0.1');
  const rxB = [];
  B.on('data', (d) => rxB.push(...d));
  B.on('error', () => {});
  await new Promise((r) => B.on('connect', r));
  B.write(Buffer.from(INIT));
  while (rxB.length < 17 && Date.now() - left < 8000) await sleep(10);
  const dt = Date.now() - left;
  B.destroy();
  must(isInitReply(rxB), `the redialled client got ${rxB.length} bytes`);
  must(dt < 1500, `the redialled client was answered ${dt} ms after the first one left`);
});

// ---------------------------------------------------------------------------------------------------
let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 400000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`);
  } catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 700)}]`); }
  for (const f of cleanup.splice(0)) { try { await f(); } catch (e) { /* already gone */ } }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
