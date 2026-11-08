// Browser row WEBPKG-BR-01: the SHIPPED zip boots (make wasm-browser-test, spec package.mjs).
// Run by browser-smoke.sh on the SDK's node, after it extracted jnext-<ver>-web.zip (made by
// `make package-web`) with a foreign reader (python3 -m zipfile) into SMOKE_PKG/jnext-<ver>-web.
//
// Server A serves ONLY that extracted directory (404 for anything else, every request logged):
// the one test that the package, not the build directory, holds everything the page loads.
// Server B stands in for the image host: the run-time minimal card and red.nex, with CORS.
// Oracle: the API's documented events (the user guide's chapter on JNEXT in a web page) and
// WEBAPI-25's own states. The card is copyrighted: it lives in the scratch directory, which the
// script deletes; tracing, HAR and video are never enabled and nothing is uploaded.
import { Harness, waitFor } from './harness.mjs';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, VERSION = E.SMOKE_BUILD_VERSION;
const PKG = path.join(E.SMOKE_PKG, `jnext-${VERSION}-web`);
const SHA = E.SMOKE_CARD_SHA256;

const h = new Harness({});
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.txt': 'text/plain' };
const aLog = [];
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const name = decodeURIComponent(u.pathname.slice(1));
  const f = path.join(PKG, name);
  const known = /^[^/]+$/.test(name) && fs.existsSync(f) && fs.statSync(f).isFile();
  const type = MIMES[path.extname(name)] || 'application/octet-stream';
  aLog.push({ name, status: known ? 200 : 404, type });
  if (!known) { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(f);
  res.writeHead(200, { 'Content-Type': type, 'Content-Length': body.length });
  res.end(body);
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

await h.launch();
const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

row('WEBPKG-BR-01', 'the extracted zip alone boots plain.html: click to start, ready, running; no 404, the wasm served as application/wasm', async () => {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  const msgs = [];
  page.on('pageerror', (e) => msgs.push('PAGEERROR ' + e.message));
  const q = new URLSearchParams({ image: BURL + '/card.img', sha256: SHA, file: BURL + '/red.nex' });
  await page.goto(`${AURL}/plain.html?` + q.toString());
  await waitFor(page, () => document.getElementById('status').textContent === 'click the player to start', null, 300000);
  await page.click('.jnext-overlay', { timeout: 600000, noWaitAfter: true });
  await waitFor(page, () => document.getElementById('status').textContent === 'ready', null, 300000);
  const st = await page.evaluate(() => window.player.state);
  await ctx.close();
  const bad = aLog.filter((r) => r.status === 404);
  const js = aLog.find((r) => r.name === `jnext-${VERSION}.js`), wasm = aLog.find((r) => r.name === `jnext-${VERSION}.wasm`);
  must(bad.length === 0, '404s: ' + JSON.stringify(bad));
  must(js && wasm, 'requests: ' + JSON.stringify(aLog.map((r) => r.name)));
  must(wasm.type === 'application/wasm', 'wasm type ' + wasm.type);
  must(msgs.length === 0, msgs.join('; '));
  must(st === 'running', 'state ' + st);
});

let pass = 0, fail = 0;
for (const r of rows) {
  try {
    await Promise.race([r.fn(), new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), 400000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
