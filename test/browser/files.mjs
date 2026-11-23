// Browser rows BOUT-01..20: the files JNEXT writes, listed and fetched by the page
// (player.listFiles(), readFile(), stop(); GH #553). Run by browser-smoke.sh on the SDK's node.
//
// Oracles: the native runs of browser-smoke.sh's chain_outputs (the same arguments with the output
// directory in place of /tmp, on the SDL frontend and the dummy drivers), the writers' own formats
// (WAV, TZX, ZIP, PNG read by foreign readers), and the contract of src/doc/user-guide/08-web-page/
// 02-options-and-the-player.md. The page's players get the card from a real http server on
// 127.0.0.1 and the card is copyrighted: it lives in the scratch directory and the page only;
// tracing, video and HAR are never enabled. The card image itself is never read by a row: the
// guard rows prove readFile() refuses it, and read it only through the test seam
// (player._module.FS) to compare its SIZE. What an export a mount asks for contains is that mount's
// choice, as on the desktop: BOUT-08 compares a SkoolKit export of a 48K run, ROM space included,
// with the native one.
import { Harness, waitFor, sleep } from './harness.mjs';
import { execFileSync } from 'child_process';
import fs from 'fs';
import http from 'http';
import path from 'path';

const E = process.env;
const BUILD = E.SMOKE_BUILD, ROOT = E.SMOKE_ROOT, SCRATCH = E.SMOKE_SCRATCH, ORACLE = E.SMOKE_ORACLE, WORK = E.SMOKE_WORK;
const VERSION = E.SMOKE_BUILD_VERSION;
const MODULE = `/build/jnext-${VERSION}.js`;
const SHA = E.SMOKE_CARD_SHA256;
const OUT = path.join(ORACLE, 'outputs');

const h = new Harness({});

const B = http.createServer((req, res) => {
  if (req.url !== '/card.img') { res.writeHead(404); res.end(); return; }
  const body = fs.readFileSync(path.join(SCRATCH, 'card.img'));
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': body.length,
                       'Access-Control-Allow-Origin': '*' });
  res.end(body);
});
await new Promise((r) => B.listen(0, '127.0.0.1', r));
const MIMES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const ROOTS = { build: BUILD, page: path.join(ROOT, 'test/browser/page') };
const A = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const m = /^\/([^/]+)\/([^/]+)$/.exec(u.pathname);
  const f = m && ROOTS[m[1]] && path.join(ROOTS[m[1]], decodeURIComponent(m[2]));
  if (!f || !fs.existsSync(f) || !fs.statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIMES[path.extname(f)] || 'application/octet-stream', 'Access-Control-Allow-Origin': '*' });
  res.end(fs.readFileSync(f));
});
await new Promise((r) => A.listen(0, '127.0.0.1', r));
const AURL = `http://localhost:${A.address().port}`;
const BURL = `http://127.0.0.1:${B.address().port}`;
const IMAGE = { url: BURL + '/card.img', sha256: SHA };

await h.launch();
const util = await (await h.browser.newContext()).newPage();   // decodes PNGs
await util.goto('about:blank');

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };

