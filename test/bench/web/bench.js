// Page of the browser bench (make bench-web). Test-only, never packaged.
//
//   bench.html?module=/build/jnext-<ver>.js&mode=driver|auto
//
// It measures what the shipped page does: jnext.mount() of the build's own jnext-<ver>.js. The
// measurement is made from outside the module, by two wrappers installed BEFORE it loads:
//   - window.requestAnimationFrame: Emscripten schedules every main-loop iteration through the
//     global rAF, so each callback is one run of the frontend's tick (input, frames, audio push,
//     upload, present); it is timed, and the emulator frames it ran are read from the module's
//     frame counter before and after;
//   - AudioContext.createScriptProcessor / onaudioprocess: SDL's Web Audio handler, timed.
// The page's own bookkeeping uses the ORIGINAL rAF, so it is never in the measurement. The
// arithmetic (percentiles, rates, verdict) is done by the driver (bench-stats.mjs), not here.
//
// mode=driver: the page waits for window.benchGo(cell) (Chrome: the driver applies the CPU
// throttle first, then calls it and takes the returned raw cell).
// mode=auto: it asks GET /next for a cell, runs it, POSTs the raw cell to /result and navigates
// to itself for the next one (Firefox, which has no driver protocol here).
(function () {
  'use strict';
  const q = new URLSearchParams(location.search);
  const origRAF = window.requestAnimationFrame.bind(window);
  const S = { rec: false, mod: null, costs: [], deltas: [], audio: [], ctxs: [], audioCalls: 0, done: false,
              anchorT: 0, anchorF: 0, anchored: false, frames: 0, endT: 0, cell: null, capped: false };
  const live = document.getElementById('live');
  const say = (t) => { live.textContent = t; };

  window.requestAnimationFrame = function (cb) {
    return origRAF(function (ts) {
      if (!S.rec || !S.mod) return cb(ts);
      const f0 = S.mod._jnext_web_frame();
      const t0 = performance.now();
      cb(ts);
      const t1 = performance.now();
      const f1 = S.mod._jnext_web_frame();
      if (!S.anchored) {          // the window starts where the first recorded callback ends
        S.anchored = true; S.anchorT = t1; S.anchorF = f1; return;
      }
      S.costs.push(t1 - t0); S.deltas.push(f1 - f0); S.frames += f1 - f0;
      if (S.frames >= S.cell.M || t1 - S.anchorT >= S.cell.capMs) {
        S.capped = S.frames < S.cell.M;
        S.rec = false; S.done = true; S.endT = t1;
      }
    });
  };

  const origCSP = AudioContext.prototype.createScriptProcessor;
  AudioContext.prototype.createScriptProcessor = function (...a) {
    S.ctxs.push(this);
    const node = origCSP.apply(this, a);
    let h = null;
    Object.defineProperty(node, 'onaudioprocess', {
      set(f) {
        h = f;
        node.addEventListener('audioprocess', function (e) {
          S.audioCalls++;
          if (!S.rec) return f.call(node, e);
          const t = performance.now();
          f.call(node, e);
          S.audio.push(performance.now() - t);
        });
      },
      get() { return h; },
    });
    return node;
  };

  // A fixed integer loop (about 50 ms unthrottled): the witness of the CPU throttle and of host load.
  let sink = 0;
  function kernel() {
    const t0 = performance.now();
    let x = 1;
    for (let i = 0; i < 40000000; i++) x = (Math.imul(x, 1664525) + 1013904223) | 0;
    sink ^= x;
    return performance.now() - t0;
  }
  function calibrate() {
    kernel();                                   // warm the JIT, not recorded
    const r = [kernel(), kernel(), kernel()].sort((a, b) => a - b);
    return r[1];
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const waitFor = (pred, ms) => new Promise((res, rej) => {
    const t0 = performance.now();
    (function tick() {
      if (pred()) return res();
      if (performance.now() - t0 > ms) return rej(new Error('timeout'));
      origRAF(tick);
    })();
  });

  function glInfo(canvas) {
    const gl = canvas.getContext('webgl');
    if (!gl) return { gl: false, renderer: '' };
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    return { gl: true, renderer: String(gl.getParameter(ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER)) };
  }
  const memSize = (m) => {
    try { return m.HEAPU8 ? m.HEAPU8.length : (m.wasmMemory ? m.wasmMemory.buffer.byteLength : -1); } catch (e) { return -1; }
  };

  async function runCell(cell) {
    S.cell = cell;
    const raw = { program: cell.program, config: cell.config, ua: navigator.userAgent, error: null };
    try {
      raw.calib_ms = calibrate();
      raw.timer_res_ms = (() => { let m = 1e9, p = performance.now(); for (let i = 0; i < 2000; i++) { const n = performance.now(); if (n > p) { m = Math.min(m, n - p); p = n; } } return m; })();
      await new Promise((res, rej) => {
        const s = document.createElement('script');
        s.src = cell.module; s.onload = res; s.onerror = () => rej(new Error('module script failed to load'));
        document.head.appendChild(s);
      });
      let ready = false, err = null;
      const player = await window.jnext.mount(document.getElementById('box'), {
        image: cell.image, file: cell.file, machine: 'next', width: 640, height: 512,
        onReady: () => { ready = true; }, onError: (m) => { err = m; },
      });
      S.mod = player._module;
      document.querySelector('.jnext-overlay').click();
      await waitFor(() => ready || err, cell.readyMs);
      if (err) throw new Error(err);
      const f0 = S.mod._jnext_web_frame();
      await waitFor(() => S.mod._jnext_web_frame() >= f0 + cell.W, cell.readyMs);
      raw.period_ms = S.mod._jnext_web_frame_period_ms();
      raw.cpu_mhz = S.mod._jnext_web_cpu_mhz();
      raw.mem_start = memSize(S.mod);
      let ticks = 0, stop = false;
      (function count() { if (!stop) { ticks++; origRAF(count); } })();
      const wall0 = performance.now();
      S.rec = true;
      await waitFor(() => S.done, cell.capMs + 30000);
      stop = true;
      raw.wall_ms = S.endT - S.anchorT;
      raw.window_real_ms = performance.now() - wall0;
      raw.raf_count = ticks;
      raw.period_end_ms = S.mod._jnext_web_frame_period_ms();
      raw.cpu_mhz_end = S.mod._jnext_web_cpu_mhz();
      raw.mem_end = memSize(S.mod);
      raw.costs = S.costs; raw.deltas = S.deltas; raw.audio = S.audio; raw.audio_calls = S.audioCalls;
      raw.capped = S.capped;
      raw.audio_state = S.ctxs[0] ? S.ctxs[0].state : 'none';
      const g = glInfo(document.querySelector('#box canvas'));
      raw.gl = g.gl; raw.renderer = g.renderer;
      say(`${cell.config} ${cell.program}: ${S.frames} frames in ${(raw.wall_ms / 1000).toFixed(2)} s`);
    } catch (e) {
      raw.error = String(e && e.message || e);
      raw.gl = false; raw.costs = raw.costs || []; raw.deltas = raw.deltas || [];
      say('error: ' + raw.error);
    }
    S.rec = false;
    return raw;
  }

  window.benchGo = (cell) => runCell(cell);
  window.__benchSink = () => sink;

  if (q.get('mode') === 'auto') {
    (async () => {
      const r = await fetch('/next');
      const cell = await r.json();
      if (cell.done) { say('done'); return; }
      cell.module = q.get('module');
      const raw = await runCell(cell);
      await fetch('/result', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(raw) });
      location.replace(location.href);
    })();
  } else {
    say('waiting for benchGo()');
  }
})();
