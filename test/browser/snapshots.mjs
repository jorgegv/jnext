// Browser rows BSN-01..11: snapshots in a page (.sna, .z80, .szx, .jns loaded through the
// JavaScript API; player.saveSnapshot()), in headless Chromium (make wasm-browser-test, spec
// snapshots.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Page: test/browser/page/api.html through real servers, as api.mjs. Every fixture is made at run
// time by browser-smoke.sh in its scratch directory (from OUR programs, on clones of the minimal
// card, deleted with it); nothing is committed.
// Oracles: the native SDL jnext loading the SAME file (oracle/sna.png, z80.png, szx.png, jns.png,
// bluejns.png, 50 frames after the restore) and the native 48K boot screen (boot.png). s48.z80 is
// the .sna converted by sna2z80.py, written from the .z80 format spec. The card is copyrighted:
// it lives in the scratch directory and in the page's storage only.
import { Harness, waitFor, sleep } from './harness.mjs';
import { execFileSync } from 'child_process';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256;
const NATIVE = path.join(ROOT, 'build/sdl-release/jnext');

const h = new Harness({});
const B = http.createServer((req, res) => {
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), nex: path.join(ROOT, 'test/00regression/nex'), scratch: SCRATCH };
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream', 'Access-Control-Allow-Origin': '*' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`;
const BURL = `http://127.0.0.1:${B.address().port}`;
const IMAGE = { url: BURL + '/card.img', sha256: SHA };

await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

// ---- helpers -------------------------------------------------------------------------------
async function newPage() {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx;
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  return page;
}
async function open(page, players) {
  page.msgs.length = 0;
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(IMAGE), players: JSON.stringify(players) });
  await page.goto(AURL + '/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length);
}
const rec = (page, i = 0) => page.evaluate((i) => {
  const r = window.apiTest.players[i];
  return { progress: r.progress, ready: r.ready, errors: r.errors, mountError: r.mountError, argv: r.player ? r.player.argv : null,
           state: r.player ? r.player.state : null };
}, i);
const click = (page, i = 0) => page.click(`#p${i} .jnext-overlay`, { timeout: 600000, noWaitAfter: true });
const waitReady = (page, i, n, ms = 300000) =>
  waitFor(page, ([i, n]) => window.apiTest.players[i].ready >= n || window.apiTest.players[i].errors.length > 0, [i, n], ms);
const frame = (page, i = 0) => page.evaluate((i) => window['player' + i]._module._jnext_web_frame(), i);
const waitFrame = (page, i, n, ms = 120000) =>
  waitFor(page, ([i, n]) => window['player' + i]._module._jnext_web_frame() >= n, [i, n], ms);
