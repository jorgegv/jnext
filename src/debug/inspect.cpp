// ---------------------------------------------------------------------------
// jnext::dbg — the two free functions `inspect.h` declares.
//
// `key_name_to_matrix()` (IN-01/IN-02) and `rrrgggbb_to_argb()` (INS-15) of
// doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md §4, for work package B1 (§10.1).
//
// Nothing here touches an `Emulator`: this file is the value-type half of the
// backend, and it is what lets `--delayed-keypress`, both GUI frontends and the
// DSL share ONE key vocabulary without any of them depending on the others.
// ---------------------------------------------------------------------------

#include "debug/inspect.h"

#include <cctype>

#include "video/renderer.h"   // Renderer::rrrgggbb_to_argb — the one expansion

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// INS-15 — the published RRRGGGBB -> ARGB8888 expansion.
//
// A FORWARDER, deliberately: `Renderer::rrrgggbb_to_argb` is the expansion the
// picture is actually built with, and a second implementation here would be
// free to drift from it one bit at a time. `video/renderer.h` cannot be
// included by `inspect.h` (it pulls `video/ula.h` and `video/lores.h` in, and
// the published headers' include budget does not stretch that far — see that
// header's banner), which is why the declaration is there and the body is here.
// B5 pins the agreement over all 256 inputs.
// ---------------------------------------------------------------------------

uint32_t rrrgggbb_to_argb(uint8_t rrrgggbb) {
    return Renderer::rrrgggbb_to_argb(rrrgggbb);
}

// ---------------------------------------------------------------------------
// IN-01 / IN-02 — the man page's key vocabulary.
//
// MOVED VERBATIM out of `src/platform/headless_app.cpp`, where both halves were
// file-static and therefore reachable only by `--delayed-keypress`. §4.5 makes
// this the ONE table four callers share (the DSL, both GUI frontends and the
// CLI), so it moves down to the backend rather than being copied up.
//
// The only change from the original is the signature: it fills a `MatrixKey`
// instead of four `int&` out-parameters, and it DEFAULT-CONSTRUCTS that key on
// the false path. The original left `row1`/`col1` as it found them when
// char_to_matrix() failed, which is a footgun in a function this many hands
// call: one caller that forgets to test the bool would inject whatever the
// previous call left behind.
// ---------------------------------------------------------------------------


// Map a character to ZX Spectrum keyboard matrix position (row, col).
// Returns false if the key is not recognised.
static bool char_to_matrix(char key, int& row, int& col) {
    // Row 0: SHIFT Z X C V
    // Row 1: A S D F G
    // Row 2: Q W E R T
    // Row 3: 1 2 3 4 5
    // Row 4: 0 9 8 7 6
    // Row 5: P O I U Y
    // Row 6: ENTER L K J H
    // Row 7: SPACE SYM M N B
    switch (key) {
        // digits
        case '1': row=3; col=0; return true;
        case '2': row=3; col=1; return true;
        case '3': row=3; col=2; return true;
        case '4': row=3; col=3; return true;
        case '5': row=3; col=4; return true;
        case '6': row=4; col=4; return true;
        case '7': row=4; col=3; return true;
        case '8': row=4; col=2; return true;
        case '9': row=4; col=1; return true;
        case '0': row=4; col=0; return true;
        // row 1 letters
        case 'a': row=1; col=0; return true;
        case 's': row=1; col=1; return true;
        case 'd': row=1; col=2; return true;
        case 'f': row=1; col=3; return true;
        case 'g': row=1; col=4; return true;
        // row 2 letters
        case 'q': row=2; col=0; return true;
        case 'w': row=2; col=1; return true;
        case 'e': row=2; col=2; return true;
        case 'r': row=2; col=3; return true;
        case 't': row=2; col=4; return true;
        // row 5 letters
        case 'p': row=5; col=0; return true;
        case 'o': row=5; col=1; return true;
        case 'i': row=5; col=2; return true;
        case 'u': row=5; col=3; return true;
        case 'y': row=5; col=4; return true;
        // row 6 letters
        case 'l': row=6; col=1; return true;
        case 'k': row=6; col=2; return true;
        case 'j': row=6; col=3; return true;
        case 'h': row=6; col=4; return true;
        // row 0 letters
        case 'z': row=0; col=1; return true;
        case 'x': row=0; col=2; return true;
        case 'c': row=0; col=3; return true;
        case 'v': row=0; col=4; return true;
        // row 7 letters
        case 'm': row=7; col=2; return true;
        case 'n': row=7; col=3; return true;
        case 'b': row=7; col=4; return true;
        // specials
        case ' ': row=7; col=0; return true;  // SPACE
        case '\n': row=6; col=0; return true;  // ENTER
        default: return false;
    }
}

