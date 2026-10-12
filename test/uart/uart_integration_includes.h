// Shared scaffolding of the two UART integration suites (GH #214):
// uart_integration_test (every OS) and uart_posix_test (`# os: posix`). What was
// the part of uart_integration_test.cpp that is not a test row moved here
// verbatim, so the POSIX-only rows can live in a suite of their own without
// copying it. Each suite includes it into its own translation unit, so the
// counters and result lists are per suite.
//
// Two headers, because the traceability generator has to find the row-reporting
// helper (check / skip) DEFINED in the suite's own source to read its row
// descriptions: a .cpp includes the system headers (this file), defines the
// test infrastructure itself, then includes uart_integration_helpers.h.
#pragma once
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/pi_qemu.h"
#include "core/nextpi_provisioner.h"
#include "core/rzx.h"
#include "core/saveable.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/pi_uart_device.h"
#include "peripheral/joy_uart_source.h"
#include "peripheral/uart_device.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <zlib.h>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <process.h>
#endif
#include "../row_id.h"
#include "../test_portable.h"
