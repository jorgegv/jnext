#include "input/keyboard.h"
#include "core/log.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Scancode → (row, col) lookup table
// ---------------------------------------------------------------------------

namespace {

struct MatrixPos { int8_t row; int8_t col; };

// SDL_NUM_SCANCODES is 512; we use a flat array indexed by SDL_Scancode.
// Unrecognised scancodes have row == -1.
static MatrixPos s_map[SDL_NUM_SCANCODES];

// Compound keys press two matrix positions simultaneously (e.g. DELETE = Caps Shift + 0).
struct CompoundPos { MatrixPos a; MatrixPos b; };
static CompoundPos s_compound[SDL_NUM_SCANCODES];

static bool      s_map_init = false;

static void init_map() {
    if (s_map_init) return;
    // Fill all positions as invalid.
    std::memset(s_map,      -1, sizeof(s_map));
    std::memset(s_compound, -1, sizeof(s_compound));

    // Row 0: CAPS-SHIFT  Z  X  C  V
    // Ctrl keys map to Caps Shift (row 0, col 0).
    s_map[SDL_SCANCODE_LCTRL]  = {0, 0};
    s_map[SDL_SCANCODE_RCTRL]  = {0, 0};
    s_map[SDL_SCANCODE_Z]      = {0, 1};
    s_map[SDL_SCANCODE_X]      = {0, 2};
    s_map[SDL_SCANCODE_C]      = {0, 3};
    s_map[SDL_SCANCODE_V]      = {0, 4};

    // Row 1: A  S  D  F  G
    s_map[SDL_SCANCODE_A]      = {1, 0};
    s_map[SDL_SCANCODE_S]      = {1, 1};
    s_map[SDL_SCANCODE_D]      = {1, 2};
    s_map[SDL_SCANCODE_F]      = {1, 3};
    s_map[SDL_SCANCODE_G]      = {1, 4};

    // Row 2: Q  W  E  R  T
    s_map[SDL_SCANCODE_Q]      = {2, 0};
    s_map[SDL_SCANCODE_W]      = {2, 1};
    s_map[SDL_SCANCODE_E]      = {2, 2};
    s_map[SDL_SCANCODE_R]      = {2, 3};
    s_map[SDL_SCANCODE_T]      = {2, 4};

    // Row 3: 1  2  3  4  5
    s_map[SDL_SCANCODE_1]      = {3, 0};
    s_map[SDL_SCANCODE_2]      = {3, 1};
    s_map[SDL_SCANCODE_3]      = {3, 2};
    s_map[SDL_SCANCODE_4]      = {3, 3};
    s_map[SDL_SCANCODE_5]      = {3, 4};

    // Row 4: 0  9  8  7  6
    s_map[SDL_SCANCODE_0]      = {4, 0};
    s_map[SDL_SCANCODE_9]      = {4, 1};
    s_map[SDL_SCANCODE_8]      = {4, 2};
    s_map[SDL_SCANCODE_7]      = {4, 3};
    s_map[SDL_SCANCODE_6]      = {4, 4};

    // Row 5: P  O  I  U  Y
    s_map[SDL_SCANCODE_P]      = {5, 0};
    s_map[SDL_SCANCODE_O]      = {5, 1};
    s_map[SDL_SCANCODE_I]      = {5, 2};
    s_map[SDL_SCANCODE_U]      = {5, 3};
    s_map[SDL_SCANCODE_Y]      = {5, 4};

    // Row 6: ENTER  L  K  J  H
    s_map[SDL_SCANCODE_RETURN]   = {6, 0};
    s_map[SDL_SCANCODE_RETURN2]  = {6, 0};
    s_map[SDL_SCANCODE_KP_ENTER] = {6, 0};
    s_map[SDL_SCANCODE_L]        = {6, 1};
    s_map[SDL_SCANCODE_K]      = {6, 2};
    s_map[SDL_SCANCODE_J]      = {6, 3};
    s_map[SDL_SCANCODE_H]      = {6, 4};

    // Row 7: SPACE  SYM-SHIFT  M  N  B
    // Shift keys map to Symbol Shift (row 7, col 1).
    s_map[SDL_SCANCODE_SPACE]  = {7, 0};
    s_map[SDL_SCANCODE_LSHIFT] = {7, 1};
    s_map[SDL_SCANCODE_RSHIFT] = {7, 1};
    s_map[SDL_SCANCODE_M]      = {7, 2};
    s_map[SDL_SCANCODE_N]      = {7, 3};
    s_map[SDL_SCANCODE_B]      = {7, 4};

    // Compound keys: BACKSPACE = Caps Shift (row 0, col 0) + 0 (row 4, col 0)
    // This produces the ZX Spectrum RUBOUT/DELETE function.
    s_compound[SDL_SCANCODE_BACKSPACE] = {{0, 0}, {4, 0}};

    // Cursor keys: PC arrows → Caps Shift + 5/6/7/8
    s_compound[SDL_SCANCODE_LEFT]  = {{0, 0}, {3, 4}};  // Caps Shift + 5
    s_compound[SDL_SCANCODE_DOWN]  = {{0, 0}, {4, 4}};  // Caps Shift + 6
    s_compound[SDL_SCANCODE_UP]    = {{0, 0}, {4, 3}};  // Caps Shift + 7
    s_compound[SDL_SCANCODE_RIGHT] = {{0, 0}, {4, 2}};  // Caps Shift + 8

    s_map_init = true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void Keyboard::reset() {
    init_map();
    // All bits set = all keys released (active-low).
    std::memset(matrix_, 0xFF, sizeof(matrix_));
}

void Keyboard::set_key(SDL_Scancode sc, bool pressed) {
    if (sc < 0 || sc >= SDL_NUM_SCANCODES) return;
    // Check compound map first (e.g. DELETE = Caps Shift + 0).
    const CompoundPos& cp = s_compound[sc];
    if (cp.a.row >= 0) {
        set_matrix_bit(cp.a.row, cp.a.col, pressed);
        set_matrix_bit(cp.b.row, cp.b.col, pressed);
        return;
    }
    const MatrixPos& pos = s_map[sc];
    if (pos.row < 0) return;   // not mapped
    set_matrix_bit(pos.row, pos.col, pressed);
}

uint8_t Keyboard::read_rows(uint8_t addr_high) const {
    // Start with all bits 1 (no key pressed in result).
    uint8_t result = 0x1F;
    for (int row = 0; row < 8; ++row) {
        // Row N is selected when bit N of addr_high is 0.
        if (!(addr_high & (1 << row))) {
            result &= matrix_[row];
        }
    }
    return result & 0x1F;
}

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

void Keyboard::set_matrix_bit(int row, int col, bool pressed) {
    Log::input()->trace("Key matrix [{},{}] {}", row, col, pressed ? "pressed" : "released");
    if (pressed) {
        // Clear bit: key pressed (active-low)
        matrix_[row] &= ~(1 << col);
    } else {
        // Set bit: key released
        matrix_[row] |= (1 << col);
    }
}
