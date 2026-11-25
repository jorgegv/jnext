// Browser rows WIDGET-01..32: the ready-made widget (web/jnext-widget.js.in) in headless Chromium
// (make wasm-browser-test, spec widget.mjs). Run by browser-smoke.sh on the SDK's node.
//
// Server A is the page host (http://localhost:<port>, a secure context): it serves the files of the
// build directory by name (jnext-<ver>.js/.wasm, jnext-widget.js, widget.html) and a few virtual
// pages a row defines. Server B stands in for the image host: the run-time minimal card and red.nex,
// with CORS. Nothing is intercepted; Chromium applies its own rules. The card is copyrighted: it lives
// in the scratch directory, which the script deletes; tracing, HAR and video are never enabled.
// Oracles: the contract of the user guide's section "Two ways to embed JNEXT" (attributes, themes,
// toolbar), and the API's own state (player.state) and call-outs. WIDGET-06 and WIDGET-07 run the
// two code blocks of that section VERBATIM (extracted from its source, three placeholders filled in),
// so the guide cannot drift from what is tested.
import { Harness, waitFor, sleep } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, VERSION = E.SMOKE_BUILD_VERSION;
const SHA = E.SMOKE_CARD_SHA256;
const SHOTS = E.WIDGET_SHOT_DIR || '';   // development aid: where to leave screenshots (unset in the suite)
const ONLY = (E.WIDGET_ROWS || '').split(' ').filter(Boolean);   // development aid: run only these rows (unset in the suite)
// WIDGET_SCHEME: development aid, light or dark for every page of a run (unset in the suite; the colour rows set their own)

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const pages = {};   // virtual pages: name -> html
const pageHeaders = {};   // virtual page name -> extra response headers
const NOCORE = '/nocore/jnext-widget.js';   // the widget script alone, in a directory with no jnext-<ver>.js beside it
const A = http.createServer((req, res) => {
  const name = decodeURIComponent(new URL(req.url, 'http://x').pathname.slice(1));
  if ('/' + name === NOCORE) { res.writeHead(200, { 'Content-Type': 'text/javascript' }); res.end(fs.readFileSync(path.join(BUILD, 'jnext-widget.js'))); return; }
  if (pages[name] !== undefined) { res.writeHead(200, { 'Content-Type': 'text/html', ...(pageHeaders[name] || {}) }); res.end(pages[name]); return; }
  const f = path.join(BUILD, name);
  if (!/^[^/]+$/.test(name) || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(name)] || 'application/octet-stream' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
let releaseSlow = () => {};
const B = http.createServer((req, res) => {
  if (req.url === '/slow.img') {   // the card, held back at 40% until the row releases it
    const body = fs.readFileSync(path.join(SCRATCH, 'card.img')), cut = Math.floor(body.length * 0.4);
    const gate = new Promise((r) => { releaseSlow = r; });
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
    res.write(body.subarray(0, cut));
    gate.then(() => res.end(body.subarray(cut)));
    return;
  }
  const f = { '/card.img': path.join(SCRATCH, 'card.img'), '/red.nex': path.join(ROOT, 'test/00regression/nex/red.nex') }[req.url];
  if (!f) { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length, 'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`, BURL = `http://127.0.0.1:${B.address().port}`;
const QUERY = new URLSearchParams({ file: BURL + '/red.nex' }).toString();
// A page names no SD card: the run-time card goes in through the localhost-only development seam.
const CARD_SEAM = { card: { url: BURL + '/card.img', sha256: SHA } };

await h.launch();
const rows = [];
const row = (id, text, fn) => { if (!ONLY.length || ONLY.includes(id)) rows.push({ id, text, fn }); };
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

async function open(url, ctxOpts = {}, seam = CARD_SEAM) {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1, ...(E.WIDGET_SCHEME ? { colorScheme: E.WIDGET_SCHEME } : {}), ...ctxOpts });
  await ctx.addInitScript((d) => { window['jnextDev'] = d; }, seam);
  const page = await ctx.newPage();
  page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  await page.goto(url);
  return page;
}
// widget i is ready for the click: its status line says so
const waitClickable = (page, i = 0) => waitFor(page, (i) => {
  const e = document.querySelectorAll('.jnext-widget')[i];
  return e && e.jnextPlayer && e.querySelector('.jw-status').textContent === 'click the screen to start';
}, i, 300000);
const stateOf = (page, i = 0) => page.evaluate((i) => document.querySelectorAll('.jnext-widget')[i].jnextPlayer.state, i);
const clickStart = (page, i = 0) =>
  page.locator('.jnext-widget').nth(i).locator('.jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
const waitState = (page, want, i = 0) => waitFor(page, ([w, i]) => {
  const e = document.querySelectorAll('.jnext-widget')[i];
  return e && e.jnextPlayer && e.jnextPlayer.state === w;
}, [want, i], 300000);
// the status line says 'running' once onReady came
const waitRunning = (page, i = 0) => waitFor(page, (i) =>
  document.querySelectorAll('.jnext-widget')[i].querySelector('.jw-status').textContent === 'running', i, 300000);

// The code blocks of the guide section, verbatim, three placeholders filled in.
function guideBlocks() {
  const md = fs.readFileSync(path.join(ROOT, 'src/doc/user-guide/08-web-page/05-two-ways-to-embed.md'), 'utf8');
  const blocks = [...md.matchAll(/```html\n([\s\S]*?)```/g)].map((m) => m[1]);
  must(blocks.length === 2, 'the guide section has ' + blocks.length + ' html blocks, expected 2');
  return blocks.map((b) => b.split('jnext-VERSION.js').join(`jnext-${VERSION}.js`)
    .split('game.nex').join(BURL + '/red.nex'));
}

row('WIDGET-01', 'widget.html (the snippet): a framed player with title, toolbar and status; the click starts it; ready after the click', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  const before = await page.evaluate(() => {
    const e = document.querySelector('.jnext-widget');
    return { title: e.querySelector('.jw-title').textContent, buttons: [...e.querySelectorAll('.jw-btn')].map((b) => b.textContent),
             overlay: e.querySelector('.jnext-overlay').textContent, state: e.jnextPlayer.state,
             ovStyle: [getComputedStyle(e.querySelector('.jnext-overlay')).backgroundColor, getComputedStyle(e.querySelector('.jnext-overlay')).color],
             canvas: e.querySelector('canvas').width > 0, pressed: e.querySelector('[data-jw=mute]').getAttribute('aria-pressed') };
  });
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'widget-before.png') });
  await clickStart(page);
  await waitRunning(page);
  const st = await stateOf(page);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'widget-running.png') });
  await page.context().close();
  must(before.title === 'JNEXT', 'default title ' + before.title);
  must(JSON.stringify(before.buttons) === JSON.stringify(['Pause', 'Reset', 'Mute', 'Fullscreen']), 'buttons ' + before.buttons);
  must(before.overlay === 'Click to start' && before.state === 'waiting', 'before the click: ' + JSON.stringify(before));
  must(st === 'running', 'state after the click ' + st);
  must(before.pressed === 'false', 'Mute aria-pressed before the first click: ' + before.pressed);
  must(before.ovStyle.join() === 'rgba(244, 245, 247, 0.82),rgb(29, 35, 48)', 'overlay colours ' + before.ovStyle);
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-02', 'themes: prefers-color-scheme dark gives the dark palette, light the light one; data-theme overrides both ways', async () => {
  const html = `<!doctype html><body>
<div class="jnext-widget"></div>
<div class="jnext-widget" data-theme="light"></div>
<div class="jnext-widget" data-theme="dark" data-title="Dark one"></div>
<script src="jnext-widget.js"></script></body>`;
  pages['themes.html'] = html;
  const bg = (page) => page.evaluate(() => [...document.querySelectorAll('.jnext-widget .jw-frame')].map((f) => getComputedStyle(f).backgroundColor));
  const LIGHT = 'rgb(244, 245, 247)', DARK = 'rgb(27, 30, 38)';
  const dark = await open(`${AURL}/themes.html`, { colorScheme: 'dark' });
  await waitFor(dark, () => document.querySelectorAll('.jw-frame').length === 3, null, 60000);
  const d = await bg(dark);
  const title = await dark.evaluate(() => document.querySelectorAll('.jw-title')[2].textContent);
  if (SHOTS) await dark.screenshot({ path: path.join(SHOTS, 'themes-dark.png') });
  await dark.context().close();
  const light = await open(`${AURL}/themes.html`, { colorScheme: 'light' });
  await waitFor(light, () => document.querySelectorAll('.jw-frame').length === 3, null, 60000);
  const l = await bg(light);
  if (SHOTS) await light.screenshot({ path: path.join(SHOTS, 'themes-light.png') });
  await light.context().close();
  must(JSON.stringify(d) === JSON.stringify([DARK, LIGHT, DARK]), 'prefers dark: ' + d);
  must(JSON.stringify(l) === JSON.stringify([LIGHT, LIGHT, DARK]), 'prefers light: ' + l);
  must(title === 'Dark one', 'data-title ' + title);
});

