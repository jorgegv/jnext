// Driver of `make bench-web` (run by browser-bench.sh on the SDK's node, under taskset):
// the regression NEX set in the host's Chrome, Chrome at 2x CPU throttle and the host's Firefox.
// See the developer guide, "The browser build". It serves an allow-list on 127.0.0.1 only, queues
// the cells, collects each program's raw measurement from bench.js and does all the arithmetic in
// bench-stats.mjs. Nothing here asserts a speed: the numbers go to a results file and the issue.
//
// Environment (set by browser-bench.sh): BENCH_ROOT BENCH_BUILD BENCH_VERSION BENCH_SCRATCH
// BENCH_SHA256 (the run-time card's digest) BENCH_OUT BENCH_CONFIGS BENCH_SET BENCH_PROGRAMS
// BENCH_W BENCH_M BENCH_PROFILE BENCH_CORES JNEXT_PLAYWRIGHT_DIR [JNEXT_BENCH_CHROME].
import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
import os from 'os';
import { spawn, execFileSync } from 'child_process';
import { judge, needsRepeat, settle, checkComplete, formatLine, paceMetrics, formatPaceLine, markdownTable, speedLabel, metrics, percentile } from './bench-stats.mjs';

const E = process.env;
const ROOT = E.BENCH_ROOT, BUILD = E.BENCH_BUILD, VERSION = E.BENCH_VERSION, SCRATCH = E.BENCH_SCRATCH;
const W = Number(E.BENCH_W || 250), M = Number(E.BENCH_M || 600), CAP_MS = 60000;
const CONFIGS = (E.BENCH_CONFIGS || 'chrome,chrome-x2,firefox').split(',');
const PROFILE = E.BENCH_PROFILE ? E.BENCH_PROFILE.split(',') : [];
const LOAD_MAX = 1.5, QUIET_WAIT_MS = 30 * 60 * 1000;
// Playwright puts the browser's profile (which caches the page's SD card) and its artifacts in os.tmpdir():
// aim that at the scratch directory, so a killed run leaves nothing in /tmp and the wrapper's rm removes all.
fs.mkdirSync(path.join(SCRATCH, 'tmp'), { recursive: true });
process.env.TMPDIR = path.join(SCRATCH, 'tmp');
const require = createRequire(E.JNEXT_PLAYWRIGHT_DIR + '/');
const { chromium } = require('playwright-core');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (...a) => console.log('bench-web:', ...a);
const childEnv = { PATH: E.PATH, HOME: E.HOME, LANG: "C", LC_ALL: "C", ...(E.DISPLAY ? { DISPLAY: E.DISPLAY } : {}), ...(E.XDG_RUNTIME_DIR ? { XDG_RUNTIME_DIR: E.XDG_RUNTIME_DIR } : {}) };

// ---- the program set ---------------------------------------------------------------------------
const nexIn = (dir) => fs.readdirSync(path.join(ROOT, dir)).filter((f) => f.endsWith('.nex')).sort().map((f) => ({ name: f.slice(0, -4), dir, file: f }));
const ACCEPT = [...nexIn('test/00regression/nex'), ...nexIn('test/bench/nex').filter((p) => p.name === 'halt_idle_35')];
const INFORM = [...nexIn('test/nex-nonworking'), ...nexIn('test/bench/nex').filter((p) => p.name !== 'halt_idle_35')];
const URLDIR = { 'test/00regression/nex': 'regression', 'test/bench/nex': 'bench', 'test/nex-nonworking': 'nonworking' };
let programs = [...ACCEPT.map((p) => ({ ...p, acceptance: true })), ...INFORM.map((p) => ({ ...p, acceptance: false }))];
if (new Set(programs.map((p) => p.name)).size !== programs.length) throw new Error('duplicate program names across the NEX directories');
const sel = E.BENCH_SET ? E.BENCH_SET.split(',') : (PROFILE.length ? PROFILE : null);
if (sel) {
  const have = new Set(programs.map((p) => p.name));
  for (const s of sel) if (!have.has(s)) throw new Error('unknown program ' + s);
  programs = programs.filter((p) => sel.includes(p.name));
}
const byName = new Map(programs.map((p) => [p.name, p]));
log(`${programs.length} programs (${programs.filter((p) => p.acceptance).length} acceptance), configs ${CONFIGS.join(',')}, W=${W} M=${M}`);

