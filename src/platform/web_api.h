#pragma once
// The browser build's JavaScript API (GH #465): the options of
// `jnext.mount(container, options)` as ONE table, each bound by enumerator to
// the CLI flag or the --set key it becomes, and the one function that turns an
// options object into the command line `main()` parses. No Emscripten include:
// the native suite web_api_test drives it.
//
// jnext_api.js carries no flag spelling and no --set key. It hands the whole
// options object to build_argv() (through jnext_web_argv()), with three
// substitutions made on its side: every function value becomes `true`, `file`
// becomes its in-memory path, and `image` becomes {path, sha256}.
#include "core/cli_options.h"
#include "platform/config_keys.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace web_api {

enum class Kind : uint8_t {
    Image,      // --sdcard PATH --sdcard-sha256 HEX (opt, opt2)
    Flag,       // one flag with one value (opt)
    SetKey,     // one --set key, boolean valued (key)
    SetMap,     // an object of --set section.key=value
    Argv,       // an array of arguments, appended verbatim
    Host,       // a property of the page, not a jnext setting (not_setting)
    Callback,   // a function the page supplies (not_setting)
};

struct Option {
    const char*                        name;         // the mount() option, as spelled in JS
    Kind                               kind;
    std::optional<cli::OptId>          opt;          // Image, Flag
    std::optional<cli::OptId>          opt2;         // Image: the digest flag
    std::optional<config_keys::KeyId>  key;          // SetKey
    bool                               required;
    const char*                        not_setting;  // Host, Callback: why it is not a jnext setting
    const char*                        help;         // one line
};

inline constexpr Option WEB_OPTIONS[] = {
    { "image", Kind::Image, cli::OptId::Sdcard, cli::OptId::SdcardSha256, std::nullopt, true, nullptr,
      "The SD-card image: {url, sha256}, fetched, checked and cached by the page" },
    { "file", Kind::Flag, cli::OptId::Load, std::nullopt, std::nullopt, false, nullptr,
      "The program to run: a URL, {url}, or {data, name}; absent boots to the NextZXOS menu" },
    { "machine", Kind::Flag, cli::OptId::Machine, std::nullopt, std::nullopt, false, nullptr,
      "The machine to start" },
    { "muted", Kind::SetKey, std::nullopt, std::nullopt, config_keys::KeyId::StartupSilent, false, nullptr,
      "Start with no audio device for the session" },
    { "set", Kind::SetMap, std::nullopt, std::nullopt, std::nullopt, false, nullptr,
      "An object of section.key settings" },
    { "args", Kind::Argv, std::nullopt, std::nullopt, std::nullopt, false, nullptr,
      "Further command-line arguments, appended verbatim" },
    { "width", Kind::Host, std::nullopt, std::nullopt, std::nullopt, false,
      "the box the player fills in the page", "Width of the player: a number (CSS pixels) or a CSS length" },
    { "height", Kind::Host, std::nullopt, std::nullopt, std::nullopt, false,
      "the box the player fills in the page", "Height of the player: a number (CSS pixels) or a CSS length" },
    { "wasmUrl", Kind::Host, std::nullopt, std::nullopt, std::nullopt, false,
      "where the page serves the .wasm file from", "Where the emulator's .wasm file is served from" },
    { "onProgress", Kind::Callback, std::nullopt, std::nullopt, std::nullopt, false,
      "a function of the page", "Called with (phase, fraction) while the image loads or the first boot is recorded" },
    { "onReady", Kind::Callback, std::nullopt, std::nullopt, std::nullopt, false,
      "a function of the page", "Called when the program is in the machine and a frame has been shown" },
    { "onError", Kind::Callback, std::nullopt, std::nullopt, std::nullopt, false,
      "a function of the page", "Called with a message on every failure" },
};

/// The spelling of `id` in cli::OPTIONS (its Doc::Documented row), or nullptr.
const char* flag_spelling(cli::OptId id);
/// "section.key" of `id` in config_keys::KEYS.
std::string key_spelling(config_keys::KeyId id);

/// Turn the options object (JSON text, see above) into the command line, without
/// the program name. Checks names, shapes, types and the required `image` only:
/// every value is validated where it is used, by main(). On a refusal `argv` is
/// left empty and `why` names the option.
bool build_argv(std::string_view options_json, std::vector<std::string>& argv, std::string& why);

/// The master gain in dB for a volume in [0, 1]: 20*log10(v), 0 = kMuteDb.
/// nullopt outside [0, 1] and for NaN.
inline constexpr float kMuteDb = -120.0f;
std::optional<double> volume_to_gain_db(double v);

}  // namespace web_api
