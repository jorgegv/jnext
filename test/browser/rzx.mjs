// Browser rows BRZ-01..12: RZX recordings in a page (played at start-up, by load() and by reset();
// recorded with player.startRzxRecording() / stopRzxRecording()), in headless Chromium
// (make wasm-browser-test, spec rzx.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Page: test/browser/page/api.html through real servers, as api.mjs. Every fixture is made at run
// time by browser-smoke.sh in its scratch directory: keys48.rzx and s128.rzx are RECORDED by the
// native jnext from our own inputs (the `keys` typing from power-on on a 48K; #471's bank-7 shadow
// screen snapshot on a 128K); nothing is committed. Oracles: the native SDL jnext replaying the SAME
// file (oracle/rzxplay48.png, rzxend48.png, rzxplay128.png) and its own recording run
// (rzxrec48.png, rzxrec128.png), and the native replay's frames 200 and 201 (rzxplay48f200.png, rzxplay48f201.png: the first key's echo, so a one-frame offset shows); keys.png / keys352.png are the native typist; idle.png the native
// untyped 48K. The card is copyrighted: it lives in the scratch directory and in the page's storage only.
import { Harness, waitFor, sleep } from './harness.mjs';
import { spawnSync } from 'child_process';
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
  return { progress: r.progress, ready: r.ready, applied: r.applied, errors: r.errors, mountError: r.mountError, argv: r.player ? r.player.argv : null,
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
const shot = (n) => ['--delayed-screenshot', '/tmp/s.png', '--delayed-screenshot-frames', String(n)];
const bytesOf = (page, url) => page.evaluate(async (u) => new Uint8Array(await (await fetch(u)).arrayBuffer()), url);
const logHas = (page, re) => page.msgs.some((m) => re.test(m));
const logAt = (page, re) => page.msgs.findIndex((m) => re.test(m));
const MAGIC = Buffer.from([0x52, 0x5a, 0x58, 0x21]);   // "RZX!"
// The recording scratch files the player methods leave in the page's /tmp (there must be none).
const leftover = (page) => page.evaluate(() => { const m = window.player0._module; return m && m.FS && m.FS.analyzePath('/tmp').exists ? m.FS.readdir('/tmp').filter((n) => /^jnext-page-.*\.rzx$/.test(n)) : []; });
const rejection = (page, call) => page.evaluate((c) => window.player0[c]().then(() => 'resolved', (e) => e.message), call);
// The paper region of the 640x512 PNG (x 64..575, y 64..447).
function paperDiff(a, b) {
  if (a.w !== b.w || a.h !== b.h) return `size ${a.w}x${a.h} vs ${b.w}x${b.h}`;
  let n = 0;
  for (let y = 64; y < 448; y++) for (let x = 64; x < 576; x++) {
    const o = (y * a.w + x) * 4;
    if (a.d[o] !== b.d[o] || a.d[o + 1] !== b.d[o + 1] || a.d[o + 2] !== b.d[o + 2]) n++;
  }
  return n;
}
// Replay `bytes` natively (SDL dummy drivers, a card clone); returns the log and, when a screenshot
// frame is given, the picture. The card is deleted with the directory.
function nativeReplay(bytes, shotFrame) {
  const dir = fs.mkdtempSync(path.join(SCRATCH, 'brz-'));
  try {
    fs.writeFileSync(path.join(dir, 'page.rzx'), bytes);
    fs.copyFileSync(path.join(SCRATCH, 'card.img'), path.join(dir, 'card.img'));
    const args = ['--sdcard', path.join(dir, 'card.img'), '--silent', '--load', path.join(dir, 'page.rzx')];
    if (shotFrame) args.push('--delayed-screenshot', path.join(dir, 'x.png'), '--delayed-screenshot-frames', String(shotFrame));
    args.push('--delayed-automatic-exit-frames', String((shotFrame || 100) + 50));
    const r = spawnSync(NATIVE, args, { encoding: 'utf8', timeout: 300000, killSignal: 'SIGKILL',
      env: { PATH: E.PATH, LANG: 'C', LC_ALL: 'C', SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy', JNEXT_CONFIG_DIR: path.join(dir, 'cfg') } });
    must(r.status === 0, `native replay exit ${r.status}: ${(r.stderr || '').slice(0, 300)}`);
    return { log: (r.stdout || '') + (r.stderr || ''), png: shotFrame ? fs.readFileSync(path.join(dir, 'x.png')) : null };
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}
async function typeKeys(page) {
  await page.focus('#p0 canvas');
  for (const k of ['p', '1', '2', 'Enter']) { await page.keyboard.press(k, { delay: 60 }); await sleep(120); }
}
// Mount one player on the card, start it, wait for the first ready.
async function boot(opts) {
  const page = await newPage();
  await open(page, [{ muted: true, ...opts }]);
  must(!(await rec(page)).mountError, 'mount: ' + (await rec(page)).mountError);
  await click(page);
  return page;
}
// ---- rows ------------------------------------------------------------------------------------
row('BRZ-01', 'file keys48.rzx, no machine: --load /media/0-keys48.rzx, ready with the load applied (1), the log says loaded + playback started, the 48K is initialised, the frame-320 screenshot equals the native replay and the native recording run (0 px) and differs from the untyped idle screen', async () => {
  const page = await boot({ file: '/scratch/keys48.rzx', args: shot(320) });
  await waitReady(page, 0, 1);
  const r = await rec(page);
  must(r.errors.length === 0 && r.ready === 1, `ready ${r.ready} errors ${JSON.stringify(r.errors)}`);
  must(r.applied === 1, 'load_applied at ready ' + r.applied);
  const i = r.argv.indexOf('--load');
  must(i >= 0 && r.argv[i + 1] === '/media/0-keys48.rzx', 'argv ' + JSON.stringify(r.argv));
  must(logHas(page, /RZX: loaded '\/media\/0-keys48\.rzx'/) && logHas(page, /RZX: playback started/), 'no loaded/started line');
  must(lastMachine(page) === '48K', 'last init line names ' + lastMachine(page));
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 120000);
  const got = await grabPng(page, 0, '/tmp/s.png');
  const dp = diff(got, await oracle('rzxplay48')), dr = diff(got, await oracle('rzxrec48')), di = diff(got, await oracle('idle'));
  must(dp === 0 && dr === 0, `vs rzxplay48.png ${dp} px, vs rzxrec48.png ${dr} px`);
  must(typeof di === 'number' && di > 0, 'the replay screen equals the untyped idle screen: input was not replayed');
  await page.ctx.close();
});

row('BRZ-02', 'the same file run past its 400 frames: "playback complete" is logged before the frame-500 screenshot, which equals the native end screen (0 px)', async () => {
  const page = await boot({ file: '/scratch/keys48.rzx', args: shot(500) });
  await waitReady(page, 0, 1);
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 200000);
  const c = logAt(page, /RZX: playback complete/), s = logAt(page, /screenshot saved/);
  must(c >= 0 && s >= 0 && c < s, `log order: complete at ${c}, screenshot at ${s}`);
  const d = diff(await grabPng(page, 0, '/tmp/s.png'), await oracle('rzxend48'));
  must(d === 0, `screenshot vs rzxend48.png: ${d} px differ`);
  await page.ctx.close();
});

row('BRZ-03', 'a 48K player load()s s128.rzx (url): onReady +1, applied +1, the 128K is initialised, the centre turns green; reset(hard) keeps the 128K; load({data, name: KEYS48.RZX}) boots the 48K', async () => {
  const page = await newPage();
  await open(page, [{ muted: true, machine: '48k' }]);
  await click(page); await waitReady(page, 0, 1);
  const r0 = await rec(page);
  await page.evaluate(() => window.player0.load({ url: '/scratch/s128.rzx' }));
  const r1 = await rec(page);
  must(r1.ready === r0.ready + 1 && r1.errors.length === 0, `ready ${r0.ready} -> ${r1.ready}, errors ${JSON.stringify(r1.errors)}`);
  must(r1.applied === r0.applied + 1, `applied ${r0.applied} -> ${r1.applied}`);
  must(lastMachine(page) === '128K', 'after load: ' + lastMachine(page));
  const f0 = await frame(page);
  await waitFrame(page, 0, f0 + 60);
  const c = await centre(page), want = px(await oracle('rzxplay128'));
  must(near(c, want), `centre ${JSON.stringify(c)} vs rzxplay128.png ${JSON.stringify(want)}`);
  const n = initLines(page);
  await page.evaluate(() => window.player0.reset('hard'));
  must(initLines(page) > n && lastMachine(page) === '128K', `after hard reset: ${lastMachine(page)} (${n} -> ${initLines(page)} init lines)`);
  const before = (await rec(page)).ready;
  const b = await bytesOf(page, '/scratch/keys48.rzx');
  await page.evaluate((a) => window.player0.load({ data: Uint8Array.from(a), name: 'KEYS48.RZX' }), Array.from(b));
  must((await rec(page)).ready === before + 1, 'onReady did not fire for the data load');
  must(lastMachine(page) === '48K', 'after data load: ' + lastMachine(page));
  must(logHas(page, /RZX: loaded '\/media\/\d+-KEYS48\.RZX'/), 'no loaded line for KEYS48.RZX');
  await page.ctx.close();
});

row('BRZ-04', 'reset(program) after "playback complete" plays the file again (second "playback started", applied +1); reset(hard) during that replay logs "ends the playback" and leaves the 48K', async () => {
  const page = await boot({ file: '/scratch/keys48.rzx' });
  await waitReady(page, 0, 1);
  await waitFor(page, () => window.player0._module._jnext_web_frame() > 0, null, 60000);
  for (let t = 0; t < 600 && !logHas(page, /RZX: playback complete/); t++) await sleep(250);
  must(logHas(page, /RZX: playback complete/), 'the first playback never completed');
  const r0 = await rec(page);
  await page.evaluate(() => window.player0.reset('program'));
  const r1 = await rec(page);
  must(r1.ready === r0.ready + 1 && r1.applied === r0.applied + 1, `ready ${r0.ready} -> ${r1.ready}, applied ${r0.applied} -> ${r1.applied}`);
  must(page.msgs.filter((m) => /RZX: playback started/.test(m)).length === 2, 'no second "playback started"');
  await page.evaluate(() => window.player0.reset('hard'));
  must(logHas(page, /RZX: .*ends the playback/), 'no "ends the playback" line');
  must(lastMachine(page) === '48K', 'after hard reset: ' + lastMachine(page));
  await page.ctx.close();
});

row('BRZ-05', 'failures and refusals reach the page: a bad .rzx rejects load() and fires onError once; --rzx-play with a file rejects ready (one RZX can play); --rzx-record rejects ready naming startRzxRecording', async () => {
  const a = await newPage();
  await open(a, [{ muted: true, machine: '48k' }]);
  await click(a); await waitReady(a, 0, 1);
  const out = await a.evaluate(() => window.player0.load({ data: Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]), name: 'bad.rzx' }).then(() => 'resolved', (e) => e.message));
  must(/failed to load '\/media\/\d+-bad\.rzx'/.test(out), 'rejection: ' + out);
  const ra = await rec(a);
  must(ra.errors.length === 1, 'errors ' + JSON.stringify(ra.errors));
  must(logHas(a, /RZX: failed to parse/), 'no parse failure in the log');
  const f = await frame(a);
  await waitFrame(a, 0, f + 20);
  await a.ctx.close();
  const b = await newPage();
  await open(b, [{ muted: true, file: '/scratch/keys48.rzx', args: ['--rzx-play', '/media/0-keys48.rzx'] }]);
  await click(b);
  const ob = await b.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  must(ob !== 'ok', 'ready resolved');
  must(logHas(b, /only one RZX recording can play/), 'no "only one RZX recording can play" in the log: ' + JSON.stringify(b.msgs.slice(-6)));
  await b.ctx.close();
  const c = await newPage();
  await open(c, [{ muted: true, machine: '48k', args: ['--rzx-record', '/tmp/x.rzx'] }]);
  await click(c);
  const oc = await c.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  must(/--rzx-record: not available in a browser page/.test(oc) && /startRzxRecording/.test(oc), 'ready: ' + oc);
  await c.ctx.close();
});

row('BRZ-06', 'recording round trip: start, type p 1 2 Enter, stop returns an RZX! file and leaves no scratch file; the native replay shows the typed screen (paper region equals keys.png, differs from idle.png); load({data}) plays it in the page', async () => {
  const page = await boot({ machine: '48k' });
  await waitReady(page, 0, 1);
  await waitFrame(page, 0, 150);
  await page.evaluate(() => window.player0.startRzxRecording());
  must(logHas(page, /RZX: recording started - output: \/tmp\/jnext-page-/), 'no "recording started" line');
  const f0 = await frame(page);
  await typeKeys(page);
  await waitFrame(page, 0, f0 + 150);
  const bytes = Buffer.from(await page.evaluate(() => window.player0.stopRzxRecording().then((b) => Array.from(b))));
  must(bytes.length > 4 && bytes.slice(0, 4).equals(MAGIC), 'not an RZX file: ' + bytes.slice(0, 4).toString('hex'));
  must((await leftover(page)).length === 0, 'scratch files left: ' + JSON.stringify(await leftover(page)));
  fs.writeFileSync(path.join(E.SMOKE_OUT, 'page.rzx'), bytes);
  const saved = page.msgs.map((m) => /recording saved - (\d+) frames/.exec(m)).filter(Boolean).pop();
  must(saved, 'no "recording saved" line');
  const n = Number(saved[1]);
  const replay = nativeReplay(bytes, n + 100);
  const png = await decode(replay.png);
  const dk = paperDiff(png, await oracle('keys')), di = paperDiff(png, await oracle('idle'));
  must(dk === 0, `native replay paper region vs keys.png: ${dk} px differ`);
  must(di > 0, 'the replay of the typed recording shows the untyped screen');
  const before = (await rec(page)).ready;
  await page.evaluate((a) => window.player0.load({ data: Uint8Array.from(a), name: 'mine.rzx' }), Array.from(bytes));
  must((await rec(page)).ready === before + 1, 'onReady did not fire for load(mine.rzx)');
  must(page.msgs.filter((m) => /RZX: playback started/.test(m)).length === 1, 'no "playback started" for mine.rzx');
  await page.ctx.close();
});

row('BRZ-07', 'the methods\' contract: both reject before the start click; stop with no recording, start twice, start during a playback (onError once, the log says why) all reject; after destroy() both throw', async () => {
  const a = await newPage();
  await open(a, [{ muted: true, machine: '48k' }]);
  for (const m of ['startRzxRecording', 'stopRzxRecording']) {
    const out = await rejection(a, m);
    must(/jnext: the machine is not running/.test(out), `${m} before the click: ${out}`);
  }
  must((await rec(a)).errors.length === 0, 'onError fired for a call made before the start');
  await click(a); await waitReady(a, 0, 1);
  const s = await rejection(a, 'stopRzxRecording');
  must(/jnext: no RZX recording is running/.test(s), 'stop with none: ' + s);
  await a.evaluate(() => window.player0.startRzxRecording());
  const twice = await rejection(a, 'startRzxRecording');
  must(/the last RZX recording has not been collected; call stopRzxRecording\(\) first/.test(twice), 'second start: ' + twice);
  await a.evaluate(() => window.player0.destroy());
  for (const m of ['startRzxRecording', 'stopRzxRecording']) {
    const thrown = await a.evaluate((m) => { try { window.player0[m](); return 'no throw'; } catch (e) { return e.message; } }, m);
    must(/destroyed/.test(thrown), `${m} after destroy: ${thrown}`);
  }
  await a.ctx.close();
  const b = await boot({ file: '/scratch/keys48.rzx' });
  await waitReady(b, 0, 1);
  const out = await rejection(b, 'startRzxRecording');
  must(/the RZX recording did not start/.test(out), 'start during playback: ' + out);
  const rb = await rec(b);
  must(rb.errors.length === 1, 'errors ' + JSON.stringify(rb.errors));
  must(logHas(b, /cannot record while an RZX recording is playing/), 'the log does not say why');
  await b.ctx.close();
});

row('BRZ-08', 'on the default machine (a Next) startRzxRecording() rejects, onError fires once, the log says a Next cannot record, no scratch file, the frame counter keeps moving', async () => {
  const page = await boot({ file: '/nex/red.nex' });
  await waitReady(page, 0, 1);
  const out = await rejection(page, 'startRzxRecording');
  must(/^the RZX recording did not start \(the log says why\)$/.test(out), 'rejection: ' + out);
  must((await rec(page)).errors.length === 1, 'errors ' + JSON.stringify((await rec(page)).errors));
  must(logHas(page, /cannot record on a ZX Spectrum Next/), 'the log does not say why');
  must((await leftover(page)).length === 0, 'scratch files left');
  const f = await frame(page);
  await waitFrame(page, 0, f + 20);
  await page.ctx.close();
});

row('BRZ-09', 'a reset ends the recording and keeps it: the log says the reset ends the recording to /tmp/jnext-page-*, stopRzxRecording() still returns an RZX! file, and the native replay of it plays at least 90 frames', async () => {
  const page = await boot({ machine: '48k' });
  await waitReady(page, 0, 1);
  await waitFrame(page, 0, 100);
  await page.evaluate(() => window.player0.startRzxRecording());
  const f0 = await frame(page);
  await waitFrame(page, 0, f0 + 100);
  await page.evaluate(() => window.player0.reset('hard'));
  must(logHas(page, /RZX: .*ends the recording to '\/tmp\/jnext-page-/), 'no "ends the recording" line: ' + JSON.stringify(page.msgs.filter((m) => /RZX/.test(m))));
  const bytes = Buffer.from(await page.evaluate(() => window.player0.stopRzxRecording().then((b) => Array.from(b))));
  must(bytes.length > 4 && bytes.slice(0, 4).equals(MAGIC), 'not an RZX file: ' + bytes.slice(0, 4).toString('hex'));
  must((await leftover(page)).length === 0, 'scratch files left');
  const m = /RZX: playback started - (\d+) frames/.exec(nativeReplay(bytes, 0).log);
  must(m && Number(m[1]) >= 90, 'native replay: ' + (m ? m[0] : 'no "playback started" line'));
  await page.ctx.close();
});

row('BRZ-10', 'the replay is aligned with the native one frame by frame: at frame 201, where the native screen differs from frame 200, the page equals the native frame 201 (0 px)', async () => {
  const f200 = await oracle('rzxplay48f200'), f201 = await oracle('rzxplay48f201');
  const dn = diff(f200, f201);
  must(typeof dn === 'number' && dn > 0, `the native frames 200 and 201 are the same picture (${dn} px): the row would not see an offset`);
  const page = await boot({ file: '/scratch/keys48.rzx', args: shot(201) });
  await waitReady(page, 0, 1);
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 120000);
  const got = await grabPng(page, 0, '/tmp/s.png');
  const d = diff(got, f201);
  must(d === 0, `frame-201 screenshot vs native frame 201: ${d} px differ (vs native 200: ${diff(got, f200)} px)`);
  await page.ctx.close();
});

row('BRZ-11', 'two recordings in one player: start, 50 frames, stop, start, 50 frames, stop: both files are RZX! and no scratch file is left', async () => {
  const page = await boot({ machine: '48k' });
  await waitReady(page, 0, 1);
  await waitFrame(page, 0, 100);
  for (let n = 1; n <= 2; n++) {
    await page.evaluate(() => window.player0.startRzxRecording());
    const f = await frame(page);
    await waitFrame(page, 0, f + 50);
    const bytes = Buffer.from(await page.evaluate(() => window.player0.stopRzxRecording().then((b) => Array.from(b))));
    must(bytes.length > 4 && bytes.slice(0, 4).equals(MAGIC), `recording ${n}: not an RZX file: ${bytes.slice(0, 4).toString('hex')}`);
  }
  must((await leftover(page)).length === 0, 'scratch files left: ' + JSON.stringify(await leftover(page)));
  must((await rec(page)).errors.length === 0, 'errors ' + JSON.stringify((await rec(page)).errors));
  await page.ctx.close();
});

row('BRZ-12', 'a recording ended by reset() waits to be collected: startRzxRecording() rejects "has not been collected", stopRzxRecording() returns the file, and a start after that resolves', async () => {
  const page = await boot({ machine: '48k' });
  await waitReady(page, 0, 1);
  await waitFrame(page, 0, 100);
  await page.evaluate(() => window.player0.startRzxRecording());
  const f = await frame(page);
  await waitFrame(page, 0, f + 50);
  await page.evaluate(() => window.player0.reset('hard'));
  must(logHas(page, /RZX: .*ends the recording to '\/tmp\/jnext-page-/), 'the reset did not end the recording');
  const out = await rejection(page, 'startRzxRecording');
  must(out === 'jnext: the last RZX recording has not been collected; call stopRzxRecording() first', 'start after the reset: ' + out);
  const bytes = Buffer.from(await page.evaluate(() => window.player0.stopRzxRecording().then((b) => Array.from(b))));
  must(bytes.length > 4 && bytes.slice(0, 4).equals(MAGIC), 'not an RZX file: ' + bytes.slice(0, 4).toString('hex'));
  await page.evaluate(() => window.player0.startRzxRecording());
  must((await rec(page)).errors.length === 0, 'errors ' + JSON.stringify((await rec(page)).errors));
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
