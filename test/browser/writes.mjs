// Browser rows WBW-01..17: the guest's writes to the SD card kept across page loads (keepWrites,
// resetCard()), in headless Chromium (make wasm-browser-test, spec writes.mjs). Run by
// browser-smoke.sh on the SDK's node.
//
// Guest: test/fixtures/nex/save_counter.nex (source demo/save_counter): it keeps a one-byte run
// counter in raw sectors 60000 and 60001 of the card (two writes in a row), over the SPI ports (an esxDOS file would be answered by
// JNEXT's stand-in and never reach the card), and prints "SAVE <n> OK" on the magic port, which reaches the
// page's console. WBW-13..17 use test/fixtures/nex/esx_save_counter.nex (source demo/esx_save_counter), whose
// counter is a file saved through esxDOS (the stand-in's file, kept in the journal), and its variant
// esx_save_counter_b.nex (another constant byte: a changed NEX), which print "ESX <n> OK". The page is the API fixture (page/api.html, mounted with the options a row gives
// it); the card comes from a real server on 127.0.0.1 with CORS, as in api.mjs.
//
// Oracles: the NATIVE run of browser-smoke.sh (the same program, twice, on a clone of the card with
// --sdcard-writes): its journal after each run (w1.jsdw, w2.jsdw) is what the page's stored record
// must equal byte for byte, and its magic-port lines are what the page must print. The key of a
// record is the image's SHA-256 and the first 16 hex digits of the SHA-256 of the page's path,
// computed here with node's own crypto. The card is copyrighted: it lives in the scratch directory
// and in the page's storage only; a journal holds only the sectors the guest wrote. Tracing, video
// and HAR are never enabled.
import { Harness, waitFor, sleep } from './harness.mjs';
import crypto from 'crypto';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256, ZIPSHA = E.SMOKE_ZIP_SHA256;
const W1 = fs.readFileSync(path.join(ORACLE, 'w1.jsdw')), W2 = fs.readFileSync(path.join(ORACLE, 'w2.jsdw'));
const E1 = fs.readFileSync(path.join(ORACLE, 'e1.jsdw')), E2 = fs.readFileSync(path.join(ORACLE, 'e2.jsdw')),
      E3 = fs.readFileSync(path.join(ORACLE, 'e3.jsdw')), E4 = fs.readFileSync(path.join(ORACLE, 'e4.jsdw'));