// ---- helpers -------------------------------------------------------------------------------
const OWN = ['/sdcard', '/jnext', '/media'];
const isOwn = (p) => OWN.some((r) => p === r || p.startsWith(r + '/'));
const pages = [];   // every player page opened: each row ends by checking none lists the player's own locations
async function newPage() {
  const ctx = await h.browser.newContext({ viewport: { width: 1000, height: 800 }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.ctx = ctx;
  page.msgs = [];
  page.on('console', (m) => page.msgs.push(m.text()));
  page.on('pageerror', (e) => page.msgs.push('PAGEERROR ' + e.message));
  pages.push(page);
  return page;
}
async function open(page, players) {
  const q = new URLSearchParams({ module: MODULE, image: JSON.stringify(IMAGE), players: JSON.stringify(players) });
  await page.goto(AURL + '/page/api.html?' + q.toString());
  await waitFor(page, (n) => window.apiTest && window.apiTest.players.length === n && window.apiTest.players.every((p) => p.settled), players.length);
}
const errors = (page, i = 0) => page.evaluate((i) => window.apiTest.players[i].errors.slice(), i);
const stateOf = (page, i = 0) => page.evaluate((i) => window['player' + i].state, i);
const click = (page, i = 0) => page.click(`#p${i} .jnext-overlay`, { timeout: 600000, noWaitAfter: true });
const waitExited = (page, i = 0, ms = 600000) => waitFor(page, (i) => window['player' + i].state === 'exited', i, ms);
const waitFrame = (page, i, n, ms = 300000) =>
  waitFor(page, ([i, n]) => window['player' + i]._module._jnext_web_frame() >= n, [i, n], ms);
const list = (page, i = 0) => page.evaluate((i) => window['player' + i].listFiles(), i);
// The test seam: the module's file system, for fixtures and for the SIZE of a file only.
const fsWrite = (page, p, bytes, i = 0) => page.evaluate(([p, b64, i]) => {
  const FS = window['player' + i]._module.FS;
  FS.mkdirTree(p.substring(0, p.lastIndexOf('/')) || '/');
  FS.writeFile(p, Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)));
}, [p, Buffer.from(bytes).toString('base64'), i]);
const fsMkdir = (page, d, i = 0) => page.evaluate(([d, i]) => window['player' + i]._module.FS.mkdirTree(d), [d, i]);
const fsSize = (page, p, i = 0) => page.evaluate(([p, i]) => window['player' + i]._module.FS.stat(p).size, [p, i]);
const fsRead = async (page, p, i = 0) => Buffer.from(await page.evaluate(([p, i]) => {
  const d = window['player' + i]._module.FS.readFile(p); let s = '';
  for (let k = 0; k < d.length; k += 8192) s += String.fromCharCode.apply(null, d.subarray(k, k + 8192));
  return btoa(s);
}, [p, i]), 'base64');
// What the page's readFile() resolves to, as a Buffer; or the rejection message as a string.
async function readF(page, p, i = 0) {
  const r = await page.evaluate(([p, i]) => window['player' + i].readFile(p).then((u8) => {
    let s = ''; for (let k = 0; k < u8.length; k += 8192) s += String.fromCharCode.apply(null, u8.subarray(k, k + 8192));
    return { ok: btoa(s) };
  }, (e) => ({ err: (e instanceof TypeError ? 'TypeError: ' : '') + e.message })), [p, i]);
  return r.ok !== undefined ? Buffer.from(r.ok, 'base64') : r.err;
}
const REFUSED = {
  not: /^jnext: .* is not a file this player's machine wrote$/,
  own: /is one of the player's own files \(the SD card and what the player keeps\); readFile\(\) never returns them$/,
  busy: /is still being written \(listFiles\(\) says when it is complete; stop\(\) ends the machine\)$/,
};
// The native output tree: relative name -> Buffer.
function tree(dir, rel = '') {
  const m = new Map();
  for (const e of fs.readdirSync(path.join(dir, rel), { withFileTypes: true })) {
    const r = rel ? rel + '/' + e.name : e.name;
    if (e.isDirectory()) for (const [k, v] of tree(dir, r)) m.set(k, v);
    else m.set(r, fs.readFileSync(path.join(dir, r)));
  }
  return m;
}
const nat = (sub, name) => fs.readFileSync(path.join(OUT, sub, name));
const sameBytes = (a, b) => Buffer.isBuffer(a) && Buffer.isBuffer(b) && a.length === b.length && a.equals(b);
const differ = (a, b) => {
  if (!Buffer.isBuffer(a)) return 'page: ' + a;
  if (a.length !== b.length) return `length ${a.length} vs native ${b.length}`;
  let i = 0; while (i < a.length && a[i] === b[i]) i++;
  return `first difference at byte ${i}`;
};
const byFile = (l, p) => l.find((e) => e.path === p);
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
// TZX block IDs in order (the walk of tape-save-mic-func): 10, 11, 13 and 20 are all a save writes.
function tzxIds(d) {
  if (d.length < 10 || d.toString('latin1', 0, 8) !== 'ZXTape!\x1A') return null;
  const ids = []; let i = 10;
  while (i < d.length) {
    const id = d[i]; ids.push(id);
    if (id === 0x10) i += 5 + d.readUInt16LE(i + 3);
    else if (id === 0x11) i += 19 + (d[i + 16] | (d[i + 17] << 8) | (d[i + 18] << 16));
    else if (id === 0x13) i += 2 + 2 * d[i + 1];
    else if (id === 0x20) i += 3;
    else return null;
  }
  return ids;
}
const tsd = fs.readFileSync(path.join(ROOT, 'test/00regression/bin/tape_save_demo.bin'));
const dacBin = fs.readFileSync(path.join(SCRATCH, 'outputs-dac.bin'));

// ---- Mount A: every output option a page takes, one run to the automatic exit ----------------
// --speed 99 pins the page and the native run to the same emulated frames: at 100% the audio pacer runs 0,
// 1 or 2 frames in a tick and the exit counts ticks, so the WAV, the profile and the RAM at the exit differ
// from run to run, natively too (measured: the same native command gave 5.33 and 5.67 MB of WAV). Any
// speed but 100% runs one frame in each tick (SdlApp::tick); below 100% every frame is also rendered, which
// the compositor trace's frame 50 counts. --silent cannot be the pin: it refuses --wav-record.
const A_ARGS = ['--speed', '99', '--inject', '/tmp/tape_save_demo.bin', '--magic-port', '0xBEEF', '--magic-port-mode', 'line', '--rtc', '2026-07-10T08:55:00',
  '--tape-save', '/tmp/o.tzx', '--wav-record', '/tmp/o.wav', '--profile', '--profile-output', '/tmp/p.dat',
  '--compositor-trace', '/tmp/c.csv', '--compositor-trace-frame', '50',
  '--delayed-screenshot', '/tmp/s.png', '--delayed-screenshot-frames', '1400',
  '--coverage', '--export-skoolkit', '/tmp/sk', '--export-ghidra', '/tmp/gh', '--bug-report', '/tmp/br.zip',
  '--log-file', '/tmp/j.log', '--delayed-automatic-exit-frames', '1500'];
