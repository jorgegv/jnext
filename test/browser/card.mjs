// Browser rows BCARD-01..21 and 26..33: the card flow of the JavaScript API (GH #458), in headless Chromium
// (make wasm-browser-test, spec card.mjs). Run by browser-smoke.sh on the SDK's node.
//
// The player makes NextZXOS's SD card in the page from the official distribution zip, after the
// visitor's click: from the browser's cache, from a download (when the host allows the page to
// read it) or from a file the visitor chooses. These rows run it against a SYNTHETIC distribution
// (invented files, built with mtools and python at run time, licence-clean) through the
// localhost-only development seam, and against REAL data in BCARD-13 only (a zip of the cached
// image made at run time, the pinned card digest must come out). Nothing of NextZXOS is ever
// committed: every card and zip lives in the scratch directory the script deletes.
//
// Hosts are REAL servers (ephemeral ports on 127.0.0.1), so Chromium applies its own CORS rules:
//   A  the page host (http://localhost:<port>): the fixture page, the build, the NEX files;
//   D  a distribution host WITH Access-Control-Allow-Origin: *; E one WITHOUT; F one that never
//      answers a HEAD. BCARD-09 serves a page from https://other.example (a routed origin) to prove the
//      seam is ignored off localhost.
// Oracles: the native tool's card of the synthetic zip (SMOKE_SYN_CARD_SHA256), the pinned card
// digest of the release (SMOKE_PINNED_CARD_SHA256), the native run of tilemap_demo.nex
// ($SMOKE_ORACLE/tilemap.png), the three statements of the user guide's 8.4 page.
import { Harness, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';
import zlib from 'zlib';
import crypto from 'crypto';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SYN_DIR = E.SMOKE_SYN_DIR;
const SYN_ZIP = path.join(SYN_DIR, 'synthetic-distro.zip');
const SYN = { size: Number(E.SMOKE_SYN_ZIP_SIZE), sha256: E.SMOKE_SYN_ZIP_SHA256, cardSha256: E.SMOKE_SYN_CARD_SHA256 };
const REAL = { size: Number(E.SMOKE_REAL_ZIP_SIZE), sha256: E.SMOKE_REAL_ZIP_SHA256 };
const PINNED_CARD = E.SMOKE_PINNED_CARD_SHA256;
const DISTRO_URL = E.SMOKE_DISTRO_URL;
const CACHE = 'jnext-sdcard-v2', OLD = 'jnext-sdcard-v1';
const SYN_CARD = path.join(SYN_DIR, 'synthetic-card.img');
const KEY = (sha) => 'https://card.jnext.invalid/' + sha;
const STATEMENTS = ["SpecNext's copyrighted software", 'JNEXT does not ship, host or serve', "stays in the visitor's browser"];

const h = new Harness({});   // launches and closes the pinned browser; every server below is real

const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream',
                '.png': 'image/png' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), nex: path.join(ROOT, 'test/00regression/nex'),
                img: path.join(ROOT, 'test/00regression/img'), syn: SYN_DIR };
function serveFile(req, res) {
  const u = new URL(req.url, 'http://x');
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return null; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream' });
  res.end(fs.readFileSync(f));
  return f;
}
const aLog = [];
const A = http.createServer((req, res) => { aLog.push(new URL(req.url, 'http://x').pathname); serveFile(req, res); });
await new Promise((r) => A.listen(0, '127.0.0.1', r));

