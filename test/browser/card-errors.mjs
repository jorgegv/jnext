// Browser-flow rows BCARD-22..24: how the pick path words a refusal (GH #458), by cause. No browser:
// web/jnext-image.js loads under the SDK's node (it exports through module.exports) and
// JnextImage.buildFromFile(Module, pins, file, onProgress) is driven with a STUB Module and a
// File-like object, so every branch is reached with no production hook and no real file.
//   BCARD-22  a failed read of the chosen file: "the chosen file could not be read: ..."
//   BCARD-23  the card could not be made here ("cannot write ..."): "the card could not be made in this browser: ..."
//   BCARD-24  a structural refusal ("not a zip file"): "this file is not <file> from specnext.com (...)"
//   BCARD-25  a failure AFTER a successful build (reading the card back throws): "the card could not be made in this browser: ..."
import { createRequire } from 'module';
import path from 'path';

const ROOT = process.env.SMOKE_ROOT;
globalThis.isSecureContext = true;
globalThis.caches = {};
const J = createRequire(import.meta.url)(path.join(ROOT, 'web/jnext-image.js'));

const PINS = { url: 'https://example.invalid/sn-emulator-test.zip', file: 'sn-emulator-test.zip', size: 4096, zipSha256: '0'.repeat(64),
               cardSha256: '1'.repeat(64), standIn: false };
// A stub of the wasm module: the card exports answer as the row says (feedRc 3 = a refusal with `err`).
function stub(feedRc, err, afterBuild) {
  const mem = new Uint8Array(1 << 16);
  return { FS: { mkdirTree() {}, unlink() {}, readFile() { throw new Error(afterBuild); } }, HEAPU8: mem,
           stringToNewUTF8: () => 8, UTF8ToString: () => err, _free() {}, _malloc: () => 64,
           _jnext_card_begin: () => 0, _jnext_card_feed: () => feedRc, _jnext_card_finish: () => (afterBuild ? 2 : 3),
           _jnext_card_error: () => 1, _jnext_card_sha256: () => 2 };
}
const goodStream = () => ({ getReader: () => { let n = 0; return { read: async () => (n++ ? { done: true } : { done: false, value: new Uint8Array(64) }), cancel() {} }; } });
const failingStream = (msg) => ({ getReader: () => ({ read: () => Promise.reject(new DOMException(msg, 'NotReadableError')), cancel() {} }) });
const file = (stream) => ({ size: PINS.size, stream: () => stream });
const outcome = (p) => p.then(() => 'resolved', (e) => e.message);

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (c, d) => { if (!c) throw new Error(d || 'condition failed'); };

row('BCARD-22', 'a failed read of the chosen file says "the chosen file could not be read: ...", not that it is not the official file', async () => {
  const m = await outcome(J.buildFromFile(stub(0, ''), PINS, file(failingStream('the file changed')), () => {}));
  must(m === 'the chosen file could not be read: the file changed', m);
});

row('BCARD-23', 'the card cannot be made here (cannot write ...): the message says the card could not be made in this browser, not that the file is wrong', async () => {
  const m = await outcome(J.buildFromFile(stub(3, 'cannot write /sdcard/image.img'), PINS, file(goodStream()), () => {}));
  must(m === 'the card could not be made in this browser: cannot write /sdcard/image.img', m);
});

row('BCARD-24', 'a structural refusal (not a zip file) says this file is not the official one, naming the file', async () => {
  const m = await outcome(J.buildFromFile(stub(3, 'not a zip file'), PINS, file(goodStream()), () => {}));
  must(m === 'this file is not sn-emulator-test.zip from specnext.com (not a zip file)', m);
});

row('BCARD-25', 'a failure after the build succeeded (reading the card back throws): the card could not be made in this browser, not that the file is wrong', async () => {
  const m = await outcome(J.buildFromFile(stub(0, 'abc', 'Cannot enlarge memory arrays'), PINS, file(goodStream()), () => {}));
  must(m === 'the card could not be made in this browser: Cannot enlarge memory arrays', m);
});

let pass = 0, fail = 0;
for (const r of rows) {
  try { await r.fn(); pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 400)}]`); }
}
console.log(`\nTotal: ${String(rows.length).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
