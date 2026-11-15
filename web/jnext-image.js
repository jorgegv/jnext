// NextZXOS's SD card in the browser: the card is made in the visitor's browser from the
// official distribution zip on specnext.com (fetched by the page when specnext.com
// allows it, or chosen by the visitor after downloading it), checked, kept in the Cache API
// under the card's own SHA-256, and put in the in-memory filesystem. The zip itself is never
// kept. It also persists in IndexedDB the warm-start recording and, for a page that asks
// (keepWrites), the sectors the guest wrote to the card and the file JNEXT's esxDOS
// stand-in keeps for it (one journal file holds both).
//
// Plain script, no dependencies: it defines window.JnextImage (and
// module.exports under a CommonJS loader). Program text is ASCII.
//
//   JnextImage.devSeam()                              -> object | null  (localhost only, see the developer guide)
//   JnextImage.distribution(Module, seam)             -> pins {url, file, size, zipSha256, cardSha256, ...}
//   JnextImage.cachedCard(Module, pins)               -> Promise<{path, sha256} | null>
//   JnextImage.probe(pins, timeoutMs)                 -> Promise<{ok, reason}>
//   JnextImage.fetchDistribution(Module, pins, onProgress)  -> Promise<{path, sha256}>
//   JnextImage.buildFromFile(Module, pins, file, onProgress) -> Promise<{path, sha256}>
//   JnextImage.installImage(Module, image)            -> ['--sdcard', path, '--sdcard-sha256', hex]
//   JnextImage.mountWarmStartStore(Module)            -> Promise
//   JnextImage.cardWritesKey(sha256, pathname)        -> Promise<string>
//   JnextImage.loadCardWrites(Module, key)            -> Promise<path>  (the writes file's in-memory path)
//   JnextImage.saveCardWrites(Module, key)            -> Promise
//
// onProgress(phase, fraction) is the same seam the C++ side reports through
// Module.jnextProgress(phase, fraction): this file sends 'download' and 'build'; the core
// sends 'warm-start' and 'restore'.
//
// The card is copyrighted NextZXOS content: it is never logged, traced or
// stored anywhere but the Cache API entry and the in-memory filesystem. What
// keepWrites stores is only the sectors the guest wrote and that one file (a few KB), in the
// visitor's own IndexedDB; the card itself is never written.
(function (root) {
  'use strict';

  var CACHE_NAME = 'jnext-sdcard-v1';
  // Cache keys of the built card: one per card SHA-256. The host is a reserved name that can
  // never be fetched, so the key is only a label.
  var CARD_HOST = 'https://card.jnext.invalid/';
  var CARD_PATH = '/sdcard/image.img';
  var PROBE_MS = 15000;
  var WARM_START_DIR = '/jnext/warm-start';
  var WRITES_DIR = '/jnext/card-writes';
  var WRITES_DB = 'jnext-card-writes';
  var WRITES_STORE = 'journals';

  function hex(buf) {
    var b = new Uint8Array(buf), s = '';
    for (var i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16);
    return s;
  }

  function digest(bytes) {
    return crypto.subtle.digest('SHA-256', bytes).then(hex);
  }

  function needSecureContext() {
    if (!root.isSecureContext || !root.caches || !root.crypto || !root.crypto.subtle) {
      throw new Error('the SD card needs a secure context (https, or http://localhost): ' +
                      'the Cache API and crypto.subtle are not available here');
    }
  }

  // Read a response body, reporting received/total when the length is known and
  // the body is not content-encoded (otherwise the byte count does not match).
  function readBody(resp, onProgress) {
    var len = resp.headers.get('Content-Length');
    var enc = resp.headers.get('Content-Encoding');
    var total = len && !enc ? parseInt(len, 10) : 0;
    if (!resp.body || !resp.body.getReader) {
      return resp.arrayBuffer().then(function (ab) { return new Uint8Array(ab); });
    }
    var reader = resp.body.getReader(), chunks = [], got = 0;
    function pump() {
      return reader.read().then(function (r) {
        if (r.done) return null;
        chunks.push(r.value);
        got += r.value.length;
        if (total > 0 && onProgress) onProgress('download', Math.min(got / total, 1));
        return pump();
      });
    }
    return pump().then(function () {
      var out = new Uint8Array(got), off = 0;
      for (var i = 0; i < chunks.length; i++) { out.set(chunks[i], off); off += chunks[i].length; }
      return out;
    });
  }

  function fetchImage(spec, onProgress) {
    var url = spec && spec.url, want = spec && spec.sha256;
    return Promise.resolve().then(function () {
      if (!url) throw new Error('no image url');
      if (!/^[0-9a-f]{64}$/i.test(want || '')) throw new Error('the image sha256 is not 64 hex digits');
      want = want.toLowerCase();
      needSecureContext();
      return caches.open(CACHE_NAME);
    }).then(function (cache) {
      function done(bytes) {
        if (onProgress) onProgress('download', 1);
        return { bytes: bytes, sha256: want,
                 zipped: bytes.length > 3 && bytes[0] === 0x50 && bytes[1] === 0x4b && bytes[2] === 3 && bytes[3] === 4 };
      }
      return cache.match(url).then(function (hit) {
        if (!hit) return null;
        return hit.arrayBuffer().then(function (ab) {
          var bytes = new Uint8Array(ab);
          return digest(bytes).then(function (got) {
            if (got === want) return done(bytes);
            // Corrupted or replaced: forget it and go to the network.
            return cache.delete(url).then(function () { return null; });
          });
        });
      }).then(function (cached) {
        if (cached) return cached;
        if (onProgress) onProgress('download', 0);
        return fetch(url, { mode: 'cors', credentials: 'omit', cache: 'no-store' }).then(function (resp) {
          if (!resp.ok) throw new Error('cannot fetch ' + url + ': HTTP ' + resp.status);
          return readBody(resp, onProgress);
        }, function (e) {
          if (e instanceof TypeError) {
            throw new Error('cannot fetch ' + url + ': a network error, or the server does not allow ' +
                            'cross-origin reads (no Access-Control-Allow-Origin header)');
          }
          throw e;
        }).then(function (bytes) {
          return digest(bytes).then(function (got) {
            if (got !== want) throw new Error('SHA-256 mismatch for ' + url + ': got ' + got + ', expected ' + want);
            // A quota error is not fatal: this session goes on, the next visit downloads again.
            return cache.put(url, new Response(bytes)).catch(function (e) {
              console.warn('jnext: the image could not be cached: ' + e);
            }).then(function () { return done(bytes); });
          });
        });
      });
    });
  }

  // ---- the card made from the official distribution zip ---------------------------------------

  // The development and test seam: honoured on the developer's own machine only (localhost),
  // read once per mount. On any other origin it is ignored, with one console warning.
  var warned = false;
  function devSeam() {
    var d = root['jnextDev'];
    if (d === undefined || d === null) return null;
    var h = root.location && root.location.hostname;
    if (h === 'localhost' || h === '127.0.0.1' || h === '[::1]') return d;
    if (!warned) { warned = true; console.warn('jnext: jnextDev is honoured only on localhost; ignored'); }
    return null;
  }

  function lastElement(url) {
    var p = String(url).split('#')[0].split('?')[0];
    return p.substring(p.lastIndexOf('/') + 1);
  }

  function takeString(Module, ptr) {
    var s = Module.UTF8ToString(ptr);
    Module._free(ptr);
    return s;
  }

  // What this release accepts (from the emulator's pins), with the seam's stand-in distribution
  // laid over it when there is one.
  function distribution(Module, seam) {
    var p = JSON.parse(takeString(Module, Module._jnext_card_distribution()));
    var pins = { url: p.url, file: p.file, size: p.size, zipSha256: p.zipSha256, cardSha256: p.cardSha256,
                 recipe: p.recipe, standIn: false };
    var d = seam && seam.distribution;
    if (d && typeof d === 'object') {
      pins.url = d.url;
      pins.file = lastElement(d.url) || pins.file;
      pins.size = d.size;
      pins.zipSha256 = d.sha256;
      if (typeof d.cardSha256 === 'string') pins.cardSha256 = d.cardSha256;
      pins.standIn = true;
    }
    return pins;
  }

  function cardKey(pins) { return CARD_HOST + pins.cardSha256; }

  // The card kept by an earlier visit: re-hashed every time (a corrupted entry is deleted and
  // reported as a miss); a hit also removes any OTHER card entry (an older release's).
  function cachedCard(Module, pins) {
    return Promise.resolve().then(function () {
      needSecureContext();
      return caches.open(CACHE_NAME);
    }).then(function (cache) {
      var key = cardKey(pins);
      return cache.match(key).then(function (hit) {
        if (!hit) return null;
        return hit.arrayBuffer().then(function (ab) {
          var bytes = new Uint8Array(ab);
          return digest(bytes).then(function (got) {
            if (got !== pins.cardSha256) return cache.delete(key).then(function () { return null; });
            return cache.keys().then(function (keys) {
              return Promise.all(keys.filter(function (k) { return k.url.indexOf(CARD_HOST) === 0 && k.url !== key; })
                                     .map(function (k) { return cache.delete(k); }));
            }).then(function () {
              Module.FS.mkdirTree('/sdcard');
              Module.FS.writeFile(CARD_PATH, bytes);
              return { path: CARD_PATH, sha256: got };
            });
          });
        });
      });
    });
  }

  // Can the page read the official file? One CORS HEAD: it needs no preflight and costs a
  // few hundred bytes. "Not available" is a refusal, an abort, an error status or a length
  // that is not the official file's; the reason is kept for the panel.
  function probe(pins, timeoutMs) {
    var ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
    var timer = ctl ? setTimeout(function () { ctl.abort(); }, timeoutMs > 0 ? timeoutMs : PROBE_MS) : null;
    var init = { method: 'HEAD', mode: 'cors', credentials: 'omit', cache: 'no-store' };
    if (ctl) init.signal = ctl.signal;
    return fetch(pins.url, init).then(function (resp) {
      if (timer) clearTimeout(timer);
      if (!resp.ok) return { ok: false, reason: 'specnext.com answered HTTP ' + resp.status };
      var len = resp.headers.get('Content-Length');
      if (len && pins.size && parseInt(len, 10) !== pins.size) {
        return { ok: false, reason: 'the file at that address is ' + len + ' bytes, not the official ' + pins.size };
      }
      return { ok: true, reason: '' };
    }, function () {
      if (timer) clearTimeout(timer);
      return { ok: false, reason: ctl && ctl.signal.aborted
        ? 'the check for the file timed out'
        : 'specnext.com does not allow this page to read the file (or you are offline)' };
    });
  }

  function expectJson(pins) {
    if (!pins.standIn) return 0;
    return JSON.stringify({ file: pins.file, zipSha256: pins.zipSha256, size: pins.size, cardSha256: pins.cardSha256 });
  }

  // Push a ReadableStream of the official zip through the builder, one chunk at a time. The
  // zip is never held: each chunk goes through one reusable staging buffer into the wasm
  // builder, which keeps a few MB. Reports `phase` as bytes/total, then 'build' 1; on success
  // the card is in the in-memory filesystem and in the Cache API, under its own SHA-256.
  function buildFromStream(Module, pins, stream, total, phase, onProgress) {
    var reader = stream.getReader();
    var staging = 0, cap = 0, got = 0;
    function say(ph, f) { if (onProgress) onProgress(ph, f); }
    function release() { if (staging) { Module._free(staging); staging = 0; } }
    function refuse() {
      release();
      try { Module.FS.unlink(CARD_PATH); } catch (e) { /* not there */ }
      try { reader.cancel(); } catch (e) { /* done */ }
      var be = new Error(Module.UTF8ToString(Module._jnext_card_error()));
      be.fromBuilder = true;   // a refusal of the builder, as opposed to a failed read of the stream
      return be;
    }
    Module.FS.mkdirTree('/sdcard');
    try { Module.FS.unlink(CARD_PATH); } catch (e) { /* not there */ }
    var pp = Module.stringToNewUTF8(CARD_PATH);
    var ej = expectJson(pins);
    var ep = ej ? Module.stringToNewUTF8(ej) : 0;
    var rc = Module._jnext_card_begin(pp, ep);
    Module._free(pp);
    if (ep) Module._free(ep);
    if (rc !== 0) return Promise.reject(refuse());
    say(phase, 0);
    function pump() {
      return reader.read().catch(function (e) {
        // The source of the bytes failed (a file that changed or moved, a dropped download).
        var re = new Error(e && e.message ? e.message : String(e));
        re.fromRead = true;
        throw re;
      }).then(function (r) {
        if (r.done) return null;
        var v = r.value;
        if (v.length > cap) {
          release();
          cap = Math.max(v.length, 1 << 16);
          staging = Module._malloc(cap);
        }
        Module.HEAPU8.set(v, staging);
        if (Module._jnext_card_feed(staging, v.length) === 3) throw refuse();
        got += v.length;
        if (total > 0) say(phase, Math.min(got / total, 1));
        return pump();
      });
    }
    return pump().then(function () {
      release();
      say(phase, 1);
      say('build', 1);
      if (Module._jnext_card_finish() !== 2) throw refuse();
      var sha = takeString(Module, Module._jnext_card_sha256());
      var bytes = Module.FS.readFile(CARD_PATH);
      return caches.open(CACHE_NAME).then(function (cache) {
        // A quota error is not fatal: this session goes on, the next visit makes the card again.
        return cache.put(cardKey(pins), new Response(bytes)).catch(function (e) {
          console.warn('jnext: the SD card could not be cached: ' + e);
        });
      }).then(function () { return { path: CARD_PATH, sha256: sha }; });
    }, function (e) { release(); throw e; });
  }

  // Download the official zip from specnext.com and build the card from the stream.
  function fetchDistribution(Module, pins, onProgress) {
    return Promise.resolve().then(function () {
      needSecureContext();
      return fetch(pins.url, { mode: 'cors', credentials: 'omit', cache: 'no-store' });
    }).then(function (resp) {
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      if (!resp.body || !resp.body.getReader) throw new Error('this browser cannot stream a download');
      var len = resp.headers.get('Content-Length'), enc = resp.headers.get('Content-Encoding');
      var total = len && !enc ? parseInt(len, 10) : pins.size;
      return buildFromStream(Module, pins, resp.body, total, 'download', onProgress);
    });
  }

  // Build the card from a file the visitor chose. The size is checked first (cheap), the
  // builder checks the rest; a refusal says "not the official file" in the visitor's words.
  function buildFromFile(Module, pins, file, onProgress) {
    var name = pins.file;
    var notIt = function (why) { return new Error('this file is not ' + name + ' from specnext.com (' + why + ')'); };
    if (file.size !== pins.size) {
      return Promise.reject(notIt('it is ' + file.size + ' bytes, the official one is ' + pins.size));
    }
    return Promise.resolve().then(function () {
      needSecureContext();
      var stream = file.stream ? file.stream() : new Response(file).body;
      return buildFromStream(Module, pins, stream, file.size, 'build', onProgress);
    }).catch(function (e) {
      var m = e && e.message ? e.message : String(e);
      if (/secure context/.test(m)) throw e;
      // A failed READ of the chosen file (it changed or moved after it was chosen): the visitor's
      // file is not at fault.
      if (e && e.fromRead) throw new Error('the chosen file could not be read: ' + m);
      if (!e || !e.fromBuilder) throw new Error('the card could not be made in this browser: ' + m);
      if (/^this is not /.test(m)) throw notIt('its SHA-256 differs');
      // The card could not be made or stored here (memory, storage, the builder's own check of its
      // result): the official file would fail the same way, so it is not the file's fault.
      if (/not the expected one|^cannot |^destination exists|^inflateInit2|^f_mkfs|^metadata of|^f_(open|write|mkdir)|too few clusters/.test(m)) {
        throw new Error('the card could not be made in this browser: ' + m);
      }
      throw notIt(m);
    });
  }

  function installImage(Module, image) {
    var path = '/sdcard/' + (image.zipped ? 'image.zip' : 'image.img');
    Module.FS.mkdirTree('/sdcard');
    Module.FS.writeFile(path, image.bytes);
    return ['--sdcard', path, '--sdcard-sha256', image.sha256];
  }

  // IndexedDB on the warm-start directory ONLY: the card itself stays in memory, and the
  // guest's writes to it are kept apart (keepWrites, below). The page sets ENV.JNEXT_CONFIG_DIR = '/jnext'
  // before callMain so that the core's cache directory is this one.
  function mountWarmStartStore(Module) {
    var FS = Module.FS;
    var IDBFS = Module.IDBFS || (FS.filesystems && FS.filesystems.IDBFS);
    if (!IDBFS) return Promise.reject(new Error('this build has no IDBFS'));
    FS.mkdirTree(WARM_START_DIR);
    FS.mount(IDBFS, {}, WARM_START_DIR);
    return new Promise(function (res, rej) {
      FS.syncfs(true, function (e) { if (e) rej(e); else res(); });
    });
  }

  // ---- keepWrites: the guest's sector writes, one record per (image, page) ----------------
  //
  // The core keeps them in a journal file (--sdcard-writes) in the in-memory filesystem. This
  // side stores that file whole in its own database, and puts it back before the next start.
  // A database of its own, created only for a page that uses keepWrites: the warm-start store
  // stays the only one a plain page ever makes.

  // <image sha256>-<first 16 hex digits of SHA-256(page path)>: a page and an image each get
  // their own saves; the query string is not part of it, so a tracking parameter loses nothing.
  function cardWritesKey(sha256, pathname) {
    return digest(new TextEncoder().encode(pathname)).then(function (h) {
      return sha256.toLowerCase() + '-' + h.substring(0, 16);
    });
  }

  function writesPath(key) { return WRITES_DIR + '/' + key + '.jsdw'; }

  function openWritesDb() {
    return new Promise(function (res, rej) {
      var r = indexedDB.open(WRITES_DB, 1);
      r.onupgradeneeded = function () { r.result.createObjectStore(WRITES_STORE); };
      r.onsuccess = function () { res(r.result); };
      r.onerror = function () { rej(r.error); };
    });
  }

  // One request on the store, the database closed afterwards.
  function writesRequest(mode, make) {
    return openWritesDb().then(function (db) {
      return new Promise(function (res, rej) {
        var tx = db.transaction(WRITES_STORE, mode);
        var req = make(tx.objectStore(WRITES_STORE));
        tx.oncomplete = function () { db.close(); res(req.result); };
        tx.onerror = tx.onabort = function () { db.close(); rej(tx.error || req.error); };
      });
    });
  }

  function loadCardWrites(Module, key) {
    var path = writesPath(key);
    return writesRequest('readonly', function (st) { return st.get(key); }).then(function (rec) {
      if (rec) {
        Module.FS.mkdirTree(WRITES_DIR);
        Module.FS.writeFile(path, rec instanceof Uint8Array ? rec : new Uint8Array(rec));
      }
      return path;
    });
  }

  // The writes file as it is now: stored when it exists, the record deleted when it does not
  // (the core removes it for --sdcard-writes-reset and for resetCard()). The file is read
  // before anything is awaited, so what is stored is what the core had at the call.
  function saveCardWrites(Module, key) {
    var path = writesPath(key), bytes = null;
    if (Module.FS.analyzePath(path).exists) bytes = Module.FS.readFile(path);
    return writesRequest('readwrite', function (st) {
      return bytes ? st.put(bytes, key) : st.delete(key);
    });
  }

  var api = { fetchImage: fetchImage, installImage: installImage, mountWarmStartStore: mountWarmStartStore,
              devSeam: devSeam, distribution: distribution, cachedCard: cachedCard, probe: probe,
              fetchDistribution: fetchDistribution, buildFromFile: buildFromFile,
              cardWritesKey: cardWritesKey, loadCardWrites: loadCardWrites, saveCardWrites: saveCardWrites,
              CACHE_NAME: CACHE_NAME, WARM_START_DIR: WARM_START_DIR, CARD_HOST: CARD_HOST };
  root.JnextImage = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
