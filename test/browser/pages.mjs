// Browser rows PAGES-01..05: the GitHub Pages demo site (`make pages-site`, build/pages) served ALONE
// (make wasm-browser-test, spec pages.mjs). PAGES-03..05 are the two game demos and the fallback. Run by browser-smoke.sh on the SDK's node.
//
// Server A serves ONLY build/pages (404 for anything else, every request logged), as package.mjs
// serves the extracted web zip. Server B stands in for the distribution host: a zip of the cached
// real image made at run time by browser-smoke.sh (python deflate), through the localhost-only
// development seam set by addInitScript. The zip, the card the page builds from it and everything
// else copyrighted live in the scratch directory and in the page's storage only; the script deletes
// the scratch directory; tracing, HAR and video are never enabled and nothing is uploaded.
// Oracles: the package zip extracted by a FOREIGN reader (python3 -m zipfile, browser-smoke.sh) for
// the site's file list; the native `--dirs /GAMES,/APPS` card of tools/make-minimal-card for the
// card's SHA-256 (REAL_DIRS); the site contract of the issue (no image, program or archive is ever
// requested from the site's origin). One browser context for all rows, so the card is built once.
import { Harness, waitFor } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, VERSION = E.SMOKE_BUILD_VERSION;
const PKG = path.join(E.SMOKE_PKG, `jnext-${VERSION}-web`);
const SITE = path.join(ROOT, 'build/pages');
const REAL_DIRS = E.SMOKE_REAL_DIRS_CARD_SHA256;
const MTX = E.SMOKE_MTX;   // mtools copies of the game directories (a foreign FAT reader), made by browser-smoke.sh
const REAL = { size: Number(E.SMOKE_REAL_ZIP_SIZE), sha256: E.SMOKE_REAL_ZIP_SHA256 };
// The same list as packaging/web/pages-site.sh: a name with one of these as a dot-separated part, or ending in a dot or whitespace.
const BANNED_PARTS = new Set(['img', 'mmc', 'rom', 'bin', 'dsk', 'trd', 'scl', 'hdf', 'vhd', 'mdr', 'p', 'nex', 'tap', 'tzx', 'sna', 'szx', 'z80',
  'jns', 'rzx', 'wav', 'raw', 'zip', 'gz', 'tgz', 'bz2', 'xz', '7z', 'rar', 'tar']);
const BANNED = { test: (n) => { const b = n.split('/').pop(); return b === '' || /[.\s]$/.test(b) || b.split('.').slice(1).some((x) => BANNED_PARTS.has(x.toLowerCase())); } };
const EXAMPLES = ['plain.html', 'itch-host.html', 'itch-game.html', 'widget.html'];

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.txt': 'text/plain' };
const aLog = [];
const A = http.createServer((req, res) => {
  const name = decodeURIComponent(new URL(req.url, 'http://x').pathname.slice(1)) || 'index.html';
  const f = path.resolve(SITE, name);
  const known = f.startsWith(SITE + path.sep) && fs.existsSync(f) && fs.statSync(f).isFile();
  aLog.push({ name, status: known ? 200 : 404 });
  if (!known) { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(name)] || 'application/octet-stream', 'Content-Length': body.length });
  res.end(body);
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const bLog = [];
const B = http.createServer((req, res) => {
  bLog.push({ method: req.method, path: req.url });
  if (req.url !== '/real-distro.zip') { res.writeHead(404); res.end(); return; }
  const f = path.join(SCRATCH, 'real-distro.zip');
  const hd = { 'Content-Type': 'application/zip', 'Content-Length': fs.statSync(f).size, 'Access-Control-Allow-Origin': '*' };
  res.writeHead(200, hd);
  if (req.method === 'HEAD') { res.end(); return; }
  fs.createReadStream(f).pipe(res);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`, BURL = `http://127.0.0.1:${B.address().port}`;

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

const urls = [];   // every http(s) request of the context
const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
await ctx.addInitScript((d) => { window['jnextDev'] = d; }, { distribution: { url: BURL + '/real-distro.zip', ...REAL } });
ctx.on('request', (r) => { if (/^https?:/.test(r.url())) urls.push(r.url()); });
// The page's visitor statistics script: answered here with an empty script, so no test run reaches the real
// statistics server; it is the one origin outside the site and the distribution the page may ask.
const STATS = 'https://stats.jogv.es', STATS_ID = '403ba580-ee55-42b9-9709-dd5961bade47';
await ctx.route(STATS + '/**', (r) => r.fulfill({ status: 200, contentType: 'text/javascript', body: '' }));
async function open(query = '') {
  const page = await ctx.newPage();
  page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error') page.msgs.push('CONSOLE ' + m.text()); });
  await page.goto(`${AURL}/index.html${query}`);
  return page;
}
const status = (page) => page.evaluate(() => document.querySelector('.jnext-widget .jw-status').textContent);
const memCardSha = (page) => page.evaluate(async () => {
  const d = document.querySelector('.jnext-widget').jnextPlayer._module.FS.readFile('/sdcard/image.img');
  const x = new Uint8Array(await crypto.subtle.digest('SHA-256', d));
  return [...x].map((b) => b.toString(16).padStart(2, '0')).join('');
});
const listTreeFlat = (dir) => {
  const out = {};
  (function walk(d, rel) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      if (e.isDirectory()) walk(path.join(d, e.name), rel + e.name + '/'); else out[rel + e.name] = 1;
    }
  })(dir, '');
  return out;
};
const listTree = (dir) => {
  const out = [];
  (function walk(d, rel) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      if (e.isDirectory()) walk(path.join(d, e.name), rel + e.name + '/'); else out.push(rel + e.name);
    }
  })(dir, '');
  return out.sort();
};

