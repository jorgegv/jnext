// Test fixture for the browser smoke test: never shipped. Query string:
//   module=<jnext-VERSION.js>   the build output to load (served by the harness)
//   args=<json array>           argv for callMain
//   files=<json [[url, path]]>  fetched into MEMFS before the start
//   canvas=WxH | <css>          CSS size of the canvas (pixels, or CSS declarations)
//   env=<json object>           extra environment (SDL hints) for the module
//   autostart=1                 callMain on load, no click
//   image=<json {url, sha256}>  fetch the SD-card image with web/jnext-image.js (Cache API,
//                               SHA-256 check) and add --sdcard/--sdcard-sha256 to the args
//   idbfs=1                     JNEXT_CONFIG_DIR=/jnext and IndexedDB on /jnext/warm-start
(function () {
  'use strict';
  const q = new URLSearchParams(location.search);
  const canvas = document.getElementById('jnext-canvas');
  const overlay = document.getElementById('overlay');
  const out = { stdout: [], stderr: [], exit: null, started: false, ready: false, error: null, progress: [] };
  window.jnextTest = out;

  // canvas=WxH (pixels) or canvas=<css declarations>, e.g. width:50vw;height:40vw
  if (q.get('canvas')) {
    const c = q.get('canvas');
    if (c.includes(':')) {
      canvas.style.cssText = c;
    } else {
      const [w, h] = c.split('x');
      canvas.style.width = w + 'px';
      canvas.style.height = h + 'px';
    }
  }

  function start(mod) {
    if (out.started) return;
    out.started = true;
    console.log('__JNEXT started');
    overlay.style.display = 'none';
    canvas.focus();
    try {
      out.mainResult = mod.callMain(JSON.parse(q.get('args') || '[]').concat(out.imageArgs || []));
    } catch (e) {
      out.error = String(e);
    }
  }

  function loadScript(src) {
    return new Promise((res, rej) => {
      const s = document.createElement('script');
      s.src = src; s.onload = res; s.onerror = () => rej(new Error('script load failed: ' + src));
      document.head.appendChild(s);
    });
  }

  async function boot() {
    if (q.get('image') || q.get('idbfs') === '1') await loadScript('/web/jnext-image.js');
    const script = document.createElement('script');
    script.src = q.get('module');
    await new Promise((res, rej) => { script.onload = res; script.onerror = () => rej(new Error('module load failed')); document.head.appendChild(script); });
    const mod = await createJnextModule({
      canvas: canvas,
      print: (s) => out.stdout.push(s),
      printErr: (s) => out.stderr.push(s),
      onExit: (c) => { out.exit = c; console.log('__JNEXT exit ' + c); },
      locateFile: (p) => (p.endsWith('.wasm') ? q.get('module').replace(/\.js$/, '.wasm') : p),
      preRun: [(m) => {
        m.ENV.SDL_EMSCRIPTEN_CANVAS_SELECTOR = '#jnext-canvas';
        Object.assign(m.ENV, JSON.parse(q.get('env') || '{}'));
        if (q.get('idbfs') === '1') m.ENV.JNEXT_CONFIG_DIR = '/jnext';
        m.jnextProgress = (phase, fraction) => out.progress.push([phase, fraction, performance.now()]);
      }],
    });
    window.jnextModule = mod;
    for (const [url, path] of JSON.parse(q.get('files') || '[]')) {
      overlay.textContent = 'Loading ' + url;
      const r = await fetch(url);
      if (!r.ok) throw new Error('fetch ' + url + ': ' + r.status);
      const dir = path.substring(0, path.lastIndexOf('/'));
      if (dir) mod.FS.mkdirTree(dir);
      mod.FS.writeFile(path, new Uint8Array(await r.arrayBuffer()));
    }
    if (q.get('idbfs') === '1') await JnextImage.mountWarmStartStore(mod);
    if (q.get('image')) {
      overlay.textContent = 'Loading the image';
      const img = await JnextImage.fetchImage(JSON.parse(q.get('image')), (p, f) => out.progress.push([p, f, performance.now()]));
      out.imageArgs = JnextImage.installImage(mod, img);
    }
    out.ready = true;
    console.log('__JNEXT ready');
    overlay.textContent = 'Click to start';
    if (q.get('autostart') === '1') start(mod);
    else overlay.addEventListener('click', () => start(mod));
  }
  boot().catch((e) => { out.error = String(e); });
})();