row('WIDGET-03', 'toolbar: Pause/Resume toggles the player state and stops the frames, Mute/Unmute calls setVolume(0/1), Reset calls reset(), Fullscreen asks the frame', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  await page.evaluate(() => {
    const e = document.querySelector('.jnext-widget'), p = e.jnextPlayer;
    window.calls = { vol: [], reset: 0, fs: 0 };
    const sv = p.setVolume, rs = p.reset;
    p.setVolume = function (v) { window.calls.vol.push(v); return sv.apply(this, arguments); };
    p.reset = function () { window.calls.reset++; return rs.apply(this, arguments); };
    e.querySelector('.jw-frame').requestFullscreen = function () { window.calls.fs++; return Promise.resolve(); };
  });
  await clickStart(page);
  await waitRunning(page);
  const btn = (n) => page.locator(`.jnext-widget [data-jw=${n}]`);
  const frames = () => page.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer._module._jnext_web_frame());
  const text = (n) => btn(n).textContent();
  await btn('pause').click();
  await waitState(page, 'paused');
  const f1 = await frames(); await sleep(300); const f2 = await frames();
  const t1 = await text('pause');
  await btn('pause').click();
  await waitState(page, 'running');
  await waitFor(page, (f) => document.querySelector('.jnext-widget').jnextPlayer._module._jnext_web_frame() > f, f2, 60000);
  const t2 = await text('pause');
  await btn('mute').click();
  const m1 = [await text('mute'), await btn('mute').getAttribute('aria-pressed')];
  await btn('mute').click();
  const m2 = [await text('mute'), await btn('mute').getAttribute('aria-pressed')];
  await btn('reset').click();
  await btn('fullscreen').click();
  const calls = await page.evaluate(() => window.calls);
  await waitRunning(page);
  await page.context().close();
  must(t1 === 'Resume' && t2 === 'Pause', 'pause labels ' + t1 + ' / ' + t2);
  must(f1 === f2, 'frames advanced while paused: ' + f1 + ' -> ' + f2);
  must(JSON.stringify(calls.vol) === '[0,1]', 'setVolume calls ' + JSON.stringify(calls.vol));
  must(m1.join() === 'Unmute,true' && m2.join() === 'Mute,false', 'mute label/pressed ' + m1 + ' / ' + m2);
  must(calls.reset === 1, 'reset calls ' + calls.reset);
  must(calls.fs === 1, 'requestFullscreen calls ' + calls.fs);
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-04', 'two widgets on one page are independent: own player, own state, own toolbar', async () => {
  const one = `<div class="jnext-widget" data-file="${BURL}/red.nex"></div>`;
  pages['two.html'] = `<!doctype html><body>${one}${one.replace('data-file', 'data-title="Second" data-file')}<script src="jnext-widget.js"></script></body>`;
  const page = await open(`${AURL}/two.html`);
  await waitClickable(page, 0);
  await waitClickable(page, 1);
  const distinct = await page.evaluate(() => {
    const w = document.querySelectorAll('.jnext-widget');
    return w[0].jnextPlayer !== w[1].jnextPlayer && w[0].querySelector('canvas').id !== w[1].querySelector('canvas').id;
  });
  await clickStart(page, 0);
  await waitRunning(page, 0);
  const s1 = [await stateOf(page, 0), await stateOf(page, 1)];
  await page.locator('.jnext-widget').nth(0).locator('[data-jw=pause]').click();
  const s2 = [await stateOf(page, 0), await stateOf(page, 1)];
  await clickStart(page, 1);
  await waitRunning(page, 1);
  const s3 = [await stateOf(page, 0), await stateOf(page, 1)];
  const titles = await page.evaluate(() => [...document.querySelectorAll('.jw-title')].map((t) => t.textContent));
  await page.context().close();
  must(distinct, 'the two players share a player or a canvas');
  must(s1.join() === 'running,waiting', 'after starting the first: ' + s1);
  must(s2.join() === 'paused,waiting', 'after pausing the first: ' + s2);
  must(s3.join() === 'paused,running', 'after starting the second: ' + s3);
  must(titles.join() === 'JNEXT,Second', 'titles ' + titles);
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-05', 'host CSS isolation: a hostile page stylesheet leaves the toolbar in one row and the frame its size; the widget adds only .jnext-widget rules and changes nothing outside', async () => {
  const hostile = `*{box-sizing:content-box;color:#f00;font-size:40px;letter-spacing:5px}
div,span{display:inline;width:3px;padding:30px;margin:11px;border:5px dotted #f0f;background:#ff0;position:relative;float:right;min-height:700px;opacity:.6;transform:translateX(30px)}
canvas{filter:blur(2px);opacity:.5;border:4px solid #f0f}
button{display:block;width:100%;height:80px;padding:20px;margin:9px;background:#f00;color:#0f0;font:italic 40px serif}
body div,body button{text-transform:uppercase;line-height:90px}`;
  const side = `<div id="o1">outside div</div><button id="o2">outside button</button>`;
  pages['hostile.html'] = `<!doctype html><style>${hostile}</style><body>${side}<div class="jnext-widget" data-title="Hostile" data-file="${BURL}/red.nex"></div><script src="jnext-widget.js"></script></body>`;
  pages['bare.html'] = `<!doctype html><style>${hostile}</style><body>${side}</body>`;
  const probe = (page) => page.evaluate(() => ['o1', 'o2'].map((id) => {
    const cs = getComputedStyle(document.getElementById(id)), r = document.getElementById(id).getBoundingClientRect();
    return [cs.display, cs.width, cs.padding, cs.margin, cs.color, cs.fontSize, cs.border, cs.backgroundColor, r.top, r.left].join('|');
  }));
  const bare = await open(`${AURL}/bare.html`);
  const outsideBare = await probe(bare);
  await bare.context().close();
  const page = await open(`${AURL}/hostile.html`);
  await waitClickable(page);
  const info = await page.evaluate(() => {
    const e = document.querySelector('.jnext-widget'), f = e.querySelector('.jw-frame');
    const bs = [...e.querySelectorAll('.jw-btn')].map((b) => b.getBoundingClientRect());
    const tr = e.querySelector('.jw-title').getBoundingClientRect(), st = e.querySelector('.jw-stage').getBoundingClientRect();
    const pb = e.querySelector('.jnext-player').getBoundingClientRect(), ob = e.querySelector('.jnext-overlay').getBoundingClientRect();
    const cs = (q) => { const c = getComputedStyle(e.querySelector(q)); return [c.opacity, c.transform, c.filter, c.minHeight].join('|'); };
    const look = ['.jw-frame', '.jw-title', '.jw-stage', '.jw-tools', '.jnext-player', '.jnext-overlay', 'canvas'].map(cs);
    const sheet = document.getElementById('jnext-widget-css').sheet;
    const sel = [];
    (function walk(rules) { for (const r of rules) { if (r.selectorText) sel.push(r.selectorText); else if (r.cssRules) walk(r.cssRules); } })(sheet.cssRules);
    return { frameW: f.getBoundingClientRect().width, tops: bs.map((b) => Math.round(b.top)), widths: bs.map((b) => Math.round(b.width)),
             look, titleH: tr.height, player: [pb.width, pb.height], overlay: [ob.width, ob.height], stageTop: st.top, playerTop: pb.top, stageW: st.width, sel, sheets: document.querySelectorAll('style').length };
  });
  const outsideWith = await probe(page);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'hostile.png') });
  await page.context().close();
  must(info.frameW === 642, 'frame width ' + info.frameW);
  must(info.stageW === 640, 'stage width ' + info.stageW);
  must(new Set(info.tops).size === 1, 'toolbar buttons not in one row: tops ' + info.tops);
  must(info.widths.every((w) => w > 20 && w < 130), 'button widths ' + info.widths);
  must(info.player.join() === '640,512' && info.overlay.join() === '640,512' && info.playerTop === info.stageTop,
       'player box ' + info.player + ' overlay ' + info.overlay + ' top ' + info.playerTop + ' vs stage ' + info.stageTop);
  must(info.look.every((l) => l === '1|none|none|0px' || l === '1|none|none|auto'), 'opacity|transform|filter|min-height leaked: ' + info.look);
  must(info.titleH < 40, 'title bar height ' + info.titleH);
  must(info.sel.length > 10 && info.sel.every((s) => s.split(',').every((p) => p.trim().startsWith('.jnext-widget'))),
       'rules outside .jnext-widget: ' + info.sel.filter((s) => s.split(',').some((p) => !p.trim().startsWith('.jnext-widget'))));
  must(info.sheets === 2, 'style elements ' + info.sheets);   // the page's own and the widget's one
  must(JSON.stringify(outsideBare) === JSON.stringify(outsideWith), 'outside elements changed: ' + outsideBare + ' vs ' + outsideWith);
});