async function decode(buf) {
  const r = await util.evaluate(async (b64) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const bmp = await createImageBitmap(new Blob([bytes], { type: 'image/png' }), { colorSpaceConversion: 'none' });
    const c = new OffscreenCanvas(bmp.width, bmp.height);
    const g = c.getContext('2d'); g.drawImage(bmp, 0, 0);
    const d = g.getImageData(0, 0, bmp.width, bmp.height).data;
    let s = ''; for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
    return { w: bmp.width, h: bmp.height, b64: btoa(s) };
  }, buf.toString('base64'));
  return { w: r.w, h: r.h, d: Buffer.from(r.b64, 'base64') };
}
const oracle = (name) => decode(fs.readFileSync(path.join(ORACLE, name + '.png')));
const px = (im) => { const o = ((im.h >> 1) * im.w + (im.w >> 1)) * 4; return [im.d[o], im.d[o + 1], im.d[o + 2]]; };
const centre = async (page, i = 0) => px(await decode(await page.locator(`#p${i} canvas`).screenshot()));
const near = (a, b, tol = 8) => a.every((v, k) => Math.abs(v - b[k]) <= tol);
async function grabPng(page, i, p) {
  const b64 = await page.evaluate(([i, p]) => {
    const d = window['player' + i]._module.FS.readFile(p); let s = '';
    for (let k = 0; k < d.length; k += 8192) s += String.fromCharCode.apply(null, d.subarray(k, k + 8192));
    return btoa(s);
  }, [i, p]);
  return decode(Buffer.from(b64, 'base64'));
}
function diff(a, b) {
  if (a.w !== b.w || a.h !== b.h) return `size ${a.w}x${a.h} vs ${b.w}x${b.h}`;
  let n = 0;
  for (let i = 0; i < a.d.length; i += 4) if (a.d[i] !== b.d[i] || a.d[i + 1] !== b.d[i + 1] || a.d[i + 2] !== b.d[i + 2]) n++;
  return n;
}
// The machine the LAST "Initializing emulator" log line names.
const lastMachine = (page) => {
  const l = page.msgs.filter((m) => m.includes('Initializing emulator: machine_type='));
  const m = l.length ? /machine_type=\d+\[([^\]]+)\]/.exec(l[l.length - 1]) : null;
  return m ? m[1] : null;
};
const initLines = (page) => page.msgs.filter((m) => m.includes('Initializing emulator: machine_type=')).length;
const SHOT = ['--delayed-screenshot', '/tmp/s.png', '--delayed-screenshot-frames', '50'];
const bytesOf = (page, url) => page.evaluate(async (u) => new Uint8Array(await (await fetch(u)).arrayBuffer()), url);
// A snapshot of the page as a Buffer (a .jns is a ZIP).
const saved = async (page, i = 0) => Buffer.from(await page.evaluate((i) => window['player' + i].saveSnapshot().then((b) => Array.from(b)), i));
// The compression method of every member, from the ZIP's central directory.
function methods(buf) {
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  must(e >= 0, 'no ZIP end record');
  const n = buf.readUInt16LE(e + 10);
  let o = buf.readUInt32LE(e + 16);
  const out = [];
  for (let k = 0; k < n; k++) {
    must(buf.readUInt32LE(o) === 0x02014b50, 'bad central directory entry');
    const nl = buf.readUInt16LE(o + 28), el = buf.readUInt16LE(o + 30), cl = buf.readUInt16LE(o + 32);
    out.push({ method: buf.readUInt16LE(o + 10), name: buf.toString('latin1', o + 46, o + 46 + nl) });
    o += 46 + nl + el + cl;
  }
  return out;
}
// The scratch files saveSnapshot() leaves in the page's /tmp (there must be none).
const leftover = (page) => page.evaluate(() => { const m = window.player0._module; return m && m.FS && m.FS.analyzePath('/tmp').exists ? m.FS.readdir('/tmp').filter((n) => /^jnext-page-.*\.jns$/.test(n)) : []; });
// Mount one snapshot player (no `machine`: the file names it), start it, and compare its screenshot to the oracle.
async function snapshotRow(file, name, ownMachine) {
  const page = await newPage();
  await open(page, [{ muted: true, file, args: SHOT }]);
  const r0 = await rec(page);
  must(!r0.mountError, 'mount: ' + r0.mountError);
  await click(page);
  await waitReady(page, 0, 1);
  const r = await rec(page);
  must(r.errors.length === 0 && r.ready === 1, `ready ${r.ready} errors ${JSON.stringify(r.errors)}`);
  const i = r.argv.indexOf('--load');
  must(i >= 0 && r.argv[i + 1] === '/media/0-' + path.basename(file), 'argv ' + JSON.stringify(r.argv));
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 60000);
  const d = diff(await grabPng(page, 0, '/tmp/s.png'), await oracle(name));
  must(d === 0, `screenshot vs ${name}.png: ${d} px differ`);
  must(lastMachine(page) === ownMachine, `last init line names ${lastMachine(page)}`);
  await page.ctx.close();
  return d;
}

// ---- rows ------------------------------------------------------------------------------------
row('BSN-01', 'a .sna mounted with no machine option: ready resolves, argv has --load /media/0-s48.sna, the screenshot equals the native run (0 px), the 48K was initialised', async () => {
  await snapshotRow('/scratch/s48.sna', 'sna', '48K');
});

