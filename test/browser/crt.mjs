// Browser rows BCRT-01..13 (make wasm-browser-test, spec crt.mjs): the CRT effect of the browser
// build, crtemu on SDL's WebGL context. Run by browser-smoke.sh on the SDK's node; each row prints
// PASS|FAIL <ID> <text>, then the run ends with the `Total:` line.
//
// Oracles are the vendored shaders (third_party/crtemu/crtemu.h), not reference images: a GPU
// shader's pixels are not portable between WebGL implementations, so the rows pin STRUCTURE that
// the GLSL fixes exactly. Geometry: the TV and PC fragment shaders take curved_uv =
// mix(curve(uv), uv, 0.4 | 0.8) and multiply a pixel by 0.0 when curved_uv leaves [0,1], then by
// the modulate colour (white) and mix with the unused bezel at 0: exactly (0,0,0). On the middle
// row the glass edge is at x/640 = 0.01733 (TV, 11.1 px) and 0.00591 (PC, 3.8 px); the four
// corners are outside the glass in both. Lite has no curve. Lite scanlines are
// 0.5-0.5*cos(2*pi*uv.y*size.y), size.y = min(target_h/2, 256): every fragment of a 640x512
// target sits on a zero crossing (uniform dimming); at 1280x1024 the period is 4 device rows.
// The shadow mask is 1-0.23*clamp(mod(x+0.5,3)/2): 0.94 / 0.83 / 0.77 for x%3 = 0 / 1 / 2.
// The software scanlines are QColor(0,0,0,64) source-over on odd logical rows (the Qt overlay's
// rule, emulator_widget.cpp). The border colour is read at (0,0) of the native idle.png.
// The top border band is rows 0..62 of the 640x512 canvas (63 is blended with the first paper row by
// the renderer's LINEAR 1:2 filter). "Lit" = the largest channel is at least 16. Rows wait on emulator frame counts, never on wall
// time, except BCRT-08 (the effect's own clock). Measured values are printed as `# ...` lines.
import { Harness, pageUrl, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const MODULE = `jnext-${E.SMOKE_BUILD_VERSION}.js`;
const CARD = ['/scratch/card.img', '/media/card.img'];
const BASE = ['--machine', '48k', '--sdcard', '/media/card.img', '--silent'];

const h = new Harness({ build: BUILD, page: path.join(ROOT, 'test/browser/page'), scratch: SCRATCH });
await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };
const note = (id, text) => console.log(`# ${id} measured: ${text}`);

// ---- helpers -------------------------------------------------------------------------------
// Installed before the page's scripts run. `failCompile`: crtemu's CRT fragment shaders report a
// failed compile (the shader the effect is built from contains `uniform sampler2D blurbuffer`);
// `noGL`: the page has no WebGL at all. Every shader source SDL or crtemu hands to the context
// is recorded in window.__src.
function installHooks({ failCompile = false, noGL = false } = {}) {
  window.__src = [];
  const ss = WebGLRenderingContext.prototype.shaderSource;
  WebGLRenderingContext.prototype.shaderSource = function (shader, text) {
    window.__src.push(text); shader.__text = text; return ss.call(this, shader, text);
  };
  if (failCompile) {
    const gp = WebGLRenderingContext.prototype.getShaderParameter;
    WebGLRenderingContext.prototype.getShaderParameter = function (shader, pname) {
      if (pname === 0x8B81 && shader.__text && shader.__text.includes('uniform sampler2D blurbuffer')) return false;
      return gp.call(this, shader, pname);
    };
  }
  if (noGL) {
    const gc = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (t, ...a) { return /webgl/.test(t) ? null : gc.call(this, t, ...a); };
  }
}

async function newPage(ctxOpts = {}, hooks = {}) {
  const page = await h.newPage(ctxOpts);
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  await page.addInitScript(installHooks, hooks);
  return page;
}
let lastPage = null;
async function open(args, { ctx = {}, hooks = {}, canvas = '640x512' } = {}) {
  const page = await newPage(ctx, hooks);
  lastPage = page;
  await page.goto(pageUrl({ module: MODULE, args: [...BASE, ...args], files: [CARD], canvas }));
  await waitFor(page, () => window.jnextTest.ready || window.jnextTest.error);
  const err = await page.evaluate(() => window.jnextTest.error);
  must(!err, 'page error: ' + err);
  await page.click('#overlay', { timeout: 600000, noWaitAfter: true });
  return page;
}
const stderr = (page) => page.evaluate(() => window.jnextTest.stderr);
const waitFrame = (page, n, ms = 240000) =>
  waitFor(page, (n) => window.jnextModule._jnext_web_frame() >= n, n, ms);