let mountA = null;
const useA = () => mountA || (mountA = (async () => {
  const page = await newPage();
  await open(page, [{ machine: '48k', args: A_ARGS }]);
  const before = await list(page);
  const stateBefore = await stateOf(page);
  await fsWrite(page, '/tmp/tape_save_demo.bin', tsd);
  await fsMkdir(page, '/tmp/sk'); await fsMkdir(page, '/tmp/gh');
  await click(page);
  await waitFrame(page, 0, 100);
  const mid = await list(page);   // frame 100: the compositor trace (frame 50) is finished, the rest is open
  await waitExited(page);
  const rc = await page.evaluate(() => window.player0.stop());
  return { page, before, stateBefore, mid, rc, files: await list(page) };
})());

row('BOUT-01', 'listFiles(): [] before the click; at frame 100 the tape, WAV and log are open and the compositor trace (frame 50) is complete; after the automatic exit exactly the native run\'s files, every one complete with its FS size; never a file under /sdcard, /jnext or /media', async () => {
  const a = await useA();
  must(a.stateBefore === 'waiting' && a.before.length === 0, `before the click: state ${a.stateBefore}, ${JSON.stringify(a.before)}`);
  must(a.rc === 0, 'stop() after the automatic exit resolved ' + a.rc);
  const open = (p) => a.mid.find((e) => e.path === p);
  must(['/tmp/o.tzx', '/tmp/o.wav', '/tmp/j.log'].every((p) => open(p) && open(p).complete === false) &&
       open('/tmp/c.csv') && open('/tmp/c.csv').complete === true, 'at frame 100: ' + JSON.stringify(a.mid));
  const want = [...tree(path.join(OUT, 'a')).keys()].map((k) => '/tmp/' + k).sort();
  const got = a.files.map((e) => e.path).sort();
  must(JSON.stringify(got) === JSON.stringify(want), `listed ${JSON.stringify(got)} native ${JSON.stringify(want)}`);
  for (const e of a.files) {
    must(e.complete === true, e.path + ' is not complete');
    const sz = await fsSize(a.page, e.path);
    must(e.size === sz, `${e.path}: size ${e.size}, file system ${sz}`);
  }
  must(a.files.every((e) => !isOwn(e.path)), 'a player location is listed');
});

row('BOUT-02', 'readFile() resolves to a new copy each call: equal bytes in distinct Uint8Arrays; changing one changes neither the other nor the file', async () => {
  const a = await useA();
  const r = await a.page.evaluate(async () => {
    const p = window.player0;
    const x = await p.readFile('/tmp/o.tzx'), y = await p.readFile('/tmp/o.tzx');
    const eq = x.length === y.length && x.every((v, i) => v === y[i]);
    const first = x[0];
    x[0] = (x[0] + 1) & 255;
    const z = await p.readFile('/tmp/o.tzx');
    const fsb = p._module.FS.readFile('/tmp/o.tzx');
    return { eq, distinct: x !== y && x.buffer !== y.buffer, other: y[0] === first, again: z[0] === first, file: fsb[0] === first, len: x.length };
  });
  must(r.len > 10 && r.eq && r.distinct && r.other && r.again && r.file, JSON.stringify(r));
});

row('BOUT-03', '--tape-save .tzx: the bytes equal the native run\'s; the data holds ID 0x10 blocks (an empty file cannot pass); same block order as native', async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/o.tzx');
  const n = nat('a', 'o.tzx');
  const ids = Buffer.isBuffer(b) ? tzxIds(b) : null, nids = tzxIds(n);
  must(sameBytes(b, n), 'o.tzx: ' + differ(b, n) + (Buffer.isBuffer(b) ? '' : '') + `; in the file system: ${await a.page.evaluate(() => window.player0._module.FS.analyzePath('/tmp/o.tzx').exists)}`);
  must(ids && ids.includes(0x10) && JSON.stringify(ids) === JSON.stringify(nids), 'ids ' + JSON.stringify(ids));
});

row('BOUT-04', '--wav-record: the bytes equal the native run\'s (44100 Hz), and the RIFF and data sizes match the length', async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/o.wav');
  const n = nat('a', 'o.wav');
  must(Buffer.isBuffer(b), 'o.wav: ' + b);
  must(b.toString('latin1', 0, 4) === 'RIFF' && b.toString('latin1', 36, 40) === 'data' &&
       b.readUInt32LE(4) === b.length - 8 && b.readUInt32LE(40) === b.length - 44 && b.readUInt32LE(24) === 44100 && b.length > 44,
       `header: riff ${b.readUInt32LE(4)}, data ${b.readUInt32LE(40)}, length ${b.length}, rate ${b.readUInt32LE(24)}`);
  must(sameBytes(b, n), 'o.wav vs native: ' + differ(b, n));
});