row('BSN-02', 'a .z80 (converted from the .sna by a foreign writer): the screenshot equals the native .z80 run (0 px), which equals the native .sna run, and loading it leaves the same machine as the .sna', async () => {
  await snapshotRow('/scratch/s48.z80', 'z80', '48K');
  const d = diff(await oracle('z80'), await oracle('sna'));
  must(d === 0, `native z80.png vs sna.png: ${d} px differ`);
  const a = fs.readFileSync(path.join(SCRATCH, 'rtsna.sna')), b = fs.readFileSync(path.join(SCRATCH, 'rtz80.sna'));
  must(a.length === b.length && a.equals(b), 'the native .z80 load and the native .sna load do not leave the same machine');
});

row('BSN-03', 'a 128K .szx whose bank-7 shadow screen shows: the screenshot equals the native run (0 px), the 128K was initialised', async () => {
  await snapshotRow('/scratch/s128.szx', 'szx', '128K');
});

row('BSN-04', 'a .jns in a fresh context: equals the native run (0 px); no warm-start or restore progress, no warm start in the log, nothing stored under /jnext/warm-start', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: 'next', file: '/scratch/tilemap.jns', args: ['--rtc', '2026-07-10T08:55:00', ...SHOT] }]);
  await click(page);
  await waitReady(page, 0, 1);
  const r = await rec(page);
  must(r.errors.length === 0 && r.ready === 1, `ready ${r.ready} errors ${JSON.stringify(r.errors)}`);
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 60000);
  const d = diff(await grabPng(page, 0, '/tmp/s.png'), await oracle('jns'));
  must(d === 0, `screenshot vs jns.png: ${d} px differ`);
  const phases = r.progress.map((p) => p[0]);
  must(!phases.some((p) => p === 'warm-start' || p === 'restore'), 'progress phases ' + JSON.stringify(phases));
  must(!page.msgs.some((m) => /warm start:|recorded a NextZXOS-resident machine/.test(m)), 'a warm-start log line');
  const files = await page.evaluate(() => new Promise((res, rej) => {
    const rq = indexedDB.open('/jnext/warm-start');
    rq.onerror = () => rej(rq.error);
    rq.onsuccess = () => {
      const db = rq.result;
      if (!db.objectStoreNames.contains('FILE_DATA')) { res([]); return; }
      const tx = db.transaction('FILE_DATA', 'readonly'), st = tx.objectStore('FILE_DATA');
      const keys = st.getAllKeys(), vals = st.getAll();
      tx.oncomplete = () => res(keys.result.map((k, i) => ({ key: k, bytes: vals.result[i].contents ? vals.result[i].contents.length : -1 })));
    };
  }));
  must(files.filter((f) => f.bytes >= 0).length === 0, 'stored warm-start files ' + JSON.stringify(files));
  await page.ctx.close();
});

row('BSN-05', 'one 48K player loads .sna, .Z80 (data), .szx, .JnS (data) in turn: each resolves, onReady +1, names its machine, and its canvas centre equals the native picture', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: '48k' }]);
  await click(page); await waitReady(page, 0, 1);
  const steps = [
    ['{url}', () => page.evaluate(() => window.player0.load({ url: '/scratch/s48.sna' })), '48K', 'sna'],
    ['data .Z80', async () => { const b = await bytesOf(page, '/scratch/s48.z80'); return page.evaluate((a) => window.player0.load({ data: Uint8Array.from(a), name: 'S48.Z80' }), Array.from(b)); }, '48K', 'z80'],
    ['url .szx', () => page.evaluate(() => window.player0.load('/scratch/s128.szx')), '128K', 'szx'],
    ['data .JnS', async () => { const b = await bytesOf(page, '/scratch/blue.jns'); return page.evaluate((a) => window.player0.load({ data: Uint8Array.from(a), name: 'Blue.JnS' }), Array.from(b)); }, 'ZX Next', 'bluejns'],
  ];
  for (const [what, go, machine, name] of steps) {
    const before = (await rec(page)).ready;
    await go();
    const r = await rec(page);
    must(r.ready === before + 1, `${what}: ready ${before} -> ${r.ready}, errors ${JSON.stringify(r.errors)}`);
    must(lastMachine(page) === machine, `${what}: last init line names ${lastMachine(page)}`);
    const f0 = await frame(page);
    await waitFrame(page, 0, f0 + 100);
    const c = await centre(page), want = px(await oracle(name));
    must(near(c, want), `${what}: canvas centre ${JSON.stringify(c)} vs ${name}.png ${JSON.stringify(want)}`);
  }
  await page.ctx.close();
});