row('PAGES-01', 'the served site is index.html plus the extracted package minus the four example pages; the page has a title, the issues link, the three demo links, the toolbar Pause, Reset, NMI (enabled), Mute, Fullscreen, the statistics script tag and no error', async () => {
  const want = [...listTree(PKG).filter((n) => !EXAMPLES.includes(n)), 'index.html'].sort();
  const got = listTree(SITE);
  must(JSON.stringify(got) === JSON.stringify(want), `site ${JSON.stringify(got)} != package-derived ${JSON.stringify(want)}`);
  const page = await open();
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'click the screen to start', null, 300000);
  const info = await page.evaluate(() => ({
    title: document.title, h1: document.querySelector('h1').textContent,
    issues: [...document.querySelectorAll('a')].map((a) => a.href).filter((u) => u.includes('github.com')),
    widgets: document.querySelectorAll('.jnext-widget').length, dirs: document.querySelector('.jnext-widget').getAttribute('data-card-dirs'),
    version: document.querySelector('p').textContent,
    buttons: [...document.querySelectorAll('.jnext-widget .jw-btn')].map((b) => b.textContent),
    nmiOn: !document.querySelector('.jnext-widget [data-jw=nmi]').disabled,
    links: [...document.querySelectorAll('nav a')].map((a) => a.getAttribute('href')),
    current: [...document.querySelectorAll('nav a[aria-current]')].map((a) => a.getAttribute('data-demo')),
    wtitle: document.querySelector('.jw-title').textContent,
    stats: [...document.querySelectorAll('script[src="https://stats.jogv.es/script.js"]')].map((e) => [e.defer, e.getAttribute('data-website-id')]) }));
  await page.close();
  must(info.title.includes('JNEXT') && info.h1.includes('JNEXT'), 'title ' + JSON.stringify(info));
  must(info.issues.includes('https://github.com/jorgegv/jnext/issues'), 'links ' + JSON.stringify(info.issues));
  must(JSON.stringify(info.links) === JSON.stringify(['?demo=nextzxos', '?demo=warhawk', '?demo=tx-1696']) && JSON.stringify(info.current) === '["nextzxos"]' && info.wtitle === 'NextZXOS', 'demo links ' + JSON.stringify([info.links, info.current, info.wtitle]));
  must(info.widgets === 1 && info.dirs === '/APPS,/GAMES', 'widget ' + JSON.stringify(info));
  must(JSON.stringify(info.buttons) === JSON.stringify(['Pause', 'Reset', 'NMI', 'Mute', 'Fullscreen']) && info.nmiOn, 'toolbar ' + JSON.stringify([info.buttons, info.nmiOn]));
  must(info.version.includes(`JNEXT ${VERSION},`), 'version text ' + info.version);
  must(JSON.stringify(info.stats) === JSON.stringify([[true, STATS_ID]]), 'statistics script ' + JSON.stringify(info.stats));
  must(page.msgs.length === 0, page.msgs.join('; '));
  must(aLog.filter((r) => r.status === 404 && r.name !== 'favicon.ico').length === 0, '404s ' + JSON.stringify(aLog.filter((r) => r.status === 404)));
});

row('PAGES-02', 'no query string gives the NextZXOS demo: the click builds the /APPS,/GAMES card from the distribution; ready, no error, argv has --sdcard and no --load, the card is the native --dirs card; no request outside the site, the distribution and the statistics script, none for an image, program or archive name', async () => {
  const n0 = urls.length, a0 = aLog.length;
  const page = await open();
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'click the screen to start', null, 300000);
  await page.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'running', null, 900000);
  const argv = await page.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer.argv);
  const sha = await memCardSha(page);
  const st = await status(page);
  await page.close();
  must(page.msgs.length === 0, page.msgs.join('; '));
  must(st === 'running', 'status ' + st);
  must(argv.includes('--sdcard') && !argv.includes('--load'), 'argv ' + JSON.stringify(argv));
  must(sha === REAL_DIRS, `card ${sha} != native --dirs card ${REAL_DIRS}`);
  const asked = urls.slice(n0).map((u) => new URL(u));
  const stray = asked.filter((u) => u.origin !== AURL && u.origin !== BURL && u.origin !== STATS);
  must(stray.length === 0, 'requests elsewhere: ' + JSON.stringify(stray.map((u) => u.href)));
  must(bLog.some((r) => r.path === '/real-distro.zip' && r.method === 'GET'), 'the distribution was never fetched: ' + JSON.stringify(bLog));
  const banned = aLog.slice(a0).filter((r) => BANNED.test(r.name));
  must(banned.length === 0, 'the site was asked for ' + JSON.stringify(banned));
});

