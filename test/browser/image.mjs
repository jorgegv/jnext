// Browser rows WBI-PW-01..08: the SD-card image and the warm start in the browser
// (make wasm-browser-test, spec image.mjs). Run by browser-smoke.sh on the SDK's node.
//
// The page fetches the minimal card CROSS-ORIGIN from a stand-in host (the harness answers
// http://img.example from the run's scratch directory, with or without CORS headers), checks
// its SHA-256, keeps it in the Cache API, and persists only the warm-start recording in
// IndexedDB. Oracles: the native run of the same arguments ($SMOKE_ORACLE/tilemap.png), the
// sha256sum of the card and of its zip (computed by browser-smoke.sh at run time), and the
// plan's byte layout (the cache header holds the digest at offset 40).
// The card is copyrighted: it only ever lives in the scratch directory and in the page's
// storage; tracing, video and HAR are never enabled.
import { Harness, pageUrl, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const MODULE = `jnext-${E.SMOKE_BUILD_VERSION}.js`;
const CARD = { url: 'http://img.example/card.img', sha256: E.SMOKE_CARD_SHA256 };
const ZIP = { url: 'http://img.example/card.img.zip', sha256: E.SMOKE_ZIP_SHA256 };
const NEX = ['/nex/tilemap_demo.nex', '/media/tilemap_demo.nex'];
const ARGS = ['--machine', 'next', '--silent', '--rtc', '2026-07-10T08:55:00', '--load', '/media/tilemap_demo.nex',
              '--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', '150'];
const WS_FILE = '/jnext/warm-start/warm-start-m0.jwss';

const h = new Harness({ build: BUILD, page: path.join(ROOT, 'test/browser/page'), web: path.join(ROOT, 'web'),
                        nex: path.join(ROOT, 'test/00regression/nex') });
h.origins['http://img.example'] = { root: SCRATCH, headers: { 'Access-Control-Allow-Origin': '*' } };
// The host with no CORS header is a REAL server (ephemeral port): Chromium does not apply its CORS
// check to a response the harness fulfils, so only a real one proves the page's error.
const noCors = http.createServer((req, res) => {
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length });
  res.end(body);
});
await new Promise((r) => noCors.listen(0, '127.0.0.1', r));
const NOCORS = { url: `http://127.0.0.1:${noCors.address().port}/card.img`, sha256: E.SMOKE_CARD_SHA256 };
h.origins[`http://127.0.0.1:${noCors.address().port}`] = { passthrough: true };
await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };
const imgReqs = () => h.originRequests.filter((r) => r.url.startsWith('http://img.example/'));

// ---- helpers -------------------------------------------------------------------------------
function attach(page) {
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
}
// Load the fixture page for `cfg`; resolves when it is ready or has failed. Returns the page.
async function visit(page, cfg) {
  await page.goto(pageUrl({ module: MODULE, canvas: '640x512', ...cfg }));
  await waitFor(page, () => window.jnextTest.ready || window.jnextTest.error);
}
const err = (page) => page.evaluate(() => window.jnextTest.error);
const start = (page) => page.click('#overlay', { timeout: 600000, noWaitAfter: true });
const stderr = (page) => page.evaluate(() => window.jnextTest.stderr);
const progress = (page) => page.evaluate(() => window.jnextTest.progress);
const waitLog = (page, re, ms = 300000) =>
  waitFor(page, (src) => window.jnextTest.stderr.some((l) => new RegExp(src).test(l)), re.source, ms);
