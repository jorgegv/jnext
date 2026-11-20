// Browser rows WEBNMI-01..04: the NMI button (player.nmi() and the widget's NMI button) in headless
// Chromium (make wasm-browser-test, spec nmi.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Server A is the page host (http://localhost:<port>): the build directory, the fixture page, the
// programs of the regression suite and, for the widget row, a virtual page. Server B stands in for
// the image host (the run-time minimal card, with CORS): real servers, so Chromium applies its own
// rules. The card is copyrighted: it lives in the scratch directory and in the page's storage only;
// tracing, HAR and video are never enabled.
// Oracles: the NATIVE run of browser-smoke.sh (`oracle nmi`: NextZXOS booted, the Multiface NMI
// pressed with --delayed-nmi-frames, picture at SMOKE_NMI_SHOT; browser-smoke.sh first proves the
// NMI Menu picture does not change over the next 100 frames, so the page's press frame need not be
// exact); the man page's description of the case button "NMI" (Multiface) and of --no-sdcard (no
// ROM, so no Multiface); the contract of the API (a refusal is the call's own error).
import { Harness, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256;
const PRESS = Number(E.SMOKE_NMI_PRESS), SHOT = Number(E.SMOKE_NMI_SHOT);

const h = new Harness({});
const bLog = [];
const B = http.createServer((req, res) => {
  bLog.push(req.url);
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), nex: path.join(ROOT, 'test/00regression/nex') };
const pages = {};   // virtual pages: name -> html
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const v = /^\/v\/([^/]+)$/.exec(u.pathname);
  if (v && pages[v[1]] !== undefined) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(pages[v[1]]); return; }
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
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

