// GH #452 - corpus equivalence, shard 8 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-42", "nex-nonworking/nextoid.nex", 0, true, true},
    {"HBC-43", "nex-nonworking/night-knight-demo.nex"},
    {"HBC-44", "nex-nonworking/revivalsurvival.nex"},
    {"HBC-45", "nex-nonworking/TX-1696.nex"},
    {"HBC-46", "nex-nonworking/warhawk.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 8 of 8"); }
