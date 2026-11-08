// GH #452 - corpus equivalence, shard 7 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-37", "00regression/nex/tilemap_demo.nex"},
    {"HBC-38", "00regression/nex/trainyard-express.nex"},
    {"HBC-39", "nex-nonworking/bloaters.nex", 400, true},
    {"HBC-40", "nex-nonworking/hott.nex"},
    {"HBC-41", "nex-nonworking/lom.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 7 of 8"); }