row('BOUT-05', '--profile-output: the bytes equal the native run\'s and are not empty', async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/p.dat');
  const n = nat('a', 'p.dat');
  must(Buffer.isBuffer(b) && b.length > 0, `p.dat: ${Buffer.isBuffer(b) ? 'empty' : b}; in the file system: ${await a.page.evaluate(() => window.player0._module.FS.analyzePath('/tmp/p.dat').exists)}`);
  must(sameBytes(b, n), 'p.dat vs native: ' + differ(b, n));
});

row('BOUT-06', '--compositor-trace frame 50: the bytes equal the native run\'s: the header line and at least one row', async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/c.csv');
  const n = nat('a', 'c.csv');
  must(Buffer.isBuffer(b), 'c.csv: ' + b);
  const lines = b.toString('latin1').split('\n');
  must(lines[0].startsWith('frame,row,x,') && lines.length > 2, `${lines.length} lines, first ${lines[0]}`);
  must(sameBytes(b, n), 'c.csv vs native: ' + differ(b, n));
});

row('BOUT-07', '--delayed-screenshot: readFile()\'s PNG is the file system\'s bytes and its decoded pixels equal the native run\'s (0 px)', async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/s.png');
  must(Buffer.isBuffer(b), 's.png: ' + b);
  must(sameBytes(b, await fsRead(a.page, '/tmp/s.png')), 'readFile() is not the file system\'s bytes');
  const n = nat('a', 's.png');
  const x = await decode(b), y = await decode(n);
  must(x.w === y.w && x.h === y.h, `size ${x.w}x${x.h} vs native ${y.w}x${y.h}`);
  let px = 0;
  for (let i = 0; i < x.d.length; i += 4) if (x.d[i] !== y.d[i] || x.d[i + 1] !== y.d[i + 1] || x.d[i + 2] !== y.d[i + 2]) px++;
  must(px === 0, px + ' pixels differ; bytes equal: ' + sameBytes(b, n));
});

async function dirRow(a, sub, tag, nat = 'a') {
  const nt = tree(path.join(OUT, nat, sub));
  const pageNames = a.files.map((e) => e.path).filter((p) => p.startsWith(`/tmp/${sub}/`)).map((p) => p.slice(`/tmp/${sub}/`.length)).sort();
  const want = [...nt.keys()].sort();
  must(JSON.stringify(pageNames) === JSON.stringify(want), `${tag}: listed ${JSON.stringify(pageNames)} native ${JSON.stringify(want)}`);
  for (const k of want) {
    const b = await readF(a.page, `/tmp/${sub}/${k}`);
    must(sameBytes(b, nt.get(k)), `${tag}/${k}: ${differ(b, nt.get(k))}`);
  }
  return want.length;
}
row('BOUT-08', '--export-skoolkit: the same file names as the native run and every file byte-equal', async () => {
  const a = await useA();
  const n = await dirRow(a, 'sk', 'sk');
  must(n > 0, 'no skoolkit file');
});
// ---- Mount G: a NEX with its z88dk map beside it, so the Ghidra export has labels (BOUT-09) -------
// Mount A's program has no labels and --export-ghidra writes 0 files for it, natively too. The sidecar
// <stem>.map beside the NEX is found by the core on the page's file system as it is natively.
let mountG = null;
const useG = () => mountG || (mountG = (async () => {
  const page = await newPage();
  await open(page, [{ machine: 'next', args: ['--speed', '99', '--load', '/tmp/magic_bp_demo.nex', '--export-ghidra', '/tmp/gh',
    '--delayed-automatic-exit-frames', '300'] }]);
  const nex = path.join(ROOT, 'test/00regression/nex');
  await fsWrite(page, '/tmp/magic_bp_demo.nex', fs.readFileSync(path.join(nex, 'magic_bp_demo.nex')));
  await fsWrite(page, '/tmp/magic_bp_demo.map', fs.readFileSync(path.join(nex, 'magic_bp_demo.map')));
  await click(page);
  await waitExited(page);
  return { page, files: await list(page) };
})());
row('BOUT-09', '--export-ghidra of a NEX with its map beside it: the same 3 files as the native run (CPU, P04 and the view) and every file byte-equal', async () => {
  const g = await useG();
  const n = await dirRow(g, 'gh', 'gh', 'd');
  must(n === 3, 'the native run wrote ' + n + ' files, expected 3 (CPU, P04, view)');
});