const built = (page) => page.evaluate(() => window.__src.some((t) => t.includes('uniform sampler2D blurbuffer')));
const glError = (page, sel = '#jnext-canvas') => page.evaluate((sel) => {
  const gl = document.querySelector(sel).getContext('webgl');
  return gl ? gl.getError() : 'no webgl context';
}, sel);
const glMessages = (page) => page.msgs.filter((m) => /WebGL:|INVALID_|GL_INVALID/.test(m));
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
const shot = async (page, sel = '#jnext-canvas') => decode(await page.locator(sel).screenshot());
const px = (im, x, y) => [im.d[(y * im.w + x) * 4], im.d[(y * im.w + x) * 4 + 1], im.d[(y * im.w + x) * 4 + 2]];
const eq = (a, b, tol = 0) => a.every((v, i) => Math.abs(v - b[i]) <= tol);
const black = (c) => c[0] === 0 && c[1] === 0 && c[2] === 0;
const lit = (c) => Math.max(c[0], c[1], c[2]) >= 16;
const readBorder = async () => {
  const f = path.join(ORACLE, 'idle.png');
  return px(await decode(fs.readFileSync(f)), 0, 0);
};
// Hold each key for 5 emulated frames and leave 5 between keys: with the effect on, a page can run
// well below 50 frames a second, and a key held for a wall-clock 60 ms would then be gone before
// the ROM's keyboard scan sees it.
const frameNow = (page) => page.evaluate(() => window.jnextModule._jnext_web_frame());
const typeKeys = async (page, keys) => {
  for (const k of keys) {
    await page.keyboard.down(k);
    await waitFrame(page, (await frameNow(page)) + 5);
    await page.keyboard.up(k);
    await waitFrame(page, (await frameNow(page)) + 5);
  }
};
// every pixel of rows [y0,y1], cols [x0,x1] satisfies ok(colour, x, y); returns the first violation
function bandFirstBad(im, y0, y1, x0, x1, ok) {
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) if (!ok(px(im, x, y), x, y)) return `(${x},${y}) = ${px(im, x, y)}`;
  return null;
}
const plainBand = (im, border) => bandFirstBad(im, 0, 62, 0, 639, (c) => eq(c, border, 1));
// The scanlines cover the whole height: the top band, and the left border (cols 0..60) down all 512 rows.
const scanOk = (border) => (c, x, y) => eq(c, (y & 1) ? border.map((v) => Math.round(v * 191 / 255)) : border, 1);
const scanBand = (im, border) => bandFirstBad(im, 0, 62, 0, 639, scanOk(border)) ||
                                 bandFirstBad(im, 0, im.h - 1, 0, 60, scanOk(border));
const noEffectWarning = (lines) => !lines.some((l) => /has no effect|CRT effect:/.test(l));
const effectLines = (lines) => lines.filter((l) => /CRT effect:/.test(l));
const corners = (im) => [[0, 0], [im.w - 1, 0], [0, im.h - 1], [im.w - 1, im.h - 1]];
const mean = (im, pts) => pts.reduce((a, [x, y]) => a + (px(im, x, y).reduce((s, v) => s + v, 0) / 3), 0) / pts.length;

// ---- rows ------------------------------------------------------------------------------------
row('BCRT-01', 'off by default and with --crt off: no crtemu shader is built, the top border band is the plain border colour, no GL error', async () => {
  const border = await readBorder();
  // the last CRT setting wins: tv (or scanlines) then off is off
  for (const args of [[], ['--crt', 'off'], ['--crt', 'tv', '--crt', 'off'], ['--crt', 'scanlines', '--crt', 'off']]) {
    const page = await open(args);
    await waitFrame(page, 60);
    const im = await shot(page);
    must(im.w === 640 && im.h === 512, `canvas ${im.w}x${im.h}`);
    must(!(await built(page)), `${args}: crtemu was built`);
    const bad = plainBand(im, border);
    must(!bad, `${args}: band pixel ${bad}, border ${border}`);
    const err = await glError(page);
    must(err === 0, `${args}: getError ${err}`);
    must(noEffectWarning(await stderr(page)), `${args}: a CRT or has-no-effect line was printed`);
    await page.ctx.close();
  }
});

