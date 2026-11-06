// Browser smoke rows BRW-01..26 (make wasm-browser-test), run by browser-smoke.sh
// on the SDK's node. Each row prints PASS|FAIL <ID> <text>; the run ends with the
// suite's `Total:` line and exits non-zero on any FAIL. Oracles are NATIVE runs
// of the same arguments, made by browser-smoke.sh into $SMOKE_ORACLE (build/
// sdl-release on the dummy SDL drivers), the committed reference images where
// they hold, and arithmetic on SDL's letterbox and the renderer's LINEAR filter.
import { Harness, pageUrl, waitFor, frame, sleep, installAudioHook, audioInfo } from './harness.mjs';
import fs from 'fs';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE;
const OUT = E.SMOKE_OUT, VERSION = E.SMOKE_VERSION;
const MODULE = `jnext-${E.SMOKE_BUILD_VERSION}.js`;
const TITLE = 'jnext browser smoke fixture';
const CARD = ['/scratch/card.img', '/media/card.img'];
const TAP = ['/tap/beeper_demo.tap', '/media/beeper_demo.tap'];
const BASE = ['--machine', '48k', '--sdcard', '/media/card.img', '--silent'];
const shotArgs = (frames) => ['--delayed-screenshot', '/media/s.png', '--delayed-screenshot-frames', String(frames)];

const h = new Harness({ build: BUILD, page: path.join(ROOT, 'test/browser/page'),
                        nex: path.join(ROOT, 'test/00regression/nex'), tap: path.join(ROOT, 'test/00regression/tap'),
                        scratch: SCRATCH });
await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs; never sees the game
await util.goto('about:blank');

const results = [];
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

// ---- helpers -------------------------------------------------------------------------------
let lastPage = null;
async function open(cfg, ctxOpts = {}, { report = false } = {}) {
  const page = await h.newPage(ctxOpts);
  lastPage = page;
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  await installAudioHook(page, report);
  await page.goto(pageUrl({ module: MODULE, ...cfg }));
  await waitFor(page, () => window.jnextTest.ready || window.jnextTest.error);
  const err = await page.evaluate(() => window.jnextTest.error);
  must(!err, 'page error: ' + err);
  // INVOKE_RUN=0: nothing runs until the page's callMain, so nothing has been logged yet.
  const early = await page.evaluate(() => window.jnextTest.stderr.concat(window.jnextTest.stdout));
  must(early.length === 0, 'the module ran before callMain: ' + JSON.stringify(early.slice(0, 2)));
  return page;
}
// main() runs inside this click and a first NEX load blocks the page for many seconds.
const start = async (page) => { await page.click('#overlay', { timeout: 600000, noWaitAfter: true }); };
const stderr = (page) => page.evaluate(() => window.jnextTest.stderr);
const stdout = (page) => page.evaluate(() => window.jnextTest.stdout);
const waitLog = (page, re, ms = 120000) =>
  waitFor(page, (src) => window.jnextTest.stderr.some((l) => new RegExp(src).test(l)), re.source, ms);
const waitFrame = (page, n, ms = 120000) =>
  waitFor(page, (n) => window.jnextModule._jnext_web_frame() >= n, n, ms);
const waitExit = async (page, ms = 120000) => {
  // main() returns at once once the loop is installed: the run's own end is onExit; a
  // callMain that returned with no loop installed (a refusal) is its return value.
  await waitFor(page, () => window.jnextTest.exit !== null || window.jnextTest.error ||
                            (window.jnextTest.mainResult !== undefined && !window.jnextModule._jnext_web_loop_active()), null, ms);
  return page.evaluate(() => (window.jnextTest.exit !== null ? window.jnextTest.exit : window.jnextTest.mainResult));
};
async function grab(page, memfsPath) {
  const b64 = await page.evaluate((p) => {
    const d = window.jnextModule.FS.readFile(p); let s = '';
    for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192));
    return btoa(s);
  }, memfsPath);
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
const readPng = (file) => decode(fs.readFileSync(file));
function diff(a, b) {
  if (a.w !== b.w || a.h !== b.h) return `size ${a.w}x${a.h} vs ${b.w}x${b.h}`;
  let n = 0;
  for (let i = 0; i < a.d.length; i += 4) if (a.d[i] !== b.d[i] || a.d[i + 1] !== b.d[i + 1] || a.d[i + 2] !== b.d[i + 2]) n++;
  return n;
}
const px = (im, x, y) => [im.d[(y * im.w + x) * 4], im.d[(y * im.w + x) * 4 + 1], im.d[(y * im.w + x) * 4 + 2]];
const eq = (a, b, tol = 0) => a.every((v, i) => Math.abs(v - b[i]) <= tol);
async function shotEquals(page, memfs, oracleFile, label) {
  const got = await decode(await grab(page, memfs));
  const want = await readPng(oracleFile);
  const d = diff(got, want);
  must(d === 0, `${label}: ${d} differing pixels vs ${path.basename(oracleFile)}`);
}
const refImg = (n) => path.join(ROOT, 'test/00regression/img', n);
const orc = (n) => path.join(ORACLE, n);
const typeKeys = async (page, keys) => { for (const k of keys) { await page.keyboard.press(k, { delay: 60 }); await sleep(120); } };

