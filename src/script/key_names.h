#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the key names the recorder WRITES and `press`/`release`
// READ, for the two inputs the backend's shared name table
// (`dbg::key_name_to_matrix`, the `--delayed-keypress` vocabulary) does not
// cover (GH #26 WP6 / #20, dsl-frontend.md §7.2; "WP6 as built", Appendix L):
//
//   * a single membrane-matrix BIT, by name where one exists. The shared table
//     names keys, some of them compounds (`left` = CAPS SHIFT + 5); the
//     recorder samples bits, one edge per bit, so it needs the inverse for one
//     bit. CAPS SHIFT (0,0) and SYMBOL SHIFT (7,1) have no single-key name in
//     that table and are written as their `row,col` pair, which `press`
//     already accepts.
//   * the 16 Next EXTENDED keys (NR 0xB0 / 0xB1), as `ext:<name>`. The host's
//     arrows, Backspace, Esc and friends drive these, not the matrix (issue
//     #33), so a recording that could not name them would drop real input.
// ---------------------------------------------------------------------------

#include <string>

namespace jnext {
namespace script {

/// The name `press` reads for matrix bit (row, col): a single character
/// (`q`, `1`), `enter` / `space`, or `row,col` for CAPS SHIFT and SYMBOL
/// SHIFT. Empty for a position outside the 8x5 matrix.
std::string matrix_bit_name(int row, int col);

/// `ext:<name>` for extended key `id` (0..15, NR 0xB0 bits 0..7 then NR 0xB1
/// bits 0..7). Empty for an id out of range.
std::string ext_key_name(int id);

/// The id of an `ext:<name>` key name (case-insensitive), or -1 when `name`
/// is not one.
int ext_key_id(const std::string& name);

}  // namespace script
}  // namespace jnext
