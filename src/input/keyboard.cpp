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
    // Extended-key matrix is ACTIVE-HIGH: 0 means all released
    // (matches VHDL i_KBD_EXTENDED_KEYS-after-inversion, zxnext.vhd:6206-6212
    // where jnext exposes NR 0xB0/0xB1 as active-high).
    ex_matrix_       = 0x0000;
    // Shift-hysteresis scan buffers — initial "all released" state.
    shift_hist_[0]   = 0xFF;
    shift_hist_[1]   = 0xFF;
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
    // Standard 8×5 membrane AND across selected rows, then apply the
    // extended-column folding from membrane.vhd:236-240. Fold semantics
    // (active-low on the wire): pressing an extended key forces its
    // target (row,col) bit low in addition to any physical membrane
    // contribution. Extended keys that target a row other than the
    // currently-selected ones have no observable effect (the fold
    // result is gated by the row select at membrane.vhd:242-249).
    //
    // Table (ExtKey → matrix_state_ex bit → (row, col)):
    //   RIGHT      (id 0)  → bit  8 → (4, 2)
    //   LEFT       (id 1)  → bit  5 → (3, 4)
    //   DOWN       (id 2)  → bit 10 → (4, 4)
    //   UP         (id 3)  → bit  9 → (4, 3)
    //   DOT  '.'   (id 4)  → bit 15 → (7, 2)
    //   COMMA ','  (id 5)  → bit 16 → (7, 3)
    //   QUOTE '"'  (id 6)  → bit 11 → (5, 0)
    //   SEMI  ';'  (id 7)  → bit 12 → (5, 1)
    //   EXTEND     (id 8)  → (only via Agent F shift hysteresis)
    //   CAPS LOCK  (id 9)  → bit  2 → (3, 1)
    //   GRAPH      (id 10) → bit  7 → (4, 1)
    //   TRUE VIDEO (id 11) → bit  3 → (3, 2)
    //   INV VIDEO  (id 12) → bit  4 → (3, 3)
    //   BREAK      (id 13) → bit 13 → (7, 0)
    //   EDIT       (id 14) → bit  1 → (3, 0)
    //   DELETE     (id 15) → bit  6 → (4, 0)
    //
    // ex_matrix_ is active-high (bit=1 ⇒ pressed); when pressed we
    // clear the target (row,col) bit on the active-low result.

    // --- 1. Effective per-row masks after extended folding --------
    // ext_rowmask[row] = active-low mask to AND into matrix_[row].
    // Start 0x1F (no fold effect); clear target bits for each pressed
    // extended key per the table above.
    uint8_t ext_rowmask[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};

    const uint16_t ex = ex_matrix_;
    auto clear_if = [&](int id, int row, int col) {
        if (ex & (1u << id)) {
            ext_rowmask[row] &= static_cast<uint8_t>(~(1u << col));
        }
    };
    // Row 3 folds (matrix_state_3, membrane.vhd:237 — state_ex(5..1)):
    clear_if(14, 3, 0);   // EDIT        → col 0 (key '1')
    clear_if( 9, 3, 1);   // CAPS LOCK   → col 1 (key '2')
    clear_if(11, 3, 2);   // TRUE VIDEO  → col 2 (key '3')
    clear_if(12, 3, 3);   // INV  VIDEO  → col 3 (key '4')
    clear_if( 1, 3, 4);   // LEFT        → col 4 (key '5')
    // Row 4 folds (matrix_state_4, membrane.vhd:238 — state_ex(10..6)):
    clear_if(15, 4, 0);   // DELETE      → col 0 (key '0')
    clear_if(10, 4, 1);   // GRAPH       → col 1 (key '9')
    clear_if( 0, 4, 2);   // RIGHT       → col 2 (key '8')
    clear_if( 3, 4, 3);   // UP          → col 3 (key '7')
    clear_if( 2, 4, 4);   // DOWN        → col 4 (key '6')
    // Row 5 folds (matrix_state_5, membrane.vhd:239 — state_ex(12..11)):
    clear_if( 6, 5, 0);   // '"'         → col 0 (key 'P')
    clear_if( 7, 5, 1);   // ';'         → col 1 (key 'O')
    // Row 7 folds (matrix_state_7, membrane.vhd:240 — state_ex(16..13)):
    clear_if(13, 7, 0);   // BREAK       → col 0 (key SPACE)
    // Note: state_ex(14) fold into (7,1) = SYM SHIFT is the Sym-Shift
    // hysteresis bit — Agent F's scope, not folded here.
    clear_if( 4, 7, 2);   // '.'         → col 2 (key 'M')
    clear_if( 5, 7, 3);   // ','         → col 3 (key 'N')
    // Row 0 col 0 fold (matrix_state_0, membrane.vhd:236 — state_ex(0))
    // is the Caps-Shift hysteresis bit — Agent F's scope.

    // --- 2. Row select AND, exactly as before for unchanged rows --
    uint8_t result = 0x1F;
    for (int row = 0; row < 8; ++row) {
        // Row N is selected when bit N of addr_high is 0.
        if (!(addr_high & (1 << row))) {
            result &= static_cast<uint8_t>(matrix_[row] & ext_rowmask[row]);
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

// ---------------------------------------------------------------------------
// Auto-type
// ---------------------------------------------------------------------------

void Keyboard::queue_auto_type(const std::vector<AutoKey>& keys) {
    auto_queue_ = keys;
    auto_frame_count_ = 0;
    auto_gap_ = false;
    Log::input()->info("Auto-type: queued {} keystrokes", keys.size());
}

void Keyboard::tick_auto_type() {
    if (auto_queue_.empty()) return;

    if (auto_gap_) {
        // Gap between keys: all keys released for 4 frames
        // (ROM debounce needs to see key released before re-accepting)
        ++auto_frame_count_;
        if (auto_frame_count_ >= 4) {
            auto_gap_ = false;
            auto_frame_count_ = 0;
        }
        return;
    }

    const AutoKey& key = auto_queue_.front();

    if (auto_frame_count_ == 0) {
        // Press the key(s)
        set_matrix_bit(key.row1, key.col1, true);
        if (key.row2 >= 0) {
            set_matrix_bit(key.row2, key.col2, true);
        }
    }

    ++auto_frame_count_;

    if (auto_frame_count_ >= key.frames) {
        // Release the key(s)
        set_matrix_bit(key.row1, key.col1, false);
        if (key.row2 >= 0) {
            set_matrix_bit(key.row2, key.col2, false);
        }
        auto_queue_.erase(auto_queue_.begin());
        auto_frame_count_ = 0;
        auto_gap_ = true;  // enter gap before next key
    }
}

// ---------------------------------------------------------------------------
// Phase 2 Agent G: extended-key matrix + NR 0xB0/0xB1 readback.
// ---------------------------------------------------------------------------

void Keyboard::set_extended_key(int id, bool pressed) {
    // Out-of-range IDs are ignored — only 16 extended keys are defined
    // (ExtKey::RIGHT..DELETE, ids 0..15). See zxnext.vhd:6206-6212.
    if (id < 0 || id > 15) return;
    if (pressed) {
        ex_matrix_ |= static_cast<uint16_t>(1u << id);
    } else {
        ex_matrix_ &= static_cast<uint16_t>(~(1u << id));
    }
}

void Keyboard::tick_scan() {
    // Phase 1 stub — no-op. Agent F advances the shift-hysteresis
    // confirm-on-two-agreements FSM each scan tick.
}

uint8_t Keyboard::nr_b0_byte() const {
    // zxnext.vhd:6208 composes port_253b_dat as
    //   i_KBD_EXTENDED_KEYS(8) & (9) & (10) & (11)      // ; " , .
    //   & (1) & (15 downto 13)                          // UP DOWN LEFT RIGHT
    // Mapped 1:1 onto ExtKey IDs 7..0 (SEMICOLON..RIGHT), so the low
    // byte of ex_matrix_ IS the NR 0xB0 byte (active-high).
    return static_cast<uint8_t>(ex_matrix_ & 0x00FFu);
}

uint8_t Keyboard::nr_b1_byte() const {
    // zxnext.vhd:6212 composes port_253b_dat as
    //   i_KBD_EXTENDED_KEYS(12) & (7 downto 2) & (0)
    //   // DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
    // Mapped 1:1 onto ExtKey IDs 15..8 (DELETE..EXTEND), so the high
    // byte of ex_matrix_ IS the NR 0xB1 byte (active-high).
    return static_cast<uint8_t>((ex_matrix_ >> 8) & 0x00FFu);
}
