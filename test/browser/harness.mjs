// Shared by the browser smoke test (and the later API rows): launches the
// pinned headless Chromium and serves every request from an allow-list.
// Nothing is served from anywhere else, so no image, ROM or file outside the
// run's directories can reach the page (and none is logged or traced).
import { createRequire } from 'module';
import fs from 'fs';
import path from 'path';

const env = process.env;
const require = createRequire(env.JNEXT_PLAYWRIGHT_DIR + '/');
const { chromium } = require('playwright-core');

export const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
                      '.wasm': 'application/wasm', '.nex': 'application/octet-stream',
                      '.tap': 'application/octet-stream', '.tzx': 'application/octet-stream',
                      '.img': 'application/octet-stream', '.png': 'image/png' };

// roots: { prefix: directory }, e.g. { build: '/x/build/wasm-core', page: ... }
export class Harness {
  constructor(roots) {
    this.roots = roots; this.requests = []; this.refused = [];
    // Further ORIGINS the page may fetch from (a stand-in image host): { 'http://img.example':
    // { root: dir, headers: {...} } }. Served from `root` by file name, with `headers` added (CORS
    // or not, as the row needs); every request is logged in `originRequests` with its Origin header.
    this.origins = {}; this.originRequests = [];
  }

  async launch(extraArgs = []) {
    this.browser = await chromium.launch({
      executablePath: env.JNEXT_PLAYWRIGHT_DIR + '/chromium/chrome-headless-shell-linux64/chrome-headless-shell',
      args: ['--force-color-profile=srgb', ...extraArgs],
    });
  }

  async newPage(contextOpts = {}) {
    const ctx = await this.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1, ...contextOpts });
    const page = await ctx.newPage();
    page.ctx = ctx;
    await ctx.route('**/*', (route) => {
      const url = new URL(route.request().url());
      this.requests.push(url.pathname);
      if (url.origin !== 'http://localhost') {
        const o = this.origins[url.origin];
        // `passthrough`: a real server (a row's own, on 127.0.0.1): the request goes to the network,
        // so the browser's CORS check is the real one. A response the harness fulfils itself is
        // NOT checked for CORS by Chromium, so a row about a missing header needs this.
        if (o && o.passthrough) {
          this.originRequests.push({ url: url.href, origin: route.request().headers()['origin'] || null });
          return route.continue();
        }
        const f = o && path.resolve(o.root, decodeURIComponent(url.pathname.slice(1)));
        if (!f || !f.startsWith(path.resolve(o.root) + path.sep) || !fs.existsSync(f) || !fs.statSync(f).isFile()) {
          this.refused.push(url.href); return route.abort();
        }
        this.originRequests.push({ url: url.href, origin: route.request().headers()['origin'] || null });
        const body = fs.readFileSync(f);
        return route.fulfill({ status: 200, body, headers: { 'Content-Type': 'application/octet-stream',
                                                            'Content-Length': String(body.length), ...o.headers } });
      }
      const m = /^\/([^/]+)\/(.+)$/.exec(url.pathname);
      const root = m && this.roots[m[1]];
      const file = root && path.resolve(root, decodeURIComponent(m[2]));
      if (!file || !file.startsWith(path.resolve(root) + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
        this.refused.push(url.href);
        return route.fulfill({ status: 404, body: 'not in the allow-list' });
      }
      return route.fulfill({ status: 200, contentType: MIME[path.extname(file)] || 'application/octet-stream',
                             body: fs.readFileSync(file) });
    });
    return page;
  }

  async close() { if (this.browser) await this.browser.close(); }
}

export function pageUrl({ module, args = [], files = [], canvas, autostart, env, image, idbfs }) {
  const q = new URLSearchParams();
  q.set('module', '/build/' + module);
  q.set('args', JSON.stringify(args));
  q.set('files', JSON.stringify(files));
  if (canvas) q.set('canvas', canvas);
  if (autostart) q.set('autostart', '1');
  if (env) q.set('env', JSON.stringify(env));
  if (image) q.set('image', JSON.stringify(image));
  if (idbfs) q.set('idbfs', '1');
  return 'http://localhost/page/index.html?' + q.toString();
}

export async function waitFor(page, fn, arg, timeoutMs = 120000) {
  await page.waitForFunction(fn, arg, { timeout: timeoutMs, polling: 50 });
}

export const frame = (page) => page.evaluate(() => window.jnextModule._jnext_web_frame());
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Wrap AudioContext.createScriptProcessor so a spec can see the contexts SDL
// opens, their state and what reaches the output (SDL's Web Audio backend runs
// on a ScriptProcessorNode). With `report` the page also logs one `__AU {json}`
// console line per second, so a spec can observe WITHOUT page.evaluate: every
// Playwright evaluate runs as a user gesture, which would resume a suspended
// context and defeat a "no gesture" check.
export async function installAudioHook(page, report = false) {
  await page.addInitScript((report) => {
    window.__au = { ctxs: [], calls: 0, max: 0 };
    const orig = AudioContext.prototype.createScriptProcessor;
    AudioContext.prototype.createScriptProcessor = function (...a) {
      window.__au.ctxs.push(this);
      const node = orig.apply(this, a);
      let h = null;
      Object.defineProperty(node, 'onaudioprocess', {
        set(f) {
          h = f;
          node.addEventListener('audioprocess', f);   // SDL fills the buffer first
          node.addEventListener('audioprocess', (e) => {
            window.__au.calls++;
            const d = e.outputBuffer.getChannelData(0);
            let m = 0;
            for (let i = 0; i < d.length; i++) m = Math.max(m, Math.abs(d[i]));
            window.__au.max = Math.max(window.__au.max, m);
          });
        },
        get() { return h; },
      });
      return node;
    };
    if (report) {
      setInterval(() => {
        const a = window.__au;
        console.log('__AU ' + JSON.stringify({
          n: a.ctxs.length, state: a.ctxs[0] && a.ctxs[0].state, calls: a.calls, max: a.max,
          frame: window.jnextModule && window.jnextModule._jnext_web_frame ? window.jnextModule._jnext_web_frame() : -1 }));
      }, 1000);
    }
  }, report);
}

export const audioInfo = (page) => page.evaluate(() => ({
  n: window.__au.ctxs.length, state: window.__au.ctxs[0] && window.__au.ctxs[0].state,
  calls: window.__au.calls, max: window.__au.max }));