row('WIDGET-06', 'the guide\'s own-page snippet (jnext.mount, verbatim) boots: click to start, then the player runs', async () => {
  pages['own.html'] = guideBlocks()[0];
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
  await ctx.addInitScript((d) => { window['jnextDev'] = d; }, CARD_SEAM);   // a page names no card
  const page = await ctx.newPage();
  const msgs = [];
  page.on('pageerror', (e) => msgs.push('PAGEERROR ' + e.message));
  // The snippet keeps no reference to its player: catch the promise mount() returns.
  await page.addInitScript(() => {
    let real;
    Object.defineProperty(window, 'jnext', {
      configurable: true,
      get() { return real; },
      set(v) { real = { version: v.version, mount: function () { const p = v.mount.apply(v, arguments); window.mounted = p; return p; } }; }
    });
  });
  await page.goto(`${AURL}/own.html`);
  await waitFor(page, () => document.querySelector('.jnext-overlay') && document.querySelector('.jnext-overlay').textContent === 'Click to start', null, 300000);
  await page.click('.jnext-overlay', { timeout: 600000, noWaitAfter: true });
  await waitFor(page, async () => { const p = await window.mounted; return p.state === 'running'; }, null, 300000);
  const ready = await page.evaluate(async () => { const p = await window.mounted; await p.ready; return p.state + ' ' + p.argv.join(' '); });
  await ctx.close();
  must(ready.startsWith('running ') && /--load \S*red\.nex/.test(ready), 'state and argv: ' + ready);
  must(msgs.length === 0, msgs.join('; '));
});

row('WIDGET-07', 'the guide\'s widget snippet (verbatim) boots: the widget loads jnext itself, click to start, ready', async () => {
  pages['guide-widget.html'] = guideBlocks()[1];
  const page = await open(`${AURL}/guide-widget.html`);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  const st = await stateOf(page);
  const argv = await page.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer.argv.join(' '));
  await page.context().close();
  must(st === 'running', 'state ' + st);
  must(/--load \S*red\.nex/.test(argv), 'the guide\'s program is not loaded: ' + argv);
  must(page.msgs.length === 0, page.msgs.join('; '));
});

const buttonsDisabled = (page) => page.evaluate(() => [...document.querySelectorAll('.jnext-widget .jw-btn')].every((b) => b.disabled));
const statusOf = (page) => page.evaluate(() => { const s = document.querySelector('.jnext-widget .jw-status'); return { text: s.textContent, cls: s.className, role: s.getAttribute('role'), color: getComputedStyle(s).color }; });
const ERR_LIGHT = 'rgb(179, 38, 30)';

row('WIDGET-08', 'real fullscreen with the game running: the screen fills the space between title bar and toolbar, its pixel buffer follows (not stretched), and both return to 640x512 on exit', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  const geo = () => page.evaluate(() => {
    const e = document.querySelector('.jnext-widget'), r = (q) => { const b = e.querySelector(q).getBoundingClientRect(); return [b.left, b.top, b.width, b.height]; };
    return { fs: document.fullscreenElement === e.querySelector('.jw-frame'), frame: r('.jw-frame'), title: r('.jw-title'), stage: r('.jw-stage'),
             tools: r('.jw-tools'), player: r('.jnext-player'), canvas: r('canvas'), buf: [e.querySelector('canvas').width, e.querySelector('canvas').height], boxW: e.querySelector('.jnext-player').style.width,
             vw: innerWidth, vh: innerHeight };
  });
  const before = await geo();
  await page.locator('.jnext-widget [data-jw=fullscreen]').click();
  await waitFor(page, () => document.fullscreenElement && document.querySelector('.jnext-widget .jnext-player').getBoundingClientRect().width > 700, null, 60000);
  // SDL follows the canvas's CSS size on a window resize: give it its time, then judge
  const settled = () => { const c = document.querySelector('.jnext-widget canvas'), b = c.getBoundingClientRect(); return c.width === Math.round(b.width) && c.height === Math.round(b.height); };
  await waitFor(page, settled, null, 15000).catch(() => {});
  const on = await geo();
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'fullscreen.png') });
  await page.evaluate(() => document.exitFullscreen());
  await waitFor(page, () => !document.fullscreenElement && document.querySelector('.jnext-widget .jnext-player').style.height === '512px', null, 60000);
  await waitFor(page, settled, null, 15000).catch(() => {});
  const off = await geo();
  await page.context().close();
  must(before.canvas[2] === 640 && before.canvas[3] === 512, 'before: canvas ' + before.canvas);
  must(on.fs && on.canvas[2] > 640 && on.canvas[3] > 512, 'fullscreen: canvas ' + on.canvas);
  must(Math.abs(on.canvas[2] - on.stage[2]) < 1 && Math.abs(on.canvas[3] - on.stage[3]) < 1, 'canvas ' + on.canvas + ' is not the stage ' + on.stage);
  must(Math.abs(on.stage[1] - (on.title[1] + on.title[3])) < 2 && Math.abs((on.stage[1] + on.stage[3]) - on.tools[1]) < 2, 'stage ' + on.stage + ' not between title ' + on.title + ' and toolbar ' + on.tools);
  must(on.tools[1] + on.tools[3] <= on.vh + 1 && on.frame[2] >= on.vw - 1, 'frame ' + on.frame + ' viewport ' + on.vw + 'x' + on.vh);
  must(on.buf[0] === Math.round(on.canvas[2]) && on.buf[1] === Math.round(on.canvas[3]), 'fullscreen: pixel buffer ' + on.buf + ' vs canvas ' + on.canvas + ' (stretched)');
  must(off.buf[0] === 640 && off.buf[1] === 512, 'after exit: pixel buffer ' + off.buf + ' (squashed)');
  must(!off.fs && off.canvas[2] === 640 && off.canvas[3] === 512 && off.boxW === '640px', 'after exit: canvas ' + off.canvas + ' width ' + off.boxW);
});

