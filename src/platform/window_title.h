#pragma once
// The emulator window's title, shared by both frontends (Qt MainWindow and the
// SDL window). It carries the version so that a screenshot of the window names
// the build it came from (GH #155). The version is JNEXT_VERSION_STRING, the
// same generated-from-version.yaml string `--version` prints.
#include "version.h"

inline constexpr const char JNEXT_WINDOW_TITLE[] =
    "JNEXT " JNEXT_VERSION_STRING " — ZX Spectrum Next Emulator";
