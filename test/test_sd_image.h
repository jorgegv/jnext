// The SD card the unit suites boot from (GH #555).
//
// run-unit-tests.sh exports JNEXT_TEST_SD_IMAGE (a private clone of the test master) to every
// suite. A suite run by hand, with it unset, reads the test master itself. The test master is
// the suites' OWN card, under ~/.jnext/sdcard/test-suite/: never the user's card, which the
// test harness must not touch (it re-derives its master whenever the distribution or the recipe
// moves, and a user's saved files would go with it).
//
// The same relative path is spelled in test/00regression/test-functions.inc
// (sd_test_state_dir); harness-selftest HS-197 checks that the two agree. The card's name follows
// the product's rule (sdcard::card_name_in, GH #579): jnext-sdcard.img when that file exists, else
// cspect-next-1gb-fixed.img when that does, else jnext-sdcard.img; HS-204a pins both names against
// the provisioner's constants.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

inline std::string test_sd_image_path()
{
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    if (!(home && *home)) home = std::getenv("USERPROFILE");
    const std::string dir = std::string(home ? home : ".") + "/.jnext/sdcard/test-suite/sdcard";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dir + "/jnext-sdcard.img", ec) &&
        std::filesystem::is_regular_file(dir + "/cspect-next-1gb-fixed.img", ec))
        return dir + "/cspect-next-1gb-fixed.img";
    return dir + "/jnext-sdcard.img";
}
