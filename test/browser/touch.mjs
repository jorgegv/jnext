// Browser rows TKB-01..17: the on-screen Spectrum Next keyboard (web/jnext-touch-keyboard.js) in
// headless Chromium under touch-device emulation (make wasm-browser-test, spec touch.mjs). Run by
// browser-smoke.sh on the SDK's node.
//
// Guest: test/fixtures/tap/key_matrix_demo.tap (source demo/key_matrix_demo, a 48K program). It reads
// the eight half-rows and NextReg 0xB0/0xB1 in a loop and writes one line on the magic port at every
// change: "KMX r0 r1 r2 r3 r4 r5 r6 r7 b0 b1" (r = the low five bits of IN 0xFEFE .. 0x7FFE, 1 =
// released; b = NR 0xB0, 0xB1, 1 = pressed). The page's console carries the lines.
//
// Touch input is real input: Chromium's Input.dispatchTouchEvent through a CDP session, at the centre
// of the key's rectangle, one or two touch points. The page is page/touch.html (the API fixture's
// player filling the viewport); the card comes from a real server on 127.0.0.1 with CORS, as in
// api.mjs.
//
// Oracles, ALL the spec's own: the 8x7 key matrix of the FPGA's keymaps.vhd (rows 0..7 are the half
// rows read at the ports 0xFEFE .. 0x7FFE, columns 0..4 the 40 keys, columns 5 and 6 the 16 extended
// keys), the fold of each extended key into the 8x5 matrix (membrane.vhd, the ex register and its
// per-row decode: CAPS SHIFT or SYMBOL SHIFT plus a key) and the raw readback (zxnext.vhd NR 0xB0 and
// 0xB1, nextreg.txt). Nothing is read from the product. The native oracles of browser-smoke.sh are
// the typist's screen (keys.png) and the same program's own log (kmx.log). The card is copyrighted:
// it lives in the scratch directory only; tracing, video and HAR are never enabled.
import { Harness, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256;
const ONLY = (E.TOUCH_ROWS || '').split(' ').filter(Boolean);   // development aid: run only these rows (unset in the suite)

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.tap': 'application/octet-stream',
                '.nex': 'application/octet-stream' };
const vpages = {};   // virtual pages: name -> html
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page'), tap: path.join(ROOT, 'test/fixtures/tap') };
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  if (m && m[1] === 'v' && vpages[m[2]] !== undefined) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(vpages[m[2]]); return; }
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream', 'Access-Control-Allow-Origin': '*' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const B = http.createServer((req, res) => {
  const f = { '/card.img': path.join(SCRATCH, 'card.img'), '/red.nex': path.join(ROOT, 'test/00regression/nex/red.nex') }[req.url];
  if (!f) { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`, BURL = `http://127.0.0.1:${B.address().port}`;
const IMAGE = { url: BURL + '/card.img', sha256: SHA };

await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');
const rows = [];
const row = (id, text, fn) => { if (!ONLY.length || ONLY.includes(id)) rows.push({ id, text, fn }); };
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

const PORTRAIT = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true };
const LANDSCAPE = { viewport: { width: 844, height: 390 }, deviceScaleFactor: 1, isMobile: true, hasTouch: true };
const DESKTOP = { viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 };

// ---- the oracle: the Next's keyboard, from the VHDL (the spec's own table) ------------------------
// keymaps.vhd rows 0..7, columns 0..4: the 40 matrix keys.
const HALF = [['caps', 'z', 'x', 'c', 'v'], ['a', 's', 'd', 'f', 'g'], ['q', 'w', 'e', 'r', 't'], ['1', '2', '3', '4', '5'],
              ['0', '9', '8', '7', '6'], ['p', 'o', 'i', 'u', 'y'], ['enter', 'l', 'k', 'j', 'h'], ['space', 'symbol', 'm', 'n', 'b']];
