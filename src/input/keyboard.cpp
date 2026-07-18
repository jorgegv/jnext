#include "input/keyboard.h"
#include "input/membrane_stick.h"
#include "input/joystick_dispatcher.h"
#include "core/log.h"
#include "core/saveable.h"
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

// Task 77 — Alt-modified compounds. A scancode present here resolves to THIS
// compound while a host Alt key is held, and to its s_compound / s_map entry
// otherwise. Kept as a separate table (rather than a modifier field on
// CompoundPos) so every existing lookup is untouched.
static CompoundPos s_alt_compound[SDL_NUM_SCANCODES];

static bool      s_map_init = false;

static void init_map() {
    if (s_map_init) return;
    // Fill all positions as invalid.
    std::memset(s_map,          -1, sizeof(s_map));
    std::memset(s_compound,     -1, sizeof(s_compound));
    std::memset(s_alt_compound, -1, sizeof(s_alt_compound));

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

    // -----------------------------------------------------------------------
    // Task 77 — the remaining Spectrum compound keys.
    //
    // The ZX-side halves are the VHDL extended-key set (membrane.vhd:180-240):
    // real Next hardware synthesises each of these into exactly the classic
    // 48K two-key compound, which is what we assert here directly.
    //
    //   BREAK       = CS + SPACE (Esc)  GRAPH    = CS + 9   (Alt+G)
    //   DELETE      = CS + 0          CAPS LOCK  = CS + 2   (Alt+C)
    //   EDIT        = CS + 1          '"'        = SS + P
    //   TRUE VIDEO  = CS + 3          ';'        = SS + O
    //   INV  VIDEO  = CS + 4          '.'        = SS + M
    //   EXTEND MODE = CS + SS         ','        = SS + N
    //
    //
    // Host-side choices are the project owner's (see the commit message for
    // the FUSE / ZEsarUX convention survey that informed them).

    // EXTEND MODE = Caps Shift + Symbol Shift, on Tab — the strongest
    // cross-emulator convention in this set (FUSE and ZEsarUX both use Tab
    // for EXTEND MODE; neither uses it for BREAK).
    s_compound[SDL_SCANCODE_TAB]    = {{0, 0}, {7, 1}};

    // BREAK = Caps Shift + SPACE, on Esc (ZEsarUX / Spectaculator convention).
    // Esc is UNCONDITIONAL — it no longer exits fullscreen in either frontend
    // (F11 is now the only fullscreen toggle), so BREAK behaves identically
    // windowed and fullscreen.
    s_compound[SDL_SCANCODE_ESCAPE] = {{0, 0}, {7, 0}};

    // TRUE VIDEO = Caps Shift + 3, on the key immediately LEFT of "1".
    // SDL scancodes are PHYSICAL positions, so SDL_SCANCODE_GRAVE is that
    // key on every layout — the owner's "\" label describes an ISO keycap,
    // the position is what is actually being specified.
    s_compound[SDL_SCANCODE_GRAVE]  = {{0, 0}, {3, 2}};
    // INV VIDEO = Caps Shift + 4, same key with Alt.
    s_alt_compound[SDL_SCANCODE_GRAVE] = {{0, 0}, {3, 3}};

    // EDIT = Caps Shift + 1 (Alt+E). Alt+D is NOT usable: the Qt main window
    // has a top-level "&Debug" menu whose mnemonic consumes Alt+D before any
    // key event reaches the emulator. Taken mnemonics are &File &Machine
    // &Input &Tape &Debug &View &Settings &Help, so Alt + F/M/I/T/D/V/S/H are
    // all unavailable; E and the grave key are free.
    s_alt_compound[SDL_SCANCODE_E]  = {{0, 0}, {3, 0}};
    // GRAPH = Caps Shift + 9 (Alt+G); CAPS LOCK = Caps Shift + 2 (Alt+C).
    // Same VHDL fold table as the rest — see the clear_if() rows in
    // read_rows(): GRAPH folds onto row 4 col 1 (key '9'), CAPS LOCK onto
    // row 3 col 1 (key '2'). G and C are both free of menu mnemonics.
    s_alt_compound[SDL_SCANCODE_G]  = {{0, 0}, {4, 1}};
    s_alt_compound[SDL_SCANCODE_C]  = {{0, 0}, {3, 1}};

    // Symbol-shifted punctuation, on the natural PC key for each glyph.
    s_compound[SDL_SCANCODE_APOSTROPHE] = {{7, 1}, {5, 0}};  // SS + P  → '"'
    s_compound[SDL_SCANCODE_SEMICOLON]  = {{7, 1}, {5, 1}};  // SS + O  → ';'
    s_compound[SDL_SCANCODE_PERIOD]     = {{7, 1}, {7, 2}};  // SS + M  → '.'
    s_compound[SDL_SCANCODE_COMMA]      = {{7, 1}, {7, 3}};  // SS + N  → ','

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
    // Task 77 — host Alt modifier state. Clearing alt_held_ is load-bearing:
    // a reset taken with Alt physically down would otherwise keep resolving
    // Alt variants afterwards (covered by T77K-17).
    alt_held_        = false;
    // The per-scancode latch is cleared for hygiene only, and is deliberately
    // NOT claimed as tested: it is unobservable. Every press edge overwrites
    // alt_variant_[sc] before any release reads it, so the only path that
    // could see a stale entry is a release with no intervening press — which
    // clears matrix bits that reset() has already cleared. Verified by
    // mutation: removing this line fails no row, in either direction.
    std::memset(alt_variant_, 0, sizeof(alt_variant_));
    // NOTE: `membrane_stick_` is lifetime-bound wiring from the owning
    // Emulator (see Emulator ctor). It must NOT be cleared here — a soft
    // reset does not rebuild the Emulator's member graph.
}

