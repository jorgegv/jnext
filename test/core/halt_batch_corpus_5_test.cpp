// GH #452 - corpus equivalence, shard 5 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-25", "00regression/nex/sprite_anchor_test.nex"},
    {"HBC-26", "00regression/nex/sprite_scaling_test.nex"},
    {"HBC-27", "00regression/nex/stencil_test.nex"},
    {"HBC-28", "00regression/nex/test01l2empty.nex"},
    {"HBC-29", "00regression/nex/test02layer2.nex"},
    {"HBC-30", "00regression/nex/test03sprite.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 5 of 8"); }