const POS = {};
HALF.forEach((r, i) => r.forEach((k, b) => { POS[k] = `m${i}${b}`; }));
// The 16 extended keys: the fold (membrane.vhd, ex register and its per-row decode) and the raw bit
// (NR 0xB0 or 0xB1, nextreg.txt: B0 bit 7..0 = ; " , . UP DOWN LEFT RIGHT, B1 bit 7..0 = DELETE EDIT
// BREAK INV TRUE GRAPH CAPSLOCK EXTEND).
const EXT = {
  'semicolon': [['symbol', 'o'], 'x07'], 'quote': [['symbol', 'p'], 'x06'], 'comma': [['symbol', 'n'], 'x05'],
  'period': [['symbol', 'm'], 'x04'], 'up': [['caps', '7'], 'x03'], 'down': [['caps', '6'], 'x02'],
  'left': [['caps', '5'], 'x01'], 'right': [['caps', '8'], 'x00'],
  'delete': [['caps', '0'], 'x17'], 'edit': [['caps', '1'], 'x16'], 'break': [['caps', 'space'], 'x15'],
  'inv-video': [['caps', '4'], 'x14'], 'true-video': [['caps', '3'], 'x13'], 'graph': [['caps', '9'], 'x12'],
  'caps-lock': [['caps', '2'], 'x11'], 'extend': [['caps', 'symbol'], 'x10'],
};
const MATRIX_KEYS = HALF.flat().filter((k) => k !== 'caps' && k !== 'symbol').concat(['caps-shift', 'symbol-shift']);
function expectedOf(id) {
  if (EXT[id]) return new Set([...EXT[id][0].map((k) => POS[k]), EXT[id][1]]);
  const base = id.replace(/-r$/, '');
  const name = { 'caps-shift': 'caps', 'symbol-shift': 'symbol' }[base] || base;
  return new Set([POS[name]]);
}
const COMPACT_IDS = MATRIX_KEYS;                                   // 40
const FULL_IDS = MATRIX_KEYS.concat(['caps-shift-r', 'symbol-shift-r'], Object.keys(EXT));   // 58
const REL = '1F 1F 1F 1F 1F 1F 1F 1F 00 00';
function pressed(line) {
  const t = line.split(' ').map((x) => parseInt(x, 16));
  const s = new Set();
  for (let r = 0; r < 8; r++) for (let b = 0; b < 5; b++) if (!((t[r] >> b) & 1)) s.add(`m${r}${b}`);
  for (let b = 0; b < 8; b++) { if ((t[8] >> b) & 1) s.add(`x0${b}`); if ((t[9] >> b) & 1) s.add(`x1${b}`); }
  return s;
}
const same = (a, b) => a.size === b.size && [...a].every((x) => b.has(x));
const subset = (a, b) => [...a].every((x) => b.has(x));
const NAMES = {   // the accessible names, the spec's own list of the 58
  'enter': 'Enter', 'space': 'Space', 'caps-shift': 'Caps Shift', 'caps-shift-r': 'Caps Shift', 'symbol-shift': 'Symbol Shift',
  'symbol-shift-r': 'Symbol Shift', 'true-video': 'True video', 'inv-video': 'Inverse video', 'break': 'Break', 'delete': 'Delete',
  'extend': 'Extend mode', 'caps-lock': 'Caps lock', 'edit': 'Edit', 'graph': 'Graphics mode', 'semicolon': 'Semicolon',
  'quote': 'Double quote', 'period': 'Period', 'comma': 'Comma', 'left': 'Cursor left', 'right': 'Cursor right', 'up': 'Cursor up',
  'down': 'Cursor down',
};
for (const c of '1234567890') NAMES[c] = 'Key ' + c;
for (const c of 'qwertyuiopasdfghjklzxcvbnm') NAMES[c] = 'Key ' + c.toUpperCase();

// ---- the page and the player --------------------------------------------------------------------
const GUEST = (extra = {}) => Object.assign({
  file: '/tap/key_matrix_demo.tap', machine: '48k', muted: true, width: '100%', height: '100%',
  args: ['--magic-port', '0xCAFE', '--magic-port-mode', 'line'],
}, extra);
const PLAIN = (extra = {}) => Object.assign({ machine: '48k', muted: true, width: '100%', height: '100%' }, extra);

async function until(fn, ms = 15000, what = 'condition') {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > ms) throw new Error('timeout waiting for ' + what);
    await sleep(25);
  }
}
async function newPage(ctxOpts) {
  const ctx = await h.browser.newContext(ctxOpts);
  const page = await ctx.newPage();
  page.ctx = ctx;
  page.msgs = []; page.kmx = []; page.kmxReady = false; page.touches = new Map();
  page.on('console', (m) => {
    const t = m.text();
    page.msgs.push(t);
    if (t === 'KMX READY') page.kmxReady = true;
    else if (t.startsWith('KMX ')) page.kmx.push(t.slice(4).trim());
  });
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  return page;
}
async function open(page, players) {
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(IMAGE), players: JSON.stringify(players) });
  await page.goto(`${AURL}/page/touch.html?` + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.loaded && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled),
                players.length);
  const r = await page.evaluate(() => ({ err: window.apiTest.players[0].mountError }));
  must(!r.err, 'mount: ' + r.err);
}
const click = (page) => page.click('#p0 .jnext-overlay', { timeout: 600000, noWaitAfter: true });
const running = (page) => waitFor(page, () => window.player0 && window.player0.state === 'running', null, 300000);
// A page whose guest is the KMX program: started, and its first line seen.
async function kmxPage(ctxOpts, extra) {
  const page = await newPage(ctxOpts);
  await open(page, [GUEST(extra)]);
  await click(page);
  await until(() => page.kmxReady && page.kmx.length > 0, 300000, 'KMX READY and the first line');
  return page;
}
async function plainPage(ctxOpts, extra) {
  const page = await newPage(ctxOpts);
  await open(page, [PLAIN(extra)]);
  await click(page);
  await running(page);
  return page;
}
const close = (page) => page.ctx.close();
const kb = (page, q = '') => page.evaluate((q) => {
  const k = document.querySelector('#p0 .jnext-keyboard');
  return k ? (q ? !!k.querySelector(q) : true) : false;
}, q);