// The members of a zip by a FOREIGN reader (CPython's zipfile): name -> sha256 of the content.
function zipMembers(file) {
  const out = execFileSync('python3', ['-I', '-c', 'import sys, zipfile, hashlib, json\n' +
    'z = zipfile.ZipFile(sys.argv[1])\nassert z.testzip() is None\n' +
    'print(json.dumps({i.filename: hashlib.sha256(z.read(i)).hexdigest() for i in z.infolist()}))', file], { encoding: 'utf8' });
  return JSON.parse(out);
}
// The members whose bytes differ between the page's report and the native one, each with its reason
// (found from a measured diff): the report says when and where it was made.
const BR_DIFFERS = {
  'manifest.json': 'the creation time, the card file name and the sizes of settings.json and log.txt, and the host arch, OS and toolkit',
  'settings.json': 'the argv: the program name, the card path and the page\'s --sdcard-sha256, in the page\'s order',
  'log.txt': 'the timestamps, the audio driver and buffer line, and the page\'s ffmpeg warning',
};
row('BOUT-10', '--bug-report: both zips open in CPython\'s zipfile; the same member names; every member byte-equal except ' +
    (Object.keys(BR_DIFFERS).length ? Object.entries(BR_DIFFERS).map(([k, v]) => `${k} (${v})`).join(', ') : 'none'), async () => {
  const a = await useA();
  const b = await readF(a.page, '/tmp/br.zip');
  must(Buffer.isBuffer(b), 'br.zip: ' + b);
  const pf = path.join(WORK, 'page-br.zip');
  fs.writeFileSync(pf, b);
  const pm = zipMembers(pf), nm = zipMembers(path.join(OUT, 'a', 'br.zip'));
  must(JSON.stringify(Object.keys(pm).sort()) === JSON.stringify(Object.keys(nm).sort()),
       `members ${JSON.stringify(Object.keys(pm))} native ${JSON.stringify(Object.keys(nm))}`);
  const diff = Object.keys(nm).filter((k) => pm[k] !== nm[k]);
  const unexpected = diff.filter((k) => !(k in BR_DIFFERS));
  must(unexpected.length === 0, 'members that differ: ' + JSON.stringify(unexpected) + ' (zips equal as bytes: ' + sameBytes(b, nat('a', 'br.zip')) + ')');
});

row('BOUT-11', '--log-file: complete after the exit and holding the native log\'s export, Tape SAVE, bug report and (the last line of the run) profiler lines (timestamp dropped, the output directory as a token)', async () => {
  const a = await useA();
  const e = byFile(a.files, '/tmp/j.log');
  must(e && e.complete, 'j.log ' + JSON.stringify(e));
  const b = await readF(a.page, '/tmp/j.log');
  must(Buffer.isBuffer(b), 'j.log: ' + b);
  const dir = path.join(OUT, 'a');
  const pick = (text, root) => text.toString('latin1').split('\n')
    .filter((l) => /--export-skoolkit .*: \d+ files|Tape SAVE:|bug report saved:|profiler: wrote \d+ non-zero entries/.test(l))
    .map((l) => l.replace(/^\[[0-9:.]+\] /, '').split(root).join('OUT'));
  const pl = pick(b, '/tmp'), nl = pick(nat('a', 'j.log'), dir);
  must(nl.length >= 3 && JSON.stringify(pl) === JSON.stringify(nl), `page ${JSON.stringify(pl)} native ${JSON.stringify(nl)}`);
});

// ---- Mount B: files still being written, then stop() ------------------------------------------
const B_ARGS = ['--inject', '/tmp/dac.bin', '--inject-org', '8000', '--inject-pc', '8000', '--dac-trace', '/tmp/d.csv',
  '--wav-record', '/tmp/live.wav', '--tape-save', '/tmp/n.tap', '--compositor-trace', '/tmp/late.csv', '--compositor-trace-frame', '1000000'];
let mountB = null;
const useB = () => mountB || (mountB = (async () => {
  const page = await newPage();
  await open(page, [{ machine: 'next', args: B_ARGS }]);
  await fsWrite(page, '/tmp/dac.bin', dacBin);
  await click(page);
  await waitFrame(page, 0, 100);
  const running = await list(page);
  const busy = {};
  for (const e of running) busy[e.path] = await readF(page, e.path);
  return { page, running, busy };
})());

row('BOUT-12', 'running at frame 100 or later: listFiles() has d.csv, live.wav, n.tap and late.csv, each complete false; readFile() of each rejects "still being written"; onError unchanged', async () => {
  const b = await useB();
  const names = b.running.map((e) => e.path).sort();
  must(JSON.stringify(names) === JSON.stringify(['/tmp/d.csv', '/tmp/late.csv', '/tmp/live.wav', '/tmp/n.tap']), JSON.stringify(b.running));
  must(b.running.every((e) => e.complete === false), JSON.stringify(b.running));
  for (const k of names) must(typeof b.busy[k] === 'string' && REFUSED.busy.test(b.busy[k]), `${k}: ${typeof b.busy[k] === 'string' ? b.busy[k] : 'resolved'}`);
  must((await errors(b.page)).length === 0, 'onError ' + JSON.stringify(await errors(b.page)));
});