// ---- rows ------------------------------------------------------------------------------------
row('BRW-01', 'callMain --version prints "jnext <version.yaml version>" and exits 0', async () => {
  const page = await open({ args: ['--version'] });
  await start(page);
  const code = await waitExit(page);
  const out = await stdout(page);
  must(code === 0 && out.join('\n') === `jnext ${VERSION}`, `exit=${code} out=${JSON.stringify(out)}`);
});

row('BRW-02', '48K boot at frame 150 is pixel-identical to the native SDL run and boot-48k-reference.png', async () => {
  const page = await open({ args: [...BASE, ...shotArgs(150)], files: [CARD], canvas: '640x512' });
  await start(page); await waitLog(page, /screenshot saved/);
  await shotEquals(page, '/media/s.png', orc('boot.png'), 'native SDL');
  await shotEquals(page, '/media/s.png', refImg('boot-48k-reference.png'), 'reference');
});

row('BRW-03', 'Next + minimal card + tilemap_demo.nex at frame 150 equals the native run; the warm-start state is NextZXOS-resident', async () => {
  const page = await open({ args: ['--machine', 'next', '--sdcard', '/media/card.img', '--silent', '--rtc', '2026-07-10T08:55:00',
                                   '--load', '/media/tilemap_demo.nex', ...shotArgs(150)],
                           files: [CARD, ['/nex/tilemap_demo.nex', '/media/tilemap_demo.nex']], canvas: '640x512' });
  await start(page); await waitLog(page, /screenshot saved/, 300000);
  await shotEquals(page, '/media/s.png', orc('tilemap.png'), 'native SDL');
  const log = (await stderr(page)).join('\n');
  must(/recorded a NextZXOS-resident machine/.test(log) && !/synthetic machine/.test(log), 'warm-start log lines');
});

row('BRW-04', '48K beeper_demo.tap at frame 300 equals the native run and tap-demo-reference.png', async () => {
  const page = await open({ args: [...BASE, '--load', '/media/beeper_demo.tap', ...shotArgs(300)], files: [CARD, TAP], canvas: '640x512' });
  await start(page); await waitLog(page, /screenshot saved/);
  await shotEquals(page, '/media/s.png', orc('tap.png'), 'native SDL');
  await shotEquals(page, '/media/s.png', refImg('tap-demo-reference.png'), 'reference');
});

row('BRW-05', '48K TZX made from beeper_demo.tap at frame 400 equals the native run of the same TZX', async () => {
  const page = await open({ args: [...BASE, '--load', '/media/beeper_demo.tzx', ...shotArgs(400)],
                            files: [CARD, ['/scratch/beeper_demo.tzx', '/media/beeper_demo.tzx']], canvas: '640x512' });
  await start(page); await waitLog(page, /screenshot saved/);
  await shotEquals(page, '/media/s.png', orc('tzx.png'), 'native SDL');
});

row('BRW-06', 'SDL renders through WebGL (no 2D context, a WebGL one)', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 20);
  const r = await page.evaluate(() => { const c = document.getElementById('jnext-canvas'); return { c2d: c.getContext('2d') === null, gl: c.getContext('webgl') !== null }; });
  must(r.c2d && r.gl, JSON.stringify(r));
});