// A distribution host: serves the synthetic zip and the real one, HEAD and GET, logging every request.
// mode 'cors': Access-Control-Allow-Origin: *; 'nocors': none; 'hang': a HEAD is never answered;
// 'get500': a good HEAD, then every GET fails with HTTP 500; 'badlen': a HEAD that claims another length.
function distHost(mode) {
  const log = [];
  const srv = http.createServer((req, res) => {
    log.push({ method: req.method, path: req.url, origin: req.headers.origin || null });
    if (mode === 'hang' && req.method === 'HEAD') return;   // held open until the row ends
    const f = { '/synthetic-distro.zip': SYN_ZIP, '/real-distro.zip': path.join(SCRATCH, 'real-distro.zip') }[req.url];
    if (!f) { res.writeHead(404); res.end(); return; }
    const size = fs.statSync(f).size;
    const hd = { 'Content-Type': 'application/zip', 'Content-Length': size };
    if (mode !== 'nocors') hd['Access-Control-Allow-Origin'] = '*';
    if (mode === 'badlen' && req.method === 'HEAD') hd['Content-Length'] = size + 7;
    if (mode === 'get500' && req.method === 'GET') { res.writeHead(500, { 'Access-Control-Allow-Origin': '*' }); res.end(); return; }
    res.writeHead(200, hd);
    if (req.method === 'HEAD') { res.end(); return; }
    fs.createReadStream(f).pipe(res);
  });
  return { srv, log };
}
const D = distHost('cors'), N = distHost('nocors'), F = distHost('hang'), G = distHost('get500'), L = distHost('badlen');
for (const x of [D, N, F, G, L]) await new Promise((r) => x.srv.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`;
const url = (x, p) => `http://127.0.0.1:${x.srv.address().port}${p}`;

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count

// ---- helpers -------------------------------------------------------------------------------
async function newPage(ctxOpts = {}) {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1, ...ctxOpts });
  const page = await ctx.newPage();
  page.ctx = ctx;
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  return page;
}
const seam = (x, file, extra = {}) => ({ distribution: { url: url(x, '/' + file), ...(file.startsWith('synthetic') ? SYN : REAL) }, ...extra });
const SYNSEAM = (x, extra) => seam(x, 'synthetic-distro.zip', extra);
// Load the fixture with `players`, wait until every mount() settled (the click has not happened yet).
async function open(page, players, dev, base = AURL) {
  const q = new URLSearchParams({ module: MODULE, players: JSON.stringify(players) });
  if (dev) q.set('dev', JSON.stringify(dev));
  await page.goto(base + '/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length, 120000);
}
const rec = (page, i = 0) => page.evaluate((i) => {
  const r = window.apiTest.players[i];
  return { progress: r.progress, ready: r.ready, errors: r.errors, mountError: r.mountError, order: r.order,
           argv: r.player ? r.player.argv : null, state: r.player ? r.player.state : null };
}, i);
const click = (page, i = 0) => page.click(`#p${i} .jnext-overlay`, { timeout: 120000, noWaitAfter: true });
const waitReady = (page, i = 0, ms = 300000) =>
  waitFor(page, (i) => window.apiTest.players[i].ready >= 1 || window.apiTest.players[i].errors.length > 0, i, ms);
const waitPanel = (page, i = 0, ms = 120000) => waitFor(page, (i) => !!document.querySelector(`#p${i} .jnext-pick`), i, ms);
// the SHA-256 of the card in the page's file system, and of the Cache API's entries
const memCardSha = (page, i = 0) => page.evaluate(async (i) => {
  const d = window['player' + i]._module.FS.readFile('/sdcard/image.img');
  const h = new Uint8Array(await crypto.subtle.digest('SHA-256', d));
  return [...h].map((b) => b.toString(16).padStart(2, '0')).join('');
}, i);
const cacheKeys = (page) => page.evaluate(async (name) => {
  if (!(await caches.has(name))) return [];
  const c = await caches.open(name);
  return (await c.keys()).map((r) => r.url);
}, CACHE);
const allCaches = (page) => page.evaluate(async () => {
  const out = {};
  for (const n of await caches.keys()) out[n] = (await (await caches.open(n)).keys()).map((r) => r.url);
  return out;
});
const kinds = (log) => log.map((r) => r.method);
const panelText = (page, i = 0) => page.evaluate((i) => document.querySelector(`#p${i} .jnext-pick`).innerText, i);
const lastLine = (page, i = 0) => page.evaluate((i) => document.querySelector(`#p${i} .jnext-pick-reason`).textContent, i);
const P48 = { machine: '48k', muted: true };
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
// A stored entry as node bytes (moved as base64), or null; and the same stored from node bytes.
const entry = async (page, name, key) => {
  const b64 = await page.evaluate(async ([name, key]) => {
    const r = await (await caches.open(name)).match(key);
    if (!r) return null;
    const d = new Uint8Array(await r.arrayBuffer()); let s = '';
    for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
    return btoa(s);
  }, [name, key]);
  return b64 === null ? null : Buffer.from(b64, 'base64');
};
const putEntry = (page, name, key, buf) => page.evaluate(async ([name, key, b64]) => {
  await (await caches.open(name)).put(key, new Response(Uint8Array.from(atob(b64), (c) => c.charCodeAt(0))));
}, [name, key, buf.toString('base64')]);
// The gunzipped entry's SHA-256 with a FOREIGN reader (node's zlib); throws when node refuses it.
const entrySha = async (page) => sha256(zlib.gunzipSync(await entry(page, CACHE, KEY(SYN.cardSha256))));
// A fresh context that has made the card once (first visit through D), left on its fixture page.
async function warm(ctxOpts) {
  const p = await newPage(ctxOpts);
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  must((await rec(p)).errors.length === 0, 'the first visit failed');
  return p;
}
// Visit again through `x`; resolves when ready (or failed). Returns the requests it made to x.
async function revisit(p, x) {
  const n = x.log.length;
  await open(p, [P48], SYNSEAM(x));
  await click(p);
  await waitReady(p);
  return x.log.slice(n);
}
const gets = (log) => log.filter((q) => q.method === 'GET').length;

// ---- rows ------------------------------------------------------------------------------------
let main = null;   // BCARD-01 and 02 share one context: the Cache API persists in it

row('BCARD-01', 'auto, CORS available: nothing before the click, then one HEAD and one GET; download reaches 1, then build 1; the card equals the native one and is the ONE cache entry (no zip anywhere); argv starts --sdcard', async () => {
  main = await newPage();
  await open(main, [P48], SYNSEAM(D));
  must(D.log.length === 0, 'a request before the click: ' + JSON.stringify(D.log));
  must((await rec(main)).state === 'waiting' && (await rec(main)).argv == null, 'state/argv before the click');
  await click(main);
  await waitReady(main);
  const r = await rec(main);
  must(r.errors.length === 0, 'errors ' + JSON.stringify(r.errors));
  must(JSON.stringify(kinds(D.log)) === '["HEAD","GET"]' && D.log.every((q) => q.path === '/synthetic-distro.zip'), 'requests ' + JSON.stringify(D.log));
  const dl = r.progress.filter((p) => p[0] === 'download').map((p) => p[1]), bi = r.progress.findIndex((p) => p[0] === 'build' && p[1] === 1);
  must(dl.length > 0 && dl[dl.length - 1] === 1 && r.progress.findIndex((p) => p[0] === 'download' && p[1] === 1) < bi, 'progress ' + JSON.stringify(r.progress));
  must(await memCardSha(main) === SYN.cardSha256, 'card sha256 ' + await memCardSha(main) + ' != ' + SYN.cardSha256);
  const caches_ = await allCaches(main);
  must(JSON.stringify(caches_) === JSON.stringify({ [CACHE]: [KEY(SYN.cardSha256)] }), 'caches ' + JSON.stringify(caches_));
  const idb = await main.evaluate(async () => {
    const dbs = indexedDB.databases ? await indexedDB.databases() : [];
    return dbs.map((d) => d.name);
  });
  must(idb.every((n) => n === '/jnext/warm-start'), 'databases ' + JSON.stringify(idb));
  must(JSON.stringify(r.argv.slice(0, 4)) === JSON.stringify(['--sdcard', '/sdcard/image.img', '--sdcard-sha256', SYN.cardSha256]), 'argv ' + JSON.stringify(r.argv));
});

row('BCARD-02', 'a second visit in the same context makes no request at all to the distribution host; the card comes from the cache with the same sha256; argv as before', async () => {
  const n = D.log.length;
  await open(main, [P48], SYNSEAM(D));
  await click(main);
  await waitReady(main);
  const r = await rec(main);
  must(D.log.length === n, 'requests on the second visit: ' + JSON.stringify(D.log.slice(n)));
  must(r.errors.length === 0 && await memCardSha(main) === SYN.cardSha256, 'errors ' + JSON.stringify(r.errors));
  must(JSON.stringify(r.argv.slice(0, 4)) === JSON.stringify(['--sdcard', '/sdcard/image.img', '--sdcard-sha256', SYN.cardSha256]), 'argv ' + JSON.stringify(r.argv));
  await main.ctx.close();
});

let picked = null;   // BCARD-03 leaves its page for 04
row('BCARD-03', 'auto, no CORS: one HEAD and no GET; the pick panel shows the official link and the three statements; choose is reported once; the player waits', async () => {
  picked = await newPage();
  await open(picked, [P48], SYNSEAM(N));
  await click(picked);
  await waitPanel(picked);
  must(JSON.stringify(kinds(N.log)) === '["HEAD"]', 'requests ' + JSON.stringify(N.log));
  const link = await picked.evaluate(() => { const a = document.querySelector('#p0 .jnext-pick a'); return [a.href, a.textContent, a.target, a.rel]; });
  must(link[0] === url(N, '/synthetic-distro.zip') && link[1] === 'synthetic-distro.zip' && link[2] === '_blank' && /noopener/.test(link[3]), 'link ' + JSON.stringify(link));
  const text = await panelText(picked);
  must(STATEMENTS.every((s) => text.replace(/\s+/g, ' ').includes(s)), 'statements missing in: ' + text);
  const r = await rec(picked);
  must(r.progress.filter((p) => p[0] === 'choose').length === 1 && r.ready === 0 && r.state === 'starting', JSON.stringify(r));
});

row('BCARD-04', 'from BCARD-03: choosing the (synthetic) zip makes the card, equal to the native one, cached under its digest; the player starts; argv as in BCARD-01', async () => {
  await picked.setInputFiles('#p0 .jnext-pick input[type=file]', SYN_ZIP);
  await waitReady(picked);
  const r = await rec(picked);
  must(r.errors.length === 0 && r.state === 'running', JSON.stringify(r.errors) + r.state);
  must(await memCardSha(picked) === SYN.cardSha256, 'card ' + await memCardSha(picked));
  must(JSON.stringify(await cacheKeys(picked)) === JSON.stringify([KEY(SYN.cardSha256)]), 'cache ' + JSON.stringify(await cacheKeys(picked)));
  must(JSON.stringify(r.argv.slice(0, 4)) === JSON.stringify(['--sdcard', '/sdcard/image.img', '--sdcard-sha256', SYN.cardSha256]), 'argv ' + JSON.stringify(r.argv));
  must(await picked.evaluate(() => !document.querySelector('#p0 .jnext-pick')), 'the panel is still there');
  await picked.ctx.close();
});

row('BCARD-05', 'the pick panel refuses a wrong-size file and a same-size wrong file, naming the official file and staying up; then the right one is accepted', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(N));
  await click(p);
  await waitPanel(p);
  const wrongSize = path.join(SCRATCH, 'wrong-size.bin'), wrongBytes = path.join(SCRATCH, 'wrong-bytes.bin');
  fs.writeFileSync(wrongSize, Buffer.alloc(1234, 7));
  fs.writeFileSync(wrongBytes, Buffer.alloc(SYN.size, 7));
  const input = '#p0 .jnext-pick input[type=file]';
  await p.setInputFiles(input, wrongSize);
  await waitFor(p, () => /this file is not/.test(document.querySelector('#p0 .jnext-pick-reason').textContent), null, 60000);
  const a = await lastLine(p);
  must(a.includes('synthetic-distro.zip') && a.includes('1234') && a.includes(String(SYN.size)), 'size refusal: ' + a);
  await p.setInputFiles(input, wrongBytes);
  await waitFor(p, () => /this file is not/.test(document.querySelector('#p0 .jnext-pick-reason').textContent) && !/1234/.test(document.querySelector('#p0 .jnext-pick-reason').textContent), null, 60000);
  const b = await lastLine(p);
  must(b.includes('synthetic-distro.zip') && !b.includes('1234'), 'content refusal: ' + b);
  must(await p.evaluate(() => !!document.querySelector('#p0 .jnext-pick')), 'the panel went away');
  must((await rec(p)).ready === 0, 'ready after a refusal');
  await p.setInputFiles(input, SYN_ZIP);
  await waitReady(p);
  must((await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'the right file was not accepted');
  fs.rmSync(wrongSize); fs.rmSync(wrongBytes);
  await p.ctx.close();
});

