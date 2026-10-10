// Includes shared by the --sdcard-file-add suites (GH #214): sdcard_file_add_test
// (every OS), sdcard_file_add_posix_test (`# os: posix`) and
// sdcard_file_add_linux_test (`# os: linux`). The fixture helpers are in
// sdcard_file_add_helpers.h, which each suite includes AFTER defining its own
// check()/skip(): the traceability generator reads row descriptions from the
// helper defined in the suite's own source.
#pragma once

#include "core/sdcard_file_add.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace fs = std::filesystem;
using sdcard::FileAddStatus;
