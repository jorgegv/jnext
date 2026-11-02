#pragma once

// GH #422 stage 2: the colour scheme of the debugger's Source tab.
//
// Header-only and Qt-free so the settings code (config_keys, AppConfig) and the Qt
// highlighter name one type. The order is the config words' order,
// `source.colour_scheme = auto|light|dark|borland`; app_config.cpp and
// debugger/source_highlighter.cpp static_assert it against config_keys.

#include <cstdint>

enum class SourceScheme : uint8_t { Auto, Light, Dark, Borland };