row("BCARD-06", "card:{source:'fetch'} with no CORS: onError names the address and Access-Control-Allow-Origin; no panel; the player stops", async () => {
  const p = await newPage();
  await open(p, [{ ...P48, card: { source: 'fetch' } }], SYNSEAM(N));
  await click(p);
  await waitReady(p);
  const r = await rec(p);
  must(r.errors.length === 1 && r.errors[0].includes(url(N, '/synthetic-distro.zip')) && r.errors[0].includes('Access-Control-Allow-Origin'), 'errors ' + JSON.stringify(r.errors));
  must(await p.evaluate(() => !document.querySelector('#p0 .jnext-pick')), 'a panel was shown');
  must(r.state === 'exited' && r.ready === 0, 'state ' + r.state);
  await p.ctx.close();
});

row("BCARD-07", "card:{source:'pick'}: no request of any method to the distribution host; the panel is shown", async () => {
  const p = await newPage();
  const n = D.log.length;
  await open(p, [{ ...P48, card: { source: 'pick' } }], SYNSEAM(D));
  await click(p);
  await waitPanel(p);
  must(D.log.length === n, 'requests ' + JSON.stringify(D.log.slice(n)));
  await p.ctx.close();
});

row('BCARD-08', 'a cached card corrupted in place is dropped: with CORS, one GET and a rebuilt card equal to the native one', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  must((await cacheKeys(p)).length === 1, 'no card cached');
  await p.evaluate(async ([name, key]) => {
    const c = await caches.open(name);
    await c.put(key, new Response(new Uint8Array(1 << 20).fill(9)));   // the same key, other bytes
  }, [CACHE, KEY(SYN.cardSha256)]);
  const n = D.log.length;
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  const gets = D.log.slice(n).filter((q) => q.method === 'GET').length;
  must(gets === 1, 'GETs after the corruption: ' + gets);
  must((await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'the card was not rebuilt');
  await p.ctx.close();
});

