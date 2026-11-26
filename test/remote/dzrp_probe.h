#pragma once

// ---------------------------------------------------------------------------
// The DZRP client probe's logic (GH #511, epic #486; the R0.5 checkpoint).
//
// `dzrp_client_probe` (dzrp_client_probe.cpp) is a TEST-SUPPORT binary: a human
// runs it against a real Next running dezogif_ng (or another jnext) and tees its
// output into doc/testing/REMOTE-TARGET-VALIDATION.md. The logic lives here so
// `dzrp_client_test` runs it over a memory link. The reply decoders it prints
// (registers, read-mem, tbblue) live here too: the probe is their only user for
// now (a product codec comes with the first command that needs one).
// ---------------------------------------------------------------------------

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "remote/client_link.h"
#include "remote/dzrp/dzrp_client.h"

namespace jnext {
namespace remote {
namespace dzrp {

struct ProbeEnv {
    /// Called every turn of every wait (a hook that pumps an in-process server).
    DzrpClient::WaitHook wait_hook;
    /// Default (null) = a `TcpLink` to the URI's host and port.
    std::function<std::unique_ptr<ClientLink>(const LinkSpec&)> make_link;
    ClientConfig base;
};

/// Exit: 0 every step ran; 1 connect / CMD_INIT / a command failed, or the reconnect failed;
/// 2 usage error. `argv[0]` is the program name.
int run_probe(int argc, char** argv, std::FILE* out, const ProbeEnv& env);

/// Test support: run the probe with its output captured in a string (a scratch file in the
/// temp directory, removed afterwards: `tmpfile()` needs the volume root on Windows). `rc` is
/// the probe's exit status, or -1 when no scratch file could be made.
std::string run_probe_captured(int argc, char** argv, const ProbeEnv& env, int& rc);

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
