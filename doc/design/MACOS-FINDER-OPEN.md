# Opening Files From the Finder (macOS)

> Status: **implemented** (Qt frontend; the document types are macOS-only).
> Tests: `test/gui/desktop_open_test.cpp`, rows DOP-01..09.

## 1. Use case

A Mac user who has associated `.nex` files with JNext (Get Info > Open with)
double-clicks one in the Finder. Before this change JNext started, booted
NextZXOS and ignored the file: the user then had to find the same file again
through File > Load NEX File…. The same happened with Open With, with a drop
on the Dock icon, and with a double-click while JNext was already running.

On Linux and Windows a desktop association runs `jnext <file>`, which the
command line already loads (a bare filename is `--load`). macOS does not do
that. LaunchServices starts the app with no arguments and delivers the file as
an Apple Event (`kAEOpenDocuments`), which Qt turns into a `QFileOpenEvent`
sent to the `QApplication`. Nothing in JNext listened for it.

There was a second gap: the bundle's `Info.plist` declared no document types,
so the Finder did not offer JNext for any Spectrum file.

## 2. Behaviour

| situation | what happens |
|-----------|--------------|
| the open starts JNext (double-click, Open With, Dock drop on a closed app) | the file loads straight away, exactly as File > Load NEX File… would (cold boot, NEX V1.3 warning, archive picker, Recent Files) |
| JNext is already running | the window comes forward and asks *Open "name"? Whatever is running now will be lost.* with **Open** / **Cancel**. Cancel is the default and the Esc button, and leaves the machine untouched |
| an unattended run (`--delayed-automatic-exit`, a script) | nothing is asked and nothing is loaded; the open is logged |
| another open arrives while the question is up (the dialog's event loop delivers it) | it is dropped and logged; the question already on screen is not stacked with a second one |
| several files opened at once at launch | each loads in turn; the last one is what runs |

Registered document types (`packaging/macos/Info.plist.in`):

| type | extensions | role | rank |
|------|-----------|------|------|
| jnext snapshot (exported UTI `io.github.zxjogv.jnext.jns`) | `.jns` | Editor | **Owner**: JNext's own format, so JNext is its default app |
| Spectrum program (by extension, `CFBundleTypeExtensions`) | `.nex .sna .szx .z80 .tap .tzx .rzx` | Viewer | Alternate |
| ZIP archive (`public.zip-archive`) | `.zip` | Viewer | Alternate |
| WAV audio (`com.microsoft.waveform-audio`) | `.wav` | Viewer | Alternate |

Alternate rank lists JNext under Open With and makes it choosable as the
default, but never takes a type over from the user's own choice (another
emulator, Archive Utility, a media player). `.pok` is not registered: a
trainer applies to a program that is already loaded, not to a fresh machine.

The Spectrum formats are matched **by extension**, not by declaring type
identifiers for them. A first version declared imported UTIs
(`io.github.zxjogv.jnext.nex` and so on). That made every such file resolve to
JNext's identifier, and it broke an association the user had already made: a
user preference for `.nex`, which LaunchServices stores against the extension,
stopped applying, and another emulator that claims `.nex` by extension became
the default instead. Matching by extension holds whatever identifier a file
resolves to, and it is what other emulators do. Measured with
`NSWorkspace.urlsForApplications(toOpen:)` on a Mac with one such emulator
installed: JNext is offered for every registered extension, and the user's
existing choice keeps working. `.jns` keeps an exported identifier, because no
other application reads it.

## 3. Design

* **`src/gui/desktop_open.h`** (header-only, Qt-only):
  * `FileOpenFilter`, an event filter installed on the `QApplication`. It
    hands each `QFileOpenEvent::file()` to a handler and consumes the event.
    It is installed on every platform; only macOS ever sends the event, and
    that keeps the filter reachable by the Linux suites.
  * `at_launch(ms_since_loop_start)`: true before the event loop starts and
    for `kLaunchWindowMs` (3 s) after. LaunchServices delivers the launch open
    as the loop starts (Qt's `finishLaunching`), well inside the window, and
    in that time no user state can exist that a reload would lose. Anything
    later goes to an app the user may have been using.
* **`MainWindow::open_from_desktop(path, at_launch)`** raises the window,
  asks unless `at_launch` (through an injectable
  `ConfirmDesktopOpenCallback`, the pattern of `set_confirm_restart_callback()`;
  the real `QMessageBox` when unset), then calls `handle_load_path()`, the
  route File > Load and Recent Files already share. Every format rule, refusal
  and dialog therefore applies unchanged. `desktop_open_asking_` is set while
  the question is up, so an open delivered by the question's own event loop is
  dropped rather than stacking a second question.
* **`QtApp`** installs the filter before `exec()` and stamps
  `loop_started_` immediately before entering it.
* **CMake** points `MACOSX_BUNDLE_INFO_PLIST` at the template, which is
  CMake's own `MacOSXBundleInfo.plist.in` plus `CFBundleDocumentTypes` and
  `UTExportedTypeDeclarations` (for `.jns`). It only takes
  effect with `-DMACOS_APP_BUNDLE=ON` (the `.dmg` build).

## 4. Tests

`desktop_open_test` (offscreen QPA, a real `MainWindow`; the load is observed
at its load callback, the seam the frontend's cold boot hangs off):

| row | asserts |
|-----|---------|
| DOP-01 | `at_launch()` at -1, 0 and window-1 ms is the launch; at the window it is not |
| DOP-02 | a `QFileOpenEvent` sent to the application reaches the handler with its file, consumed |
| DOP-03 | any other event passes the filter untouched: it reaches its target |
| DOP-04 | at launch: loads with no question |
| DOP-05 | running, answer Cancel: asked about this file, nothing loaded |
| DOP-06 | running, answer Open: the file loads |
| DOP-07 | running unattended: asks nobody, loads nothing |
| DOP-08 | the real dialog: Cancel is default and Esc button; Esc loads nothing |
| DOP-09 | the real dialog: Open loads the file |
| DOP-10 | an open arriving while the question is up is dropped, not stacked; the next one asks |

Discrimination: without the feature the suite does not compile (neither the
filter nor `open_from_desktop()` exists). Each mutant below was built and run:

| mutant | rows that fail |
|--------|----------------|
| `open_from_desktop()` never asks | DOP-05..09 |
| the filter consumes every event | DOP-03 |
| no `desktop_open_asking_` guard | DOP-10 |

The `Info.plist` entries and the Apple Event delivery are outside any suite;
they were checked by hand on macOS with a `-DMACOS_APP_BUNDLE=ON` build:
`open -a jnext.app file.nex` with JNext closed (loads with no question) and
with it running (asks; Cancel keeps the program, Open reloads), and the Open
With lists above. The `QtApp` wiring (the filter's lambda, the `loop_started_`
stamp) is reachable only through that path.
