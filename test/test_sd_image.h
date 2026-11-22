// The SD card the unit suites boot from (GH #555).
//
// run-unit-tests.sh exports JNEXT_TEST_SD_IMAGE (a private clone of the test master) to every
// suite. A suite run by hand, with it unset, reads the test master itself. The test master is
// the suites' OWN card, under ~/.jnext/sdcard/test-suite/: never the user's card, which the
// test harness must not touch (it re-derives its master whenever the distribution or the recipe
// moves, and a user's saved files would go with it).
//
// The same relative path is spelled in test/00regression/test-functions.inc
// (sd_test_state_dir); harness-selftest HS-196 checks that the two agree.
#pragma once

#include <cstdlib>
#include <string>

inline std::string test_sd_image_path()
{
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    if (!(home && *home)) home = std::getenv("USERPROFILE");
    return std::string(home ? home : ".") +
           "/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img";
}