row('WIDGET-09', 'progress bar: topmost at its position while the image downloads, and its width follows the download', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY, {}, { card: { url: BURL + '/slow.img', sha256: SHA } });
  await waitClickable(page);
  await clickStart(page);   // nothing is fetched before the click
  const probe = () => page.evaluate(() => {
    const e = document.querySelector('.jnext-widget'), bar = e.querySelector('.jw-progress'), b = bar.getBoundingClientRect();
    const top = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2);
    return { pct: parseInt(bar.style.width, 10) || 0, w: b.width, top: top === bar, status: e.querySelector('.jw-status').textContent,
             stage: e.querySelector('.jw-stage').getBoundingClientRect().width };
  });
  await waitFor(page, () => { const w = parseInt(document.querySelector('.jnext-widget .jw-progress').style.width, 10); return w >= 10 && w < 100; }, null, 120000);
  await waitFor(page, () => { const e = document.querySelector('.jnext-widget'), p = parseInt(e.querySelector('.jw-progress').style.width, 10);
    return Math.abs(e.querySelector('.jw-progress').getBoundingClientRect().width - 640 * p / 100) < 2; }, null, 30000);
  const mid = await probe();
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'progress.png') });
  releaseSlow();
  await waitRunning(page);   // the card, then the program: the bar is back to 0 at onReady
  const done = await probe();
  await page.context().close();
  must(mid.top, 'the progress bar is covered by another element at 40%');
  must(mid.pct >= 10 && mid.pct < 100 && Math.abs(mid.w - mid.stage * mid.pct / 100) < 2, 'bar ' + mid.w + 'px = ' + mid.pct + '% of ' + mid.stage);
  must(mid.status === 'download ' + mid.pct + '%', 'status ' + mid.status + ' vs bar ' + mid.pct + '%');
  must(done.pct === 0, 'bar after the download: ' + done.pct + '%');
});

row('WIDGET-10', 'the toolbar and status follow the player when the page itself calls pause(), resume() and setVolume()', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  const view = () => page.evaluate(() => { const e = document.querySelector('.jnext-widget');
    return [e.querySelector('[data-jw=pause]').textContent, e.querySelector('[data-jw=mute]').textContent,
            e.querySelector('[data-jw=mute]').getAttribute('aria-pressed'), e.querySelector('.jw-status').textContent, e.jnextPlayer.state].join('|'); });
  const call = (f) => page.evaluate(f);
  await call(() => document.querySelector('.jnext-widget').jnextPlayer.pause());
  const paused = await view();
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'paused.png') });
  await page.locator('.jnext-widget [data-jw=pause]').click();       // the button now resumes
  await waitState(page, 'running');
  const resumed = await view();
  await call(() => document.querySelector('.jnext-widget').jnextPlayer.setVolume(0));
  const muted = await view();
  await call(() => document.querySelector('.jnext-widget').jnextPlayer.setVolume(1));
  const unmuted = await view();
  await page.context().close();
  must(paused === 'Resume|Mute|false|paused|paused', 'page-side pause: ' + paused);
  must(resumed === 'Pause|Mute|false|running|running', 'button after a page-side pause: ' + resumed);
  must(muted === 'Pause|Unmute|true|running|running', 'page-side setVolume(0): ' + muted);
  must(unmuted === 'Pause|Mute|false|running|running', 'page-side setVolume(1): ' + unmuted);
});

row('WIDGET-11', 'an image that fails (wrong digest): the status line says error in the error colour, the buttons stay disabled', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY, {}, { card: { url: BURL + '/card.img', sha256: 'a'.repeat(64) } });
  await waitClickable(page);
  await clickStart(page);   // the card is fetched at the click
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent.startsWith('error: '), null, 300000);
  const st = await statusOf(page), dis = await buttonsDisabled(page);
  await page.context().close();
  must(st.cls.includes('jw-error') && st.color === ERR_LIGHT && st.role === 'status', 'status ' + JSON.stringify(st));
  must(dis, 'a button is enabled after the error');
});

row('WIDGET-12', "data-card-source='x': the status line refuses it in the error colour, the buttons stay disabled", async () => {
  pages['badsource.html'] = '<!doctype html><body><div class="jnext-widget" data-card-source="x"></div><script src="jnext-widget.js"></script></body>';
  const page = await open(`${AURL}/badsource.html`);
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status').textContent.startsWith('error: '), null, 120000);
  const st = await statusOf(page), dis = await buttonsDisabled(page);
  await page.context().close();
  must(st.text === "error: option 'card': source must be 'auto', 'fetch' or 'pick'" && st.cls.includes('jw-error') && st.color === ERR_LIGHT, 'status ' + JSON.stringify(st));
  must(dis, 'a button is enabled');
});

row('WIDGET-13', 'jnext-<ver>.js cannot be loaded: the framed widget shows the error in its status line', async () => {
  pages['nocore.html'] = '<!doctype html><body><div class="jnext-widget"></div><script src="/nocore/jnext-widget.js"></script></body>';
  const page = await open(`${AURL}/nocore.html`);
  await waitFor(page, () => document.querySelector('.jnext-widget .jw-status') && document.querySelector('.jnext-widget .jw-status').textContent.startsWith('error: '), null, 60000);
  const st = await statusOf(page), dis = await buttonsDisabled(page);
  const framed = await page.evaluate(() => !!document.querySelector('.jnext-widget .jw-frame .jw-title'));
  await page.context().close();
  must(st.text === `error: cannot load ${AURL}/nocore/jnext-${VERSION}.js` && st.cls.includes('jw-error') && st.color === ERR_LIGHT, 'status ' + JSON.stringify(st));
  must(framed && dis, 'framed ' + framed + ' disabled ' + dis);
});

row('WIDGET-14', 'the click-to-start overlay takes the dark theme colours', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY, { colorScheme: 'dark' });
  await waitClickable(page);
  const ov = await page.evaluate(() => { const c = getComputedStyle(document.querySelector('.jnext-widget .jnext-overlay')); return [c.backgroundColor, c.color, c.fontWeight].join('|'); });
  await page.context().close();
  must(ov === 'rgba(20, 22, 28, 0.82)|rgb(230, 233, 240)|600', 'overlay ' + ov);
});

row('WIDGET-15', 'after a toolbar click while running, the keyboard focus is back on the canvas', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  await page.locator('.jnext-widget [data-jw=mute]').click();
  const a = await page.evaluate(() => document.activeElement && document.activeElement.tagName + ':' + document.activeElement.id);
  await page.locator('.jnext-widget [data-jw=reset]').click();
  const b = await page.evaluate(() => document.activeElement && document.activeElement.tagName + ':' + document.activeElement.id);
  await page.context().close();
  must(/^CANVAS:jnext-canvas-/.test(a) && /^CANVAS:jnext-canvas-/.test(b), 'focus after Mute: ' + a + ', after Reset: ' + b);
});