// ---- the allow-list server -----------------------------------------------------------------------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.nex': 'application/octet-stream', '.img': 'application/octet-stream' };
const refused = [];
let queue = [], sink = null;       // Firefox: the cells still to hand out, and where results go
let inflight = null, lastActivity = Date.now();
function fileFor(p) {
  let m;
  if (p === '/bench.html' || p === '/bench.js') return path.join(ROOT, 'test/bench/web', p.slice(1));
  if (p === `/build/jnext-${VERSION}.js` || p === `/build/jnext-${VERSION}.wasm`) return path.join(BUILD, p.slice(7));
  if (p === '/scratch/card.img') return path.join(SCRATCH, 'card.img');
  if ((m = /^\/nex\/(regression|bench|nonworking)\/([A-Za-z0-9_.-]+\.nex)$/.exec(p))) {
    const dir = Object.keys(URLDIR).find((d) => URLDIR[d] === m[1]);
    return path.join(ROOT, dir, m[2]);
  }
  return null;
}
const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  lastActivity = Date.now();
  if (req.method === 'GET' && u.pathname === '/next') {
    const c = queue.shift();
    inflight = c || null;
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify(c || { done: true }));
    return;
  }
  if (req.method === 'POST' && u.pathname === '/result') {
    const chunks = [];
    req.on('data', (d) => chunks.push(d));
    req.on('end', () => { inflight = null; if (sink) sink(JSON.parse(Buffer.concat(chunks).toString())); res.writeHead(204); res.end(); });
    return;
  }
  const f = req.method === 'GET' ? fileFor(u.pathname) : null;
  if (!f || !fs.existsSync(f)) { refused.push(u.pathname); res.writeHead(404); res.end('not in the allow-list'); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream', 'Content-Length': body.length, 'Cache-Control': 'no-store' });
  res.end(body);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const ORIGIN = `http://127.0.0.1:${server.address().port}`;
const MODULE = `/build/jnext-${VERSION}.js`;

const cellFor = (config, p, run) => ({
  program: p.name, config, run, W, M, capMs: CAP_MS, readyMs: 240000, module: MODULE,
  file: `/nex/${URLDIR[p.dir]}/${p.file}`, image: { url: '/scratch/card.img', sha256: E.BENCH_SHA256 },
});

// ---- quiet host --------------------------------------------------------------------------------
const load1 = () => Number(fs.readFileSync('/proc/loadavg', 'utf8').split(' ')[0]);
async function waitQuiet(label) {
  const t0 = Date.now();
  let l = load1();
  while (E.BENCH_QUIET_WAIT !== '0' && l > LOAD_MAX && Date.now() - t0 < QUIET_WAIT_MS) {
    log(`${label}: load ${l.toFixed(2)} > ${LOAD_MAX}, waiting`);
    await sleep(30000);
    l = load1();
  }
  return { load: l, loaded: l > LOAD_MAX };
}

// ---- Chrome ------------------------------------------------------------------------------------
const chromePath = E.JNEXT_BENCH_CHROME || '/usr/bin/google-chrome';
let chromeVersion = '', firefoxVersion = '';
// Firefox routes (BENCH_FIREFOX): `xvfb` (default) = MOZ_HEADLESS=1 on a private Xvfb display: WebGL on the GPU;
// `headed` = a headed Firefox on that display: WebGL in SOFTWARE (Xvfb has no DRI: llvmpipe), so its cells are
// INVALID (software_renderer); `headless` = `firefox --headless` with no Xvfb and no DISPLAY: no WebGL at all.
const FIREFOX_MODE = ['headed', 'headless'].includes(E.BENCH_FIREFOX) ? E.BENCH_FIREFOX : 'xvfb';
let ffProc = null, chromeBrowser = null;

// INT and TERM: kill the Firefox process group (timeout, xvfb-run, Xvfb, Firefox: it was spawned detached, so a
// signal to this process never reaches it) and close Chrome, then leave. browser-bench.sh removes the scratch
// directory afterwards, so nothing may still be running to recreate files in it.
let dying = false;
async function die(code) {
  if (dying) return;
  dying = true;
  try {
    if (ffProc) {
      try { process.kill(-ffProc.pid, 'SIGTERM'); } catch (e) { /* gone */ }
      await sleep(1500);
      try { process.kill(-ffProc.pid, 'SIGKILL'); } catch (e) { /* gone */ }
    }
    if (chromeBrowser) await Promise.race([chromeBrowser.close().catch(() => {}), sleep(3000)]);
  } finally { process.exit(code); }
}
// Once dying, start nothing more: a pass, a cell or a Firefox launched after die() has looked would outlive it.
const parkIfDying = async () => { if (dying) await new Promise(() => {}); };
process.on('SIGINT', () => { die(130); });
process.on('SIGTERM', () => { die(143); });
// --use-angle=gl-egl: headless Chrome's default is SwiftShader (software WebGL); a visitor's Chrome
// draws on the GPU (the results file records the renderer used).
const CHROME_ARGS = ['--mute-audio', '--autoplay-policy=no-user-gesture-required', '--use-angle=gl-egl', ...(E.BENCH_CHROME_ARGS ? E.BENCH_CHROME_ARGS.split(' ') : [])];

// Group `Profiler` nodes into the report's classes by function name (self time).
function splitProfile(prof) {
  const byId = new Map(prof.nodes.map((n) => [n.id, n]));
  const self = new Map();
  const dt = prof.timeDeltas;
  prof.samples.forEach((id, i) => { self.set(id, (self.get(id) || 0) + (dt[i] || 0)); });
  const fn = new Map();
  let total = 0;
  for (const [id, us] of self) { const nm = byId.get(id).callFrame.functionName || '(anonymous)'; fn.set(nm, (fn.get(nm) || 0) + us); total += us; }
  const cls = { core: 0, render: 0, audio: 0, upload_present: 0, js_glue: 0, idle_gc_other: 0 };
  for (const [nm, us] of fn) {
    if (/\b(Renderer|render_row|compose|Compositor|ULA|Ula|Layer2|Tilemap|Sprites|Lores|ScanlineRenderer|render_)/.test(nm)) cls.render += us;
    else if (/(Mixer|mixer|Audio|audio|SDL_.*Audio|Ay|AY|Turbosound|Dac|Beeper|onaudioprocess|ScriptProcessor)/.test(nm)) cls.audio += us;
    else if (/(texImage|texSubImage|SDL_RenderPresent|SDL_UpdateTexture|GLES2|glTex|glDraw|webgl|WebGL|GL_|emscripten_webgl|renderPresent)/i.test(nm)) cls.upload_present += us;
    else if (/(Emulator|Z80|Cpu|cpu|z80|Copper|DMA|Dma|Ctc|Memory|Mmu|Port|tick|run_frame|execute|Nextreg|Sd|Spi|Layer|Device)/.test(nm)) cls.core += us;
    else if (/^(\(idle\)|\(program\)|\(garbage collector\)|\(root\))/.test(nm)) cls.idle_gc_other += us;
    else cls.js_glue += us;
  }
  // Percentages are of the BUSY time: the window includes the idle between callbacks, reported apart.
  const idle = fn.get('(idle)') || 0;
  const busy = total - idle;
  cls.idle_gc_other -= idle;
  const top = [...fn].filter(([n]) => n !== '(idle)').sort((a, b) => b[1] - a[1]).slice(0, 20).map(([n, us]) => ({ fn: n, pct: 100 * us / busy }));
  return { total_ms: total / 1000, idle_pct_of_window: 100 * idle / total,
           classes: Object.fromEntries(Object.entries(cls).map(([k, v]) => [k, 100 * v / busy])), top };
}

async function chromePass(config, cells, onResult) {
  await parkIfDying();
  const browser = await chromium.launch({ executablePath: chromePath, headless: true, args: CHROME_ARGS });
  chromeBrowser = browser;
  chromeVersion = browser.version();
  const ctx = await browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const x2 = config === 'chrome-x2';
  for (const cell of cells) {
    await parkIfDying();
    const page = await ctx.newPage();
    let raw;
    const prof = PROFILE.includes(cell.program);
    let cdp = null, profile = null, chain = Promise.resolve();
    try {
      await page.exposeFunction('benchMark', (what) => {
        if (!prof) return;
        // start and stop in order, and the driver awaits the chain before it reads the profile
        chain = chain.then(async () => {
          try {
            if (what === 'start') { await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 250 }); await cdp.send('Profiler.start'); }
            else if (what === 'stop') { profile = (await cdp.send('Profiler.stop')).profile; }
          } catch (e) { log('profiler ' + what + ' failed: ' + e.message); }
        });
      });
      await page.goto(`${ORIGIN}/bench.html?mode=driver&module=${encodeURIComponent(MODULE)}`);
      cdp = await ctx.newCDPSession(page);
      if (x2) await cdp.send('Emulation.setCPUThrottlingRate', { rate: 2 });
      raw = await Promise.race([
        page.evaluate((c) => window.benchGo(c), cell),
        sleep(cell.readyMs + CAP_MS + 120000).then(() => ({ program: cell.program, config, error: 'cell_timeout', gl: false, costs: [], deltas: [] })),
      ]);
    } catch (e) {
      raw = { program: cell.program, config, error: String(e.message || e).slice(0, 200), gl: false, costs: [], deltas: [] };
    }
    await chain;
    if (profile) {
      const f = path.join(SCRATCH, `${cell.program}.${config}.cpuprofile`);
      fs.writeFileSync(f, JSON.stringify(profile));
      raw.profile = splitProfile(profile);
    }
    await page.close().catch(() => {});
    raw.run = cell.run;
    onResult(raw);
  }
  await browser.close();
  chromeBrowser = null;
}

