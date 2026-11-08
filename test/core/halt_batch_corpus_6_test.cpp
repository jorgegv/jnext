// GH #452 - corpus equivalence, shard 6 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-31", "00regression/nex/test04tilemap.nex"},
    {"HBC-32", "00regression/nex/test05print.nex"},
    {"HBC-33", "00regression/nex/test06keyb.nex"},
    {"HBC-34", "00regression/nex/test07joystick.nex"},
    {"HBC-35", "00regression/nex/test09videoint.nex"},
    {"HBC-36", "00regression/nex/test10tilemapper.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 6 of 8"); }