// The 12-screen shared by BRW-07 and BRW-12.
async function typedScreen(page) {
  await waitFrame(page, 200);
  await typeKeys(page, ['p', '1', '2', 'Enter']);
  await waitLog(page, /screenshot saved/);
}
const KEYED = [...BASE, ...shotArgs(320)];

row('BRW-07', 'a 640x512 canvas shows the framebuffer through the LINEAR 1:2 vertical filter', async () => {
  const page = await open({ args: KEYED, files: [CARD], canvas: '640x512' });
  await start(page); await typedScreen(page);
  const fbi = await decode(await grab(page, '/media/s.png'));       // 640x512, rows doubled: row y>>1 of a 640x256 texture
  const cv = await decode(await page.locator('#jnext-canvas').screenshot());
  must(cv.w === 640 && cv.h === 512 && fbi.w === 640 && fbi.h === 512, `sizes ${cv.w}x${cv.h} ${fbi.w}x${fbi.h}`);
  let bad = 0, first = '';
  for (let y = 0; y < 512; y++) {
    const r = y >> 1, nb = Math.min(255, Math.max(0, (y & 1) ? r + 1 : r - 1));
    for (let x = 0; x < 640; x++) {
      const a = px(fbi, x, 2 * r), b = px(fbi, x, 2 * nb), c = px(cv, x, y);
      let ok;
      if (eq(a, b)) ok = eq(c, a);
      else ok = c.every((v, i) => v >= Math.min(a[i], b[i]) && v <= Math.max(a[i], b[i]));
      if (!ok) { bad++; if (!first) first = `(${x},${y}) got ${c} rows ${a} / ${b}`; }
    }
  }
  must(bad === 0, `${bad} pixels violate the linear rule, first ${first}`);
});

row('BRW-08', 'the 640x512 logical screen is letterboxed in 800x600 (side bars) and 640x600 (top/bottom bars)', async () => {
  const border = px(await readPng(orc('idle.png')), 0, 0);
  for (const [w, hh] of [[800, 600], [640, 600]]) {
    const page = await open({ args: BASE, files: [CARD], canvas: `${w}x${hh}` });
    await start(page); await waitFrame(page, 60);
    const im = await decode(await page.locator('#jnext-canvas').screenshot());
    must(im.w === w && im.h === hh, `canvas ${im.w}x${im.h}`);
    if (w === 800) {
      const y = 300;
      for (const x of [0, 24, 775, 799]) must(eq(px(im, x, y), [0, 0, 0]), `bar pixel (${x},${y}) = ${px(im, x, y)}`);
      for (const x of [25, 774]) must(eq(px(im, x, y), border, 1), `edge pixel (${x},${y}) = ${px(im, x, y)}, border ${border}`);
    } else {
      const x = 320;
      for (const y of [0, 43, 556, 599]) must(eq(px(im, x, y), [0, 0, 0]), `bar pixel (${x},${y}) = ${px(im, x, y)}`);
      for (const y of [44, 555]) must(eq(px(im, x, y), border, 1), `edge pixel (${x},${y}) = ${px(im, x, y)}, border ${border}`);
    }
    await page.ctx.close();
  }
});

row('BRW-09', 'deviceScaleFactor 2 renders the canvas at device pixels (1280x1024 for a 640x512 CSS box)', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' }, { deviceScaleFactor: 2 });
  await start(page); await waitFrame(page, 20);
  const r = await page.evaluate(() => { const c = document.getElementById('jnext-canvas'); return [c.width, c.height]; });
  must(r[0] === 1280 && r[1] === 1024, JSON.stringify(r));
});

row('BRW-10', 'the canvas follows its CSS size on a window resize (RESIZABLE)', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: 'display:block;width:50vw;height:40vw' }, { viewport: { width: 1000, height: 800 } });
  await start(page); await waitFrame(page, 20);
  const a = await page.evaluate(() => document.getElementById('jnext-canvas').width);
  await page.setViewportSize({ width: 1600, height: 1000 });
  await waitFor(page, () => document.getElementById('jnext-canvas').width === 800, null, 10000);
  const b = await page.evaluate(() => document.getElementById('jnext-canvas').width);
  must(a === 500 && b === 800, `widths ${a} -> ${b}`);
});