// ---- Firefox -----------------------------------------------------------------------------------
async function firefoxPass(config, cells, onResult) {
  await parkIfDying();
  const prof = fs.mkdtempSync(path.join(SCRATCH, 'ffprofile-'));
  fs.writeFileSync(path.join(prof, 'user.js'), [
    'user_pref("media.autoplay.default", 0);', 'user_pref("media.autoplay.blocking_policy", 0);',
    'user_pref("media.volume_scale", "0.0");', 'user_pref("dom.max_script_run_time", 0);',
    'user_pref("dom.max_chrome_script_run_time", 0);', 'user_pref("browser.shell.checkDefaultBrowser", false);',
    'user_pref("browser.startup.homepage_override.mstone", "ignore");', 'user_pref("datareporting.policy.dataSubmissionEnabled", false);',
    'user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);', 'user_pref("browser.aboutwelcome.enabled", false);',
    'user_pref("app.update.enabled", false);', 'user_pref("startup.homepage_welcome_url", "");',
    'user_pref("browser.tabs.warnOnClose", false);', 'user_pref("dom.disable_beforeunload", true);',
  ].join('\n') + '\n');
  queue = cells.slice();
  const want = cells.length;
  const got = [];
  sink = (raw) => { got.push(raw); onResult(raw); };
  const url = `${ORIGIN}/bench.html?mode=auto&module=${encodeURIComponent(MODULE)}`;
  const set = E.BENCH_CORES ? ['taskset', '-c', E.BENCH_CORES] : [];
  let proc = null;
  const launch = () => {
    if (dying) return;                          // die() has looked for a Firefox group already
    // Measured with a static page (Firefox 159): with no display connection a headless Firefox gives no canvas
    // any WebGL context, version 1 or 2, any attributes; with the Xvfb display it does. Which renderer it gets
    // depends on the route: MOZ_HEADLESS=1 on Xvfb draws on the GPU (EGL), a headed Firefox draws through the X
    // display, which is Mesa software.
    // -d lets Xvfb pick its display, -noreset keeps it up between Firefox's clients (never -a / -n).
    const xvfb = ['xvfb-run', '-d', '--server-args=-screen 0 1280x1024x24 -noreset', 'firefox'];
    const head = FIREFOX_MODE === 'headless' ? ['firefox', '--headless'] : xvfb;
    const env = { ...childEnv, TMPDIR: path.join(SCRATCH, 'tmp') };      // xvfb-run's directory, Firefox's .xpi files
    if (FIREFOX_MODE === 'headless') { delete env.DISPLAY; delete env.WAYLAND_DISPLAY; }
    if (FIREFOX_MODE === 'xvfb') env.MOZ_HEADLESS = '1';
    proc = spawn('timeout', ['--kill-after=10s', '14400s', ...set, ...head, '--no-remote', '--profile', prof, url],
                 { detached: true, stdio: 'ignore', env });
    ffProc = proc;
  };
  const kill = async () => {
    if (!proc) return;
    try { process.kill(-proc.pid, 'SIGTERM'); } catch (e) { /* gone */ }
    await sleep(3000);
    try { process.kill(-proc.pid, 'SIGKILL'); } catch (e) { /* gone */ }
    proc = null;
    ffProc = null;
  };
  lastActivity = Date.now();
  launch();
  while (got.length < want) {
    await sleep(2000);
    await parkIfDying();
    if (Date.now() - lastActivity > 420000) {          // a hung cell: record it, restart the browser
      const c = inflight || queue[0];
      log(`firefox: no activity for 420 s; cell ${c && c.program} recorded as hung`);
      if (inflight) { sink({ program: inflight.program, config, run: inflight.run, error: 'cell_hung', gl: false, costs: [], deltas: [] }); inflight = null; }
      else if (queue.length) { const x = queue.shift(); sink({ program: x.program, config, run: x.run, error: 'cell_hung', gl: false, costs: [], deltas: [] }); }
      await kill();
      if (got.length < want) { lastActivity = Date.now(); launch(); }
    }
  }
  sink = null;
  await kill();
}