row('BSN-06', 'after a .szx: reset(hard) re-initialises the 128K, not the Next; reset(program) comes back to the snapshot (centre green); a later .tzx boots the kept 128K', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: 'next' }]);
  await click(page); await waitReady(page, 0, 1);
  await page.evaluate(() => window.player0.load('/scratch/s128.szx'));
  must(lastMachine(page) === '128K', 'after load: ' + lastMachine(page));
  const n = initLines(page);
  await page.evaluate(() => window.player0.reset('hard'));
  must(initLines(page) > n && lastMachine(page) === '128K', `after hard reset: ${lastMachine(page)} (${n} -> ${initLines(page)} init lines)`);
  const ready = (await rec(page)).ready;
  await page.evaluate(() => window.player0.reset('program'));
  must((await rec(page)).ready === ready + 1, 'reset(program) did not reach a ready');
  const f0 = await frame(page);
  await waitFrame(page, 0, f0 + 100);
  const c = await centre(page), want = px(await oracle('szx'));
  must(near(c, want), `centre ${JSON.stringify(c)} vs szx.png ${JSON.stringify(want)}`);
  // The snapshot's machine is kept for a later load of a tape too: the page's own 'next' is not booted again.
  await page.evaluate(() => window.player0.load('/scratch/beeper_demo.tzx'));
  must(lastMachine(page) === '128K', 'after load(.tzx): ' + lastMachine(page));
  await page.ctx.close();
});

row('BSN-07', 'a .jns taken on another card: ready rejects, onError once, the log names the refusal; with --snapshot-mode force it loads and the log says so', async () => {
  const a = await newPage();
  await open(a, [{ muted: true, machine: '48k', file: '/scratch/other48.jns' }]);
  await click(a);
  const out = await a.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  const ra = await rec(a);
  must(/failed to load/.test(out), 'ready: ' + out);
  must(ra.errors.length === 1, 'errors ' + JSON.stringify(ra.errors));
  must(a.msgs.some((m) => /the mounted SD card is not the one the snapshot was taken on/.test(m)), 'no refusal in the log');
  await a.ctx.close();
  const b = await newPage();
  await open(b, [{ muted: true, machine: '48k', file: '/scratch/other48.jns', args: ['--snapshot-mode', 'force'] }]);
  await click(b);
  const out2 = await b.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  const rb = await rec(b);
  must(out2 === 'ok' && rb.errors.length === 0, `ready: ${out2} errors ${JSON.stringify(rb.errors)}`);
  must(b.msgs.some((m) => /but --snapshot-mode force was given/.test(m)), 'no force warning in the log');
  await b.ctx.close();
});

let defaultBytes = null;   // BSN-08's snapshot, for BSN-10's compression check
row('BSN-08', 'saveSnapshot() on a running Next: a ZIP with manifest.json; load(red.nex) turns the canvas red, load({data, name: b.jns}) restores blue', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: 'next', file: '/nex/blue.nex' }]);
  await click(page); await waitReady(page, 0, 1);
  const f0 = await frame(page);
  await waitFrame(page, 0, f0 + 100);
  const bytes = await saved(page);
  defaultBytes = bytes;
  must((await leftover(page)).length === 0, 'scratch files left after saveSnapshot: ' + JSON.stringify(await leftover(page)));
  must(bytes.slice(0, 4).equals(Buffer.from([0x50, 0x4b, 0x03, 0x04])), 'not a ZIP: ' + bytes.slice(0, 4).toString('hex'));
  must(methods(bytes).some((m) => m.name === 'manifest.json'), 'no manifest.json: ' + JSON.stringify(methods(bytes).map((m) => m.name)));
  await page.evaluate(() => window.player0.load('/nex/red.nex'));
  let f = await frame(page);
  await waitFrame(page, 0, f + 100);
  const red = await centre(page);
  must(red[0] > 150 && red[1] < 80 && red[2] < 80, 'canvas not red: ' + JSON.stringify(red));
  const before = (await rec(page)).ready;
  await page.evaluate((a) => window.player0.load({ data: Uint8Array.from(a), name: 'b.jns' }), Array.from(bytes));
  must((await rec(page)).ready === before + 1, 'onReady did not fire for the restore');
  f = await frame(page);
  await waitFrame(page, 0, f + 100);
  const blue = await centre(page);
  must(blue[2] > 150 && blue[0] < 80 && blue[1] < 80, 'canvas not blue: ' + JSON.stringify(blue));
  await page.ctx.close();
});