const h = new Harness({});
const bLog = [];
const B = http.createServer((req, res) => {
  bLog.push(req.url);
  const f = { '/card.img': path.join(SCRATCH, 'card.img'), '/card.zip': path.join(SCRATCH, 'card.img.zip') }[req.url];
  if (!f) { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
// `page` and `page2` are the same directory under two paths: two pages of one site.
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), page2: path.join(ROOT, 'test/browser/page'),
                fixture: path.join(ROOT, 'test/fixtures/nex') };
const aLog = [];
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  aLog.push(u.pathname);
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
const ZIPIMAGE = { url: BURL + '/card.zip', sha256: ZIPSHA };

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

const pageKey = (pathname) => crypto.createHash('sha256').update(pathname).digest('hex').slice(0, 16);
const keyOf = (sha, pathname) => sha + '-' + pageKey(pathname);

// The guest: a NEX on the next, quiet, with the clock pinned as in the native oracle and the magic
// port on. `extra` is merged over the options.
const GUEST = (extra = {}) => Object.assign({
  file: '/fixture/save_counter.nex', machine: 'next', muted: true,
  args: ['--rtc', '2026-07-10T08:55:00', '--magic-port', '0xCAFE', '--magic-port-mode', 'line'],
}, extra);

async function newPage() {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx;
  page.msgs = [];
  page.reqs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  page.on('request', (r) => page.reqs.push({ url: r.url(), method: r.method() }));
  return page;
}
// Load the API fixture (at `where`, default /page/) with the players; wait until every mount() settled.
async function open(page, players, { where = 'page', image = IMAGE } = {}) {
  page.msgs.length = 0;
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(image), players: JSON.stringify(players) });
  await page.goto(`${AURL}/${where}/api.html?` + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.loaded && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled),
                players.length);
}
const rec = (page, i = 0) => page.evaluate((i) => {
  const r = window.apiTest.players[i];
  return { ready: r.ready, errors: r.errors, mountError: r.mountError, argv: r.player ? r.player.argv : null };
}, i);
const click = (page, i = 0) => page.click(`#p${i} .jnext-overlay`, { timeout: 600000, noWaitAfter: true });
async function waitMsg(page, text, ms = 400000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    if (page.msgs.some((m) => m.trim() === text)) return;
    const errs = await page.evaluate(() => window.apiTest.players.flatMap((p) => p.errors));
    if (errs.length) throw new Error('onError before ' + text + ': ' + errs.join(' | ').slice(0, 300));
    await sleep(50);
  }
  throw new Error('no "' + text + '" within ' + ms + ' ms; saw ' + JSON.stringify(page.msgs.filter((m) => /SAVE|ESX/.test(m))));
}
// A visit: open, click, wait for the guest's line; the player stays mounted.
async function visit(page, players, line, opts) {
  await open(page, players, opts);
  const r = await rec(page);
  must(!r.mountError, 'mount: ' + r.mountError);
  await click(page);
  await waitMsg(page, line);
}
// Every record of the keepWrites store: { key: Buffer }. null when the database does not exist (it is not created).
async function stored(page) {
  const b64 = await page.evaluate(async () => {
    const names = (await indexedDB.databases()).map((d) => d.name);
    if (!names.includes('jnext-card-writes')) return null;
    const db = await new Promise((res, rej) => { const q = indexedDB.open('jnext-card-writes'); q.onsuccess = () => res(q.result); q.onerror = () => rej(q.error); });
    if (!db.objectStoreNames.contains('journals')) { db.close(); return {}; }
    const out = await new Promise((res, rej) => {
      const tx = db.transaction('journals', 'readonly'), st = tx.objectStore('journals');
      const k = st.getAllKeys(), v = st.getAll();
      tx.oncomplete = () => res(k.result.map((key, i) => [key, v.result[i]]));
      tx.onerror = () => rej(tx.error);
    });
    db.close();
    const o = {};
    for (const [key, val] of out) {
      const bytes = val instanceof Uint8Array ? val : new Uint8Array(val);
      let s = ''; for (let i = 0; i < bytes.length; i += 8192) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
      o[key] = btoa(s);
    }
    return o;
  });
  if (b64 === null) return null;
  return Object.fromEntries(Object.entries(b64).map(([k, v]) => [k, Buffer.from(v, 'base64')]));
}
async function storedWithin(page, ms, ok) {
  const t0 = Date.now();
  let s = null;
  while (Date.now() - t0 < ms) {
    s = await stored(page);
    if (s && ok(s)) return s;
    await sleep(100);
  }
  return s;
}
const sameBytes = (a, b) => a && b && Buffer.compare(a, b) === 0;
const desc = (s) => s === null ? 'no database' : JSON.stringify(Object.fromEntries(Object.entries(s).map(([k, v]) => [k, v.length])));
// Slow the store of the writes down by `ms`, in one of two ways. 'resolve': the store happens at once and
// reads the file at the call, as it does unslowed, but it stays "in flight" for `ms` more, so the writes
// the guest makes meanwhile can only be stored by the player's second pass. 'write': the store itself
// happens `ms` late, so a destroy() that does not wait for it finds nothing stored.
const slowPersist = (page, ms, mode) => page.evaluate(([ms, mode]) => {
  const orig = JnextImage.saveCardWrites;
  JnextImage.saveCardWrites = mode === 'resolve'
    ? (M, key) => orig(M, key).then((v) => new Promise((r) => setTimeout(() => r(v), ms)))
    : (M, key) => new Promise((r) => setTimeout(r, ms)).then(() => orig(M, key));
}, [ms, mode]);

const K1 = keyOf(SHA, '/page/api.html');
let main = null;   // rows 01-05, 07, 09 share one context: its IndexedDB is the visitor's browser