row('WIDGET-16', 'a page-side destroy(): the toolbar is disabled, the status says stopped, clicking raises no page error', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  await page.evaluate(() => document.querySelector('.jnext-widget').jnextPlayer.destroy());
  const st = await statusOf(page), dis = await buttonsDisabled(page);
  await page.evaluate(() => document.querySelectorAll('.jnext-widget .jw-btn').forEach((b) => b.click()));
  await sleep(200);
  await page.context().close();
  must(st.text === 'stopped', 'status ' + JSON.stringify(st));
  must(dis, 'a button is still enabled after destroy()');
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-17', 'data-keep-writes gives a widget player whose command line carries --sdcard-writes, and el.jnextPlayer.resetCard is a function; without the attribute, and with data-keep-writes="false", neither', async () => {
  const one = `<div class="jnext-widget" data-file="${BURL}/red.nex"`;
  pages['keep.html'] = `<!doctype html><body>${one} data-keep-writes></div>${one}></div>${one} data-keep-writes="false"></div><script src="jnext-widget.js"></script></body>`;
  const page = await open(`${AURL}/keep.html`);
  await waitClickable(page, 0);
  await waitClickable(page, 1);
  await waitClickable(page, 2);
  for (const i of [0, 1, 2]) await clickStart(page, i);   // argv exists once the click has got the card
  await waitFor(page, () => [...document.querySelectorAll('.jnext-widget')].every((e) => e.jnextPlayer.argv), null, 600000);
  const r = await page.evaluate(() => [...document.querySelectorAll('.jnext-widget')].map((e) => ({
    argv: e.jnextPlayer.argv, fn: typeof e.jnextPlayer.resetCard })));
  await page.context().close();
  const w = r[0].argv.indexOf('--sdcard-writes');
  must(w >= 0 && /^\/jnext\/card-writes\/[0-9a-f]{64}-[0-9a-f]{16}\.jsdw$/.test(r[0].argv[w + 1]) && r[0].fn === 'function',
       'keep-writes widget: ' + JSON.stringify(r[0]));
  must(r[1].argv.indexOf('--sdcard-writes') < 0, 'plain widget argv: ' + JSON.stringify(r[1].argv));
  must(r[2].argv.indexOf('--sdcard-writes') < 0, 'data-keep-writes="false" widget argv: ' + JSON.stringify(r[2].argv));
});

// GH #563: the attribution mount() adds under every player, so the widget and the plain template page both show it.
const creditOf = (page, sel) => page.evaluate((sel) => {
  const c = document.querySelector('.jnext-credit'), a = c && c.querySelector('a'), img = a && a.querySelector('svg');
  const cv = document.querySelector(sel + ' canvas').getBoundingClientRect(), r = c.getBoundingClientRect();
  const ir = img.getBoundingClientRect(), box = document.querySelector(sel + ' .jnext-player').getBoundingClientRect();
  return { href: a.href, target: a.target, rel: a.rel, text: a.textContent, icon: img.getBoundingClientRect().width > 0 && !a.querySelector('img'),
           overlap: !(r.top >= cv.bottom - 0.5 || r.bottom <= cv.top + 0.5 || r.left >= cv.right || r.right <= cv.left),
           below: r.top >= cv.bottom - 0.5, visible: r.width > 0 && r.height > 0 && getComputedStyle(c).display !== 'none',
           vis: getComputedStyle(a).visibility + '/' + getComputedStyle(c).visibility, opacity: parseFloat(getComputedStyle(c).opacity),
           hit: (() => { const e = document.elementFromPoint(a.getBoundingClientRect().left + a.getBoundingClientRect().width / 2, a.getBoundingClientRect().top + a.getBoundingClientRect().height / 2); return !!e && (e === a || a.contains(e)); })(),
           credRight: r.right, boxRight: box.right, iconW: ir.width, iconH: ir.height, vw: window.innerWidth,
           color: getComputedStyle(a).color, bg: getComputedStyle(document.querySelector('.jw-frame') || document.body).backgroundColor };
}, sel);
const mustCredit = (r) => {
  must(r.href === 'https://www.jnext.es/' && r.target === '_blank' && /noopener/.test(r.rel), 'link ' + JSON.stringify(r));
  must(r.text === 'JNext' && r.icon, 'text/icon ' + JSON.stringify(r));
  must(r.visible && r.below && !r.overlap, 'placement ' + JSON.stringify(r));
  must(r.vis === 'visible/visible' && r.opacity >= 0.5 && r.hit, 'not really visible or covered: ' + JSON.stringify(r));
  must(Math.abs(r.credRight - r.boxRight) <= 2, 'credit right edge ' + r.credRight + ' vs player panel ' + r.boxRight + ' (viewport ' + r.vw + ')');
  must(Math.abs(r.iconW - 18) <= 0.5 && Math.abs(r.iconH - 18) <= 0.5, 'icon size ' + r.iconW + 'x' + r.iconH);
};

row('WIDGET-18', 'the widget shows the JNext attribution link (www.jnext.es, icon 18 px + text, new tab) under the screen, right edge on the player panel edge, no overlap with the canvas, in light and dark at a 1600 px viewport', async () => {
  const out = [];
  for (const scheme of ['light', 'dark']) {
    const page = await open(`${AURL}/widget.html?` + QUERY, { colorScheme: scheme, viewport: { width: 1600, height: 900 } });
    await waitClickable(page);
    const r = await creditOf(page, '.jnext-widget');
    if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-widget-' + scheme + '.png') });
    await page.context().close();
    out.push(r);
    must(page.msgs.length === 0, page.msgs.join('; '));
  }
  out.forEach(mustCredit);
  must(out[0].color !== out[0].bg && out[1].color !== out[1].bg, 'link colour equals the background: ' + JSON.stringify(out));
});

row('WIDGET-19', 'plain.html (the basic template page) shows the same attribution link under its player, right-aligned to the player panel at 1600 px and at a 390 px phone width', async () => {
  const page = await open(`${AURL}/plain.html?` + QUERY, { viewport: { width: 1600, height: 900 } });
  await waitFor(page, () => document.querySelector('.jnext-credit') && document.querySelector('#status').textContent === 'click the player to start', null, 300000);
  const r = await creditOf(page, '#player');
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-plain.png') });
  await page.context().close();
  mustCredit(r);
  must(page.msgs.length === 0, page.msgs.join('; '));
  // a phone width: the page shrinks the player; the credit follows its right edge
  const ph = await open(`${AURL}/plain.html?` + QUERY, { viewport: { width: 390, height: 800 } });
  await waitFor(ph, () => document.querySelector('.jnext-credit') && document.querySelector('#status').textContent === 'click the player to start', null, 300000);
  const n = await creditOf(ph, '#player');
  if (SHOTS) await ph.screenshot({ path: path.join(SHOTS, 'credit-plain-narrow.png') });
  await ph.context().close();
  mustCredit(n);
});

// The credit in any parent layout, the shipped itch.io page, a strict CSP, and its lifecycle (GH #563 review 1).
const ctxPage = async (url, ctxOpts = {}, extra = {}) => {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1, ...ctxOpts });
  await ctx.addInitScript((d) => { window['jnextDev'] = d; }, CARD_SEAM);
  if (extra.init) await ctx.addInitScript(extra.init);
  const page = await ctx.newPage();
  page.msgs = [];
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error') page.msgs.push('CONSOLE ' + m.text()); });
  await page.goto(url);
  return page;
};
const layoutPage = (name, host) => {
  pages[name] = `<!doctype html><body>${host}<script src="jnext-${VERSION}.js"></script><script>
window.pl = null; jnext.mount(document.getElementById('p'), { width: 640, height: 512, file: ${JSON.stringify(BURL + '/red.nex')} }).then((p) => { window.pl = p; });
</script></body>`;
};