// ---- touch --------------------------------------------------------------------------------------
async function cdp(page) { if (!page.cdp) page.cdp = await page.ctx.newCDPSession(page); return page.cdp; }
const center = (page, id) => page.evaluate((id) => {
  const e = document.querySelector(`#p0 [data-key="${id}"]`);
  if (!e) return null;
  const r = e.getBoundingClientRect();
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
}, id);
async function touchDown(page, id, tid = 1) {
  const c = await center(page, id);
  must(c, 'no key ' + id);
  page.touches.set(tid, { x: c.x, y: c.y, id: tid });
  await (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [...page.touches.values()] });
}
async function touchUp(page, tid = 1) {
  const p = page.touches.get(tid);
  page.touches.delete(tid);
  await (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [p] });
}
async function touchTap(page, id, hold = 120) { await touchDown(page, id); await sleep(hold); await touchUp(page); }
const last = (page) => page.kmx[page.kmx.length - 1];
const lastIs = (page, set) => page.kmx.length > 0 && same(pressed(last(page)), set);
const released = (page) => page.kmx.length > 0 && last(page) === REL;
const ariaPressed = (page, id) => page.evaluate((id) => document.querySelector(`#p0 [data-key="${id}"]`).getAttribute('aria-pressed'), id);
// The lines since `idx`: a problem text, or '' when they show exactly the key `want` typed.
function judge(page, idx, want) {
  const ls = page.kmx.slice(idx);
  const sets = ls.map(pressed);
  const ok = sets.some((s) => same(s, want)) && sets.every((s) => subset(s, want)) && ls[ls.length - 1] === REL;
  return ok ? '' : `want {${[...want]}} saw [${ls.join(' | ')}]`;
}
// A tap on key `id`, judged as that key typed (its whole expected state, nothing outside it, then released).
async function tapKey(page, id, hold = 120) {
  const idx = page.kmx.length;
  await touchTap(page, id, hold);
  await until(() => page.kmx.length > idx && released(page), 6000, `${id} released`).catch(() => {});
  const p = judge(page, idx, expectedOf(id));
  return p ? id + ': ' + p : '';
}
const MODS = ['caps-shift', 'caps-shift-r', 'symbol-shift', 'symbol-shift-r'];
async function typeAll(page, ids) {
  const bad = [];
  for (const id of ids) {
    const n = MODS.includes(id) ? 2 : 1;   // a modifier tap latches: the second tap cancels it
    for (let i = 0; i < n; i++) { const p = await tapKey(page, id); if (p) { bad.push(p); break; } }
  }
  return bad;
}
const keyIds = (page) => page.evaluate(() => [...document.querySelectorAll('#p0 .jnext-keyboard [data-key]')].map((e) => e.getAttribute('data-key')));
const nativeKmx = () => fs.readFileSync(path.join(ORACLE, 'kmx.log'), 'latin1').split('\n').filter((l) => l.startsWith('KMX ')).map((l) => l.slice(4).trim());

// ---- PNG helpers (as smoke.mjs) -------------------------------------------------------------------
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

// ---- rows ---------------------------------------------------------------------------------------
row('TKB-01', 'auto on a touch device (portrait): after the start the keyboard is visible at the bottom of the box, the canvas is shorter by its height and its pixel buffer follows; argv has no --touch-keyboard', async () => {
  const page = await plainPage(PORTRAIT);
  const argv = await page.evaluate(() => window.player0.argv);
  const geo = () => page.evaluate(() => {
    const box = document.querySelector('#p0 .jnext-player'), k = box.querySelector('.jnext-keyboard'), cv = box.querySelector('canvas');
    const b = box.getBoundingClientRect(), kr = k.getBoundingClientRect(), c = cv.getBoundingClientRect();
    return { vis: window.player0.keyboardVisible, box: [b.top, b.height, b.bottom], kb: [kr.top, kr.height, kr.bottom], cv: [c.width, c.height],
             buf: [cv.width, cv.height], display: getComputedStyle(k).display, inBox: k.parentNode === box };
  });
  await until(async () => { const g = await geo(); return g.buf[0] === Math.round(g.cv[0]) && g.buf[1] === Math.round(g.cv[1]); }, 15000, 'the buffer to follow').catch(() => {});
  const g = await geo();
  await close(page);
  must(g.vis === true && g.inBox && g.display !== 'none', 'visible: ' + JSON.stringify(g));
  must(Math.abs(g.kb[2] - g.box[2]) <= 1, 'keyboard bottom ' + g.kb[2] + ' vs box bottom ' + g.box[2]);
  must(Math.abs(g.cv[1] + g.kb[1] - g.box[1]) <= 1, 'canvas ' + g.cv[1] + ' + keyboard ' + g.kb[1] + ' != box ' + g.box[1]);
  must(g.buf[0] === Math.round(g.cv[0]) && g.buf[1] === Math.round(g.cv[1]), 'buffer ' + g.buf + ' vs canvas css ' + g.cv);
  must(!argv.includes('--touch-keyboard'), 'argv ' + argv);
});

row('TKB-02', 'auto on the desktop: after the start keyboardVisible is false, no .jnext-keyboard element exists and the canvas fills the box', async () => {
  const page = await plainPage(DESKTOP);
  const g = await page.evaluate(() => {
    const box = document.querySelector('#p0 .jnext-player'), cv = box.querySelector('canvas');
    const b = box.getBoundingClientRect(), c = cv.getBoundingClientRect();
    return { vis: window.player0.keyboardVisible, n: document.querySelectorAll('.jnext-keyboard').length, box: [b.width, b.height], cv: [c.width, c.height],
             kids: [...box.children].map((e) => e.tagName + '.' + e.className) };
  });
  await close(page);
  must(g.vis === false && g.n === 0, JSON.stringify(g));
  must(g.box.join() === g.cv.join(), 'canvas ' + g.cv + ' vs box ' + g.box);
  must(g.kids.join() === 'CANVAS.,DIV.jnext-overlay', 'the box children: ' + g.kids);
});

