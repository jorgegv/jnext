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
  constructor(roots) { this.roots = roots; this.requests = []; this.refused = []; }

  async launch(extraArgs = []) {
    this.browser = await chromium.launch({
      executablePath: env.JNEXT_PLAYWRIGHT_DIR + '/chromium/chrome-headless-shell-linux64/chrome-headless-shell',
      args: ['--autoplay-policy=user-gesture-required', '--force-color-profile=srgb', ...extraArgs],
    });
  }

  async newPage(contextOpts = {}) {
    const ctx = await this.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1, ...contextOpts });
    const page = await ctx.newPage();
    page.ctx = ctx;
    await ctx.route('**/*', (route) => {
      const url = new URL(route.request().url());
      this.requests.push(url.pathname);
      if (url.origin !== 'http://localhost') { this.refused.push(url.href); return route.abort(); }
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

export function pageUrl({ module, args = [], files = [], canvas, autostart }) {
  const q = new URLSearchParams();
  q.set('module', '/build/' + module);
  q.set('args', JSON.stringify(args));
  q.set('files', JSON.stringify(files));
  if (canvas) q.set('canvas', canvas);
  if (autostart) q.set('autostart', '1');
  return 'http://localhost/page/index.html?' + q.toString();
}

export async function waitFor(page, fn, arg, timeoutMs = 120000) {
  await page.waitForFunction(fn, arg, { timeout: timeoutMs, polling: 50 });
}

export const frame = (page) => page.evaluate(() => window.jnextModule._jnext_web_frame());
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