// ---- passes ------------------------------------------------------------------------------------
const header = [];
const results = [];          // settled cells
const rawAll = [];           // every run, for the scratch JSON
const passInfo = [];
let baseCalib = 0;

async function pass(config) {
  await parkIfDying();
  const q = await waitQuiet(config);
  const info = { config, load_start: q.load, loaded: q.loaded, load_end: 0 };
  log(`pass ${config} start, load ${q.load.toFixed(2)}${q.loaded ? ' LOADED' : ''}`);
  const runs = new Map();
  const collect = (raw) => {
    const k = raw.program;
    raw.config = config;
    rawAll.push(raw);
    if (E.BENCH_RAW_DIR) fs.writeFileSync(path.join(E.BENCH_RAW_DIR, `${config}.${raw.program}.${raw.run}.json`), JSON.stringify(raw));
    if (!runs.has(k)) runs.set(k, []);
    runs.get(k).push(raw);
    if (!raw.error && raw.costs.length > 1) {   // stability of the window: p50 over its two halves (IC5)
      const h = raw.costs.length >> 1;
      raw.half_p50 = [percentile(raw.costs.slice(0, h), 50), percentile(raw.costs.slice(h), 50)];
      log(`  halves p50 ${raw.half_p50.map((x) => x.toFixed(2)).join(' / ')} ms, ms_per_frame ${(raw.costs.reduce((a, b) => a + b, 0) / raw.deltas.reduce((a, b) => a + b, 0)).toFixed(3)}`);
    }
    log(`  ${config} ${raw.program} run ${raw.run}: ${raw.error ? 'ERROR ' + raw.error : 'frames ' + raw.deltas.reduce((a, b) => a + b, 0) + ' calib ' + raw.calib_ms.toFixed(1)}`);
  };
  const exec = config === 'firefox' ? firefoxPass : chromePass;
  const set1 = programs.map((p) => cellFor(config, p, 1));
  await exec(config, set1, collect);
  // judge, find repeats
  const ctxFor = () => {
    const all = [...runs.values()].flat().filter((r) => !r.error && r.calib_ms);
    // The pass reference is the MEDIAN calibration: a minimum is one lucky cell, and Firefox's first cell of a
    // pass calibrates 35% faster than every later one.
    const cs = all.map((r) => r.calib_ms).sort((a, b) => a - b);
    const ref = cs.length ? cs[Math.floor((cs.length - 1) / 2)] : 0;
    return { x2: config === 'chrome-x2', base_calib_ms: baseCalib, pass_ref_calib_ms: ref, window_frames: M };
  };
  const judged = (r) => ({ ...judge(r, ctxFor()), raw: r });
  // A cell whose every run was INVALID for a busier host (or a slower CPU state: the calibration is bimodal on
  // this laptop, 58 and 82 ms) is run again, up to three rounds, so the pass ends with a valid run per program.
  const busyOnly = (p) => runs.get(p.name).every((r) => judge(r, ctxFor()).reason === 'host_got_busier');
  for (let round = 0; round < 3 && !PROFILE.length; round++) {
    const busy = programs.filter(busyOnly);
    if (!busy.length) break;
    log(`pass ${config}: ${busy.length} cell(s) all-busy, retry round ${round + 1}: ${busy.map((p) => p.name).join(',')}`);
    await exec(config, busy.map((p) => cellFor(config, p, 4 + round)), collect);
  }
  const rep = [];
  for (const p of programs) {
    const j = runs.get(p.name).map(judged).find((x) => x.verdict !== 'INVALID');
    if (j && needsRepeat({ verdict: j.verdict, m: j.m })) rep.push(p);
  }
  if (rep.length && !PROFILE.length && E.BENCH_NO_REPEAT !== '1') {
    log(`pass ${config}: repeating ${rep.map((p) => p.name).join(',')} twice`);
    await exec(config, rep.flatMap((p) => [cellFor(config, p, 2), cellFor(config, p, 3)]), collect);
  }
  // The unthrottled reference for the x2 band is the chrome pass's own median calibration (the figure the header
  // prints and the busy witness uses). It used to be the median of every program's FIRST run, which on a laptop in
  // its slow CPU state (calibration 82 ms instead of 58) skewed it to 64.9 and made 20 of 39 x2 cells
  // `throttle_not_in_effect` (the P2 table).
  if (config === 'chrome') baseCalib = ctxFor().pass_ref_calib_ms;
  const ctx = ctxFor();
  for (const p of programs) {
    const rs = runs.get(p.name).map((r) => {
      const j = judge(r, ctx);
      return { config, program: p.name, verdict: j.verdict, reason: j.reason, would: j.would, m: j.m, speed: speedLabel(r), calib_ms: r.calib_ms || NaN, raw: r };
    });
    const s = settle(rs);
    results.push({ ...s, acceptance: p.acceptance });
  }
  info.load_end = load1();
  info.calib_ref = ctx.pass_ref_calib_ms;
  info.base_calib = baseCalib;
  passInfo.push(info);
  log(`pass ${config} end, load ${info.load_end.toFixed(2)}`);
}

