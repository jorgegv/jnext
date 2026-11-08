// GH #452 - corpus equivalence, shard 1 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-01", "00regression/nex/beanbros.nex"},
    {"HBC-02", "00regression/nex/beast.nex", 0, true, true},
    {"HBC-03", "00regression/nex/blue.nex"},
    {"HBC-04", "00regression/nex/celeste.nex"},
    {"HBC-05", "00regression/nex/celeste2.nex"},
    {"HBC-06", "00regression/nex/contention_test.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 1 of 8"); }
