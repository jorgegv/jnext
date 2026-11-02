#pragma once

// ---------------------------------------------------------------------------
// The skoolkit control-file export (GH #389, E23-26): labels, comments and the
// coverage code/data map as the control files `sna2skool.py` reads.
//
// One file per (space, window): `<WWWW>-<TAG>.ctl`, WWWW the 4-digit upper-case hex
// base of an aligned window of the space (8 KB; 16 KB for ROM, alternate ROM and
// config), TAG the space's `space_tag()`; beside it `<WWWW>-<TAG>.bin`, the
// window's bytes, so `sna2skool.py -o 0xWWWW <WWWW>-<TAG>.bin` picks that control
// file (and only it) by default. Unpaged labels are in `0000-CPU.ctl`. The names
// sort in address order, which `sna2skool.py -c DIR` needs: two files that
// disagree at a shared boundary address are applied in command-line order.
//
// Per window: `# jnext space <TAG>` (a control-file comment skoolkit ignores),
// `@ $WWWW start` / `@ $WWWW org`, then block directives from the code/data
// classification (below), `@ $AAAA label=NAME`, sub-blocks for comments and for the
// bytes skoolkit cannot decode, and `i $EEEE` at the window's end.
//
// THE CLASSIFIER. For a space's whole store, from its coverage flags and bytes: an
// X byte starts an instruction whose extent is `instruction_length()` over the
// store, clipped at the store's end and at the next X start; class per byte is
// code (inside an extent) > data (R or W) > unknown. A space with no mark at all
// is one code run with no known instruction starts, as skoolkit's own default.
//
// SKOOLKIT IS Z80-ONLY. A Z80N extension (`ED` + NEXTREG, MUL, ...) and a clipped
// instruction are written as `B` (DEFB) sub-blocks, so the code after them stays
// aligned; a label or comment inside an instruction splits it into `B` sub-blocks
// at the key, since sna2skool silently drops a label that is not at a line start.
//
// Pure: std, `mem_space.h`, `long_address.h`, `disasm.h`. No machine, no Qt.
// ---------------------------------------------------------------------------

#include "debug/reexport.h"

#include <functional>

namespace jnext {
namespace dbg {
namespace skoolkit {

enum class Class : uint8_t { Unknown, Data, Code };

/// A maximal run of one class, store offsets [start, end).
struct Run {
    Class cls = Class::Unknown;
    uint32_t start = 0;
    uint32_t end = 0;
};

/// One executed instruction: where it starts, how many bytes it is given (`len`,
/// after clipping), whether it was clipped, whether it is a Z80N extension.
struct Insn {
    uint32_t off = 0;
    uint32_t len = 0;
    bool clipped = false;
    bool z80n = false;
};

struct Classified {
    std::vector<Run> runs;      ///< cover [0, size) with no gap
    std::vector<Insn> insns;    ///< ascending; empty when `!marked`
    bool marked = false;        ///< the space had at least one mark
};

/// Classify a store. `flags` and `bytes` have the store's size, or `flags` is empty
/// (no marks): the result is then one code run over `size` bytes.
Classified classify(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& flags,
                    uint32_t size);

/// One space's store: the bytes `peek()` reads (empty when they cannot be read) and
/// its coverage flags (empty when nothing is marked or they cannot be used).
struct Store {
    MemSpace space;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> flags;
};

struct Input {
    std::vector<reexport::Label> labels;
    std::vector<reexport::Comment> comments;
    std::vector<Store> stores;
    /// The lowest window base at which `space` is mapped now (the memory a CPU
    /// fetch at that address would read), or nullopt. Used for a space with no keys.
    std::function<std::optional<uint16_t>(MemSpace)> mapped_window;
};

/// The window size of a space: the size of its store (8 KB, 16 KB for Rom, AltRom, Config).
uint32_t window_size(MemSpace s);

/// Where a space with neither keys nor a current mapping is put: the 128K bank
/// positions for a RAM page (0x0A/0x0B at $4000/$6000, 0x04/0x05 at $8000/$A000,
/// any other n at $C000 + (n & 1) * $2000), $0000 for ROM, $2000 for a DivMMC bank.
uint16_t home_window(MemSpace s);

reexport::Output build(const Input& in);

}  // namespace skoolkit
}  // namespace dbg
}  // namespace jnext
