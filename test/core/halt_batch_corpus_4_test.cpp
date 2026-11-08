// GH #452 - corpus equivalence, shard 4 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-19", "00regression/nex/palette_demo.nex"},
    {"HBC-20", "00regression/nex/parallax.nex", 0, true},
    {"HBC-21", "00regression/nex/red.nex"},
    {"HBC-22", "00regression/nex/santaspressie.nex"},
    {"HBC-23", "00regression/nex/shift.nex"},
    {"HBC-24", "00regression/nex/show512.nex", 0, true},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 4 of 8"); }
