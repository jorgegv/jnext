// ===========================================================================
// Issue #123 — shifted symbols must reach the guest.
//
// THE DEFECT. MainWindow::handle_key() translates through qt_key_to_sdl(),
// which switches on QKeyEvent::key() — the LOGICAL key. On a US layout
// Shift+';' arrives as Qt::Key_Colon and Shift+'2' as Qt::Key_At, neither of
// which the table held, so the scancode came back SDL_SCANCODE_UNKNOWN and the
// keystroke was DROPPED before Keyboard::set_key() ever ran. Nineteen keys were
// affected: the ten digits (whose Caps-Shift compounds are EDIT, CAPS LOCK,
// TRUE/INV VIDEO, the four cursor keys, GRAPH and DELETE) and the nine
// punctuation keys the table already carried unshifted.
//
// WHY THE FIX IS THE HARDWARE'S OWN MODEL. The Next's PS/2 keymap is indexed by
// PHYSICAL scancode; LShift/RShift merely raise capshift_count (keymaps.vhd:83,
// ps2_keyb.vhd:196-198) and do not select a different keymap entry. SDL
// scancodes are physical positions too, so the shifted logical key must resolve
// to the SAME scancode as its unshifted sibling, with the host Shift key
// asserting Caps Shift independently. Group A pins exactly that.
//
// WHERE THE PAIRINGS COME FROM. They were MEASURED, not recalled: a Qt 6.11
// widget under a real X server (Xvfb, us layout) fed by xdotool reported every
// pair below, each member carrying the same QKeyEvent::nativeScanCode() — e.g.
// nativeScanCode 47 for both Qt::Key_Semicolon and Qt::Key_Colon.
//
// WHAT THIS SUITE DOES NOT COVER. Layouts whose glyphs fall outside the US/UK
// ASCII set (Spanish 'ñ' on the ';' key; the ISO key left of Z) still produce
// logical keys with no table entry and are still dropped. That needs
// nativeScanCode() plus a per-platform physical-key table and is recorded as an
// open item, not silently implied by a green run here.
//
// MainWindow owns QWidgets, so a QApplication is required — but not a display:
// the offscreen QPA platform is forced in main(), the same idiom as
// esc_break_test and host_hotkey_test.
// ===========================================================================
#include <QApplication>
#include <QKeyEvent>

#include <cstdio>
#include <string>
#include <vector>

#include "gui/main_window.h"
#include "input/keyboard.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

// One recorded delivery to the ZX keyboard.
struct KeyEv { SDL_Scancode sc; bool pressed; };

std::string fmt(const std::vector<KeyEv>& v) {
    std::string s = "seen=[";
    for (const auto& e : v) {
        s += std::to_string(static_cast<int>(e.sc));
        s += e.pressed ? "d " : "u ";
    }
    return s + "]";
}

// ---------------------------------------------------------------------------
// The nineteen keys whose shifted logical key differs from the unshifted one
// AND whose unshifted form qt_key_to_sdl() already carried. `glyph` is what the
// key types on a US layout with Shift held; `expect` is the physical scancode
// its unshifted sibling maps to.
// ---------------------------------------------------------------------------
struct ShiftedKey {
    Qt::Key      shifted;     // Qt logical key with Shift held
    SDL_Scancode expect;      // scancode the UNSHIFTED sibling maps to
    const char*  glyph;       // US-layout shifted glyph
    const char*  unshifted;   // US-layout unshifted glyph
    const char*  sc_name;     // scancode name, for the row description
};