const probeCredit = (page) => page.evaluate(() => {
  const box = document.querySelector('.jnext-player').getBoundingClientRect(), c = document.querySelector('.jnext-credit'), r = c.getBoundingClientRect(),
        a = c.querySelector('a').getBoundingClientRect();
  const mid = document.elementFromPoint(box.left + box.width / 2, box.top + box.height / 2);
  const link = document.elementFromPoint(a.left + a.width / 2, a.top + a.height / 2);
  return { overlap: !(r.top >= box.bottom - 0.5 || r.bottom <= box.top + 0.5 || r.left >= box.right - 0.5 || r.right <= box.left + 0.5),
           midInPlayer: !!mid && !!mid.closest('.jnext-player'), linkHit: !!link && !!link.closest('.jnext-credit'),
           right: r.right, boxRight: box.right, boxW: box.width, boxH: box.height, vis: getComputedStyle(c).visibility };
});

row('WIDGET-20', 'a flex-row and a grid host page: the credit never overlaps the player, hit-tests as a link beside the click target, its right edge is the player panel edge, and a click on the player starts it', async () => {
  layoutPage('flex.html', '<div style="display:flex"><div id="p"></div><aside style="width:200px">sidebar</aside></div>');
  layoutPage('grid.html', '<div style="display:grid;grid-template-columns:auto 200px"><div id="p"></div><aside>sidebar</aside></div>');
  const probe = probeCredit;
  const out = [];
  for (const name of ['flex.html', 'grid.html']) {
    const page = await ctxPage(`${AURL}/${name}`);
    await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
    const r = await probe(page);
    must(!r.overlap && r.midInPlayer && r.linkHit && r.vis === 'visible', name + ': ' + JSON.stringify(r));
    must(Math.abs(r.right - r.boxRight) <= 2 && r.boxW === 640 && r.boxH === 512, name + ' alignment: ' + JSON.stringify(r));
    if (name === 'flex.html') {
      await page.locator('.jnext-overlay').click({ timeout: 600000, noWaitAfter: true });
      await waitFor(page, () => window.pl.state === 'running', null, 600000);
      r.started = true;
    }
    if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-' + name.replace('.html', '') + '.png') });
    await page.context().close();
    out.push([name, r, page.msgs]);
  }
  for (const [name, r, msgs] of out) {
    must(!r.overlap && r.midInPlayer && r.linkHit && r.vis === 'visible', name + ': ' + JSON.stringify(r));
    must(Math.abs(r.right - r.boxRight) <= 2 && r.boxW === 640 && r.boxH === 512, name + ' alignment: ' + JSON.stringify(r));
    must(msgs.length === 0, name + ': ' + msgs.join('; '));
  }
  must(out[0][1].started, 'the click did not start the player');
});

row('WIDGET-24', 'an absolutely positioned player whose container is smaller than the 640x512 box: the credit lies below the box, never over the picture, and its right edge is the box edge', async () => {
  layoutPage('abs.html', '<div id="p" style="position:absolute;top:0;left:0;width:320px;height:256px"></div>');
  const page = await ctxPage(`${AURL}/abs.html`);
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  const r = await probeCredit(page);
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-abs.png') });
  await page.context().close();
  must(!r.overlap && r.midInPlayer && r.linkHit && r.vis === 'visible', 'abs: ' + JSON.stringify(r));
  must(Math.abs(r.right - r.boxRight) <= 2 && r.boxW === 640 && r.boxH === 512, 'abs alignment: ' + JSON.stringify(r));
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-21', 'itch-game.html (the shipped itch.io page) at the 640x512 frame: the credit is inside the viewport, below the canvas, not covered, right edge on the player edge', async () => {
  const page = await ctxPage(`${AURL}/itch-game.html?` + QUERY, { viewport: { width: 640, height: 512 } });
  await waitFor(page, () => window.player && document.querySelector('.jnext-credit'), null, 120000);
  const r = await page.evaluate(() => {
    const c = document.querySelector('.jnext-credit'), a = c.querySelector('a'), rr = c.getBoundingClientRect(),
          cv = document.querySelector('canvas').getBoundingClientRect(), box = document.querySelector('.jnext-player').getBoundingClientRect(),
          ar = a.getBoundingClientRect(), hit = document.elementFromPoint(ar.left + ar.width / 2, ar.top + ar.height / 2);
    return { top: rr.top, bottom: rr.bottom, vh: innerHeight, canvasBottom: cv.bottom, right: rr.right, boxRight: box.right,
             hit: !!hit && !!hit.closest('.jnext-credit'), canvasH: cv.height };
  });
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-itch.png') });
  await page.context().close();
  must(r.bottom <= r.vh + 0.5 && r.top >= r.canvasBottom - 1 && r.hit, 'credit placement ' + JSON.stringify(r));
  must(Math.abs(r.right - r.boxRight) <= 2 && r.canvasH > 400, 'alignment/canvas ' + JSON.stringify(r));
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-22', 'under a page CSP with img-src self the credit still has its icon (inline SVG, 18 px) and no CSP violation is logged', async () => {
  pages['csp.html'] = `<!doctype html><body><div id="p"></div><script src="jnext-${VERSION}.js"></script><script>window.pl = null; jnext.mount(document.getElementById('p'), { width: 640, height: 512 }).then((p) => { window.pl = p; });</script></body>`;
  pageHeaders['csp.html'] = { 'Content-Security-Policy': "default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; connect-src *; img-src 'self'" };
  const page = await ctxPage(`${AURL}/csp.html`);
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  const r = await page.evaluate(() => {
    const c = document.querySelector('.jnext-credit'), svg = c.querySelector('a svg'), b = svg && svg.getBoundingClientRect();
    return { svg: !!svg, w: b && b.width, h: b && b.height, shapes: svg ? svg.querySelectorAll('path,polygon,rect').length : 0, img: c.querySelectorAll('img').length };
  });
  await page.context().close();
  must(r.svg && Math.abs(r.w - 18) < 0.5 && Math.abs(r.h - 18) < 0.5 && r.shapes > 3 && r.img === 0, 'icon ' + JSON.stringify(r));
  must(page.msgs.length === 0, 'console/page errors: ' + page.msgs.join('; '));
});

row('WIDGET-25', 'under a page that requires Trusted Types for script, mount() resolves, the credit has its inline SVG icon and nothing reaches the console', async () => {
  pages['tt.html'] = `<!doctype html><body><div id="p"></div><script src="jnext-${VERSION}.js"></script><script>window.pl = null; window.thrown = null; try { jnext.mount(document.getElementById('p'), { width: 640, height: 512 }).then((p) => { window.pl = p; }); } catch (e) { window.thrown = String(e); }</script></body>`;
  pageHeaders['tt.html'] = { 'Content-Security-Policy': "require-trusted-types-for 'script'" };
  const page = await ctxPage(`${AURL}/tt.html`);
  await waitFor(page, () => window.thrown || (window.pl && document.querySelector('.jnext-credit')), null, 120000);
  const r = await page.evaluate(() => {
    const c = document.querySelector('.jnext-credit'), svg = c && c.querySelector('a svg'), b = svg && svg.getBoundingClientRect();
    return { thrown: window.thrown, svg: !!svg, w: b && b.width, shapes: svg ? svg.querySelectorAll('path,polygon,rect').length : 0 };
  });
  await page.context().close();
  must(!r.thrown && r.svg && Math.abs(r.w - 18) < 0.5 && r.shapes > 3, 'trusted types: ' + JSON.stringify(r));
  must(page.msgs.length === 0, 'console/page errors: ' + page.msgs.join('; '));
});