async function grab(page, p) {
  const b64 = await page.evaluate((p) => {
    const d = window.jnextModule.FS.readFile(p); let s = '';
    for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
    return btoa(s);
  }, p);
  return Buffer.from(b64, 'base64');
}
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
function diff(a, b) {
  if (a.w !== b.w || a.h !== b.h) return `size ${a.w}x${a.h} vs ${b.w}x${b.h}`;
  let n = 0;
  for (let i = 0; i < a.d.length; i += 4) if (a.d[i] !== b.d[i] || a.d[i + 1] !== b.d[i + 1] || a.d[i + 2] !== b.d[i + 2]) n++;
  return n;
}
async function shotMatches(page) {
  const got = await decode(await grab(page, '/media/s.png'));
  const want = await decode(fs.readFileSync(path.join(ORACLE, 'tilemap.png')));
  const d = diff(got, want);
  must(d === 0, `${d} differing pixels vs the native run`);
}
// One run of the tilemap demo to its screenshot. Returns the ms from the click to the screenshot.
async function runToShot(page) {
  const t0 = Date.now();
  await start(page);
  await waitLog(page, /screenshot saved/);
  return Date.now() - t0;
}
const warmFractions = (prog) => prog.filter((p) => p[0] === 'warm-start').map((p) => p[1]);
const tickGaps = (prog) => {
  const t = prog.filter((p) => p[0] === 'warm-start').map((p) => p[2]);
  const g = []; for (let i = 1; i < t.length; i++) g.push(t[i] - t[i - 1]);
  return g;
};
const cfg = (image) => ({ args: ARGS, files: [NEX], image, idbfs: true });
let mainPage = null;   // the context rows 01, 02, 06, 07 and 08 share: IndexedDB and the Cache API persist in it
const IDB_DUMP = () => new Promise((res, rej) => {
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
row('WBI-PW-01', 'first visit: one cross-origin image request, download and warm-start progress, recorded with 0 boot writes, screen equals the native run', async () => {
  mainPage = await h.newPage(); attach(mainPage);
  await visit(mainPage, cfg(CARD));
  must(!(await err(mainPage)), 'page error: ' + (await err(mainPage)));
  const ms = await runToShot(mainPage);
  const log = (await stderr(mainPage)).join('\n');
  const prog = await progress(mainPage);
  const dl = prog.filter((p) => p[0] === 'download').map((p) => p[1]);
  const frac = warmFractions(prog);
  const distinct = new Set(frac);
  const inc = frac.every((f, i) => i === 0 || f >= frac[i - 1]);
  const gaps = tickGaps(prog).sort((a, b) => a - b);
  console.log(`# WBI-PW-01 first visit ${ms} ms to the screenshot; ${frac.length} warm-start updates, ` +
              `tick gap median ${gaps[gaps.length >> 1] | 0} ms max ${gaps[gaps.length - 1] | 0} ms`);
  must(imgReqs().length === 1 && imgReqs()[0].origin === 'http://localhost', 'image requests: ' + JSON.stringify(imgReqs()));
  must(dl.includes(1), 'no download progress to 1');
  must(distinct.size >= 10 && inc && frac[frac.length - 1] === 1, `warm-start fractions: ${distinct.size} distinct, increasing=${inc}, last=${frac[frac.length - 1]}`);
  must(/recorded a NextZXOS-resident machine \(.*the boot wrote 0 sector\(s\)/.test(log), 'no recorded line with 0 boot writes');
  must(/NextZXOS is resident; the program is applied/.test(log), 'no resident line');
  await shotMatches(mainPage);
});

row('WBI-PW-02', 'second visit (reload): no image request, restore reported, no warm-start progress below 1, screen equals the native run', async () => {
  const before = imgReqs().length;
  await visit(mainPage, cfg(CARD));
  must(!(await err(mainPage)), 'page error: ' + (await err(mainPage)));
  const ms = await runToShot(mainPage);
  const log = (await stderr(mainPage)).join('\n');
  const prog = await progress(mainPage);
  console.log(`# WBI-PW-02 second visit ${ms} ms to the screenshot`);
  must(imgReqs().length === before, `${imgReqs().length - before} image request(s) on the second visit`);
  must(prog.some((p) => p[0] === 'restore'), 'no restore progress');
  must(!warmFractions(prog).some((f) => f < 1), 'warm-start progress below 1 on a restore');
  must(/restored a recorded NextZXOS machine/.test(log) && !/cold-booting/.test(log), 'log: ' + log.slice(0, 200));
  await shotMatches(mainPage);
});

row('WBI-PW-03', 'zipped image: extracted, recorded, keyed on the zip digest, and the extracted card is gone at exit', async () => {
  const page = await h.newPage(); attach(page);
  await visit(page, { args: ['--machine', 'next', '--silent', '--rtc', '2026-07-10T08:55:00', '--load', '/media/tilemap_demo.nex',
                              '--delayed-automatic-exit-frames', '400'],
                      files: [NEX], image: ZIP, idbfs: true });
  must(!(await err(page)), 'page error: ' + (await err(page)));
  await start(page);
  await waitFor(page, () => window.jnextTest.exit !== null || window.jnextTest.error, null, 300000);
  const log = (await stderr(page)).join('\n');
  must(/using minimal\.img from \/sdcard\/image\.zip/.test(log), 'no extraction line: ' + log.slice(0, 300));
  const hdr = await grab(page, WS_FILE);
  const got = hdr.subarray(40, 72).toString('hex');
  must(got === E.SMOKE_ZIP_SHA256, `header digest ${got} != the zip's ${E.SMOKE_ZIP_SHA256}`);
  const left = await page.evaluate(() => {
    const FS = window.jnextModule.FS, found = [];
    const walk = (d) => { let n; try { n = FS.readdir(d); } catch (e) { return; }
      for (const e of n) { if (e === '.' || e === '..') continue; const p = d + '/' + e; const st = FS.stat(p);
        if (FS.isDir(st.mode)) walk(p); else if (/\.(img|zip)$/i.test(p)) found.push(p + ' ' + st.size); } };
    walk('/jnext'); walk('/sdcard'); walk('/tmp');
    return found;
  });
  must(left.length === 0, 'card copies left in memory after exit: ' + JSON.stringify(left));
  await page.ctx.close();
});

row('WBI-PW-04', 'a wrong sha256 is refused with both digests, callMain never runs and nothing is cached', async () => {
  const page = await h.newPage(); attach(page);
  const wrong = '0123456789abcdef'.repeat(4);
  await visit(page, { args: ARGS, files: [NEX], image: { url: CARD.url, sha256: wrong }, idbfs: true });
  const e = await err(page);
  const started = await page.evaluate(() => window.jnextTest.started);
  const cached = await page.evaluate(async (u) => { const c = await caches.open('jnext-sdcard-v1'); return (await c.keys()).map((r) => r.url).includes(u); }, CARD.url);
  must(/SHA-256 mismatch/.test(e || '') && e.includes(wrong) && e.includes(E.SMOKE_CARD_SHA256), 'error: ' + e);
  must(!started && !(await stderr(page)).some((l) => /SD card image mounted/.test(l)), 'the module ran');
  must(!cached, 'the refused image was cached');
  await page.ctx.close();
});

row('WBI-PW-05', 'an image host without CORS is refused naming the URL and Access-Control-Allow-Origin; nothing starts', async () => {
  const page = await h.newPage(); attach(page);
  await visit(page, { args: ARGS, files: [NEX], image: NOCORS, idbfs: true });
  const e = await err(page);
  const started = await page.evaluate(() => window.jnextTest.started);
  must(e && e.includes(NOCORS.url) && /Access-Control-Allow-Origin/.test(e), 'error: ' + e);
  must(!started, 'the module ran');
  await page.ctx.close();
});

row('WBI-PW-06', 'a corrupted image cache entry is dropped: one network request, restore from IndexedDB, entry re-hashes right', async () => {
  await mainPage.evaluate(async (u) => {
    const c = await caches.open('jnext-sdcard-v1');
    await c.put(u, new Response(new Uint8Array(1000)));
  }, CARD.url);
  const before = imgReqs().length;
  await visit(mainPage, cfg(CARD));
  must(!(await err(mainPage)), 'page error: ' + (await err(mainPage)));
  await runToShot(mainPage);
  const log = (await stderr(mainPage)).join('\n');
  const after = await mainPage.evaluate(async (u) => {
    const c = await caches.open('jnext-sdcard-v1'); const r = await c.match(u);
    const d = await crypto.subtle.digest('SHA-256', await r.arrayBuffer());
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('');
  }, CARD.url);
  must(imgReqs().length === before + 1, `${imgReqs().length - before} image requests`);
  must(/restored a recorded NextZXOS machine/.test(log), 'not restored');
  must(after === E.SMOKE_CARD_SHA256, 'cache entry hash ' + after);
});

row('WBI-PW-07', 'what persists: IndexedDB holds exactly the warm-start file, Cache Storage exactly the image, the card is in neither as a written copy', async () => {
  const idb = await mainPage.evaluate(IDB_DUMP);
  const files = idb.filter((e) => e.bytes >= 0);
  const cache = await mainPage.evaluate(async () => {
    const out = [];
    for (const n of await caches.keys()) { const c = await caches.open(n); for (const r of await c.keys()) out.push(n + ' ' + r.url); }
    return out;
  });
  must(files.length === 1 && files[0].key === WS_FILE, 'IndexedDB files: ' + JSON.stringify(idb));
  must(files[0].bytes < 1024 * 1024, 'the IndexedDB file is ' + files[0].bytes + ' bytes: a card copy?');
  must(cache.length === 1 && cache[0] === 'jnext-sdcard-v1 ' + CARD.url, 'Cache Storage: ' + JSON.stringify(cache));
  const dbs = await mainPage.evaluate(async () => (await indexedDB.databases()).map((d) => d.name));
  must(dbs.every((n) => n === '/jnext/warm-start'), 'IndexedDB databases: ' + JSON.stringify(dbs));
});

row('WBI-PW-08', 'a corrupted recording is refused and re-recorded, and the next visit restores', async () => {
  await visit(mainPage, cfg(CARD));   // ready, not started: the store is mounted and populated
  const len = await mainPage.evaluate(async () => {
    const FS = window.jnextModule.FS, n = FS.readFile('/jnext/warm-start/warm-start-m0.jwss').length;
    const g = new Uint8Array(n); for (let i = 0; i < n; i++) g[i] = (i * 31 + 7) & 255;
    FS.writeFile('/jnext/warm-start/warm-start-m0.jwss', g);
    await new Promise((res, rej) => FS.syncfs(false, (e) => (e ? rej(e) : res())));
    return n;
  });
  must(len > 1000, 'the warm-start file was missing before the corruption');
  await visit(mainPage, cfg(CARD));
  await runToShot(mainPage);
  const log = (await stderr(mainPage)).join('\n');
  must(/is not a jnext warm-start file|does not inflate/.test(log), 'no refusal line');
  must(/recorded a NextZXOS-resident machine/.test(log), 'not re-recorded');
  await sleep(500);
  await visit(mainPage, cfg(CARD));
  await runToShot(mainPage);
  must(/restored a recorded NextZXOS machine/.test((await stderr(mainPage)).join('\n')), 'the next visit did not restore');
  await shotMatches(mainPage);
});

// ---- run -------------------------------------------------------------------------------------
let pass = 0, fail = 0;
for (const r of rows) {
  try { await r.fn(); pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
noCors.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