void Keyboard::set_key(SDL_Scancode sc, bool pressed) {
    if (sc < 0 || sc >= SDL_NUM_SCANCODES) return;

    // Task 79 — cursor-keys-as-Kempston-joystick. When a connector is the
    // cursor-key target, the arrows drive its direction bits and Space is
    // Fire; the keys are consumed here and do NOT touch the ZX matrix (so the
    // two behaviours are mutually exclusive). The dispatcher re-checks its own
    // per-connector source gate, so this stays correct even if the target and
    // the source momentarily disagree.
    if (cursor_target_slot_ >= 0 && joy_dispatcher_) {
        using CB = JoystickDispatcher::CursorBit;
        switch (sc) {
            case SDL_SCANCODE_UP:    joy_dispatcher_->set_cursor_bit(cursor_target_slot_, CB::Up,    pressed); return;
            case SDL_SCANCODE_DOWN:  joy_dispatcher_->set_cursor_bit(cursor_target_slot_, CB::Down,  pressed); return;
            case SDL_SCANCODE_LEFT:  joy_dispatcher_->set_cursor_bit(cursor_target_slot_, CB::Left,  pressed); return;
            case SDL_SCANCODE_RIGHT: joy_dispatcher_->set_cursor_bit(cursor_target_slot_, CB::Right, pressed); return;
            case SDL_SCANCODE_SPACE: joy_dispatcher_->set_cursor_bit(cursor_target_slot_, CB::Fire,  pressed); return;
            default: break;   // all other keys fall through to the ZX matrix
        }
    }

    // Task 77 — host Alt is a MODIFIER, never a ZX key. Track it and consume
    // it: it has no s_map/s_compound entry, so falling through would be a
    // no-op anyway, but returning here makes the intent explicit.
    if (sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT) {
        alt_held_ = pressed;
        return;
    }

    // Task 77 — resolve which mapping this scancode means RIGHT NOW. A key
    // with an Alt variant means one thing with Alt held and another without,
    // so the choice must be latched at press time and reused at release time:
    // the user may well release Alt before the key, and clearing a different
    // pair of matrix bits than were set would strand a key down forever.
    //
    // Only the press edge consults alt_held_; the release edge replays the
    // latch. `alt_variant_[]` is therefore the authoritative record of what
    // this scancode actually asserted.
    bool use_alt;
    if (pressed) {
        use_alt = alt_held_ && s_alt_compound[sc].a.row >= 0;
        alt_variant_[sc] = use_alt;
    } else {
        use_alt = alt_variant_[sc];
        alt_variant_[sc] = false;
    }

    // Alt-modified compound (e.g. EDIT = Alt+E = Caps Shift + 1). Resolved
    // BEFORE the plain tables so the unmodified meaning of the key cannot
    // leak: Alt+E asserts CS+1 and never the ZX 'E' key.
    if (use_alt) {
        const CompoundPos& acp = s_alt_compound[sc];
        set_matrix_bit(acp.a.row, acp.a.col, pressed);
        set_matrix_bit(acp.b.row, acp.b.col, pressed);
        return;
    }

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
    // Two layered post-processes apply BEFORE the row-select AND:
    //
    // (1) Agent F — shift hysteresis on matrix_[0] bit 0 (Caps Shift) and
    //     matrix_[7] bit 1 (Symbol Shift). Per membrane.vhd:178 the
    //     shift-column state is "held an extra scan" — a release is
    //     delayed by one tick. Active-low semantics:
    //       effective_CS  = matrix_[0] bit 0 AND shift_hist_[0] bit 0
    //       effective_SYM = matrix_[7] bit 1 AND shift_hist_[0] bit 1
    //     If the current matrix says "released" (bit=1) but the previous
    //     scan saw "pressed" (bit=0), the AND yields "pressed" (bit=0)
    //     for one extra scan. Existing KBD-01..KBD-21 tests never call
    //     tick_scan() so shift_hist_[] stays at its reset {0xFF, 0xFF},
    //     making the AND degenerate to matrix_[] and preserving every
    //     pre-Phase-2 expected value.
    //
    // (2) Agent G — extended-column folding from membrane.vhd:236-240.
    //     Pressing an extended key forces its target (row,col) bit low
    //     on the active-low result IN ADDITION to any membrane bit.
    //     Per ExtKey ID → (row, col) table:
    //       RIGHT (0)  → (4,2)   LEFT  (1)  → (3,4)   DOWN   (2)  → (4,4)
    //       UP    (3)  → (4,3)   DOT   (4)  → (7,2)   COMMA  (5)  → (7,3)
    //       QUOTE (6)  → (5,0)   SEMI  (7)  → (5,1)   CAPSLK (9)  → (3,1)
    //       GRAPH (10) → (4,1)   TRUEV (11) → (3,2)   INVV   (12) → (3,3)
    //       BREAK (13) → (7,0)   EDIT  (14) → (3,0)   DELETE (15) → (4,0)
    //     EXTEND (8) and shift-fold targets (0,0)/(7,1) live in Agent F's
    //     hysteresis layer, not the ext-fold layer.

    // --- (1) Hysteresis: hold-extend bit 0 of row 0 + bit 1 of row 7. -
    uint8_t row0_eff = matrix_[0];
    uint8_t row7_eff = matrix_[7];
    const uint8_t cs_prev  = shift_hist_[0] & 0x01u;
    row0_eff = (row0_eff & ~0x01u) | ((row0_eff & 0x01u) & cs_prev);
    const uint8_t sym_prev = shift_hist_[0] & 0x02u;
    row7_eff = (row7_eff & ~0x02u) | ((row7_eff & 0x02u) & sym_prev);

    // --- (2) Extended-column folding masks (Agent G). ----------------
    uint8_t ext_rowmask[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    const uint16_t ex = ex_matrix_;
    auto clear_if = [&](int id, int row, int col) {
        if (ex & (1u << id)) {
            ext_rowmask[row] &= static_cast<uint8_t>(~(1u << col));
        }
    };
    // Row 3 folds (membrane.vhd:237):
    clear_if(14, 3, 0);   // EDIT        → col 0 (key '1')
    clear_if( 9, 3, 1);   // CAPS LOCK   → col 1 (key '2')
    clear_if(11, 3, 2);   // TRUE VIDEO  → col 2 (key '3')
    clear_if(12, 3, 3);   // INV  VIDEO  → col 3 (key '4')
    clear_if( 1, 3, 4);   // LEFT        → col 4 (key '5')
    // Row 4 folds (membrane.vhd:238):
    clear_if(15, 4, 0);   // DELETE      → col 0 (key '0')
    clear_if(10, 4, 1);   // GRAPH       → col 1 (key '9')
    clear_if( 0, 4, 2);   // RIGHT       → col 2 (key '8')
    clear_if( 3, 4, 3);   // UP          → col 3 (key '7')
    clear_if( 2, 4, 4);   // DOWN        → col 4 (key '6')
    // Row 5 folds (membrane.vhd:239):
    clear_if( 6, 5, 0);   // '"'         → col 0 (key 'P')
    clear_if( 7, 5, 1);   // ';'         → col 1 (key 'O')
    // Row 7 folds (membrane.vhd:240):
    clear_if(13, 7, 0);   // BREAK       → col 0 (key SPACE)
    clear_if( 4, 7, 2);   // '.'         → col 2 (key 'M')
    clear_if( 5, 7, 3);   // ','         → col 3 (key 'N')

    // --- Row select AND with all layers applied per row --------------
    //
    // (3) MembraneStick fold (joystick→membrane adapter). Applied AFTER
    //     the extended-key fold so the active-low AND of every layer is
    //     the effective row mask — matches VHDL
    //     `zxnext_top_issue4.vhd:1843`:
    //         keyb_col <= keyb_col_i_q AND membrane_stick_col AND ps2_kbd_col
    //     Null-safe: bare Keyboard instances (unit tests) leave
    //     membrane_stick_ at nullptr and skip this fold entirely. Per-row
    //     gating / NR-0x05 mode / joystick-enable semantics all live
    //     inside MembraneStick::compose_into_row() — we just splice the
    //     5-bit row mask through it.
    uint8_t result = 0x1F;
    for (int row = 0; row < 8; ++row) {
        if (!(addr_high & (1 << row))) {
            uint8_t r = matrix_[row];
            if      (row == 0) r = row0_eff;   // (1) hysteresis
            else if (row == 7) r = row7_eff;
            r = static_cast<uint8_t>(r & ext_rowmask[row]);  // (2) ext fold
            if (membrane_stick_) {
                r = membrane_stick_->compose_into_row(row, r);  // (3) joy fold
            }
            result &= r;
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
    // Two-scan shift hysteresis advance. Mirrors membrane.vhd:188-191
    // where on a scan-cycle boundary (state(0)='0'):
    //     matrix_state_ex_1 <= matrix_state_ex_0;
    //     matrix_state_ex_0 <= <this scan's observations>;
    //     matrix_work_ex    <= (others => '1');
    //
    // We track only the two shift bits (CS row 0 col 0, SYM row 7 col 1)
    // because everything else in matrix_state_ex is extended-key
    // territory (Agent G). Both stored fields are active-low to match
    // the VHDL, with bit 0 = CS and bit 1 = SYM; other bits are kept as
    // 1 (released) to stay harmless.
    //
    // Snapshot the CURRENT membrane state into shift_hist_[0] and
    // push the previous shift_hist_[0] into shift_hist_[1].
    const uint8_t cs_now  = (matrix_[0] & 0x01u) ? 0x01u : 0x00u;   // active-low
    const uint8_t sym_now = (matrix_[7] & 0x02u) ? 0x02u : 0x00u;   // active-low
    const uint8_t snap = 0xFCu | cs_now | sym_now;  // bits 2..7 stay 1
    shift_hist_[1] = shift_hist_[0];
    shift_hist_[0] = snap;
}

void Keyboard::cancel_extended_entries() {
    // Per membrane.vhd:183-186: on i_cancel_extended_entries='1' the
    // FPGA flushes matrix_state_ex_{0,1} and matrix_work_ex to all-'1'
    // in VHDL terms (i.e. all-released in the internal active-low
    // representation). Our ex_matrix_ is stored ACTIVE-HIGH in the C++
    // model (Phase-1 polarity fix; NR 0xB0/0xB1 bit=1 => pressed, per
    // zxnext.vhd:6206-6212) so "all released" means ex_matrix_ = 0.
    //
    // The shift-hysteresis history is also flushed (both scans snap to
    // "CS and SYM released") because the VHDL reset/cancel branch
    // clears the whole matrix_state_ex pipeline in one go, which
    // includes the two shift bits folded in at lines 190/232.
    ex_matrix_     = 0x0000;
    shift_hist_[0] = 0xFF;
    shift_hist_[1] = 0xFF;
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

// ---------------------------------------------------------------------------
// Task 60c — state serialisation
// ---------------------------------------------------------------------------

void Keyboard::save_state(StateWriter& w) const
{
    // 8-row membrane matrix (active-low, bit N=0 ⇒ col N pressed).
    w.write_bytes(matrix_, 8);
    // 16-bit extended-key register (active-high).
    w.write_u16(ex_matrix_);
    // Two-scan shift-hysteresis buffer (active-low).
    w.write_bytes(shift_hist_, 2);
    // Auto-type FSM: the in-flight queue plus the frame counter and the
    // inter-key gap flag. The queue can be non-empty mid-play (e.g. an
    // instant-TAP LOAD"" sequence), so it must travel with the snapshot.
    w.write_u32(static_cast<uint32_t>(auto_queue_.size()));
    for (const auto& k : auto_queue_) {
        w.write_i32(k.row1);
        w.write_i32(k.col1);
        w.write_i32(k.row2);
        w.write_i32(k.col2);
        w.write_i32(k.frames);
    }
    w.write_i32(auto_frame_count_);
    w.write_bool(auto_gap_);
}

void Keyboard::load_state(StateReader& r)
{
    r.read_bytes(matrix_, 8);
    ex_matrix_ = r.read_u16();
    r.read_bytes(shift_hist_, 2);
    const uint32_t n = r.read_u32();
    auto_queue_.clear();
    // Guard against a corrupt count causing a pathological loop: each entry
    // consumes 20 bytes, so a bad `n` exhausts the buffer within
    // capacity/20 iterations and out_of_bounds() latches — bail then. The
    // sentinel check in Emulator::load_state reports the corruption loudly.
    for (uint32_t i = 0; i < n && !r.out_of_bounds(); ++i) {
        AutoKey k;
        k.row1   = r.read_i32();
        k.col1   = r.read_i32();
        k.row2   = r.read_i32();
        k.col2   = r.read_i32();
        k.frames = r.read_i32();
        auto_queue_.push_back(k);
    }
    auto_frame_count_ = r.read_i32();
    auto_gap_         = r.read_bool();
}