row('TKB-03', 'the C++ side decides: off on touch hides; on on the desktop shows; an args --touch-keyboard after the option wins; a bad value is refused with the flag\'s own message', async () => {
  const a = await plainPage(PORTRAIT, { touchKeyboard: 'off' });
  const ra = await a.evaluate(() => ({ vis: window.player0.keyboardVisible, n: document.querySelectorAll('.jnext-keyboard').length, argv: window.player0.argv }));
  await close(a);
  must(ra.vis === false && ra.n === 0 && ra.argv.join(' ').includes('--touch-keyboard off'), '(a) ' + JSON.stringify(ra));
  const b = await plainPage(DESKTOP, { touchKeyboard: 'on' });
  const rb = await b.evaluate(() => ({ vis: window.player0.keyboardVisible, n: document.querySelectorAll('.jnext-keyboard').length }));
  await close(b);
  must(rb.vis === true && rb.n === 1, '(b) ' + JSON.stringify(rb));
  const c = await plainPage(PORTRAIT, { touchKeyboard: 'off', args: ['--touch-keyboard', 'on'] });
  const rc = await c.evaluate(() => ({ vis: window.player0.keyboardVisible, argv: window.player0.argv }));
  await close(c);
  must(rc.vis === true && rc.argv.join(' ').includes('--touch-keyboard off') && rc.argv.join(' ').includes('--touch-keyboard on'), '(c) ' + JSON.stringify(rc));
  const d = await newPage(PORTRAIT);
  await open(d, [PLAIN({ touchKeyboard: 'sideways' })]);
  await click(d);
  const want = 'Unknown --touch-keyboard value: sideways (valid: auto, on, off)';
  await until(() => d.evaluate(() => window.apiTest.players[0].errors.length > 0), 60000, 'onError');
  const rd = await d.evaluate(async () => ({ errors: window.apiTest.players[0].errors, ready: await window.player0.ready.then(() => 'resolved', (e) => e.message) }));
  await close(d);
  must(rd.errors.some((e) => e.includes(want)) && rd.ready.includes(want), '(d) ' + JSON.stringify(rd));
});

row('TKB-04', 'every one of the 40 keys of the compact layout (portrait), tapped, types exactly its keymaps.vhd position and leaves NR 0xB0/0xB1 at 00; CS and SS are tapped twice (latch, cancel); the key set is exactly the 40; the lines for P equal the native program\'s', async () => {
  const page = await kmxPage(PORTRAIT);
  const ids = await keyIds(page);
  must(ids.length === 40 && same(new Set(ids), new Set(COMPACT_IDS)), 'compact data-key set: ' + ids.length + ' ' + ids);
  const bad = await typeAll(page, COMPACT_IDS);
  const idx = page.kmx.length;
  const pOut = await tapKey(page, 'p');
  const got = page.kmx.slice(idx);
  const nat = nativeKmx();
  await close(page);
  must(bad.length === 0, bad.join('; '));
  must(pOut === '', pOut);
  must(nat.length === 4 && got.length === 2 && got[0] === nat[2] && got[1] === nat[3], 'browser P lines ' + JSON.stringify(got) + ' vs native ' + JSON.stringify(nat));
});

row('TKB-05', 'every one of the 58 keys of the full layout (landscape) types its expected state: matrix keys as TKB-04, each extended key its fold plus exactly its own NR 0xB0/0xB1 bit, the second CS and SS as their twins; the key set is exactly the 58', async () => {
  const page = await kmxPage(LANDSCAPE);
  const ids = await keyIds(page);
  must(ids.length === 58 && same(new Set(ids), new Set(FULL_IDS)), 'full data-key set: ' + ids.length + ' ' + ids);
  const bad = await typeAll(page, FULL_IDS);
  await close(page);
  must(bad.length === 0, bad.join('; '));
});

row('TKB-06', '48K BASIC through the touch keys: P 1 2 Enter tapped at frame >= 200 give the screen of the native typist at frame 320', async () => {
  const page = await plainPage(PORTRAIT, { args: ['--delayed-screenshot', '/tmp/s.png', '--delayed-screenshot-frames', '320'] });
  await waitFor(page, () => window.player0._module._jnext_web_frame() >= 200, null, 120000);
  for (const k of ['p', '1', '2', 'enter']) { await touchTap(page, k, 60); await sleep(120); }
  await until(() => page.msgs.some((m) => /screenshot saved/.test(m)), 120000, 'the screenshot');
  const b64 = await page.evaluate(() => {
    const d = window.player0._module.FS.readFile('/tmp/s.png'); let s = '';
    for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
    return btoa(s);
  });
  await close(page);
  const got = await decode(Buffer.from(b64, 'base64'));
  const want = await decode(fs.readFileSync(path.join(ORACLE, 'keys.png')));
  const d = diff(got, want);
  must(d === 0, d + ' differing pixels vs the native typist (keys.png)');
});

