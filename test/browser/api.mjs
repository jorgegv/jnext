// Browser rows WEBAPI-01..21: the JavaScript API (jnext.mount) in headless Chromium
// (make wasm-browser-test, spec api.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Pages: test/browser/page/api.html (the fixture, mounts one or two players with the options
// a row gives it) and the shipped examples (build/<name>.html). The SD-card image is served by
// a REAL http server on 127.0.0.1 with Access-Control-Allow-Origin, so the browser's own CORS
// check decides (WEBAPI-21 is that proof; image.mjs WBI-PW-05 is the no-header counterpart).
// The iframe row serves the game page from a second real server (another site).
// Oracles: the native runs of browser-smoke.sh (keys.png, idle.png), the contract of the man
// page's THE BROWSER BUILD, #463's keyed-screen protocol (frame 200, P 1 2 Enter, screenshot at 320),
// and version.yaml. The card is copyrighted: it lives in the scratch directory and in the page's
// storage only; tracing, video and HAR are never enabled.
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

const h = new Harness({ build: BUILD, page: path.join(ROOT, 'test/browser/page'),
                        nex: path.join(ROOT, 'test/00regression/nex'), scratch: SCRATCH });

// Real servers (ephemeral ports on 127.0.0.1). B: the image, with CORS. C: another site's game page.
const bLog = [];
const B = http.createServer((req, res) => {
  bLog.push({ url: req.url, origin: req.headers.origin || null });
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length,
                       'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
const cLog = [];
const C = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  cLog.push(u.pathname);
  const m = /^\/(build|nex)\/([^/]+)$/.exec(u.pathname);
  const f = m && path.join(m[1] === 'build' ? BUILD : path.join(ROOT, 'test/00regression/nex'), m[2]);
  if (!f || !fs.existsSync(f)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => C.listen(0, '127.0.0.1', r));
const BURL = `http://127.0.0.1:${B.address().port}`;
const CURL = `http://127.0.0.1:${C.address().port}`;
h.origins[BURL] = { passthrough: true };
h.origins[CURL] = { passthrough: true };
const IMAGE = { url: BURL + '/card.img', sha256: SHA };

await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

// ---- helpers -------------------------------------------------------------------------------
function attach(page) {
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
}
const pageErrors = (page) => page.msgs.filter((m) => m.startsWith('PAGEERROR'));
// Load the fixture with `players` (option objects), wait until every mount() settled.
async function open(page, players, extra = {}) {
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(extra.image || IMAGE), players: JSON.stringify(players) });
  await page.goto('http://localhost/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length);
}
const rec = (page, i = 0) => page.evaluate((i) => {
  const r = window.apiTest.players[i];
  return { progress: r.progress, ready: r.ready, errors: r.errors, mountError: r.mountError, order: r.order,
           applied: r.applied, argv: r.player ? r.player.argv : null, state: r.player ? r.player.state : null };
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
const centre = async (page, i = 0) => {
  const im = await decode(await page.locator(`#p${i} canvas`).screenshot());
  const o = ((im.h >> 1) * im.w + (im.w >> 1)) * 4;
  return [im.d[o], im.d[o + 1], im.d[o + 2]];
};
const isRed = (c) => c[0] > 150 && c[1] < 80 && c[2] < 80;
const isBlue = (c) => c[2] > 150 && c[0] < 80 && c[1] < 80;
async function waitColour(page, i, test, frames = 100) {
  const f0 = await frame(page, i);
  await waitFrame(page, i, f0 + frames);
  const c = await centre(page, i);
  must(test(c), `canvas centre ${JSON.stringify(c)}`);
}
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
const RED = { file: '/nex/red.nex', machine: 'next', muted: true };
const BLUE = { file: '/nex/blue.nex', machine: 'next', muted: true };
let main = null;   // the page rows 01-11 share: one context, so the warm start is recorded once
const IDB_FILES = () => new Promise((res, rej) => {
  const rq = indexedDB.open('/jnext/warm-start');
  rq.onerror = () => rej(rq.error);
  rq.onsuccess = () => {
    const db = rq.result;
    if (!db.objectStoreNames.contains('FILE_DATA')) { res([]); return; }
    const tx = db.transaction('FILE_DATA', 'readonly'), st = tx.objectStore('FILE_DATA');
    const keys = st.getAllKeys(), vals = st.getAll();
    tx.oncomplete = () => res(keys.result.map((k, i) => ({ key: k, bytes: vals.result[i].contents ? vals.result[i].contents.length : -1 })));
  };
});

// ---- rows ------------------------------------------------------------------------------------
row('WEBAPI-01', 'mount() resolves; player.argv is the documented command line, its first four elements equal installImage\'s; state is waiting', async () => {
  main = await h.newPage(); attach(main);
  await open(main, [RED]);
  const r = await rec(main);
  must(!r.mountError, 'mount: ' + r.mountError);
  const inst = await main.evaluate((sha) => JnextImage.installImage(
    { FS: { mkdirTree() {}, writeFile() {} } }, { bytes: new Uint8Array(0), sha256: sha, zipped: false }), SHA);
  const want = ['--sdcard', '/sdcard/image.img', '--sdcard-sha256', SHA, '--machine', 'next', '--load', '/media/0-red.nex',
                '--set', 'startup.silent=true'];
  must(JSON.stringify(r.argv) === JSON.stringify(want), 'argv ' + JSON.stringify(r.argv));
  must(JSON.stringify(r.argv.slice(0, 4)) === JSON.stringify(inst), 'installImage ' + JSON.stringify(inst));
  must(r.state === 'waiting', 'state ' + r.state);
});

row('WEBAPI-02', 'first visit: every mount-time onProgress phase is documented; download fractions in [0,1], never decreasing, ending at 1', async () => {
  const r = await rec(main);
  const dl = r.progress.filter((p) => p[0] === 'download').map((p) => p[1]);
  must(r.progress.every((p) => ['download', 'warm-start', 'restore'].includes(p[0])), JSON.stringify(r.progress.map((p) => p[0])));
  must(dl.length > 0 && dl.every((f, i) => f >= 0 && f <= 1 && (i === 0 || f >= dl[i - 1])) && dl[dl.length - 1] === 1, 'download ' + JSON.stringify(dl));
});

row('WEBAPI-03', 'after the click: warm-start progress reaches 1, exactly one onReady after every onProgress, ready resolves, no onError, state running', async () => {
  await click(main);
  await waitReady(main, 0, 1);
  const r = await rec(main);
  const ws = r.progress.filter((p) => p[0] === 'warm-start').map((p) => p[1]);
  const lastProgress = r.order.lastIndexOf('progress'), readyAt = r.order.indexOf('ready');
  const settled = await main.evaluate(() => window.player0.ready.then(() => 'ok', (e) => 'rejected ' + e.message));
  must(ws.length > 0 && ws[ws.length - 1] === 1, 'warm-start ' + JSON.stringify(ws.slice(-3)));
  must(r.ready === 1 && readyAt > lastProgress, `ready=${r.ready} order=${r.order.slice(-4)}`);
  must(settled === 'ok' && r.errors.length === 0 && r.state === 'running', `${settled} errors=${JSON.stringify(r.errors)} state=${r.state}`);
});

row('WEBAPI-04', 'a .tzx program: at onReady the load is applied (load_applied == 1); ready never precedes the load', async () => {
  const page = main;
  await open(page, [{ machine: '48k', muted: true, file: '/scratch/beeper_demo.tzx' }]);
  await click(page);
  await waitReady(page, 0, 1);
  const r = await rec(page);
  must(r.applied === 1, 'applied at ready: ' + r.applied + ' errors ' + JSON.stringify(r.errors));
});

row('WEBAPI-05', 'red.nex: the canvas centre is red within 100 emulated frames of ready', async () => {
  await open(main, [RED]);
  await click(main);
  await waitReady(main, 0, 1);
  await waitColour(main, 0, isRed);
});

row('WEBAPI-06', 'pause(): the frame counter stops and state is paused; resume(): it advances again, with no catch-up burst', async () => {
  await main.evaluate(() => window.player0.pause());
  const f = await frame(main);
  await sleep(500);
  const f2 = await frame(main);
  const st = await main.evaluate(() => window.player0.state);
  must(f2 === f && st === 'paused', `frames ${f} -> ${f2} state ${st}`);
  await main.evaluate(() => window.player0.resume());
  await sleep(500);
  const f3 = await frame(main);
  const st2 = await main.evaluate(() => window.player0.state);
  must(f3 - f2 >= 5 && f3 - f2 <= 30 && st2 === 'running', `advanced ${f3 - f2} frames in 500 ms, state ${st2}`);
});

row('WEBAPI-07', 'setVolume(0.5) -> -6.02 dB, setVolume(0) -> -120 dB, setVolume(2) throws RangeError and leaves the gain', async () => {
  const g = (p) => p.evaluate(() => window.player0._module._jnext_web_gain_db());
  await main.evaluate(() => window.player0.setVolume(0.5));
  const a = await g(main);
  await main.evaluate(() => window.player0.setVolume(0));
  const b = await g(main);
  const thrown = await main.evaluate(() => { try { window.player0.setVolume(2); return 'no throw'; } catch (e) { return e.name; } });
  const c = await g(main);
  must(Math.abs(a + 6.02) < 0.01 && b === -120 && thrown === 'RangeError' && c === -120, `${a} ${b} ${thrown} ${c}`);
});

row('WEBAPI-08', 'load(blue.nex) resolves; onReady count increases; the canvas centre turns blue within 100 frames', async () => {
  const before = (await rec(main)).ready;
  await main.evaluate(() => window.player0.load('/nex/blue.nex'));
  const r = await rec(main);
  must(r.ready === before + 1, `ready ${before} -> ${r.ready}`);
  await waitColour(main, 0, isBlue);
});

row('WEBAPI-09', 'reset() resolves with onReady and the program is back; reset(soft) and reset(hard) resolve without an error', async () => {
  const before = await rec(main);
  await main.evaluate(() => window.player0.reset());
  const r = await rec(main);
  must(r.ready === before.ready + 1, `ready ${before.ready} -> ${r.ready}`);
  await waitColour(main, 0, isBlue);
  await main.evaluate(() => window.player0.reset('soft'));
  await main.evaluate(() => window.player0.reset('hard'));
  const e = (await rec(main)).errors;
  must(e.length === 0, 'errors ' + JSON.stringify(e));
  await main.evaluate(() => window.player0.reset());   // the program again, for the next rows
  await waitColour(main, 0, isBlue);
});

row('WEBAPI-10', 'load() of 16 random bytes named bad.nex rejects, onError gets the loader\'s message, state stays running', async () => {
  const before = (await rec(main)).errors.length;
  const out = await main.evaluate(() => window.player0.load({ data: Uint8Array.from({ length: 16 }, (_, i) => (i * 37 + 11) & 255), name: 'bad.nex' })
    .then(() => 'resolved', (e) => e.message));
  const r = await rec(main);
  must(out !== 'resolved' && /bad\.nex/.test(out), 'rejection: ' + out);
  must(r.errors.length === before + 1 && /bad\.nex/.test(r.errors[r.errors.length - 1]), 'errors ' + JSON.stringify(r.errors));
  must(r.state === 'running', 'state ' + r.state);
  await main.evaluate(() => window.player0.reset());   // blue back for the next row
  await waitColour(main, 0, isBlue);
});

row('WEBAPI-11', 'load(x.sna) rejects with the format refusal (.nex, .tap, .tzx); no cold boot: frames keep counting, the canvas stays blue', async () => {
  const f = await frame(main);
  const out = await main.evaluate(() => window.player0.load({ data: new Uint8Array(8), name: 'x.sna' }).then(() => 'resolved', (e) => e.message));
  await sleep(300);
  const f2 = await frame(main);
  must(/\.nex, \.tap, \.tzx/.test(out), 'rejection: ' + out);
  must(f2 > f, `frames ${f} -> ${f2}`);
  must(isBlue(await centre(main)), 'canvas not blue');
});

row('WEBAPI-12', 'args --esp and --headless: ready rejects with the browser refusal; state is exited', async () => {
  const a = await h.newPage(); attach(a);
  await open(a, [{ args: ['--esp'] }]);
  await click(a);
  const ra = await a.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  const sa = await a.evaluate(() => window.player0.state);
  must(/not available in the browser build/.test(ra) && sa === 'exited', `${ra} / ${sa}`);
  const b = await h.newPage(); attach(b);
  await open(b, [{ args: ['--headless'] }]);
  await click(b);
  const rb = await b.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  const sb = await b.evaluate(() => window.player0.state);
  must(/browser page/.test(rb) && sb === 'exited', `${rb} / ${sb}`);
  await a.ctx.close(); await b.ctx.close();
});

row('WEBAPI-13', 'an unknown option (machin) rejects mount(), naming it, before any request reaches the image server', async () => {
  const page = await h.newPage(); attach(page);
  const n = bLog.length;
  await open(page, [{ machin: 'next' }]);
  const r = await rec(page);
  must(r.mountError && r.mountError.includes('machin'), 'mount error: ' + r.mountError);
  must(bLog.length === n, `${bLog.length - n} request(s) reached the image server`);
  await page.ctx.close();
});

row('WEBAPI-14', 'an unknown --set key: ready rejects with the --set error naming the key', async () => {
  const page = await h.newPage(); attach(page);
  await open(page, [{ machine: '48k', set: { 'startup.nope': '1' } }]);
  await click(page);
  const out = await page.evaluate(() => window.player0.ready.then(() => 'ok', (e) => e.message));
  must(/startup\.nope/.test(out), 'ready: ' + out);
  await page.ctx.close();
});

row('WEBAPI-15', 'two players on one page: own canvases (red, blue); pausing one leaves the other running', async () => {
  const page = await h.newPage(); attach(page);
  await open(page, [RED, BLUE]);
  await click(page, 0); await click(page, 1);
  await waitReady(page, 0, 1); await waitReady(page, 1, 1);
  await waitColour(page, 0, isRed); await waitColour(page, 1, isBlue);
  await page.evaluate(() => window.player0.pause());
  const a = await frame(page, 0), b = await frame(page, 1);
  await sleep(500);
  const a2 = await frame(page, 0), b2 = await frame(page, 1);
  must(a2 === a && b2 > b, `player0 ${a} -> ${a2}, player1 ${b} -> ${b2}`);
  await page.ctx.close();
});

row('WEBAPI-16', 'two 48K players, keys typed into the focused one only: player 2 equals the native typist, player 1 the native untyped run', async () => {
  const M = 320, F0 = 200;
  const mk = () => ({ machine: '48k', muted: true, args: ['--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', String(M)] });
  const page = await h.newPage(); attach(page);
  await open(page, [mk(), mk()]);
  await click(page, 0); await click(page, 1);
  await waitReady(page, 0, 1); await waitReady(page, 1, 1);
  await waitFrame(page, 0, F0); await waitFrame(page, 1, F0);
  await page.focus('#p1 canvas');
  for (const k of ['p', '1', '2', 'Enter']) { await page.keyboard.press(k, { delay: 60 }); await sleep(120); }
  await waitFrame(page, 0, M + 5); await waitFrame(page, 1, M + 5);
  const typed = await grabPng(page, 1, '/media/s.png'), idle = await grabPng(page, 0, '/media/s.png');
  const dk = diff(typed, await decode(fs.readFileSync(path.join(ORACLE, 'keys.png'))));
  const di = diff(idle, await decode(fs.readFileSync(path.join(ORACLE, 'idle.png'))));
  must(dk === 0 && di === 0, `typed vs keys.png: ${dk}; idle vs idle.png: ${di}`);
  await page.ctx.close();
});

row('WEBAPI-17', 'destroy() resolves after the machine exits, removes the player, later calls throw; the other player runs on, no page error', async () => {
  const page = await h.newPage(); attach(page);
  await open(page, [{ machine: '48k', muted: true }, { machine: '48k', muted: true }]);
  await click(page, 0); await click(page, 1);
  await waitReady(page, 0, 1); await waitReady(page, 1, 1);
  await page.evaluate(() => window.player0.destroy());
  const left = await page.evaluate(() => document.querySelectorAll('.jnext-player').length);
  const thrown = await page.evaluate(() => { try { window.player0.pause(); return 'no throw'; } catch (e) { return e.message; } });
  const f = await frame(page, 1);
  await page.focus('#p1 canvas');
  for (const k of ['a', 'b', 'c']) { await page.keyboard.press(k); await sleep(200); }
  await sleep(2000);
  const f2 = await frame(page, 1);
  must(left === 1 && /destroyed/.test(thrown), `players left ${left}, ${thrown}`);
  must(f2 > f && pageErrors(page).length === 0, `frames ${f} -> ${f2}, errors ${JSON.stringify(pageErrors(page))}`);
  await page.ctx.close();
});

row('WEBAPI-18', 'itch-style: a host page embeds the game page from another site (iframe); a click inside gives onReady and a red canvas', async () => {
  const page = await h.newPage(); attach(page);
  const game = `${CURL}/build/itch-game.html?` + new URLSearchParams({ image: IMAGE.url, sha256: SHA, file: `${CURL}/nex/red.nex` });
  await page.goto('http://localhost/build/itch-host.html?game=' + encodeURIComponent(game));
  const frm = page.frameLocator('#game');
  await frm.locator('.jnext-overlay').waitFor({ timeout: 300000 });
  // The overlay says "Loading..." until the image is installed, then "Click to start".
  await frm.locator('.jnext-overlay', { hasText: 'Click to start' }).waitFor({ timeout: 300000 });
  await frm.locator('.jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => window.messages.includes('jnext-ready') || window.messages.some((m) => m.startsWith('jnext-error')), null, 300000);
  const msgs = await page.evaluate(() => window.messages);
  must(msgs.includes('jnext-ready'), 'messages ' + JSON.stringify(msgs));
  await sleep(1500);
  const im = await decode(await frm.locator('canvas').screenshot());
  const o = ((im.h >> 1) * im.w + (im.w >> 1)) * 4;
  must(isRed([im.d[o], im.d[o + 1], im.d[o + 2]]), 'centre ' + [im.d[o], im.d[o + 1], im.d[o + 2]]);
  await page.ctx.close();
});

row('WEBAPI-19', 'build/ holds jnext-<version>.js and .wasm; jnext.version equals version.yaml; the page asked for the versioned .wasm, never jnext_web.wasm; no version literal under web/', async () => {
  must(fs.existsSync(path.join(BUILD, `jnext-${VERSION}.js`)) && fs.existsSync(path.join(BUILD, `jnext-${VERSION}.wasm`)), 'files missing');
  const yaml = fs.readFileSync(path.join(ROOT, 'version.yaml'), 'utf8').match(/^version: *(.+)$/m)[1].trim();
  const v = await main.evaluate(() => jnext.version);
  must(v === yaml && yaml === VERSION, `jnext.version ${v}, version.yaml ${yaml}, build ${VERSION}`);
  must(h.requests.includes(`/build/jnext-${VERSION}.wasm`) && !h.requests.some((p) => /jnext_web/.test(p)), 'wasm requests: ' + JSON.stringify([...new Set(h.requests.filter((p) => /wasm/.test(p)))]));
  let found = '';
  try { found = execFileSync('git', ['-C', ROOT, 'grep', '-n', '-F', VERSION, '--', 'web'], { encoding: 'utf8' }); } catch (e) { found = ''; }
  must(found === '', 'version literal under web/: ' + found);
});

row('WEBAPI-20', 'two Next players share the warm-start store on a first visit; after a reload both restore and reach ready; IndexedDB holds a whole recording', async () => {
  const page = await h.newPage(); attach(page);
  await open(page, [RED, BLUE]);
  await click(page, 0); await click(page, 1);
  await waitReady(page, 0, 1); await waitReady(page, 1, 1);
  await sleep(1500);   // both syncfs pushes land
  await open(page, [RED, BLUE]);
  await click(page, 0); await click(page, 1);
  await waitReady(page, 0, 1); await waitReady(page, 1, 1);
  const r0 = await rec(page, 0), r1 = await rec(page, 1);
  const files = (await page.evaluate(IDB_FILES)).filter((f) => f.bytes >= 0);
  for (const r of [r0, r1]) {
    must(r.progress.some((p) => p[0] === 'restore'), 'no restore: ' + JSON.stringify(r.progress.map((p) => p[0]).slice(-4)));
    must(!r.progress.some((p) => p[0] === 'warm-start' && p[1] < 1), 'warm-start below 1 on a restore');
    must(r.errors.length === 0, 'errors ' + JSON.stringify(r.errors));
  }
  must(files.length >= 1 && files.every((f) => f.bytes > 1000), 'IndexedDB ' + JSON.stringify(files));
  await page.ctx.close();
});

row('WEBAPI-21', 'a real image host that sends Access-Control-Allow-Origin: the load succeeds and the request carried the page origin', async () => {
  const page = await h.newPage(); attach(page);
  const n = bLog.length;
  await open(page, [{ machine: '48k', muted: true }]);
  const r = await rec(page);
  const mine = bLog.slice(n).filter((x) => x.url === '/card.img');
  must(!r.mountError && r.state === 'waiting', 'mount: ' + r.mountError);
  must(mine.length === 1 && mine[0].origin === 'http://localhost', 'server log ' + JSON.stringify(bLog.slice(n)));
  await page.ctx.close();
});

// ---- run -------------------------------------------------------------------------------------
let pass = 0, fail = 0;
for (const r of rows) {
  try { await r.fn(); pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
B.close(); C.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