row('WIDGET-26', 'a flex-row player whose container clips (overflow hidden, rounded): the credit is visible and hittable under the box, and a focused credit link keeps its focus across a window resize', async () => {
  layoutPage('clip.html', '<div style="display:flex"><div id="p" style="overflow:hidden;border-radius:8px"></div><aside style="width:200px">sidebar</aside></div>');
  const page = await ctxPage(`${AURL}/clip.html`);
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  const r = await probeCredit(page);
  const kept = await page.evaluate(async () => {
    const a = document.querySelector('.jnext-credit a'); a.focus();
    window.dispatchEvent(new Event('resize'));
    await new Promise((res) => setTimeout(res, 100));
    return document.activeElement === a;
  });
  if (SHOTS) await page.screenshot({ path: path.join(SHOTS, 'credit-clip.png') });
  await page.context().close();
  must(!r.overlap && r.midInPlayer && r.linkHit && r.vis === 'visible', 'clip: ' + JSON.stringify(r));
  must(Math.abs(r.right - r.boxRight) <= 2, 'clip alignment: ' + JSON.stringify(r));
  must(kept, 'the focused credit link lost its focus on a window resize');
});

row('WIDGET-27', 'content above the player changes height after mount (block, flex, grid, positioned ancestor): the credit stays under the player box, not over it, clear of the paragraph after the host, right edge on the box edge', async () => {
  const side = '<aside style="width:200px">sidebar</aside>';
  const hosts = {
    'g-block.html': '<div id="s" style="height:10px"></div><div><div id="p"></div></div><p id="after">after</p>',
    'g-flex.html': '<div id="s" style="height:10px"></div><div style="display:flex"><div id="p"></div>' + side + '</div><p id="after">after</p>',
    'g-grid.html': '<div id="s" style="height:10px"></div><div style="display:grid;grid-template-columns:auto 200px"><div id="p"></div>' + side + '</div><p id="after">after</p>',
    'g-rel.html': '<div id="s" style="height:10px"></div><div style="position:relative;margin:40px 0 0 30px"><div style="display:flex"><div id="p"></div>' + side + '</div></div><p id="after">after</p>',
  };
  const probe = (page) => page.evaluate(() => {
    const r = document.querySelector('.jnext-credit').getBoundingClientRect(), a = document.getElementById('after').getBoundingClientRect(),
          box = document.querySelector('.jnext-player').getBoundingClientRect(), l = document.querySelector('.jnext-credit a').getBoundingClientRect(),
          hit = document.elementFromPoint(l.left + l.width / 2, l.top + l.height / 2),
          mid = document.elementFromPoint(box.left + box.width / 2, box.top + box.height / 2);
    return { top: r.top, bottom: r.bottom, boxBottom: box.bottom, right: r.right, boxRight: box.right, afterTop: a.top,
             overlap: !(r.top >= box.bottom - 0.5 || r.bottom <= box.top + 0.5 || r.left >= box.right - 0.5 || r.right <= box.left + 0.5),
             linkHit: !!hit && !!hit.closest('.jnext-credit'), midInPlayer: !!mid && !!mid.closest('.jnext-player') };
  });
  const check = (name, when, r) => {
    must(!r.overlap && r.linkHit && r.midInPlayer && r.top >= r.boxBottom - 1, name + ' ' + when + ': ' + JSON.stringify(r));
    must(Math.abs(r.right - r.boxRight) <= 2, name + ' ' + when + ' right edge: ' + JSON.stringify(r));
    must(r.afterTop >= r.bottom - 0.5, name + ' ' + when + ': the paragraph after the host lies under the credit: ' + JSON.stringify(r));
  };
  for (const [name, host] of Object.entries(hosts)) {
    layoutPage(name, host);
    const page = await ctxPage(`${AURL}/${name}`);
    await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
    check(name, 'at mount', await probe(page));
    await page.evaluate(() => { document.getElementById('s').style.height = '300px'; });
    await sleep(500);
    check(name, 'after the content above grew', await probe(page));
    await page.context().close();
    must(page.msgs.length === 0, name + ': ' + page.msgs.join('; '));
  }
});

const SPY = () => {
  const spy = window.__spy = { resize: new Set(), ro: new Set(), poll: 0 };
  const aEL = window.addEventListener, rEL = window.removeEventListener;
  window.addEventListener = function (t, f) { if (t === 'resize' && f && f.name === 'placeCredit') spy.resize.add(f); return aEL.apply(this, arguments); };
  window.removeEventListener = function (t, f) { if (t === 'resize' && f && f.name === 'placeCredit') spy.resize.delete(f); return rEL.apply(this, arguments); };
  const RO = window.ResizeObserver;
  window.ResizeObserver = class extends RO {
    constructor(cb) { super(cb); if (cb && cb.name === 'placeCredit') spy.ro.add(this); }
    disconnect() { spy.ro.delete(this); return super.disconnect(); }
  };
  const raf = window.requestAnimationFrame;
  window.requestAnimationFrame = function (cb) {
    if (cb && cb.name === 'creditPoll') return raf.call(window, (t) => { spy.poll++; return cb(t); });
    return raf.call(window, cb);
  };
};