let stopped = null;
const stopB = async () => {
  const b = await useB();
  if (!stopped) {
    // state and files in the same turn as the resolution: a stop() that resolved before the machine had
    // finished (the exit is a later animation frame) would show an open file here
    const r = await b.page.evaluate(async () => {
      const p = window.player0;
      const rc = await p.stop();
      return { rc, state: p.state, files: p.listFiles() };
    });
    stopped = { ...r, again: await b.page.evaluate(() => window.player0.stop()) };
  }
  return { b, ...stopped };
};
row('BOUT-13', 'stop() resolves 0 with the state exited and all four files complete; d.csv and n.tap equal the native run\'s; live.wav a consistent RIFF; a second stop() resolves 0', async () => {
  const s = await stopB();
  must(s.rc === 0 && s.state === 'exited' && s.again === 0, `rc ${s.rc} state ${s.state} again ${s.again}`);
  must(s.files.length === 4 && s.files.every((e) => e.complete === true), JSON.stringify(s.files));
  const d = await readF(s.b.page, '/tmp/d.csv');
  must(sameBytes(d, nat('b', 'd.csv')), 'd.csv: ' + differ(d, nat('b', 'd.csv')));
  const rows = Buffer.isBuffer(d) ? d.toString('latin1') : '';
  must([17, 34, 51, 68].every((v, i) => new RegExp(`^\\d+,\\d+,${i},${v}$`, 'm').test(rows)), 'DAC rows ' + rows);
  const t = await readF(s.b.page, '/tmp/n.tap');
  must(sameBytes(t, nat('b', 'n.tap')), 'n.tap: ' + differ(t, nat('b', 'n.tap')));
  const w = await readF(s.b.page, '/tmp/live.wav');
  must(Buffer.isBuffer(w) && w.toString('latin1', 0, 4) === 'RIFF' && w.readUInt32LE(4) === w.length - 8 &&
       w.readUInt32LE(40) === w.length - 44 && w.readUInt32LE(40) > 0, 'live.wav ' + (Buffer.isBuffer(w) ? `length ${w.length} riff ${w.readUInt32LE(4)} data ${w.readUInt32LE(40)}` : w));
});

row('BOUT-14', 'late.csv, whose frame never came: complete after stop() and equal to the native run\'s (the header line only)', async () => {
  const s = await stopB();
  const e = byFile(s.files, '/tmp/late.csv');
  const b = await readF(s.b.page, '/tmp/late.csv');
  const n = nat('b', 'late.csv');
  must(e && e.complete, 'late.csv ' + JSON.stringify(e) + '; size in the file system ' + await fsSize(s.b.page, '/tmp/late.csv'));
  must(n.toString('latin1').split('\n').filter(Boolean).length === 1, 'the native file is not the header line only');
  must(sameBytes(b, n), 'late.csv: ' + differ(b, n));
});

row('BOUT-15', 'refusals leave onError alone: readFile(42) a TypeError; an unknown path, a file the test wrote and a file deleted from the file system reject "not a file"; the deleted one leaves listFiles()', async () => {
  const s = await stopB();
  const p = s.b.page;
  const r42 = await p.evaluate(() => window.player0.readFile(42).then(() => 'resolved', (e) => (e instanceof TypeError ? 'TypeError ' : '') + e.message));
  must(/^TypeError jnext: readFile\(\) takes a path that listFiles\(\) gives$/.test(r42), r42);
  for (const k of ['/tmp/nope', '/tmp/dac.bin']) {
    const m = await readF(p, k);
    must(typeof m === 'string' && REFUSED.not.test(m), `${k}: ${typeof m === 'string' ? m : 'resolved'}`);
  }
  await p.evaluate(() => window.player0._module.FS.unlink('/tmp/n.tap'));
  const l = await list(p);
  must(!byFile(l, '/tmp/n.tap') && l.length === 3, JSON.stringify(l));
  const m = await readF(p, '/tmp/n.tap');
  must(typeof m === 'string' && REFUSED.not.test(m), 'deleted: ' + (typeof m === 'string' ? m : 'resolved'));
  must((await errors(p)).length === 0, 'onError ' + JSON.stringify(await errors(p)));
});

row('BOUT-16', 'stop() before the click rejects "the machine is not running"; after destroy() listFiles(), readFile() and stop() each throw "this player was destroyed" synchronously', async () => {
  const page = await newPage();
  await open(page, [{ machine: '48k' }]);
  const pre = await page.evaluate(() => window.player0.stop().then(() => 'resolved', (e) => e.message));
  must(pre === 'jnext: the machine is not running', pre);
  must((await errors(page)).length === 0, 'onError ' + JSON.stringify(await errors(page)));
  const r = await page.evaluate(async () => {
    const p = window.player0;
    await p.destroy();
    const t = (f) => { try { const v = f(); return v instanceof Promise ? 'returned a promise' : 'returned'; } catch (e) { return e.message; } };
    return [t(() => p.listFiles()), t(() => p.readFile('/tmp/x')), t(() => p.stop())];
  });
  must(r.every((m) => m === 'jnext: this player was destroyed'), JSON.stringify(r));
});

