#pragma once
// Run `body` and return what Log::emulator() logged meanwhile (all levels). Lives in its
// own translation unit because including spdlog's sinks makes `fmt` ambiguous in test
// files that define a `fmt()` helper of their own.
#include <functional>
#include <string>

std::string capture_emulator_log(const std::function<void()>& body);