row('WIDGET-28', 'the credit takes the container\'s room and gives it all back: its bottom margin is reserved while mounted and restored by destroy(), and its observer, resize listener and per-frame check exist while mounted and are gone after destroy()', async () => {
  layoutPage('spy.html', '<div style="display:flex"><div id="p" style="margin-bottom:10px"></div><aside style="width:200px">sidebar</aside></div>');
  const page = await ctxPage(`${AURL}/spy.html`, {}, { init: SPY });
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  await sleep(300);
  const before = await page.evaluate(() => ({ margin: document.getElementById('p').style.marginBottom, resize: window.__spy.resize.size, ro: window.__spy.ro.size, poll: window.__spy.poll }));
  const after = await page.evaluate(async () => {
    await window.pl.destroy();
    const s = window.__spy, p0 = s.poll;
    await new Promise((r) => setTimeout(r, 400));
    return { margin: document.getElementById('p').style.marginBottom, resize: s.resize.size, ro: s.ro.size, polled: s.poll - p0, credits: document.querySelectorAll('.jnext-credit').length };
  });
  await page.context().close();
  must(before.margin === '36px' && before.resize === 1 && before.ro === 1 && before.poll > 0, 'while mounted: ' + JSON.stringify(before));
  must(after.margin === '10px' && after.resize === 0 && after.ro === 0 && after.polled === 0 && after.credits === 0, 'after destroy(): ' + JSON.stringify(after));
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-29', 'the rendered icon is the packaging SVG: same elements, nesting and attributes (SVG namespace, 18 px, clip id renamed per mount, each reference inside its own icon)', async () => {
  const asset = fs.readFileSync(path.join(ROOT, 'packaging/assets/io.github.zxjogv.jnext.svg'), 'utf8');
  pages['icon.html'] = `<!doctype html><body><div id="a"></div><div id="b"></div><script src="jnext-${VERSION}.js"></script></body>`;
  const page = await ctxPage(`${AURL}/icon.html`);
  await waitFor(page, () => typeof jnext !== 'undefined', null, 60000);
  const r = await page.evaluate(async (asset) => {
    await jnext.mount(document.getElementById('a'), { width: 640, height: 512 });
    await jnext.mount(document.getElementById('b'), { width: 640, height: 512 });
    const ref = new DOMParser().parseFromString(asset, 'image/svg+xml').documentElement;
    const SVGNS = 'http://www.w3.org/2000/svg';
    const attrs = (e, root) => {
      const o = {};
      for (const a of e.attributes) {
        let k = a.name, v = a.value;
        if (k === 'xmlns') continue;
        if (k === 'style' && /^paint-order:\s*stroke;?$/.test(v)) { k = 'paint-order'; v = 'stroke'; }
        if (k === 'id' && e.localName === 'clipPath') v = 'CLIP';
        if (k === 'clip-path') v = v.replace(/#[^)]*/, '#CLIP');
        if (root && (k === 'width' || k === 'height' || k === 'aria-hidden' || k === 'style')) continue;
        o[k] = v;
      }
      return o;
    };
    const shape = (e, root) => ({ tag: e.localName, ns: e.namespaceURI === SVGNS, attrs: attrs(e, root), kids: [...e.children].map((c) => shape(c, false)) });
    const want = JSON.stringify(shape(ref, true));
    const icons = [...document.querySelectorAll('.jnext-credit a svg')];
    return icons.map((svg) => {
      const clip = svg.querySelector('clipPath'), id = clip && clip.id;
      const refs = [...svg.querySelectorAll('[clip-path]')].map((e) => e.getAttribute('clip-path'));
      return { same: JSON.stringify(shape(svg, true)) === want, id, refsOk: refs.length > 0 && refs.every((x) => x === 'url(#' + id + ')') && !!svg.querySelector('#' + id),
               size: [svg.getAttribute('width'), svg.getAttribute('height'), svg.getAttribute('aria-hidden')], nested: svg.querySelectorAll('svg *').length };
    });
  }, asset);
  await page.context().close();
  must(r.length === 2, 'icons: ' + r.length);
  must(r.every((x) => x.same && x.refsOk && x.size.join() === '18,18,true' && x.nested === 13), 'icon structure: ' + JSON.stringify(r));
  must(r[0].id && r[1].id && r[0].id !== r[1].id, 'clip ids not unique per mount: ' + JSON.stringify(r.map((x) => x.id)));
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-30', 'the per-frame check is one chain per player: after several changes of the content above, it runs at most once per animation frame (no multiplying), and is still running', async () => {
  layoutPage('chain.html', '<div id="s" style="height:10px"></div><div style="display:flex"><div id="p"></div><aside style="width:200px">sidebar</aside></div>');
  const page = await ctxPage(`${AURL}/chain.html`, {}, { init: SPY });
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  for (const h of [100, 40, 220, 60, 300]) {
    await page.evaluate((h) => { document.getElementById('s').style.height = h + 'px'; }, h);
    await sleep(150);
  }
  const r = await page.evaluate(() => new Promise((res) => {
    const s = window.__spy, p0 = s.poll; let frames = 0, t0 = performance.now();
    const f = () => { frames++; if (performance.now() - t0 < 1500) requestAnimationFrame(f); else res({ polls: s.poll - p0, frames }); };
    requestAnimationFrame(f);
  }));
  await page.context().close();
  must(r.polls >= 1 && r.polls <= r.frames * 1.2 + 2, 'per-frame check ran ' + r.polls + ' times in ' + r.frames + ' frames');
});

row('WIDGET-31', 'in a block layout (the credit in the flow) nothing is polled: no frame is scheduled for the credit, while the observer and the resize listener exist', async () => {
  layoutPage('flowspy.html', '<div><div id="p"></div></div><p>after</p>');
  const page = await ctxPage(`${AURL}/flowspy.html`, {}, { init: SPY });
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  await sleep(300);
  const r = await page.evaluate(async () => {
    const s = window.__spy, p0 = s.poll;
    await new Promise((res) => setTimeout(res, 800));
    return { polls: s.poll - p0, total: s.poll, resize: s.resize.size, ro: s.ro.size };
  });
  await page.context().close();
  must(r.polls === 0 && r.total === 0 && r.resize === 1 && r.ro === 1, 'flow layout: ' + JSON.stringify(r));
});

row('WIDGET-32', 'a responsive host (flex row above 800 px, block below): wide, the per-frame check runs and the container keeps the reserved margin; resized narrow, the check stops (no frames in 500 ms), the credit sits in the flow under the box and the container\'s margin is restored; wide again, it resumes', async () => {
  layoutPage('resp.html', '<style>.row{display:flex}@media (max-width:800px){.row{display:block}}</style><div class="row"><div id="p" style="margin-bottom:10px"></div><aside style="width:200px">sidebar</aside></div><p id="after">after</p>');
  const page = await ctxPage(`${AURL}/resp.html`, { viewport: { width: 1200, height: 900 } }, { init: SPY });
  await waitFor(page, () => window.pl && document.querySelector('.jnext-credit'), null, 120000);
  const probe = (ms) => page.evaluate(async (ms) => {
    const s = window.__spy, p0 = s.poll;
    await new Promise((r) => setTimeout(r, ms));
    const c = document.querySelector('.jnext-credit'), r = c.getBoundingClientRect(), box = document.querySelector('.jnext-player').getBoundingClientRect(),
          l = c.querySelector('a').getBoundingClientRect(), hit = document.elementFromPoint(l.left + l.width / 2, l.top + l.height / 2);
    return { polls: s.poll - p0, margin: document.getElementById('p').style.marginBottom, position: c.style.position, underBox: r.top >= box.bottom - 1,
             overlap: !(r.top >= box.bottom - 0.5 || r.bottom <= box.top + 0.5 || r.left >= box.right - 0.5 || r.right <= box.left + 0.5),
             linkHit: !!hit && !!hit.closest('.jnext-credit'), right: r.right, boxRight: box.right, resize: s.resize.size, ro: s.ro.size };
  }, ms);
  const wide = await probe(500);
  await page.setViewportSize({ width: 700, height: 900 });
  await sleep(300);
  const narrow = await probe(500);
  await page.setViewportSize({ width: 1200, height: 900 });
  await sleep(300);
  const wide2 = await probe(500);
  await page.context().close();
  must(wide.polls > 0 && wide.margin === '36px' && wide.position === 'absolute' && !wide.overlap && wide.linkHit, 'wide: ' + JSON.stringify(wide));
  must(narrow.polls === 0 && narrow.margin === '10px' && narrow.position !== 'absolute' && narrow.underBox && !narrow.overlap && narrow.linkHit &&
       Math.abs(narrow.right - narrow.boxRight) <= 2 && narrow.resize === 1 && narrow.ro === 1, 'narrow: ' + JSON.stringify(narrow));
  must(wide2.polls > 0 && wide2.margin === '36px' && !wide2.overlap && wide2.linkHit, 'wide again: ' + JSON.stringify(wide2));
  must(page.msgs.length === 0, page.msgs.join('; '));
});

row('WIDGET-23', 'lifecycle: destroy() removes the credit, a failed mount leaves none, and two players have one credit each', async () => {
  pages['life.html'] = `<!doctype html><body><div id="a"></div><div id="b"></div><div id="c"></div><script src="jnext-${VERSION}.js"></script></body>`;
  const page = await ctxPage(`${AURL}/life.html`);
  await waitFor(page, () => typeof jnext !== 'undefined', null, 60000);
  const r = await page.evaluate(async () => {
    const n = () => document.querySelectorAll('.jnext-credit').length;
    const pa = await jnext.mount(document.getElementById('a'), { width: 640, height: 512 });
    const pb = await jnext.mount(document.getElementById('b'), { width: 640, height: 512 });
    const two = n();
    await pa.destroy();
    const afterDestroy = n();
    let rejected = false;
    try { await jnext.mount(document.getElementById('c'), { width: 640, height: 512, debug: { protocol: 'dzrp', transport: 'ws' } }); } catch (e) { rejected = true; }
    const afterFail = n();
    await pb.destroy();
    return { two, afterDestroy, rejected, afterFail, end: n() };
  });
  await page.context().close();
  must(r.two === 2 && r.afterDestroy === 1 && r.rejected && r.afterFail === 1 && r.end === 0, 'credit counts ' + JSON.stringify(r));
});

let pass = 0, fail = 0;
for (const r of rows) {
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 600000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