// ---- Mount C: the guard -----------------------------------------------------------------------
row('BOUT-17', 'the guard: after the exit listFiles() holds exactly /sdcard.dat; readFile() of the card (spelt with ..), /jnext/br.zip and /media/c.csv rejects as the player\'s own; the card really grew; /sdcard.dat equals the native profile', async () => {
  const page = await newPage();
  await open(page, [{ machine: '48k', args: ['--speed', '99', '--inject', '/media/tape_save_demo.bin', '--magic-port', '0xBEEF', '--magic-port-mode', 'line',
    '--rtc', '2026-07-10T08:55:00', '--tape-save', '/tmp/../sdcard/image.img', '--bug-report', '/jnext/br.zip',
    '--compositor-trace', '/media/c.csv', '--compositor-trace-frame', '5', '--profile', '--profile-output', '/sdcard.dat',
    '--delayed-automatic-exit-frames', '1500'] }]);
  await fsWrite(page, '/media/tape_save_demo.bin', tsd);
  await click(page);
  await waitExited(page);
  const l = await list(page);
  must(JSON.stringify(l.map((e) => e.path)) === JSON.stringify(['/sdcard.dat']) && l[0].complete === true, JSON.stringify(l));
  const cardSize = fs.statSync(path.join(SCRATCH, 'card.img')).size;
  const now = await fsSize(page, '/sdcard/image.img');
  must(now > cardSize, `the card did not grow: ${now} vs ${cardSize}`);
  must(await page.evaluate(() => window.player0._module.FS.analyzePath('/jnext/br.zip').exists &&
                                  window.player0._module.FS.analyzePath('/media/c.csv').exists), 'br.zip or c.csv was not written');
  for (const k of ['/sdcard/image.img', '/tmp/../sdcard/image.img', '/./sdcard/image.img', '/jnext/br.zip', '/media/c.csv', '/media/tape_save_demo.bin']) {
    const m = await readF(page, k);
    must(typeof m === 'string' && REFUSED.own.test(m), `${k}: ${typeof m === 'string' ? m : 'resolved with ' + m.length + ' bytes'}`);
  }
  const prof = await readF(page, '/sdcard.dat');
  must(sameBytes(prof, nat('c', 'p.dat')), 'sdcard.dat vs the native profile: ' + differ(prof, nat('c', 'p.dat')));
  must((await errors(page)).length === 0, 'onError ' + JSON.stringify(await errors(page)));
});

// ---- Mount D: the guide's download button -----------------------------------------------------
function guideSnippet() {
  const md = fs.readFileSync(path.join(ROOT, 'src/doc/user-guide/08-web-page/02-options-and-the-player.md'), 'utf8');
  const sec = md.split('### Files JNEXT writes')[1];
  must(sec, 'the guide has no "Files JNEXT writes" section');
  const m = /```html\n([\s\S]*?)```/.exec(sec.split('\n### ')[0]);
  must(m, 'the section has no html block');
  return m[1];
}
row('BOUT-18', 'the guide\'s download button, run verbatim: the click gives a download named game.wav whose bytes equal readFile() and form a RIFF WAV', async () => {
  const snippet = guideSnippet();
  const page = await newPage();
  await open(page, [{ machine: '48k', args: ['--wav-record', '/tmp/game.wav'] }]);
  await click(page);
  await waitFrame(page, 0, 100);
  await page.evaluate((html) => {
    window.player = window.player0;   // "player is what jnext.mount() resolved to"
    const host = document.createElement('div');
    host.style.cssText = 'position:absolute;left:0;top:300px';
    document.body.appendChild(host);
    const t = document.createElement('template');
    t.innerHTML = html;
    host.appendChild(t.content.querySelector('button'));
    const s = document.createElement('script');
    s.textContent = t.content.querySelector('script').textContent;   // the snippet's own code, unchanged
    document.body.appendChild(s);
  }, snippet);
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#save-wav')]);
  must(dl.suggestedFilename() === 'game.wav', 'download named ' + dl.suggestedFilename());
  const got = fs.readFileSync(await dl.path());
  const want = await readF(page, '/tmp/game.wav');
  must(sameBytes(got, want), 'download vs readFile(): ' + differ(got, want));
  must(got.length > 44 && got.toString('latin1', 0, 4) === 'RIFF' && got.toString('latin1', 8, 12) === 'WAVE' &&
       got.readUInt32LE(4) === got.length - 8 && got.readUInt32LE(40) === got.length - 44, `RIFF: length ${got.length}`);
});