row('TKB-07', 'the latch: a lone SS tap latches (aria-pressed), the next key P gives exactly SS+P and the latch clears; a lone tap on a latched CS clears it and the next Z is Z alone; the latched modifier is never held in the guest', async () => {
  const page = await kmxPage(PORTRAIT);
  const SS = new Set([POS.symbol]);
  let idx = page.kmx.length;
  await touchTap(page, 'symbol-shift');
  await until(() => page.kmx.length >= idx + 2 && released(page), 6000, 'SS down and released');
  const a = page.kmx.slice(idx).map(pressed);
  const aria1 = await ariaPressed(page, 'symbol-shift');
  const n0 = page.kmx.length;
  await sleep(500);   // (d) the latch is not held in the guest: nothing changes while it waits
  const quiet = page.kmx.length === n0;
  idx = page.kmx.length;
  const pp = await tapKey(page, 'p');   // judged against p alone: expect a failure, then judge by hand
  const lines = page.kmx.slice(idx).map(pressed);
  const both = new Set([POS.symbol, POS.p]);
  const aria2 = await ariaPressed(page, 'symbol-shift');
  await touchTap(page, 'caps-shift');
  await until(() => released(page), 6000, 'CS released');
  const aria3 = await ariaPressed(page, 'caps-shift');
  await sleep(200);
  await touchTap(page, 'caps-shift');
  await until(() => released(page), 6000, 'CS released again');
  const aria4 = await ariaPressed(page, 'caps-shift');
  const zOut = await tapKey(page, 'z');
  await close(page);
  must(a.length === 2 && same(a[0], SS) && a[1].size === 0, 'lone SS lines: ' + JSON.stringify(a.map((s) => [...s])));
  must(aria1 === 'true', 'SS not marked latched: ' + aria1);
  must(quiet, '(d) a line arrived while the latch only waited');
  must(lines.some((s) => same(s, both)) && lines.every((s) => subset(s, both)) && lines[lines.length - 1].size === 0, 'SS then P: ' + JSON.stringify(lines.map((s) => [...s])) + ' (' + pp + ')');
  must(aria2 === 'false', 'SS still latched after P: ' + aria2);
  must(aria3 === 'true' && aria4 === 'false', 'CS latch then cancel: ' + aria3 + ' ' + aria4);
  must(zOut === '', 'Z after the cancelled latch: ' + zOut);
});

row('TKB-08', 'both latched: CS, SS then 1 gives exactly CS+SS+1; afterwards neither is latched', async () => {
  const page = await kmxPage(PORTRAIT);
  await touchTap(page, 'caps-shift'); await until(() => released(page), 6000, 'CS'); await sleep(150);
  await touchTap(page, 'symbol-shift'); await until(() => released(page), 6000, 'SS'); await sleep(150);
  const idx = page.kmx.length;
  const want = new Set([POS.caps, POS.symbol, POS['1']]);
  await touchTap(page, '1');
  await until(() => page.kmx.length > idx && released(page), 6000, '1 released');
  const out = judge(page, idx, want);
  const a1 = await ariaPressed(page, 'caps-shift'), a2 = await ariaPressed(page, 'symbol-shift');
  await close(page);
  must(out === '', out);
  must(a1 === 'false' && a2 === 'false', 'still latched: ' + a1 + ' ' + a2);
});

row('TKB-09', 'a lone SS held 800 ms is a real hold: SS is down in the guest during it, and it does not latch (the next P is P alone)', async () => {
  const page = await kmxPage(PORTRAIT);
  await touchDown(page, 'symbol-shift');
  await sleep(400);
  const during = lastIs(page, new Set([POS.symbol]));
  await sleep(400);
  const during2 = lastIs(page, new Set([POS.symbol]));
  await touchUp(page);
  await until(() => released(page), 6000, 'SS released');
  const aria = await ariaPressed(page, 'symbol-shift');
  const pOut = await tapKey(page, 'p');
  await close(page);
  must(during && during2, 'SS not down in the guest during the hold: ' + last(page));
  must(aria === 'false', 'a long hold latched: ' + aria);
  must(pOut === '', 'the next P: ' + pOut);
});

row('TKB-10', 'two fingers: CS held and 5 tapped give exactly CS+5, and releasing CS does not latch it; Q and P held together are both down, releasing Q leaves P down', async () => {
  const page = await kmxPage(PORTRAIT);
  await touchDown(page, 'caps-shift', 1);
  await sleep(150);
  let idx = page.kmx.length;
  await touchDown(page, '5', 2); await sleep(120); await touchUp(page, 2);
  await until(() => lastIs(page, new Set([POS.caps])), 6000, 'CS alone again');
  const ls = page.kmx.slice(idx).map(pressed);
  const cs5 = new Set([POS.caps, POS['5']]);
  await touchUp(page, 1);
  await until(() => released(page), 6000, 'CS released');
  const aria = await ariaPressed(page, 'caps-shift');
  await sleep(200);
  await touchDown(page, 'q', 1); await touchDown(page, 'p', 2);
  const qp = new Set([POS.q, POS.p]);
  await until(() => lastIs(page, qp), 6000, 'Q and P down');
  await touchUp(page, 1);
  await until(() => lastIs(page, new Set([POS.p])), 6000, 'P alone');
  await touchUp(page, 2);
  await until(() => released(page), 6000, 'all released');
  await close(page);
  must(ls.some((s) => same(s, cs5)), 'no CS+5 line: ' + JSON.stringify(ls.map((s) => [...s])));
  must(aria === 'false', 'CS latched after a hold with another key: ' + aria);
});

row('TKB-11', 'a held key stays held: P held 1500 ms shows P down and no other line at 500, 1000 and 1400 ms; released after the touch ends', async () => {
  const page = await kmxPage(PORTRAIT);
  const idx = page.kmx.length;
  await touchDown(page, 'p');
  const t0 = Date.now();
  const seen = [];
  for (const t of [500, 1000, 1400]) {
    await sleep(Math.max(0, t - (Date.now() - t0)));
    seen.push(page.kmx.slice(idx).join(' | '));
  }
  const want = pressed(page.kmx[idx] || REL);
  await touchUp(page);
  await until(() => released(page), 6000, 'P released');
  await close(page);
  const p = new Set([POS.p]);
  must(same(want, p) && seen.every((s) => s === page.kmx[idx]), 'lines during the hold: ' + JSON.stringify(seen));
});