row('BCARD-09', 'from another origin (https, not localhost) the development seam is ignored (a console warning, no request to its host); the panel link is exactly the official address', async () => {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const p = await ctx.newPage();
  p.ctx = ctx; p.msgs = [];
  p.on('console', (m) => p.msgs.push(m.text()));
  const aborted = [];
  await ctx.route('https://other.example/**', (route) => {
    const u = new URL(route.request().url());
    const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
    const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
    if (!f || !fs.existsSync(f)) return route.fulfill({ status: 404, body: 'not here' });
    return route.fulfill({ status: 200, contentType: MIMES[path.extname(f)] || 'application/octet-stream', body: fs.readFileSync(f) });
  });
  await ctx.route('https://www.specnext.com/**', (route) => { aborted.push(route.request().method() + ' ' + route.request().url()); route.abort(); });
  const n = D.log.length;
  await open(p, [P48], SYNSEAM(D), 'https://other.example');
  await click(p);
  await waitPanel(p);
  const href = await p.evaluate(() => document.querySelector('#p0 .jnext-pick a').href);
  must(href === DISTRO_URL, 'link ' + href + ' != ' + DISTRO_URL);
  must(p.msgs.some((m) => /honoured only on localhost; ignored/.test(m)), 'no seam warning: ' + JSON.stringify(p.msgs.slice(-5)));
  must(D.log.length === n, 'the seam host was asked: ' + JSON.stringify(D.log.slice(n)));
  must(aborted.some((a) => a.startsWith('HEAD ' + DISTRO_URL)), 'the probe did not go to the official address: ' + JSON.stringify(aborted));
  await ctx.close();
});

row('BCARD-10', 'refused options fail before any request: image, card:{url}, card:{source:x} and a page-supplied $card, each naming the problem', async () => {
  const cases = [
    [{ image: { url: 'u', sha256: 'x' } }, "unknown option 'image'"],
    [{ card: { url: 'x' } }, "option 'card': unknown key 'url'"],
    [{ card: { source: 'x' } }, "option 'card': source must be 'auto', 'fetch' or 'pick'"],
    [{ '$card': { path: '/x', sha256: 'y' } }, "unknown option '$card'"],
  ];
  const n = D.log.length, nA = aLog.length;
  for (const [opt, want] of cases) {
    const p = await newPage();
    await open(p, [{ ...P48, ...opt }], SYNSEAM(D));
    const r = await rec(p);
    must(r.mountError && r.mountError.includes(want), `${JSON.stringify(opt)} -> ${r.mountError}`);
    await p.ctx.close();
  }
  must(D.log.length === n, 'a request reached the distribution host: ' + JSON.stringify(D.log.slice(n)));
});