// ---- Mount L: the audio write log (BOUT-19) -------------------------------------------------------
// The program is the audio-log-func fixture (browser-smoke.sh's chain_alog writes the same bytes to SCRATCH/alog.bin and
// runs it natively): it writes the AY, the DAC and an NR mirror once, then spins, so the CSV holds the same rows
// however long the page runs. The oracle is that native run's alog.csv.
row('BOUT-19', '--audio-log: listed complete false while the page runs (and refused as still being written); after stop() complete, with readFile() equal to the native run\'s CSV byte for byte', async () => {
  const want = fs.readFileSync(path.join(ORACLE, 'alog.csv'));
  const page = await newPage();
  await open(page, [{ machine: 'next', args: ['--silent', '--rtc', '2026-07-10T08:55:00', '--inject', '/tmp/alog.bin', '--inject-org', '8000',
                                              '--inject-pc', '8000', '--audio-log', '/tmp/a.csv'] }]);
  await fsWrite(page, '/tmp/alog.bin', fs.readFileSync(path.join(SCRATCH, 'alog.bin')));
  await click(page);
  await waitFrame(page, 0, 100);
  const running = byFile(await list(page), '/tmp/a.csv');
  must(running && running.complete === false, 'while running: ' + JSON.stringify(running));
  const busy = await readF(page, '/tmp/a.csv');
  must(typeof busy === 'string' && REFUSED.busy.test(busy), 'read while running: ' + (typeof busy === 'string' ? busy : 'resolved with ' + busy.length + ' bytes'));
  must((await page.evaluate(() => window.player0.stop())) === 0, 'stop() did not resolve 0');
  const done = byFile(await list(page), '/tmp/a.csv');
  must(done && done.complete === true, 'after stop(): ' + JSON.stringify(done));
  const got = await readF(page, '/tmp/a.csv');
  must(Buffer.isBuffer(got) && got.toString('latin1').includes(',0x801E,cpu,dac_mirror,'), 'the CSV lacks the fixture rows: ' + (Buffer.isBuffer(got) ? got.length + ' bytes' : got));
  must(sameBytes(got, want), 'a.csv vs native: ' + differ(got, want));
});

// ---- Mount Z: a hard reset re-opens finished files (BOUT-20) --------------------------------------
row('BOUT-20', 'a hard reset re-opens the finished compositor trace: listed complete false and refused as still being written, then complete again', async () => {
  const page = await newPage();
  await open(page, [{ machine: '48k', args: ['--tape-save', '/tmp/z.tzx', '--compositor-trace', '/tmp/z.csv', '--compositor-trace-frame', '300'] }]);
  await click(page);
  await waitFrame(page, 0, 400);
  const before = await list(page);
  const zc = byFile(before, '/tmp/z.csv');
  must(zc && zc.complete === true, 'before the reset ' + JSON.stringify(before));
  // ~Emulator finishes the files and init() opens them again: ~Emulator + placement new in a hard reset
  const oldSize = await fsSize(page, '/tmp/z.csv');
  await page.evaluate(() => window.player0.reset('hard'));
  // reset() resolves as soon as the frame counter moves, and the tick that cold-boots renders no frame: the trace is
  // opened again (truncated) in the first render_frame after it. Until the file is smaller than it was, the old,
  // finished trace is what is listed, correctly complete. Wait for the reopen before asserting.
  await waitFor(page, ([old]) => window.player0._module.FS.stat('/tmp/z.csv').size < old, [oldSize], 60000);
  const after = await list(page);
  const za = byFile(after, '/tmp/z.csv');
  must(za && za.complete === false, 'after the reopen (size ' + (za && za.size) + ' below ' + oldSize + ') ' + JSON.stringify(after));
  const rd = await readF(page, '/tmp/z.csv');
  must(typeof rd === 'string' && REFUSED.busy.test(rd), 'read after the reset: ' + (typeof rd === 'string' ? rd : 'resolved with ' + rd.length + ' bytes'));
  const f0 = await page.evaluate(() => window.player0._module._jnext_web_frame());
  await waitFrame(page, 0, f0 + 400);
  const late = byFile(await list(page), '/tmp/z.csv');
  must(late && late.complete === true, 'later ' + JSON.stringify(late));
  must((await page.evaluate(() => window.player0.stop())) === 0, 'stop() did not resolve 0');
});

// ---- run -------------------------------------------------------------------------------------
let pass = 0, fail = 0;
const only = E.JNEXT_BROWSER_ROWS ? E.JNEXT_BROWSER_ROWS.split(',') : null;   // debugging only: a partial run fails the count
// Every row ends by checking that no page that has a player lists one of the player's own locations.
async function noOwnEverywhere() {
  for (const p of pages) {
    const l = await p.evaluate(() => {
      const w = window['player0'];
      return w && w.state !== 'destroyed' ? w.listFiles().map((e) => e.path) : [];
    }).catch(() => []);
    must(l.every((x) => !isOwn(x)), 'a player location is listed: ' + JSON.stringify(l));
  }
}
for (const r of rows) {
  if (only && !only.includes(r.id)) continue;
  try {
    await Promise.race([(async () => { await r.fn(); await noOwnEverywhere(); })(),
                        new Promise((_, rej) => setTimeout(() => rej(new Error('row timed out')), Number(E.JNEXT_ROW_TIMEOUT_MS) || 600000))]);
    pass++; console.log(`PASS ${r.id} ${r.text}`);
  } catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 700)}]`); }
}
A.close(); B.close();
await h.close();
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