const ShiftedKey SHIFTED[] = {
    { Qt::Key_Exclam,      SDL_SCANCODE_1,          "!", "1",  "SDL_SCANCODE_1"          },
    { Qt::Key_At,          SDL_SCANCODE_2,          "@", "2",  "SDL_SCANCODE_2"          },
    { Qt::Key_NumberSign,  SDL_SCANCODE_3,          "#", "3",  "SDL_SCANCODE_3"          },
    { Qt::Key_Dollar,      SDL_SCANCODE_4,          "$", "4",  "SDL_SCANCODE_4"          },
    { Qt::Key_Percent,     SDL_SCANCODE_5,          "%", "5",  "SDL_SCANCODE_5"          },
    { Qt::Key_AsciiCircum, SDL_SCANCODE_6,          "^", "6",  "SDL_SCANCODE_6"          },
    { Qt::Key_Ampersand,   SDL_SCANCODE_7,          "&", "7",  "SDL_SCANCODE_7"          },
    { Qt::Key_Asterisk,    SDL_SCANCODE_8,          "*", "8",  "SDL_SCANCODE_8"          },
    { Qt::Key_ParenLeft,   SDL_SCANCODE_9,          "(", "9",  "SDL_SCANCODE_9"          },
    { Qt::Key_ParenRight,  SDL_SCANCODE_0,          ")", "0",  "SDL_SCANCODE_0"          },
    { Qt::Key_Underscore,  SDL_SCANCODE_MINUS,      "_", "-",  "SDL_SCANCODE_MINUS"      },
    { Qt::Key_Plus,        SDL_SCANCODE_EQUALS,     "+", "=",  "SDL_SCANCODE_EQUALS"     },
    { Qt::Key_Bar,         SDL_SCANCODE_BACKSLASH,  "|", "\\", "SDL_SCANCODE_BACKSLASH"  },
    { Qt::Key_Colon,       SDL_SCANCODE_SEMICOLON,  ":", ";",  "SDL_SCANCODE_SEMICOLON"  },
    { Qt::Key_QuoteDbl,    SDL_SCANCODE_APOSTROPHE, "\"", "'", "SDL_SCANCODE_APOSTROPHE" },
    { Qt::Key_AsciiTilde,  SDL_SCANCODE_GRAVE,      "~", "`",  "SDL_SCANCODE_GRAVE"      },
    { Qt::Key_Less,        SDL_SCANCODE_COMMA,      "<", ",",  "SDL_SCANCODE_COMMA"      },
    { Qt::Key_Greater,     SDL_SCANCODE_PERIOD,     ">", ".",  "SDL_SCANCODE_PERIOD"     },
    { Qt::Key_Question,    SDL_SCANCODE_SLASH,      "?", "/",  "SDL_SCANCODE_SLASH"      },
};
constexpr int N_SHIFTED = int(sizeof(SHIFTED) / sizeof(SHIFTED[0]));

// ---------------------------------------------------------------------------
// Group A — the shifted logical key is translated to the physical scancode of
// its unshifted sibling, on BOTH edges.
// ---------------------------------------------------------------------------
//
// Only the glyph key is sent, carrying Qt::ShiftModifier exactly as a real
// Shift+<key> press does. The host Shift key itself is deliberately NOT sent
// here: it has its own well-tested s_map entry, and leaving it out keeps each
// row's assertion about the ONE translation under test — the glyph key's.
// Its Caps Shift contribution is proved end-to-end in group B.

void test_translation(MainWindow& w) {
    for (int i = 0; i < N_SHIFTED; ++i) {
        const ShiftedKey& k = SHIFTED[i];

        std::vector<KeyEv> seen;
        w.set_key_callback([&seen](SDL_Scancode sc, bool pressed) {
            seen.push_back({sc, pressed});
        });

        QKeyEvent press(QEvent::KeyPress, k.shifted, Qt::ShiftModifier);
        QKeyEvent release(QEvent::KeyRelease, k.shifted, Qt::ShiftModifier);
        QApplication::sendEvent(&w, &press);
        QApplication::sendEvent(&w, &release);
        w.set_key_callback(nullptr);

        const bool ok = seen.size() == 2
                     && seen[0].sc == k.expect &&  seen[0].pressed
                     && seen[1].sc == k.expect && !seen[1].pressed;

        char id[16], desc[192];
        std::snprintf(id, sizeof(id), "I123-%02d", 1 + i);
        std::snprintf(desc, sizeof(desc),
                      "Shift+%s ('%s') delivers %s on press and release, "
                      "like unshifted '%s'",
                      k.unshifted, k.glyph, k.sc_name, k.unshifted);
        check(id, desc, ok, fmt(seen));
    }
}

