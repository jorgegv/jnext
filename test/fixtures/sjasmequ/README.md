# sjasmplus `--sym` / `--exp` fixtures (GH #390)

`equlabels.asm` is ours, written for these rows. The other three files are what
sjasmplus v1.23.1 (z00m128/sjasmplus commit `46aae3fa784cae903345b87bd6b9db16a77c4bea`)
writes for it, byte for byte, untrimmed:

    sjasmplus --sym=equlabels.sym --exp=equlabels.exp equlabels.asm      # Linux build: LF

`equlabels-win.sym` is the `--sym` of the same source from a Windows build of the
same commit, cross-built with Fedora MinGW (`make -f Makefile.win
GCC=x86_64-w64-mingw32-gcc GPP=x86_64-w64-mingw32-g++
WINDRES=x86_64-w64-mingw32-windres`) and run under wine 11.0:

    wine sjasmplus.exe --sym=equlabels-win.sym --exp=equlabels-win.exp equlabels.asm

sjasmplus opens both files in text mode (`fopen(name, "w")`, `sjasm/support.cpp:47-50`),
so on Windows every line ends CRLF. With its 18 CRs removed, `equlabels-win.sym` is
`equlabels.sym`.

What the source exercises: an address and a size EQU, `$FFFF` and `$10000` (one each
side of 16 bits), a negative EQU (written `0xFFFFFFFF`), a DEFL exported at two values,
a struct (its size and field offsets) and an instance of it, a local label, a temporary
label (not written), a module label and its local, an EXPORT inside the module (written
under the name the EXPORT spells, `draw`), and two labels at `$C000` in different
`DEVICE ZXSPECTRUMNEXT` pages (the files carry no page).

    sha256
    d269135d1f10148e4a7c43d404809845aafe373aed08b54766b3ab3cfdd4592a  equlabels.asm
    b784e27696677ff0c4eeca433b3d13adb7fac2f773da0dd5cb9e612aad88cb61  equlabels.sym
    84667493ad10e871c073165b555aeac3e8cb4a1a6e87126873f656d8f4dbbde7  equlabels.exp
    b173a9e8706d3e51703a3c8b1dbadaded32c0a548424d62fb1e6e9629981baf4  equlabels-win.sym

Rows: `SYM-19`..`SYM-27` in `test/debug/debugger_backend_test.cpp`,
`SCRIPT-HOST-MAP-SJASM`, `-EXP`, `-CONTENT` in `test/script/script_events_test.cpp`,
`QMAP-05`, `QMAP-06` in `test/debugger/verbs_test.cpp`.