row('TKB-12', 'focus and gestures: the canvas keeps the focus through touch taps and mouse clicks, a physical key still reaches the program, the page does not scroll, touch-action is none, the context menu is cancelled', async () => {
  const page = await kmxPage(PORTRAIT);
  const focus = () => page.evaluate(() => document.activeElement && document.activeElement.tagName + ':' + document.activeElement.id);
  for (const k of ['q', '1', 'enter']) await touchTap(page, k, 100);
  const f1 = await focus();
  const idx = page.kmx.length;
  await page.keyboard.press('KeyO', { delay: 100 });
  await until(() => page.kmx.length > idx && released(page), 6000, 'a physical O');
  const o = judge(page, idx, new Set([POS.o]));
  const g = await page.evaluate(() => {
    const k = document.querySelector('#p0 [data-key="q"]');
    const ev = new MouseEvent('contextmenu', { bubbles: true, cancelable: true });
    k.dispatchEvent(ev);
    return { scroll: window.scrollY, ta: getComputedStyle(document.querySelector('#p0 .jnext-keyboard')).touchAction, kta: getComputedStyle(k).touchAction, prevented: ev.defaultPrevented };
  });
  await close(page);
  const d = await kmxPage(DESKTOP, { touchKeyboard: 'on' });
  for (const k of ['q', 'a']) {
    const c = await center(d, k);
    await d.mouse.move(c.x, c.y); await d.mouse.down(); await sleep(100); await d.mouse.up();
  }
  const f2 = await d.evaluate(() => document.activeElement && document.activeElement.tagName + ':' + document.activeElement.id);
  await close(d);
  must(/^CANVAS:jnext-canvas-/.test(f1) && /^CANVAS:jnext-canvas-/.test(f2), 'focus after touch taps ' + f1 + ', after mouse clicks ' + f2);
  must(o === '', 'physical O: ' + o);
  must(g.scroll === 0 && g.ta === 'none' && g.kta === 'none' && g.prevented, JSON.stringify(g));
});

row('TKB-13', 'showKeyboard(): a call before the start wins over the mode; true shows and shrinks the canvas; hiding releases a held key and clears the latches; a non-boolean throws a TypeError; after destroy() it throws', async () => {
  const page = await newPage(PORTRAIT);
  await open(page, [GUEST()]);
  await page.evaluate(() => window.player0.showKeyboard(false));
  await click(page);
  await until(() => page.kmxReady && page.kmx.length > 0, 300000, 'the program');
  const hidden = await page.evaluate(() => ({ vis: window.player0.keyboardVisible, n: document.querySelectorAll('.jnext-keyboard').length }));
  await page.evaluate(() => window.player0.showKeyboard(true));
  const geo = () => page.evaluate(() => {
    const box = document.querySelector('#p0 .jnext-player'), k = box.querySelector('.jnext-keyboard'), cv = box.querySelector('canvas');
    const c = cv.getBoundingClientRect();
    return { vis: window.player0.keyboardVisible, kbH: k.getBoundingClientRect().height, cvH: c.height, boxH: box.getBoundingClientRect().height, buf: [cv.width, cv.height], cvCss: [c.width, c.height] };
  });
  await until(async () => { const g = await geo(); return g.buf[1] === Math.round(g.cvCss[1]); }, 15000, 'the buffer to follow').catch(() => {});
  const shown = await geo();
  // (b) a key held when the keyboard is hidden is released in the program; a latch is cleared
  await touchDown(page, 'p');
  await until(() => lastIs(page, new Set([POS.p])), 6000, 'P down');
  await page.evaluate(() => window.player0.showKeyboard(false));
  await until(() => released(page), 6000, 'P released by the hide');
  await touchUp(page);
  await page.evaluate(() => window.player0.showKeyboard(true));
  await until(() => page.evaluate(() => !!document.querySelector('#p0 [data-key="caps-shift"]')), 6000, 'the keys');
  await touchTap(page, 'caps-shift');
  await until(() => released(page), 6000, 'CS tapped');
  await sleep(150);
  await page.evaluate(() => { window.player0.showKeyboard(false); window.player0.showKeyboard(true); });
  const zOut = await tapKey(page, 'z');
  const thrown = await page.evaluate(() => { try { window.player0.showKeyboard('yes'); return 'no throw'; } catch (e) { return e.name + ': ' + e.message; } });
  const back = await geo();
  await page.evaluate(() => window.player0.destroy());
  const after = await page.evaluate(() => { try { window.player0.showKeyboard(true); return 'no throw'; } catch (e) { return e.message; } });
  await close(page);
  must(hidden.vis === false && hidden.n === 0, '(a) before the start: ' + JSON.stringify(hidden));
  must(shown.vis === true && shown.kbH > 0 && Math.abs(shown.cvH + shown.kbH - shown.boxH) <= 1 && shown.buf[1] === Math.round(shown.cvCss[1]), '(a) shown: ' + JSON.stringify(shown));
  must(zOut === '', '(b) Z after a hide cleared the latch: ' + zOut);
  must(/^TypeError: .*showKeyboard\(\) takes a boolean/.test(thrown) && back.vis === true, '(c) ' + thrown);
  must(after === 'jnext: this player was destroyed', '(d) ' + after);
});