// ---------------------------------------------------------------------------
// Group B — the consequence in the emulated key matrix
// ---------------------------------------------------------------------------
//
// Group A proves the translation; these three rows prove the keystroke lands
// where the guest reads it, driving the FULL chord (Shift down, then the glyph
// key) into a real Keyboard and reading port 0xFE's own call, read_rows().
//
// Three rows, not nineteen: the scancodes group A produces reach the matrix by
// three DIFFERENT routes inside Keyboard::set_key(), and one witness per route
// is what makes the set discriminative rather than repetitive —
//   s_map       (keyboard.cpp:90)      digits, direct single cell
//   s_extkey    (keyboard.cpp:178)     extended keys, folded in read_rows()
//   s_compound  (keyboard.cpp:204)     two cells asserted together

// Port 0xFE semantics: bit N of addr_high == 0 selects row N; the returned
// 5 bits are ACTIVE-LOW, so a clear bit means "key down".
bool key_down(const Keyboard& kb, int row, int col) {
    const uint8_t addr_high = static_cast<uint8_t>(~(1u << row));
    return (kb.read_rows(addr_high) & (1u << col)) == 0;
}

std::string rows_detail(const Keyboard& kb) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "row0=0x%02X row3=0x%02X row5=0x%02X row7=0x%02X",
                  kb.read_rows(0xFEu),   // ~(1<<0)
                  kb.read_rows(0xF7u),   // ~(1<<3)
                  kb.read_rows(0xDFu),   // ~(1<<5)
                  kb.read_rows(0x7Fu));  // ~(1<<7)
    return buf;
}

// Shift down, then the shifted glyph key, both routed through MainWindow into
// `kb`. Left held: every row below asserts on the pressed state.
void press_shift_chord(MainWindow& w, Keyboard& kb, Qt::Key shifted) {
    kb.reset();
    w.set_key_callback([&kb](SDL_Scancode sc, bool pressed) {
        kb.set_key(sc, pressed);
    });
    QKeyEvent shift_down(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    QKeyEvent glyph_down(QEvent::KeyPress, shifted, Qt::ShiftModifier);
    QApplication::sendEvent(&w, &shift_down);
    QApplication::sendEvent(&w, &glyph_down);
    w.set_key_callback(nullptr);
}

void test_matrix(MainWindow& w) {
    // Caps Shift is row 0 col 0; Symbol Shift is row 7 col 1 (keyboard.cpp:68,124).
    {
        Keyboard kb;
        press_shift_chord(w, kb, Qt::Key_Exclam);          // Shift+1
        check("I123-20",
              "Shift+1 pulls down Caps Shift (0,0) and the '1' key (3,0) "
              "in the guest matrix",
              key_down(kb, 0, 0) && key_down(kb, 3, 0), rows_detail(kb));
    }
    {
        Keyboard kb;
        press_shift_chord(w, kb, Qt::Key_Colon);           // Shift+;
        check("I123-21",
              "Shift+; pulls down Caps Shift (0,0), Symbol Shift (7,1) and the "
              "'O' key (5,1) in the guest matrix",
              key_down(kb, 0, 0) && key_down(kb, 7, 1) && key_down(kb, 5, 1),
              rows_detail(kb));
    }
    {
        Keyboard kb;
        press_shift_chord(w, kb, Qt::Key_Question);        // Shift+/
        check("I123-22",
              "Shift+/ pulls down Caps Shift (0,0), Symbol Shift (7,1) and the "
              "'V' key (0,4) in the guest matrix",
              key_down(kb, 0, 0) && key_down(kb, 7, 1) && key_down(kb, 0, 4),
              rows_detail(kb));
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Issue #123 - shifted symbols reach the guest\n");
    std::printf("===========================================\n\n");

    MainWindow w;
    test_translation(w);
    test_matrix(w);
    std::printf("  Group: I123           - done\n");

    std::printf("\n===========================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
