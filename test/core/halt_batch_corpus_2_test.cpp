// GH #452 - corpus equivalence, shard 2 of 8 (see halt_batch_corpus.h for the method).
// The corpus is split over eight executables because one process over all 46
// programs exceeds the suite time bound; each shard owns its own programs and IDs.
#include "halt_batch_corpus.h"

namespace {
const Program kPrograms[] = {
    {"HBC-07", "00regression/nex/copper_demo.nex", true},
    {"HBC-08", "00regression/nex/dsl_demo.nex", true},
    {"HBC-09", "00regression/nex/dsl_demo_buggy.nex", true},
    {"HBC-10", "00regression/nex/floating_bus_test.nex"},
    {"HBC-11", "00regression/nex/joy_uart_demo.nex"},
    {"HBC-12", "00regression/nex/layer2_320x256_test.nex"},
};
} // namespace

int main() { return run_corpus(kPrograms, static_cast<int>(sizeof kPrograms / sizeof kPrograms[0]), "shard 2 of 8"); }
