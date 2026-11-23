// Test fixture for the JavaScript API rows: never shipped. Query string:
//   module=<jnext-VERSION.js>   the build output to load
//   image=<json {url, sha256}>  the SD card for every player: a development card, through the
//                               localhost-only seam of web/jnext-image.js (a page cannot name a card)
//   dev=<json>                  the whole development seam (card, distribution, probeTimeoutMs)
//   players=<json [options]>    one options object per player (callbacks are added here)
// Every player's events land in window.apiTest.players[i]; a mount failure in .mountError.
(function () {
  'use strict';
  const q = new URLSearchParams(location.search);
  const t = { players: [], errors: [], loaded: false };
  window.apiTest = t;
  if (q.get('dev')) window['jnextDev'] = JSON.parse(q.get('dev'));
  else if (q.get('image')) window['jnextDev'] = { card: JSON.parse(q.get('image')) };
  window.addEventListener('error', (e) => t.errors.push(String(e.message)));
  window.addEventListener('unhandledrejection', (e) => t.errors.push('unhandled: ' + String(e.reason)));

  const s = document.createElement('script');
  s.src = q.get('module');
  s.onload = () => {
    t.loaded = true;
    const list = JSON.parse(q.get('players') || '[{}]');
    list.forEach((opts, i) => {
      const rec = { player: null, progress: [], ready: 0, errors: [], notices: [], mountError: null, order: [], settled: false };
      t.players[i] = rec;
      const o = Object.assign({}, opts);
      if (o.width === undefined) o.width = 320;
      if (o.height === undefined) o.height = 256;
      if (!opts.__raw) {
        o.onProgress = (p, f) => { rec.progress.push([p, f]); rec.order.push('progress'); };
        o.onReady = () => {
          rec.ready++; rec.order.push('ready');
          const m = rec.player && rec.player._module;
          rec.applied = m ? m._jnext_web_load_applied() : -1;
        };
        o.onError = (m) => rec.errors.push(m);
        o.onNotice = (code, m) => rec.notices.push([code, m]);
      }
      if (opts.__fn) o[opts.__fn] = () => {};   // an unknown function-valued option
      delete o.__raw; delete o.__fn;
      jnext.mount(document.getElementById('p' + i), o).then((p) => { rec.player = p; rec.settled = true; window['player' + i] = p; },
                                                            (e) => { rec.mountError = String(e.message); rec.settled = true; });
    });
  };
  s.onerror = () => t.errors.push('module script failed to load');
  document.head.appendChild(s);
})();
