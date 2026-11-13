# 10.5 Two ways to embed JNEXT, your own page or the ready-made widget

There are two ways to put a player on a page. Pick `jnext.mount()` (10.1, 10.2) when
you want full control of the layout, the controls and the messages. Pick the widget
when you want a good-looking player with a title bar, a progress bar and a toolbar
from one `div` and one script tag. Both are in the release's `jnext-VERSION-web.zip`, and
both need the same things: the SD-card image's URL and SHA-256 (10.4) and a page
served over `https://` or `http://localhost` (10.1).

## Your own page: `jnext.mount()`

The shortest working page. `jnext-VERSION.js` and `jnext-VERSION.wasm` sit beside it:

```html
<!doctype html>
<div id="player"></div>
<script src="jnext-VERSION.js"></script>
<script>
  jnext.mount(document.getElementById('player'), {
    image: { url: 'https://example.com/card.img', sha256: '...64 hex digits...' },
    file: 'game.nex',
    width: 640, height: 512
  });
</script>
```

The visitor clicks the player to start it. Everything else (events, `pause()`,
`reset()`, `setVolume()`, loading another program) is yours to add: see 10.2.

## The ready-made widget

The same player, framed, from one `div` and one script tag. `jnext-widget.js` and `jnext-VERSION.js`
(with its `.wasm`) sit beside the page; the widget loads `jnext-VERSION.js` itself
unless the page has loaded it already:

```html
<!doctype html>
<div class="jnext-widget"
     data-image="https://example.com/card.img"
     data-sha256="...64 hex digits..."
     data-file="game.nex"></div>
<script src="jnext-widget.js"></script>
```

Every element with the class `jnext-widget` gets a player, so a page can hold several.
The widget draws a rounded frame with a title bar, a progress bar while the image
downloads and the machine starts, the usual "click to start" overlay, a toolbar
(Pause or Resume, Reset, Mute or Unmute, Fullscreen) and a status line that also
shows errors: a refused option, a bad digest or an image that cannot be fetched, a
missing `data-image`, or a `jnext-VERSION.js` that cannot be loaded. The buttons stay
disabled then. Fullscreen enlarges the screen to fill the display, between the title bar
and the toolbar. The labels and the status line follow the player, so they stay right when
the page itself calls `pause()` or `setVolume()`. The player behind it is the element's `jnextPlayer` property, with the
members of 10.2.

| attribute | meaning | default |
|---|---|---|
| `data-image` | the SD-card image URL (`image.url`); required | |
| `data-sha256` | its SHA-256 (`image.sha256`), 64 hex digits; required | |
| `data-file` | the program to run (`file`) | none: NextZXOS menu |
| `data-width`, `data-height` | the screen size in CSS pixels | 640 and 512 |
| `data-keep-writes` | keep the program's saves across reloads (`keepWrites`); any value but `false`. `el.jnextPlayer.resetCard()` clears them | off |
| `data-touch-keyboard` | the on-screen keyboard (`touchKeyboard`): `auto`, `on` or `off` | `auto`: shown on a phone or tablet |
| `data-title` | the title bar text | `JNEXT` |
| `data-theme` | `light` or `dark` | the visitor's browser setting |

The widget has a light and a dark theme. Without `data-theme` it follows the visitor's
`prefers-color-scheme`; `data-theme="light"` or `"dark"` fixes it. All of its styles
start with `.jnext-widget` and are added to the page once, and each part resets its own
properties, so the page's own rules for `div`, `button` or `canvas` do not change the
widget and the widget changes nothing else on the page. The limit: a page rule that is
more specific than a class (one with an `id` selector, such as `#content div`) or marked
`!important` still reaches the widget's parts, and the widget does not try to beat it. `widget.html` in the zip is the snippet
with the image, digest and program also readable from the query string, like `plain.html`.