row('BCRT-02', '--crt scanlines: no crtemu, odd logical rows darkened by 25% (alpha 64), even rows untouched, no "has no effect" warning', async () => {
  const border = await readBorder();
  // the last CRT setting wins: the shader set first, then scanlines, is scanlines
  for (const args of [['--crt', 'scanlines'], ['--set', 'startup.crt_shader=true', '--crt', 'scanlines']]) {
    const page = await open(args);
    await waitFrame(page, 60);
    const im = await shot(page);
    must(!(await built(page)), `${args}: crtemu was built`);
    const bad = scanBand(im, border);
    must(!bad, `${args}: pixel ${bad}, border ${border}`);
    must(noEffectWarning(await stderr(page)), `${args}: a CRT or has-no-effect line was printed`);
    must((await glError(page)) === 0, `${args}: GL error`);
    await page.ctx.close();
  }
});

async function tvGeometry(page, who, edgeBlack, edgeLit) {
  await waitFrame(page, 160);
  must(await built(page), `${who.name}: crtemu was not built`);
  const im = await shot(page);
  const y = im.h >> 1;
  for (let x = 0; x <= edgeBlack; x++) must(black(px(im, x, y)), `${who.name}: (${x},${y}) = ${px(im, x, y)}, not black`);
  for (const x of edgeLit) must(lit(px(im, x, y)), `${who.name}: (${x},${y}) = ${px(im, x, y)}, not lit`);
  for (const [x, yy] of corners(im)) must(black(px(im, x, yy)), `${who.name}: corner (${x},${yy}) = ${px(im, x, yy)}`);
  must(lit(px(im, im.w >> 1, im.h >> 1)), `${who.name}: centre ${px(im, im.w >> 1, im.h >> 1)} not lit`);
  note(who.id, `row ${y}: ${[0, 5, 9, 10, 11, 12, 13, 20].map((x) => `${x}:${px(im, x, y)}`).join(' ')} centre ${px(im, im.w >> 1, im.h >> 1)}`);
  return im;
}

row('BCRT-03', '--crt tv: crtemu runs on SDL\'s context with no GL error; exact black outside the curved glass (middle row x 0..9, the four corners), lit inside, centre lit', async () => {
  // the last CRT setting wins: off then tv is tv
  const later = await open(['--crt', 'off', '--crt', 'tv']);
  await waitFrame(later, 100);
  must(await built(later), 'off then tv: crtemu was not built');
  await later.ctx.close();
  const page = await open(['--crt', 'tv']);
  await tvGeometry(page, { name: 'tv', id: 'BCRT-03' }, 9, [13, 14, 15, 16, 17, 18, 19, 20]);
  const err = await glError(page);
  must(err === 0, `getError ${err}`);
  must(glMessages(page).length === 0, 'WebGL messages: ' + JSON.stringify(glMessages(page)));
  must(noEffectWarning(await stderr(page)), 'a CRT or has-no-effect line was printed');
});

row('BCRT-04', '--crt pc: the glass edge is nearer than the TV\'s (x 0..2 black, x 6..12 lit, where TV x 8 is black), corners black', async () => {
  const page = await open(['--crt', 'pc']);
  await tvGeometry(page, { name: 'pc', id: 'BCRT-04' }, 2, [6, 7, 8, 9, 10, 11, 12]);
  must((await glError(page)) === 0, 'GL error');
  // the last style wins: tv then pc is pc; a saved pc style then --crt tv is tv (x 8 is black in TV)
  const later = await open(['--crt', 'tv', '--crt', 'pc']);
  await tvGeometry(later, { name: 'tv then pc', id: 'BCRT-04' }, 2, [6, 7, 8]);
  await later.ctx.close();
  const tv = await open(['--set', 'startup.crt_shader_style=1', '--crt', 'tv']);
  await waitFrame(tv, 160);
  const im = await shot(tv);
  must(black(px(im, 8, im.h >> 1)), `pc style then --crt tv: (8,256) = ${px(im, 8, im.h >> 1)}, not black`);
});