row('BRW-11', 'the page keeps its own document.title', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 20);
  const t = await page.evaluate(() => document.title);
  must(t === TITLE, `title ${JSON.stringify(t)}`);
});

row('BRW-12', 'DOM keys P 1 2 Enter on the focused canvas reach the guest (frame 320 == native typist)', async () => {
  const page = await open({ args: KEYED, files: [CARD], canvas: '640x512' });
  await start(page); await typedScreen(page);
  await shotEquals(page, '/media/s.png', orc('keys.png'), 'native typist');
});

// A held key and a lost focus: the key-up never reaches the canvas.
async function heldKeyThenLose(lose) {
  const page = await open({ args: [...BASE, ...shotArgs(400)], files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 200);
  await page.keyboard.down('1');
  const f = await frame(page);
  await waitFrame(page, f + 3);
  await lose(page);
  await page.keyboard.up('1');
  await waitFrame(page, f + 100);                                   // well past the key-repeat delay
  await waitLog(page, /screenshot saved/);
  await shotEquals(page, '/media/s.png', orc('key1.png'), 'single 1 tap');
}
row('BRW-13', 'a key held while the canvas loses focus to a page input is released (no run of 1s); a key pressed in the very task that moves the focus is never typed',
    async () => {
      await heldKeyThenLose((page) => page.focus('#textbox'));
      // The release is then held back by the key latch, and only the canvas-blur listener's
      // FOCUS_LOST (release_all) frees it before the guest has seen the key: nothing is typed.
      const page = await open({ args: KEYED, files: [CARD], canvas: '640x512' });
      await start(page); await waitFrame(page, 200);
      await page.evaluate(() => {
        document.getElementById('jnext-canvas').dispatchEvent(new KeyboardEvent('keydown', { code: 'Digit1', key: '1', bubbles: true }));
        document.getElementById('textbox').focus();
      });
      await waitLog(page, /screenshot saved/);
      await shotEquals(page, '/media/s.png', orc('idle.png'), 'native, no typing');
    });
row('BRW-14', 'a key held across a window blur is released (SDL window-blur path)',
    () => heldKeyThenLose((page) => page.evaluate(() => window.dispatchEvent(new FocusEvent('blur')))));

row('BRW-15', 'keys typed into a page input never reach the guest and are not swallowed', async () => {
  const page = await open({ args: KEYED, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 100);
  await page.focus('#textbox');
  await page.keyboard.type('hello', { delay: 40 });
  await waitLog(page, /screenshot saved/);
  const v = await page.inputValue('#textbox');
  must(v === 'hello', `input holds ${JSON.stringify(v)}`);
  await shotEquals(page, '/media/s.png', orc('idle.png'), 'native, no typing');
});

row('BRW-16', 'Alt+Shift+F5 / Alt+Shift+F9 reach jnext after main() returned (quick save, quick load)', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 100);
  const chord = async (key) => {
    await page.keyboard.down('Alt'); await page.keyboard.down('Shift');
    await page.keyboard.press(key, { delay: 80 });
    await page.keyboard.up('Shift'); await page.keyboard.up('Alt');
  };
  await chord('F5');
  await waitLog(page, /quick save: wrote/, 30000);
  await chord('F9');
  await waitLog(page, /quick load:/, 30000);
});

row('BRW-17', 'sound: no AudioContext before the start click; after it running and the output carries signal', async () => {
  const page = await open({ args: ['--machine', '48k', '--sdcard', '/media/card.img', '--load', '/media/beeper_demo.tap'],
                            files: [CARD, TAP], canvas: '640x512' });
  const before = await audioInfo(page);
  must(before.n === 0, `${before.n} AudioContext(s) before the click`);
  await start(page);
  await waitFor(page, () => window.__au.ctxs[0] && window.__au.ctxs[0].state === 'running' && window.__au.max > 0.01, null, 30000);
  const a = await audioInfo(page);
  must(a.state === 'running' && a.max > 0.01, JSON.stringify(a));
});