// The parse itself, still in the four-out-parameter form it was moved in, so
// the tables above and below are byte-identical to the shipped ones. The
// published `MatrixKey` signature is the wrapper under it.
// Accepts (case-insensitive): a single alnum char, punctuation with a
// well-known SYMBOL SHIFT compound ('.' ',' ';' ':'), the named keys
// enter/return/space/up/down/left/right (cursors = CAPS SHIFT + 7/6/5/8,
// mirroring the s_compound PC-arrow table in src/input/keyboard.cpp), or
// an explicit compound "sym+<char>" / "caps+<char>".
// Matrix constants: CAPS SHIFT = (0,0), SYMBOL SHIFT = (7,1).
static bool key_name_to_matrix_ints(const std::string& name,
                                    int& row1, int& col1, int& row2, int& col2) {
    row2 = col2 = -1;
    std::string k;
    k.reserve(name.size());
    for (char c : name)
        k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (k.empty()) return false;

    // Named keys.
    if (k == "enter" || k == "return") return char_to_matrix('\n', row1, col1);
    if (k == "space")                  return char_to_matrix(' ', row1, col1);
    if (k == "left")  { row2=0; col2=0; row1=3; col1=4; return true; }  // CAPS + 5
    if (k == "down")  { row2=0; col2=0; row1=4; col1=4; return true; }  // CAPS + 6
    if (k == "up")    { row2=0; col2=0; row1=4; col1=3; return true; }  // CAPS + 7
    if (k == "right") { row2=0; col2=0; row1=4; col1=2; return true; }  // CAPS + 8

    // Explicit compounds: "sym+x" / "caps+x".
    auto plus = k.find('+');
    if (plus != std::string::npos && plus + 2 == k.size()) {
        const std::string mod = k.substr(0, plus);
        const char c = k[plus + 1];
        if (!char_to_matrix(c, row1, col1)) return false;
        if (mod == "sym"  || mod == "ss") { row2=7; col2=1; return true; }
        if (mod == "caps" || mod == "cs") { row2=0; col2=0; return true; }
        return false;
    }

    // Single characters. Punctuation maps to its SYMBOL SHIFT compound
    // (keyword table in the 48K ROM / NextZXOS editor):
    //   '.' = SYM+M   ',' = SYM+N   ';' = SYM+O   ':' = SYM+Z
    if (k.size() == 1) {
        switch (k[0]) {
            case '.': row1=7; col1=2; row2=7; col2=1; return true;  // SYM + M
            case ',': row1=7; col1=3; row2=7; col2=1; return true;  // SYM + N
            case ';': row1=5; col1=1; row2=7; col2=1; return true;  // SYM + O
            case ':': row1=0; col1=1; row2=7; col2=1; return true;  // SYM + Z
            default:  return char_to_matrix(k[0], row1, col1);
        }
    }
    return false;
}

bool key_name_to_matrix(const std::string& name, MatrixKey& out) {
    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    if (!key_name_to_matrix_ints(name, r1, c1, r2, c2)) {
        out = MatrixKey{};       // never "unspecified on false" — see the banner
        return false;
    }
    out.row1 = r1;
    out.col1 = c1;
    out.row2 = r2;
    out.col2 = c2;
    return true;
}

}  // namespace dbg
}  // namespace jnext