row('BCARD-11', "the widget with data-card-source='pick': the panel is inside the widget, the status line asks for the file (not an error), and a host page's rules for a, button, input and p do not reach it", async () => {
  const p = await newPage();
  await p.addInitScript((dev) => { window['jnextDev'] = dev; }, SYNSEAM(D));
  await p.addInitScript(() => {
    document.addEventListener('DOMContentLoaded', () => {
      const s = document.createElement('style');
      s.textContent = 'a, button, input, p { color: rgb(255, 0, 0); font-size: 40px; }';
      document.head.appendChild(s);
      document.querySelector('.jnext-widget').setAttribute('data-card-source', 'pick');
    }, { once: true, capture: true });
  });
  await p.goto(AURL + '/build/widget.html');
  await waitFor(p, () => { const e = document.querySelector('.jnext-widget'); return e && e.jnextPlayer; }, null, 120000);
  await p.locator('.jnext-widget .jnext-overlay').click({ timeout: 120000, noWaitAfter: true });
  await waitFor(p, () => !!document.querySelector('.jnext-widget .jnext-pick'), null, 60000);
  const r = await p.evaluate(() => {
    const w = document.querySelector('.jnext-widget'), st = w.querySelector('.jw-status');
    const cs = (e) => { const c = getComputedStyle(e); return [c.color, c.fontSize]; };
    return { status: st.textContent, error: st.className.includes('jw-error'),
             p: cs(w.querySelector('.jnext-pick p')), a: cs(w.querySelector('.jnext-pick a')), b: cs(w.querySelector('.jnext-pick button')),
             inside: !!w.querySelector('.jw-stage .jnext-pick') };
  });
  must(r.inside && /^choose the NextZXOS file/.test(r.status) && !r.error, JSON.stringify(r));
  for (const k of ['p', 'a', 'b']) must(r[k][0] !== 'rgb(255, 0, 0)' && r[k][1] !== '40px', `${k} reached by the host page: ${r[k]}`);
  await p.ctx.close();
});

row('BCARD-12', 'a HEAD that never answers: after probeTimeoutMs the panel appears with a reason that says the check timed out', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(F, { probeTimeoutMs: 1500 }));
  const t0 = Date.now();
  await click(p);
  await waitPanel(p);
  const dt = Date.now() - t0;
  const why = await lastLine(p);
  must(/timed out/.test(why), 'reason: ' + why);
  must(dt >= 1400 && dt < 20000, 'the panel came after ' + dt + ' ms');
  await p.ctx.close();
});

row('BCARD-13', 'REAL data: the wasm builder makes exactly the pinned card from a zip of the real image, and tilemap_demo.nex then warm-starts to the native screenshot (0 pixels)', async () => {
  const p = await newPage();
  const real = { distribution: { url: url(D, '/real-distro.zip'), ...REAL } };   // no cardSha256: the pinned one is enforced
  const opts = { machine: 'next', muted: true, file: '/nex/tilemap_demo.nex',
                 args: ['--rtc', '2026-07-10T08:55:00', '--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', '150'] };
  await open(p, [opts], real);
  const heap0 = await p.evaluate(() => window.apiTest.players[0].player._module.HEAPU8.length);
  const t0 = Date.now();
  await click(p);
  await waitFor(p, () => window.apiTest.players[0].progress.some((q) => q[0] === 'build' && q[1] === 1) || window.apiTest.players[0].errors.length > 0, null, 300000);
  const tBuild = Date.now() - t0;
  const heap1 = await p.evaluate(() => window.apiTest.players[0].player._module.HEAPU8.length);
  await waitReady(p, 0, 600000);
  const r = await rec(p);
  must(r.errors.length === 0, 'errors ' + JSON.stringify(r.errors));
  must(await memCardSha(p) === PINNED_CARD, 'the wasm card ' + await memCardSha(p) + ' != the pinned ' + PINNED_CARD);
  console.log(`  BCARD-13/14 info: build ${tBuild} ms from the click (download, inflate to the boot set, mkfs, hash); heap ${heap0} -> ${heap1}`);
  globalThis.__bcard14 = { heap0, heap1 };
  await waitFor(p, () => { try { return window.apiTest.players[0].player._module.FS.analyzePath('/media/s.png').exists; } catch (e) { return false; } }, null, 300000);
  const b64 = await p.evaluate(() => {
    const d = window.player0._module.FS.readFile('/media/s.png'); let s = '';
    for (let k = 0; k < d.length; k += 8192) s += String.fromCharCode.apply(null, d.subarray(k, k + 8192));
    return btoa(s);
  });
  const util = await (await h.browser.newContext()).newPage();
  await util.goto('about:blank');
  const dec = async (buf) => {
    const o = await util.evaluate(async (b) => {
      const bytes = Uint8Array.from(atob(b), (c) => c.charCodeAt(0));
      const bmp = await createImageBitmap(new Blob([bytes], { type: 'image/png' }), { colorSpaceConversion: 'none' });
      const c = new OffscreenCanvas(bmp.width, bmp.height), g = c.getContext('2d'); g.drawImage(bmp, 0, 0);
      const d = g.getImageData(0, 0, bmp.width, bmp.height).data;
      let s = ''; for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
      return { w: bmp.width, h: bmp.height, b64: btoa(s) };
    }, buf.toString('base64'));
    return { w: o.w, h: o.h, d: Buffer.from(o.b64, 'base64') };
  };
  const got = await dec(Buffer.from(b64, 'base64')), want = await dec(fs.readFileSync(path.join(ORACLE, 'tilemap.png')));
  let bad = got.w !== want.w || got.h !== want.h ? -1 : 0;
  if (bad === 0) for (let i = 0; i < got.d.length; i += 4) if (got.d[i] !== want.d[i] || got.d[i + 1] !== want.d[i + 1] || got.d[i + 2] !== want.d[i + 2]) bad++;
  must(bad === 0, `${bad} pixels differ from the native run`);
  await p.ctx.close();
});

