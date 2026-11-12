// The SD-card image in the browser: fetch it cross-origin, check its SHA-256 on
// every visit, keep it in the Cache API, put it in the in-memory filesystem, and
// persist two things in IndexedDB: the warm-start recording and, for a page that
// asks (keepWrites), the sectors the guest wrote to the card.
//
// Plain script, no dependencies: it defines window.JnextImage (and
// module.exports under a CommonJS loader). Program text is ASCII.
//
//   JnextImage.fetchImage({url, sha256}, onProgress) -> Promise<{bytes, sha256, zipped}>
//   JnextImage.installImage(Module, image)           -> ['--sdcard', path, '--sdcard-sha256', hex]
//   JnextImage.mountWarmStartStore(Module)           -> Promise
//   JnextImage.cardWritesKey(sha256, pathname)       -> Promise<string>
//   JnextImage.loadCardWrites(Module, key)           -> Promise<path>  (the writes file's in-memory path)
//   JnextImage.saveCardWrites(Module, key)           -> Promise
//
// onProgress(phase, fraction) is the same seam the C++ side reports through
// Module.jnextProgress(phase, fraction): this file sends 'download'; the core
// sends 'warm-start' and 'restore'. The image {url, sha256} is input only.
//
// The card is copyrighted NextZXOS content: it is never logged, traced or
// stored anywhere but the Cache API entry and the in-memory filesystem. What
// keepWrites stores is only the sectors the guest wrote (a few KB), in the
// visitor's own IndexedDB; the card itself is never written.
(function (root) {
  'use strict';

  var CACHE_NAME = 'jnext-sdcard-v1';
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
      throw new Error('the SD-card image needs a secure context (https, or http://localhost): ' +
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
              cardWritesKey: cardWritesKey, loadCardWrites: loadCardWrites, saveCardWrites: saveCardWrites,
              CACHE_NAME: CACHE_NAME, WARM_START_DIR: WARM_START_DIR };
  root.JnextImage = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