for (const c of CONFIGS) {
  if (c === 'chrome-x2' && !CONFIGS.includes('chrome') && !baseCalib) throw new Error('chrome-x2 needs the chrome pass for its calibration base');
  await pass(c);
}
server.close();

// ---- output ------------------------------------------------------------------------------------
if (dying) await new Promise(() => {});   // an interrupted run writes no results file: die() is exiting
try { firefoxVersion = execFileSync('firefox', ['--version'], { env: childEnv, timeout: 20000 }).toString().trim(); } catch (e) { firefoxVersion = 'n/a'; }
const sha = execFileSync('git', ['-C', ROOT, 'rev-parse', 'HEAD'], { env: childEnv }).toString().trim();
const renderers = {};
for (const r of rawAll) if (r.renderer) (renderers[r.config] ||= new Set()).add(r.renderer);
const uas = {};
for (const r of rawAll) if (r.ua) (uas[r.config] ||= new Set()).add(r.ua);
const lines = [];
lines.push('# jnext browser benchmark (make bench-web)');
lines.push(`# date:   ${new Date().toISOString().replace('T', ' ').slice(0, 19)} UTC`);
lines.push(`# host:   ${os.hostname()}, ${os.cpus()[0].model}`);
lines.push(`# commit: ${sha}`);
lines.push(`# build:  ${BUILD} (jnext-${VERSION}), emsdk ${E.BENCH_EMSDK || '6.0.13'}`);
lines.push(`# core set: ${E.BENCH_CORES || 'unpinned'} (fastest class, one logical CPU per physical core)`);
lines.push(`# chrome:  ${chromeVersion || 'n/a'} (${chromePath}), args: ${CHROME_ARGS.join(' ')}`);
lines.push(`# firefox: ${firefoxVersion}`);
lines.push(`# firefox mode: ${{ xvfb: 'MOZ_HEADLESS=1 on a private Xvfb display (xvfb-run -d): WebGL on the GPU', headed: 'headed on a private Xvfb display (xvfb-run -d): WebGL in software (llvmpipe)', headless: '--headless with no Xvfb and no DISPLAY: no WebGL' }[FIREFOX_MODE]}`);
for (const [c, s] of Object.entries(renderers)) lines.push(`# webgl renderer ${c}: ${[...s].join(' | ')}`);
for (const [c, s] of Object.entries(uas)) lines.push(`# user agent ${c}: ${[...s].join(' | ')}`);
lines.push(`# window: W=${W} warm-up frames, M=${M} frames or ${CAP_MS / 1000} s; box 640x512; machine next; one wasm instance per cell`);
for (const i of passInfo) lines.push(`# pass ${i.config}: load1 at start ${i.load_start.toFixed(2)}, at end ${i.load_end.toFixed(2)}${i.loaded ? ' LOADED (not counted)' : ''}; calibration median ${i.calib_ref.toFixed(2)} ms${i.config === 'chrome-x2' ? `, unthrottled median ${i.base_calib.toFixed(2)} ms` : ''}`);
const timer = rawAll.filter((r) => r.timer_res_ms).map((r) => r.timer_res_ms);
if (timer.length) lines.push(`# performance.now() resolution observed (median over cells): ${percentile(timer, 50).toFixed(4)} ms`);
lines.push(`# programs: ${programs.length} (acceptance: ${programs.filter((p) => p.acceptance).map((p) => p.name).join(' ')})`);
lines.push('');
for (const r of results) lines.push(formatLine(r) + (r.acceptance ? '' : ' informative=1'));
lines.push('');
for (const r of results) { const p = r.raw && r.verdict !== 'INVALID' ? paceMetrics(r.raw) : null; if (p) lines.push(formatPaceLine(r, p)); }
lines.push('');
let table = '';
try { checkComplete(results, programs.map((p) => p.name), CONFIGS); table = markdownTable(results, programs.map((p) => p.name), CONFIGS); }
catch (e) { table = 'INCOMPLETE: ' + e.message; process.exitCode = 1; }
for (const l of table.split('\n')) lines.push('# ' + l);
const acc = results.filter((r) => r.acceptance);
const fails = acc.filter((r) => r.verdict !== 'PASS');
lines.push(`# acceptance: ${acc.length - fails.length}/${acc.length} cells PASS; not PASS: ${fails.map((r) => `${r.config}/${r.program}=${r.verdict}`).join(' ') || 'none'}`);
if (passInfo.some((i) => i.loaded)) lines.push('# NOTE: at least one pass ran LOADED; it never counts for the acceptance');
fs.writeFileSync(E.BENCH_OUT, lines.join('\n') + '\n');
fs.writeFileSync(path.join(SCRATCH, 'raw.json'), JSON.stringify(rawAll.map((r) => ({ ...r, costs: undefined, deltas: undefined, audio: undefined, stamps: undefined, n: r.deltas && r.deltas.length })), null, 1));
for (const r of rawAll) if (r.profile) {
  console.log(`PROFILE ${r.config} ${r.program}: idle ${r.profile.idle_pct_of_window.toFixed(0)}% of the window; of the busy time: ` + Object.entries(r.profile.classes).map(([k, v]) => `${k}=${v.toFixed(1)}%`).join(' '));
  for (const t of r.profile.top) console.log(`  ${t.pct.toFixed(1).padStart(5)}%  ${t.fn}`);
}
console.log(lines.join('\n'));
if (refused.length) { console.log('bench-web: refused requests: ' + [...new Set(refused)].join(' ')); }
process.exit(process.exitCode || 0);
