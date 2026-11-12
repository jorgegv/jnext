// The on-screen Spectrum Next keyboard of jnext.mount(): for phones and tablets, or on request
// (the touchKeyboard option, the --touch-keyboard flag, player.showKeyboard()). Plain script,
// no dependencies, ASCII. It is bundled (after jnext-image.js, before jnext_api.js, which calls
// it) after the emulator's own code in jnext-<version>.js and defines one global,
// JnextTouchKeyboard.
//
// A touch key is a synthetic DOM KeyboardEvent (only `code`) dispatched on the player's canvas:
// it enters SDL's own key handler like a key of a physical keyboard, so SDL's key and modifier
// state, the host-key filter, the input latch and the keyboard matrix all behave as for a PC
// key, and a canvas blur releases a touch-held key as it releases a physical one.
//
//   JnextTouchKeyboard.create(box, canvas) -> { started(mode), show(on, byPage), get visible, destroy() }
//
// The DOM is built on the first show; nothing is injected into the page's stylesheets (inline
// styles only), so a page that never shows the keyboard has exactly the DOM it had without it.
(function (root) {
  'use strict';

  var FULL_MIN_WIDTH = 504;      // CSS px of the player box: the full layout from here on
  var LATCH_MAX_HOLD_MS = 500;   // a lone modifier held longer than this is a hold, not a latch
  var MIN_KEY_H = 24;            // CSS px
  var MAX_KB_FRACTION = 0.45;    // of the box height

  // id -> [legend, aria-label, DOM codes in press order]. The extended keys are the Next's own
  // host keys (NR 0xB0/0xB1 see them as its keyboard reports them), never CS/SS compounds.
  var KEYS = {
    'enter': ['ENT', 'Enter', ['Enter']],
    'space': ['SPACE', 'Space', ['Space']],
    'caps-shift': ['CAPS', 'Caps Shift', ['ShiftLeft']],
    'caps-shift-r': ['CAPS', 'Caps Shift', ['ShiftRight']],
    'symbol-shift': ['SYM', 'Symbol Shift', ['ControlLeft']],
    'symbol-shift-r': ['SYM', 'Symbol Shift', ['ControlRight']],
    'true-video': ['TRU', 'True video', ['Backquote']],
    'inv-video': ['INV', 'Inverse video', ['Backslash']],
    'break': ['BRK', 'Break', ['Escape']],
    'delete': ['DEL', 'Delete', ['Backspace']],
    'extend': ['EXT', 'Extend mode', ['Tab']],
    'caps-lock': ['LOCK', 'Caps lock', ['CapsLock']],
    'edit': ['EDIT', 'Edit', ['AltLeft', 'KeyE']],
    'graph': ['GRAPH', 'Graphics mode', ['AltLeft', 'KeyG']],
    'semicolon': [';', 'Semicolon', ['Semicolon']],
    'quote': ['"', 'Double quote', ['Quote']],
    'period': ['.', 'Period', ['Period']],
    'comma': [',', 'Comma', ['Comma']],
    'left': ['←', 'Cursor left', ['ArrowLeft']],
    'right': ['→', 'Cursor right', ['ArrowRight']],
    'up': ['↑', 'Cursor up', ['ArrowUp']],
    'down': ['↓', 'Cursor down', ['ArrowDown']]
  };
  '1234567890'.split('').forEach(function (c) { KEYS[c] = [c, 'Key ' + c, ['Digit' + c]]; });
  'qwertyuiopasdfghjklzxcvbnm'.split('').forEach(function (c) {
    KEYS[c] = [c.toUpperCase(), 'Key ' + c.toUpperCase(), ['Key' + c.toUpperCase()]];
  });

  // The two logical modifiers; the left and right key of each share one latch.
  var MODS = {
    'caps-shift': 'cs', 'caps-shift-r': 'cs', 'symbol-shift': 'ss', 'symbol-shift-r': 'ss'
  };
  var MOD_CODE = { cs: 'ShiftLeft', ss: 'ControlLeft' };   // a latched press always uses the left key
  var MOD_ORDER = ['cs', 'ss'];

  function row(spec) {
    // 'id:weight' items; 'gap:weight' is empty space (under a key of the row above that spans two rows)
    return spec.split(' ').map(function (it) {
      var p = it.split(':');
      return { id: p[0], w: p.length > 1 ? parseFloat(p[1]) : 1 };
    });
  }
  function digits() { return '1 2 3 4 5 6 7 8 9 0'; }
  function span(r, id) { r.forEach(function (k) { if (k.id === id) k.span = 2; }); return r; }

  // Compact: the 48K's 40 keys. Every extended key is CS/SS plus a key, as on a 48K.
  var COMPACT = [
    row(digits()),
    row('q w e r t y u i o p'),
    row('a s d f g h j k l enter'),
    row('caps-shift z x c v b n m symbol-shift space')
  ];
  // Full: the Next's 58 keys (40 + 16 extended + the second CS and SS), Spectrum+ arrangement.
  var FULL = [
    row('true-video inv-video ' + digits() + ' break:2'),
    span(row('delete:1.5 graph q w e r t y u i o p enter:1.5'), 'enter'),
    row('extend:2.5 edit a s d f g h j k l gap:1.5'),
    row('caps-shift:2.5 caps-lock z x c v b n m period caps-shift-r:2.5'),
    row('symbol-shift:2 semicolon quote left right space:3 up down comma symbol-shift-r:2')
  ];
  var LAYOUTS = { compact: COMPACT, full: FULL };

  function create(box, canvas) {
    var el = null;              // the keyboard element, built on the first show
    var visible = false;
    var pageWish = null;        // showKeyboard()'s last boolean: it wins over the mode
    var layoutName = null;
    var kbHeight = 0;           // CSS px the keyboard takes now (0: hidden)
    var observer = null;
    var keyEls = {};            // id -> element
    var entries = {};           // pointer id -> the press it holds
    var count = {};             // DOM code -> how many presses hold it down in the guest
    var latched = { cs: false, ss: false };
    var destroyed = false;

    function send(type, code) {
      canvas.dispatchEvent(new KeyboardEvent(type, { code: code, bubbles: true, cancelable: true }));
    }
    function down(code) { if (!count[code]) { count[code] = 1; send('keydown', code); } else count[code]++; }
    function up(code) {
      if (!count[code]) return;
      if (--count[code] === 0) send('keyup', code);
    }

    function paintMods() {
      Object.keys(MODS).forEach(function (id) {
        var k = keyEls[id];
        if (!k) return;
        var on = latched[MODS[id]];
        k.setAttribute('aria-pressed', on ? 'true' : 'false');
        k.style.background = on ? '#e8a000' : (k.getAttribute('data-down') === '1' ? '#5a5a5a' : '#3a3a3a');
      });
    }
    function paintKey(id, isDown) {
      var k = keyEls[id];
      if (!k) return;
      k.setAttribute('data-down', isDown ? '1' : '0');
      if (MODS[id]) paintMods();
      else k.style.background = isDown ? '#5a5a5a' : '#3a3a3a';
    }

    // ---- presses ----
    function press(pid, id) {
      var def = KEYS[id];
      var e = { id: id, t: Date.now(), mod: MODS[id] || null, mods: [], codes: def[2], others: false,
                wasLatched: false };
      var m;
      if (e.mod) {
        e.wasLatched = latched[e.mod];
        if (e.wasLatched) { latched[e.mod] = false; paintMods(); }   // a tap on a latched key cancels it
      } else {
        // a non-modifier key ends the lone-tap of every modifier held now
        Object.keys(entries).forEach(function (p) { entries[p].others = true; });
        for (var i = 0; i < MOD_ORDER.length; i++) {
          m = MOD_ORDER[i];
          if (latched[m]) { e.mods.push(m); down(MOD_CODE[m]); }
        }
      }
      e.codes.forEach(down);
      entries[pid] = e;
      paintKey(id, true);
      return e;
    }
    function release(pid) {
      var e = entries[pid];
      if (!e) return;
      delete entries[pid];
      for (var i = e.codes.length - 1; i >= 0; i--) up(e.codes[i]);
      e.mods.forEach(function (m) { up(MOD_CODE[m]); latched[m] = false; });
      if (e.mod && !e.others && !e.wasLatched && Date.now() - e.t < LATCH_MAX_HOLD_MS) latched[e.mod] = true;
      paintKey(e.id, false);
      if (e.mods.length) paintMods();
    }
    // Hiding or destroying: every key still held goes up, and the latches clear.
    function releaseAll() {
      Object.keys(entries).forEach(function (p) {
        var e = entries[p];
        delete entries[p];
        for (var i = e.codes.length - 1; i >= 0; i--) up(e.codes[i]);
        e.mods.forEach(function (m) { up(MOD_CODE[m]); });
        paintKey(e.id, false);
      });
      latched.cs = latched.ss = false;
      paintMods();
    }

    function wire(k, id) {
      k.addEventListener('pointerdown', function (ev) {
        ev.preventDefault();
        if (ev.pointerType === 'mouse' && ev.button !== 0) return;
        if (entries[ev.pointerId]) return;
        try { k.setPointerCapture(ev.pointerId); } catch (x) { /* a synthetic event has no active pointer */ }
        press(ev.pointerId, id);
      });
      function end(ev) { release(ev.pointerId); }
      k.addEventListener('pointerup', end);
      k.addEventListener('pointercancel', end);
      k.addEventListener('lostpointercapture', end);
      ['mousedown', 'contextmenu'].forEach(function (t) {
        k.addEventListener(t, function (ev) { ev.preventDefault(); });
      });
      k.addEventListener('touchstart', function (ev) { ev.preventDefault(); }, { passive: false });
      // A click that no pointer made (detail 0: a screen reader, element.click()) is one tap.
      k.addEventListener('click', function (ev) {
        ev.preventDefault();
        if (ev.detail !== 0) return;
        press('click', id);
        release('click');
      });
    }

    // ---- DOM, layout, sizing ----
    function build(name) {
      layoutName = name;
      el.textContent = '';
      keyEls = {};
      LAYOUTS[name].forEach(function (r) {
        r.forEach(function (it) {
          if (it.id === 'gap') return;
          var def = KEYS[it.id];
          var k = document.createElement('div');
          k.setAttribute('data-key', it.id);
          k.setAttribute('role', 'button');
          k.setAttribute('aria-label', def[1]);
          if (MODS[it.id]) k.setAttribute('aria-pressed', 'false');
          k.style.cssText = 'all:initial;position:absolute;box-sizing:border-box;display:flex;' +
            'align-items:center;justify-content:center;overflow:hidden;cursor:pointer;' +
            'background:#3a3a3a;color:' + (MODS[it.id] === 'ss' ? '#ff6b6b' : '#f2f2f2') + ';' +
            'border-radius:4px;font-family:sans-serif;font-weight:600;touch-action:none;' +
            'user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;' +
            '-webkit-tap-highlight-color:transparent;text-align:center;';
          k.textContent = def[0];
          wire(k, it.id);
          el.appendChild(k);
          keyEls[it.id] = k;
        });
      });
      paintMods();
    }

    function place() {
      var rows = LAYOUTS[layoutName];
      var W = box.clientWidth, H = box.clientHeight;
      var sum = 0;
      rows[0].forEach(function (it) { sum += it.w; });
      var u = W / sum;
      var h = Math.max(MIN_KEY_H, Math.min(0.9 * u, MAX_KB_FRACTION * H / rows.length));
      var gap = 1;
      rows.forEach(function (r, ri) {
        var tot = 0;
        r.forEach(function (it) { tot += it.w; });
        var x = 0, ru = W / tot;
        r.forEach(function (it) {
          var w = it.w * ru;
          if (it.id !== 'gap') {
            var k = keyEls[it.id], s = k.style;
            s.left = (x + gap) + 'px';
            s.top = (ri * h + gap) + 'px';
            s.width = (w - 2 * gap) + 'px';
            s.height = ((it.span || 1) * h - 2 * gap) + 'px';
            s.fontSize = Math.round(0.36 * h) + 'px';
          }
          x += w;
        });
      });
      return Math.round(rows.length * h);
    }

    function layout() {
      if (!el || !visible) return;
      var name = box.clientWidth >= FULL_MIN_WIDTH ? 'full' : 'compact';
      if (name !== layoutName) { releaseAll(); build(name); }
      var h = place();
      el.style.height = h + 'px';
      if (h !== kbHeight) { kbHeight = h; resized(); }
    }

    // SDL resizes its drawing buffer to the canvas CSS size on a window resize only.
    function resized() {
      canvas.style.height = kbHeight ? 'calc(100% - ' + kbHeight + 'px)' : '100%';
      window.dispatchEvent(new Event('resize'));
    }

    function show(on, byPage) {
      if (destroyed) return;
      if (byPage) pageWish = on;
      if (on === visible) return;
      visible = on;
      if (on) {
        if (!el) {
          el = document.createElement('div');
          el.className = 'jnext-keyboard';
          el.setAttribute('role', 'group');
          el.setAttribute('aria-label', 'Spectrum Next keyboard');
          // The canvas's next sibling: before the page's overlay, which must cover it until the start.
          box.insertBefore(el, canvas.nextSibling);
        }
        el.style.cssText = 'all:initial;position:absolute;left:0;right:0;bottom:0;display:block;' +
          'background:#1a1a1a;touch-action:none;user-select:none;-webkit-user-select:none;' +
          '-webkit-touch-callout:none;';
        el.removeAttribute('aria-hidden');
        layoutName = null;
        build(box.clientWidth >= FULL_MIN_WIDTH ? 'full' : 'compact');
        kbHeight = 0;
        layout();
        if (typeof ResizeObserver !== 'undefined' && !observer) {
          observer = new ResizeObserver(function () { layout(); });
          observer.observe(box);
        }
      } else {
        releaseAll();
        el.style.display = 'none';
        el.setAttribute('aria-hidden', 'true');
        kbHeight = 0;
        resized();
      }
    }

    return {
      // The mode main() resolved (--touch-keyboard): 0 auto, 1 on, 2 off. A page's showKeyboard() wins.
      started: function (mode) {
        if (pageWish !== null) return;
        var coarse = typeof matchMedia === 'function' && matchMedia('(pointer: coarse)').matches;
        show(mode === 1 || (mode === 0 && coarse), false);
      },
      show: show,
      get visible() { return visible; },
      destroy: function () {
        if (destroyed) return;
        releaseAll();
        destroyed = true;
        visible = false;
        if (observer) { observer.disconnect(); observer = null; }
      }
    };
  }

  root.JnextTouchKeyboard = { create: create, FULL_MIN_WIDTH: FULL_MIN_WIDTH, LATCH_MAX_HOLD_MS: LATCH_MAX_HOLD_MS };
})(typeof window !== 'undefined' ? window : globalThis);