row('BCRT-05', '--crt soft at 1280x1024: no curve (corners lit), scanline period 4 device rows, shadow mask period 3 columns', async () => {
  const page = await open(['--crt', 'soft'], { ctx: { deviceScaleFactor: 2 } });
  await waitFrame(page, 160);
  must(await built(page), 'crtemu was not built');
  const im = await shot(page);
  must(im.w === 1280 && im.h === 1024, `canvas ${im.w}x${im.h}`);
  for (const [x, y] of corners(im)) must(lit(px(im, x, y)), `corner (${x},${y}) = ${px(im, x, y)} not lit`);
  const sel = (f) => { const pts = []; for (let y = 0; y < 124; y++) for (let x = 0; x < 1280; x++) if (f(x, y)) pts.push([x, y]); return pts; };
  const on = mean(im, sel((x, y) => (y & 3) === 1 || (y & 3) === 2));
  const off = mean(im, sel((x, y) => (y & 3) === 0 || (y & 3) === 3));
  const c0 = mean(im, sel((x) => x % 3 === 0)), c2 = mean(im, sel((x) => x % 3 === 2));
  note('BCRT-05', `scanline rows ${on.toFixed(1)} / ${off.toFixed(1)} = ${(on / off).toFixed(2)}; mask columns ${c0.toFixed(1)} / ${c2.toFixed(1)} = ${(c0 / c2).toFixed(2)}`);
  must(on >= 1.2 * off, `scanline rows ${on} vs ${off}`);
  must(c0 > 1.05 * c2, `mask columns ${c0} vs ${c2}`);
});

row('BCRT-06', '--crt tv: a red border stays red (the framebuffer\'s ARGB reaches the shader as RGBA), BORDER 2 typed at frame 200', async () => {
  const page = await open(['--crt', 'tv']);
  await waitFrame(page, 200);
  await typeKeys(page, ['b', '2', 'Enter']);
  // The keys reach the emulator on its own clock: look every 20 frames, for 300 frames at most.
  let c = null;
  for (let f = 240; f <= 540; f += 20) {
    await waitFrame(page, f);
    c = px(await shot(page), 320, 30);
    if (c[0] >= 96) break;
  }
  note('BCRT-06', `(320,30) = ${c}`);
  must(c[0] >= 96 && c[0] >= 3 * Math.max(c[1], c[2]), `(320,30) = ${c}`);
});

row('BCRT-07', '--crt soft in an 800x600 and a 640x600 canvas: the picture fills exactly SDL\'s letterbox rectangle, black bars outside', async () => {
  for (const [w, hh] of [[800, 600], [640, 600]]) {
    const page = await open(['--crt', 'soft'], { canvas: `${w}x${hh}` });
    await waitFrame(page, 160);
    const im = await shot(page);
    must(im.w === w && im.h === hh, `canvas ${im.w}x${im.h}`);
    if (w === 800) {
      const y = 300;
      for (const x of [0, 24, 775, 799]) must(black(px(im, x, y)), `bar (${x},${y}) = ${px(im, x, y)}`);
      for (const x of [26, 27, 28, 29, 30, 769, 770, 771, 772, 773]) must(lit(px(im, x, y)), `picture (${x},${y}) = ${px(im, x, y)}`);
      note('BCRT-07', `800x600 y=300: ${[24, 25, 26, 27, 774, 775].map((x) => `${x}:${px(im, x, y)}`).join(' ')}`);
    } else {
      const x = 320;
      for (const y of [0, 43, 556, 599]) must(black(px(im, x, y)), `bar (${x},${y}) = ${px(im, x, y)}`);
      for (const y of [45, 46, 47, 48, 49, 50, 549, 550, 551, 552, 553, 554]) must(lit(px(im, x, y)), `picture (${x},${y}) = ${px(im, x, y)}`);
      note('BCRT-07', `640x600 x=320: ${[43, 44, 45, 46, 554, 555, 556].map((y) => `${y}:${px(im, x, y)}`).join(' ')}`);
      // The scanline period follows the picture's own height (a viewport that does not start at
      // the origin must not shift it): at 640x512 every row sits on a zero crossing, so the plain
      // border colour is dimmed evenly down a column (measured spread 6 with the viewport fix, 84 without).
      const col = []; for (let y = 48; y <= 100; y++) col.push(px(im, 300, y)[0]);
      note('BCRT-07', `640x600 x=300 y=48..100: min ${Math.min(...col)} max ${Math.max(...col)}`);
      must(Math.max(...col) - Math.min(...col) <= 12, `the border column varies ${Math.min(...col)}..${Math.max(...col)} down the picture`);
    }
    await page.ctx.close();
  }
});

