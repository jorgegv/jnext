// jnext::script — key names for the recorder and `press`. See key_names.h.

#include "script/key_names.h"

#include <cctype>

namespace jnext {
namespace script {

namespace {

// The membrane, row by row, column 0 first — the inverse of
// `char_to_matrix()` in src/debug/inspect.cpp. Empty = no single-key name.
const char* const kMatrix[8][5] = {
    {"", "z", "x", "c", "v"},             // row 0: CAPS SHIFT Z X C V
    {"a", "s", "d", "f", "g"},            // row 1
    {"q", "w", "e", "r", "t"},            // row 2
    {"1", "2", "3", "4", "5"},            // row 3
    {"0", "9", "8", "7", "6"},            // row 4
    {"p", "o", "i", "u", "y"},            // row 5
    {"enter", "l", "k", "j", "h"},        // row 6
    {"space", "", "m", "n", "b"},         // row 7: SPACE SYMBOL-SHIFT M N B
};

// `Keyboard::ExtKey`, in id order (keyboard.h).
const char* const kExt[16] = {
    "right", "left", "down", "up", "dot", "comma", "quote", "semicolon",
    "extend", "capslock", "graph", "truevideo", "invvideo", "break", "edit", "delete",
};

}  // namespace

std::string matrix_bit_name(int row, int col) {
    if (row < 0 || row > 7 || col < 0 || col > 4) return {};
    if (*kMatrix[row][col]) return kMatrix[row][col];
    return std::to_string(row) + "," + std::to_string(col);
}

std::string ext_key_name(int id) {
    if (id < 0 || id > 15) return {};
    return std::string("ext:") + kExt[id];
}

int ext_key_id(const std::string& name) {
    std::string k;
    for (char c : name) k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (k.compare(0, 4, "ext:") != 0) return -1;
    for (int i = 0; i < 16; ++i)
        if (k.compare(4, std::string::npos, kExt[i]) == 0) return i;
    return -1;
}

}  // namespace script
}  // namespace jnext