row('TKB-14', 'layout follows the width: 390x844 shows the compact layout (40 keys in 4 rows), 844x390 the full one (58 keys in 5 rows, ENTER two rows tall); the landscape keyboard is at most 45% of the box; every key is at least 24 px tall', async () => {
  const page = await plainPage(PORTRAIT);
  const probe = () => page.evaluate(() => {
    const k = document.querySelector('#p0 .jnext-keyboard'), box = document.querySelector('#p0 .jnext-player');
    const keys = [...k.querySelectorAll('[data-key]')].map((e) => ({ id: e.getAttribute('data-key'), r: e.getBoundingClientRect() }));
    const tops = [...new Set(keys.map((e) => Math.round(e.r.top)))];
    const enter = keys.find((e) => e.id === 'enter');
    const q = keys.find((e) => e.id === 'q');
    return { n: keys.length, rows: tops.length, minH: Math.min(...keys.map((e) => e.r.height)), kbH: k.getBoundingClientRect().height, boxH: box.getBoundingClientRect().height,
             enterH: enter.r.height, qH: q.r.height };
  });
  const p = await probe();
  await page.setViewportSize({ width: 844, height: 390 });
  await until(async () => (await probe()).n === 58, 15000, 'the full layout');
  const l = await probe();
  await page.setViewportSize({ width: 390, height: 844 });
  await until(async () => (await probe()).n === 40, 15000, 'the compact layout again');
  await close(page);
  must(p.n === 40 && p.rows === 4, 'portrait: ' + JSON.stringify(p));
  must(l.n === 58 && l.rows === 5, 'landscape: ' + JSON.stringify(l));
  must(l.enterH > 1.8 * l.qH, 'ENTER spans two rows: ' + l.enterH + ' vs Q ' + l.qH);
  must(l.kbH <= 0.45 * l.boxH + 1, 'landscape keyboard ' + l.kbH + ' of box ' + l.boxH);
  must(p.minH >= 24 && l.minH >= 24, 'key heights ' + p.minH + ' ' + l.minH);
});

row('TKB-15', 'accessibility: a labelled group, role=button keys with the spec\'s 58 names, aria-pressed on the shifts, a hidden keyboard display:none + aria-hidden; click() types the key once, a touch tap once', async () => {
  const page = await kmxPage(LANDSCAPE);
  const info = await page.evaluate(() => {
    const k = document.querySelector('#p0 .jnext-keyboard');
    return { role: k.getAttribute('role'), label: k.getAttribute('aria-label'),
             keys: [...k.querySelectorAll('[data-key]')].map((e) => [e.getAttribute('data-key'), e.getAttribute('role'), e.getAttribute('aria-label'), e.getAttribute('aria-pressed')]) };
  });
  const badKeys = info.keys.filter(([id, role, name, pressed]) =>
    role !== 'button' || name !== NAMES[id] || (MODS.includes(id) ? pressed !== 'false' : pressed !== null)).map((x) => x.join('/'));
  let idx = page.kmx.length;
  await page.evaluate(() => document.querySelector('#p0 [data-key="p"]').click());
  await until(() => page.kmx.length > idx && released(page), 6000, 'click() typed');
  await sleep(300);
  const viaClick = page.kmx.slice(idx);
  idx = page.kmx.length;
  await touchTap(page, 'o', 100);
  await until(() => page.kmx.length > idx && released(page), 6000, 'a touch tap typed');
  await sleep(400);
  const viaTouch = page.kmx.slice(idx);
  await page.evaluate(() => window.player0.showKeyboard(false));
  const hid = await page.evaluate(() => { const k = document.querySelector('#p0 .jnext-keyboard'); return [getComputedStyle(k).display, k.getAttribute('aria-hidden')]; });
  await close(page);
  must(info.role === 'group' && info.label, 'group: ' + info.role + ' ' + info.label);
  must(info.keys.length === 58 && badKeys.length === 0, 'keys: ' + info.keys.length + ' ' + badKeys.join(', '));
  must(viaClick.length === 2 && same(pressed(viaClick[0]), new Set([POS.p])) && viaClick[1] === REL, 'click(): ' + JSON.stringify(viaClick));
  must(viaTouch.length === 2 && same(pressed(viaTouch[0]), new Set([POS.o])) && viaTouch[1] === REL, 'touch tap: ' + JSON.stringify(viaTouch));
  must(hid.join() === 'none,true', 'hidden keyboard: ' + hid);
});

const widgetPage = (attrs, hostile = '') => `<!doctype html>${hostile ? `<style>${hostile}</style>` : ''}<body>` +
  `<div class="jnext-widget" data-image="${BURL}/card.img" data-sha256="${SHA}" data-file="${BURL}/red.nex" ${attrs}></div>` +
  `<script src="/build/jnext-widget.js"></script></body>`;