row('BCRT-08', '--crt tv: the effect has its own clock: two captures 600 ms apart differ in the border (noise and flicker)', async () => {
  const page = await open(['--crt', 'tv']);
  await waitFrame(page, 150);
  const a = await shot(page);
  await sleep(700);
  const b = await shot(page);
  let n = 0, tot = 0;
  for (let y = 8; y <= 56; y++) for (let x = 100; x <= 540; x++) { tot++; if (!eq(px(a, x, y), px(b, x, y))) n++; }
  note('BCRT-08', `${n} of ${tot} pixels differ (${(100 * n / tot).toFixed(1)}%)`);
  must(n >= 0.05 * tot, `${n} of ${tot} pixels differ`);
});

async function fallbackRow(page, border, wantText, pattern) {
  await waitFrame(page, 60);
  const im = await shot(page);
  const lines = effectLines(await stderr(page));
  must(lines.length === 1 && wantText.test(lines[0]), `CRT lines ${JSON.stringify(lines)}`);
  const bad = pattern === 'plain' ? plainBand(im, border) : scanBand(im, border);
  must(!bad, `band pixel ${bad}, border ${border}`);
  return im;
}

row('BCRT-09', '--crt tv when the shader cannot compile: one "CRT effect:" line, the run continues with the plain display', async () => {
  const border = await readBorder();
  const page = await open(['--crt', 'tv'], { hooks: { failCompile: true } });
  const im = await fallbackRow(page, border, /^.*CRT effect: .* continues without it\.$/, 'plain');
  for (const [x, y] of corners(im)) must(eq(px(im, x, y), border, 1), `corner (${x},${y}) = ${px(im, x, y)}`);
});

row('BCRT-10', '--crt soft when the shader cannot compile: one "CRT effect:" line, the display continues with the software scanlines', async () => {
  const border = await readBorder();
  const page = await open(['--crt', 'soft'], { hooks: { failCompile: true } });
  await fallbackRow(page, border, /^.*CRT effect: .* continues with the scanlines\.$/, 'scan');
});

