// Browser rows BCFILE-01..06: a program on the SD card, file: {card: PATH} (make wasm-browser-test, spec
// cardfile.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Rows 01..03 run on REAL data: the page builds the /APPS,/GAMES card from a zip of the cached real image
// (made at run time by browser-smoke.sh, python deflate, served by a localhost stand-in for the distribution
// host through the development seam) and copies the game's directory out of it. The oracles are made by
// browser-smoke.sh from the native `--dirs /GAMES,/APPS` card of tools/make-minimal-card, in the scratch
// directory: mtools (a FOREIGN FAT reader) copies of the two game directories, and native SDL runs of the
// same arguments (the card clone, --esxdos-stub, --load of the mtools copy, the same frame). Rows 04..06 use
// the SYNTHETIC distribution (invented files, licence-clean). Everything copyrighted lives in the scratch
// directory and the page's storage only, which browser-smoke.sh deletes; tracing, HAR and video stay off and
// nothing is uploaded.
import { Harness, waitFor, sleep } from './harness.mjs';
import crypto from 'crypto';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SYN = { size: Number(E.SMOKE_SYN_ZIP_SIZE), sha256: E.SMOKE_SYN_ZIP_SHA256, cardSha256: E.SMOKE_SYN_CARD_SHA256 };
const REAL = { size: Number(E.SMOKE_REAL_ZIP_SIZE), sha256: E.SMOKE_REAL_ZIP_SHA256 };
const REAL_DIRS = E.SMOKE_REAL_DIRS_CARD_SHA256;
const MTX = E.SMOKE_MTX;   // mtools copies: <MTX>/Warhawk, <MTX>/TX-1696
const WH_SHOT = Number(E.SMOKE_WH_SHOT), TX_SHOT = Number(E.SMOKE_TX_SHOT);

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), nex: path.join(ROOT, 'test/00regression/nex') };
const pages = {};
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const v = /^\/v\/([^/]+)$/.exec(u.pathname);
  if (v && pages[v[1]] !== undefined) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(pages[v[1]]); return; }
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const dLog = [];
const D = http.createServer((req, res) => {
  dLog.push({ method: req.method, path: req.url });
  const f = { '/synthetic-distro.zip': path.join(E.SMOKE_SYN_DIR, 'synthetic-distro.zip'), '/real-distro.zip': path.join(SCRATCH, 'real-distro.zip') }[req.url];
  if (!f) { res.writeHead(404); res.end(); return; }
  const size = fs.statSync(f).size;
  res.writeHead(200, { 'Content-Type': 'application/zip', 'Content-Length': size, 'Access-Control-Allow-Origin': '*' });
  if (req.method === 'HEAD') { res.end(); return; }
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => D.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`, DURL = `http://127.0.0.1:${D.address().port}`;
const REALSEAM = { distribution: { url: DURL + '/real-distro.zip', ...REAL } };
const SYNSEAM = { distribution: { url: DURL + '/synthetic-distro.zip', ...SYN } };

await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

async function newPage(shared) {
  const ctx = shared || await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx; page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  return page;
}
async function open(page, players, dev) {
  const q = new URLSearchParams({ module: MODULE, players: JSON.stringify(players) });
  if (dev) q.set('dev', JSON.stringify(dev));
  await page.goto(AURL + '/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length, 120000);
}
const rec = (page) => page.evaluate(() => {
  const r = window.apiTest.players[0];
  return { ready: r.ready, errors: r.errors, mountError: r.mountError, state: r.player ? r.player.state : null, argv: r.player ? r.player.argv : null };
});
const click = (page) => page.click('#p0 .jnext-overlay', { timeout: 120000, noWaitAfter: true });
const waitReady = (page, n = 1, ms = 900000) => waitFor(page, (n) => window.apiTest.players[0].ready >= n || window.apiTest.players[0].errors.length > 0, n, ms);
const memCardSha = (page) => page.evaluate(async () => {
  const d = window.player0._module.FS.readFile('/sdcard/image.img');
  const x = new Uint8Array(await crypto.subtle.digest('SHA-256', d));
  return [...x].map((b) => b.toString(16).padStart(2, '0')).join('');
});
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
async function shotVsOracle(page, oracle) {
  await waitFor(page, () => { try { return window.player0._module.FS.analyzePath('/media/s.png').exists; } catch (e) { return false; } }, null, 600000);
  const b64 = await page.evaluate(() => {
    const d = window.player0._module.FS.readFile('/media/s.png'); let s = '';
    for (let k = 0; k < d.length; k += 8192) s += String.fromCharCode.apply(null, d.subarray(k, k + 8192));
    return btoa(s);
  });
  return diff(await decode(Buffer.from(b64, 'base64')), await decode(fs.readFileSync(path.join(ORACLE, oracle))));
}
const DIRS = ['/APPS', '/GAMES'];
const RTC = ['--rtc', '2026-07-10T08:55:00'];
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const walk = (dir) => {   // rel path -> sha256 of every file under dir
  const out = {};
  (function w(d, rel) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      if (e.isDirectory()) w(path.join(d, e.name), rel + e.name + '/'); else out[rel + e.name] = sha(fs.readFileSync(path.join(d, e.name)));
    }
  })(dir, '');
  return out;
};
const copyDir = (page, argv) => argv[argv.indexOf('--load') + 1].replace(/\/[^/]+$/, '');
// Rows 01..03 share one browser context, so the Cache API holds the real card after the first visit
// and only BCFILE-01 builds it.
let main = null;
let realCtx = null;

row('BCFILE-01', 'REAL data: Warhawk from file {card}: ready, no error; argv has --esxdos-stub and --load /media/<k>-card/Warhawk.nex; the card is the native --dirs card (unchanged by the copy); the picture equals the native run (0 pixels)', async () => {
  realCtx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
  main = await newPage(realCtx);
  await open(main, [{ machine: 'next', muted: true, card: { dirs: DIRS }, file: { card: '/GAMES/Next/Warhawk/Warhawk.nex' },
                      args: [...RTC, '--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', String(WH_SHOT)] }], REALSEAM);
  await click(main);
  await waitReady(main);
  const r = await rec(main);
  must(r.errors.length === 0 && r.state === 'running', 'errors ' + JSON.stringify(r.errors) + ' state ' + r.state);
  must(r.argv.filter((a) => a === '--esxdos-stub').length === 1, 'argv ' + JSON.stringify(r.argv));
  const i = r.argv.indexOf('--load');
  must(i >= 0 && /^\/media\/\d+-card\/Warhawk\.nex$/.test(r.argv[i + 1]), 'argv ' + JSON.stringify(r.argv));
  const cs = await memCardSha(main);
  must(cs === REAL_DIRS, `card ${cs} != native --dirs card ${REAL_DIRS}`);
  const d = await shotVsOracle(main, 'warhawk.png');
  must(d === 0, `${d} pixels differ from the native Warhawk run`);
  await main.close();
});

row('BCFILE-02', 'REAL data: TX-1696 from file {card}: every copied file has the SHA-256 of the mtools copy (a foreign FAT reader) and no file is missing or extra; the picture equals the native run (0 pixels)', async () => {
  main = await newPage(realCtx);
  await open(main, [{ machine: 'next', muted: true, card: { dirs: DIRS }, file: { card: '/GAMES/Next/TX-1696/main.nex' },
                      args: [...RTC, '--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', String(TX_SHOT)] }], REALSEAM);
  await click(main);
  await waitReady(main);
  const r = await rec(main);
  must(r.errors.length === 0 && r.state === 'running', 'errors ' + JSON.stringify(r.errors) + ' state ' + r.state);
  must(r.argv.includes('--esxdos-stub') && /\/main\.nex$/.test(r.argv[r.argv.indexOf('--load') + 1]), 'argv ' + JSON.stringify(r.argv));
  const dir = copyDir(main, r.argv);
  const got = await main.evaluate(async (dir) => {
    const FS = window.player0._module.FS, out = {};
    const hex = async (d) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', d))].map((b) => b.toString(16).padStart(2, '0')).join('');
    const w = async (d, rel) => {
      for (const n of FS.readdir(d)) {
        if (n === '.' || n === '..') continue;
        const p = d + '/' + n, st = FS.stat(p);
        if (FS.isDir(st.mode)) await w(p, rel + n + '/'); else out[rel + n] = await hex(FS.readFile(p));
      }
    };
    await w(dir, '');
    return out;
  }, dir);
  const want = walk(path.join(MTX, 'TX-1696'));
  const bad = [];
  for (const k of Object.keys(want)) if (got[k] !== want[k]) bad.push('bad:' + k);
  for (const k of Object.keys(got)) if (!(k in want)) bad.push('extra:' + k);
  must(Object.keys(want).length > 10 && bad.length === 0, `${Object.keys(got).length} copied, ${Object.keys(want).length} expected: ${bad.slice(0, 5).join(' ')}`);
  const d = await shotVsOracle(main, 'tx1696.png');
  must(d === 0, `${d} pixels differ from the native TX-1696 run`);
});

row('BCFILE-03', 'reset() (program) of that player resolves with ready again, argv is unchanged and no error is raised', async () => {
  const before = await rec(main);
  await main.evaluate(() => window.player0.reset());
  await waitReady(main, 2, 300000);
  const after = await rec(main);
  await realCtx.close();
  must(after.ready >= 2 && after.errors.length === 0, `ready ${after.ready} errors ${JSON.stringify(after.errors)}`);
  must(JSON.stringify(before.argv) === JSON.stringify(after.argv), 'argv changed');
});

row('BCFILE-04', 'a NEX that is not on the card (synthetic distribution): onError names the path, the player stops, nothing is loaded', async () => {
  const p = await newPage();
  await open(p, [{ machine: 'next', muted: true, card: { dirs: ['/GAMES'] }, file: { card: '/GAMES/NOPE/x.nex' } }], SYNSEAM);
  await click(p);
  await waitFor(p, () => window.apiTest.players[0].errors.length > 0, null, 300000);
  const r = await rec(p);
  await p.ctx.close();
  must(r.errors.length === 1 && r.errors[0] === 'the card has no /GAMES/NOPE/x.nex', 'errors ' + JSON.stringify(r.errors));
  must(r.state === 'exited' && (r.argv === null || r.argv === undefined) && r.ready === 0, `state ${r.state} argv ${JSON.stringify(r.argv)} ready ${r.ready}`);
});

row('BCFILE-05', 'refused at mount() before any request: {card} with card source none, a path outside the dirs, a .tap, a page-supplied $serveDir; load({card}) of a running player rejects', async () => {
  const n = dLog.length;
  const cases = [
    [{ card: { source: 'none' }, file: { card: '/GAMES/x.nex' } }, "option 'file': {card} needs an SD card: not with card source 'none'"],
    [{ card: { dirs: ['/GAMES'] }, file: { card: '/APPS/x.nex' } }, "option 'file': /APPS/x.nex is not on the card: add its directory to card.dirs"],
    [{ card: { dirs: ['/GAMES'] }, file: { card: '/GAMES/x.tap' } }, "option 'file': {card} takes a .nex"],
    [{ card: { dirs: ['/GAMES'] }, file: '/nex/red.nex', $serveDir: true }, "unknown option '$serveDir'"],
  ];
  for (const [opt, want] of cases) {
    const p = await newPage();
    await open(p, [{ machine: 'next', muted: true, ...opt }], SYNSEAM);
    const r = await rec(p);
    await p.ctx.close();
    must(r.mountError && r.mountError.includes(want), `${JSON.stringify(opt)} -> ${r.mountError}`);
  }
  must(dLog.length === n, 'requests to the distribution host: ' + JSON.stringify(dLog.slice(n)));
  const p = await newPage();
  await open(p, [{ machine: '48k', muted: true }], SYNSEAM);
  await click(p);
  await waitReady(p);
  const res = await p.evaluate(() => window.player0.load({ card: '/GAMES/next/g1/a.nex' }).then(() => 'resolved', (e) => e.message));
  await p.ctx.close();
  must(res === 'jnext: load(): a program on the card is given to mount() as file: {card}', 'load() ' + res);
});

row('BCFILE-06', 'widget: data-card-file with data-card-dirs gives a player whose argv has --esxdos-stub and the copied --load path; data-file together with data-card-file is an error in the error colour, the buttons stay disabled and nothing is requested', async () => {
  pages['cf.html'] = `<!doctype html><body><div class="jnext-widget" data-card-dirs="/GAMES" data-card-file="/GAMES/next/g1/a.nex"></div><script src="/build/jnext-widget.js"></script></body>`;
  pages['cf-both.html'] = `<!doctype html><body><div class="jnext-widget" data-card-dirs="/GAMES" data-card-file="/GAMES/next/g1/a.nex" data-file="/nex/red.nex"></div><script src="/build/jnext-widget.js"></script></body>`;
  const open2 = async (name) => {
    const p = await newPage();
    await p.ctx.addInitScript((d) => { window['jnextDev'] = d; }, SYNSEAM);
    await p.goto(`${AURL}/v/${name}`);
    return p;
  };
  const n = dLog.length;
  const both = await open2('cf-both.html');
  await waitFor(both, () => document.querySelector('.jw-status').className.includes('jw-error'), null, 60000);
  const bi = await both.evaluate(() => ({ text: document.querySelector('.jw-status').textContent, color: getComputedStyle(document.querySelector('.jw-status')).color,
                                          off: [...document.querySelectorAll('.jw-btn')].every((b) => b.disabled), player: !!document.querySelector('.jnext-widget').jnextPlayer }));
  const errColor = await both.evaluate(() => { const e = document.createElement('div'); e.className = 'jw-status jw-error'; document.querySelector('.jnext-widget .jw-frame').appendChild(e); const c = getComputedStyle(e).color; e.remove(); return c; });
  await both.ctx.close();
  must(bi.text === 'error: give data-file or data-card-file, not both' && bi.off && !bi.player && bi.color === errColor, JSON.stringify(bi) + ' ' + errColor);
  must(dLog.length === n, 'requests: ' + JSON.stringify(dLog.slice(n)));
  const p = await open2('cf.html');
  await waitFor(p, () => { const e = document.querySelector('.jnext-widget'); return e.jnextPlayer && e.querySelector('.jw-status').textContent === 'click the screen to start'; }, null, 300000);
  await p.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(p, () => document.querySelector('.jnext-widget').jnextPlayer.argv !== undefined, null, 300000);
  const argv = await p.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer.argv);
  await p.ctx.close();
  const i = argv.indexOf('--load');
  must(argv.includes('--esxdos-stub') && i >= 0 && /^\/media\/\d+-card\/a\.nex$/.test(argv[i + 1]), 'argv ' + JSON.stringify(argv));
});

let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 1200000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); D.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