row('BCARD-14', 'memory: the wasm heap grew by at most 16 MiB over the real build (the zip is never held), and the build time is reported', async () => {
  const m = globalThis.__bcard14;
  must(m && m.heap1 - m.heap0 <= 16 * 1048576, `heap ${m && m.heap0} -> ${m && m.heap1}`);
});

row('BCARD-15', 'poster: shown before the click over the black player, replaced by the emulator once it starts', async () => {
  const p = await newPage();
  await open(p, [{ ...P48, poster: '/img/tilemap-demo-reference.png' }], SYNSEAM(D));
  const before = await p.evaluate(async () => {
    const i = document.querySelector('#p0 .jnext-poster');
    if (!i) return null;
    await i.decode().catch(() => {});
    const r = i.getBoundingClientRect(), b = document.querySelector('#p0').getBoundingClientRect();
    return { w: i.naturalWidth, shown: r.width > 0 && r.height > 0 && r.width <= b.width + 1, overlay: document.querySelector('#p0 .jnext-overlay').textContent };
  });
  must(before && before.w > 0 && before.shown && before.overlay === 'Click to start', JSON.stringify(before));
  must(await p.evaluate(() => !!document.querySelector('#p0 .jnext-poster')), 'poster gone');
  await click(p);
  await waitReady(p);
  must((await rec(p)).state === 'running' && await p.evaluate(() => !document.querySelector('#p0 .jnext-poster')), 'the poster is still there after the start');
  await p.ctx.close();
});

row('BCARD-16', 'no poster option: no picture in the player; a poster that cannot be loaded leaves none either', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(D));
  must(await p.evaluate(() => !document.querySelector('#p0 .jnext-poster')), 'a picture without the option');
  await p.close();
  const q = await newPage();
  await open(q, [{ ...P48, poster: '/img/does-not-exist.png' }], SYNSEAM(D));
  await sleep(500);
  must(await q.evaluate(() => !document.querySelector('#p0 .jnext-poster')) && (await rec(q)).mountError === null, 'the broken poster stayed or mount failed');
  await q.ctx.close();
});

row('BCARD-17', 'the widget: data-poster shows the picture before the click; a bad data-card-source is refused in the status line in the error colour', async () => {
  const p = await newPage();
  await p.addInitScript((dev) => { window['jnextDev'] = dev; }, SYNSEAM(D));
  await p.addInitScript(() => {
    document.addEventListener('DOMContentLoaded', () => {
      const e = document.querySelector('.jnext-widget');
      e.setAttribute('data-poster', '/img/tilemap-demo-reference.png');
    }, { once: true, capture: true });
  });
  await p.goto(AURL + '/build/widget.html');
  await waitFor(p, () => { const e = document.querySelector('.jnext-widget'); return e && e.jnextPlayer; }, null, 120000);
  const ok = await p.evaluate(async () => {
    const i = document.querySelector('.jnext-widget .jnext-poster');
    if (i) await i.decode().catch(() => {});
    return !!i && i.naturalWidth > 0;
  });
  must(ok, 'the widget shows no poster');
  const bad = await newPage();
  await bad.addInitScript((dev) => { window['jnextDev'] = dev; }, SYNSEAM(D));
  await bad.addInitScript(() => {
    document.addEventListener('DOMContentLoaded', () => {
      document.querySelector('.jnext-widget').setAttribute('data-card-source', 'x');
    }, { once: true, capture: true });
  });
  await bad.goto(AURL + '/build/widget.html');
  await waitFor(bad, () => document.querySelector('.jnext-widget .jw-status').className.includes('jw-error'), null, 120000);
  const st = await bad.evaluate(() => document.querySelector('.jnext-widget .jw-status').textContent);
  must(st.includes("option 'card': source must be 'auto', 'fetch' or 'pick'"), 'status: ' + st);
  await p.ctx.close(); await bad.ctx.close();
});

row('BCARD-18', 'auto: a download that fails (HTTP 500) after a good probe falls back to the pick panel with the reason on it; no onError; the player waits', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(G));
  await click(p);
  await waitPanel(p);
  must(JSON.stringify(kinds(G.log)) === '["HEAD","GET"]', 'requests ' + JSON.stringify(G.log));
  const why = await lastLine(p), r = await rec(p);
  must(/HTTP 500/.test(why), 'reason: ' + why);
  must(r.errors.length === 0 && r.ready === 0 && r.state === 'starting', JSON.stringify(r));
  await p.ctx.close();
});

row('BCARD-19', 'a HEAD that claims another length than the official file: one HEAD, no GET, the panel at once with the sizes as the reason', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(L));
  await click(p);
  await waitPanel(p);
  must(JSON.stringify(kinds(L.log)) === '["HEAD"]', 'requests ' + JSON.stringify(L.log));
  const why = await lastLine(p);
  must(new RegExp(`${SYN.size + 7} bytes, not the official ${SYN.size}`).test(why), 'reason: ' + why);
  await p.ctx.close();
});

