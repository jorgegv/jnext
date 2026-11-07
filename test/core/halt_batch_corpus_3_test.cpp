// GH #452 - corpus equivalence, shard 3 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-13", "00regression/nex/layer2_640x256_test.nex"},
    {"HBC-14", "00regression/nex/lores_demo.nex"},
    {"HBC-15", "00regression/nex/magic_bp_demo.nex"},
    {"HBC-16", "00regression/nex/magic_port_demo.nex"},
    {"HBC-17", "00regression/nex/menu.nex"},
    {"HBC-18", "00regression/nex/odemo.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 3 of 8"); }