async function newPage() {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx; page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
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
const waitFrame = (page, n) => waitFor(page, (n) => window.player0._module._jnext_web_frame() >= n, n, 300000);
const nmiResult = (page) => page.evaluate(() => window.player0.nmi().then(() => 'resolved', (e) => e.message));
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

row('WEBNMI-01', 'NextZXOS booted: player.nmi() resolves and the picture is the native Multiface NMI Menu (0 pixels)', async () => {
  const page = await newPage();
  await open(page, [{ machine: 'next', muted: true,
                      args: ['--rtc', '2026-07-10T08:55:00', '--delayed-screenshot', '/tmp/s.png', '--delayed-screenshot-frames', String(SHOT)] }]);
  await click(page);
  await waitReady(page);
  await waitFrame(page, PRESS);
  const res = await nmiResult(page);
  must(res === 'resolved', 'nmi() ' + res);
  await waitFor(page, () => window.player0._module.FS.analyzePath('/tmp/s.png').exists, null, 300000);
  const b64 = await page.evaluate(() => {
    const d = window.player0._module.FS.readFile('/tmp/s.png'); let s = '';
    for (let k = 0; k < d.length; k += 8192) s += String.fromCharCode.apply(null, d.subarray(k, k + 8192));
    return btoa(s);
  });
  const got = await decode(Buffer.from(b64, 'base64'));
  const want = await decode(fs.readFileSync(path.join(ORACLE, 'nmi.png')));
  const d = diff(got, want);
  const r = await rec(page);
  await page.ctx.close();
  must(r.errors.length === 0, 'errors ' + JSON.stringify(r.errors));
  must(d === 0, `${d} pixels differ from the native NMI Menu`);
});

row('WEBNMI-02', "card source 'none': nmi() rejects with the reason and onError hears nothing new", async () => {
  const page = await newPage();
  await open(page, [{ machine: 'next', muted: true, file: '/nex/magic_port_demo.nex', card: { source: 'none' } }]);
  await click(page);
  await waitReady(page);
  await sleep(1500);   // a ROM fetch would be reported within the first frames
  const before = await rec(page);
  const res = await nmiResult(page);
  await sleep(500);
  const after = await rec(page);
  await page.ctx.close();
  must(before.errors.length === 0 && before.state === 'running', 'before: ' + JSON.stringify(before));
  must(res === "the NMI button needs an SD card: not with card source 'none'", 'nmi() ' + res);
  must(JSON.stringify(after.errors) === '[]', 'onError got ' + JSON.stringify(after.errors));
});

row('WEBNMI-03', 'nmi() before the click rejects "the machine is not running"; after destroy() it throws "this player was destroyed"', async () => {
  const page = await newPage();
  await open(page, [{ machine: 'next', muted: true, file: '/nex/red.nex' }]);
  const early = await nmiResult(page);
  await click(page);
  await waitReady(page);
  await page.evaluate(() => window.player0.destroy());
  const thrown = await page.evaluate(() => { try { window.player0.nmi(); return 'no throw'; } catch (e) { return e.message; } });
  await page.ctx.close();
  must(early === 'jnext: the machine is not running', 'before the click: ' + early);
  must(thrown === 'jnext: this player was destroyed', 'after destroy: ' + thrown);
});

row('WEBNMI-04', 'widget: no NMI button without data-nmi, the toolbar Pause, Reset, NMI, Mute, Fullscreen with it; a click calls player.nmi() once and returns the focus to the canvas; with card source none the NMI button stays disabled and the others are enabled', async () => {
  const red = `${AURL}/nex/red.nex`, magic = `${AURL}/nex/magic_port_demo.nex`;
  pages['nmi.html'] = `<!doctype html><body><div class="jnext-widget" data-file="${red}" data-nmi></div><script src="/build/jnext-widget.js"></script></body>`;
  pages['nmi-none.html'] = `<!doctype html><body><div class="jnext-widget" data-file="${magic}" data-card-source="none" data-nmi></div><script src="/build/jnext-widget.js"></script></body>`;
  const open2 = async (name) => {
    const page = await newPage();
    await page.ctx.addInitScript((d) => { window['jnextDev'] = d; }, { card: IMAGE });
    await page.goto(`${AURL}/v/${name}`);
    await waitFor(page, () => { const e = document.querySelector('.jnext-widget'); return e && e.jnextPlayer && e.querySelector('.jw-status').textContent === 'click the screen to start'; }, null, 300000);
    return page;
  };
  pages['nmi-off.html'] = `<!doctype html><body><div class="jnext-widget" data-file="${red}"></div><script src="/build/jnext-widget.js"></script></body>`;
  const off = await open2('nmi-off.html');
  const offButtons = await off.evaluate(() => [...document.querySelectorAll('.jnext-widget .jw-btn')].map((b) => b.textContent));
  await off.ctx.close();
  const page = await open2('nmi.html');
  const buttons = await page.evaluate(() => [...document.querySelectorAll('.jnext-widget .jw-btn')].map((b) => b.textContent));
  await page.evaluate(() => { const p = document.querySelector('.jnext-widget').jnextPlayer, o = p.nmi; window.__calls = 0; p.nmi = function () { window.__calls++; return o.apply(p, arguments); }; });
  await page.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'running', null, 300000);
  const tip = await page.evaluate(() => document.querySelector('[data-jw=nmi]').title);
  await page.locator('[data-jw=nmi]').click();
  await sleep(300);
  const calls = await page.evaluate(() => window.__calls);
  const focus = await page.evaluate(() => document.activeElement && document.activeElement.tagName);
  const state = await page.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer.state);
  await page.ctx.close();
  const none = await open2('nmi-none.html');
  const dis = await none.evaluate(() => Object.fromEntries([...document.querySelectorAll('.jnext-widget .jw-btn')].map((b) => [b.textContent, b.disabled])));
  await none.ctx.close();
  must(JSON.stringify(offButtons) === JSON.stringify(['Pause', 'Reset', 'Mute', 'Fullscreen']), 'without data-nmi: ' + offButtons);
  must(JSON.stringify(buttons) === JSON.stringify(['Pause', 'Reset', 'NMI', 'Mute', 'Fullscreen']), 'buttons ' + buttons);
  must(tip === 'NMI button (F9)', 'tooltip ' + tip);
  must(calls === 1, 'player.nmi was called ' + calls + ' times');
  must(focus === 'CANVAS' && state === 'running', `focus ${focus}, state ${state}`);
  must(dis.NMI === true && dis.Pause === false && dis.Reset === false && dis.Mute === false && dis.Fullscreen === false, 'source none: ' + JSON.stringify(dis));
});

let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 400000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