// A game demo: the cached card (no request to the distribution), the game's directory copied out of it and
// served to it, the NMI button on, the demo's link current.
async function gameDemo(demo, file, nfilesDir) {
  const n0 = urls.length, b0 = bLog.length;
  const page = await open('?demo=' + demo);
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'click the screen to start', null, 300000);
  await page.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'running', null, 900000);
  const info = await page.evaluate(() => {
    const p = document.querySelector('.jnext-widget').jnextPlayer, FS = p._module.FS;
    const argv = p.argv, load = argv[argv.indexOf('--load') + 1], dir = load.replace(/\/[^/]+$/, '');
    let files = 0;
    (function w(d) { for (const n of FS.readdir(d)) { if (n === '.' || n === '..') continue; const q = d + '/' + n; if (FS.isDir(FS.stat(q).mode)) w(q); else files++; } })(dir);
    return { argv, load, files, nmiOn: !document.querySelector('[data-jw=nmi]').disabled,
             current: [...document.querySelectorAll('nav a[aria-current]')].map((a) => a.getAttribute('data-demo')),
             attr: document.querySelector('.jnext-widget').getAttribute('data-card-file'), title: document.querySelector('.jw-title').textContent };
  });
  await page.close();
  must(page.msgs.length === 0, page.msgs.join('; '));
  must(bLog.length === b0 && !urls.slice(n0).some((u) => u.startsWith(BURL)), 'the distribution was asked for: ' + JSON.stringify(bLog.slice(b0)));
  must(info.argv.includes('--esxdos-stub') && info.argv.includes('--sdcard') && info.load.endsWith('/' + file) && /^\/media\/\d+-card\//.test(info.load), 'argv ' + JSON.stringify(info.argv));
  must(info.nmiOn && JSON.stringify(info.current) === JSON.stringify([demo]), `nmi ${info.nmiOn} current ${JSON.stringify(info.current)}`);
  return info;
}

row('PAGES-03', '?demo=warhawk (the card is cached): no request to the distribution, ready, no error, argv has --esxdos-stub and --load .../Warhawk.nex, the NMI button is enabled, the warhawk link is the current page', async () => {
  const info = await gameDemo('warhawk', 'Warhawk.nex');
  must(info.attr === '/GAMES/Next/Warhawk/Warhawk.nex' && info.title === 'Warhawk', 'widget ' + JSON.stringify([info.attr, info.title]));
  must(info.files === Object.keys(listTreeFlat(path.join(MTX, 'Warhawk'))).length, 'copied files ' + info.files);
});

row('PAGES-04', '?demo=tx-1696 (the card is cached): same assertions, and the copied directory holds as many files as the mtools copy, with main.nex as the load target', async () => {
  const info = await gameDemo('tx-1696', 'main.nex');
  const want = Object.keys(listTreeFlat(path.join(MTX, 'TX-1696'))).length;
  must(info.attr === '/GAMES/Next/TX-1696/main.nex' && info.title === 'TX-1696', 'widget ' + JSON.stringify([info.attr, info.title]));
  must(want > 10 && info.files === want, `copied ${info.files} files, mtools copy has ${want}`);
});

row('PAGES-05', '?demo=nope gives the NextZXOS demo: no data-card-file, argv without --load and --esxdos-stub, the nextzxos link is the current page', async () => {
  const page = await open('?demo=nope');
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'click the screen to start', null, 300000);
  await page.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'running', null, 900000);
  const info = await page.evaluate(() => ({ argv: document.querySelector('.jnext-widget').jnextPlayer.argv,
    attr: document.querySelector('.jnext-widget').getAttribute('data-card-file'), title: document.querySelector('.jw-title').textContent,
    current: [...document.querySelectorAll('nav a[aria-current]')].map((a) => a.getAttribute('data-demo')) }));
  await page.close();
  must(info.attr === null && info.title === 'NextZXOS' && JSON.stringify(info.current) === '["nextzxos"]', JSON.stringify([info.attr, info.title, info.current]));
  must(!info.argv.includes('--load') && !info.argv.includes('--esxdos-stub'), 'argv ' + JSON.stringify(info.argv));
});

let pass = 0, fail = 0;
for (const r of rows) {
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 1200000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await ctx.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