row('BRW-18', 'no gesture: the context stays suspended, no audio callbacks, the frame counter still advances', async () => {
  // Observed through page-side console lines only: a Playwright evaluate is a user
  // gesture and would resume the context.
  const page = await h.newPage();
  const aus = []; let ready = false;
  page.on('console', (m) => { const t = m.text(); if (t.startsWith('__AU ')) aus.push(JSON.parse(t.slice(5))); });
  await installAudioHook(page, true);
  await page.goto(pageUrl({ module: MODULE, args: ['--machine', '48k', '--sdcard', '/media/card.img', '--load', '/media/beeper_demo.tap'],
                            files: [CARD, TAP], canvas: '640x512', autostart: true }));
  const t0 = Date.now();
  let first = null;
  while (Date.now() - t0 < 60000) {
    await sleep(500);
    const withCtx = aus.filter((a) => a.n > 0 && a.frame > 0);
    if (withCtx.length && !first) first = { at: Date.now(), a: withCtx[0] };
    if (first && Date.now() - first.at > 5000 && aus[aus.length - 1].frame - first.a.frame > 100) break;
  }
  must(first, 'the module never started');
  const last = aus[aus.length - 1];
  const after = aus.filter((a) => a.n > 0);
  must(after.every((a) => a.state === 'suspended' && a.calls === 0), 'state/calls ' + JSON.stringify(after[after.length - 1]));
  must(last.frame - first.a.frame > 100, `frames advanced ${last.frame - first.a.frame}`);
});

row('BRW-19', 'a stub standard gamepad (button 0 = Kempston fire) reaches the guest: PRINT IN 31 prints 16', async () => {
  const page = await open({ args: [...BASE, ...shotArgs(420)], files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 60);
  await page.evaluate(() => {
    const pad = { id: 'Stub Pad (STANDARD GAMEPAD Vendor: 045e Product: 028e)', index: 0, connected: true, mapping: 'standard',
                  timestamp: 1000, axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
    window.__pad = pad;
    navigator.getGamepads = () => [pad, null, null, null];
    const ev = new Event('gamepadconnected'); ev.gamepad = pad; window.dispatchEvent(ev);
  });
  await waitLog(page, /Joystick 1 connected/, 30000);
  // SDL ignores a pad state whose timestamp did not change: a real pad bumps it.
  await page.evaluate(() => { const b = window.__pad.buttons[0]; b.pressed = b.touched = true; b.value = 1; window.__pad.timestamp = 2000; });
  await sleep(500);
  await waitFrame(page, 200);
  const kb = page.keyboard;
  await kb.press('p', { delay: 60 }); await sleep(120);
  await kb.down('Shift'); await kb.down('Control'); await sleep(100); await kb.up('Control'); await kb.up('Shift'); await sleep(150);
  await kb.down('Control'); await kb.down('i'); await sleep(100); await kb.up('i'); await kb.up('Control'); await sleep(150);
  await typeKeys(page, ['3', '1', 'Enter']);
  await waitLog(page, /screenshot saved/);
  await shotEquals(page, '/media/s.png', orc('pad.png'), 'native typist PRINT 16');
});

row('BRW-20', 'pointer lock: one click on the canvas captures the mouse, exitPointerLock releases it in jnext', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 60);
  await page.click('#jnext-canvas');
  await waitFor(page, () => document.pointerLockElement === document.getElementById('jnext-canvas'), null, 20000);
  await waitLog(page, /Mouse captured/, 20000);
  await page.evaluate(() => document.exitPointerLock());
  await waitLog(page, /Mouse released/, 20000);
});

row('BRW-21', '--silent runs at the 50 Hz grid, not at the display rate (<= 50.5 frames/s, > 0)', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 50);
  const f0 = await frame(page), t0 = Date.now();
  await sleep(10000);
  const f1 = await frame(page), t1 = Date.now();
  const fps = (f1 - f0) / ((t1 - t0) / 1000);
  must(fps > 0 && fps <= 50.5, `${fps.toFixed(2)} frames/s`);
  console.error(`# BRW-21: ${fps.toFixed(2)} frames/s`);
});

row('BRW-22', 'the teardown runs after main() returned: exit 0 on --delayed-automatic-exit-frames; exit 1 + "NO screenshot" when a capture is still pending', async () => {
  let page = await open({ args: [...BASE, '--delayed-automatic-exit-frames', '100'], files: [CARD], canvas: '640x512' });
  await start(page);
  const c1 = await waitExit(page);
  must(c1 === 0, `first exit ${c1}`);
  await page.ctx.close();
  page = await open({ args: [...BASE, '--delayed-automatic-exit-frames', '100', ...shotArgs(200)], files: [CARD], canvas: '640x512' });
  await start(page);
  const c2 = await waitExit(page);
  const err = (await stderr(page)).join('\n');
  must(c2 === 1 && /NO screenshot was written/.test(err), `second exit ${c2}`);
});

