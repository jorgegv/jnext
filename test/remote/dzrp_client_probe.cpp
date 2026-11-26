// dzrp_client_probe: the R0.5 checkpoint tool (GH #511, epic #486). See dzrp_probe.h.
#include <cstdio>

#include "dzrp_probe.h"

int main(int argc, char** argv) {
    return jnext::remote::dzrp::run_probe(argc, argv, stdout, {});
}