row('BSN-09', 'a .jns saved in the page restores in the native jnext: the screenshot equals the native 48K boot screen (0 px)', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: '48k' }]);
  await click(page); await waitReady(page, 0, 1);
  await waitFrame(page, 0, 200);
  const bytes = await saved(page);
  await page.ctx.close();
  const dir = fs.mkdtempSync(path.join(SCRATCH, 'bsn09-'));
  fs.writeFileSync(path.join(dir, 'page.jns'), bytes);
  fs.copyFileSync(path.join(SCRATCH, 'card.img'), path.join(dir, 'card.img'));
  try {
    execFileSync(NATIVE, ['--sdcard', path.join(dir, 'card.img'), '--silent', '--load', path.join(dir, 'page.jns'),
                          '--delayed-screenshot', path.join(dir, 'x.png'), '--delayed-screenshot-frames', '10',
                          '--delayed-automatic-exit-frames', '60'],
                 { env: { PATH: E.PATH, LANG: 'C', LC_ALL: 'C', SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy', JNEXT_CONFIG_DIR: path.join(dir, 'cfg') },
                   timeout: 300000, killSignal: 'SIGKILL', stdio: 'pipe' });
    const d = diff(await decode(fs.readFileSync(path.join(dir, 'x.png'))), await oracle('boot'));
    must(d === 0, `native restore of the page's snapshot vs boot.png: ${d} px differ`);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

row('BSN-10', '--snapshot-compression off: every member is stored (method 0); the default snapshot has a deflated member; two saves 50+ frames apart differ', async () => {
  must(defaultBytes !== null, 'BSN-08 produced no snapshot');
  must(methods(defaultBytes).some((m) => m.method === 8), 'default snapshot methods ' + JSON.stringify(methods(defaultBytes).map((m) => m.method)));
  const page = await newPage();
  await open(page, [{ muted: true, machine: '48k', args: ['--snapshot-compression', 'off'] }]);
  await click(page); await waitReady(page, 0, 1);
  await waitFrame(page, 0, 100);
  const a = await saved(page);
  const f = await frame(page);
  await waitFrame(page, 0, f + 60);
  const b = await saved(page);
  must((await leftover(page)).length === 0, 'scratch files left after two saves: ' + JSON.stringify(await leftover(page)));
  const ma = methods(a), mb = methods(b);
  must(ma.length > 0 && ma.every((m) => m.method === 0) && mb.every((m) => m.method === 0), 'methods ' + JSON.stringify(ma.map((m) => m.method)));
  must(!a.equals(b), 'two saves 60 frames apart are identical');
  await page.ctx.close();
});

row('BSN-11', 'saveSnapshot() before the start click rejects (machine not running, no onError) and leaves no scratch file; after destroy() it throws', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: '48k' }]);
  const out = await page.evaluate(() => window.player0.saveSnapshot().then(() => 'resolved', (e) => e.message));
  must(/the machine is not running/.test(out), 'rejection: ' + out);
  must((await rec(page)).errors.length === 0, 'onError fired for a call made before the start: ' + JSON.stringify((await rec(page)).errors));
  const left = await leftover(page);
  must(left.length === 0, 'scratch files ' + JSON.stringify(left));
  await click(page); await waitReady(page, 0, 1);
  await page.evaluate(() => window.player0.destroy());
  const thrown = await page.evaluate(() => { try { window.player0.saveSnapshot(); return 'no throw'; } catch (e) { return e.message; } });
  must(/destroyed/.test(thrown), 'after destroy: ' + thrown);
  await page.ctx.close();
});

// ---- run -------------------------------------------------------------------------------------
let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), Number(E.JNEXT_ROW_TIMEOUT_MS) || 400000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