row('BRW-23', 'a browser page refuses --headless, --rzx-play, an unsupported --load and a bare unsupported FILE: exit 1, no loop', async () => {
  const cases = [['--headless'], ['--rzx-play', 'x.rzx'], ['--load', '/media/x.sna'], ['/media/x.sna']];
  const wants = [/--headless/, /--rzx-play/, /\.nex, \.tap, \.tzx/, /\.nex, \.tap, \.tzx/];
  for (let i = 0; i < cases.length; i++) {
    const page = await open({ args: cases[i], canvas: '640x512' });
    await start(page);
    const code = await waitExit(page, 30000);
    const err = (await stderr(page)).join('\n');
    const fr = await page.evaluate(() => window.jnextModule._jnext_web_frame());
    must(code === 1 && /browser page/.test(err) && wants[i].test(err) && fr === 0,
         `${JSON.stringify(cases[i])}: exit ${code} frame ${fr} err ${JSON.stringify(err)}`);
    await page.ctx.close();
  }
});

row('BRW-24', 'the WAV recorder outlives main(): a complete RIFF file after the automatic exit', async () => {
  // not --silent: the recorder taps the mixer, which a silent run does not synthesise
  const page = await open({ args: ['--machine', '48k', '--sdcard', '/media/card.img', '--wav-record', '/media/a.wav', '--delayed-automatic-exit-frames', '150'], files: [CARD], canvas: '640x512' });
  await start(page);
  const code = await waitExit(page);
  must(code === 0, `exit ${code}`);
  const w = await grab(page, '/media/a.wav');
  const riff = w.readUInt32LE(4);
  const dataAt = w.indexOf('data');
  const dataLen = w.readUInt32LE(dataAt + 4);
  must(w.toString('latin1', 0, 4) === 'RIFF' && riff === w.length - 8 && dataLen > 176400 && dataLen === w.length - dataAt - 8,
       `size ${w.length} riff ${riff} data ${dataLen}`);
});

row('BRW-25', 'F2 (window scale) leaves the canvas as the page sized it', async () => {
  const page = await open({ args: BASE, files: [CARD], canvas: '640x512' });
  await start(page); await waitFrame(page, 30);
  for (let i = 0; i < 3; i++) { await page.keyboard.press('F2'); await sleep(300); }
  await waitFrame(page, (await frame(page)) + 10);
  const r = await page.evaluate(() => { const c = document.getElementById('jnext-canvas'); return [c.width, c.height]; });
  must(r[0] === 640 && r[1] === 512, JSON.stringify(r));
});

row('BRW-26', 'every request the page made is in the allow-list; no image or ROM except the run\'s own card was fetched', async () => {
  const allowed = /^\/(build|page|nex|tap|scratch)\//;
  const bad = h.requests.filter((u) => !allowed.test(u));
  const imgs = [...new Set(h.requests.filter((u) => /\.(img|rom)$/i.test(u)))];
  must(h.refused.length === 0 && bad.length === 0, `refused ${JSON.stringify(h.refused)} outside ${JSON.stringify(bad)}`);
  must(imgs.length === 1 && imgs[0] === '/scratch/card.img', `images ${JSON.stringify(imgs)}`);
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
    const tail = await lastPage.evaluate(() => window.jnextTest.stderr.slice(-6)).catch(() => []);
    err.message += ' | stderr tail: ' + JSON.stringify(tail) + ' | console tail: ' + JSON.stringify((lastPage.msgs || []).slice(-4));
  }
  // every row's pages are closed here so the next starts from a clean browser
  for (const c of h.browser.contexts()) if (c !== util.context()) await c.close().catch(() => {});
  if (err) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(err.message).replace(/\n/g, ' ').slice(0, 600)}]`); }
  else { pass++; console.log(`PASS ${r.id} ${r.text}`); }
}
await h.close();
console.log('\n====================================================');
console.log(`Total: ${String(pass + fail).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