row('BCARD-20', 'the pick panel: a same-size valid zip with another digest is refused as "its SHA-256 differs", naming the official file', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(N));
  await click(p);
  await waitPanel(p);
  const z = Buffer.from(fs.readFileSync(SYN_ZIP));
  z[z.length - 1] ^= 1;   // the end-of-central-directory comment length: the builder never reads it
  const other = path.join(SCRATCH, 'other-digest.zip');
  fs.writeFileSync(other, z);
  await p.setInputFiles('#p0 .jnext-pick input[type=file]', other);
  await waitFor(p, () => /this file is not/.test(document.querySelector('#p0 .jnext-pick-reason').textContent), null, 60000);
  const why = await lastLine(p);
  must(why.includes('synthetic-distro.zip') && why.includes('its SHA-256 differs'), 'refusal: ' + why);
  must(await p.evaluate(() => !!document.querySelector('#p0 .jnext-pick')) && (await rec(p)).ready === 0, 'panel gone or started');
  fs.rmSync(other);
  await p.ctx.close();
});

row('BCARD-21', 'a cache hit removes an older card entry (another digest under the card key host) and keeps entries of other hosts', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  await p.evaluate(async ([name]) => {
    const c = await caches.open(name);
    await c.put('https://card.jnext.invalid/' + 'e'.repeat(64), new Response(new Uint8Array(16)));
    await c.put('https://elsewhere.invalid/keep-me', new Response(new Uint8Array(16)));
  }, [CACHE]);
  const n = D.log.length;
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  must(D.log.length === n, 'the second visit made requests: ' + JSON.stringify(D.log.slice(n)));
  const keys = (await cacheKeys(p)).sort();
  must(JSON.stringify(keys) === JSON.stringify(['https://elsewhere.invalid/keep-me', KEY(SYN.cardSha256)].sort()), 'cache ' + JSON.stringify(keys));
  await p.ctx.close();
});

row('BCARD-26', 'stored form: after a first visit Cache Storage is exactly the v2 cache with the card key (no v1); the entry is gzip, node gunzips it to the card (digest and length), and it is under a tenth of the card', async () => {
  const p = await warm();
  const all = await allCaches(p);
  must(JSON.stringify(all) === JSON.stringify({ [CACHE]: [KEY(SYN.cardSha256)] }), 'caches ' + JSON.stringify(all));
  const e = await entry(p, CACHE, KEY(SYN.cardSha256)), size = fs.statSync(SYN_CARD).size;
  must(e[0] === 0x1f && e[1] === 0x8b && e[2] === 8, 'not gzip: ' + e.subarray(0, 4).toString('hex'));
  const raw = zlib.gunzipSync(e);
  must(sha256(raw) === SYN.cardSha256 && raw.length === size, `gunzipped ${raw.length} bytes, sha ${sha256(raw)}`);
  must(e.length < size / 10, `the entry is ${e.length} bytes of ${size}`);
  console.log(`# BCARD-26 stored ${e.length} bytes for a ${size} byte card`);
  await p.ctx.close();
});

row('BCARD-27', 'a warm hit does not re-hash the card: no request to D, no SubtleCrypto digest over 1 MiB, the card is the native one, argv as before', async () => {
  const p = await newPage();
  await p.ctx.addInitScript(() => {
    window.__bigDigests = 0;
    const orig = SubtleCrypto.prototype.digest;
    SubtleCrypto.prototype.digest = function (alg, data) { if (data && data.byteLength > (1 << 20)) window.__bigDigests++; return orig.call(this, alg, data); };
  });
  await open(p, [P48], SYNSEAM(D));
  await click(p);
  await waitReady(p);
  const n = D.log.length;
  await open(p, [P48], SYNSEAM(D));
  const t0 = Date.now();
  await click(p);
  await waitReady(p);
  const ms = Date.now() - t0;
  const big = await p.evaluate(() => window.__bigDigests);   // before memCardSha, which digests the whole card itself
  const r = await rec(p);
  must(D.log.length === n, 'requests on the warm visit: ' + JSON.stringify(D.log.slice(n)));
  must(big === 0, big + ' large digest(s) on the warm visit');
  must(r.errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'errors ' + JSON.stringify(r.errors));
  must(JSON.stringify(r.argv.slice(0, 4)) === JSON.stringify(['--sdcard', '/sdcard/image.img', '--sdcard-sha256', SYN.cardSha256]), 'argv ' + JSON.stringify(r.argv));
  console.log(`# BCARD-27 warm visit ${ms} ms from the click to ready`);
  await p.ctx.close();
});

// Corrupt the v2 entry of a warm context at `at` (an offset from the end), prove with node that the
// payload is still the card, visit again: one GET, the card made again, the entry valid.
async function corruptAndRevisit(at) {
  const p = await warm();
  const good = await entry(p, CACHE, KEY(SYN.cardSha256));
  const bad = Buffer.from(good);
  bad[bad.length - at] ^= 1;   // RFC 1952: CRC-32 at len-8, ISIZE at len-4, little-endian
  must(good[3] === 0 && sha256(zlib.inflateRawSync(bad.subarray(10, bad.length - 8))) === SYN.cardSha256,
       'the deflate payload is not the card, so the row would not isolate the trailer check');
  let refused = false;
  try { zlib.gunzipSync(bad); } catch (e) { refused = true; }
  must(refused, 'node accepted the corrupted entry');
  await putEntry(p, CACHE, KEY(SYN.cardSha256), bad);
  const log = await revisit(p, D);
  const r = await rec(p);
  must(gets(log) === 1, 'GETs after the corruption: ' + gets(log));
  must(r.errors.length === 0 && r.progress.some((q) => q[0] === 'build' && q[1] === 1), 'errors ' + JSON.stringify(r.errors) + ' progress ' + JSON.stringify(r.progress.slice(-3)));
  must(await memCardSha(p) === SYN.cardSha256, 'the card was not made again');
  must(await entrySha(p) === SYN.cardSha256, 'the entry is not a valid gzip of the card again');
  await p.ctx.close();
}