row('WBW-01', 'first visit with keepWrites: SAVE 1 OK; the stored record (key image digest + page) is byte-identical to the native journal; the cache holds only the image; nothing leaves the page but the module, the program and the image', async () => {
  main = await newPage();
  await open(main, [GUEST({ keepWrites: true })]);
  await slowPersist(main, 150, 'resolve');
  const r = await rec(main);
  must(!r.mountError, 'mount: ' + r.mountError);
  must(r.argv.includes('--sdcard-writes'), 'argv ' + JSON.stringify(r.argv));
  await click(main);
  await waitMsg(main, 'SAVE 1 OK');
  const s = await storedWithin(main, 5000, (s) => Object.keys(s).length === 1 && sameBytes(s[K1], W1));
  must(s && Object.keys(s).length === 1 && Object.keys(s)[0] === K1, 'records ' + desc(s) + ' want ' + K1);
  must(sameBytes(s[K1], W1), `the record (${s[K1].length} bytes) differs from the native journal (${W1.length} bytes)`);
  const cache = await main.evaluate(async () => {
    const names = await caches.keys();
    const out = {};
    for (const n of names) {
      const c = await caches.open(n);
      out[n] = [];
      for (const q of await c.keys()) {
        const buf = await (await c.match(q)).arrayBuffer();
        const d = await crypto.subtle.digest('SHA-256', buf);
        out[n].push([q.url, [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('')]);
      }
    }
    return out;
  });
  must(JSON.stringify(Object.keys(cache)) === '["jnext-sdcard-v1"]' && cache['jnext-sdcard-v1'].length === 1 &&
       cache['jnext-sdcard-v1'][0][1] === SHA, 'cache ' + JSON.stringify(cache));
  const stray = main.reqs.filter((q) => {
    const u = new URL(q.url);
    const ok = (u.origin === AURL && /^\/(build|page|fixture)\//.test(u.pathname)) || q.url === IMAGE.url || u.pathname === '/favicon.ico';
    return !ok || q.method !== 'GET';
  });
  must(stray.length === 0, 'unexpected requests ' + JSON.stringify(stray));
});

row('WBW-02', 'reload: SAVE 2 OK; the stored record equals the native journal after the second run; the warm start was recorded again (the card changed)', async () => {
  const t0 = Date.now();
  await open(main, [GUEST({ keepWrites: true })]);
  await click(main);
  await waitMsg(main, 'SAVE 2 OK');
  const ms = Date.now() - t0;
  const s = await storedWithin(main, 5000, (s) => sameBytes(s[K1], W2));
  must(s && Object.keys(s).length === 1 && sameBytes(s[K1], W2), `the record after the reload: ${desc(s)}; native ${W2.length} bytes`);
  must(main.msgs.some((m) => /recorded a NextZXOS-resident machine/.test(m)), 'no fresh warm-start recording in the log');
  console.log(`# WBW-02: second visit, click to "SAVE 2 OK" (warm start recorded again): ${ms} ms`);
});

row('WBW-03', 'resetCard() resolves at the next ready; the program then says SAVE 1 OK and the stored record equals the native journal after the first run', async () => {
  main.msgs.length = 0;
  const r = await main.evaluate(() => window.player0.resetCard().then(() => 'ready', (e) => 'rejected ' + e.message));
  must(r === 'ready', 'resetCard(): ' + r);
  await waitMsg(main, 'SAVE 1 OK');
  const s = await storedWithin(main, 5000, (s) => sameBytes(s[K1], W1));
  must(s && Object.keys(s).length === 1 && sameBytes(s[K1], W1), `the record after resetCard(): ${desc(s)}`);
});

row('WBW-04', 'the same page with another image (the zipped card, another digest): SAVE 1 OK; the first image\'s record is untouched', async () => {
  const before = (await stored(main))[K1];
  await visit(main, [GUEST({ keepWrites: true })], 'SAVE 1 OK', { image: ZIPIMAGE });
  const K2 = keyOf(ZIPSHA, '/page/api.html');
  const s = await storedWithin(main, 5000, (s) => Object.keys(s).length === 2);
  must(s && Object.keys(s).sort().join() === [K1, K2].sort().join(), 'records ' + desc(s) + ' want ' + K1 + ', ' + K2);
  must(sameBytes(s[K1], before), 'the first image\'s record changed');
  must(s[K2].length > 0, 'no record for the zipped image');
});

row('WBW-05', 'another page path on the same site, same image: SAVE 1 OK (its own saves); a record per (image, page)', async () => {
  await visit(main, [GUEST({ keepWrites: true })], 'SAVE 1 OK', { where: 'page2' });
  const K3 = keyOf(SHA, '/page2/api.html');
  const s = await storedWithin(main, 5000, (s) => Object.keys(s).length === 3 && sameBytes(s[K3], W1));
  must(s && K3 !== K1 && Object.keys(s).includes(K3) && Object.keys(s).includes(K1), 'records ' + desc(s) + ' want ' + K3);
  must(sameBytes(s[K3], W1), 'the second page\'s record is not the first-run journal');
});

row('WBW-06', 'without keepWrites: SAVE 1 OK on two visits (nothing kept), and the browser has no jnext-card-writes database', async () => {
  const page = await newPage();
  await visit(page, [GUEST()], 'SAVE 1 OK');
  await visit(page, [GUEST()], 'SAVE 1 OK');
  const names = await page.evaluate(async () => (await indexedDB.databases()).map((d) => d.name));
  await page.ctx.close();
  must(!names.includes('jnext-card-writes'), 'databases ' + JSON.stringify(names));
  must(names.includes('/jnext/warm-start'), 'databases ' + JSON.stringify(names));
});

row('WBW-07', 'keepWrites with args --sdcard-writes-reset on a card with saves: SAVE 1 OK, and the record is the first-run journal again', async () => {
  await visit(main, [GUEST({ keepWrites: true })], 'SAVE 2 OK');   // K1 held W1: this visit saves a second time
  await visit(main, [GUEST({ keepWrites: true, args: ['--rtc', '2026-07-10T08:55:00', '--magic-port', '0xCAFE',
                                                    '--magic-port-mode', 'line', '--sdcard-writes-reset'] })], 'SAVE 1 OK');
  const s = await storedWithin(main, 5000, (s) => sameBytes(s[K1], W1));
  must(s && sameBytes(s[K1], W1), `the record after the reset: ${desc(s)}`);
});

row('WBW-08', 'keepWrites:\'yes\' rejects mount() with a TypeError naming keepWrites and fetches nothing; resetCard() without keepWrites rejects naming it', async () => {
  const page = await newPage();
  await open(page, []);
  const before = page.reqs.length;
  const r = await page.evaluate(() => {
    const el = document.createElement('div'); document.body.appendChild(el);
    return jnext.mount(el, { image: { url: 'http://127.0.0.1:1/x.img', sha256: 'a'.repeat(64) }, keepWrites: 'yes' })
      .then(() => 'resolved', (e) => (e instanceof TypeError ? 'TypeError: ' : 'other: ') + e.message);
  });
  const fetched = page.reqs.slice(before).filter((q) => !q.url.startsWith(AURL));
  must(/^TypeError: .*keepWrites/.test(r), 'mount: ' + r);
  must(fetched.length === 0, 'requests ' + JSON.stringify(fetched));
  await visit(page, [GUEST()], 'SAVE 1 OK');
  const rc = await page.evaluate(() => window.player0.resetCard().then(() => 'resolved', (e) => e.message));
  await page.ctx.close();
  must(/keepWrites/.test(rc) && rc !== 'resolved', 'resetCard(): ' + rc);
});

row('WBW-09', 'a stored record made for another image (header digest changed) is refused: onError names --sdcard-writes-reset; a mount with that argument recovers to SAVE 1 OK', async () => {
  await main.evaluate(async (key) => {
    const db = await new Promise((res, rej) => { const q = indexedDB.open('jnext-card-writes'); q.onsuccess = () => res(q.result); q.onerror = () => rej(q.error); });
    await new Promise((res, rej) => {
      const tx = db.transaction('journals', 'readwrite'), st = tx.objectStore('journals');
      const g = st.get(key);
      g.onsuccess = () => { const b = new Uint8Array(g.result); b[32] ^= 0xff; st.put(b, key); };
      tx.oncomplete = res; tx.onerror = () => rej(tx.error);
    });
    db.close();
  }, K1);
  await open(main, [GUEST({ keepWrites: true })]);
  await click(main);
  await waitFor(main, () => window.apiTest.players[0].errors.length > 0, null, 120000);
  const r = await rec(main);
  must(r.errors.some((m) => /--sdcard-writes-reset/.test(m)), 'errors ' + JSON.stringify(r.errors));
  await visit(main, [GUEST({ keepWrites: true, args: ['--rtc', '2026-07-10T08:55:00', '--magic-port', '0xCAFE',
                                                    '--magic-port-mode', 'line', '--sdcard-writes-reset'] })], 'SAVE 1 OK');
  const s = await storedWithin(main, 5000, (s) => sameBytes(s[K1], W1));
  must(s && sameBytes(s[K1], W1), `the record after the recovery: ${desc(s)}`);
  await main.ctx.close();
});

row('WBW-10', 'destroy() right after the guest saved stores the writes before it resolves: the record is there the moment it does, and a new visit says SAVE 2 OK', async () => {
  const page = await newPage();
  await open(page, [GUEST({ keepWrites: true })]);
  await slowPersist(page, 400, 'write');   // the persist the save started is still in flight when destroy() is called
  await click(page);
  await waitMsg(page, 'SAVE 1 OK');
  await page.evaluate(() => window.player0.destroy());
  const s = await stored(page);
  must(s && sameBytes(s[K1], W1), `the record at the moment destroy() resolved: ${desc(s)}`);
  await visit(page, [GUEST({ keepWrites: true })], 'SAVE 2 OK');
  await page.ctx.close();
});

row('WBW-11', 'a --sdcard-writes-reset start that writes nothing (no program: the NextZXOS menu) still deletes the stored record', async () => {
  const page = await newPage();
  await visit(page, [GUEST({ keepWrites: true })], 'SAVE 1 OK');
  const s1 = await storedWithin(page, 5000, (s) => sameBytes(s[K1], W1));
  must(s1 && sameBytes(s1[K1], W1), 'no record to start from: ' + desc(s1));
  await open(page, [{ machine: 'next', muted: true, keepWrites: true, args: ['--sdcard-writes-reset'] }]);
  must(!(await rec(page)).mountError, 'mount: ' + (await rec(page)).mountError);
  await click(page);
  await waitFor(page, () => window.apiTest.players[0].ready >= 1 || window.apiTest.players[0].errors.length > 0, null, 300000);
  const s2 = await storedWithin(page, 5000, (s) => !(K1 in s));
  await page.ctx.close();
  must(s2 && !(K1 in s2), 'the record is still there after the reset start: ' + desc(s2));
});

row('WBW-12', 'keepWrites:false is the documented boolean: mount() resolves, the command line has no --sdcard-writes, and no jnext-card-writes database appears', async () => {
  const page = await newPage();
  await open(page, [GUEST({ keepWrites: false })]);
  const r = await rec(page);
  must(!r.mountError, 'mount: ' + r.mountError);
  must(r.argv && !r.argv.includes('--sdcard-writes'), 'argv ' + JSON.stringify(r.argv));
  const names = await page.evaluate(async () => (await indexedDB.databases()).map((d) => d.name));
  await page.ctx.close();
  must(!names.includes('jnext-card-writes'), 'databases ' + JSON.stringify(names));
});

// WBW-13..17: the file the esxDOS stand-in keeps for a directly loaded NEX. The oracle is the native
// journal after each run (e1, e2, e4 of browser-smoke.sh); the record is keyed by the card and the page,
// never by the program.
const ESXGUEST = (extra = {}) => GUEST(Object.assign({ file: '/fixture/esx_save_counter.nex', keepWrites: true }, extra));
let esx = null;

row('WBW-13', 'a directly loaded NEX saving through esxDOS, first visit with keepWrites: ESX 1 OK; within 5 s the stored record is byte-identical to the native journal after its first run', async () => {
  esx = await newPage();
  await visit(esx, [ESXGUEST()], 'ESX 1 OK');
  const s = await storedWithin(esx, 5000, (s) => Object.keys(s).length === 1 && sameBytes(s[K1], E1));
  must(s && Object.keys(s).length === 1 && sameBytes(s[K1], E1), `the record (${desc(s)}) differs from the native journal (${E1.length} bytes)`);
});

row('WBW-14', 'reload: ESX 2 OK (the save survived); the record equals the native journal after the second run; the warm start was restored, not recorded again', async () => {
  await visit(esx, [ESXGUEST()], 'ESX 2 OK');
  const s = await storedWithin(esx, 5000, (s) => sameBytes(s[K1], E2));
  must(s && Object.keys(s).length === 1 && sameBytes(s[K1], E2), `the record after the reload: ${desc(s)}; native ${E2.length} bytes`);
  must(esx.msgs.some((m) => /restored a recorded NextZXOS machine/.test(m)), 'the warm start was not restored');
  must(!esx.msgs.some((m) => /recorded a NextZXOS-resident machine/.test(m)), 'the warm start was recorded again');
});

row('WBW-15', 'resetCard() clears the stand-in\'s file too: it resolves at the next ready, the program says ESX 1 OK, and the record equals the native journal after the first run', async () => {
  esx.msgs.length = 0;
  const r = await esx.evaluate(() => window.player0.resetCard().then(() => 'ready', (e) => 'rejected ' + e.message));
  must(r === 'ready', 'resetCard(): ' + r);
  await waitMsg(esx, 'ESX 1 OK');
  const s = await storedWithin(esx, 5000, (s) => sameBytes(s[K1], E1));
  await esx.ctx.close();
  must(s && Object.keys(s).length === 1 && sameBytes(s[K1], E1), `the record after resetCard(): ${desc(s)}`);
});

row('WBW-16', 'without keepWrites the stand-in\'s file lives in memory only: ESX 1 OK on two visits, and the browser has no jnext-card-writes database', async () => {
  const page = await newPage();
  await visit(page, [ESXGUEST({ keepWrites: false })], 'ESX 1 OK');
  await visit(page, [ESXGUEST({ keepWrites: false })], 'ESX 1 OK');
  const names = await page.evaluate(async () => (await indexedDB.databases()).map((d) => d.name));
  await page.ctx.close();
  must(!names.includes('jnext-card-writes'), 'databases ' + JSON.stringify(names));
});

row('WBW-17', 'a changed NEX on the same page and image reads the old save: original ESX 1, ESX 2, then the variant ESX 3, then the original ESX 4 (each visit follows the stored record of the one before, whose store is slowed by 400 ms); every record equals the native sequence\'s journal', async () => {
  const page = await newPage();
  // A visit whose store is slowed (as WBW-10): the next visit must wait for the record, as a visitor's reload would.
  const slowVisit = async (players, line) => {
    await open(page, players);
    await slowPersist(page, 400, 'write');
    const r = await rec(page);
    must(!r.mountError, 'mount: ' + r.mountError);
    await click(page);
    await waitMsg(page, line);
  };
  const want = async (b, what) => {
    const s = await storedWithin(page, 5000, (s) => sameBytes(s[K1], b));
    must(s && Object.keys(s).length === 1 && sameBytes(s[K1], b), `the record after ${what}: ${desc(s)}; native ${b.length} bytes`);
  };
  await slowVisit([ESXGUEST()], 'ESX 1 OK');
  await want(E1, 'ESX 1');
  await slowVisit([ESXGUEST()], 'ESX 2 OK');
  await want(E2, 'ESX 2');
  await slowVisit([ESXGUEST({ file: '/fixture/esx_save_counter_b.nex' })], 'ESX 3 OK');
  await want(E3, 'the changed NEX\'s ESX 3');
  await slowVisit([ESXGUEST()], 'ESX 4 OK');
  await want(E4, 'ESX 4');
  await page.ctx.close();
});

let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), Number(E.JNEXT_ROW_TIMEOUT_MS) || 600000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
