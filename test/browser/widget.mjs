// Browser rows WIDGET-01..07: the ready-made widget (web/jnext-widget.js.in) in headless Chromium
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

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const pages = {};   // virtual pages: name -> html
const A = http.createServer((req, res) => {
  const name = decodeURIComponent(new URL(req.url, 'http://x').pathname.slice(1));
  if (pages[name] !== undefined) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(pages[name]); return; }
  const f = path.join(BUILD, name);
  if (!/^[^/]+$/.test(name) || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(name)] || 'application/octet-stream' });
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
const QUERY = new URLSearchParams({ image: BURL + '/card.img', sha256: SHA, file: BURL + '/red.nex' }).toString();

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

async function open(url, ctxOpts = {}) {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1, ...ctxOpts });
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
  const md = fs.readFileSync(path.join(ROOT, 'src/doc/user-guide/10-web-page/05-two-ways-to-embed.md'), 'utf8');
  const blocks = [...md.matchAll(/```html\n([\s\S]*?)```/g)].map((m) => m[1]);
  must(blocks.length === 2, 'the guide section has ' + blocks.length + ' html blocks, expected 2');
  return blocks.map((b) => b.split('jnext-VERSION.js').join(`jnext-${VERSION}.js`)
    .split('https://example.com/card.img').join(BURL + '/card.img')
    .split('...64 hex digits...').join(SHA)
    .split('game.nex').join(BURL + '/red.nex'));
}

row('WIDGET-01', 'widget.html (the snippet): a framed player with title, toolbar and status; the click starts it; ready after the click', async () => {
  const page = await open(`${AURL}/widget.html?` + QUERY);
  await waitClickable(page);
  const before = await page.evaluate(() => {
    const e = document.querySelector('.jnext-widget');
    return { title: e.querySelector('.jw-title').textContent, buttons: [...e.querySelectorAll('.jw-btn')].map((b) => b.textContent),
             overlay: e.querySelector('.jnext-overlay').textContent, state: e.jnextPlayer.state,
             canvas: e.querySelector('canvas').width > 0 };
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
  const one = `<div class="jnext-widget" data-image="${BURL}/card.img" data-sha256="${SHA}" data-file="${BURL}/red.nex"></div>`;
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
div,span{display:inline;width:3px;padding:30px;margin:11px;border:5px dotted #f0f;background:#ff0;position:relative;float:right}
button{display:block;width:100%;height:80px;padding:20px;margin:9px;background:#f00;color:#0f0;font:italic 40px serif}
body div,body button{text-transform:uppercase;line-height:90px}`;
  const side = `<div id="o1">outside div</div><button id="o2">outside button</button>`;
  pages['hostile.html'] = `<!doctype html><style>${hostile}</style><body>${side}<div class="jnext-widget" data-title="Hostile" data-image="${BURL}/card.img" data-sha256="${SHA}" data-file="${BURL}/red.nex"></div><script src="jnext-widget.js"></script></body>`;
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
    const sheet = document.getElementById('jnext-widget-css').sheet;
    const sel = [];
    (function walk(rules) { for (const r of rules) { if (r.selectorText) sel.push(r.selectorText); else if (r.cssRules) walk(r.cssRules); } })(sheet.cssRules);
    return { frameW: f.getBoundingClientRect().width, tops: bs.map((b) => Math.round(b.top)), widths: bs.map((b) => Math.round(b.width)),
             titleH: tr.height, player: [pb.width, pb.height], overlay: [ob.width, ob.height], stageTop: st.top, playerTop: pb.top, stageW: st.width, sel, sheets: document.querySelectorAll('style').length };
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
  must(info.titleH < 40, 'title bar height ' + info.titleH);
  must(info.sel.length > 10 && info.sel.every((s) => s.split(',').every((p) => p.trim().startsWith('.jnext-widget'))),
       'rules outside .jnext-widget: ' + info.sel.filter((s) => s.split(',').some((p) => !p.trim().startsWith('.jnext-widget'))));
  must(info.sheets === 2, 'style elements ' + info.sheets);   // the page's own and the widget's one
  must(JSON.stringify(outsideBare) === JSON.stringify(outsideWith), 'outside elements changed: ' + outsideBare + ' vs ' + outsideWith);
});

row('WIDGET-06', 'the guide\'s own-page snippet (jnext.mount, verbatim) boots: click to start, then the player runs', async () => {
  pages['own.html'] = guideBlocks()[0];
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 900 }, deviceScaleFactor: 1 });
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
  const ready = await page.evaluate(async () => { const p = await window.mounted; await p.ready; return p.state; });
  await ctx.close();
  must(ready === 'running', 'state ' + ready);
  must(msgs.length === 0, msgs.join('; '));
});

row('WIDGET-07', 'the guide\'s widget snippet (verbatim) boots: the widget loads jnext itself, click to start, ready', async () => {
  pages['guide-widget.html'] = guideBlocks()[1];
  const page = await open(`${AURL}/guide-widget.html`);
  await waitClickable(page);
  await clickStart(page);
  await waitRunning(page);
  const st = await stateOf(page);
  await page.context().close();
  must(st === 'running', 'state ' + st);
  must(page.msgs.length === 0, page.msgs.join('; '));
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