// The JavaScript API fixture (test/browser/page/api.html), the card served from the same origin.
async function apiOpen(options) {
  const page = await newPage();
  lastPage = page;
  const q = new URLSearchParams({
    module: '/build/' + MODULE,
    image: JSON.stringify({ url: '/scratch/card.img', sha256: E.SMOKE_CARD_SHA256 }),
    players: JSON.stringify([{ machine: '48k', muted: true, width: 640, height: 512, ...options }]),
  });
  await page.goto('http://localhost/page/api.html?' + q.toString());
  await waitFor(page, () => window.apiTest && window.apiTest.players.length === 1 && window.apiTest.players[0].settled);
  const r = await page.evaluate(() => ({ err: window.apiTest.players[0].mountError }));
  must(!r.err, 'mount: ' + r.err);
  await page.click('#p0 .jnext-overlay', { timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => window.apiTest.players[0].ready >= 1 || window.apiTest.players[0].errors.length > 0, null, 300000);
  return page;
}

row('BCRT-11', 'the JavaScript API reaches the effect through the existing options: set startup.crt_shader / crt_shader_style (PC geometry) and args --crt tv (TV geometry)', async () => {
  const a = await apiOpen({ set: { 'startup.crt_shader': true, 'startup.crt_shader_style': 1 } });
  const argvA = await a.evaluate(() => window.player0.argv);
  const i = argvA.indexOf('startup.crt_shader=true');
  must(i > 0 && argvA[i - 1] === '--set' && argvA[i + 1] === '--set' && argvA[i + 2] === 'startup.crt_shader_style=1', 'argv ' + JSON.stringify(argvA));
  await waitFor(a, () => window.player0._module._jnext_web_frame() >= 160);
  must(await built(a), 'set: crtemu was not built');
  const ia = await shot(a, '#p0 canvas');
  must(ia.w === 640 && ia.h === 512, `canvas ${ia.w}x${ia.h}`);
  const y = 256;
  for (let x = 0; x <= 2; x++) must(black(px(ia, x, y)), `set: (${x},${y}) = ${px(ia, x, y)}`);
  for (const x of [6, 7, 8, 9, 10, 11, 12]) must(lit(px(ia, x, y)), `set: (${x},${y}) = ${px(ia, x, y)}`);
  await a.ctx.close();

  const b = await apiOpen({ args: ['--crt', 'tv'] });
  const argvB = await b.evaluate(() => window.player0.argv);
  must(argvB.slice(-2).join(' ') === '--crt tv', 'argv ' + JSON.stringify(argvB));
  await waitFor(b, () => window.player0._module._jnext_web_frame() >= 160);
  must(await built(b), 'args: crtemu was not built');
  const ib = await shot(b, '#p0 canvas');
  for (let x = 0; x <= 9; x++) must(black(px(ib, x, y)), `args: (${x},${y}) = ${px(ib, x, y)}`);
  for (const x of [13, 14, 15, 16, 17, 18, 19, 20]) must(lit(px(ib, x, y)), `args: (${x},${y}) = ${px(ib, x, y)}`);
  await b.ctx.close();
});

row('BCRT-12', 'destroy() of a player running the effect: state destroyed, no page error, no WebGL error (crtemu is destroyed before SDL\'s context)', async () => {
  const page = await apiOpen({ args: ['--crt', 'tv'] });
  await waitFor(page, () => window.player0._module._jnext_web_frame() >= 100);
  must(await built(page), 'crtemu was not built');
  const n = page.msgs.length;
  await page.evaluate(() => window.player0.destroy());
  await sleep(500);
  const state = await page.evaluate(() => window.player0.state);
  const after = page.msgs.slice(n);
  const errs = await page.evaluate(() => window.apiTest.errors);
  must(state === 'destroyed', 'state ' + state);
  must(!after.some((m) => /^PAGEERROR/.test(m) || /WebGL:|INVALID_|GL_INVALID|Uncaught/.test(m)), 'after destroy: ' + JSON.stringify(after));
  must(errs.length === 0, 'page errors ' + JSON.stringify(errs));
});

row('BCRT-13', 'a page with no WebGL at all: --crt tv prints one "CRT effect: no WebGL renderer" line and the display is plain; --crt soft continues with the scanlines', async () => {
  const border = await readBorder();
  const a = await open(['--crt', 'tv'], { hooks: { noGL: true } });
  await fallbackRow(a, border, /CRT effect: no WebGL renderer; the display continues without it\.$/, 'plain');
  must(!(await built(a)), 'crtemu was built without WebGL');
  await a.ctx.close();
  const b = await open(['--crt', 'soft'], { hooks: { noGL: true } });
  await fallbackRow(b, border, /CRT effect: no WebGL renderer; the display continues with the scanlines\.$/, 'scan');
});

// ---- run --------------------------------------------------------------------------------------
let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  let err = null;
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 400000))]);
  } catch (e) { err = e; }
  if (err && lastPage) {
    const tail = await lastPage.evaluate(() => (window.jnextTest ? window.jnextTest.stderr.slice(-6) : [])).catch(() => []);
    err.message += ' | stderr tail: ' + JSON.stringify(tail) + ' | console tail: ' + JSON.stringify((lastPage.msgs || []).slice(-4));
  }
  for (const c of h.browser.contexts()) if (c !== util.context()) await c.close().catch(() => {});
  if (err) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(err.message).replace(/\n/g, ' ').slice(0, 600)}]`); }
  else { pass++; console.log(`PASS ${r.id} ${r.text}`); }
}
await h.close();
console.log('\n====================================================');
console.log(`Total: ${String(pass + fail).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