row('BCARD-28', 'a gzip entry with a wrong CRC-32 but an intact payload is dropped: one GET, the card made again, the entry valid again', async () => {
  await corruptAndRevisit(8);
});

row('BCARD-29', 'a gzip entry with its trailer cut (payload complete) is deleted at once (gone while the pick panel is up), then the picked zip makes the card and stores it again', async () => {
  const p = await warm();
  const good = await entry(p, CACHE, KEY(SYN.cardSha256));
  const cut = good.subarray(0, good.length - 8);
  must(sha256(zlib.inflateRawSync(cut.subarray(10))) === SYN.cardSha256, 'the payload is not complete without the trailer');
  await putEntry(p, CACHE, KEY(SYN.cardSha256), cut);
  const n = N.log.length;
  await open(p, [P48], SYNSEAM(N));
  await click(p);
  await waitPanel(p);
  must(!(await cacheKeys(p)).includes(KEY(SYN.cardSha256)), 'the cut entry is still in the cache');
  const log = N.log.slice(n);
  must(JSON.stringify(kinds(log)) === '["HEAD"]', 'requests ' + JSON.stringify(log));
  await p.setInputFiles('#p0 .jnext-pick input[type=file]', SYN_ZIP);
  await waitReady(p);
  must((await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'the card was not made from the picked file');
  must(await entrySha(p) === SYN.cardSha256, 'the card was not stored again');
  await p.ctx.close();
});

row('BCARD-30', 'a gzip entry with a wrong ISIZE but an intact payload is dropped: one GET, the card made again, the entry valid again', async () => {
  await corruptAndRevisit(4);
});

row('BCARD-31', 'a good card of the old raw cache (v1) is moved to v2 gzipped with no download: the old cache is gone, the card is the native one', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(D));
  const nA = aLog.length;
  await p.evaluate(async ([old, key]) => {
    const r = await fetch('/syn/synthetic-card.img');
    await (await caches.open(old)).put(key, new Response(await r.arrayBuffer()));
  }, [OLD, KEY(SYN.cardSha256)]);
  must(JSON.stringify(aLog.slice(nA)) === '["/syn/synthetic-card.img"]', 'requests to the page host: ' + JSON.stringify(aLog.slice(nA)));
  const log = await revisit(p, D);
  must(log.length === 0, 'requests to D: ' + JSON.stringify(log));
  must(await p.evaluate(async (old) => await caches.has(old), OLD) === false, 'the v1 cache is still there');
  const all = await allCaches(p);
  must(JSON.stringify(all) === JSON.stringify({ [CACHE]: [KEY(SYN.cardSha256)] }), 'caches ' + JSON.stringify(all));
  must(await entrySha(p) === SYN.cardSha256, 'the moved entry is not a gzip of the card');
  must((await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'the card in memory');
  await p.ctx.close();
});

row('BCARD-32', 'a corrupted card in the old cache (v1) is not moved: the old cache is gone, one GET, the card made again and stored in v2', async () => {
  const p = await newPage();
  await open(p, [P48], SYNSEAM(D));
  await p.evaluate(async ([old, key]) => {
    await (await caches.open(old)).put(key, new Response(new Uint8Array(1 << 20).fill(9)));
  }, [OLD, KEY(SYN.cardSha256)]);
  const log = await revisit(p, D);
  must(gets(log) === 1, 'GETs: ' + gets(log));
  must(await p.evaluate(async (old) => await caches.has(old), OLD) === false, 'the v1 cache is still there');
  must(await entrySha(p) === SYN.cardSha256, 'the v2 entry is not a gzip of the card');
  must((await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'the card in memory');
  await p.ctx.close();
});

row('BCARD-33', 'without CompressionStream/DecompressionStream the card is kept raw and hashed on every hit: a raw entry, a warm hit with no request, a corrupted one dropped', async () => {
  const p = await newPage();
  await p.ctx.addInitScript(() => { delete window.CompressionStream; delete window.DecompressionStream; });
  await open(p, [P48], SYNSEAM(D));
  must(await p.evaluate(() => typeof CompressionStream + ' ' + typeof DecompressionStream) === 'undefined undefined', 'the streams are still there');
  await click(p);
  await waitReady(p);
  const size = fs.statSync(SYN_CARD).size;
  const e = await entry(p, CACHE, KEY(SYN.cardSha256));
  must(e.length === size && sha256(e) === SYN.cardSha256 && !(e[0] === 0x1f && e[1] === 0x8b), `the entry is ${e.length} bytes, sha ${sha256(e)}`);
  const log2 = await revisit(p, D);
  must(log2.length === 0 && (await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'visit 2: ' + JSON.stringify(log2));
  await putEntry(p, CACHE, KEY(SYN.cardSha256), Buffer.alloc(1 << 20, 9));
  const log3 = await revisit(p, D);
  must(gets(log3) === 1 && (await rec(p)).errors.length === 0 && await memCardSha(p) === SYN.cardSha256, 'visit 3: GETs ' + gets(log3));
  await p.ctx.close();
});

// ---- run -------------------------------------------------------------------------------------
let pass = 0, fail = 0;
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), Number(E.JNEXT_ROW_TIMEOUT_MS) || 900000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 700)}]`); }
}
for (const x of [A, D.srv, N.srv, F.srv, G.srv, L.srv]) x.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