async function widgetOpen(name, html, ctxOpts = { viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 }) {
  vpages[name] = html;
  const ctx = await h.browser.newContext(ctxOpts);
  const page = await ctx.newPage();
  page.ctx = ctx; page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  await page.goto(`${AURL}/v/${name}`);
  await waitFor(page, () => {
    const e = document.querySelector('.jnext-widget');
    return e && e.jnextPlayer && e.querySelector('.jw-status').textContent === 'click the screen to start';
  }, null, 300000);
  await page.locator('.jnext-widget .jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent === 'running', null, 300000);
  return page;
}

row('TKB-16', 'widget: data-touch-keyboard="on" passes --touch-keyboard on, shows the keyboard inside the stage, leaves the toolbar as it was, and in real fullscreen lies between the title bar and the toolbar with the canvas above it', async () => {
  const page = await widgetOpen('w16.html', widgetPage('data-touch-keyboard="on"'));
  const geo = () => page.evaluate(() => {
    const e = document.querySelector('.jnext-widget'), r = (q) => { const b = e.querySelector(q).getBoundingClientRect(); return [b.left, b.top, b.width, b.height]; };
    const cv = e.querySelector('canvas');
    return { argv: e.jnextPlayer.argv, vis: e.jnextPlayer.keyboardVisible, inStage: !!e.querySelector('.jw-stage .jnext-keyboard'),
             buttons: [...e.querySelectorAll('.jw-btn')].map((b) => b.textContent), title: r('.jw-title'), stage: r('.jw-stage'), tools: r('.jw-tools'),
             kb: r('.jnext-keyboard'), canvas: r('canvas'), buf: [cv.width, cv.height], fs: document.fullscreenElement !== null };
  });
  const before = await geo();
  await page.locator('.jnext-widget [data-jw=fullscreen]').click();
  await waitFor(page, () => document.fullscreenElement && document.querySelector('.jnext-widget .jnext-player').getBoundingClientRect().width > 700, null, 60000);
  const settled = () => { const c = document.querySelector('.jnext-widget canvas'), b = c.getBoundingClientRect(); return c.width === Math.round(b.width) && c.height === Math.round(b.height); };
  await waitFor(page, settled, null, 15000).catch(() => {});
  const on = await geo();
  await page.evaluate(() => document.exitFullscreen());
  await page.context().close();
  must(before.argv.join(' ').includes('--touch-keyboard on') && before.vis === true && before.inStage, 'before: ' + JSON.stringify([before.argv, before.vis, before.inStage]));
  must(before.buttons.join() === 'Pause,Reset,Mute,Fullscreen', 'toolbar ' + before.buttons);
  must(on.fs && Math.abs((on.kb[1] + on.kb[3]) - on.tools[1]) < 2 && Math.abs(on.kb[1] - (on.canvas[1] + on.canvas[3])) < 2, 'fullscreen: keyboard ' + on.kb + ' canvas ' + on.canvas + ' toolbar ' + on.tools);
  must(Math.abs(on.canvas[1] - on.stage[1]) < 2 && on.kb[1] > on.title[1] + on.title[3] - 1, 'fullscreen: canvas ' + on.canvas + ' stage ' + on.stage + ' title ' + on.title);
  must(on.buf[0] === Math.round(on.canvas[2]) && on.buf[1] === Math.round(on.canvas[3]), 'fullscreen buffer ' + on.buf + ' vs canvas ' + on.canvas);
});

row('TKB-17', 'a hostile page stylesheet around a widget with the keyboard on: every key stays inside the keyboard, one row top per layout row, no padding, float, opacity or transform leaks; the widget stylesheet still has only .jnext-widget rules', async () => {
  const hostile = `*{box-sizing:content-box;color:#f00;font-size:40px;letter-spacing:5px}
div,span{display:inline;width:3px;padding:30px;margin:11px;border:5px dotted #f0f;background:#ff0;position:relative;float:right;min-height:700px;opacity:.6;transform:translateX(30px)}
canvas{filter:blur(2px);opacity:.5;border:4px solid #f0f}
button{display:block;width:100%;height:80px;padding:20px;margin:9px;background:#f00;color:#0f0;font:italic 40px serif}
body div,body button{text-transform:uppercase;line-height:90px}`;
  const page = await widgetOpen('w17.html', widgetPage('data-touch-keyboard="on"', hostile));
  const info = await page.evaluate(() => {
    const k = document.querySelector('.jnext-widget .jnext-keyboard'), kr = k.getBoundingClientRect();
    const keys = [...k.querySelectorAll('[data-key]')];
    const inside = keys.every((e) => { const r = e.getBoundingClientRect(); return r.left >= kr.left - 1 && r.right <= kr.right + 1 && r.top >= kr.top - 1 && r.bottom <= kr.bottom + 1; });
    const tops = [...new Set(keys.map((e) => Math.round(e.getBoundingClientRect().top)))];
    const looks = new Set(keys.map((e) => { const c = getComputedStyle(e); return [c.padding, c.float, c.opacity, c.transform, c.minHeight].join('|'); }));
    const kc = getComputedStyle(k);
    const sel = [];
    (function walk(rules) { for (const r of rules) { if (r.selectorText) sel.push(r.selectorText); else if (r.cssRules) walk(r.cssRules); } })(document.getElementById('jnext-widget-css').sheet.cssRules);
    return { n: keys.length, inside, rows: tops.length, looks: [...looks], kbLook: [kc.padding, kc.float, kc.opacity, kc.transform, kc.minHeight].join('|'), sel };
  });
  await page.context().close();
  must(info.n === 58 && info.inside, 'keys inside the keyboard: ' + JSON.stringify(info));
  must(info.rows === 5, 'row tops: ' + info.rows);
  must(info.looks.length === 1 && /^0px\|none\|1\|none\|(0px|auto)$/.test(info.looks[0]) && /^0px\|none\|1\|none\|(0px|auto)$/.test(info.kbLook), 'leaked: ' + info.looks + ' / ' + info.kbLook);
  must(info.sel.length > 10 && info.sel.every((s) => s.split(',').every((p) => p.trim().startsWith('.jnext-widget'))), 'rules outside .jnext-widget');
});

let pass = 0, fail = 0;
for (const r of rows) {
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 600000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 700)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
